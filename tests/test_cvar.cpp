#include <doctest/doctest.h>

#include "quakedef.h"
#include "test_stubs.h"

#include <cstdio>
#include <cstring>

// cvar_vars is a persistent, process-lifetime registry (see the plan's
// note on test isolation) -- every cvar_t registered here must have
// static storage duration, exactly like the ~183 real cvar_t globals
// scattered across the codebase (Cvar_RegisterVariable stores a raw,
// non-owning pointer). Each test uses a unique "__test_cvar_*" name
// prefix and only asserts on the specific entries it added, never on the
// registry's total size or contents.

TEST_CASE ("Cvar_RegisterVariable / Cvar_FindVar round trip")
{
	static cvar_t testCvar{};
	testCvar.name = "__test_cvar_roundtrip";
	testCvar.string = "1";

	Cvar_RegisterVariable (&testCvar);

	auto found = Cvar_FindVar ("__test_cvar_roundtrip");
	REQUIRE (found.has_value ());
	CHECK (*found == &testCvar);
	CHECK ((*found)->value == doctest::Approx (1.0f));
	CHECK (Cvar_VariableValue ("__test_cvar_roundtrip") == doctest::Approx (1.0f));
	CHECK (std::string (Cvar_VariableString ("__test_cvar_roundtrip")) == "1");
}

TEST_CASE ("Cvar_FindVar / Cvar_VariableValue / Cvar_VariableString on unknown name")
{
	CHECK (Cvar_FindVar ("__test_cvar_does_not_exist") == std::nullopt);
	CHECK (Cvar_VariableValue ("__test_cvar_does_not_exist") == doctest::Approx (0.0f));
	CHECK (std::string (Cvar_VariableString ("__test_cvar_does_not_exist")) == "");
}

TEST_CASE ("Cvar_Set / Cvar_SetValue update value and string together")
{
	static cvar_t testCvar{};
	testCvar.name = "__test_cvar_set";
	testCvar.string = "0";
	Cvar_RegisterVariable (&testCvar);

	Cvar_Set ("__test_cvar_set", "42");
	CHECK (std::string (testCvar.string) == "42");
	CHECK (testCvar.value == doctest::Approx (42.0f));

	Cvar_SetValue ("__test_cvar_set", 3.5f);
	CHECK (testCvar.value == doctest::Approx (3.5f));
	// Exact match, not just a substring check: Cvar_SetValue formats via
	// va("{:f}", value) to match printf's old %f exactly (fixed 6 decimal
	// places). A bare {} would use std::format's shortest-round-trip
	// float representation instead ("3.5" rather than "3.500000") --
	// silently changing what gets written into a cvar's serialized
	// string value. A substring check like .find("3.5") wouldn't catch
	// that regression since both outputs contain "3.5"; this must be an
	// exact match.
	CHECK (std::string (Cvar_VariableString ("__test_cvar_set")) == "3.500000");
}

TEST_CASE ("Cvar_Set on an unregistered name reports an error, does not crash")
{
	ClearConPrint ();
	Cvar_Set ("__test_cvar_never_registered", "1");
	CHECK (g_lastConPrint.find ("not found") != std::string::npos);
}

TEST_CASE ("Cvar_RegisterVariable rejects a duplicate name")
{
	static cvar_t first{};
	first.name = "__test_cvar_dup";
	first.string = "1";
	Cvar_RegisterVariable (&first);

	static cvar_t second{};
	second.name = "__test_cvar_dup";
	second.string = "2";

	ClearConPrint ();
	Cvar_RegisterVariable (&second);

	CHECK (g_lastConPrint.find ("allready defined") != std::string::npos);
	// the original registration must be the one still in the registry
	CHECK (Cvar_FindVar ("__test_cvar_dup") == &first);
}

TEST_CASE ("Cvar_RegisterVariable rejects a name already used by a command")
{
	Cmd_AddCommand ("__test_cvar_cmd_collision", [] () {});

	static cvar_t testCvar{};
	testCvar.name = "__test_cvar_cmd_collision";
	testCvar.string = "1";

	ClearConPrint ();
	Cvar_RegisterVariable (&testCvar);

	CHECK (g_lastConPrint.find ("is a command") != std::string::npos);
	CHECK (Cvar_FindVar ("__test_cvar_cmd_collision") == std::nullopt);
}

TEST_CASE ("Cvar_WriteVariables writes archived cvars, skips non-archived")
{
	static cvar_t archived{};
	archived.name = "__test_cvar_archived";
	archived.string = "yes";
	archived.archive = true;
	Cvar_RegisterVariable (&archived);

	static cvar_t notArchived{};
	notArchived.name = "__test_cvar_not_archived";
	notArchived.string = "no";
	notArchived.archive = false;
	Cvar_RegisterVariable (&notArchived);

	FILE *f = tmpfile ();
	REQUIRE (f != nullptr);
	Cvar_WriteVariables (f);

	rewind (f);
	char buffer[4096] = {};
	fread (buffer, 1, sizeof (buffer) - 1, f);
	fclose (f);

	std::string contents (buffer);
	CHECK (contents.find ("__test_cvar_archived \"yes\"") != std::string::npos);
	CHECK (contents.find ("__test_cvar_not_archived") == std::string::npos);
}

TEST_CASE ("Cvar_NextServerVar enumerates only .server cvars, in name order")
{
	static cvar_t serverA{};
	serverA.name = "__test_cvar_server_a";
	serverA.string = "1";
	serverA.server = true;
	Cvar_RegisterVariable (&serverA);

	static cvar_t nonServer{};
	nonServer.name = "__test_cvar_server_ab_not_server";
	nonServer.string = "1";
	nonServer.server = false;
	Cvar_RegisterVariable (&nonServer);

	static cvar_t serverB{};
	serverB.name = "__test_cvar_server_b";
	serverB.string = "1";
	serverB.server = true;
	Cvar_RegisterVariable (&serverB);

	cvar_t *first = Cvar_NextServerVar ("__test_cvar_server_a");
	REQUIRE (first != nullptr);
	CHECK (std::string (first->name) == "__test_cvar_server_b");

	cvar_t *second = Cvar_NextServerVar (first->name);
	CHECK (second == nullptr); // no more .server cvars after serverB in this namespace
}

TEST_CASE ("Cvar_NextServerVar with an unknown name returns null")
{
	CHECK (Cvar_NextServerVar ("__test_cvar_totally_unknown_name") == nullptr);
}

TEST_CASE ("Cvar_CompleteVariable prefix matching")
{
	static cvar_t testCvar{};
	testCvar.name = "__test_cvar_completeme";
	testCvar.string = "1";
	Cvar_RegisterVariable (&testCvar);

	auto match = Cvar_CompleteVariable ("__test_cvar_complet");
	REQUIRE (match.has_value ());
	CHECK (*match == "__test_cvar_completeme");

	CHECK (Cvar_CompleteVariable ("__test_cvar_nomatch_prefix_xyz") == std::nullopt);
	CHECK (Cvar_CompleteVariable ("") == std::nullopt);
}

TEST_CASE ("Cvar_Command prints or sets via the console dispatch path")
{
	static cvar_t testCvar{};
	testCvar.name = "__test_cvar_command";
	testCvar.string = "start";
	Cvar_RegisterVariable (&testCvar);

	// print form: just the cvar name, no value argument
	Cmd_TokenizeString ("__test_cvar_command");
	ClearConPrint ();
	CHECK (Cvar_Command () == true);
	CHECK (g_lastConPrint.find ("start") != std::string::npos);

	// set form: cvar name plus a value argument
	Cmd_TokenizeString ("__test_cvar_command changed");
	CHECK (Cvar_Command () == true);
	CHECK (std::string (testCvar.string) == "changed");

	// not a cvar at all
	Cmd_TokenizeString ("__test_cvar_command_unknown_name");
	CHECK (Cvar_Command () == false);
}
