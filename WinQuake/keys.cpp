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
#include "quakedef.h"

/*

key up events are sent even if in console mode

*/

#define MAXCMDLINE 256
char key_lines[32][MAXCMDLINE];
int key_linepos;
int shift_down = false;
static int key_lastpress;

int edit_line = 0;
int history_line = 0;

keydest_t key_dest;

static int key_count; // incremented every key event

static std::string keybindings[256];
qboolean consolekeys[256];   // if true, can't be rebound while in console
qboolean menubound[256];     // if true, can't be rebound while in menu
int keyshift[256];           // key to map to if shift held down in console
static int key_repeats[256]; // if > 1, it is autorepeating
qboolean keydown[256];

struct keyname_t
{
    const char *name;
    int keynum;
};

keyname_t keynames[] = {{"TAB", K_TAB},
                        {"ENTER", K_ENTER},
                        {"ESCAPE", K_ESCAPE},
                        {"SPACE", K_SPACE},
                        {"BACKSPACE", K_BACKSPACE},
                        {"UPARROW", K_UPARROW},
                        {"DOWNARROW", K_DOWNARROW},
                        {"LEFTARROW", K_LEFTARROW},
                        {"RIGHTARROW", K_RIGHTARROW},

                        {"ALT", K_ALT},
                        {"CTRL", K_CTRL},
                        {"SHIFT", K_SHIFT},

                        {"F1", K_F1},
                        {"F2", K_F2},
                        {"F3", K_F3},
                        {"F4", K_F4},
                        {"F5", K_F5},
                        {"F6", K_F6},
                        {"F7", K_F7},
                        {"F8", K_F8},
                        {"F9", K_F9},
                        {"F10", K_F10},
                        {"F11", K_F11},
                        {"F12", K_F12},

                        {"INS", K_INS},
                        {"DEL", K_DEL},
                        {"PGDN", K_PGDN},
                        {"PGUP", K_PGUP},
                        {"HOME", K_HOME},
                        {"END", K_END},

                        {"MOUSE1", K_MOUSE1},
                        {"MOUSE2", K_MOUSE2},
                        {"MOUSE3", K_MOUSE3},

                        {"JOY1", K_JOY1},
                        {"JOY2", K_JOY2},
                        {"JOY3", K_JOY3},
                        {"JOY4", K_JOY4},

                        {"AUX1", K_AUX1},
                        {"AUX2", K_AUX2},
                        {"AUX3", K_AUX3},
                        {"AUX4", K_AUX4},
                        {"AUX5", K_AUX5},
                        {"AUX6", K_AUX6},
                        {"AUX7", K_AUX7},
                        {"AUX8", K_AUX8},
                        {"AUX9", K_AUX9},
                        {"AUX10", K_AUX10},
                        {"AUX11", K_AUX11},
                        {"AUX12", K_AUX12},
                        {"AUX13", K_AUX13},
                        {"AUX14", K_AUX14},
                        {"AUX15", K_AUX15},
                        {"AUX16", K_AUX16},
                        {"AUX17", K_AUX17},
                        {"AUX18", K_AUX18},
                        {"AUX19", K_AUX19},
                        {"AUX20", K_AUX20},
                        {"AUX21", K_AUX21},
                        {"AUX22", K_AUX22},
                        {"AUX23", K_AUX23},
                        {"AUX24", K_AUX24},
                        {"AUX25", K_AUX25},
                        {"AUX26", K_AUX26},
                        {"AUX27", K_AUX27},
                        {"AUX28", K_AUX28},
                        {"AUX29", K_AUX29},
                        {"AUX30", K_AUX30},
                        {"AUX31", K_AUX31},
                        {"AUX32", K_AUX32},

                        {"PAUSE", K_PAUSE},

                        {"MWHEELUP", K_MWHEELUP},
                        {"MWHEELDOWN", K_MWHEELDOWN},

                        {"XBOX_A", K_XBOX_A},
                        {"XBOX_B", K_XBOX_B},
                        {"XBOX_X", K_XBOX_X},
                        {"XBOX_Y", K_XBOX_Y},
                        {"XBOX_LSHOULDER", K_XBOX_LSHOULDER},
                        {"XBOX_RSHOULDER", K_XBOX_RSHOULDER},
                        {"XBOX_LTRIGGER", K_XBOX_LTRIGGER},
                        {"XBOX_RTRIGGER", K_XBOX_RTRIGGER},
                        {"XBOX_LTHUMB", K_XBOX_LTHUMB},
                        {"XBOX_RTHUMB", K_XBOX_RTHUMB},
                        {"XBOX_START", K_XBOX_START},
                        {"XBOX_BACK", K_XBOX_BACK},

                        {"SEMICOLON", ';'}, // because a raw semicolon seperates commands

                        {nullptr, 0}};

/*
==============================================================================

            LINE TYPING INTO THE CONSOLE

==============================================================================
*/

/*
====================
Key_Console

Interactive line editing and console scrollback
====================
*/
void Key_Console(int key)
{
    if (key == K_ENTER)
    {
        Cbuf_AddText(key_lines[edit_line] + 1); // skip the >
        Cbuf_AddText("\n");
        Con_Printf("%s\n", key_lines[edit_line]);
        edit_line = (edit_line + 1) & 31;
        history_line = edit_line;
        key_lines[edit_line][0] = ']';
        key_linepos = 1;
        if (cls.state == cactive_t::ca_disconnected)
        {
            SCR_UpdateScreen(); // force an update, because the command
                                // may take some time
        }
        return;
    }

    if (key == K_TAB)
    { // command completion
        auto cmd = Cmd_CompleteCommand(key_lines[edit_line] + 1);
        if (!cmd)
        {
            cmd = Cvar_CompleteVariable(key_lines[edit_line] + 1);
        }
        if (cmd)
        {
            Q_strlcpy(key_lines[edit_line] + 1, cmd->c_str(), sizeof(key_lines[edit_line]) - 1);
            key_linepos = (int)cmd->length() + 1;
            key_lines[edit_line][key_linepos] = ' ';
            key_linepos++;
            key_lines[edit_line][key_linepos] = 0;
            return;
        }
    }

    if (key == K_BACKSPACE || key == K_LEFTARROW)
    {
        if (key_linepos > 1)
        {
            key_linepos--;
        }
        return;
    }

    if (key == K_UPARROW)
    {
        do
        {
            history_line = (history_line - 1) & 31;
        } while (history_line != edit_line && !key_lines[history_line][1]);
        if (history_line == edit_line)
        {
            history_line = (edit_line + 1) & 31;
        }
        Q_strlcpy(key_lines[edit_line], key_lines[history_line], sizeof(key_lines[edit_line]));
        key_linepos = Q_strlen(key_lines[edit_line]);
        return;
    }

    if (key == K_DOWNARROW)
    {
        if (history_line == edit_line)
        {
            return;
        }
        do
        {
            history_line = (history_line + 1) & 31;
        } while (history_line != edit_line && !key_lines[history_line][1]);
        if (history_line == edit_line)
        {
            key_lines[edit_line][0] = ']';
            key_linepos = 1;
        }
        else
        {
            Q_strlcpy(key_lines[edit_line], key_lines[history_line], sizeof(key_lines[edit_line]));
            key_linepos = Q_strlen(key_lines[edit_line]);
        }
        return;
    }

    if (key == K_PGUP || key == K_MWHEELUP)
    {
        con.backscroll += 2;
        if (con.backscroll > con.totallines - ((int)vid.height >> 3) - 1)
        {
            con.backscroll = con.totallines - ((int)vid.height >> 3) - 1;
        }
        return;
    }

    if (key == K_PGDN || key == K_MWHEELDOWN)
    {
        con.backscroll -= 2;
        if (con.backscroll < 0)
        {
            con.backscroll = 0;
        }
        return;
    }

    if (key == K_HOME)
    {
        con.backscroll = con.totallines - (vid.height >> 3) - 1;
        return;
    }

    if (key == K_END)
    {
        con.backscroll = 0;
        return;
    }

    if (key < 32 || key > 127)
    {
        return; // non printable
    }

    if (key_linepos < MAXCMDLINE - 1)
    {
        key_lines[edit_line][key_linepos] = key;
        key_linepos++;
        key_lines[edit_line][key_linepos] = 0;
    }
}

//============================================================================

char chat_buffer[32];
qboolean team_message = false;

void Key_Message(int key)
{
    static int chat_bufferlen = 0;

    if (key == K_ENTER)
    {
        if (team_message)
        {
            Cbuf_AddText("say_team \"");
        }
        else
        {
            Cbuf_AddText("say \"");
        }
        Cbuf_AddText(chat_buffer);
        Cbuf_AddText("\"\n");

        key_dest = keydest_t::key_game;
        chat_bufferlen = 0;
        chat_buffer[0] = 0;
        return;
    }

    if (key == K_ESCAPE)
    {
        key_dest = keydest_t::key_game;
        chat_bufferlen = 0;
        chat_buffer[0] = 0;
        return;
    }

    if (key < 32 || key > 127)
    {
        return; // non printable
    }

    if (key == K_BACKSPACE)
    {
        if (chat_bufferlen)
        {
            chat_bufferlen--;
            chat_buffer[chat_bufferlen] = 0;
        }
        return;
    }

    if (chat_bufferlen == 31)
    {
        return; // all full
    }

    chat_buffer[chat_bufferlen++] = key;
    chat_buffer[chat_bufferlen] = 0;
}

//============================================================================

/*
===================
Key_StringToKeynum

Returns a key number to be used to index keybindings[] by looking at
the given string.  Single ascii characters return themselves, while
the K_* names are matched up.
===================
*/
int Key_StringToKeynum(const char *str)
{
    keyname_t *kn;

    if (!str || !str[0])
    {
        return -1;
    }
    if (!str[1])
    {
        return str[0];
    }

    for (kn = keynames; kn->name; kn++)
    {
        if (!Q_strcasecmp(str, kn->name))
        {
            return kn->keynum;
        }
    }
    return -1;
}

/*
===================
Key_KeynumToString

Returns a string (either a single ascii char, or a K_* name) for the
given keynum.
FIXME: handle quote special (general escape sequence?)
===================
*/
std::string Key_KeynumToString(int keynum)
{
    keyname_t *kn;
    char tinystr[2];

    if (keynum == -1)
    {
        return "<KEY NOT FOUND>";
    }
    if (keynum > 32 && keynum < 127)
    { // printable ascii
        tinystr[0] = keynum;
        tinystr[1] = 0;
        return tinystr;
    }

    for (kn = keynames; kn->name; kn++)
    {
        if (keynum == kn->keynum)
        {
            return kn->name;
        }
    }

    return "<UNKNOWN KEYNUM>";
}

/*
===================
Key_SetBinding
===================
*/
void Key_SetBinding(int keynum, const char *binding)
{
    if (keynum == -1)
    {
        return;
    }

    keybindings[keynum] = binding;
}

/*
===================
Key_KeysForCommand

Finds up to two keys bound to a command whose name starts with the given
string. Used by the Keys menu to display/edit bindings.
===================
*/
void Key_KeysForCommand(const char *command, int *twokeys)
{
    int count;
    int j;
    int l;

    twokeys[0] = twokeys[1] = -1;
    l = (int)strlen(command);
    count = 0;

    for (j = 0; j < 256; j++)
    {
        if (keybindings[j].empty())
        {
            continue;
        }
        if (!strncmp(keybindings[j].c_str(), command, l))
        {
            twokeys[count] = j;
            count++;
            if (count == 2)
            {
                break;
            }
        }
    }
}

/*
===================
Key_UnbindCommand

Clears every key bound to a command whose name starts with the given
string. Used by the Keys menu's "clear binding" action.
===================
*/
void Key_UnbindCommand(const char *command)
{
    int j;
    int l;

    l = (int)strlen(command);

    for (j = 0; j < 256; j++)
    {
        if (keybindings[j].empty())
        {
            continue;
        }
        if (!strncmp(keybindings[j].c_str(), command, l))
        {
            Key_SetBinding(j, "");
        }
    }
}

/*
===================
Key_ArmForKeyDownUpWait / Key_IsArmedForKeyWait

"Press a key" wait used by Con_NotifyBox -- arms key_count so it only
unblocks once a full key down-and-up pair has been seen.
===================
*/
void Key_ArmForKeyDownUpWait(void)
{
    key_count = -2;
}

bool Key_IsArmedForKeyWait(void)
{
    return key_count < 0;
}

/*
===================
Key_ArmForSingleKeyWait / Key_LastKeyPressed

"Press a key" wait used by SCR_ModalMessage -- rearmed every poll so only
the most recent key transition is examined via Key_LastKeyPressed.
===================
*/
void Key_ArmForSingleKeyWait(void)
{
    key_count = -1;
}

int Key_LastKeyPressed(void)
{
    return key_lastpress;
}

/*
===================
Key_Unbind_f
===================
*/
void Key_Unbind_f(void)
{
    int b;

    if (Cmd_Argc() != 2)
    {
        Con_Printf("unbind <key> : remove commands from a key\n");
        return;
    }

    b = Key_StringToKeynum(Cmd_Argv(1));
    if (b == -1)
    {
        Con_Printf("\"%s\" isn't a valid key\n", Cmd_Argv(1));
        return;
    }

    Key_SetBinding(b, "");
}

void Key_Unbindall_f(void)
{
    int i;

    for (i = 0; i < 256; i++)
    {
        if (!keybindings[i].empty())
        {
            Key_SetBinding(i, "");
        }
    }
}

/*
===================
Key_Bind_f
===================
*/
void Key_Bind_f(void)
{
    int i, c, b;

    c = Cmd_Argc();

    if (c != 2 && c != 3)
    {
        Con_Printf("bind <key> [command] : attach a command to a key\n");
        return;
    }
    b = Key_StringToKeynum(Cmd_Argv(1));
    if (b == -1)
    {
        Con_Printf("\"%s\" isn't a valid key\n", Cmd_Argv(1));
        return;
    }

    if (c == 2)
    {
        if (!keybindings[b].empty())
        {
            Con_Printf("\"%s\" = \"%s\"\n", Cmd_Argv(1), keybindings[b].c_str());
        }
        else
        {
            Con_Printf("\"%s\" is not bound\n", Cmd_Argv(1));
        }
        return;
    }

    // copy the rest of the command line
    std::string cmd;
    for (i = 2; i < c; i++)
    {
        if (i > 2)
        {
            cmd += " ";
        }
        cmd += Cmd_Argv(i);
    }

    Key_SetBinding(b, cmd.c_str());
}

/*
============
Key_WriteBindings

Writes lines containing "bind key value"
============
*/
void Key_WriteBindings(FILE *f)
{
    int i;

    for (i = 0; i < 256; i++)
    {
        if (!keybindings[i].empty())
        {
            fprintf(f, "bind \"%s\" \"%s\"\n", Key_KeynumToString(i).c_str(), keybindings[i].c_str());
        }
    }
}

/*
===================
Key_Init
===================
*/
void Key_Init(void)
{
    int i;

    for (i = 0; i < 32; i++)
    {
        key_lines[i][0] = ']';
        key_lines[i][1] = 0;
    }
    key_linepos = 1;

    //
    // init ascii characters in console mode
    //
    for (i = 32; i < 128; i++)
    {
        consolekeys[i] = true;
    }
    consolekeys[K_ENTER] = true;
    consolekeys[K_TAB] = true;
    consolekeys[K_LEFTARROW] = true;
    consolekeys[K_RIGHTARROW] = true;
    consolekeys[K_UPARROW] = true;
    consolekeys[K_DOWNARROW] = true;
    consolekeys[K_BACKSPACE] = true;
    consolekeys[K_PGUP] = true;
    consolekeys[K_PGDN] = true;
    consolekeys[K_SHIFT] = true;
    consolekeys[K_MWHEELUP] = true;
    consolekeys[K_MWHEELDOWN] = true;
    consolekeys['`'] = false;
    consolekeys['~'] = false;

    for (i = 0; i < 256; i++)
    {
        keyshift[i] = i;
    }
    for (i = 'a'; i <= 'z'; i++)
    {
        keyshift[i] = i - 'a' + 'A';
    }
    keyshift['1'] = '!';
    keyshift['2'] = '@';
    keyshift['3'] = '#';
    keyshift['4'] = '$';
    keyshift['5'] = '%';
    keyshift['6'] = '^';
    keyshift['7'] = '&';
    keyshift['8'] = '*';
    keyshift['9'] = '(';
    keyshift['0'] = ')';
    keyshift['-'] = '_';
    keyshift['='] = '+';
    keyshift[','] = '<';
    keyshift['.'] = '>';
    keyshift['/'] = '?';
    keyshift[';'] = ':';
    keyshift['\''] = '"';
    keyshift['['] = '{';
    keyshift[']'] = '}';
    keyshift['`'] = '~';
    keyshift['\\'] = '|';

    menubound[K_ESCAPE] = true;
    for (i = 0; i < 12; i++)
    {
        menubound[K_F1 + i] = true;
    }

    //
    // register our functions
    //
    Cmd_AddCommand("bind", Key_Bind_f);
    Cmd_AddCommand("unbind", Key_Unbind_f);
    Cmd_AddCommand("unbindall", Key_Unbindall_f);
}

/*
===================
Key_Event

Called by the system between frames for both key up and key down events
Should NOT be called during an interrupt!
===================
*/
void Key_Event(int key, qboolean down)
{
    const std::string *kb;
    std::string cmd;

    keydown[key] = down;

    if (!down)
    {
        key_repeats[key] = 0;
    }

    key_lastpress = key;
    key_count++;
    if (key_count <= 0)
    {
        return; // just catching keys for Con_NotifyBox
    }

    // update auto-repeat status
    if (down)
    {
        key_repeats[key]++;
        if (key != K_BACKSPACE && key != K_PAUSE && key_repeats[key] > 1)
        {
            return; // ignore most autorepeats
        }

        if (key >= 200 && keybindings[key].empty())
        {
            Con_Printf("%s is unbound, hit F4 to set.\n", Key_KeynumToString(key).c_str());
        }
    }

    if (key == K_SHIFT)
    {
        shift_down = down;
    }

    //
    // handle escape specialy, so the user can never unbind it
    //
    if (key == K_ESCAPE)
    {
        if (!down)
        {
            return;
        }
        switch (key_dest)
        {
        case keydest_t::key_message:
            Key_Message(key);
            break;
        case keydest_t::key_menu:
            M_Keydown(key);
            break;
        case keydest_t::key_game:
        case keydest_t::key_console:
            M_ToggleMenu_f();
            break;
        default:
            Sys_Error("Bad key_dest");
        }
        return;
    }

    //
    // key up events only generate commands if the game key binding is
    // a button command (leading + sign).  These will occur even in console mode,
    // to keep the character from continuing an action started before a console
    // switch.  Button commands include the kenum as a parameter, so multiple
    // downs can be matched with ups
    //
    if (!down)
    {
        kb = &keybindings[key];
        if (!kb->empty() && (*kb)[0] == '+')
        {
            cmd = "-" + kb->substr(1) + " " + std::to_string(key) + "\n";
            Cbuf_AddText(cmd.c_str());
        }
        if (keyshift[key] != key)
        {
            kb = &keybindings[keyshift[key]];
            if (!kb->empty() && (*kb)[0] == '+')
            {
                cmd = "-" + kb->substr(1) + " " + std::to_string(key) + "\n";
                Cbuf_AddText(cmd.c_str());
            }
        }
        return;
    }

    //
    // during demo playback, most keys bring up the main menu
    //
    if (cls.demoplayback && down && consolekeys[key] && key_dest == keydest_t::key_game)
    {
        M_ToggleMenu_f();
        return;
    }

    //
    // if not a consolekey, send to the interpreter no matter what mode is
    //
    if ((key_dest == keydest_t::key_menu && menubound[key]) ||
        (key_dest == keydest_t::key_console && !consolekeys[key]) ||
        (key_dest == keydest_t::key_game && (!con.forcedup || !consolekeys[key])))
    {
        kb = &keybindings[key];
        if (!kb->empty())
        {
            if ((*kb)[0] == '+')
            { // button commands add keynum as a parm
                cmd = *kb + " " + std::to_string(key) + "\n";
                Cbuf_AddText(cmd.c_str());
            }
            else
            {
                Cbuf_AddText(kb->c_str());
                Cbuf_AddText("\n");
            }
        }
        return;
    }

    if (!down)
    {
        return; // other systems only care about key down events
    }

    if (shift_down)
    {
        key = keyshift[key];
    }

    switch (key_dest)
    {
    case keydest_t::key_message:
        Key_Message(key);
        break;
    case keydest_t::key_menu:
        M_Keydown(key);
        break;

    case keydest_t::key_game:
    case keydest_t::key_console:
        Key_Console(key);
        break;
    default:
        Sys_Error("Bad key_dest");
    }
}

/*
===================
Key_ClearStates
===================
*/
void Key_ClearStates(void)
{
    int i;

    for (i = 0; i < 256; i++)
    {
        keydown[i] = false;
        key_repeats[i] = 0;
    }
}
