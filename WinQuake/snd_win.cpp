/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// snd_win.cpp -- WASAPI shared-mode sound output.
//
// Replaces the original DirectSound/waveOut dual-path implementation.
// WASAPI has shipped since Vista and covers everything both legacy paths
// did, so there's no fallback path kept here (unlike XInput/legacy
// joystick, which cover genuinely different hardware).
//
// Quake's mixer (snd_mix.cpp/snd_dma.cpp) paints stereo 16-bit samples
// directly into a persistent, power-of-two-sized circular "DMA" buffer
// (shm->buffer) exactly like it always has -- that part is completely
// unchanged. This file's only job is to own a shadow copy of that ring
// (sized independently of WASAPI's own internal buffer) and periodically
// copy the newly-painted range into WASAPI's render buffer, plus report
// back an emulated hardware "read cursor" so the mixer knows how far
// ahead of playback it's safe to paint.

#include "quakedef.h"
#include "snd_internal.h"
#include "winquake.h"

#include <mmdeviceapi.h>
#include <audioclient.h>

static IMMDeviceEnumerator		*pEnumerator;
static IMMDevice				*pDevice;
static IAudioClient			*pAudioClient;
static IAudioRenderClient		*pRenderClient;

static qboolean	wasapi_active;
static qboolean	wasapi_client_initialized;	// true once IAudioClient::Initialize succeeded -- Stop() is undefined before that
static UINT32	wasapi_buffer_frames;		// size of WASAPI's own internal ring
static UINT64	wasapi_submitted_frames;	// monotonic count of frames handed to WASAPI so far

// generous buffer duration (100ns units) since we poll once per game frame
// rather than from a dedicated real-time audio thread -- matches the
// safety margin the original ~1.5-second DirectSound secondary buffer gave
#define WASAPI_BUFFER_DURATION	10000000LL	// 1 second


/*
==================
SNDDMA_NegotiateFormat

Always requests 16-bit stereo PCM -- Quake's mixer and stereo
spatialization are hardcoded for 2 channels, and the shared-mode audio
engine transparently handles channel-count conversion for us, so there's
no reason to ever adopt a device's suggested channel count. Only the
sample rate is negotiated: try the classic 11025Hz first, then the
device's suggested rate, then its shared-mode mix format's rate.

Note: IsFormatSupported's ppClosestMatch parameter is documented as
optional (NULL-able) in shared mode, but at least one real driver in the
wild returns E_POINTER if it's actually NULL -- so every call here always
passes a real out-pointer and frees whatever comes back, even when the
suggestion itself isn't used.
==================
*/
static qboolean SNDDMA_NegotiateFormat (WAVEFORMATEX *wfx)
{
	HRESULT			hr;
	WAVEFORMATEX	*closest;
	int				suggestedRate = 0;

	memset (wfx, 0, sizeof(*wfx));
	wfx->wFormatTag = WAVE_FORMAT_PCM;
	wfx->nChannels = 2;
	wfx->wBitsPerSample = 16;
	wfx->nSamplesPerSec = 11025;
	wfx->nBlockAlign = wfx->nChannels * wfx->wBitsPerSample / 8;
	wfx->nAvgBytesPerSec = wfx->nSamplesPerSec * wfx->nBlockAlign;

	closest = NULL;
	hr = pAudioClient->IsFormatSupported (AUDCLNT_SHAREMODE_SHARED, wfx, &closest);
	if (hr == S_OK)
	{
		if (closest)
			CoTaskMemFree (closest);
		return true;
	}

	if (hr == S_FALSE && closest)
		suggestedRate = closest->nSamplesPerSec;
	if (closest)
		CoTaskMemFree (closest);

	if (!suggestedRate)
	{
		WAVEFORMATEX *mixfmt = NULL;
		if (SUCCEEDED (pAudioClient->GetMixFormat (&mixfmt)) && mixfmt)
		{
			suggestedRate = mixfmt->nSamplesPerSec;
			CoTaskMemFree (mixfmt);
		}
	}

	if (!suggestedRate)
		return false;

	wfx->nSamplesPerSec = suggestedRate;
	wfx->nAvgBytesPerSec = wfx->nSamplesPerSec * wfx->nBlockAlign;

	closest = NULL;
	hr = pAudioClient->IsFormatSupported (AUDCLNT_SHAREMODE_SHARED, wfx, &closest);
	if (closest)
		CoTaskMemFree (closest);

	return hr == S_OK;
}


/*
==================
SNDDMA_Shutdown

Reset the sound device for exiting
==================
*/
void SNDDMA_Shutdown (void)
{
	if (pAudioClient && wasapi_client_initialized)
		pAudioClient->Stop ();

	if (pRenderClient) { pRenderClient->Release (); pRenderClient = NULL; }
	if (pAudioClient)  { pAudioClient->Release ();  pAudioClient = NULL; }
	if (pDevice)       { pDevice->Release ();       pDevice = NULL; }
	if (pEnumerator)   { pEnumerator->Release ();   pEnumerator = NULL; }

	wasapi_client_initialized = false;
	wasapi_active = false;
}


/*
==================
SNDDMA_InitWASAPI
==================
*/
static qboolean SNDDMA_InitWASAPI (void)
{
	HRESULT			hr;
	WAVEFORMATEX	wfx;
	BYTE			*pData;
	int				bytesPerFrame;
	int				shadowFrames;

	hr = CoCreateInstance (__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
							__uuidof(IMMDeviceEnumerator), (void **)&pEnumerator);
	if (FAILED (hr))
	{
		Con_SafePrintf ("WASAPI: CoCreateInstance(MMDeviceEnumerator) failed\n");
		return false;
	}

	hr = pEnumerator->GetDefaultAudioEndpoint (eRender, eConsole, &pDevice);
	if (FAILED (hr))
	{
		Con_SafePrintf ("WASAPI: no default audio render endpoint\n");
		return false;
	}

	hr = pDevice->Activate (__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void **)&pAudioClient);
	if (FAILED (hr))
	{
		Con_SafePrintf ("WASAPI: IAudioClient activation failed\n");
		return false;
	}

	if (!SNDDMA_NegotiateFormat (&wfx))
	{
		Con_SafePrintf ("WASAPI: no supported audio format found\n");
		return false;
	}

	hr = pAudioClient->Initialize (AUDCLNT_SHAREMODE_SHARED, 0, WASAPI_BUFFER_DURATION, 0, &wfx, NULL);
	if (FAILED (hr))
	{
		Con_SafePrintf ("WASAPI: IAudioClient::Initialize failed (hr=0x%x)\n", (unsigned)hr);
		return false;
	}
	wasapi_client_initialized = true;

	hr = pAudioClient->GetBufferSize (&wasapi_buffer_frames);
	if (FAILED (hr))
	{
		Con_SafePrintf ("WASAPI: GetBufferSize failed\n");
		return false;
	}

	hr = pAudioClient->GetService (__uuidof(IAudioRenderClient), (void **)&pRenderClient);
	if (FAILED (hr))
	{
		Con_SafePrintf ("WASAPI: GetService(IAudioRenderClient) failed\n");
		return false;
	}

	shm = &sn;
	shm->channels = wfx.nChannels;
	shm->samplebits = wfx.wBitsPerSample;
	shm->speed = wfx.nSamplesPerSec;
	shm->soundalive = true;
	shm->splitbuffer = false;
	shm->submission_chunk = 1;
	shm->samplepos = 0;

	// shadow ring buffer the mixer paints into directly -- sized a
	// power of two (required by the mixer's "& (samples-1)" wrap math)
	// and comfortably larger than WASAPI's own internal buffer so
	// per-frame polling never has to fight for room to paint ahead
	bytesPerFrame = shm->channels * (shm->samplebits / 8);
	shadowFrames = 1;
	while (shadowFrames < (int)wasapi_buffer_frames * 2)
		shadowFrames <<= 1;

	shm->samples = shadowFrames * shm->channels;
	shm->buffer = (unsigned char *)Hunk_AllocName (shadowFrames * bytesPerFrame, "shmbuf");

	// prime WASAPI's buffer with silence and start the stream
	hr = pRenderClient->GetBuffer (wasapi_buffer_frames, &pData);
	if (SUCCEEDED (hr))
		pRenderClient->ReleaseBuffer (wasapi_buffer_frames, AUDCLNT_BUFFERFLAGS_SILENT);

	wasapi_submitted_frames = wasapi_buffer_frames;

	hr = pAudioClient->Start ();
	if (FAILED (hr))
	{
		Con_SafePrintf ("WASAPI: IAudioClient::Start failed\n");
		return false;
	}

	wasapi_active = true;
	return true;
}


/*
==================
SNDDMA_Init

Try to find a sound device to mix for.
Returns false if nothing is found.
==================
*/
int SNDDMA_Init (void)
{
	if (!SNDDMA_InitWASAPI ())
	{
		SNDDMA_Shutdown ();
		return 0;
	}

	Con_SafePrintf ("WASAPI sound initialized\n");
	Con_SafePrintf ("   %d channel(s)\n"
	                 "   %d bits/sample\n"
	                 "   %d samples/sec\n",
	                 shm->channels, shm->samplebits, shm->speed);

	return 1;
}


/*
==============
SNDDMA_GetDMAPos

Return the current emulated hardware read position (in interleaved
samples, matching shm->samples' units) so the mixer knows how far ahead
of actual playback it's safe to paint. Derived from how many frames
we've handed to WASAPI so far minus how many are still queued and unplayed
(IAudioClient::GetCurrentPadding) -- the WASAPI equivalent of DirectSound's
GetCurrentPosition play cursor.
===============
*/
int SNDDMA_GetDMAPos (void)
{
	UINT32	padding;
	UINT64	playedFrames;

	if (!wasapi_active)
		return 0;

	padding = 0;
	if (FAILED (pAudioClient->GetCurrentPadding (&padding)))
		padding = 0;

	playedFrames = wasapi_submitted_frames - padding;

	return (int)((playedFrames * shm->channels) % (UINT64)shm->samples);
}


/*
==============
SNDDMA_Submit

Copy newly-painted samples (sound.paintedtime is how far the mixer has
painted, in mono frames) from the shadow ring buffer into however much
room WASAPI currently has free.
===============
*/
void SNDDMA_Submit (void)
{
	UINT32	padding, framesFree, framesToWrite;
	UINT64	targetFrames;
	BYTE	*pData;
	int		bytesPerFrame, shadowFrameCount, startFrame, firstChunk;

	if (!wasapi_active)
		return;

	targetFrames = (UINT64)sound.paintedtime;
	if (targetFrames <= wasapi_submitted_frames)
		return;

	if (FAILED (pAudioClient->GetCurrentPadding (&padding)))
		return;

	framesFree = wasapi_buffer_frames - padding;
	framesToWrite = (UINT32)(targetFrames - wasapi_submitted_frames);
	if (framesToWrite > framesFree)
		framesToWrite = framesFree;
	if (framesToWrite == 0)
		return;

	if (FAILED (pRenderClient->GetBuffer (framesToWrite, &pData)))
		return;

	bytesPerFrame = shm->channels * (shm->samplebits / 8);
	shadowFrameCount = shm->samples / shm->channels;
	startFrame = (int)(wasapi_submitted_frames % (UINT64)shadowFrameCount);

	firstChunk = shadowFrameCount - startFrame;
	if (firstChunk > (int)framesToWrite)
		firstChunk = framesToWrite;

	memcpy (pData, shm->buffer + (size_t)startFrame * bytesPerFrame, (size_t)firstChunk * bytesPerFrame);

	if ((int)framesToWrite > firstChunk)
	{
		memcpy (pData + (size_t)firstChunk * bytesPerFrame, shm->buffer,
				(size_t)((int)framesToWrite - firstChunk) * bytesPerFrame);
	}

	pRenderClient->ReleaseBuffer (framesToWrite, 0);
	wasapi_submitted_frames += framesToWrite;
}


/*
==================
S_BlockSound / S_UnblockSound

Nothing to do -- like DirectSound, WASAPI's shared-mode ring buffer just
keeps playing (and naturally runs down to silence once drained) while
S_Update stops submitting new samples. Kept only for the sound.blocked
reference-count bookkeeping S_Update already relies on.
==================
*/
void S_BlockSound (void)
{
	sound.blocked++;
}

void S_UnblockSound (void)
{
	sound.blocked--;
}
