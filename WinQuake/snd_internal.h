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
// snd_internal.h -- private to the sound subsystem's own translation units
// (snd_dma.cpp, snd_mem.cpp, snd_mix.cpp, snd_win.cpp). Nothing outside
// those files should include this or reach into the state it declares --
// sound.h is the actual public interface. Confirmed via a full-codebase
// grep before this split that nothing outside those 4 files touched any
// of this, so this header is closing a gap, not fixing a live violation.

#ifndef __SND_INTERNAL__
#define __SND_INTERNAL__

// !!! if this is changed, it much be changed in asm_i386.h too !!!
typedef struct
{
	int left;
	int right;
} portable_samplepair_t;

// !!! if this is changed, it much be changed in asm_i386.h too !!!
typedef struct
{
	int 	length;
	int 	loopstart;
	int 	speed;
	int 	width;
	int 	stereo;
	byte	data[1];		// variable sized
} sfxcache_t;

typedef struct
{
	qboolean		gamealive;
	qboolean		soundalive;
	qboolean		splitbuffer;
	int				channels;
	int				samples;				// mono samples in buffer
	int				submission_chunk;		// don't mix less than this #
	int				samplepos;				// in mono samples
	int				samplebits;
	int				speed;
	unsigned char	*buffer;
} dma_t;

// !!! if this is changed, it much be changed in asm_i386.h too !!!
typedef struct
{
	sfx_t	*sfx;			// sfx number
	int		leftvol;		// 0-255 volume
	int		rightvol;		// 0-255 volume
	int		end;			// end time in global paintsamples
	int 	pos;			// sample position in sfx
	int		looping;		// where to loop, -1 = no looping
	int		entnum;			// to allow overriding a specific sound
	int		entchannel;		//
	vec3_t	origin;			// origin of sound effect
	vec_t	dist_mult;		// distance multiplier (attenuation/clipK)
	int		master_vol;		// 0-255 master volume
} channel_t;

typedef struct
{
	int		rate;
	int		width;
	int		channels;
	int		loopstart;
	int		samples;
	int		dataofs;		// chunk starts this many bytes from file start
} wavinfo_t;

#define	MAX_CHANNELS			128
#define	MAX_DYNAMIC_CHANNELS	8

extern	channel_t   channels[MAX_CHANNELS];
// 0 to MAX_DYNAMIC_CHANNELS-1	= normal entity sounds
// MAX_DYNAMIC_CHANNELS to MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS -1 = water, etc
// MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS to total_channels = static sounds

extern volatile dma_t *shm;
extern volatile dma_t sn;

// Mixer/subsystem state -- everything sound-related that isn't the DMA
// device (shm/sn above) or the channel pool (channels[] above), both of
// which are already a single canonical instance and heavily dereferenced
// in the mixer's hot path, so left as standalone globals rather than
// folded in here too.
struct sound_state_t
{
	int			total_channels;

	int			blocked = 0;
	qboolean	ambient_enabled = true;
	qboolean	initialized = false;

	vec3_t		listener_origin;
	vec3_t		listener_forward;
	vec3_t		listener_right;
	vec3_t		listener_up;
	vec_t		nominal_clip_dist = 1000.0;

	int			time;			// sample PAIRS
	int			paintedtime;	// sample PAIRS

	sfx_t		*known_sfx;		// hunk allocated [MAX_SFX]
	int			num_sfx;

	sfx_t		*ambient_sfx[NUM_AMBIENTS];

	int			desired_speed = 11025;
	int			desired_bits = 16;

	int			started = 0;

	// Fake dma is a synchronous faking of the DMA progress used for
	// isolating performance in the renderer. fakedma_updates is the
	// number of times S_Update() is called per second.
	qboolean	fakedma = false;
	int			fakedma_updates = 15;
};

extern sound_state_t sound;

void S_Startup (void);
void S_ClearPrecache (void);
void S_PaintChannels(int endtime);
void S_InitPaintChannels (void);

// picks a channel based on priorities, empty slots, number of channels
channel_t *SND_PickChannel(int entnum, int entchannel);

// spatializes a channel
void SND_Spatialize(channel_t *ch);

// initializes cycling through a DMA buffer and returns information on it
int SNDDMA_Init(void);

// gets the current DMA position
int SNDDMA_GetDMAPos(void);

// shutdown the DMA xfer.
void SNDDMA_Shutdown(void);

sfxcache_t *S_LoadSound (sfx_t *s);

wavinfo_t GetWavinfo (const char *name, byte *wav, int wavlength);

void SND_InitScaletable (void);
void SNDDMA_Submit(void);

void S_AmbientOff (void);
void S_AmbientOn (void);

#endif
