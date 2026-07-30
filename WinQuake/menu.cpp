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

#ifdef _WIN32
#include "winquake.h"
#endif

void (*vid_menudrawfn)(void);
void (*vid_menukeyfn)(int key);

// enum {m_none, m_main, m_singleplayer, m_load, m_save, m_multiplayer, m_setup, m_net, m_options, m_video, m_keys,
// m_help, m_quit, m_lanconfig, m_gameoptions, m_search, m_slist} m_state;
m_state_t m_state;

void M_Menu_Main_f(void);
void M_Menu_SinglePlayer_f(void);
void M_Menu_Load_f(void);
void M_Menu_Save_f(void);
void M_Menu_MultiPlayer_f(void);
void M_Menu_Setup_f(void);
void M_Menu_Net_f(void);
void M_Menu_Options_f(void);
void M_Menu_Keys_f(void);
void M_Menu_Video_f(void);
void M_Menu_Help_f(void);
void M_Menu_Quit_f(void);
void M_Menu_LanConfig_f(void);
void M_Menu_GameOptions_f(void);
void M_Menu_Search_f(void);
void M_Menu_ServerList_f(void);

qboolean m_entersound; // play after drawing a frame, so caching
                       // won't disrupt the sound
qboolean m_recursiveDraw;

int m_return_state;
qboolean m_return_onerror;
char m_return_reason[32];

#define StartingGame (multiPlayerMenu.cursor == 1)
#define JoiningGame (multiPlayerMenu.cursor == 0)
#define IPXConfig (netMenu.cursor == 0)
#define TCPIPConfig (netMenu.cursor == 1)

void M_ConfigureNetSubsystem(void);

/*
================
M_DrawCharacter

Draws one solid graphics character
================
*/
void M_DrawCharacter(int cx, int line, int num)
{
    Draw_Character(cx + ((vid.width - 320) >> 1), line, num);
}

void M_Print(int cx, int cy, const char *str)
{
    while (*str)
    {
        M_DrawCharacter(cx, cy, (*str) + 128);
        str++;
        cx += 8;
    }
}

void M_PrintWhite(int cx, int cy, const char *str)
{
    while (*str)
    {
        M_DrawCharacter(cx, cy, *str);
        str++;
        cx += 8;
    }
}

void M_DrawTransPic(int x, int y, qpic_t *pic)
{
    Draw_TransPic(x + ((vid.width - 320) >> 1), y, pic);
}

void M_DrawPic(int x, int y, qpic_t *pic)
{
    Draw_Pic(x + ((vid.width - 320) >> 1), y, pic);
}

byte identityTable[256];
byte translationTable[256];

void M_BuildTranslationTable(int top, int bottom)
{
    int j;
    byte *dest, *source;

    for (j = 0; j < 256; j++)
    {
        identityTable[j] = j;
    }
    dest = translationTable;
    source = identityTable;
    memcpy(dest, source, 256);

    if (top < 128) // the artists made some backwards ranges.  sigh.
    {
        memcpy(dest + TOP_RANGE, source + top, 16);
    }
    else
    {
        for (j = 0; j < 16; j++)
        {
            dest[TOP_RANGE + j] = source[top + 15 - j];
        }
    }

    if (bottom < 128)
    {
        memcpy(dest + BOTTOM_RANGE, source + bottom, 16);
    }
    else
    {
        for (j = 0; j < 16; j++)
        {
            dest[BOTTOM_RANGE + j] = source[bottom + 15 - j];
        }
    }
}

void M_DrawTransPicTranslate(int x, int y, qpic_t *pic)
{
    Draw_TransPicTranslate(x + ((vid.width - 320) >> 1), y, pic, translationTable);
}

void M_DrawTextBox(int x, int y, int width, int lines)
{
    qpic_t *p;
    int cx, cy;
    int n;

    // draw left side
    cx = x;
    cy = y;
    p = Draw_CachePic("gfx/box_tl.lmp");
    M_DrawTransPic(cx, cy, p);
    p = Draw_CachePic("gfx/box_ml.lmp");
    for (n = 0; n < lines; n++)
    {
        cy += 8;
        M_DrawTransPic(cx, cy, p);
    }
    p = Draw_CachePic("gfx/box_bl.lmp");
    M_DrawTransPic(cx, cy + 8, p);

    // draw middle
    cx += 8;
    while (width > 0)
    {
        cy = y;
        p = Draw_CachePic("gfx/box_tm.lmp");
        M_DrawTransPic(cx, cy, p);
        p = Draw_CachePic("gfx/box_mm.lmp");
        for (n = 0; n < lines; n++)
        {
            cy += 8;
            if (n == 1)
            {
                p = Draw_CachePic("gfx/box_mm2.lmp");
            }
            M_DrawTransPic(cx, cy, p);
        }
        p = Draw_CachePic("gfx/box_bm.lmp");
        M_DrawTransPic(cx, cy + 8, p);
        width -= 2;
        cx += 16;
    }

    // draw right side
    cy = y;
    p = Draw_CachePic("gfx/box_tr.lmp");
    M_DrawTransPic(cx, cy, p);
    p = Draw_CachePic("gfx/box_mr.lmp");
    for (n = 0; n < lines; n++)
    {
        cy += 8;
        M_DrawTransPic(cx, cy, p);
    }
    p = Draw_CachePic("gfx/box_br.lmp");
    M_DrawTransPic(cx, cy + 8, p);
}

//=============================================================================

// A menu screen's Draw()/Key() -- replaces the M_<Name>_Draw()/M_<Name>_Key()
// free-function pairs the corresponding screens used to dispatch to via
// M_Draw()/M_Keydown()'s switch statements. m_state remains the authoritative,
// externally-writable dispatch key (net_dgrm.cpp writes it directly to
// recover from a failed connection attempt) -- M_ScreenForState() maps it to
// the active screen object each frame, so this class hierarchy is a
// presentation-layer addition underneath m_state, not a replacement for it.
class MenuScreen
{
  public:
    virtual ~MenuScreen() = default;
    virtual void Draw() = 0;
    virtual void Key(int key) = 0;
};

int m_save_demonum;

/*
================
M_ToggleMenu_f
================
*/
void M_ToggleMenu_f(void)
{
    m_entersound = true;

    if (key_dest == keydest_t::key_menu)
    {
        if (m_state != m_main)
        {
            M_Menu_Main_f();
            return;
        }
        key_dest = keydest_t::key_game;
        m_state = m_none;
        return;
    }
    if (key_dest == keydest_t::key_console)
    {
        Con_ToggleConsole_f();
    }
    else
    {
        M_Menu_Main_f();
    }
}

//=============================================================================
/* MAIN MENU */

#define MAIN_ITEMS 5

class MainMenu : public MenuScreen
{
  public:
    int cursor = 0;
    void Draw() override;
    void Key(int key) override;
};
MainMenu mainMenu;

void M_Menu_Main_f(void)
{
    if (key_dest != keydest_t::key_menu)
    {
        m_save_demonum = cls.demonum;
        cls.demonum = -1;
    }
    key_dest = keydest_t::key_menu;
    m_state = m_main;
    m_entersound = true;
}

void MainMenu::Draw(void)
{
    int f;
    qpic_t *p;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/ttl_main.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);
    M_DrawTransPic(72, 32, Draw_CachePic("gfx/mainmenu.lmp"));

    f = (int)(host_time * 10) % 6;

    M_DrawTransPic(54, 32 + cursor * 20, Draw_CachePic(va("gfx/menudot{}.lmp", f + 1)));
}

void MainMenu::Key(int key)
{
    switch (key)
    {
    case K_ESCAPE:
        key_dest = keydest_t::key_game;
        m_state = m_none;
        cls.demonum = m_save_demonum;
        if (cls.demonum != -1 && !cls.demoplayback && cls.state != ca_connected)
        {
            CL_NextDemo();
        }
        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        if (++cursor >= MAIN_ITEMS)
        {
            cursor = 0;
        }
        break;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        if (--cursor < 0)
        {
            cursor = MAIN_ITEMS - 1;
        }
        break;

    case K_ENTER:
        m_entersound = true;

        switch (cursor)
        {
        case 0:
            M_Menu_SinglePlayer_f();
            break;

        case 1:
            M_Menu_MultiPlayer_f();
            break;

        case 2:
            M_Menu_Options_f();
            break;

        case 3:
            M_Menu_Help_f();
            break;

        case 4:
            M_Menu_Quit_f();
            break;
        }
    }
}

//=============================================================================
/* SINGLE PLAYER MENU */

#define SINGLEPLAYER_ITEMS 3

class SinglePlayerMenu : public MenuScreen
{
  public:
    int cursor = 0;
    void Draw() override;
    void Key(int key) override;
};
SinglePlayerMenu singlePlayerMenu;

void M_Menu_SinglePlayer_f(void)
{
    key_dest = keydest_t::key_menu;
    m_state = m_singleplayer;
    m_entersound = true;
}

void SinglePlayerMenu::Draw(void)
{
    int f;
    qpic_t *p;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/ttl_sgl.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);
    M_DrawTransPic(72, 32, Draw_CachePic("gfx/sp_menu.lmp"));

    f = (int)(host_time * 10) % 6;

    M_DrawTransPic(54, 32 + cursor * 20, Draw_CachePic(va("gfx/menudot{}.lmp", f + 1)));
}

void SinglePlayerMenu::Key(int key)
{
    switch (key)
    {
    case K_ESCAPE:
        M_Menu_Main_f();
        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        if (++cursor >= SINGLEPLAYER_ITEMS)
        {
            cursor = 0;
        }
        break;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        if (--cursor < 0)
        {
            cursor = SINGLEPLAYER_ITEMS - 1;
        }
        break;

    case K_ENTER:
        m_entersound = true;

        switch (cursor)
        {
        case 0:
            if (SV_Active())
            {
                if (!SCR_ModalMessage("Are you sure you want to\nstart a new game?\n"))
                {
                    break;
                }
            }
            key_dest = keydest_t::key_game;
            if (SV_Active())
            {
                Cbuf_AddText("disconnect\n");
            }
            Cbuf_AddText("maxplayers 1\n");
            Cbuf_AddText("map start\n");
            break;

        case 1:
            M_Menu_Load_f();
            break;

        case 2:
            M_Menu_Save_f();
            break;
        }
    }
}

//=============================================================================
/* LOAD/SAVE MENU */

int load_cursor; // 0 < load_cursor < MAX_SAVEGAMES

#define MAX_SAVEGAMES 12
char m_filenames[MAX_SAVEGAMES][SAVEGAME_COMMENT_LENGTH + 1];
int loadable[MAX_SAVEGAMES];

void M_ScanSaves(void)
{
    int i, j;
    char name[MAX_OSPATH];
    std::string path;
    FILE *f;
    int version;

    for (i = 0; i < MAX_SAVEGAMES; i++)
    {
        strcpy(m_filenames[i], "--- UNUSED SLOT ---");
        loadable[i] = false;
        path = std::format("{}/s{}.sav", com_gamedir, i);
        f = fopen(path.c_str(), "r");
        if (!f)
        {
            continue;
        }
        fscanf(f, "%i\n", &version);
        fscanf(f, "%79s\n", name);
        strncpy(m_filenames[i], name, sizeof(m_filenames[i]) - 1);

        // change _ back to space
        for (j = 0; j < SAVEGAME_COMMENT_LENGTH; j++)
        {
            if (m_filenames[i][j] == '_')
            {
                m_filenames[i][j] = ' ';
            }
        }
        loadable[i] = true;
        fclose(f);
    }
}

// load_cursor/m_filenames/loadable are shared between Load and Save (the
// cursor position and scan results carry over between the two screens, an
// existing behavior preserved exactly here) -- so, unlike a screen-private
// cursor, they stay as plain globals rather than becoming a class member.
class LoadGameMenu : public MenuScreen
{
  public:
    void Draw() override;
    void Key(int key) override;
};
LoadGameMenu loadGameMenu;

class SaveMenu : public MenuScreen
{
  public:
    void Draw() override;
    void Key(int key) override;
};
SaveMenu saveMenu;

void M_Menu_Load_f(void)
{
    m_entersound = true;
    m_state = m_load;
    key_dest = keydest_t::key_menu;
    M_ScanSaves();
}

void M_Menu_Save_f(void)
{
    if (!SV_Active())
    {
        return;
    }
    if (CL_Intermission())
    {
        return;
    }
    if (SV_NumClients() != 1)
    {
        return;
    }
    m_entersound = true;
    m_state = m_save;
    key_dest = keydest_t::key_menu;
    M_ScanSaves();
}

void LoadGameMenu::Draw(void)
{
    int i;
    qpic_t *p;

    p = Draw_CachePic("gfx/p_load.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);

    for (i = 0; i < MAX_SAVEGAMES; i++)
    {
        M_Print(16, 32 + 8 * i, m_filenames[i]);
    }

    // line cursor
    M_DrawCharacter(8, 32 + load_cursor * 8, 12 + ((int)(realtime * 4) & 1));
}

void SaveMenu::Draw(void)
{
    int i;
    qpic_t *p;

    p = Draw_CachePic("gfx/p_save.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);

    for (i = 0; i < MAX_SAVEGAMES; i++)
    {
        M_Print(16, 32 + 8 * i, m_filenames[i]);
    }

    // line cursor
    M_DrawCharacter(8, 32 + load_cursor * 8, 12 + ((int)(realtime * 4) & 1));
}

void LoadGameMenu::Key(int k)
{
    switch (k)
    {
    case K_ESCAPE:
        M_Menu_SinglePlayer_f();
        break;

    case K_ENTER:
        S_LocalSound("misc/menu2.wav");
        if (!loadable[load_cursor])
        {
            return;
        }
        m_state = m_none;
        key_dest = keydest_t::key_game;

        // Host_Loadgame_f can't bring up the loading plaque because too much
        // stack space has been used, so do it now
        SCR_BeginLoadingPlaque();

        // issue the load command
        Cbuf_AddText(va("load s{}\n", load_cursor));
        return;

    case K_UPARROW:
    case K_LEFTARROW:
        S_LocalSound("misc/menu1.wav");
        load_cursor--;
        if (load_cursor < 0)
        {
            load_cursor = MAX_SAVEGAMES - 1;
        }
        break;

    case K_DOWNARROW:
    case K_RIGHTARROW:
        S_LocalSound("misc/menu1.wav");
        load_cursor++;
        if (load_cursor >= MAX_SAVEGAMES)
        {
            load_cursor = 0;
        }
        break;
    }
}

void SaveMenu::Key(int k)
{
    switch (k)
    {
    case K_ESCAPE:
        M_Menu_SinglePlayer_f();
        break;

    case K_ENTER:
        m_state = m_none;
        key_dest = keydest_t::key_game;
        Cbuf_AddText(va("save s{}\n", load_cursor));
        return;

    case K_UPARROW:
    case K_LEFTARROW:
        S_LocalSound("misc/menu1.wav");
        load_cursor--;
        if (load_cursor < 0)
        {
            load_cursor = MAX_SAVEGAMES - 1;
        }
        break;

    case K_DOWNARROW:
    case K_RIGHTARROW:
        S_LocalSound("misc/menu1.wav");
        load_cursor++;
        if (load_cursor >= MAX_SAVEGAMES)
        {
            load_cursor = 0;
        }
        break;
    }
}

//=============================================================================
/* MULTIPLAYER MENU */

#define MULTIPLAYER_ITEMS 3

class MultiPlayerMenu : public MenuScreen
{
  public:
    int cursor = 0;
    void Draw() override;
    void Key(int key) override;
};
MultiPlayerMenu multiPlayerMenu;

void M_Menu_MultiPlayer_f(void)
{
    key_dest = keydest_t::key_menu;
    m_state = m_multiplayer;
    m_entersound = true;
}

void MultiPlayerMenu::Draw(void)
{
    int f;
    qpic_t *p;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/p_multi.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);
    M_DrawTransPic(72, 32, Draw_CachePic("gfx/mp_menu.lmp"));

    f = (int)(host_time * 10) % 6;

    M_DrawTransPic(54, 32 + cursor * 20, Draw_CachePic(va("gfx/menudot{}.lmp", f + 1)));

    if (net.ipxAvailable || net.tcpipAvailable)
    {
        return;
    }
    M_PrintWhite((320 / 2) - ((27 * 8) / 2), 148, "No Communications Available");
}

void MultiPlayerMenu::Key(int key)
{
    switch (key)
    {
    case K_ESCAPE:
        M_Menu_Main_f();
        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        if (++cursor >= MULTIPLAYER_ITEMS)
        {
            cursor = 0;
        }
        break;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        if (--cursor < 0)
        {
            cursor = MULTIPLAYER_ITEMS - 1;
        }
        break;

    case K_ENTER:
        m_entersound = true;
        switch (cursor)
        {
        case 0:
            if (net.ipxAvailable || net.tcpipAvailable)
            {
                M_Menu_Net_f();
            }
            break;

        case 1:
            if (net.ipxAvailable || net.tcpipAvailable)
            {
                M_Menu_Net_f();
            }
            break;

        case 2:
            M_Menu_Setup_f();
            break;
        }
    }
}

//=============================================================================
/* SETUP MENU */

int setup_cursor_table[] = {40, 56, 80, 104, 140};

#define NUM_SETUP_CMDS 5

class SetupMenu : public MenuScreen
{
  public:
    int cursor = 4;
    char hostnameBuf[16] = {0};
    char nameBuf[16] = {0};
    int oldTop = 0;
    int oldBottom = 0;
    int top = 0;
    int bottom = 0;
    void Draw() override;
    void Key(int key) override;
};
SetupMenu setupMenu;

void M_Menu_Setup_f(void)
{
    key_dest = keydest_t::key_menu;
    m_state = m_setup;
    m_entersound = true;
    Q_strlcpy(setupMenu.nameBuf, cl_name.string.c_str(), sizeof(setupMenu.nameBuf));
    Q_strlcpy(setupMenu.hostnameBuf, hostname.string.c_str(), sizeof(setupMenu.hostnameBuf));
    setupMenu.top = setupMenu.oldTop = ((int)cl_color.value) >> 4;
    setupMenu.bottom = setupMenu.oldBottom = ((int)cl_color.value) & 15;
}

void SetupMenu::Draw(void)
{
    qpic_t *p;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/p_multi.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);

    M_Print(64, 40, "Hostname");
    M_DrawTextBox(160, 32, 16, 1);
    M_Print(168, 40, hostnameBuf);

    M_Print(64, 56, "Your name");
    M_DrawTextBox(160, 48, 16, 1);
    M_Print(168, 56, nameBuf);

    M_Print(64, 80, "Shirt color");
    M_Print(64, 104, "Pants color");

    M_DrawTextBox(64, 140 - 8, 14, 1);
    M_Print(72, 140, "Accept Changes");

    p = Draw_CachePic("gfx/bigbox.lmp");
    M_DrawTransPic(160, 64, p);
    p = Draw_CachePic("gfx/menuplyr.lmp");
    M_BuildTranslationTable(top * 16, bottom * 16);
    M_DrawTransPicTranslate(172, 72, p);

    M_DrawCharacter(56, setup_cursor_table[cursor], 12 + ((int)(realtime * 4) & 1));

    if (cursor == 0)
    {
        M_DrawCharacter(168 + 8 * (int)strlen(hostnameBuf), setup_cursor_table[cursor], 10 + ((int)(realtime * 4) & 1));
    }

    if (cursor == 1)
    {
        M_DrawCharacter(168 + 8 * (int)strlen(nameBuf), setup_cursor_table[cursor], 10 + ((int)(realtime * 4) & 1));
    }
}

void SetupMenu::Key(int k)
{
    int l;

    switch (k)
    {
    case K_ESCAPE:
        M_Menu_MultiPlayer_f();
        break;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        cursor--;
        if (cursor < 0)
        {
            cursor = NUM_SETUP_CMDS - 1;
        }
        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        cursor++;
        if (cursor >= NUM_SETUP_CMDS)
        {
            cursor = 0;
        }
        break;

    case K_LEFTARROW:
        if (cursor < 2)
        {
            return;
        }
        S_LocalSound("misc/menu3.wav");
        if (cursor == 2)
        {
            top = top - 1;
        }
        if (cursor == 3)
        {
            bottom = bottom - 1;
        }
        break;
    case K_RIGHTARROW:
        if (cursor < 2)
        {
            return;
        }
    forward:
        S_LocalSound("misc/menu3.wav");
        if (cursor == 2)
        {
            top = top + 1;
        }
        if (cursor == 3)
        {
            bottom = bottom + 1;
        }
        break;

    case K_ENTER:
        if (cursor == 0 || cursor == 1)
        {
            return;
        }

        if (cursor == 2 || cursor == 3)
        {
            goto forward;
        }

        // cursor == 4 (OK)
        if (Q_strcmp(cl_name.string.c_str(), nameBuf) != 0)
        {
            Cbuf_AddText(va("name \"{}\"\n", nameBuf));
        }
        if (Q_strcmp(hostname.string.c_str(), hostnameBuf) != 0)
        {
            Cvar_Set("hostname", hostnameBuf);
        }
        if (top != oldTop || bottom != oldBottom)
        {
            Cbuf_AddText(va("color {} {}\n", top, bottom));
        }
        m_entersound = true;
        M_Menu_MultiPlayer_f();
        break;

    case K_BACKSPACE:
        if (cursor == 0)
        {
            if (strlen(hostnameBuf))
            {
                hostnameBuf[strlen(hostnameBuf) - 1] = 0;
            }
        }

        if (cursor == 1)
        {
            if (strlen(nameBuf))
            {
                nameBuf[strlen(nameBuf) - 1] = 0;
            }
        }
        break;

    default:
        if (k < 32 || k > 127)
        {
            break;
        }
        if (cursor == 0)
        {
            l = (int)strlen(hostnameBuf);
            if (l < 15)
            {
                hostnameBuf[l + 1] = 0;
                hostnameBuf[l] = k;
            }
        }
        if (cursor == 1)
        {
            l = (int)strlen(nameBuf);
            if (l < 15)
            {
                nameBuf[l + 1] = 0;
                nameBuf[l] = k;
            }
        }
    }

    if (top > 13)
    {
        top = 0;
    }
    if (top < 0)
    {
        top = 13;
    }
    if (bottom > 13)
    {
        bottom = 0;
    }
    if (bottom < 0)
    {
        bottom = 13;
    }
}

//=============================================================================
/* NET MENU */

class NetMenu : public MenuScreen
{
  public:
    int cursor = 0;
    int items = 0;
    int saveHeight = 0;
    void Draw() override;
    void Key(int key) override;
};
NetMenu netMenu;

const char *net_helpMessage[] = {
    /* .........1.........2.... */
    " Novell network LANs    ", " or Windows 95 DOS-box. ", "                        ", "(LAN=Local Area Network)",

    " Commonly used to play  ", " over the Internet, but ", " also used on a Local   ", " Area Network.          "};

void M_Menu_Net_f(void)
{
    key_dest = keydest_t::key_menu;
    m_state = m_net;
    m_entersound = true;
    netMenu.items = 2;

    if (netMenu.cursor >= netMenu.items)
    {
        netMenu.cursor = 0;
    }
    netMenu.cursor--;
    netMenu.Key(K_DOWNARROW);
}

void NetMenu::Draw(void)
{
    int f;
    qpic_t *p;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/p_multi.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);

    f = 32;

    if (net.ipxAvailable)
    {
        p = Draw_CachePic("gfx/netmen3.lmp");
    }
    else
    {
        p = Draw_CachePic("gfx/dim_ipx.lmp");
    }
    M_DrawTransPic(72, f, p);

    f += 19;
    if (net.tcpipAvailable)
    {
        p = Draw_CachePic("gfx/netmen4.lmp");
    }
    else
    {
        p = Draw_CachePic("gfx/dim_tcp.lmp");
    }
    M_DrawTransPic(72, f, p);

    if (items == 5) // JDC, could just be removed
    {
        f += 19;
        p = Draw_CachePic("gfx/netmen5.lmp");
        M_DrawTransPic(72, f, p);
    }

    f = (320 - 26 * 8) / 2;
    M_DrawTextBox(f, 134, 24, 4);
    f += 8;
    M_Print(f, 142, net_helpMessage[cursor * 4 + 0]);
    M_Print(f, 150, net_helpMessage[cursor * 4 + 1]);
    M_Print(f, 158, net_helpMessage[cursor * 4 + 2]);
    M_Print(f, 166, net_helpMessage[cursor * 4 + 3]);

    f = (int)(host_time * 10) % 6;
    M_DrawTransPic(54, 32 + cursor * 20, Draw_CachePic(va("gfx/menudot{}.lmp", f + 1)));
}

void NetMenu::Key(int k)
{
again:
    switch (k)
    {
    case K_ESCAPE:
        M_Menu_MultiPlayer_f();
        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        if (++cursor >= items)
        {
            cursor = 0;
        }
        break;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        if (--cursor < 0)
        {
            cursor = items - 1;
        }
        break;

    case K_ENTER:
        m_entersound = true;

        switch (cursor)
        {
        case 0:
            M_Menu_LanConfig_f();
            break;

        case 1:
            M_Menu_LanConfig_f();
            break;

        case 4:
            // multiprotocol
            break;
        }
    }

    if (cursor == 0 && !net.ipxAvailable)
    {
        goto again;
    }
    if (cursor == 1 && !net.tcpipAvailable)
    {
        goto again;
    }
}

//=============================================================================
/* OPTIONS MENU */

#ifdef _WIN32
#define OPTIONS_ITEMS 14
#else
#define OPTIONS_ITEMS 13
#endif

#define SLIDER_RANGE 10

class OptionsMenu : public MenuScreen
{
  public:
    int cursor = 0;
    void Draw() override;
    void Key(int key) override;
    void AdjustSliders(int dir);
};
OptionsMenu optionsMenu;

void M_Menu_Options_f(void)
{
    key_dest = keydest_t::key_menu;
    m_state = m_options;
    m_entersound = true;

#ifdef _WIN32
    if ((optionsMenu.cursor == 13) && (modestate != MS_WINDOWED))
    {
        optionsMenu.cursor = 0;
    }
#endif
}

void OptionsMenu::AdjustSliders(int dir)
{
    S_LocalSound("misc/menu3.wav");

    switch (cursor)
    {
    case 3: // screen size
        scr_viewsize.value += dir * 10;
        if (scr_viewsize.value < 30)
        {
            scr_viewsize.value = 30;
        }
        if (scr_viewsize.value > 120)
        {
            scr_viewsize.value = 120;
        }
        Cvar_SetValue("viewsize", scr_viewsize.value);
        break;
    case 4: // gamma
        v_gamma.value -= dir * 0.05;
        if (v_gamma.value < 0.5)
        {
            v_gamma.value = 0.5;
        }
        if (v_gamma.value > 1)
        {
            v_gamma.value = 1;
        }
        Cvar_SetValue("gamma", v_gamma.value);
        break;
    case 5: // mouse speed
        sensitivity.value += dir * 0.5;
        if (sensitivity.value < 1)
        {
            sensitivity.value = 1;
        }
        if (sensitivity.value > 11)
        {
            sensitivity.value = 11;
        }
        Cvar_SetValue("sensitivity", sensitivity.value);
        break;
    case 6: // music volume
#ifdef _WIN32
        bgmvolume.value += dir * 1.0;
#else
        bgmvolume.value += dir * 0.1;
#endif
        if (bgmvolume.value < 0)
        {
            bgmvolume.value = 0;
        }
        if (bgmvolume.value > 1)
        {
            bgmvolume.value = 1;
        }
        Cvar_SetValue("bgmvolume", bgmvolume.value);
        break;
    case 7: // sfx volume
        volume.value += dir * 0.1;
        if (volume.value < 0)
        {
            volume.value = 0;
        }
        if (volume.value > 1)
        {
            volume.value = 1;
        }
        Cvar_SetValue("volume", volume.value);
        break;

    case 8: // allways run
        if (cl_forwardspeed.value > 200)
        {
            Cvar_SetValue("cl_forwardspeed", 200);
            Cvar_SetValue("cl_backspeed", 200);
        }
        else
        {
            Cvar_SetValue("cl_forwardspeed", 400);
            Cvar_SetValue("cl_backspeed", 400);
        }
        break;

    case 9: // invert mouse
        Cvar_SetValue("m_pitch", -m_pitch.value);
        break;

    case 10: // lookspring
        Cvar_SetValue("lookspring", !lookspring.value);
        break;

    case 11: // lookstrafe
        Cvar_SetValue("lookstrafe", !lookstrafe.value);
        break;

#ifdef _WIN32
    case 13: // _windowed_mouse
        Cvar_SetValue("_windowed_mouse", !_windowed_mouse.value);
        break;
#endif
    }
}

void M_DrawSlider(int x, int y, float range)
{
    int i;

    if (range < 0)
    {
        range = 0;
    }
    if (range > 1)
    {
        range = 1;
    }
    M_DrawCharacter(x - 8, y, 128);
    for (i = 0; i < SLIDER_RANGE; i++)
    {
        M_DrawCharacter(x + i * 8, y, 129);
    }
    M_DrawCharacter(x + i * 8, y, 130);
    M_DrawCharacter(x + (SLIDER_RANGE - 1) * 8 * range, y, 131);
}

void M_DrawCheckbox(int x, int y, int on)
{
#if 0
	if (on)
		M_DrawCharacter (x, y, 131);
	else
		M_DrawCharacter (x, y, 129);
#endif
    if (on)
    {
        M_Print(x, y, "on");
    }
    else
    {
        M_Print(x, y, "off");
    }
}

void OptionsMenu::Draw(void)
{
    float r;
    qpic_t *p;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/p_option.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);

    M_Print(16, 32, "    Customize controls");
    M_Print(16, 40, "         Go to console");
    M_Print(16, 48, "     Reset to defaults");

    M_Print(16, 56, "           Screen size");
    r = (scr_viewsize.value - 30) / (120 - 30);
    M_DrawSlider(220, 56, r);

    M_Print(16, 64, "            Brightness");
    r = (1.0 - v_gamma.value) / 0.5;
    M_DrawSlider(220, 64, r);

    M_Print(16, 72, "           Mouse Speed");
    r = (sensitivity.value - 1) / 10;
    M_DrawSlider(220, 72, r);

    M_Print(16, 80, "       CD Music Volume");
    r = bgmvolume.value;
    M_DrawSlider(220, 80, r);

    M_Print(16, 88, "          Sound Volume");
    r = volume.value;
    M_DrawSlider(220, 88, r);

    M_Print(16, 96, "            Always Run");
    M_DrawCheckbox(220, 96, cl_forwardspeed.value > 200);

    M_Print(16, 104, "          Invert Mouse");
    M_DrawCheckbox(220, 104, m_pitch.value < 0);

    M_Print(16, 112, "            Lookspring");
    M_DrawCheckbox(220, 112, lookspring.value);

    M_Print(16, 120, "            Lookstrafe");
    M_DrawCheckbox(220, 120, lookstrafe.value);

    if (vid_menudrawfn)
    {
        M_Print(16, 128, "         Video Options");
    }

#ifdef _WIN32
    if (modestate == MS_WINDOWED)
    {
        M_Print(16, 136, "             Use Mouse");
        M_DrawCheckbox(220, 136, _windowed_mouse.value);
    }
#endif

    // cursor
    M_DrawCharacter(200, 32 + cursor * 8, 12 + ((int)(realtime * 4) & 1));
}

void OptionsMenu::Key(int k)
{
    switch (k)
    {
    case K_ESCAPE:
        M_Menu_Main_f();
        break;

    case K_ENTER:
        m_entersound = true;
        switch (cursor)
        {
        case 0:
            M_Menu_Keys_f();
            break;
        case 1:
            m_state = m_none;
            Con_ToggleConsole_f();
            break;
        case 2:
            Cbuf_AddText("exec default.cfg\n");
            break;
        case 12:
            M_Menu_Video_f();
            break;
        default:
            AdjustSliders(1);
            break;
        }
        return;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        cursor--;
        if (cursor < 0)
        {
            cursor = OPTIONS_ITEMS - 1;
        }
        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        cursor++;
        if (cursor >= OPTIONS_ITEMS)
        {
            cursor = 0;
        }
        break;

    case K_LEFTARROW:
        AdjustSliders(-1);
        break;

    case K_RIGHTARROW:
        AdjustSliders(1);
        break;
    }

    if (cursor == 12 && vid_menudrawfn == nullptr)
    {
        if (k == K_UPARROW)
        {
            cursor = 11;
        }
        else
        {
            cursor = 0;
        }
    }

#ifdef _WIN32
    if ((cursor == 13) && (modestate != MS_WINDOWED))
    {
        if (k == K_UPARROW)
        {
            cursor = 12;
        }
        else
        {
            cursor = 0;
        }
    }
#endif
}

//=============================================================================
/* KEYS MENU */

const char *bindnames[][2] = {{"+attack", "attack"},       {"impulse 10", "change weapon"},
                              {"+jump", "jump / swim up"}, {"+forward", "walk forward"},
                              {"+back", "backpedal"},      {"+left", "turn left"},
                              {"+right", "turn right"},    {"+speed", "run"},
                              {"+moveleft", "step left"},  {"+moveright", "step right"},
                              {"+strafe", "sidestep"},     {"+lookup", "look up"},
                              {"+lookdown", "look down"},  {"centerview", "center view"},
                              {"+mlook", "mouse look"},    {"+klook", "keyboard look"},
                              {"+moveup", "swim up"},      {"+movedown", "swim down"}};

#define NUMCOMMANDS (sizeof(bindnames) / sizeof(bindnames[0]))

class KeysMenu : public MenuScreen
{
  public:
    int cursor = 0;
    int bindGrab = 0;
    void Draw() override;
    void Key(int key) override;
};
KeysMenu keysMenu;

void M_Menu_Keys_f(void)
{
    key_dest = keydest_t::key_menu;
    m_state = m_keys;
    m_entersound = true;
}

void KeysMenu::Draw(void)
{
    int i, l;
    int keys[2];
    std::string name;
    int x, y;
    qpic_t *p;

    p = Draw_CachePic("gfx/ttl_cstm.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);

    if (bindGrab)
    {
        M_Print(12, 32, "Press a key or button for this action");
    }
    else
    {
        M_Print(18, 32, "Enter to change, backspace to clear");
    }

    // search for known bindings
    for (i = 0; i < NUMCOMMANDS; i++)
    {
        y = 48 + 8 * i;

        M_Print(16, y, bindnames[i][1]);

        l = (int)strlen(bindnames[i][0]);

        Key_KeysForCommand(bindnames[i][0], keys);

        if (keys[0] == -1)
        {
            M_Print(140, y, "???");
        }
        else
        {
            name = Key_KeynumToString(keys[0]);
            M_Print(140, y, name.c_str());
            x = (int)name.length() * 8;
            if (keys[1] != -1)
            {
                M_Print(140 + x + 8, y, "or");
                M_Print(140 + x + 32, y, Key_KeynumToString(keys[1]).c_str());
            }
        }
    }

    if (bindGrab)
    {
        M_DrawCharacter(130, 48 + cursor * 8, '=');
    }
    else
    {
        M_DrawCharacter(130, 48 + cursor * 8, 12 + ((int)(realtime * 4) & 1));
    }
}

void KeysMenu::Key(int k)
{
    std::string cmd;
    int keys[2];

    if (bindGrab)
    { // defining a key
        S_LocalSound("misc/menu1.wav");
        if (k == K_ESCAPE)
        {
            bindGrab = false;
        }
        else if (k != '`')
        {
            cmd = std::format("bind \"{}\" \"{}\"\n", Key_KeynumToString(k), bindnames[cursor][0]);
            Cbuf_InsertText(cmd.c_str());
        }

        bindGrab = false;
        return;
    }

    switch (k)
    {
    case K_ESCAPE:
        M_Menu_Options_f();
        break;

    case K_LEFTARROW:
    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        cursor--;
        if (cursor < 0)
        {
            cursor = NUMCOMMANDS - 1;
        }
        break;

    case K_DOWNARROW:
    case K_RIGHTARROW:
        S_LocalSound("misc/menu1.wav");
        cursor++;
        if (cursor >= NUMCOMMANDS)
        {
            cursor = 0;
        }
        break;

    case K_ENTER: // go into bind mode
        Key_KeysForCommand(bindnames[cursor][0], keys);
        S_LocalSound("misc/menu2.wav");
        if (keys[1] != -1)
        {
            Key_UnbindCommand(bindnames[cursor][0]);
        }
        bindGrab = true;
        break;

    case K_BACKSPACE: // delete bindings
    case K_DEL:       // delete bindings
        S_LocalSound("misc/menu2.wav");
        Key_UnbindCommand(bindnames[cursor][0]);
        break;
    }
}

//=============================================================================
/* VIDEO MENU */

class VideoMenu : public MenuScreen
{
  public:
    void Draw() override;
    void Key(int key) override;
};
VideoMenu videoMenu;

void M_Menu_Video_f(void)
{
    key_dest = keydest_t::key_menu;
    m_state = m_video;
    m_entersound = true;
}

void VideoMenu::Draw(void)
{
    (*vid_menudrawfn)();
}

void VideoMenu::Key(int key)
{
    (*vid_menukeyfn)(key);
}

//=============================================================================
/* HELP MENU */

#define NUM_HELP_PAGES 6

class HelpMenu : public MenuScreen
{
  public:
    int page = 0;
    void Draw() override;
    void Key(int key) override;
};
HelpMenu helpMenu;

void M_Menu_Help_f(void)
{
    key_dest = keydest_t::key_menu;
    m_state = m_help;
    m_entersound = true;
    helpMenu.page = 0;
}

void HelpMenu::Draw(void)
{
    M_DrawPic(0, 0, Draw_CachePic(va("gfx/help{}.lmp", page)));
}

void HelpMenu::Key(int key)
{
    switch (key)
    {
    case K_ESCAPE:
        M_Menu_Main_f();
        break;

    case K_UPARROW:
    case K_RIGHTARROW:
        m_entersound = true;
        if (++page >= NUM_HELP_PAGES)
        {
            page = 0;
        }
        break;

    case K_DOWNARROW:
    case K_LEFTARROW:
        m_entersound = true;
        if (--page < 0)
        {
            page = NUM_HELP_PAGES - 1;
        }
        break;
    }
}

//=============================================================================
/* QUIT MENU */

int msgNumber;
class QuitMenu : public MenuScreen
{
  public:
    int prevState = 0;
    qboolean wasInMenus = false;
    void Draw() override;
    void Key(int key) override;
};
QuitMenu quitMenu;

#ifndef _WIN32
const char *quitMessage[] = {
    /* .........1.........2.... */
    "  Are you gonna quit    ", "  this game just like   ", "   everything else?     ", "                        ",

    " Milord, methinks that  ", "   thou art a lowly     ", " quitter. Is this true? ", "                        ",

    " Do I need to bust your ", "  face open for trying  ", "        to quit?        ", "                        ",

    " Man, I oughta smack you", "   for trying to quit!  ", "     Press Y to get     ", "      smacked out.      ",

    " Press Y to quit like a ", "   big loser in life.   ", "  Press N to stay proud ", "    and successful!     ",

    "   If you press Y to    ", "  quit, I will summon   ", "  Satan all over your   ", "      hard drive!       ",

    "  Um, Asmodeus dislikes ", " his children trying to ", " quit. Press Y to return", "   to your Tinkertoys.  ",

    "  If you quit now, I'll ", "  throw a blanket-party ", "   for you next time!   ", "                        "};
#endif

void M_Menu_Quit_f(void)
{
    if (m_state == m_quit)
    {
        return;
    }
    quitMenu.wasInMenus = (key_dest == keydest_t::key_menu);
    key_dest = keydest_t::key_menu;
    quitMenu.prevState = m_state;
    m_state = m_quit;
    m_entersound = true;
    msgNumber = rand() & 7;
}

void QuitMenu::Key(int key)
{
    switch (key)
    {
    case K_ESCAPE:
    case 'n':
    case 'N':
        if (wasInMenus)
        {
            m_state = (decltype(m_state))prevState;
            m_entersound = true;
        }
        else
        {
            key_dest = keydest_t::key_game;
            m_state = m_none;
        }
        break;

    case 'Y':
    case 'y':
        key_dest = keydest_t::key_console;
        Host_Quit_f();
        break;

    default:
        break;
    }
}

void QuitMenu::Draw(void)
{
    if (wasInMenus)
    {
        m_state = (decltype(m_state))prevState;
        m_recursiveDraw = true;
        M_Draw();
        m_state = m_quit;
    }

#ifdef _WIN32
    M_DrawTextBox(0, 0, 38, 23);
    M_PrintWhite(16, 12, "  Quake version 1.09 by id Software\n\n");
    M_PrintWhite(16, 28, "Programming        Art \n");
    M_Print(16, 36, " John Carmack       Adrian Carmack\n");
    M_Print(16, 44, " Michael Abrash     Kevin Cloud\n");
    M_Print(16, 52, " John Cash          Paul Steed\n");
    M_Print(16, 60, " Dave 'Zoid' Kirsch\n");
    M_PrintWhite(16, 68, "Design             Biz\n");
    M_Print(16, 76, " John Romero        Jay Wilbur\n");
    M_Print(16, 84, " Sandy Petersen     Mike Wilson\n");
    M_Print(16, 92, " American McGee     Donna Jackson\n");
    M_Print(16, 100, " Tim Willits        Todd Hollenshead\n");
    M_PrintWhite(16, 108, "Support            Projects\n");
    M_Print(16, 116, " Barrett Alexander  Shawn Green\n");
    M_PrintWhite(16, 124, "Sound Effects\n");
    M_Print(16, 132, " Trent Reznor and Nine Inch Nails\n\n");
    M_PrintWhite(16, 140, "Quake is a trademark of Id Software,\n");
    M_PrintWhite(16, 148, "inc., (c)1996 Id Software, inc. All\n");
    M_PrintWhite(16, 156, "rights reserved. NIN logo is a\n");
    M_PrintWhite(16, 164, "registered trademark licensed to\n");
    M_PrintWhite(16, 172, "Nothing Interactive, Inc. All rights\n");
    M_PrintWhite(16, 180, "reserved. Press y to exit\n");
#else
    M_DrawTextBox(56, 76, 24, 4);
    M_Print(64, 84, quitMessage[msgNumber * 4 + 0]);
    M_Print(64, 92, quitMessage[msgNumber * 4 + 1]);
    M_Print(64, 100, quitMessage[msgNumber * 4 + 2]);
    M_Print(64, 108, quitMessage[msgNumber * 4 + 3]);
#endif
}

//=============================================================================
/* LAN CONFIG MENU */

int lanConfig_cursor_table[] = {72, 92, 124};
#define NUM_LANCONFIG_CMDS 3

class LanConfigMenu : public MenuScreen
{
  public:
    int cursor = -1;
    int port = 0;
    char portname[6] = {0};
    char joinname[22] = {0};
    void Draw() override;
    void Key(int key) override;
};
LanConfigMenu lanConfigMenu;

void M_Menu_LanConfig_f(void)
{
    key_dest = keydest_t::key_menu;
    m_state = m_lanconfig;
    m_entersound = true;
    if (lanConfigMenu.cursor == -1)
    {
        if (JoiningGame && TCPIPConfig)
        {
            lanConfigMenu.cursor = 2;
        }
        else
        {
            lanConfigMenu.cursor = 1;
        }
    }
    if (StartingGame && lanConfigMenu.cursor == 2)
    {
        lanConfigMenu.cursor = 1;
    }
    lanConfigMenu.port = DEFAULTnet_hostport;
    sprintf(lanConfigMenu.portname, "%u", lanConfigMenu.port);

    m_return_onerror = false;
    m_return_reason[0] = 0;
}

void LanConfigMenu::Draw(void)
{
    qpic_t *p;
    int basex;
    const char *startJoin;
    const char *protocol;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/p_multi.lmp");
    basex = (320 - p->width) / 2;
    M_DrawPic(basex, 4, p);

    if (StartingGame)
    {
        startJoin = "New Game";
    }
    else
    {
        startJoin = "Join Game";
    }
    if (IPXConfig)
    {
        protocol = "IPX";
    }
    else
    {
        protocol = "TCP/IP";
    }
    M_Print(basex, 32, va("{} - {}", startJoin, protocol));
    basex += 8;

    M_Print(basex, 52, "Address:");
    if (IPXConfig)
    {
        M_Print(basex + 9 * 8, 52, NET_IPXAddressString().c_str());
    }
    else
    {
        M_Print(basex + 9 * 8, 52, NET_TCPIPAddressString().c_str());
    }

    M_Print(basex, lanConfig_cursor_table[0], "Port");
    M_DrawTextBox(basex + 8 * 8, lanConfig_cursor_table[0] - 8, 6, 1);
    M_Print(basex + 9 * 8, lanConfig_cursor_table[0], portname);

    if (JoiningGame)
    {
        M_Print(basex, lanConfig_cursor_table[1], "Search for local games...");
        M_Print(basex, 108, "Join game at:");
        M_DrawTextBox(basex + 8, lanConfig_cursor_table[2] - 8, 22, 1);
        M_Print(basex + 16, lanConfig_cursor_table[2], joinname);
    }
    else
    {
        M_DrawTextBox(basex, lanConfig_cursor_table[1] - 8, 2, 1);
        M_Print(basex + 8, lanConfig_cursor_table[1], "OK");
    }

    M_DrawCharacter(basex - 8, lanConfig_cursor_table[cursor], 12 + ((int)(realtime * 4) & 1));

    if (cursor == 0)
    {
        M_DrawCharacter(basex + 9 * 8 + 8 * (int)strlen(portname), lanConfig_cursor_table[0],
                        10 + ((int)(realtime * 4) & 1));
    }

    if (cursor == 2)
    {
        M_DrawCharacter(basex + 16 + 8 * (int)strlen(joinname), lanConfig_cursor_table[2],
                        10 + ((int)(realtime * 4) & 1));
    }

    if (*m_return_reason)
    {
        M_PrintWhite(basex, 148, m_return_reason);
    }
}

void LanConfigMenu::Key(int key)
{
    int l;

    switch (key)
    {
    case K_ESCAPE:
        M_Menu_Net_f();
        break;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        cursor--;
        if (cursor < 0)
        {
            cursor = NUM_LANCONFIG_CMDS - 1;
        }
        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        cursor++;
        if (cursor >= NUM_LANCONFIG_CMDS)
        {
            cursor = 0;
        }
        break;

    case K_ENTER:
        if (cursor == 0)
        {
            break;
        }

        m_entersound = true;

        M_ConfigureNetSubsystem();

        if (cursor == 1)
        {
            if (StartingGame)
            {
                M_Menu_GameOptions_f();
                break;
            }
            M_Menu_Search_f();
            break;
        }

        if (cursor == 2)
        {
            m_return_state = m_state;
            m_return_onerror = true;
            key_dest = keydest_t::key_game;
            m_state = m_none;
            Cbuf_AddText(va("connect \"{}\"\n", joinname));
            break;
        }

        break;

    case K_BACKSPACE:
        if (cursor == 0)
        {
            if (strlen(portname))
            {
                portname[strlen(portname) - 1] = 0;
            }
        }

        if (cursor == 2)
        {
            if (strlen(joinname))
            {
                joinname[strlen(joinname) - 1] = 0;
            }
        }
        break;

    default:
        if (key < 32 || key > 127)
        {
            break;
        }

        if (cursor == 2)
        {
            l = (int)strlen(joinname);
            if (l < 21)
            {
                joinname[l + 1] = 0;
                joinname[l] = key;
            }
        }

        if (key < '0' || key > '9')
        {
            break;
        }
        if (cursor == 0)
        {
            l = (int)strlen(portname);
            if (l < 5)
            {
                portname[l + 1] = 0;
                portname[l] = key;
            }
        }
    }

    if (StartingGame && cursor == 2)
    {
        if (key == K_UPARROW)
        {
            cursor = 1;
        }
        else
        {
            cursor = 0;
        }
    }

    l = Q_atoi(portname);
    if (l > 65535)
    {
        l = port;
    }
    else
    {
        port = l;
    }
    sprintf(portname, "%u", port);
}

//=============================================================================
/* GAME OPTIONS MENU */

struct level_t
{
    const char *name;
    const char *description;
};

level_t levels[] = {{"start", "Entrance"}, // 0

                    {"e1m1", "Slipgate Complex"}, // 1
                    {"e1m2", "Castle of the Damned"},
                    {"e1m3", "The Necropolis"},
                    {"e1m4", "The Grisly Grotto"},
                    {"e1m5", "Gloom Keep"},
                    {"e1m6", "The Door To Chthon"},
                    {"e1m7", "The House of Chthon"},
                    {"e1m8", "Ziggurat Vertigo"},

                    {"e2m1", "The Installation"}, // 9
                    {"e2m2", "Ogre Citadel"},
                    {"e2m3", "Crypt of Decay"},
                    {"e2m4", "The Ebon Fortress"},
                    {"e2m5", "The Wizard's Manse"},
                    {"e2m6", "The Dismal Oubliette"},
                    {"e2m7", "Underearth"},

                    {"e3m1", "Termination Central"}, // 16
                    {"e3m2", "The Vaults of Zin"},
                    {"e3m3", "The Tomb of Terror"},
                    {"e3m4", "Satan's Dark Delight"},
                    {"e3m5", "Wind Tunnels"},
                    {"e3m6", "Chambers of Torment"},
                    {"e3m7", "The Haunted Halls"},

                    {"e4m1", "The Sewage System"}, // 23
                    {"e4m2", "The Tower of Despair"},
                    {"e4m3", "The Elder God Shrine"},
                    {"e4m4", "The Palace of Hate"},
                    {"e4m5", "Hell's Atrium"},
                    {"e4m6", "The Pain Maze"},
                    {"e4m7", "Azure Agony"},
                    {"e4m8", "The Nameless City"},

                    {"end", "Shub-Niggurath's Pit"}, // 31

                    {"dm1", "Place of Two Deaths"}, // 32
                    {"dm2", "Claustrophobopolis"},
                    {"dm3", "The Abandoned Base"},
                    {"dm4", "The Bad Place"},
                    {"dm5", "The Cistern"},
                    {"dm6", "The Dark Zone"}};

// MED 01/06/97 added hipnotic levels
level_t hipnoticlevels[] = {
    {"start", "Command HQ"}, // 0

    {"hip1m1", "The Pumping Station"}, // 1
    {"hip1m2", "Storage Facility"},
    {"hip1m3", "The Lost Mine"},
    {"hip1m4", "Research Facility"},
    {"hip1m5", "Military Complex"},

    {"hip2m1", "Ancient Realms"}, // 6
    {"hip2m2", "The Black Cathedral"},
    {"hip2m3", "The Catacombs"},
    {"hip2m4", "The Crypt"},
    {"hip2m5", "Mortum's Keep"},
    {"hip2m6", "The Gremlin's Domain"},

    {"hip3m1", "Tur Torment"}, // 12
    {"hip3m2", "Pandemonium"},
    {"hip3m3", "Limbo"},
    {"hip3m4", "The Gauntlet"},

    {"hipend", "Armagon's Lair"}, // 16

    {"hipdm1", "The Edge of Oblivion"} // 17
};

// PGM 01/07/97 added rogue levels
// PGM 03/02/97 added dmatch level
level_t roguelevels[] = {{"start", "Split Decision"},  {"r1m1", "Deviant's Domain"},     {"r1m2", "Dread Portal"},
                         {"r1m3", "Judgement Call"},   {"r1m4", "Cave of Death"},        {"r1m5", "Towers of Wrath"},
                         {"r1m6", "Temple of Pain"},   {"r1m7", "Tomb of the Overlord"}, {"r2m1", "Tempus Fugit"},
                         {"r2m2", "Elemental Fury I"}, {"r2m3", "Elemental Fury II"},    {"r2m4", "Curse of Osiris"},
                         {"r2m5", "Wizard's Keep"},    {"r2m6", "Blood Sacrifice"},      {"r2m7", "Last Bastion"},
                         {"r2m8", "Source of Evil"},   {"ctf1", "Division of Change"}};

struct episode_t
{
    const char *description;
    int firstLevel;
    int levels;
};

episode_t episodes[] = {{"Welcome to Quake", 0, 1}, {"Doomed Dimension", 1, 8}, {"Realm of Black Magic", 9, 7},
                        {"Netherworld", 16, 7},     {"The Elder World", 23, 8}, {"Final Level", 31, 1},
                        {"Deathmatch Arena", 32, 6}};

// MED 01/06/97  added hipnotic episodes
episode_t hipnoticepisodes[] = {{"Scourge of Armagon", 0, 1},   {"Fortress of the Dead", 1, 5},
                                {"Dominion of Darkness", 6, 6}, {"The Rift", 12, 4},
                                {"Final Level", 16, 1},         {"Deathmatch Arena", 17, 1}};

// PGM 01/07/97 added rogue episodes
// PGM 03/02/97 added dmatch episode
episode_t rogueepisodes[] = {
    {"Introduction", 0, 1}, {"Hell's Fortress", 1, 7}, {"Corridors of Time", 8, 8}, {"Deathmatch Arena", 16, 1}};

int gameoptions_cursor_table[] = {40, 56, 64, 72, 80, 88, 96, 112, 120};
#define NUM_GAMEOPTIONS 9

class GameOptionsMenu : public MenuScreen
{
  public:
    int cursor = 0;
    int startepisode = 0;
    int startlevel = 0;
    int maxplayers = 0;
    qboolean serverInfoMessage = false;
    double serverInfoMessageTime = 0;
    void Draw() override;
    void Key(int key) override;
    void NetStartChange(int dir);
};
GameOptionsMenu gameOptionsMenu;

void M_Menu_GameOptions_f(void)
{
    key_dest = keydest_t::key_menu;
    m_state = m_gameoptions;
    m_entersound = true;
    if (gameOptionsMenu.maxplayers == 0)
    {
        gameOptionsMenu.maxplayers = SV_NumClients();
    }
    if (gameOptionsMenu.maxplayers < 2)
    {
        gameOptionsMenu.maxplayers = SV_MaxClientsLimit();
    }
}

void GameOptionsMenu::Draw(void)
{
    qpic_t *p;
    int x;

    M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
    p = Draw_CachePic("gfx/p_multi.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);

    M_DrawTextBox(152, 32, 10, 1);
    M_Print(160, 40, "begin game");

    M_Print(0, 56, "      Max players");
    M_Print(160, 56, va("{}", maxplayers));

    M_Print(0, 64, "        Game Type");
    if (coop.value)
    {
        M_Print(160, 64, "Cooperative");
    }
    else
    {
        M_Print(160, 64, "Deathmatch");
    }

    M_Print(0, 72, "        Teamplay");
    if (rogue)
    {
        const char *msg;

        switch ((int)teamplay.value)
        {
        case 1:
            msg = "No Friendly Fire";
            break;
        case 2:
            msg = "Friendly Fire";
            break;
        case 3:
            msg = "Tag";
            break;
        case 4:
            msg = "Capture the Flag";
            break;
        case 5:
            msg = "One Flag CTF";
            break;
        case 6:
            msg = "Three Team CTF";
            break;
        default:
            msg = "Off";
            break;
        }
        M_Print(160, 72, msg);
    }
    else
    {
        const char *msg;

        switch ((int)teamplay.value)
        {
        case 1:
            msg = "No Friendly Fire";
            break;
        case 2:
            msg = "Friendly Fire";
            break;
        default:
            msg = "Off";
            break;
        }
        M_Print(160, 72, msg);
    }

    M_Print(0, 80, "            Skill");
    if (skill.value == 0)
    {
        M_Print(160, 80, "Easy difficulty");
    }
    else if (skill.value == 1)
    {
        M_Print(160, 80, "Normal difficulty");
    }
    else if (skill.value == 2)
    {
        M_Print(160, 80, "Hard difficulty");
    }
    else
    {
        M_Print(160, 80, "Nightmare difficulty");
    }

    M_Print(0, 88, "       Frag Limit");
    if (fraglimit.value == 0)
    {
        M_Print(160, 88, "none");
    }
    else
    {
        M_Print(160, 88, va("{} frags", (int)fraglimit.value));
    }

    M_Print(0, 96, "       Time Limit");
    if (timelimit.value == 0)
    {
        M_Print(160, 96, "none");
    }
    else
    {
        M_Print(160, 96, va("{} minutes", (int)timelimit.value));
    }

    M_Print(0, 112, "         Episode");
    // MED 01/06/97 added hipnotic episodes
    if (hipnotic)
    {
        M_Print(160, 112, hipnoticepisodes[startepisode].description);
    }
    // PGM 01/07/97 added rogue episodes
    else if (rogue)
    {
        M_Print(160, 112, rogueepisodes[startepisode].description);
    }
    else
    {
        M_Print(160, 112, episodes[startepisode].description);
    }

    M_Print(0, 120, "           Level");
    // MED 01/06/97 added hipnotic episodes
    if (hipnotic)
    {
        M_Print(160, 120, hipnoticlevels[hipnoticepisodes[startepisode].firstLevel + startlevel].description);
        M_Print(160, 128, hipnoticlevels[hipnoticepisodes[startepisode].firstLevel + startlevel].name);
    }
    // PGM 01/07/97 added rogue episodes
    else if (rogue)
    {
        M_Print(160, 120, roguelevels[rogueepisodes[startepisode].firstLevel + startlevel].description);
        M_Print(160, 128, roguelevels[rogueepisodes[startepisode].firstLevel + startlevel].name);
    }
    else
    {
        M_Print(160, 120, levels[episodes[startepisode].firstLevel + startlevel].description);
        M_Print(160, 128, levels[episodes[startepisode].firstLevel + startlevel].name);
    }

    // line cursor
    M_DrawCharacter(144, gameoptions_cursor_table[cursor], 12 + ((int)(realtime * 4) & 1));

    if (serverInfoMessage)
    {
        if ((realtime - serverInfoMessageTime) < 5.0)
        {
            x = (320 - 26 * 8) / 2;
            M_DrawTextBox(x, 138, 24, 4);
            x += 8;
            M_Print(x, 146, "  More than 4 players   ");
            M_Print(x, 154, " requires using command ");
            M_Print(x, 162, "line parameters; please ");
            M_Print(x, 170, "   see techinfo.txt.    ");
        }
        else
        {
            serverInfoMessage = false;
        }
    }
}

void GameOptionsMenu::NetStartChange(int dir)
{
    int count;

    switch (cursor)
    {
    case 1:
        maxplayers += dir;
        if (maxplayers > SV_MaxClientsLimit())
        {
            maxplayers = SV_MaxClientsLimit();
            serverInfoMessage = true;
            serverInfoMessageTime = realtime;
        }
        if (maxplayers < 2)
        {
            maxplayers = 2;
        }
        break;

    case 2:
        Cvar_SetValue("coop", coop.value ? 0 : 1);
        break;

    case 3:
        if (rogue)
        {
            count = 6;
        }
        else
        {
            count = 2;
        }

        Cvar_SetValue("teamplay", teamplay.value + dir);
        if (teamplay.value > count)
        {
            Cvar_SetValue("teamplay", 0);
        }
        else if (teamplay.value < 0)
        {
            Cvar_SetValue("teamplay", count);
        }
        break;

    case 4:
        Cvar_SetValue("skill", skill.value + dir);
        if (skill.value > 3)
        {
            Cvar_SetValue("skill", 0);
        }
        if (skill.value < 0)
        {
            Cvar_SetValue("skill", 3);
        }
        break;

    case 5:
        Cvar_SetValue("fraglimit", fraglimit.value + dir * 10);
        if (fraglimit.value > 100)
        {
            Cvar_SetValue("fraglimit", 0);
        }
        if (fraglimit.value < 0)
        {
            Cvar_SetValue("fraglimit", 100);
        }
        break;

    case 6:
        Cvar_SetValue("timelimit", timelimit.value + dir * 5);
        if (timelimit.value > 60)
        {
            Cvar_SetValue("timelimit", 0);
        }
        if (timelimit.value < 0)
        {
            Cvar_SetValue("timelimit", 60);
        }
        break;

    case 7:
        startepisode += dir;
        // MED 01/06/97 added hipnotic count
        if (hipnotic)
        {
            count = 6;
        }
        // PGM 01/07/97 added rogue count
        // PGM 03/02/97 added 1 for dmatch episode
        else if (rogue)
        {
            count = 4;
        }
        else if (registered.value)
        {
            count = 7;
        }
        else
        {
            count = 2;
        }

        if (startepisode < 0)
        {
            startepisode = count - 1;
        }

        if (startepisode >= count)
        {
            startepisode = 0;
        }

        startlevel = 0;
        break;

    case 8:
        startlevel += dir;
        // MED 01/06/97 added hipnotic episodes
        if (hipnotic)
        {
            count = hipnoticepisodes[startepisode].levels;
        }
        // PGM 01/06/97 added hipnotic episodes
        else if (rogue)
        {
            count = rogueepisodes[startepisode].levels;
        }
        else
        {
            count = episodes[startepisode].levels;
        }

        if (startlevel < 0)
        {
            startlevel = count - 1;
        }

        if (startlevel >= count)
        {
            startlevel = 0;
        }
        break;
    }
}

void GameOptionsMenu::Key(int key)
{
    switch (key)
    {
    case K_ESCAPE:
        M_Menu_Net_f();
        break;

    case K_UPARROW:
        S_LocalSound("misc/menu1.wav");
        cursor--;
        if (cursor < 0)
        {
            cursor = NUM_GAMEOPTIONS - 1;
        }
        break;

    case K_DOWNARROW:
        S_LocalSound("misc/menu1.wav");
        cursor++;
        if (cursor >= NUM_GAMEOPTIONS)
        {
            cursor = 0;
        }
        break;

    case K_LEFTARROW:
        if (cursor == 0)
        {
            break;
        }
        S_LocalSound("misc/menu3.wav");
        NetStartChange(-1);
        break;

    case K_RIGHTARROW:
        if (cursor == 0)
        {
            break;
        }
        S_LocalSound("misc/menu3.wav");
        NetStartChange(1);
        break;

    case K_ENTER:
        S_LocalSound("misc/menu2.wav");
        if (cursor == 0)
        {
            if (SV_Active())
            {
                Cbuf_AddText("disconnect\n");
            }
            Cbuf_AddText("listen 0\n"); // so host_netport will be re-examined
            Cbuf_AddText(va("maxplayers {}\n", maxplayers));
            SCR_BeginLoadingPlaque();

            if (hipnotic)
            {
                Cbuf_AddText(
                    va("map {}\n", hipnoticlevels[hipnoticepisodes[startepisode].firstLevel + startlevel].name));
            }
            else if (rogue)
            {
                Cbuf_AddText(va("map {}\n", roguelevels[rogueepisodes[startepisode].firstLevel + startlevel].name));
            }
            else
            {
                Cbuf_AddText(va("map {}\n", levels[episodes[startepisode].firstLevel + startlevel].name));
            }

            return;
        }

        NetStartChange(1);
        break;
    }
}

//=============================================================================
/* SEARCH MENU */

class SearchMenu : public MenuScreen
{
  public:
    qboolean complete = false;
    double completeTime = 0;
    void Draw() override;
    void Key(int key) override;
};
SearchMenu searchMenu;

void M_Menu_Search_f(void)
{
    key_dest = keydest_t::key_menu;
    m_state = m_search;
    m_entersound = false;
    slistSilent = true;
    slistLocal = false;
    searchMenu.complete = false;
    NET_Slist_f();
}

void SearchMenu::Draw(void)
{
    qpic_t *p;
    int x;

    p = Draw_CachePic("gfx/p_multi.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);
    x = (320 / 2) - ((12 * 8) / 2) + 4;
    M_DrawTextBox(x - 8, 32, 12, 1);
    M_Print(x, 40, "Searching...");

    if (slistInProgress)
    {
        NET_Poll();
        return;
    }

    if (!complete)
    {
        complete = true;
        completeTime = realtime;
    }

    if (hostCacheCount)
    {
        M_Menu_ServerList_f();
        return;
    }

    M_PrintWhite((320 / 2) - ((22 * 8) / 2), 64, "No Quake servers found");
    if ((realtime - completeTime) < 3.0)
    {
        return;
    }

    M_Menu_LanConfig_f();
}

void SearchMenu::Key(int key)
{
}

//=============================================================================
/* SLIST MENU */

class ServerListMenu : public MenuScreen
{
  public:
    int cursor = 0;
    qboolean sorted = false;
    void Draw() override;
    void Key(int key) override;
};
ServerListMenu serverListMenu;

void M_Menu_ServerList_f(void)
{
    key_dest = keydest_t::key_menu;
    m_state = m_slist;
    m_entersound = true;
    serverListMenu.cursor = 0;
    m_return_onerror = false;
    m_return_reason[0] = 0;
    serverListMenu.sorted = false;
}

void ServerListMenu::Draw(void)
{
    int n;
    std::string string;
    qpic_t *p;

    if (!sorted)
    {
        if (hostCacheCount > 1)
        {
            int i, j;
            hostcache_t temp;
            for (i = 0; i < hostCacheCount; i++)
            {
                for (j = i + 1; j < hostCacheCount; j++)
                {
                    if (strcmp(hostcache[j].name, hostcache[i].name) < 0)
                    {
                        Q_memcpy(&temp, &hostcache[j], sizeof(hostcache_t));
                        Q_memcpy(&hostcache[j], &hostcache[i], sizeof(hostcache_t));
                        Q_memcpy(&hostcache[i], &temp, sizeof(hostcache_t));
                    }
                }
            }
        }
        sorted = true;
    }

    p = Draw_CachePic("gfx/p_multi.lmp");
    M_DrawPic((320 - p->width) / 2, 4, p);
    for (n = 0; n < hostCacheCount; n++)
    {
        if (hostcache[n].maxusers)
        {
            string = std::format("{:<15.15} {:<15.15} {:2}/{:2}\n", hostcache[n].name, hostcache[n].map,
                                 hostcache[n].users, hostcache[n].maxusers);
        }
        else
        {
            string = std::format("{:<15.15} {:<15.15}\n", hostcache[n].name, hostcache[n].map);
        }
        M_Print(16, 32 + 8 * n, string.c_str());
    }
    M_DrawCharacter(0, 32 + cursor * 8, 12 + ((int)(realtime * 4) & 1));

    if (*m_return_reason)
    {
        M_PrintWhite(16, 148, m_return_reason);
    }
}

void ServerListMenu::Key(int k)
{
    switch (k)
    {
    case K_ESCAPE:
        M_Menu_LanConfig_f();
        break;

    case K_SPACE:
        M_Menu_Search_f();
        break;

    case K_UPARROW:
    case K_LEFTARROW:
        S_LocalSound("misc/menu1.wav");
        cursor--;
        if (cursor < 0)
        {
            cursor = hostCacheCount - 1;
        }
        break;

    case K_DOWNARROW:
    case K_RIGHTARROW:
        S_LocalSound("misc/menu1.wav");
        cursor++;
        if (cursor >= hostCacheCount)
        {
            cursor = 0;
        }
        break;

    case K_ENTER:
        S_LocalSound("misc/menu2.wav");
        m_return_state = m_state;
        m_return_onerror = true;
        sorted = false;
        key_dest = keydest_t::key_game;
        m_state = m_none;
        Cbuf_AddText(va("connect \"{}\"\n", hostcache[cursor].cname));
        break;

    default:
        break;
    }
}

//=============================================================================
/* Menu Subsystem */

void M_Init(void)
{
    Cmd_AddCommand("togglemenu", M_ToggleMenu_f);

    Cmd_AddCommand("menu_main", M_Menu_Main_f);
    Cmd_AddCommand("menu_singleplayer", M_Menu_SinglePlayer_f);
    Cmd_AddCommand("menu_load", M_Menu_Load_f);
    Cmd_AddCommand("menu_save", M_Menu_Save_f);
    Cmd_AddCommand("menu_multiplayer", M_Menu_MultiPlayer_f);
    Cmd_AddCommand("menu_setup", M_Menu_Setup_f);
    Cmd_AddCommand("menu_options", M_Menu_Options_f);
    Cmd_AddCommand("menu_keys", M_Menu_Keys_f);
    Cmd_AddCommand("menu_video", M_Menu_Video_f);
    Cmd_AddCommand("help", M_Menu_Help_f);
    Cmd_AddCommand("menu_quit", M_Menu_Quit_f);
}

// Resolves m_state to the corresponding MenuScreen object. Only screens
// converted to the MenuScreen hierarchy so far have a case here; the
// remaining ones fall through to nullptr, and M_Draw()/M_Keydown() fall
// back to the old per-screen free functions for those until they're
// converted too (temporary scaffolding, removed once all screens are
// converted).
// All 18 screens are now MenuScreen subtypes; this is the sole dispatch
// point resolving m_state (still the authoritative, externally-writable
// dispatch key -- see the MenuScreen class comment) to the active screen.
MenuScreen *M_ScreenForState(m_state_t state)
{
    switch (state)
    {
    case m_main:
        return &mainMenu;
    case m_singleplayer:
        return &singlePlayerMenu;
    case m_load:
        return &loadGameMenu;
    case m_save:
        return &saveMenu;
    case m_multiplayer:
        return &multiPlayerMenu;
    case m_setup:
        return &setupMenu;
    case m_net:
        return &netMenu;
    case m_options:
        return &optionsMenu;
    case m_keys:
        return &keysMenu;
    case m_video:
        return &videoMenu;
    case m_help:
        return &helpMenu;
    case m_quit:
        return &quitMenu;
    case m_lanconfig:
        return &lanConfigMenu;
    case m_gameoptions:
        return &gameOptionsMenu;
    case m_search:
        return &searchMenu;
    case m_slist:
        return &serverListMenu;
    default:
        return nullptr; // m_none
    }
}

void M_Draw(void)
{
    if (m_state == m_none || key_dest != keydest_t::key_menu)
    {
        return;
    }

    if (!m_recursiveDraw)
    {
        scr_copyeverything = 1;

        if (scr_con_current)
        {
            Draw_ConsoleBackground(vid.height);
            VID_UnlockBuffer();
            S_ExtraUpdate();
            VID_LockBuffer();
        }
        else
        {
            Draw_FadeScreen();
        }

        scr_fullupdate = 0;
    }
    else
    {
        m_recursiveDraw = false;
    }

    MenuScreen *screen = M_ScreenForState(m_state);
    if (screen)
    {
        screen->Draw();
    }

    if (m_entersound)
    {
        S_LocalSound("misc/menu2.wav");
        m_entersound = false;
    }

    VID_UnlockBuffer();
    S_ExtraUpdate();
    VID_LockBuffer();
}

void M_Keydown(int key)
{
    MenuScreen *screen = M_ScreenForState(m_state);
    if (screen)
    {
        screen->Key(key);
    }
}

void M_ConfigureNetSubsystem(void)
{
    // enable/disable net systems to match desired config

    Cbuf_AddText("stopdemo\n");

    if (IPXConfig || TCPIPConfig)
    {
        net_hostport = lanConfigMenu.port;
    }
}
