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
// console.c

#ifdef NeXT
#include <libc.h>
#endif
#ifndef _MSC_VER
#include <unistd.h>
#else
#include <io.h>
#endif
#include <fcntl.h>
#include <string>
#include "quakedef.h"
#include "hunk_resource.h"

console_state_t con;

#define CON_TEXTSIZE 16384

cvar_t con_notifytime = {"con_notifytime", "3"}; // seconds

#define MAXCMDLINE 256
extern char key_lines[32][MAXCMDLINE];
extern int edit_line;
extern int key_linepos;

extern void M_Menu_Main_f(void);

/*
================
Con_ToggleConsole_f
================
*/
void Con_ToggleConsole_f(void)
{
    if (key_dest == keydest_t::key_console)
    {
        if (cls.state == cactive_t::ca_connected)
        {
            key_dest = keydest_t::key_game;
            key_lines[edit_line][1] = 0; // clear any typing
            key_linepos = 1;
        }
        else
        {
            M_Menu_Main_f();
        }
    }
    else
    {
        key_dest = keydest_t::key_console;
    }

    SCR_EndLoadingPlaque();
    memset(con.times, 0, sizeof(con.times));
}

/*
================
Con_Clear_f
================
*/
void Con_Clear_f(void)
{
    if (!con.text.empty())
    {
        Q_memset(con.text.data(), ' ', CON_TEXTSIZE);
    }
}

/*
================
Con_ClearNotify
================
*/
void Con_ClearNotify(void)
{
    int i;

    for (i = 0; i < NUM_CON_TIMES; i++)
    {
        con.times[i] = 0;
    }
}

/*
================
Con_MessageMode_f
================
*/
extern qboolean team_message;

void Con_MessageMode_f(void)
{
    key_dest = keydest_t::key_message;
    team_message = false;
}

/*
================
Con_MessageMode2_f
================
*/
void Con_MessageMode2_f(void)
{
    key_dest = keydest_t::key_message;
    team_message = true;
}

/*
================
Con_CheckResize

If the line width has changed, reformat the buffer.
================
*/
void Con_CheckResize(void)
{
    int i, j, width, oldwidth, oldtotallines, numlines, numchars;
    char tbuf[CON_TEXTSIZE];

    width = (vid.width >> 3) - 2;

    if (width == con.linewidth)
    {
        return;
    }

    if (width < 1) // video hasn't been initialized yet
    {
        width = 38;
        con.linewidth = width;
        con.totallines = CON_TEXTSIZE / con.linewidth;
        Q_memset(con.text.data(), ' ', CON_TEXTSIZE);
    }
    else
    {
        oldwidth = con.linewidth;
        con.linewidth = width;
        oldtotallines = con.totallines;
        con.totallines = CON_TEXTSIZE / con.linewidth;
        numlines = oldtotallines;

        if (con.totallines < numlines)
        {
            numlines = con.totallines;
        }

        numchars = oldwidth;

        if (con.linewidth < numchars)
        {
            numchars = con.linewidth;
        }

        Q_memcpy(tbuf, con.text.data(), CON_TEXTSIZE);
        Q_memset(con.text.data(), ' ', CON_TEXTSIZE);

        for (i = 0; i < numlines; i++)
        {
            for (j = 0; j < numchars; j++)
            {
                con.text[(con.totallines - 1 - i) * con.linewidth + j] =
                    tbuf[((con.current - i + oldtotallines) % oldtotallines) * oldwidth + j];
            }
        }

        Con_ClearNotify();
    }

    con.backscroll = 0;
    con.current = con.totallines - 1;
}

/*
================
Con_Init
================
*/
void Con_Init(void)
{
#define MAXGAMEDIRLEN 1000
    char temp[MAXGAMEDIRLEN + 1];
    const char *t2 = "/qconsole.log";

    con.debuglog = COM_CheckParm("-condebug");

    if (con.debuglog)
    {
        if (strlen(com_gamedir) < (MAXGAMEDIRLEN - strlen(t2)))
        {
            auto result = std::format_to_n(temp, sizeof(temp) - 1, "{}{}", com_gamedir, t2);
            *result.out = '\0';
            _unlink(temp);
        }
    }

    con.text = std::pmr::vector<char>(CON_TEXTSIZE, ' ', Hunk_GetResource());
    con.linewidth = -1;
    Con_CheckResize();

    Con_Printf("Console initialized.\n");

    //
    // register our commands
    //
    Cvar_RegisterVariable(&con_notifytime);

    Cmd_AddCommand("toggleconsole", Con_ToggleConsole_f);
    Cmd_AddCommand("messagemode", Con_MessageMode_f);
    Cmd_AddCommand("messagemode2", Con_MessageMode2_f);
    Cmd_AddCommand("clear", Con_Clear_f);
    con.initialized = true;
}

/*
===============
Con_Linefeed
===============
*/
void Con_Linefeed(void)
{
    con.x = 0;
    con.current++;
    Q_memset(&con.text[(con.current % con.totallines) * con.linewidth], ' ', con.linewidth);
}

/*
================
Con_Print

Handles cursor positioning, line wrapping, etc
All console printing must go through this in order to be logged to disk
If no console is visible, the notify window will pop up.
================
*/
void Con_Print(const char *txt)
{
    int y;
    int c, l;
    static int cr;
    int mask;

    con.backscroll = 0;

    if (txt[0] == 1)
    {
        mask = 128; // go to colored text
        S_LocalSound("misc/talk.wav");
        // play talk wav
        txt++;
    }
    else if (txt[0] == 2)
    {
        mask = 128; // go to colored text
        txt++;
    }
    else
    {
        mask = 0;
    }

    while ((c = *txt))
    {
        // count word length
        for (l = 0; l < con.linewidth; l++)
        {
            if (txt[l] <= ' ')
            {
                break;
            }
        }

        // word wrap
        if (l != con.linewidth && (con.x + l > con.linewidth))
        {
            con.x = 0;
        }

        txt++;

        if (cr)
        {
            con.current--;
            cr = false;
        }

        if (!con.x)
        {
            Con_Linefeed();
            // mark time for transparent overlay
            if (con.current >= 0)
            {
                con.times[con.current % NUM_CON_TIMES] = realtime;
            }
        }

        switch (c)
        {
        case '\n':
            con.x = 0;
            break;

        case '\r':
            con.x = 0;
            cr = 1;
            break;

        default: // display character and advance
            y = con.current % con.totallines;
            con.text[y * con.linewidth + con.x] = c | mask;
            con.x++;
            if (con.x >= con.linewidth)
            {
                con.x = 0;
            }
            break;
        }
    }
}

/*
================
Con_DebugLog
================
*/
void Con_DebugLog(const char *file, const char *fmt, ...)
{
    va_list argptr;
    int fd;

    va_start(argptr, fmt);
    std::string data = COM_FormatVA(fmt, argptr);
    va_end(argptr);

    fd = _open(file, O_WRONLY | O_CREAT | O_APPEND, 0666);
    _write(fd, data.c_str(), (unsigned)data.size());
    _close(fd);
}

/*
================
Con_Printf

Handles cursor positioning, line wrapping, etc
================
*/
void Con_Printf(const char *fmt, ...)
{
    va_list argptr;
    static qboolean inupdate;

    va_start(argptr, fmt);
    std::string msg = COM_FormatVA(fmt, argptr);
    va_end(argptr);

    // also echo to debugging console
    Sys_Printf("{}", msg.c_str()); // also echo to debugging console

    // log all messages to file
    if (con.debuglog)
    {
        Con_DebugLog(va("{}/qconsole.log", com_gamedir), "%s", msg.c_str());
    }

    if (!con.initialized)
    {
        return;
    }

    if (cls.state == cactive_t::ca_dedicated)
    {
        return; // no graphics mode
    }

    // write it to the scrollable buffer
    Con_Print(msg.c_str());

    // update the screen if the console is displayed
    if (cls.signon != SIGNONS && !scr_disabled_for_loading)
    {
        // protect against infinite loop if something in SCR_UpdateScreen calls
        // Con_Printd
        if (!inupdate)
        {
            inupdate = true;
            SCR_UpdateScreen();
            inupdate = false;
        }
    }
}

/*
================
Con_DPrintf

A Con_Printf that only shows up if the "developer" cvar is set
================
*/
void Con_DPrintf(const char *fmt, ...)
{
    va_list argptr;

    if (!developer.value)
    {
        return; // don't confuse non-developers with techie stuff...
    }

    va_start(argptr, fmt);
    std::string msg = COM_FormatVA(fmt, argptr);
    va_end(argptr);

    Con_Printf("%s", msg.c_str());
}

/*
==================
Con_SafePrintf

Okay to call even when the screen can't be updated
==================
*/
void Con_SafePrintf(const char *fmt, ...)
{
    va_list argptr;
    int temp;

    va_start(argptr, fmt);
    std::string msg = COM_FormatVA(fmt, argptr);
    va_end(argptr);

    temp = scr_disabled_for_loading;
    scr_disabled_for_loading = true;
    Con_Printf("%s", msg.c_str());
    scr_disabled_for_loading = temp;
}

/*
==============================================================================

DRAWING

==============================================================================
*/

/*
================
Con_DrawInput

The input line scrolls horizontally if typing goes beyond the right edge
================
*/
void Con_DrawInput(void)
{
    int y;
    int i;
    char *text;

    if (key_dest != keydest_t::key_console && !con.forcedup)
    {
        return; // don't draw anything
    }

    text = key_lines[edit_line];

    // add the cursor frame
    text[key_linepos] = 10 + ((int)(realtime * con.cursorspeed) & 1);

    // fill out remainder with spaces
    for (i = key_linepos + 1; i < con.linewidth; i++)
    {
        text[i] = ' ';
    }

    //	prestep if horizontally scrolling
    if (key_linepos >= con.linewidth)
    {
        text += 1 + key_linepos - con.linewidth;
    }

    // draw it
    y = con.vislines - 16;

    for (i = 0; i < con.linewidth; i++)
    {
        Draw_Character((i + 1) << 3, con.vislines - 16, text[i]);
    }

    // remove cursor
    key_lines[edit_line][key_linepos] = 0;
}

/*
================
Con_DrawNotify

Draws the last few lines of output transparently over the game top
================
*/
void Con_DrawNotify(void)
{
    int x, v;
    char *text;
    int i;
    float time;
    extern char chat_buffer[];

    v = 0;
    for (i = con.current - NUM_CON_TIMES + 1; i <= con.current; i++)
    {
        if (i < 0)
        {
            continue;
        }
        time = con.times[i % NUM_CON_TIMES];
        if (time == 0)
        {
            continue;
        }
        time = realtime - time;
        if (time > con_notifytime.value)
        {
            continue;
        }
        text = con.text.data() + (i % con.totallines) * con.linewidth;

        clearnotify = 0;
        scr_copytop = 1;

        for (x = 0; x < con.linewidth; x++)
        {
            Draw_Character((x + 1) << 3, v, text[x]);
        }

        v += 8;
    }

    if (key_dest == keydest_t::key_message)
    {
        clearnotify = 0;
        scr_copytop = 1;

        x = 0;

        Draw_String(8, v, "say:");
        while (chat_buffer[x])
        {
            Draw_Character((x + 5) << 3, v, chat_buffer[x]);
            x++;
        }
        Draw_Character((x + 5) << 3, v, 10 + ((int)(realtime * con.cursorspeed) & 1));
        v += 8;
    }

    if (v > con.notifylines)
    {
        con.notifylines = v;
    }
}

/*
================
Con_DrawConsole

Draws the console with the solid background
The typing input line at the bottom should only be drawn if typing is allowed
================
*/
void Con_DrawConsole(int lines, qboolean drawinput)
{
    int i, x, y;
    int rows;
    char *text;
    int j;

    if (lines <= 0)
    {
        return;
    }

    // draw the background
    Draw_ConsoleBackground(lines);

    // draw the text
    con.vislines = lines;

    rows = (lines - 16) >> 3;     // rows of text to draw
    y = lines - 16 - (rows << 3); // may start slightly negative

    for (i = con.current - rows + 1; i <= con.current; i++, y += 8)
    {
        j = i - con.backscroll;
        if (j < 0)
        {
            j = 0;
        }
        text = con.text.data() + (j % con.totallines) * con.linewidth;

        for (x = 0; x < con.linewidth; x++)
        {
            Draw_Character((x + 1) << 3, y, text[x]);
        }
    }

    // draw the input prompt, user text, and cursor if desired
    if (drawinput)
    {
        Con_DrawInput();
    }
}

/*
==================
Con_NotifyBox
==================
*/
void Con_NotifyBox(char *text)
{
    double t1, t2;

    // during startup for sound / cd warnings
    Con_Printf("\n\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36"
               "\36\36\36\37\n");

    Con_Printf(text);

    Con_Printf("Press a key.\n");
    Con_Printf("\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36"
               "\36\36\37\n");

    Key_ArmForKeyDownUpWait(); // wait for a key down and up
    key_dest = keydest_t::key_console;

    do
    {
        t1 = Sys_FloatTime();
        SCR_UpdateScreen();
        Sys_SendKeyEvents();
        t2 = Sys_FloatTime();
        realtime += t2 - t1; // make the cursor blink
    } while (Key_IsArmedForKeyWait());

    Con_Printf("\n");
    key_dest = keydest_t::key_game;
    realtime = 0; // put the cursor back to invisible
}
