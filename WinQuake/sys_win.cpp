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
// sys_win.c -- Win32 system interface code

#include "quakedef.h"
#include "winquake.h"
#include "errno.h"
#include "resource.h"
#include "conproc.h"
#include <direct.h>

#define MINIMUM_WIN_MEMORY 0x0880000
#define MAXIMUM_WIN_MEMORY 0x1000000

#define CONSOLE_ERROR_TIMEOUT                                                                                          \
    60.0                   // # of seconds to wait on Sys_Error running
                           //  dedicated before exiting
#define PAUSE_SLEEP 50     // sleep time on pause or minimization
#define NOT_FOCUS_SLEEP 20 // sleep time when not focus

int starttime;
qboolean ActiveApp, Minimized;

/*
==================
Sys_EnableDpiAwareness

Requests per-monitor DPI awareness so the window renders at native
resolution on high-DPI displays instead of being bitmap-stretched by
Windows. Resolved dynamically rather than statically linked, since
SetProcessDpiAwarenessContext only exists on Windows 10 1607+ and a
static import would fail to load at all on older systems; a missing
entry point here just means DPI awareness is silently skipped, same
class of graceful-degradation already used for wglCreateContextAttribsARB
and DirectInput elsewhere in this codebase. Must run before any window
is created, so this is called first thing in WinMain.
==================
*/
typedef BOOL(WINAPI *SETPROCESSDPIAWARENESSCONTEXTPROC)(HANDLE);
#define QUAKE_DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE) - 4)

static void Sys_EnableDpiAwareness(void)
{
    HMODULE user32 = GetModuleHandle("user32.dll");
    if (!user32)
    {
        return;
    }

    SETPROCESSDPIAWARENESSCONTEXTPROC pSetProcessDpiAwarenessContext =
        (SETPROCESSDPIAWARENESSCONTEXTPROC)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    if (pSetProcessDpiAwarenessContext)
    {
        pSetProcessDpiAwarenessContext(QUAKE_DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
}

static double pfreq;
static double curtime = 0.0;
static double lastcurtime = 0.0;
static int lowshift;
qboolean isDedicated;
static qboolean sc_return_on_enter = false;
static HANDLE hinput, houtput;

static const char *tracking_tag = "Clams & Mooses";

static HANDLE tevent;
static HANDLE hFile;
static HANDLE heventParent;
static HANDLE heventChild;

void MaskExceptions(void);
void Sys_InitFloatTime(void);
void Sys_PushFPCW_SetHigh(void);
void Sys_PopFPCW(void);

volatile int sys_checksum;

/*
================
Sys_PageIn
================
*/
void Sys_PageIn(void *ptr, size_t size)
{
    byte *x;
    size_t m, n;

    // touch all the memory to make sure it's there. The 16-page skip is to
    // keep Win 95 from thinking we're trying to page ourselves in (we are
    // doing that, of course, but there's no reason we shouldn't)
    x = static_cast<byte *>(ptr);

    for (n = 0; n < 4; n++)
    {
        for (m = 0; m < (size - 16 * 0x1000); m += 4)
        {
            sys_checksum += *reinterpret_cast<int *>(&x[m]);
            sys_checksum += *reinterpret_cast<int *>(&x[m + 16 * 0x1000]);
        }
    }
}

/*
===============================================================================

FILE IO

===============================================================================
*/

#define MAX_HANDLES 10
FILE *sys_handles[MAX_HANDLES];

int findhandle(void)
{
    int i;

    for (i = 1; i < MAX_HANDLES; i++)
    {
        if (!sys_handles[i])
        {
            return i;
        }
    }
    Sys_Error("out of handles");
    return -1;
}

/*
================
filelength
================
*/
int filelength(FILE *f)
{
    int pos;
    int end;
    int t;

    t = VID_ForceUnlockedAndReturnState();

    pos = ftell(f);
    fseek(f, 0, SEEK_END);
    end = ftell(f);
    fseek(f, pos, SEEK_SET);

    VID_ForceLockState(t);

    return end;
}

int Sys_FileOpenRead(const char *path, int *hndl)
{
    FILE *f;
    int i, retval;
    int t;

    t = VID_ForceUnlockedAndReturnState();

    i = findhandle();

    f = fopen(path, "rb");

    if (!f)
    {
        *hndl = -1;
        retval = -1;
    }
    else
    {
        sys_handles[i] = f;
        *hndl = i;
        retval = filelength(f);
    }

    VID_ForceLockState(t);

    return retval;
}

int Sys_FileOpenWrite(const char *path)
{
    FILE *f;
    int i;
    int t;

    t = VID_ForceUnlockedAndReturnState();

    i = findhandle();

    f = fopen(path, "wb");
    if (!f)
    {
        Sys_Error("Error opening %s: %s", path, strerror(errno));
    }
    sys_handles[i] = f;

    VID_ForceLockState(t);

    return i;
}

void Sys_FileClose(int handle)
{
    int t;

    t = VID_ForceUnlockedAndReturnState();
    fclose(sys_handles[handle]);
    sys_handles[handle] = nullptr;
    VID_ForceLockState(t);
}

void Sys_FileSeek(int handle, int position)
{
    int t;

    t = VID_ForceUnlockedAndReturnState();
    fseek(sys_handles[handle], position, SEEK_SET);
    VID_ForceLockState(t);
}

int Sys_FileRead(int handle, void *dest, int count)
{
    int t, x;

    t = VID_ForceUnlockedAndReturnState();
    x = (int)fread(dest, 1, count, sys_handles[handle]);
    VID_ForceLockState(t);
    return x;
}

int Sys_FileWrite(int handle, const void *data, int count)
{
    int t, x;

    t = VID_ForceUnlockedAndReturnState();
    x = (int)fwrite(data, 1, count, sys_handles[handle]);
    VID_ForceLockState(t);
    return x;
}

int Sys_FileTime(const char *path)
{
    FILE *f;
    int t, retval;

    t = VID_ForceUnlockedAndReturnState();

    f = fopen(path, "rb");

    if (f)
    {
        fclose(f);
        retval = 1;
    }
    else
    {
        retval = -1;
    }

    VID_ForceLockState(t);
    return retval;
}

void Sys_mkdir(const char *path)
{
    _mkdir(path);
}

/*
===============================================================================

SYSTEM IO

===============================================================================
*/

/*
================
Sys_MakeCodeWriteable
================
*/
void Sys_MakeCodeWriteable(size_t startaddr, size_t length)
{
    DWORD flOldProtect;

    if (!VirtualProtect((LPVOID)startaddr, length, PAGE_READWRITE, &flOldProtect))
    {
        Sys_Error("Protection change failed\n");
    }
}

#ifndef _M_IX86

void Sys_SetFPCW(void)
{
}

void Sys_PushFPCW_SetHigh(void)
{
}

void Sys_PopFPCW(void)
{
}

void MaskExceptions(void)
{
}

#endif

/*
================
Sys_Init
================
*/
void Sys_Init(void)
{
    LARGE_INTEGER PerformanceFreq;
    unsigned int lowpart, highpart;

    MaskExceptions();
    Sys_SetFPCW();

    if (!QueryPerformanceFrequency(&PerformanceFreq))
    {
        Sys_Error("No hardware timer available");
    }

    // get 32 out of the 64 time bits such that we have around
    // 1 microsecond resolution
    lowpart = (unsigned int)PerformanceFreq.LowPart;
    highpart = (unsigned int)PerformanceFreq.HighPart;
    lowshift = 0;

    while (highpart || (lowpart > 2000000.0))
    {
        lowshift++;
        lowpart >>= 1;
        lowpart |= (highpart & 1) << 31;
        highpart >>= 1;
    }

    pfreq = 1.0 / (double)lowpart;

    Sys_InitFloatTime();
}

[[noreturn]] void Sys_Error(const char *error, ...)
{
    va_list argptr;
    char text[1024], text2[1024];
    const char *text3 = "Press Enter to exit\n";
    const char *text4 = "***********************************\n";
    const char *text5 = "\n";
    DWORD dummy;
    double starttime;
    static int in_sys_error0 = 0;
    static int in_sys_error1 = 0;
    static int in_sys_error2 = 0;
    static int in_sys_error3 = 0;

    if (!in_sys_error3)
    {
        in_sys_error3 = 1;
        VID_ForceUnlockedAndReturnState();
    }

    va_start(argptr, error);
    vsnprintf(text, sizeof(text), error, argptr);
    va_end(argptr);

    if (isDedicated)
    {
        va_start(argptr, error);
        vsnprintf(text, sizeof(text), error, argptr);
        va_end(argptr);

        auto text2Result = std::format_to_n(text2, sizeof(text2) - 1, "ERROR: {}\n", text);
        *text2Result.out = '\0';
        WriteFile(houtput, text5, (DWORD)strlen(text5), &dummy, nullptr);
        WriteFile(houtput, text4, (DWORD)strlen(text4), &dummy, nullptr);
        WriteFile(houtput, text2, (DWORD)strlen(text2), &dummy, nullptr);
        WriteFile(houtput, text3, (DWORD)strlen(text3), &dummy, nullptr);
        WriteFile(houtput, text4, (DWORD)strlen(text4), &dummy, nullptr);

        starttime = Sys_FloatTime();
        sc_return_on_enter = true; // so Enter will get us out of here

        while (!Sys_ConsoleInput() && ((Sys_FloatTime() - starttime) < CONSOLE_ERROR_TIMEOUT))
        {
        }
    }
    else
    {
        // switch to windowed so the message box is visible, unless we already
        // tried that and failed
        if (!in_sys_error0)
        {
            in_sys_error0 = 1;
            VID_SetDefaultMode();
            MessageBox(nullptr, text, "Quake Error", MB_OK | MB_SETFOREGROUND | MB_ICONSTOP);
        }
        else
        {
            MessageBox(nullptr, text, "Double Quake Error", MB_OK | MB_SETFOREGROUND | MB_ICONSTOP);
        }
    }

    if (!in_sys_error1)
    {
        in_sys_error1 = 1;
        Host_Shutdown();
    }

    // shut down QHOST hooks if necessary
    if (!in_sys_error2)
    {
        in_sys_error2 = 1;
        DeinitConProc();
    }

    exit(1);
}

void Sys_PrintfImpl(const std::string &text)
{
    DWORD dummy;

    if (isDedicated)
    {
        WriteFile(houtput, text.c_str(), (DWORD)text.size(), &dummy, nullptr);
    }
}

void Sys_Quit(void)
{

    VID_ForceUnlockedAndReturnState();

    Host_Shutdown();

    CoUninitialize();

    if (tevent)
    {
        CloseHandle(tevent);
    }

    if (isDedicated)
    {
        FreeConsole();
    }

    // shut down QHOST hooks if necessary
    DeinitConProc();

    exit(0);
}

/*
================
Sys_FloatTime
================
*/
double Sys_FloatTime(void)
{
    static int sametimecount;
    static unsigned int oldtime;
    static int first = 1;
    LARGE_INTEGER PerformanceCount;
    unsigned int temp, t2;
    double time;

    Sys_PushFPCW_SetHigh();

    QueryPerformanceCounter(&PerformanceCount);

    temp = ((unsigned int)PerformanceCount.LowPart >> lowshift) |
           ((unsigned int)PerformanceCount.HighPart << (32 - lowshift));

    if (first)
    {
        oldtime = temp;
        first = 0;
    }
    else
    {
        // check for turnover or backward time
        if ((temp <= oldtime) && ((oldtime - temp) < 0x10000000))
        {
            oldtime = temp; // so we can't get stuck
        }
        else
        {
            t2 = temp - oldtime;

            time = (double)t2 * pfreq;
            oldtime = temp;

            curtime += time;

            if (curtime == lastcurtime)
            {
                sametimecount++;

                if (sametimecount > 100000)
                {
                    curtime += 1.0;
                    sametimecount = 0;
                }
            }
            else
            {
                sametimecount = 0;
            }

            lastcurtime = curtime;
        }
    }

    Sys_PopFPCW();

    return curtime;
}

/*
================
Sys_InitFloatTime
================
*/
void Sys_InitFloatTime(void)
{
    int j;

    Sys_FloatTime();

    j = COM_CheckParm("-starttime");

    if (j)
    {
        curtime = (double)(Q_atof(com_argv[j + 1]));
    }
    else
    {
        curtime = 0.0;
    }

    lastcurtime = curtime;
}

std::optional<std::string> Sys_ConsoleInput(void)
{
    static char text[256];
    static int len;
    INPUT_RECORD recs[1024];
    int ch;
    DWORD dummy, numread, numevents;

    if (!isDedicated)
    {
        return std::nullopt;
    }

    for (;;)
    {
        if (!GetNumberOfConsoleInputEvents(hinput, &numevents))
        {
            Sys_Error("Error getting # of console events");
        }

        if (numevents <= 0)
        {
            break;
        }

        if (!ReadConsoleInput(hinput, recs, 1, &numread))
        {
            Sys_Error("Error reading console input");
        }

        if (numread != 1)
        {
            Sys_Error("Couldn't read console input");
        }

        if (recs[0].EventType == KEY_EVENT)
        {
            if (!recs[0].Event.KeyEvent.bKeyDown)
            {
                ch = recs[0].Event.KeyEvent.uChar.AsciiChar;

                switch (ch)
                {
                case '\r':
                    WriteFile(houtput, "\r\n", 2, &dummy, nullptr);

                    if (len)
                    {
                        text[len] = 0;
                        len = 0;
                        return std::string(text);
                    }
                    else if (sc_return_on_enter)
                    {
                        // special case to allow exiting from the error handler on Enter
                        text[0] = '\r';
                        len = 0;
                        return std::string(text, 1);
                    }

                    break;

                case '\b':
                    WriteFile(houtput, "\b \b", 3, &dummy, nullptr);
                    if (len)
                    {
                        len--;
                    }
                    break;

                default:
                    if (ch >= ' ')
                    {
                        WriteFile(houtput, &ch, 1, &dummy, nullptr);
                        text[len] = ch;
                        len = (len + 1) & 0xff;
                    }

                    break;
                }
            }
        }
    }

    return std::nullopt;
}

void Sys_Sleep(void)
{
    Sleep(1);
}

void Sys_SendKeyEvents(void)
{
    MSG msg;

    while (PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE))
    {
        // we always update if there are any event, even if we're paused
        scr_skipupdate = 0;

        if (!GetMessage(&msg, nullptr, 0, 0))
        {
            Sys_Quit();
        }

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

/*
==============================================================================

 WINDOWS CRAP

==============================================================================
*/

/*
==================
WinMain
==================
*/
void SleepUntilInput(int time)
{

    MsgWaitForMultipleObjects(1, &tevent, FALSE, time, QS_ALLINPUT);
}

/*
==================
WinMain
==================
*/
HINSTANCE global_hInstance;
int global_nCmdShow;
const char *argv[MAX_NUM_ARGVS];
static const char *empty_string = "";
static HWND hwnd_dialog;

// the startup splash dialog is created here, but torn down by Video once
// the real GL window is up -- exposed as a one-shot action instead of the
// raw HWND, so Video can't do anything to the dialog except close it
void Sys_CloseSplashDialog(void)
{
    if (hwnd_dialog)
    {
        DestroyWindow(hwnd_dialog);
        hwnd_dialog = nullptr;
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    quakeparms_t parms;
    double time, oldtime, newtime;
    MEMORYSTATUSEX lpBuffer;
    static char cwd[1024];
    int t;
    RECT rect;

    /* previous instances do not exist in Win32 */
    if (hPrevInstance)
    {
        return 0;
    }

    Sys_EnableDpiAwareness();

    // needed by snd_win.cpp's WASAPI device enumeration/activation
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    global_hInstance = hInstance;
    global_nCmdShow = nCmdShow;

    lpBuffer.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&lpBuffer);

    if (!GetCurrentDirectory(sizeof(cwd), cwd))
    {
        Sys_Error("Couldn't determine current directory");
    }

    if (cwd[Q_strlen(cwd) - 1] == '/')
    {
        cwd[Q_strlen(cwd) - 1] = 0;
    }

    parms.basedir = cwd;
    parms.cachedir = nullptr;

    parms.argc = 1;
    argv[0] = empty_string;

    while (*lpCmdLine && (parms.argc < MAX_NUM_ARGVS))
    {
        while (*lpCmdLine && ((*lpCmdLine <= 32) || (*lpCmdLine > 126)))
        {
            lpCmdLine++;
        }

        if (*lpCmdLine)
        {
            argv[parms.argc] = lpCmdLine;
            parms.argc++;

            while (*lpCmdLine && ((*lpCmdLine > 32) && (*lpCmdLine <= 126)))
            {
                lpCmdLine++;
            }

            if (*lpCmdLine)
            {
                *lpCmdLine = 0;
                lpCmdLine++;
            }
        }
    }

    parms.argv = argv;

    COM_InitArgv(parms.argc, parms.argv);

    parms.argc = com_argc;
    parms.argv = com_argv;

    isDedicated = (COM_CheckParm("-dedicated") != 0);

    if (!isDedicated)
    {
        hwnd_dialog = CreateDialog(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), nullptr, nullptr);

        if (hwnd_dialog)
        {
            if (GetWindowRect(hwnd_dialog, &rect))
            {
                if (rect.left > (rect.top * 2))
                {
                    SetWindowPos(hwnd_dialog, 0, (rect.left / 2) - ((rect.right - rect.left) / 2), rect.top, 0, 0,
                                 SWP_NOZORDER | SWP_NOSIZE);
                }
            }

            ShowWindow(hwnd_dialog, SW_SHOWDEFAULT);
            UpdateWindow(hwnd_dialog);
            SetForegroundWindow(hwnd_dialog);
        }
    }

    // take the greater of all the available memory or half the total memory,
    // but at least 8 Mb and no more than 16 Mb, unless they explicitly
    // request otherwise
    parms.memsize = (size_t)lpBuffer.ullAvailPhys;

    if (parms.memsize < MINIMUM_WIN_MEMORY)
    {
        parms.memsize = MINIMUM_WIN_MEMORY;
    }

    if (parms.memsize < (size_t)(lpBuffer.ullTotalPhys >> 1))
    {
        parms.memsize = (size_t)(lpBuffer.ullTotalPhys >> 1);
    }

    if (parms.memsize > MAXIMUM_WIN_MEMORY)
    {
        parms.memsize = MAXIMUM_WIN_MEMORY;
    }

    if (COM_CheckParm("-heapsize"))
    {
        t = COM_CheckParm("-heapsize") + 1;

        if (t < com_argc)
        {
            parms.memsize = (size_t)Q_atoi(com_argv[t]) * 1024;
        }
    }

    parms.membase = malloc(parms.memsize);

    if (!parms.membase)
    {
        Sys_Error("Not enough memory free; check disk space\n");
    }

    Sys_PageIn(parms.membase, parms.memsize);

    tevent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    if (!tevent)
    {
        Sys_Error("Couldn't create event");
    }

    if (isDedicated)
    {
        if (!AllocConsole())
        {
            Sys_Error("Couldn't create dedicated server console");
        }

        hinput = GetStdHandle(STD_INPUT_HANDLE);
        houtput = GetStdHandle(STD_OUTPUT_HANDLE);

        // give QHOST a chance to hook into the console
        if ((t = COM_CheckParm("-HFILE")) > 0)
        {
            if (t < com_argc)
            {
                hFile = (HANDLE)(UINT_PTR)strtoull(com_argv[t + 1], nullptr, 0);
            }
        }

        if ((t = COM_CheckParm("-HPARENT")) > 0)
        {
            if (t < com_argc)
            {
                heventParent = (HANDLE)(UINT_PTR)strtoull(com_argv[t + 1], nullptr, 0);
            }
        }

        if ((t = COM_CheckParm("-HCHILD")) > 0)
        {
            if (t < com_argc)
            {
                heventChild = (HANDLE)(UINT_PTR)strtoull(com_argv[t + 1], nullptr, 0);
            }
        }

        InitConProc(hFile, heventParent, heventChild);
    }

    Sys_Init();

    // because sound is off until we become active
    S_BlockSound();

    Sys_Printf("Host_Init\n");
    Host_Init(&parms);

    oldtime = Sys_FloatTime();

    /* main window message loop */
    while (1)
    {
        if (isDedicated)
        {
            newtime = Sys_FloatTime();
            time = newtime - oldtime;

            while (time < sys_ticrate.value)
            {
                Sys_Sleep();
                newtime = Sys_FloatTime();
                time = newtime - oldtime;
            }
        }
        else
        {
            // yield the CPU for a little while when paused, minimized, or not the focus
            if ((cl.paused && !ActiveApp) || Minimized || block_drawing)
            {
                SleepUntilInput(PAUSE_SLEEP);
                scr_skipupdate = 1; // no point in bothering to draw
            }
            else if (!ActiveApp)
            {
                SleepUntilInput(NOT_FOCUS_SLEEP);
            }

            newtime = Sys_FloatTime();
            time = newtime - oldtime;
        }

        Host_Frame(time);
        oldtime = newtime;
    }

    /* return success of application */
    return TRUE;
}
