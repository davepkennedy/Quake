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

// Thrown by the test-only Host_Error implementation in test_stubs.cpp,
// standing in for the real host.cpp version's Host_ShutdownServer +
// CL_Disconnect + Host_AbortFrame unwind. PR_RunError funnels through
// Host_Error (see pr_exec.cpp), so this is what CHECK_THROWS_AS asserts
// against for VM error paths (runaway loop, stack overflow, bad opcode, ...).
struct HostErrorException : std::runtime_error
{
	explicit HostErrorException (const std::string &msg) : std::runtime_error (msg) {}
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

// Idempotent, shared across every test file: the real zone.cpp requires
// Memory_Init to have run before any Z_/Hunk_/Cache_ call is safe (its
// state starts as a null hunk_base). Both test_cmd.cpp's alias-dispatch
// test (Cbuf_Init -> SZ_Alloc -> Hunk_AllocName) and test_zone.cpp call
// this directly, since doctest's TEST_CASE execution order across files
// isn't something to rely on -- whichever runs first performs the real
// initialization exactly once.
void EnsureMemoryInit ();

// Idempotent (static-bool guarded, same shape as EnsureMemoryInit): calls
// EnsureMemoryInit(), sets the LittleLong/LittleShort byte-swap function
// pointers to identity (this project only targets little-endian x64
// Windows, so there's no real detection dance to reproduce -- see
// common.cpp's COM_Init), then calls the real PR_LoadProgs() once against
// tests/fixtures/progs.dat (extracted from id1/PAK0.PAK -- see
// tools/extract_progs_fixture.ps1). Must not be called more than once per
// process: PR_LoadProgs unconditionally re-parses and re-Hunk_Allocs, and
// doctest's cross-TEST_CASE ordering isn't guaranteed, so every test that
// needs real progs.dat data calls this and relies on the guard.
void EnsureRealProgsLoaded ();

// Idempotent, must be called after EnsureRealProgsLoaded() (needs
// pr_edict_size to size the pool correctly). Hunk_Allocs a small edict
// pool, points sv.edicts/max_edicts at it, and reserves slot 0 as the
// world entity (sv.num_edicts = 1) -- matching the real engine's
// convention (ED_Alloc starts scanning from svs.maxclients+1). Tests that
// call ED_Alloc/EDICT_NUM/PROG_TO_EDICT need this first; sv{} otherwise
// zero-inits max_edicts to 0, so every edict access fails immediately.
void EnsureTestEdictsInit ();
