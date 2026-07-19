#pragma once

#include <stdexcept>
#include <string>

// Thrown by the test-only Sys_Error implementation in test_stubs.cpp,
// standing in for the real sys_win.cpp version's dialog + ExitProcess.
struct SysErrorException : std::runtime_error
{
	explicit SysErrorException (const std::string &msg) : std::runtime_error (msg) {}
};
