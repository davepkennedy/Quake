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
// input.h -- external (non-keyboard) input devices
#pragma once

#include "client.h" // usercmd_t

void IN_Init(void);

void IN_Shutdown(void);

void IN_Commands(void);
// oportunity for devices to stick commands on the script buffer

void IN_Move(usercmd_t *cmd);
// add additional movement on top of the keyboard move cmd

void IN_ClearStates(void);
// restores all button and position states to defaults

void IN_Accumulate(void);

// mouse show/hide/capture state, driven by focus and menu/console changes
// elsewhere in the app -- these mutate in_win.cpp's own internal mouse
// state, which nothing outside this file touches directly
void IN_ShowMouse(void);
void IN_HideMouse(void);
void IN_ActivateMouse(void);
void IN_DeactivateMouse(void);
void IN_RestoreOriginalMouseState(void);
void IN_SetQuakeMouseState(void);
void IN_MouseEvent(int mstate);
void IN_RawMouseMoved(int dx, int dy);
void IN_UpdateClipCursor(void);
