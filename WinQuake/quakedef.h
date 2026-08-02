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
// quakedef.h -- primary header for client
#pragma once

// #define	GLTEST			// experimental stuff

#define QUAKE_GAME // as opposed to utilities

#define VERSION 1.09
#define GLQUAKE_VERSION 1.00
#define D3DQUAKE_VERSION 0.01
#define WINQUAKE_VERSION 0.996
#define LINUX_VERSION 1.30
#define X11_VERSION 1.10

// define	PARANOID			// speed sapping error checking

#define GAMENAME "id1"

#include <cstddef>
#include <cmath>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#if defined(_WIN32) && !defined(WINDED)

#if defined(_M_IX86)
#define __i386__ 1
#endif

void VID_LockBuffer(void);
void VID_UnlockBuffer(void);

#else

#define VID_LockBuffer()
#define VID_UnlockBuffer()

#endif

#if defined __i386__ // && !defined __sun__
#define id386 1
#else
#define id386 0
#endif

#if id386
#define UNALIGNED_OK 1 // set to 0 if unaligned accesses are not supported
#else
#define UNALIGNED_OK 0
#endif

// !!! if this is changed, it must be changed in d_ifacea.h too !!!
#define CACHE_SIZE 32 // used to align key data structures

#define UNUSED(x) (x = x) // for pesky compiler / lint warnings

#define MINIMUM_MEMORY 0x550000
#define MINIMUM_MEMORY_LEVELPAK (MINIMUM_MEMORY + 0x100000)

#define MAX_NUM_ARGVS 50

// up / down
#define PITCH 0

// left / right
#define YAW 1

// fall over
#define ROLL 2

// This makes anyone on id's net privileged
// Use for multiplayer testing only - VERY dangerous!!!
// #define IDGODS

#include "qlimits.h"
#include "common.h"
#include "bspfile.h"
#include "vid.h"
#include "sys.h"
#include "zone.h"
#include "mathlib.h"
#include "entity_state.h"

#include "wad.h"
#include "draw.h"
#include "cvar.h"
#include "screen.h"
#include "net.h"
#include "protocol.h"
#include "cmd.h"
#include "sbar.h"
#include "sound.h"
#include "render.h"
#include "client.h"
#include "progs.h"
#include "server.h"

#include "gl_model.h"

#include "input.h"
#include "world.h"
#include "keys.h"
#include "console.h"
#include "view.h"
#include "menu.h"
#include "crc.h"
#include "cdaudio.h"

#ifdef GLQUAKE
#include "glquake.h"
#endif

//=============================================================================

// the host system specifies the base of the directory tree, the
// command line parms passed to the program, and the amount of memory
// available for the program to use

struct quakeparms_t
{
    char *basedir;
    char *cachedir; // for development over ISDN lines
    int argc;
    const char **argv;
    void *membase;
    size_t memsize;
};

//=============================================================================

extern qboolean noclip_anglehack;

//
// host
//
extern quakeparms_t host_parms;

extern cvar_t sys_ticrate;
extern cvar_t sys_nostdout;
extern cvar_t developer;

extern qboolean host_initialized; // true if into command execution
extern double host_frametime;
extern byte *host_basepal;
extern byte *host_colormap;
extern int host_framecount; // incremented every frame, never reset
extern double realtime;     // not bounded in any way, changed at
                            // start of every frame, never reset

void Host_ClearMemory(void);
void Host_ServerFrame(void);
void Host_InitCommands(void);
void Host_Init(quakeparms_t *parms);
void Host_Shutdown(void);
void Host_Error(const char *error, ...);
void Host_EndGame(const char *message, ...);
void Host_Frame(float time);
void Host_Quit_f(void);
void Host_ClientCommands(const char *fmt, ...);
void Host_ShutdownServer(qboolean crash);

extern qboolean msg_suppress_1; // suppresses resolution and cache size console output
                                //  an fullscreen DIB focus gain/loss
extern int current_skill;       // skill level for currently loaded level (in case
                                //  the user changes the cvar while the level is
                                //  running, this reflects the level actually in use)

extern qboolean isDedicated;

extern int minimum_memory;

//
// chase
//
extern cvar_t chase_active;

void Chase_Init(void);
void Chase_Reset(void);
void Chase_Update(void);
