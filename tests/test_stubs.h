#pragma once

#include <stdexcept>
#include <string>

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
