#include <doctest/doctest.h>

#include "quakedef.h"
#include "test_stubs.h"

namespace
{
	// Sets a global slot's raw INTEGER bit pattern (not a numeric float
	// conversion) -- matches how eval_t's edict/_int/function/string
	// members all reinterpret the same 4 bytes that pr_exec.cpp's opcodes
	// read. A plain `globals[ofs] = 2.0f;` would write float 2.0's bit
	// pattern (0x40000000), not integer 2 (0x00000002) -- wrong for any
	// slot an opcode reads as ->_int/->edict/->function/->string.
	void SetGlobalInt (float *globals, int ofs, int value)
	{
		*reinterpret_cast<int *> (&globals[ofs]) = value;
	}

	// Synthetic VM program data (pr_globals/pr_functions/pr_statements/
	// progs, plus the call-stack depth counters and builtin table) are
	// cross-TEST_CASE singletons -- the same bare file-scope globals the
	// whole survey behind this test file exists to eventually encapsulate.
	// This guard borrows them for the lifetime of one TEST_CASE, pointing
	// them at hand-built local bytecode instead of whatever real
	// progs.dat data another test may have loaded, and restores the
	// previous values on destruction (including on the exception path --
	// CHECK_THROWS_AS catches inside the TEST_CASE body, so this
	// destructor still runs at normal scope exit either way). Without
	// this, a synthetic test's member-array storage would leave dangling
	// pointers for whichever test runs next.
	//
	// Deliberately does NOT touch pr_edict_size: opcode tests that never
	// address real edict memory don't care about it, and the one test
	// that does (real edict field round-trip) needs it to stay at
	// whatever EnsureTestEdictsInit() already set it to.
	struct SyntheticProgsGuard
	{
		static constexpr int NUM_GLOBALS = 256;
		static constexpr int NUM_STATEMENTS = 64;
		static constexpr int NUM_FUNCTIONS = 8;
		static constexpr int NUM_GLOBALDEFS = 16;

		dprograms_t header{};
		float globals[NUM_GLOBALS]{};
		dstatement_t statements[NUM_STATEMENTS]{};
		dfunction_t functions[NUM_FUNCTIONS]{};
		ddef_t globaldefs[NUM_GLOBALDEFS]{};
		char strings[1] = {0};

		dprograms_t *savedProgs;
		dfunction_t *savedFunctions;
		dstatement_t *savedStatements;
		char *savedStrings;
		float *savedGlobals;
		globalvars_t *savedGlobalStruct;
		ddef_t *savedGlobaldefs;
		int savedDepth;
		int savedLocalstackUsed;
		builtin_t *savedBuiltins;
		int savedNumBuiltins;

		SyntheticProgsGuard ()
			: savedProgs (progs), savedFunctions (pr_functions), savedStatements (pr_statements),
			  savedStrings (pr_strings), savedGlobals (pr_globals), savedGlobalStruct (pr_global_struct),
			  savedGlobaldefs (pr_globaldefs), savedDepth (pr_vm.depth), savedLocalstackUsed (pr_vm.localstack_used),
			  savedBuiltins (pr_builtins), savedNumBuiltins (pr_numbuiltins)
		{
			header.numfunctions = NUM_FUNCTIONS;
			header.numstatements = NUM_STATEMENTS;
			header.numglobaldefs = 0; // tests that need globaldefs set header.numglobaldefs themselves
			progs = &header;
			pr_functions = functions;
			pr_statements = statements;
			pr_strings = strings;
			pr_globals = globals;
			pr_global_struct = reinterpret_cast<globalvars_t *> (globals);
			pr_globaldefs = globaldefs;
			pr_vm.depth = 0;
			pr_vm.localstack_used = 0;
		}

		~SyntheticProgsGuard ()
		{
			progs = savedProgs;
			pr_functions = savedFunctions;
			pr_statements = savedStatements;
			pr_strings = savedStrings;
			pr_globals = savedGlobals;
			pr_global_struct = savedGlobalStruct;
			pr_globaldefs = savedGlobaldefs;
			pr_vm.depth = savedDepth;
			pr_vm.localstack_used = savedLocalstackUsed;
			pr_builtins = savedBuiltins;
			pr_numbuiltins = savedNumBuiltins;
		}
	};
}

// ===========================================================================
// Synthetic opcode / call-stack tests -- hand-built dfunction_t/dstatement_t
// sequences, no PR_LoadProgs involved. Precise per-opcode coverage.
// ===========================================================================

TEST_CASE ("PR_ExecuteProgram: OP_ADD_F computes a float sum")
{
	SyntheticProgsGuard guard;
	constexpr int SCRATCH = 40;

	guard.functions[1] = {0, OFS_PARM0, 0, 0, 0, 0, 0, {}};
	guard.statements[0] = {OP_ADD_F, OFS_PARM0, OFS_PARM1, SCRATCH};
	guard.statements[1] = {OP_RETURN, SCRATCH, 0, 0};

	guard.globals[OFS_PARM0] = 2.0f;
	guard.globals[OFS_PARM1] = 3.0f;

	PR_ExecuteProgram (1);

	CHECK (guard.globals[OFS_RETURN] == doctest::Approx (5.0f));
}

TEST_CASE ("PR_ExecuteProgram: OP_ADD_V / OP_MUL_V vector arithmetic")
{
	SyntheticProgsGuard guard;
	constexpr int SCRATCH = 40;

	guard.functions[1] = {0, OFS_PARM0, 0, 0, 0, 0, 0, {}};
	guard.statements[0] = {OP_ADD_V, OFS_PARM0, OFS_PARM1, SCRATCH};
	guard.statements[1] = {OP_RETURN, SCRATCH, 0, 0};

	guard.globals[OFS_PARM0 + 0] = 1;
	guard.globals[OFS_PARM0 + 1] = 2;
	guard.globals[OFS_PARM0 + 2] = 3;
	guard.globals[OFS_PARM1 + 0] = 10;
	guard.globals[OFS_PARM1 + 1] = 20;
	guard.globals[OFS_PARM1 + 2] = 30;

	PR_ExecuteProgram (1);

	CHECK (guard.globals[OFS_RETURN + 0] == doctest::Approx (11));
	CHECK (guard.globals[OFS_RETURN + 1] == doctest::Approx (22));
	CHECK (guard.globals[OFS_RETURN + 2] == doctest::Approx (33));

	// OP_MUL_V is the dot product (a scalar result), distinct from OP_ADD_V's
	// per-component sum -- worth covering separately since it's the one
	// "V op V -> F" arithmetic opcode.
	guard.statements[0] = {OP_MUL_V, OFS_PARM0, OFS_PARM1, SCRATCH};
	PR_ExecuteProgram (1);
	CHECK (guard.globals[OFS_RETURN] == doctest::Approx (1 * 10 + 2 * 20 + 3 * 30));
}

TEST_CASE ("PR_ExecuteProgram: OP_IFNOT/OP_GOTO select the correct if/else branch")
{
	// i=0: IFNOT cond, b=3   -- if cond==0 (false), jump to i=3 (skip i=1,2)
	// i=1: STORE_F TRUE_VAL -> RESULT     ("then", only reached if cond true)
	// i=2: GOTO +2                        -- skip the "else" block at i=3
	// i=3: STORE_F FALSE_VAL -> RESULT    ("else", only reached via the jump)
	// i=4: RETURN RESULT
	constexpr int COND = 40, TRUE_VAL = 41, FALSE_VAL = 42, RESULT = 43;

	auto run = [&] (bool cond) {
		SyntheticProgsGuard guard;
		guard.functions[1] = {0, OFS_PARM0, 0, 0, 0, 0, 0, {}};
		guard.statements[0] = {OP_IFNOT, COND, 3, 0};
		guard.statements[1] = {OP_STORE_F, TRUE_VAL, RESULT, 0};
		guard.statements[2] = {OP_GOTO, 2, 0, 0};
		guard.statements[3] = {OP_STORE_F, FALSE_VAL, RESULT, 0};
		guard.statements[4] = {OP_RETURN, RESULT, 0, 0};

		guard.globals[COND] = cond ? 1.0f : 0.0f;
		guard.globals[TRUE_VAL] = 999.0f;
		guard.globals[FALSE_VAL] = 111.0f;

		PR_ExecuteProgram (1);
		return guard.globals[OFS_RETURN];
	};

	CHECK (run (true) == doctest::Approx (999.0f));
	CHECK (run (false) == doctest::Approx (111.0f));
}

TEST_CASE ("PR_ExecuteProgram: OP_GOTO jumps backward correctly")
{
	// Execution starts at i=2 (first_statement=2, proving a function can
	// validly start anywhere in the shared statements array, not just 0 --
	// matching real compiled progs.dat layout). i=2 is a GOTO with a=-2,
	// jumping backward to land exactly on i=0.
	SyntheticProgsGuard guard;
	constexpr int RESULT = 40;

	guard.functions[1] = {2, OFS_PARM0, 0, 0, 0, 0, 0, {}};
	guard.statements[0] = {OP_STORE_F, 41, RESULT, 0}; // backward-jump target
	guard.statements[1] = {OP_RETURN, RESULT, 0, 0};
	guard.statements[2] = {OP_GOTO, -2, 0, 0}; // s(=2) += -2-1 = -3 -> s=-1, then s++ => s=0

	guard.globals[41] = 777.0f;

	PR_ExecuteProgram (1);

	CHECK (guard.globals[OFS_RETURN] == doctest::Approx (777.0f));
}

TEST_CASE ("PR_EnterFunction/PR_LeaveFunction: depth and parameters propagate across a real QC-to-QC call")
{
	// caller (functions[1]): stores two constants into OFS_PARM0/1, CALL0s
	// the callee, then forwards OFS_RETURN as its own return value.
	// callee (functions[2], first_statement=10, deliberately far from the
	// caller's statements to prove first_statement placement is arbitrary):
	// ADD_F's its two now-copied-in parameters and RETURNs the sum.
	SyntheticProgsGuard guard;
	constexpr int CONST_A = 60, CONST_B = 61, FUNC_SLOT = 62;
	constexpr int CALLEE_PARM_START = 50; // callee's incoming-parameter/local slots
	constexpr int CALLEE_RESULT = 52;

	guard.functions[1] = {0, OFS_PARM0, 0, 0, 0, 0, 0, {}}; // caller
	guard.functions[2] = {
		10, CALLEE_PARM_START, /*locals=*/2, 0, 0, 0, /*numparms=*/2, {1, 1}}; // callee: two 1-word float params

	guard.statements[0] = {OP_STORE_F, CONST_A, OFS_PARM0, 0};
	guard.statements[1] = {OP_STORE_F, CONST_B, OFS_PARM1, 0};
	guard.statements[2] = {OP_CALL0, FUNC_SLOT, 0, 0};
	guard.statements[3] = {OP_RETURN, OFS_RETURN, 0, 0};

	guard.statements[10] = {OP_ADD_F, CALLEE_PARM_START, CALLEE_PARM_START + 1, CALLEE_RESULT};
	guard.statements[11] = {OP_RETURN, CALLEE_RESULT, 0, 0};

	guard.globals[CONST_A] = 4.0f;
	guard.globals[CONST_B] = 6.0f;
	SetGlobalInt (guard.globals, FUNC_SLOT, 2); // callee's function index

	PR_ExecuteProgram (1);

	CHECK (guard.globals[OFS_RETURN] == doctest::Approx (10.0f));
	CHECK (pr_vm.depth == 0);
}

TEST_CASE ("PR_ExecuteProgram: OP_CALL0 dispatches to a builtin without pushing a call frame")
{
	// Builtins execute inline (pr_exec.cpp's OP_CALLn case calls
	// pr_builtins[i]() directly, with no PR_EnterFunction) -- so pr_vm.depth
	// during the builtin should equal the CALLER's depth (1), not 2.
	static int depthDuringBuiltin = -1;
	static builtin_t testBuiltins[2] = {nullptr, [] () { depthDuringBuiltin = pr_vm.depth; }};

	SyntheticProgsGuard guard;
	constexpr int FUNC_SLOT = 40;

	guard.functions[1] = {0, OFS_PARM0, 0, 0, 0, 0, 0, {}};
	guard.functions[2] = {-1, 0, 0, 0, 0, 0, 0, {}}; // builtin marker: pr_builtins[1]

	guard.statements[0] = {OP_CALL0, FUNC_SLOT, 0, 0};
	guard.statements[1] = {OP_RETURN, 0, 0, 0};

	SetGlobalInt (guard.globals, FUNC_SLOT, 2);
	pr_builtins = testBuiltins;
	pr_numbuiltins = 2;

	depthDuringBuiltin = -1;
	PR_ExecuteProgram (1);

	CHECK (depthDuringBuiltin == 1);
	CHECK (pr_vm.depth == 0);
}

TEST_CASE ("PR_EnterFunction: unbounded recursive CALL0 throws once MAX_STACK_DEPTH is exceeded")
{
	SyntheticProgsGuard guard;
	constexpr int FUNC_SLOT = 40;

	guard.functions[1] = {0, OFS_PARM0, 0, 0, 0, 0, 0, {}};
	guard.statements[0] = {OP_CALL0, FUNC_SLOT, 0, 0}; // recurses into itself, never returns
	SetGlobalInt (guard.globals, FUNC_SLOT, 1);

	CHECK_THROWS_AS (PR_ExecuteProgram (1), HostErrorException);
}

TEST_CASE ("PR_ExecuteProgram: runaway loop error throws after too many statements")
{
	// OP_GOTO with a=0 jumps to itself: s += 0-1 = -1, then s++ => s stays
	// at 0 forever. No recursion/stack growth (unlike the test above) --
	// this trivial infinite loop burns through the runaway budget instead.
	SyntheticProgsGuard guard;

	guard.functions[1] = {0, OFS_PARM0, 0, 0, 0, 0, 0, {}};
	guard.statements[0] = {OP_GOTO, 0, 0, 0};

	CHECK_THROWS_AS (PR_ExecuteProgram (1), HostErrorException);
}

TEST_CASE ("PR_ExecuteProgram: nullptr function call throws")
{
	SyntheticProgsGuard guard;
	guard.functions[1] = {0, OFS_PARM0, 0, 0, 0, 0, 0, {}};
	guard.statements[0] = {OP_CALL0, 45, 0, 0}; // globals[45] is 0 by default -- nullptr function

	CHECK_THROWS_AS (PR_ExecuteProgram (1), HostErrorException);
}

TEST_CASE ("PR_ExecuteProgram: out-of-range opcode throws")
{
	SyntheticProgsGuard guard;
	guard.functions[1] = {0, OFS_PARM0, 0, 0, 0, 0, 0, {}};
	guard.statements[0] = {9999, 0, 0, 0};

	CHECK_THROWS_AS (PR_ExecuteProgram (1), HostErrorException);
}

TEST_CASE ("PR_ExecuteProgram: out-of-range function number throws")
{
	SyntheticProgsGuard guard;
	CHECK_THROWS_AS (PR_ExecuteProgram (999999), HostErrorException);
}

// ===========================================================================
// Real progs.dat tests -- id1's actual compiled QuakeC bytecode, extracted
// from id1/PAK0.PAK into tests/fixtures/progs.dat (see
// tools/extract_progs_fixture.ps1). Exercises the real, unmodified
// PR_LoadProgs and the real field/function reflection tables.
// ===========================================================================

TEST_CASE ("PR_LoadProgs loads the real id1 progs.dat with a valid header")
{
	EnsureRealProgsLoaded ();

	CHECK (progs->version == PROG_VERSION);
	CHECK (progs->crc == PROGHEADER_CRC);
	CHECK (progs->numfunctions > 0);
	CHECK (pr_crc != 0);
}

TEST_CASE ("COM_LoadHunkFile test-fixture loader returns nullptr for a missing file")
{
	EnsureMemoryInit ();

	byte *result = COM_LoadHunkFile ("this_file_does_not_exist.dat");

	CHECK (result == nullptr);
	CHECK (com_filesize == -1);
}

TEST_CASE ("ED_FindFunction locates a known id1 function by name")
{
	EnsureRealProgsLoaded ();

	auto found = ED_FindFunction ("SUB_Null");
	CHECK (found.has_value ());
}

TEST_CASE ("PR_ExecuteProgram executes a real builtin-free QC function end-to-end")
{
	// SUB_Null (defs.qc: "void() SUB_Null = {};") is id1 QuakeC's
	// empty/no-op placeholder function -- takes no parameters and its body
	// is just an implicit return, so it exercises PR_EnterFunction/
	// PR_LeaveFunction/OP_DONE against real compiled bytecode without
	// needing pr_builtins (out of scope for Phase 1 -- see plan).
	EnsureRealProgsLoaded ();

	auto found = ED_FindFunction ("SUB_Null");
	REQUIRE (found.has_value ());

	func_t fnum = static_cast<func_t> (*found - pr_functions);
	CHECK_NOTHROW (PR_ExecuteProgram (fnum));
}

TEST_CASE ("ED_Alloc returns a usable edict from the test pool")
{
	EnsureTestEdictsInit ();

	edict_t *e = ED_Alloc ();

	CHECK (e != nullptr);
	CHECK (NUM_FOR_EDICT (e) >= 1);
}

TEST_CASE ("Real edict field round-trips through OP_ADDRESS/OP_STOREP_F/OP_LOAD_F")
{
	EnsureTestEdictsInit ();

	// Real field/edict lookups must happen before SyntheticProgsGuard swaps
	// progs/pr_globals/etc to synthetic data below -- ED_FindField reads
	// the real pr_fielddefs table.
	auto healthField = ED_FindField ("health");
	REQUIRE (healthField.has_value ());
	int fieldWordOfs = (*healthField)->ofs;

	edict_t *e = ED_Alloc ();
	int edictProgOfs = EDICT_TO_PROG (e);

	SyntheticProgsGuard guard;
	constexpr int EDICT_SLOT = 40, FIELDOFS_SLOT = 41, ADDR_SLOT = 42, VALUE_SLOT = 43, READBACK_SLOT = 44;

	guard.functions[1] = {0, OFS_PARM0, 0, 0, 0, 0, 0, {}};
	guard.statements[0] = {OP_ADDRESS, EDICT_SLOT, FIELDOFS_SLOT, ADDR_SLOT};
	guard.statements[1] = {OP_STOREP_F, VALUE_SLOT, ADDR_SLOT, 0};
	guard.statements[2] = {OP_LOAD_F, EDICT_SLOT, FIELDOFS_SLOT, READBACK_SLOT};
	guard.statements[3] = {OP_RETURN, READBACK_SLOT, 0, 0};

	SetGlobalInt (guard.globals, EDICT_SLOT, edictProgOfs);
	SetGlobalInt (guard.globals, FIELDOFS_SLOT, fieldWordOfs);
	guard.globals[VALUE_SLOT] = 123.0f;

	PR_ExecuteProgram (1);

	CHECK (guard.globals[OFS_RETURN] == doctest::Approx (123.0f));
	CHECK (e->v.health == doctest::Approx (123.0f));
}

TEST_CASE ("PR_ExecuteProgram: OP_LOAD_F with a wildly out-of-range field offset throws instead of reading OOB")
{
	// Regression test for the VM-separation phase-4 fix: OP_LOAD_F/V/S/ENT/
	// FLD/FNC used to compute ed->v + fieldOfs and dereference it directly
	// with no bounds check at all (unlike OP_STOREP_*, which always went
	// through PR_FieldAddress's check) -- a corrupted/malformed field offset
	// on a LOAD was a silent out-of-bounds read. Now routed through the same
	// PR_FieldAddress check via PR_EdictFieldOffset.
	EnsureTestEdictsInit ();

	edict_t *e = ED_Alloc ();
	int edictProgOfs = EDICT_TO_PROG (e);

	SyntheticProgsGuard guard;
	constexpr int EDICT_SLOT = 40, FIELDOFS_SLOT = 41, RESULT_SLOT = 42;
	constexpr int HUGE_BAD_FIELD_OFFSET = 10'000'000;

	guard.functions[1] = {0, OFS_PARM0, 0, 0, 0, 0, 0, {}};
	guard.statements[0] = {OP_LOAD_F, EDICT_SLOT, FIELDOFS_SLOT, RESULT_SLOT};
	guard.statements[1] = {OP_RETURN, RESULT_SLOT, 0, 0};

	SetGlobalInt (guard.globals, EDICT_SLOT, edictProgOfs);
	SetGlobalInt (guard.globals, FIELDOFS_SLOT, HUGE_BAD_FIELD_OFFSET);

	CHECK_THROWS_AS (PR_ExecuteProgram (1), SysErrorException);
}

// ===========================================================================
// PR_ValidateOperandTypes -- load-time defense-in-depth check (phase 3 of
// the VM-separation initiative, load-time-only design; see
// project_quake_vm_separation.md for why a runtime tagged-value model was
// ruled out). Synthetic ddef_t/dstatement_t data, same pattern as the
// synthetic opcode tests above.
// ===========================================================================

TEST_CASE ("PR_ValidateOperandTypes: accepts correctly-typed operands")
{
	SyntheticProgsGuard guard;
	constexpr int ENTITY_SLOT = 40, FIELD_SLOT = 41;

	guard.globaldefs[0] = {static_cast<unsigned short> (etype_t::ev_entity), ENTITY_SLOT, 0};
	guard.globaldefs[1] = {static_cast<unsigned short> (etype_t::ev_field), FIELD_SLOT, 0};
	guard.header.numglobaldefs = 2;

	guard.statements[0] = {OP_LOAD_FNC, ENTITY_SLOT, FIELD_SLOT, 42};
	guard.header.numstatements = 1;

	CHECK_NOTHROW (PR_ValidateOperandTypes ());
}

TEST_CASE ("PR_ValidateOperandTypes: rejects a mistyped operand against progs.dat's own reflection data")
{
	SyntheticProgsGuard guard;
	constexpr int SLOT = 40;

	// SLOT's own globaldef says it's a function, but this statement uses it
	// as OP_STOREP_ENT's source operand -- an entity. Real-world equivalent:
	// hand-edited/corrupted progs.dat bytecode referencing the wrong offset.
	// (ev_float/ev_vector defs are deliberately excluded from the checked
	// map entirely -- see PR_BuildOperandTypeMap -- so this test uses two
	// types that ARE both checked, function vs. entity, to produce a real
	// detectable mismatch.)
	guard.globaldefs[0] = {static_cast<unsigned short> (etype_t::ev_function), SLOT, 0};
	guard.header.numglobaldefs = 1;

	guard.statements[0] = {OP_STOREP_ENT, SLOT, 41, 0};
	guard.header.numstatements = 1;

	CHECK_THROWS_AS (PR_ValidateOperandTypes (), SysErrorException);
}

TEST_CASE ("PR_ValidateOperandTypes: a slot with no reflection entry is unverifiable, not rejected")
{
	SyntheticProgsGuard guard;

	// No globaldefs at all -- every compiler temp/local looks like this.
	guard.header.numglobaldefs = 0;
	guard.statements[0] = {OP_STOREP_ENT, 40, 41, 0};
	guard.header.numstatements = 1;

	CHECK_NOTHROW (PR_ValidateOperandTypes ());
}

TEST_CASE ("PR_ValidateOperandTypes: float/vector operands are never checked, even when mistyped")
{
	// Mirrors the real id1 progs.dat's own legitimate behavior: a global
	// offset's def can say ev_vector while an unrelated OP_ADD_F reuses that
	// same offset as a float temp elsewhere in the program (confirmed
	// empirically -- 126 such offsets exist in the shipped progs.dat). This
	// opcode family is deliberately excluded from the check entirely.
	SyntheticProgsGuard guard;
	constexpr int SLOT = 40;

	guard.globaldefs[0] = {static_cast<unsigned short> (etype_t::ev_vector), SLOT, 0};
	guard.header.numglobaldefs = 1;

	guard.statements[0] = {OP_ADD_F, SLOT, SLOT, SLOT};
	guard.header.numstatements = 1;

	CHECK_NOTHROW (PR_ValidateOperandTypes ());
}
