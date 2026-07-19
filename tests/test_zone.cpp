#include <doctest/doctest.h>

#include "quakedef.h"
#include "test_stubs.h"

#include <cstring>

// zone.cpp's entire allocator state (memzone_t, hunk marks, cache LRU
// list) is private to that file -- confirmed by reading the whole file,
// re-confirming Milestone 4's consolidation notes. The only way to touch
// any of it from a test is through the public Z_/Hunk_/Cache_ API, and
// Memory_Init must run first (see EnsureMemoryInit in test_stubs.h/cpp,
// shared with test_cmd.cpp since either file's tests might run first).
//
// Per the user's explicit framing for this pass: this allocator is
// intended to be replaced eventually by STL containers, so these tests
// deliberately check *behavioral contracts* (allocate returns usable
// zeroed memory, free makes it reusable, errors on misuse, marks
// round-trip) rather than internal implementation details (exact
// free-block-merge mechanics, Cache_TryAlloc's placement search order,
// LRU eviction) -- the internals are exactly what disappears when this
// is eventually replaced, so they're not worth deep coverage now.
//
// Hunk-touching tests snapshot Hunk_LowMark()/restore via
// Hunk_FreeToLowMark() -- the allocator's own intended idiom for scoped
// allocation -- so net hunk usage stays flat across test cases without
// needing a test-only reset hook.

TEST_CASE ("Z_Malloc returns usable zeroed memory; Z_Free makes it reusable")
{
	EnsureMemoryInit ();

	void *p = Z_Malloc (64);
	REQUIRE (p != nullptr);

	byte *bytes = (byte *)p;
	for (int i = 0; i < 64; i++)
		CHECK (bytes[i] == 0);

	memset (p, 0xAB, 64);
	CHECK (bytes[0] == 0xAB);

	Z_Free (p);

	// same size, reallocated -- should succeed (the freed block is reusable)
	void *p2 = Z_Malloc (64);
	CHECK (p2 != nullptr);
	Z_Free (p2);
}

TEST_CASE ("Z_Malloc/Z_Free churn does not exhaust the zone")
{
	EnsureMemoryInit ();

	// A basic "merging isn't broken" sanity check -- if adjacent free
	// blocks failed to merge back together, this would eventually fail
	// with a fragmented zone even though net usage is zero.
	for (int i = 0; i < 200; i++)
	{
		void *p = Z_Malloc (128);
		REQUIRE (p != nullptr);
		Z_Free (p);
	}
}

TEST_CASE ("Z_Free on an already-freed pointer throws")
{
	EnsureMemoryInit ();

	void *p = Z_Malloc (32);
	Z_Free (p);
	CHECK_THROWS_AS (Z_Free (p), SysErrorException);
}

TEST_CASE ("Z_Free on null throws")
{
	EnsureMemoryInit ();
	CHECK_THROWS_AS (Z_Free (nullptr), SysErrorException);
}

TEST_CASE ("Hunk_Alloc returns usable zeroed memory")
{
	EnsureMemoryInit ();

	size_t mark = Hunk_LowMark ();

	void *p = Hunk_Alloc (128);
	REQUIRE (p != nullptr);

	byte *bytes = (byte *)p;
	for (int i = 0; i < 128; i++)
		CHECK (bytes[i] == 0);
	memset (p, 0xCD, 128);
	CHECK (bytes[0] == 0xCD);

	Hunk_FreeToLowMark (mark);
}

TEST_CASE ("Hunk_LowMark / Hunk_FreeToLowMark round trip")
{
	EnsureMemoryInit ();

	size_t markBefore = Hunk_LowMark ();
	Hunk_Alloc (256);
	Hunk_Alloc (512);
	CHECK (Hunk_LowMark () > markBefore);

	Hunk_FreeToLowMark (markBefore);
	CHECK (Hunk_LowMark () == markBefore);
}

TEST_CASE ("Hunk_AllocName throws when the request exceeds remaining space")
{
	EnsureMemoryInit ();

	size_t mark = Hunk_LowMark ();
	// Larger than the whole 4MB EnsureMemoryInit backing buffer -- must
	// fail regardless of how much of it prior tests have already used.
	CHECK_THROWS_AS (Hunk_AllocName (16 * 1024 * 1024, "toobig"), SysErrorException);
	Hunk_FreeToLowMark (mark);
}

TEST_CASE ("Cache_Alloc / Cache_Check / Cache_Free")
{
	EnsureMemoryInit ();

	size_t mark = Hunk_LowMark (); // Cache_Alloc places data below the low hunk

	cache_user_t user{};
	void *data = Cache_Alloc (&user, 256, "__test_cache_entry");
	REQUIRE (data != nullptr);

	CHECK (Cache_Check (&user) == data);

	Cache_Free (&user);
	CHECK (Cache_Check (&user) == nullptr);

	Hunk_FreeToLowMark (mark);
}

TEST_CASE ("Cache_Alloc on an already-allocated cache_user_t throws")
{
	EnsureMemoryInit ();

	size_t mark = Hunk_LowMark ();

	cache_user_t user{};
	Cache_Alloc (&user, 64, "__test_cache_dup");
	CHECK_THROWS_AS (Cache_Alloc (&user, 64, "__test_cache_dup"), SysErrorException);

	Cache_Free (&user);
	Hunk_FreeToLowMark (mark);
}
