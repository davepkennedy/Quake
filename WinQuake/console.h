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

#include "common.h" // qboolean
#include <format>
#include <memory_resource>
#include <string>
#include <utility>
#include <vector>

//
// console
//
#define NUM_CON_TIMES 4

struct console_state_t
{
    int linewidth; // current width of the console, in characters
    float cursorspeed = 4;

    qboolean forcedup; // because no entities to refresh

    int totallines; // total lines in console scrollback
    int backscroll; // lines up from bottom to display
    int current;    // where next message will be printed
    int x;          // offset in current line for next print
    std::pmr::vector<char> text;

    float times[NUM_CON_TIMES]; // realtime time the line was generated
                                // for transparent notify lines

    int vislines;

    qboolean debuglog;

    qboolean initialized;

    int notifylines; // scan lines to clear for notify lines
};

extern console_state_t con;

void Con_DrawCharacter(int cx, int line, int num);

void Con_CheckResize(void);
void Con_Init(void);
void Con_DrawConsole(int lines, qboolean drawinput);
void Con_Print(const char *txt);
void Con_Printf(const char *fmt, ...);
void Con_DPrintf(const char *fmt, ...);
// Sends already-formatted text; defined in console.cpp.
void Con_SafePrintfImpl(const std::string &msg);

template <typename... Args> void Con_SafePrintf(std::format_string<Args...> fmt, Args &&...args)
{
    Con_SafePrintfImpl(std::format(fmt, std::forward<Args>(args)...));
}
void Con_Clear_f(void);
void Con_DrawNotify(void);
void Con_ClearNotify(void);
void Con_ToggleConsole_f(void);

void Con_NotifyBox(const char *text); // during startup for sound / cd warnings
