#include "test_stubs.h"

#include <cstdarg>
#include <cstdio>

// mathlib.cpp's only external dependency (beyond header declarations) is
// Sys_Error (sys.h). The real implementation (sys_win.cpp) shows a dialog
// and calls ExitProcess -- unusable inside a test binary. This throws
// instead, so BoxOnPlaneSide's and FloorDivMod's error paths can be
// asserted with CHECK_THROWS_AS rather than only testing happy paths.
[[noreturn]] void Sys_Error (const char *error, ...)
{
	char text[1024];
	va_list argptr;
	va_start (argptr, error);
	vsnprintf (text, sizeof(text), error, argptr);
	va_end (argptr);
	throw SysErrorException (text);
}
