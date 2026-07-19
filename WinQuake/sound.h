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
// sound.h -- public interface to the sound subsystem. Everything the
// mixer/DMA layer needs internally (dma_t, channel_t, the channel pool,
// sound_state_t, ...) lives in snd_internal.h instead, included only by
// snd_dma.cpp/snd_mem.cpp/snd_mix.cpp/snd_win.cpp -- nothing outside those
// files needs it, confirmed via a full-codebase grep before the split.
#pragma once

#include "qlimits.h"	// MAX_QPATH
#include "common.h"	// qboolean
#include "zone.h"	// cache_user_t
#include "mathlib.h"	// vec3_t
#include "cvar.h"	// cvar_t

#ifndef __SOUND__
#define __SOUND__

#define DEFAULT_SOUND_PACKET_VOLUME 255
#define DEFAULT_SOUND_PACKET_ATTENUATION 1.0

typedef struct sfx_s
{
	char 	name[MAX_QPATH];
	cache_user_t	cache;
} sfx_t;

void S_Init (void);
void S_Shutdown (void);
void S_StartSound (int entnum, int entchannel, sfx_t *sfx, vec3_t origin, float fvol,  float attenuation);
void S_StaticSound (sfx_t *sfx, vec3_t origin, float vol, float attenuation);
void S_StopSound (int entnum, int entchannel);
void S_StopAllSounds(qboolean clear);
void S_ClearBuffer (void);
void S_Update (vec3_t origin, vec3_t v_forward, vec3_t v_right, vec3_t v_up);
void S_ExtraUpdate (void);

sfx_t *S_PrecacheSound (const char *sample);
void S_TouchSound (const char *sample);
void S_BeginPrecaching (void);
void S_EndPrecaching (void);

void S_LocalSound (const char *s);

void S_BlockSound (void);
void S_UnblockSound (void);

extern	cvar_t loadas8bit;
extern	cvar_t bgmvolume;
extern	cvar_t volume;

#endif
