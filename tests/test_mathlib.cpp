#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "quakedef.h"
#include "test_stubs.h"

#include <cmath>

namespace
{
	bool ApproxVec (const vec3_t a, const vec3_t b, float eps = 1e-4f)
	{
		return std::fabs (a[0] - b[0]) < eps
			&& std::fabs (a[1] - b[1]) < eps
			&& std::fabs (a[2] - b[2]) < eps;
	}
}

TEST_CASE ("DotProduct")
{
	vec3_t a = {1, 2, 3};
	vec3_t b = {4, 5, 6};
	CHECK (DotProduct (a, b) == doctest::Approx (32.0f));
}

TEST_CASE ("VectorAdd / VectorSubtract")
{
	vec3_t a = {1, 2, 3};
	vec3_t b = {4, -1, 2};
	vec3_t out;

	VectorAdd (a, b, out);
	CHECK (out[0] == doctest::Approx (5.0f));
	CHECK (out[1] == doctest::Approx (1.0f));
	CHECK (out[2] == doctest::Approx (5.0f));

	VectorSubtract (a, b, out);
	CHECK (out[0] == doctest::Approx (-3.0f));
	CHECK (out[1] == doctest::Approx (3.0f));
	CHECK (out[2] == doctest::Approx (1.0f));
}

TEST_CASE ("VectorScale")
{
	vec3_t v = {1, -2, 3};
	vec3_t out;
	VectorScale (v, 2.0f, out);
	CHECK (out[0] == doctest::Approx (2.0f));
	CHECK (out[1] == doctest::Approx (-4.0f));
	CHECK (out[2] == doctest::Approx (6.0f));
}

TEST_CASE ("VectorMA")
{
	vec3_t veca = {1, 1, 1};
	vec3_t vecb = {2, 0, -1};
	vec3_t out;
	VectorMA (veca, 3.0f, vecb, out); // veca + scale * vecb
	CHECK (out[0] == doctest::Approx (7.0f));
	CHECK (out[1] == doctest::Approx (1.0f));
	CHECK (out[2] == doctest::Approx (-2.0f));
}

TEST_CASE ("CrossProduct")
{
	vec3_t x = {1, 0, 0};
	vec3_t y = {0, 1, 0};
	vec3_t out;
	CrossProduct (x, y, out);
	CHECK (out[0] == doctest::Approx (0.0f));
	CHECK (out[1] == doctest::Approx (0.0f));
	CHECK (out[2] == doctest::Approx (1.0f));
}

TEST_CASE ("Length / VectorNormalize")
{
	vec3_t v = {3, 4, 0};
	CHECK (Length (v) == doctest::Approx (5.0f));

	vec3_t n = {3, 4, 0};
	float len = VectorNormalize (n);
	CHECK (len == doctest::Approx (5.0f));
	CHECK (Length (n) == doctest::Approx (1.0f));
}

TEST_CASE ("VectorInverse")
{
	vec3_t v = {1, -2, 3};
	VectorInverse (v);
	CHECK (v[0] == doctest::Approx (-1.0f));
	CHECK (v[1] == doctest::Approx (2.0f));
	CHECK (v[2] == doctest::Approx (-3.0f));
}

TEST_CASE ("VectorCompare")
{
	vec3_t a = {1, 2, 3};
	vec3_t b = {1, 2, 3};
	vec3_t c = {1, 2, 3.0001f};
	CHECK (VectorCompare (a, b) != 0);
	CHECK (VectorCompare (a, c) == 0);
}

TEST_CASE ("RotatePointAroundVector - axis aligned 90 degrees")
{
	// Rotating (1,0,0) by 90 degrees around Z should give (0,1,0).
	vec3_t dir = {0, 0, 1};
	vec3_t point = {1, 0, 0};
	vec3_t out;
	RotatePointAroundVector (out, dir, point, 90.0f);
	CHECK (out[0] == doctest::Approx (0.0f).epsilon (0.001));
	CHECK (out[1] == doctest::Approx (1.0f).epsilon (0.001));
	CHECK (out[2] == doctest::Approx (0.0f).epsilon (0.001));
}

TEST_CASE ("RotatePointAroundVector - full rotation returns to start")
{
	vec3_t dir = {0.3f, 0.7f, -0.2f};
	VectorNormalize (dir);
	vec3_t point = {2.0f, -1.0f, 0.5f};
	vec3_t out;
	RotatePointAroundVector (out, dir, point, 360.0f);
	CHECK (ApproxVec (out, point, 0.01f));
}

TEST_CASE ("RotatePointAroundVector - off axis, near 180 degrees preserves length")
{
	vec3_t dir = {1, 1, 1};
	VectorNormalize (dir);
	vec3_t point = {1, 0, 0};
	vec3_t out;
	RotatePointAroundVector (out, dir, point, 179.9f);
	// Rotation preserves length and should have moved the point meaningfully.
	CHECK (Length (out) == doctest::Approx (Length (point)).epsilon (0.001));
	CHECK_FALSE (ApproxVec (out, point, 0.1f));
}

TEST_CASE ("BoxOnPlaneSide - bad signbits throws (BOPS_Error)")
{
	// signbits is a 3-bit index (0-7) into BoxOnPlaneSide's switch; the
	// PARANOID-gated bounds check is permanently disabled (never defined),
	// but the switch's own default: case still calls BOPS_Error -> Sys_Error.
	mplane_t plane{};
	plane.normal[0] = 1;
	plane.dist = 0;
	plane.signbits = 8; // out of the valid 0-7 range
	vec3_t mins = {-1, -1, -1};
	vec3_t maxs = {1, 1, 1};
	CHECK_THROWS_AS (BoxOnPlaneSide (mins, maxs, &plane), SysErrorException);
}

TEST_CASE ("FloorDivMod - non-positive denominator throws")
{
	int quotient, rem;
	CHECK_THROWS_AS (FloorDivMod (10.0, 0.0, &quotient, &rem), SysErrorException);
}

TEST_CASE ("FloorDivMod - basic floor division")
{
	int quotient, rem;

	FloorDivMod (7.0, 2.0, &quotient, &rem);
	CHECK (quotient == 3);
	CHECK (rem == 1);

	FloorDivMod (-7.0, 2.0, &quotient, &rem);
	CHECK (quotient == -4);
	CHECK (rem == 1);
}

TEST_CASE ("anglemod")
{
	CHECK (anglemod (370.0f) == doctest::Approx (10.0f).epsilon (0.01));
	CHECK (anglemod (-10.0f) == doctest::Approx (350.0f).epsilon (0.01));
	CHECK (anglemod (0.0f) == doctest::Approx (0.0f).epsilon (0.01));
}

TEST_CASE ("Q_log2")
{
	CHECK (Q_log2 (1) == 0);
	CHECK (Q_log2 (2) == 1);
	CHECK (Q_log2 (4) == 2);
	CHECK (Q_log2 (1024) == 10);
}

TEST_CASE ("GreatestCommonDivisor")
{
	CHECK (GreatestCommonDivisor (12, 18) == 6);
	CHECK (GreatestCommonDivisor (17, 5) == 1);
	CHECK (GreatestCommonDivisor (0, 5) == 5);
}
