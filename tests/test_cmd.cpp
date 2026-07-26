#include <doctest/doctest.h>

#include "quakedef.h"
#include "test_stubs.h"

// cmd_functions/cmd_alias (static to cmd.cpp) and cvar_vars are all
// persistent, process-lifetime registries -- see test_cvar.cpp's header
// comment. Tests use unique "__test_cmd_*" name prefixes and assert only
// on the specific entries they added.

// Cbuf_Init/Cmd_Init only need to run once per process (they're normally
// called exactly once from Host_Init, which this test project never
// runs). Cmd_Init registers the real "alias" -> Cmd_Alias_f (among
// others) the same way the engine does, so the alias test below doesn't
// need its own forward-declared workaround.
static void EnsureCmdInit ()
{
	static bool initialized = false;
	if (!initialized)
	{
		EnsureMemoryInit (); // Cbuf_Init -> SZ_Alloc -> Hunk_AllocName needs this first
		Cbuf_Init ();
		Cmd_Init ();
		initialized = true;
	}
}

TEST_CASE ("Cmd_AddCommand / Cmd_Exists / dispatch through Cmd_ExecuteString")
{
	static bool fired = false;
	fired = false;
	Cmd_AddCommand ("__test_cmd_basic", [] () { fired = true; });

	CHECK (Cmd_Exists ("__test_cmd_basic") == true);
	CHECK (Cmd_Exists ("__test_cmd_never_registered") == false);

	Cmd_ExecuteString ("__test_cmd_basic", cmd_source_t::src_command);
	CHECK (fired == true);
}

TEST_CASE ("Cmd_AddCommand rejects a duplicate name")
{
	Cmd_AddCommand ("__test_cmd_dup", [] () {});

	ClearConPrint ();
	Cmd_AddCommand ("__test_cmd_dup", [] () {});
	CHECK (g_lastConPrint.find ("already defined") != std::string::npos);
}

TEST_CASE ("Command dispatch is case-insensitive")
{
	static int callCount = 0;
	callCount = 0;
	Cmd_AddCommand ("__test_cmd_case", [] () { callCount++; });

	Cmd_ExecuteString ("__TEST_CMD_CASE", cmd_source_t::src_command);
	Cmd_ExecuteString ("__Test_Cmd_Case", cmd_source_t::src_command);
	Cmd_ExecuteString ("__test_cmd_case", cmd_source_t::src_command);

	CHECK (callCount == 3);
}

TEST_CASE ("Cmd_CompleteCommand prefix matching")
{
	Cmd_AddCommand ("__test_cmd_completeme", [] () {});

	auto match = Cmd_CompleteCommand ("__test_cmd_complet");
	REQUIRE (match.has_value ());
	CHECK (*match == "__test_cmd_completeme");

	CHECK (Cmd_CompleteCommand ("__test_cmd_nomatch_xyz") == std::nullopt);
	CHECK (Cmd_CompleteCommand ("") == std::nullopt);
}

TEST_CASE ("alias defines and dispatches a command, case-insensitively, end to end")
{
	EnsureCmdInit ();

	static bool aliasFired = false;
	aliasFired = false;
	Cmd_AddCommand ("__test_cmd_alias_target", [] () { aliasFired = true; });

	// "alias <name> <command>" -- reaches Cmd_Alias_f (registered under
	// "alias" by the real Cmd_Init above) through ordinary dispatch.
	Cmd_ExecuteString ("alias __test_cmd_myalias __test_cmd_alias_target", cmd_source_t::src_command);

	// invoke the alias in a different case than it was defined
	Cmd_ExecuteString ("__TEST_CMD_MYALIAS", cmd_source_t::src_command);
	// Cmd_ExecuteString only inserts the alias's expansion into the
	// command buffer (Cbuf_InsertText) -- Cbuf_Execute is what actually
	// runs it, proving the aliased command really fires, not just that
	// insertion happened.
	Cbuf_Execute ();

	CHECK (aliasFired == true);
}

TEST_CASE ("unknown command falls through to Cvar_Command, then reports Unknown command")
{
	ClearConPrint ();
	Cmd_ExecuteString ("__test_cmd_totally_unknown_xyz", cmd_source_t::src_command);
	CHECK (g_lastConPrint.find ("Unknown command") != std::string::npos);
}

TEST_CASE ("a real cvar shadows an unknown-command report when typed at the console")
{
	static cvar_t testCvar{};
	testCvar.name = "__test_cmd_cvar_dispatch";
	testCvar.string = "5";
	Cvar_RegisterVariable (&testCvar);

	ClearConPrint ();
	Cmd_ExecuteString ("__test_cmd_cvar_dispatch 10", cmd_source_t::src_command);

	CHECK (g_lastConPrint.find ("Unknown command") == std::string::npos);
	CHECK (std::string (testCvar.string) == "10");
}
