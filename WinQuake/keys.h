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
#pragma once

#include <string>
#include <fstream>

#include "common.h" // qboolean

//
// these are the key numbers that should be passed to Key_Event
//
constexpr int K_TAB = 9;
constexpr int K_ENTER = 13;
constexpr int K_ESCAPE = 27;
constexpr int K_SPACE = 32;

// normal keys should be passed as lowercased ascii

constexpr int K_BACKSPACE = 127;
constexpr int K_UPARROW = 128;
constexpr int K_DOWNARROW = 129;
constexpr int K_LEFTARROW = 130;
constexpr int K_RIGHTARROW = 131;

constexpr int K_ALT = 132;
constexpr int K_CTRL = 133;
constexpr int K_SHIFT = 134;
constexpr int K_F1 = 135;
constexpr int K_F2 = 136;
constexpr int K_F3 = 137;
constexpr int K_F4 = 138;
constexpr int K_F5 = 139;
constexpr int K_F6 = 140;
constexpr int K_F7 = 141;
constexpr int K_F8 = 142;
constexpr int K_F9 = 143;
constexpr int K_F10 = 144;
constexpr int K_F11 = 145;
constexpr int K_F12 = 146;
constexpr int K_INS = 147;
constexpr int K_DEL = 148;
constexpr int K_PGDN = 149;
constexpr int K_PGUP = 150;
constexpr int K_HOME = 151;
constexpr int K_END = 152;

constexpr int K_PAUSE = 255;

//
// mouse buttons generate virtual keys
//
constexpr int K_MOUSE1 = 200;
constexpr int K_MOUSE2 = 201;
constexpr int K_MOUSE3 = 202;

//
// joystick buttons
//
constexpr int K_JOY1 = 203;
constexpr int K_JOY2 = 204;
constexpr int K_JOY3 = 205;
constexpr int K_JOY4 = 206;

//
// aux keys are for multi-buttoned joysticks to generate so they can use
// the normal binding process
//
constexpr int K_AUX1 = 207;
constexpr int K_AUX2 = 208;
constexpr int K_AUX3 = 209;
constexpr int K_AUX4 = 210;
constexpr int K_AUX5 = 211;
constexpr int K_AUX6 = 212;
constexpr int K_AUX7 = 213;
constexpr int K_AUX8 = 214;
constexpr int K_AUX9 = 215;
constexpr int K_AUX10 = 216;
constexpr int K_AUX11 = 217;
constexpr int K_AUX12 = 218;
constexpr int K_AUX13 = 219;
constexpr int K_AUX14 = 220;
constexpr int K_AUX15 = 221;
constexpr int K_AUX16 = 222;
constexpr int K_AUX17 = 223;
constexpr int K_AUX18 = 224;
constexpr int K_AUX19 = 225;
constexpr int K_AUX20 = 226;
constexpr int K_AUX21 = 227;
constexpr int K_AUX22 = 228;
constexpr int K_AUX23 = 229;
constexpr int K_AUX24 = 230;
constexpr int K_AUX25 = 231;
constexpr int K_AUX26 = 232;
constexpr int K_AUX27 = 233;
constexpr int K_AUX28 = 234;
constexpr int K_AUX29 = 235;
constexpr int K_AUX30 = 236;
constexpr int K_AUX31 = 237;
constexpr int K_AUX32 = 238;

// JACK: Intellimouse(c) Mouse Wheel Support

constexpr int K_MWHEELUP = 239;
constexpr int K_MWHEELDOWN = 240;

// XInput (Xbox-style) controller buttons -- the d-pad reuses K_AUX29-32
// (the existing legacy-joystick POV-hat slots) since XInput and the old
// joyGetPosEx joystick path are mutually exclusive (XInput preferred when
// a controller is present), so there's no risk of collision.
constexpr int K_XBOX_A = 241;
constexpr int K_XBOX_B = 242;
constexpr int K_XBOX_X = 243;
constexpr int K_XBOX_Y = 244;
constexpr int K_XBOX_LSHOULDER = 245;
constexpr int K_XBOX_RSHOULDER = 246;
constexpr int K_XBOX_LTRIGGER = 247;
constexpr int K_XBOX_RTRIGGER = 248;
constexpr int K_XBOX_LTHUMB = 249;
constexpr int K_XBOX_RTHUMB = 250;
constexpr int K_XBOX_START = 251;
constexpr int K_XBOX_BACK = 252;

enum class keydest_t
{
    key_game,
    key_console,
    key_message,
    key_menu
};

// widely-polled UI-focus routing flag -- read/written from ~10 files across
// client, server, and video code as ordinary coordination state, not an
// implementation detail of Input worth hiding behind an accessor
extern keydest_t key_dest;

void Key_Event(int key, qboolean down);
void Key_Init(void);
void Key_WriteBindings(std::ofstream &f);
void Key_SetBinding(int keynum, const char *binding);
std::string Key_KeynumToString(int keynum);
void Key_ClearStates(void);

// keybindings[] queries used by the Keys menu (bind display/edit, unbind)
void Key_KeysForCommand(const char *command, int *twokeys);
void Key_UnbindCommand(const char *command);

// "press a key to continue" primitives used by console notify boxes
// (Con_NotifyBox) and modal screen messages (SCR_ModalMessage)
void Key_ArmForKeyDownUpWait(void);
bool Key_IsArmedForKeyWait(void);
void Key_ArmForSingleKeyWait(void);
int Key_LastKeyPressed(void);
