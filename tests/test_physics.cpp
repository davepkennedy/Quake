#include <doctest/doctest.h>

#include "quakedef.h"
#include "test_stubs.h"

namespace
{
	// A single-plane hull representing an infinite floor at z=0: normal
	// (0,0,1), dist 0, axis-aligned (type 2 = Z, so SV_HullPointContents/
	// SV_RecursiveHullCheck use the direct p[type] axis lookup rather than
	// the general DotProduct path). children[0] (d>=0, at/above the
	// floor) is CONTENTS_EMPTY; children[1] (d<0, below the floor) is
	// CONTENTS_SOLID.
	hull_t MakeFloorHull (mplane_t &plane, dclipnode_t &node)
	{
		plane = mplane_t{};
		plane.normal[0] = 0; plane.normal[1] = 0; plane.normal[2] = 1;
		plane.dist = 0;
		plane.type = 2;

		node.planenum = 0;
		node.children[0] = CONTENTS_EMPTY;
		node.children[1] = CONTENTS_SOLID;

		hull_t hull{};
		hull.clipnodes = &node;
		hull.planes = &plane;
		hull.firstclipnode = 0;
		hull.lastclipnode = 0;
		return hull;
	}
}

TEST_CASE ("SV_HullPointContents resolves points on either side of the floor")
{
	mplane_t plane;
	dclipnode_t node;
	hull_t hull = MakeFloorHull (plane, node);

	vec3_t above = {0, 0, 5};
	vec3_t below = {0, 0, -5};
	CHECK (SV_HullPointContents (&hull, 0, above) == CONTENTS_EMPTY);
	CHECK (SV_HullPointContents (&hull, 0, below) == CONTENTS_SOLID);
}

TEST_CASE ("SV_HullPointContents throws on an out-of-range node number")
{
	mplane_t plane;
	dclipnode_t node;
	hull_t hull = MakeFloorHull (plane, node);

	vec3_t p = {0, 0, 0};
	CHECK_THROWS_AS (SV_HullPointContents (&hull, 5, p), SysErrorException);
}

TEST_CASE ("SV_RecursiveHullCheck stops a trace that crosses the floor")
{
	mplane_t plane;
	dclipnode_t node;
	hull_t hull = MakeFloorHull (plane, node);

	// SV_Move's own convention (mirrored here): fraction starts at 1 and
	// allsolid starts true, both cleared/overwritten as the trace proceeds.
	trace_t trace{};
	trace.fraction = 1;
	trace.allsolid = true;

	vec3_t start = {0, 0, 10};
	vec3_t end = {0, 0, -10};
	qboolean clear = SV_RecursiveHullCheck (&hull, hull.firstclipnode, 0, 1, start, end, &trace);

	CHECK (clear == false); // false = something was hit
	CHECK (trace.fraction == doctest::Approx (0.5).epsilon (0.01));
	CHECK (trace.endpos[2] == doctest::Approx (0.0).epsilon (0.1));
	CHECK (trace.plane.normal[0] == doctest::Approx (0.0f));
	CHECK (trace.plane.normal[1] == doctest::Approx (0.0f));
	CHECK (trace.plane.normal[2] == doctest::Approx (1.0f));
}

TEST_CASE ("SV_RecursiveHullCheck reports no impact for a trace that never crosses the floor")
{
	mplane_t plane;
	dclipnode_t node;
	hull_t hull = MakeFloorHull (plane, node);

	trace_t trace{};
	trace.fraction = 1;
	trace.allsolid = true;

	vec3_t start = {0, 0, 10};
	vec3_t end = {0, 0, 5}; // stays entirely above the floor
	qboolean clear = SV_RecursiveHullCheck (&hull, hull.firstclipnode, 0, 1, start, end, &trace);

	CHECK (clear == true); // true = the move completed unobstructed
	CHECK (trace.fraction == doctest::Approx (1.0));
}

TEST_CASE ("ClipVelocity sets the floor bit for an upward-facing normal")
{
	vec3_t in = {10, 0, -5};
	vec3_t normal = {0, 0, 1};
	vec3_t out;
	int blocked = ClipVelocity (in, normal, out, 1);
	CHECK ((blocked & 1) != 0); // floor bit
}

TEST_CASE ("ClipVelocity sets the step/wall bit for a vertical wall normal")
{
	vec3_t in = {10, 0, -5};
	vec3_t normal = {1, 0, 0}; // normal[2] == 0 -> a wall, not a floor
	vec3_t out;
	int blocked = ClipVelocity (in, normal, out, 1);
	CHECK ((blocked & 2) != 0); // step/wall bit
}

TEST_CASE ("ClipVelocity removes the velocity component along the normal")
{
	// Moving straight down (0,0,-10) into a floor (normal 0,0,1) should
	// come out with zero vertical component and the horizontal part
	// unaffected -- the classic "slide along the floor" case.
	vec3_t in = {5, 0, -10};
	vec3_t normal = {0, 0, 1};
	vec3_t out;
	ClipVelocity (in, normal, out, 1);
	CHECK (out[0] == doctest::Approx (5.0f));
	CHECK (out[1] == doctest::Approx (0.0f));
	CHECK (out[2] == doctest::Approx (0.0f));
}

TEST_CASE ("ClipVelocity zeroes components smaller than STOP_EPSILON")
{
	vec3_t in = {0.05f, 0, -0.05f};
	vec3_t normal = {0, 0, 1};
	vec3_t out;
	ClipVelocity (in, normal, out, 1);
	CHECK (out[0] == doctest::Approx (0.0f)); // within STOP_EPSILON (0.1) of zero
	CHECK (out[2] == doctest::Approx (0.0f));
}

TEST_CASE ("SV_WallFriction reduces tangential velocity when moving into a wall")
{
	static edict_t ent{};
	ent.v.v_angle[0] = 0; ent.v.v_angle[1] = 0; ent.v.v_angle[2] = 0; // facing +X (yaw 0)
	ent.v.velocity[0] = 100; ent.v.velocity[1] = 0; ent.v.velocity[2] = 0;

	trace_t trace{};
	trace.plane.normal[0] = -1; trace.plane.normal[1] = 0; trace.plane.normal[2] = 0; // wall facing back at the player

	float before = ent.v.velocity[0];
	SV_WallFriction (&ent, &trace);
	CHECK (ent.v.velocity[0] < before); // tangential speed was cut
}

TEST_CASE ("SV_WallFriction leaves velocity untouched when not facing into a wall")
{
	static edict_t ent{};
	ent.v.v_angle[0] = 0; ent.v.v_angle[1] = 0; ent.v.v_angle[2] = 0; // facing +X
	ent.v.velocity[0] = 100; ent.v.velocity[1] = 0; ent.v.velocity[2] = 0;

	trace_t trace{};
	trace.plane.normal[0] = 1; trace.plane.normal[1] = 0; trace.plane.normal[2] = 0; // wall facing the same way as movement, not opposing it

	SV_WallFriction (&ent, &trace);
	CHECK (ent.v.velocity[0] == doctest::Approx (100.0f));
}
