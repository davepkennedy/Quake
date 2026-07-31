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
// winquake.h: Win32-specific Quake header file
#pragma once

#include <windows.h>
#define WM_MOUSEWHEEL 0x020A

extern HINSTANCE global_hInstance;
extern int global_nCmdShow;

extern HWND mainwindow;
extern qboolean ActiveApp, Minimized;

// startup splash dialog: created in sys_win.cpp, torn down by Video once
// the real GL window is up. Exposed as a one-shot action rather than the
// raw HWND.
void Sys_CloseSplashDialog(void);

// window geometry, owned and computed by Video (gl_vidnt.cpp) but also
// needed by in_win.cpp's cursor-warp mouse-look fallback. RECT-typed, so
// these live here (the Win32-specific header) rather than in vid.h.
void VID_GetWindowCenter(int *x, int *y);
const RECT *VID_GetWindowRect(void);

// Video's cvar, declared here rather than vid.h -- vid.h is included (via
// quakedef.h) before cvar.h, so cvar_t isn't a known type there yet
extern cvar_t _windowed_mouse;
