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
// in_win.c -- windows mouse and joystick code

#include "quakedef.h"
#include "winquake.h"
#include "dosisms.h"

// mouse variables
cvar_t	m_filter = {"m_filter","0"};

int			mouse_buttons;
int			mouse_oldbuttonstate;
POINT		current_pos;
int			mouse_x, mouse_y, old_mouse_x, old_mouse_y, mx_accum, my_accum;

static qboolean	restore_spi;
static int		originalmouseparms[3], newmouseparms[3] = {0, 0, 1};

unsigned int uiWheelMessage;
qboolean	mouseactive;
static qboolean	mouseinitialized;
static qboolean	mouseparmsvalid, mouseactivatetoggle;
static qboolean	mouseshowtoggle = 1;

// Raw Input (WM_INPUT) -- replaces the old GetCursorPos/SetCursorPos
// warp-to-center hack and the legacy DirectInput mouse path. rawinput_active
// is set once RegisterRawInputDevices succeeds in IN_StartupMouse; IN_RawMouseMoved
// is called from MainWndProc's WM_INPUT handler to accumulate relative deltas,
// which IN_MouseMove consumes and resets each frame.
static qboolean	rawinput_active;
static int		raw_mx, raw_my;

void IN_RawMouseMoved (int dx, int dy)
{
	raw_mx += dx;
	raw_my += dy;
}

// ------------------------------------------------------------------------
// XInput (Xbox-style controller) support -- preferred over the legacy
// joyGetPosEx joystick path below whenever a controller actually shows up
// connected, since it gives proper separate analog triggers and a fixed,
// standard button layout instead of joyGetPosEx's generic N-axis/N-button
// abstraction that needs manual per-device axis-mapping cvars to be usable
// at all. The legacy path remains as a fallback for wheels, flight sticks,
// and other non-Xbox-style devices XInput doesn't cover.
// ------------------------------------------------------------------------
#include <xinput.h>

extern cvar_t in_joystick;	// defined further down, alongside the rest of the joystick cvars

typedef DWORD (WINAPI *PFN_XInputGetState)(DWORD, XINPUT_STATE *);

cvar_t	joy_xinput_deadzone = {"joy_xinput_deadzone", "0.24"};

static HMODULE				xinput_dll;
static PFN_XInputGetState	pXInputGetState;
static qboolean				xinput_avail;
static qboolean				xinput_connected;
static DWORD				xinput_index;
static WORD					xinput_oldbuttons;
static qboolean				xinput_oldlt, xinput_oldrt;

void IN_StartupXInput (void)
{
	if (COM_CheckParm ("-noxinput"))
		return;

	xinput_dll = LoadLibrary ("xinput1_4.dll");
	if (!xinput_dll)
		xinput_dll = LoadLibrary ("xinput9_1_0.dll");	// Vista-era stub, always present

	if (!xinput_dll)
		return;

	pXInputGetState = (PFN_XInputGetState)GetProcAddress (xinput_dll, "XInputGetState");

	if (!pXInputGetState)
	{
		FreeLibrary (xinput_dll);
		xinput_dll = nullptr;
		return;
	}

	xinput_avail = true;
	Con_SafePrintf ("XInput initialized\n");
}

void IN_ShutdownXInput (void)
{
	if (xinput_dll)
	{
		FreeLibrary (xinput_dll);
		xinput_dll = nullptr;
	}
	pXInputGetState = nullptr;
	xinput_avail = xinput_connected = false;
}

static qboolean IN_XInputFindController (void)
{
	XINPUT_STATE state;

	for (DWORD i = 0; i < XUSER_MAX_COUNT; i++)
	{
		memset (&state, 0, sizeof(state));
		if (pXInputGetState (i, &state) == ERROR_SUCCESS)
		{
			xinput_index = i;
			return true;
		}
	}
	return false;
}

// Radial deadzone: below the threshold, no output; above it, rescaled so
// the stick's physical extremes still reach exactly -1..1.
static float IN_XInputAxis (SHORT raw)
{
	float v = raw / 32768.0f;
	float dz = joy_xinput_deadzone.value;

	if (fabs(v) < dz)
		return 0.0f;

	return (v < 0 ? -1.0f : 1.0f) * (fabs(v) - dz) / (1.0f - dz);
}

qboolean IN_XInputActive (void)
{
	return xinput_avail && xinput_connected;
}

void IN_XInputMove (usercmd_t *cmd)
{
	XINPUT_STATE	state;
	WORD			buttons;
	float			speed, aspeed, lx, ly, rx, ry;
	qboolean		ltdown, rtdown;

	static const struct { WORD mask; int key; } buttonmap[] =
	{
		{ XINPUT_GAMEPAD_DPAD_UP,        K_AUX29 },	// matches the legacy
		{ XINPUT_GAMEPAD_DPAD_RIGHT,     K_AUX30 },	// joystick's POV-hat
		{ XINPUT_GAMEPAD_DPAD_DOWN,      K_AUX31 },	// key slots -- mutually
		{ XINPUT_GAMEPAD_DPAD_LEFT,      K_AUX32 },	// exclusive, no clash
		{ XINPUT_GAMEPAD_START,          K_XBOX_START },
		{ XINPUT_GAMEPAD_BACK,           K_XBOX_BACK },
		{ XINPUT_GAMEPAD_LEFT_THUMB,     K_XBOX_LTHUMB },
		{ XINPUT_GAMEPAD_RIGHT_THUMB,    K_XBOX_RTHUMB },
		{ XINPUT_GAMEPAD_LEFT_SHOULDER,  K_XBOX_LSHOULDER },
		{ XINPUT_GAMEPAD_RIGHT_SHOULDER, K_XBOX_RSHOULDER },
		{ XINPUT_GAMEPAD_A,              K_XBOX_A },
		{ XINPUT_GAMEPAD_B,              K_XBOX_B },
		{ XINPUT_GAMEPAD_X,              K_XBOX_X },
		{ XINPUT_GAMEPAD_Y,              K_XBOX_Y },
	};

	if (!xinput_avail || !in_joystick.value)
		return;

	if (!xinput_connected)
	{
		if (!IN_XInputFindController ())
			return;
		xinput_connected = true;
	}

	memset (&state, 0, sizeof(state));
	if (pXInputGetState (xinput_index, &state) != ERROR_SUCCESS)
	{
		// controller unplugged -- go back to looking for one next frame
		xinput_connected = false;
		return;
	}

	buttons = state.Gamepad.wButtons;
	for (auto &b : buttonmap)
	{
		qboolean down = !!(buttons & b.mask);
		if (down != !!(xinput_oldbuttons & b.mask))
			Key_Event (b.key, down);
	}
	xinput_oldbuttons = buttons;

	ltdown = state.Gamepad.bLeftTrigger  > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
	rtdown = state.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
	if (ltdown != xinput_oldlt) Key_Event (K_XBOX_LTRIGGER, ltdown);
	if (rtdown != xinput_oldrt) Key_Event (K_XBOX_RTRIGGER, rtdown);
	xinput_oldlt = ltdown;
	xinput_oldrt = rtdown;

	// left stick = move, right stick = look (always-on, like mlook)
	lx = IN_XInputAxis (state.Gamepad.sThumbLX);
	ly = IN_XInputAxis (state.Gamepad.sThumbLY);
	rx = IN_XInputAxis (state.Gamepad.sThumbRX);
	ry = IN_XInputAxis (state.Gamepad.sThumbRY);

	speed = (in_speed.state & 1) ? cl_movespeedkey.value : 1;
	aspeed = speed * host_frametime;

	cmd->forwardmove += ly * speed * cl_forwardspeed.value;
	cmd->sidemove    += lx * speed * cl_sidespeed.value;

	cl.viewangles[YAW]   -= rx * aspeed * cl_yawspeed.value;
	cl.viewangles[PITCH] -= ry * aspeed * cl_pitchspeed.value;

	if (cl.viewangles[PITCH] > 80.0)
		cl.viewangles[PITCH] = 80.0;
	if (cl.viewangles[PITCH] < -70.0)
		cl.viewangles[PITCH] = -70.0;
}

// joystick defines and variables
// where should defines be moved?
#define JOY_ABSOLUTE_AXIS	0x00000000		// control like a joystick
#define JOY_RELATIVE_AXIS	0x00000010		// control like a mouse, spinner, trackball
#define	JOY_MAX_AXES		6				// X, Y, Z, R, U, V
#define JOY_AXIS_X			0
#define JOY_AXIS_Y			1
#define JOY_AXIS_Z			2
#define JOY_AXIS_R			3
#define JOY_AXIS_U			4
#define JOY_AXIS_V			5

enum _ControlList
{
	AxisNada = 0, AxisForward, AxisLook, AxisSide, AxisTurn
};

DWORD	dwAxisFlags[JOY_MAX_AXES] =
{
	JOY_RETURNX, JOY_RETURNY, JOY_RETURNZ, JOY_RETURNR, JOY_RETURNU, JOY_RETURNV
};

DWORD	dwAxisMap[JOY_MAX_AXES];
DWORD	dwControlMap[JOY_MAX_AXES];
PDWORD	pdwRawValue[JOY_MAX_AXES];

// none of these cvars are saved over a session
// this means that advanced controller configuration needs to be executed
// each time.  this avoids any problems with getting back to a default usage
// or when changing from one controller to another.  this way at least something
// works.
cvar_t	in_joystick = {"joystick","0", true};
cvar_t	joy_name = {"joyname", "joystick"};
cvar_t	joy_advanced = {"joyadvanced", "0"};
cvar_t	joy_advaxisx = {"joyadvaxisx", "0"};
cvar_t	joy_advaxisy = {"joyadvaxisy", "0"};
cvar_t	joy_advaxisz = {"joyadvaxisz", "0"};
cvar_t	joy_advaxisr = {"joyadvaxisr", "0"};
cvar_t	joy_advaxisu = {"joyadvaxisu", "0"};
cvar_t	joy_advaxisv = {"joyadvaxisv", "0"};
cvar_t	joy_forwardthreshold = {"joyforwardthreshold", "0.15"};
cvar_t	joy_sidethreshold = {"joysidethreshold", "0.15"};
cvar_t	joy_pitchthreshold = {"joypitchthreshold", "0.15"};
cvar_t	joy_yawthreshold = {"joyyawthreshold", "0.15"};
cvar_t	joy_forwardsensitivity = {"joyforwardsensitivity", "-1.0"};
cvar_t	joy_sidesensitivity = {"joysidesensitivity", "-1.0"};
cvar_t	joy_pitchsensitivity = {"joypitchsensitivity", "1.0"};
cvar_t	joy_yawsensitivity = {"joyyawsensitivity", "-1.0"};
cvar_t	joy_wwhack1 = {"joywwhack1", "0.0"};
cvar_t	joy_wwhack2 = {"joywwhack2", "0.0"};

qboolean	joy_avail, joy_advancedinit, joy_haspov;
DWORD		joy_oldbuttonstate, joy_oldpovstate;

int			joy_id;
DWORD		joy_flags;
DWORD		joy_numbuttons;

static JOYINFOEX	ji;

// forward-referenced functions
void IN_StartupJoystick (void);
void Joy_AdvancedUpdate_f (void);
void IN_JoyMove (usercmd_t *cmd);


/*
===========
Force_CenterView_f
===========
*/
void Force_CenterView_f (void)
{
	cl.viewangles[PITCH] = 0;
}


/*
===========
IN_UpdateClipCursor
===========
*/
void IN_UpdateClipCursor (void)
{

	if (mouseinitialized && mouseactive)
	{
		ClipCursor (VID_GetWindowRect ());
	}
}


/*
===========
IN_ShowMouse
===========
*/
void IN_ShowMouse (void)
{

	if (!mouseshowtoggle)
	{
		ShowCursor (TRUE);
		mouseshowtoggle = 1;
	}
}


/*
===========
IN_HideMouse
===========
*/
void IN_HideMouse (void)
{

	if (mouseshowtoggle)
	{
		ShowCursor (FALSE);
		mouseshowtoggle = 0;
	}
}


/*
===========
IN_ActivateMouse
===========
*/
void IN_ActivateMouse (void)
{

	mouseactivatetoggle = true;

	if (mouseinitialized)
	{
		if (mouseparmsvalid)
			restore_spi = SystemParametersInfo (SPI_SETMOUSE, 0, newmouseparms, 0);

		if (!rawinput_active)
		{
			int cx, cy;
			VID_GetWindowCenter (&cx, &cy);
			SetCursorPos (cx, cy);
		}
		SetCapture (mainwindow);
		ClipCursor (VID_GetWindowRect ());

		mouseactive = true;
	}
}


/*
===========
IN_SetQuakeMouseState
===========
*/
void IN_SetQuakeMouseState (void)
{
	if (mouseactivatetoggle)
		IN_ActivateMouse ();
}


/*
===========
IN_DeactivateMouse
===========
*/
void IN_DeactivateMouse (void)
{

	mouseactivatetoggle = false;

	if (mouseinitialized)
	{
		if (restore_spi)
			SystemParametersInfo (SPI_SETMOUSE, 0, originalmouseparms, 0);

		ClipCursor (nullptr);
		ReleaseCapture ();

		mouseactive = false;
	}
}


/*
===========
IN_RestoreOriginalMouseState
===========
*/
void IN_RestoreOriginalMouseState (void)
{
	if (mouseactivatetoggle)
	{
		IN_DeactivateMouse ();
		mouseactivatetoggle = true;
	}

// try to redraw the cursor so it gets reinitialized, because sometimes it
// has garbage after the mode switch
	ShowCursor (TRUE);
	ShowCursor (FALSE);
}


/*
===========
IN_StartupMouse
===========
*/
void IN_StartupMouse (void)
{
	if ( COM_CheckParm ("-nomouse") )
		return;

	mouseinitialized = true;

	// Raw Input gives us un-accelerated relative deltas straight from the
	// HID mouse report, independent of cursor position -- no more warping
	// the cursor back to window_center every frame to measure movement.
	RAWINPUTDEVICE rid;
	rid.usUsagePage = 0x01;	// HID_USAGE_PAGE_GENERIC
	rid.usUsage = 0x02;		// HID_USAGE_GENERIC_MOUSE
	rid.dwFlags = 0;
	rid.hwndTarget = mainwindow;
	rawinput_active = RegisterRawInputDevices (&rid, 1, sizeof(rid));

	if (rawinput_active)
		Con_SafePrintf ("Raw input mouse initialized\n");
	else
		Con_SafePrintf ("RegisterRawInputDevices failed, falling back to cursor-warp mouse input\n");

	mouseparmsvalid = SystemParametersInfo (SPI_GETMOUSE, 0, originalmouseparms, 0);

	if (mouseparmsvalid)
	{
		if ( COM_CheckParm ("-noforcemspd") )
			newmouseparms[2] = originalmouseparms[2];

		if ( COM_CheckParm ("-noforcemaccel") )
		{
			newmouseparms[0] = originalmouseparms[0];
			newmouseparms[1] = originalmouseparms[1];
		}

		if ( COM_CheckParm ("-noforcemparms") )
		{
			newmouseparms[0] = originalmouseparms[0];
			newmouseparms[1] = originalmouseparms[1];
			newmouseparms[2] = originalmouseparms[2];
		}
	}

	mouse_buttons = 3;

// if a fullscreen video mode was set before the mouse was initialized,
// set the mouse state appropriately
	if (mouseactivatetoggle)
		IN_ActivateMouse ();
}


/*
===========
IN_Init
===========
*/
void IN_Init (void)
{
	// mouse variables
	Cvar_RegisterVariable (&m_filter);

	// joystick variables
	Cvar_RegisterVariable (&in_joystick);
	Cvar_RegisterVariable (&joy_name);
	Cvar_RegisterVariable (&joy_advanced);
	Cvar_RegisterVariable (&joy_advaxisx);
	Cvar_RegisterVariable (&joy_advaxisy);
	Cvar_RegisterVariable (&joy_advaxisz);
	Cvar_RegisterVariable (&joy_advaxisr);
	Cvar_RegisterVariable (&joy_advaxisu);
	Cvar_RegisterVariable (&joy_advaxisv);
	Cvar_RegisterVariable (&joy_forwardthreshold);
	Cvar_RegisterVariable (&joy_sidethreshold);
	Cvar_RegisterVariable (&joy_pitchthreshold);
	Cvar_RegisterVariable (&joy_yawthreshold);
	Cvar_RegisterVariable (&joy_forwardsensitivity);
	Cvar_RegisterVariable (&joy_sidesensitivity);
	Cvar_RegisterVariable (&joy_pitchsensitivity);
	Cvar_RegisterVariable (&joy_yawsensitivity);
	Cvar_RegisterVariable (&joy_wwhack1);
	Cvar_RegisterVariable (&joy_wwhack2);
	Cvar_RegisterVariable (&joy_xinput_deadzone);

	Cmd_AddCommand ("force_centerview", Force_CenterView_f);
	Cmd_AddCommand ("joyadvancedupdate", Joy_AdvancedUpdate_f);

	uiWheelMessage = RegisterWindowMessage ( "MSWHEEL_ROLLMSG" );

	IN_StartupMouse ();
	IN_StartupJoystick ();
	IN_StartupXInput ();
}

/*
===========
IN_Shutdown
===========
*/
void IN_Shutdown (void)
{

	IN_DeactivateMouse ();
	IN_ShowMouse ();
	IN_ShutdownXInput ();
}


/*
===========
IN_MouseEvent
===========
*/
void IN_MouseEvent (int mstate)
{
	int	i;

	if (mouseactive)
	{
	// perform button actions
		for (i=0 ; i<mouse_buttons ; i++)
		{
			if ( (mstate & (1<<i)) &&
				!(mouse_oldbuttonstate & (1<<i)) )
			{
				Key_Event (K_MOUSE1 + i, true);
			}

			if ( !(mstate & (1<<i)) &&
				(mouse_oldbuttonstate & (1<<i)) )
			{
				Key_Event (K_MOUSE1 + i, false);
			}
		}	
			
		mouse_oldbuttonstate = mstate;
	}
}


/*
===========
IN_MouseMove
===========
*/
void IN_MouseMove (usercmd_t *cmd)
{
	int					mx, my;
	int					center_x = 0, center_y = 0;

	if (!mouseactive)
		return;

	if (rawinput_active)
	{
		mx = raw_mx;
		my = raw_my;
		raw_mx = 0;
		raw_my = 0;
	}
	else
	{
		// fallback if RegisterRawInputDevices failed in IN_StartupMouse
		VID_GetWindowCenter (&center_x, &center_y);
		GetCursorPos (&current_pos);
		mx = current_pos.x - center_x + mx_accum;
		my = current_pos.y - center_y + my_accum;
		mx_accum = 0;
		my_accum = 0;
	}

//if (mx ||  my)
//	Con_DPrintf("mx=%d, my=%d\n", mx, my);

	if (m_filter.value)
	{
		mouse_x = (mx + old_mouse_x) * 0.5;
		mouse_y = (my + old_mouse_y) * 0.5;
	}
	else
	{
		mouse_x = mx;
		mouse_y = my;
	}

	old_mouse_x = mx;
	old_mouse_y = my;

	mouse_x *= sensitivity.value;
	mouse_y *= sensitivity.value;

// add mouse X/Y movement to cmd
	if ( (in_strafe.state & 1) || (lookstrafe.value && (in_mlook.state & 1) ))
		cmd->sidemove += m_side.value * mouse_x;
	else
		cl.viewangles[YAW] -= m_yaw.value * mouse_x;

	if (in_mlook.state & 1)
		V_StopPitchDrift ();
		
	if ( (in_mlook.state & 1) && !(in_strafe.state & 1))
	{
		cl.viewangles[PITCH] += m_pitch.value * mouse_y;
		if (cl.viewangles[PITCH] > 80)
			cl.viewangles[PITCH] = 80;
		if (cl.viewangles[PITCH] < -70)
			cl.viewangles[PITCH] = -70;
	}
	else
	{
		if ((in_strafe.state & 1) && noclip_anglehack)
			cmd->upmove -= m_forward.value * mouse_y;
		else
			cmd->forwardmove -= m_forward.value * mouse_y;
	}

// if the mouse has moved, force it to the center, so there's room to move
// (only needed for the cursor-warp fallback -- raw input deltas are
// independent of cursor position)
	if (!rawinput_active && (mx || my))
	{
		SetCursorPos (center_x, center_y);
	}
}


/*
===========
IN_Move
===========
*/
void IN_Move (usercmd_t *cmd)
{

	if (ActiveApp && !Minimized)
	{
		IN_MouseMove (cmd);
		IN_XInputMove (cmd);
		if (!IN_XInputActive ())
			IN_JoyMove (cmd);
	}
}


/*
===========
IN_Accumulate
===========
*/
void IN_Accumulate (void)
{
	// raw input deltas already accumulate via WM_INPUT messages as they're
	// pumped, so there's nothing to do here unless we fell back to cursor-warp
	if (mouseactive && !rawinput_active)
	{
		int center_x, center_y;
		VID_GetWindowCenter (&center_x, &center_y);

		GetCursorPos (&current_pos);

		mx_accum += current_pos.x - center_x;
		my_accum += current_pos.y - center_y;

	// force the mouse to the center, so there's room to move
		SetCursorPos (center_x, center_y);
	}
}


/*
===================
IN_ClearStates
===================
*/
void IN_ClearStates (void)
{

	if (mouseactive)
	{
		mx_accum = 0;
		my_accum = 0;
		mouse_oldbuttonstate = 0;
	}
}


/* 
=============== 
IN_StartupJoystick 
=============== 
*/  
void IN_StartupJoystick (void) 
{ 
	int			numdevs;
	JOYCAPS		jc;
	MMRESULT	mmr;
 
 	// assume no joystick
	joy_avail = false; 

	// abort startup if user requests no joystick
	if ( COM_CheckParm ("-nojoy") ) 
		return; 
 
	// verify joystick driver is present
	if ((numdevs = joyGetNumDevs ()) == 0)
	{
		Con_Printf ("\njoystick not found -- driver not present\n\n");
		return;
	}

	// cycle through the joystick ids for the first valid one
	for (joy_id=0 ; joy_id<numdevs ; joy_id++)
	{
		memset (&ji, 0, sizeof(ji));
		ji.dwSize = sizeof(ji);
		ji.dwFlags = JOY_RETURNCENTERED;

		if ((mmr = joyGetPosEx (joy_id, &ji)) == JOYERR_NOERROR)
			break;
	} 

	// abort startup if we didn't find a valid joystick
	if (mmr != JOYERR_NOERROR)
	{
		Con_Printf ("\njoystick not found -- no valid joysticks (%x)\n\n", mmr);
		return;
	}

	// get the capabilities of the selected joystick
	// abort startup if command fails
	memset (&jc, 0, sizeof(jc));
	if ((mmr = joyGetDevCaps (joy_id, &jc, sizeof(jc))) != JOYERR_NOERROR)
	{
		Con_Printf ("\njoystick not found -- invalid joystick capabilities (%x)\n\n", mmr); 
		return;
	}

	// save the joystick's number of buttons and POV status
	joy_numbuttons = jc.wNumButtons;
	joy_haspov = jc.wCaps & JOYCAPS_HASPOV;

	// old button and POV states default to no buttons pressed
	joy_oldbuttonstate = joy_oldpovstate = 0;

	// mark the joystick as available and advanced initialization not completed
	// this is needed as cvars are not available during initialization

	joy_avail = true; 
	joy_advancedinit = false;

	Con_Printf ("\njoystick detected\n\n"); 
}


/*
===========
RawValuePointer
===========
*/
PDWORD RawValuePointer (int axis)
{
	switch (axis)
	{
	case JOY_AXIS_X:
		return &ji.dwXpos;
	case JOY_AXIS_Y:
		return &ji.dwYpos;
	case JOY_AXIS_Z:
		return &ji.dwZpos;
	case JOY_AXIS_R:
		return &ji.dwRpos;
	case JOY_AXIS_U:
		return &ji.dwUpos;
	case JOY_AXIS_V:
		return &ji.dwVpos;
	default:
		Sys_Error ("RawValuePointer: invalid axis %d", axis);
	}
}


/*
===========
Joy_AdvancedUpdate_f
===========
*/
void Joy_AdvancedUpdate_f (void)
{

	// called once by IN_ReadJoystick and by user whenever an update is needed
	// cvars are now available
	int	i;
	DWORD dwTemp;

	// initialize all the maps
	for (i = 0; i < JOY_MAX_AXES; i++)
	{
		dwAxisMap[i] = AxisNada;
		dwControlMap[i] = JOY_ABSOLUTE_AXIS;
		pdwRawValue[i] = RawValuePointer(i);
	}

	if( joy_advanced.value == 0.0)
	{
		// default joystick initialization
		// 2 axes only with joystick control
		dwAxisMap[JOY_AXIS_X] = AxisTurn;
		// dwControlMap[JOY_AXIS_X] = JOY_ABSOLUTE_AXIS;
		dwAxisMap[JOY_AXIS_Y] = AxisForward;
		// dwControlMap[JOY_AXIS_Y] = JOY_ABSOLUTE_AXIS;
	}
	else
	{
		if (Q_strcmp (joy_name.string.c_str(), "joystick") != 0)
		{
			// notify user of advanced controller
			Con_Printf ("\n%s configured\n\n", joy_name.string.c_str());
		}

		// advanced initialization here
		// data supplied by user via joy_axisn cvars
		dwTemp = (DWORD) joy_advaxisx.value;
		dwAxisMap[JOY_AXIS_X] = dwTemp & 0x0000000f;
		dwControlMap[JOY_AXIS_X] = dwTemp & JOY_RELATIVE_AXIS;
		dwTemp = (DWORD) joy_advaxisy.value;
		dwAxisMap[JOY_AXIS_Y] = dwTemp & 0x0000000f;
		dwControlMap[JOY_AXIS_Y] = dwTemp & JOY_RELATIVE_AXIS;
		dwTemp = (DWORD) joy_advaxisz.value;
		dwAxisMap[JOY_AXIS_Z] = dwTemp & 0x0000000f;
		dwControlMap[JOY_AXIS_Z] = dwTemp & JOY_RELATIVE_AXIS;
		dwTemp = (DWORD) joy_advaxisr.value;
		dwAxisMap[JOY_AXIS_R] = dwTemp & 0x0000000f;
		dwControlMap[JOY_AXIS_R] = dwTemp & JOY_RELATIVE_AXIS;
		dwTemp = (DWORD) joy_advaxisu.value;
		dwAxisMap[JOY_AXIS_U] = dwTemp & 0x0000000f;
		dwControlMap[JOY_AXIS_U] = dwTemp & JOY_RELATIVE_AXIS;
		dwTemp = (DWORD) joy_advaxisv.value;
		dwAxisMap[JOY_AXIS_V] = dwTemp & 0x0000000f;
		dwControlMap[JOY_AXIS_V] = dwTemp & JOY_RELATIVE_AXIS;
	}

	// compute the axes to collect from DirectInput
	joy_flags = JOY_RETURNCENTERED | JOY_RETURNBUTTONS | JOY_RETURNPOV;
	for (i = 0; i < JOY_MAX_AXES; i++)
	{
		if (dwAxisMap[i] != AxisNada)
		{
			joy_flags |= dwAxisFlags[i];
		}
	}
}


/*
===========
IN_Commands
===========
*/
void IN_Commands (void)
{
	int		i, key_index;
	DWORD	buttonstate, povstate;

	if (!joy_avail)
	{
		return;
	}

	
	// loop through the joystick buttons
	// key a joystick event or auxillary event for higher number buttons for each state change
	buttonstate = ji.dwButtons;
	for (i=0 ; i < (int)joy_numbuttons ; i++)
	{
		if ( (buttonstate & (1<<i)) && !(joy_oldbuttonstate & (1<<i)) )
		{
			key_index = (i < 4) ? K_JOY1 : K_AUX1;
			Key_Event (key_index + i, true);
		}

		if ( !(buttonstate & (1<<i)) && (joy_oldbuttonstate & (1<<i)) )
		{
			key_index = (i < 4) ? K_JOY1 : K_AUX1;
			Key_Event (key_index + i, false);
		}
	}
	joy_oldbuttonstate = buttonstate;

	if (joy_haspov)
	{
		// convert POV information into 4 bits of state information
		// this avoids any potential problems related to moving from one
		// direction to another without going through the center position
		povstate = 0;
		if(ji.dwPOV != JOY_POVCENTERED)
		{
			if (ji.dwPOV == JOY_POVFORWARD)
				povstate |= 0x01;
			if (ji.dwPOV == JOY_POVRIGHT)
				povstate |= 0x02;
			if (ji.dwPOV == JOY_POVBACKWARD)
				povstate |= 0x04;
			if (ji.dwPOV == JOY_POVLEFT)
				povstate |= 0x08;
		}
		// determine which bits have changed and key an auxillary event for each change
		for (i=0 ; i < 4 ; i++)
		{
			if ( (povstate & (1<<i)) && !(joy_oldpovstate & (1<<i)) )
			{
				Key_Event (K_AUX29 + i, true);
			}

			if ( !(povstate & (1<<i)) && (joy_oldpovstate & (1<<i)) )
			{
				Key_Event (K_AUX29 + i, false);
			}
		}
		joy_oldpovstate = povstate;
	}
}


/* 
=============== 
IN_ReadJoystick
=============== 
*/  
qboolean IN_ReadJoystick (void)
{

	memset (&ji, 0, sizeof(ji));
	ji.dwSize = sizeof(ji);
	ji.dwFlags = joy_flags;

	if (joyGetPosEx (joy_id, &ji) == JOYERR_NOERROR)
	{
		// this is a hack -- there is a bug in the Logitech WingMan Warrior DirectInput Driver
		// rather than having 32768 be the zero point, they have the zero point at 32668
		// go figure -- anyway, now we get the full resolution out of the device
		if (joy_wwhack1.value != 0.0)
		{
			ji.dwUpos += 100;
		}
		return true;
	}
	else
	{
		// read error occurred
		// turning off the joystick seems too harsh for 1 read error,\
		// but what should be done?
		// Con_Printf ("IN_ReadJoystick: no response\n");
		// joy_avail = false;
		return false;
	}
}


/*
===========
IN_JoyMove
===========
*/
void IN_JoyMove (usercmd_t *cmd)
{
	float	speed, aspeed;
	float	fAxisValue, fTemp;
	int		i;

	// complete initialization if first time in
	// this is needed as cvars are not available at initialization time
	if( joy_advancedinit != true )
	{
		Joy_AdvancedUpdate_f();
		joy_advancedinit = true;
	}

	// verify joystick is available and that the user wants to use it
	if (!joy_avail || !in_joystick.value)
	{
		return; 
	}
 
	// collect the joystick data, if possible
	if (IN_ReadJoystick () != true)
	{
		return;
	}

	if (in_speed.state & 1)
		speed = cl_movespeedkey.value;
	else
		speed = 1;
	aspeed = speed * host_frametime;

	// loop through the axes
	for (i = 0; i < JOY_MAX_AXES; i++)
	{
		// get the floating point zero-centered, potentially-inverted data for the current axis
		fAxisValue = (float) *pdwRawValue[i];
		// move centerpoint to zero
		fAxisValue -= 32768.0;

		if (joy_wwhack2.value != 0.0)
		{
			if (dwAxisMap[i] == AxisTurn)
			{
				// this is a special formula for the Logitech WingMan Warrior
				// y=ax^b; where a = 300 and b = 1.3
				// also x values are in increments of 800 (so this is factored out)
				// then bounds check result to level out excessively high spin rates
				fTemp = 300.0 * pow(fabs(fAxisValue) / 800.0, 1.3);
				if (fTemp > 14000.0)
					fTemp = 14000.0;
				// restore direction information
				fAxisValue = (fAxisValue > 0.0) ? fTemp : -fTemp;
			}
		}

		// convert range from -32768..32767 to -1..1 
		fAxisValue /= 32768.0;

		switch (dwAxisMap[i])
		{
		case AxisForward:
			if ((joy_advanced.value == 0.0) && (in_mlook.state & 1))
			{
				// user wants forward control to become look control
				if (fabs(fAxisValue) > joy_pitchthreshold.value)
				{		
					// if mouse invert is on, invert the joystick pitch value
					// only absolute control support here (joy_advanced is false)
					if (m_pitch.value < 0.0)
					{
						cl.viewangles[PITCH] -= (fAxisValue * joy_pitchsensitivity.value) * aspeed * cl_pitchspeed.value;
					}
					else
					{
						cl.viewangles[PITCH] += (fAxisValue * joy_pitchsensitivity.value) * aspeed * cl_pitchspeed.value;
					}
					V_StopPitchDrift();
				}
				else
				{
					// no pitch movement
					// disable pitch return-to-center unless requested by user
					// *** this code can be removed when the lookspring bug is fixed
					// *** the bug always has the lookspring feature on
					if(lookspring.value == 0.0)
						V_StopPitchDrift();
				}
			}
			else
			{
				// user wants forward control to be forward control
				if (fabs(fAxisValue) > joy_forwardthreshold.value)
				{
					cmd->forwardmove += (fAxisValue * joy_forwardsensitivity.value) * speed * cl_forwardspeed.value;
				}
			}
			break;

		case AxisSide:
			if (fabs(fAxisValue) > joy_sidethreshold.value)
			{
				cmd->sidemove += (fAxisValue * joy_sidesensitivity.value) * speed * cl_sidespeed.value;
			}
			break;

		case AxisTurn:
			if ((in_strafe.state & 1) || (lookstrafe.value && (in_mlook.state & 1)))
			{
				// user wants turn control to become side control
				if (fabs(fAxisValue) > joy_sidethreshold.value)
				{
					cmd->sidemove -= (fAxisValue * joy_sidesensitivity.value) * speed * cl_sidespeed.value;
				}
			}
			else
			{
				// user wants turn control to be turn control
				if (fabs(fAxisValue) > joy_yawthreshold.value)
				{
					if(dwControlMap[i] == JOY_ABSOLUTE_AXIS)
					{
						cl.viewangles[YAW] += (fAxisValue * joy_yawsensitivity.value) * aspeed * cl_yawspeed.value;
					}
					else
					{
						cl.viewangles[YAW] += (fAxisValue * joy_yawsensitivity.value) * speed * 180.0;
					}

				}
			}
			break;

		case AxisLook:
			if (in_mlook.state & 1)
			{
				if (fabs(fAxisValue) > joy_pitchthreshold.value)
				{
					// pitch movement detected and pitch movement desired by user
					if(dwControlMap[i] == JOY_ABSOLUTE_AXIS)
					{
						cl.viewangles[PITCH] += (fAxisValue * joy_pitchsensitivity.value) * aspeed * cl_pitchspeed.value;
					}
					else
					{
						cl.viewangles[PITCH] += (fAxisValue * joy_pitchsensitivity.value) * speed * 180.0;
					}
					V_StopPitchDrift();
				}
				else
				{
					// no pitch movement
					// disable pitch return-to-center unless requested by user
					// *** this code can be removed when the lookspring bug is fixed
					// *** the bug always has the lookspring feature on
					if(lookspring.value == 0.0)
						V_StopPitchDrift();
				}
			}
			break;

		default:
			break;
		}
	}

	// bounds check pitch
	if (cl.viewangles[PITCH] > 80.0)
		cl.viewangles[PITCH] = 80.0;
	if (cl.viewangles[PITCH] < -70.0)
		cl.viewangles[PITCH] = -70.0;
}
