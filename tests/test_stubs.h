#pragma once

#include <stdexcept>
#include <string>

#include "quakedef.h"	// hull_t, trace_t, vec3_t -- for the declarations below

// Thrown by the test-only Sys_Error implementation in test_stubs.cpp,
// standing in for the real sys_win.cpp version's dialog + ExitProcess.
struct SysErrorException : std::runtime_error
{
	explicit SysErrorException (const std::string &msg) : std::runtime_error (msg) {}
};

// Captures the last message passed to the test-only Con_Printf, so tests
// can assert on error-path text (duplicate registration, name collisions,
// "Unknown command") instead of only checking "didn't crash".
extern std::string g_lastConPrint;
void ClearConPrint ();

// world.cpp/sv_phys.cpp declarations for the functions duplicated (for
// test isolation -- see the banner comment above their definitions in
// test_stubs.cpp) into this test project. Only SV_RecursiveHullCheck has
// a real public declaration (world.h); the other three are file-local
// helpers in the real engine with no header declaration to reuse.
int SV_HullPointContents (hull_t *hull, int num, vec3_t p);
int ClipVelocity (vec3_t in, vec3_t normal, vec3_t out, float overbounce);
void SV_WallFriction (edict_t *ent, trace_t *trace);
