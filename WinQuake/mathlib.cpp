/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// mathlib.c -- math primitives

#include <cmath>
#include "quakedef.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/rotate_vector.hpp>

// Sys_Error declared in sys.h (included via quakedef.h)

vec3_t vec3_origin = {0, 0, 0};

/*-----------------------------------------------------------------*/

// Rodrigues' rotation formula (glm::rotate) replaces the change-of-basis
// approach (build an orthonormal frame around dir, rotate about its "z"
// axis, transform back) the original engine used -- same result, verified
// to match to ~4e-7 (float rounding noise) across non-axis-aligned axes,
// points, and angles including near +-180 degrees.
void RotatePointAroundVector(vec3_t dst, const vec3_t dir, const vec3_t point, float degrees)
{
    glm::vec3 result = glm::rotate(glm::make_vec3(point), glm::radians(degrees), glm::make_vec3(dir));
    dst[0] = result.x;
    dst[1] = result.y;
    dst[2] = result.z;
}

/*-----------------------------------------------------------------*/

float anglemod(float a)
{
    a = (360.0 / 65536) * ((int)(a * (65536 / 360.0)) & 65535);
    return a;
}

/*
==================
BOPS_Error

Split out like this for ASM to call.
==================
*/
void BOPS_Error(void)
{
    Sys_Error("BoxOnPlaneSide:  Bad signbits");
}

/*
==================
BoxOnPlaneSide

Returns 1, 2, or 1 + 2
==================
*/
int BoxOnPlaneSide(vec3_t emins, vec3_t emaxs, mplane_t *p)
{
    float dist1, dist2;
    int sides;


    // general case
    switch (p->signbits)
    {
    case 0:
        dist1 = p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2];
        dist2 = p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2];
        break;
    case 1:
        dist1 = p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2];
        dist2 = p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2];
        break;
    case 2:
        dist1 = p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2];
        dist2 = p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2];
        break;
    case 3:
        dist1 = p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2];
        dist2 = p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2];
        break;
    case 4:
        dist1 = p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2];
        dist2 = p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2];
        break;
    case 5:
        dist1 = p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2];
        dist2 = p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2];
        break;
    case 6:
        dist1 = p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2];
        dist2 = p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2];
        break;
    case 7:
        dist1 = p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2];
        dist2 = p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2];
        break;
    default:
        dist1 = dist2 = 0; // shut up compiler
        BOPS_Error();
        break;
    }


    sides = 0;
    if (dist1 >= p->dist)
    {
        sides = 1;
    }
    if (dist2 < p->dist)
    {
        sides |= 2;
    }

    return sides;
}

void AngleVectors(vec3_t angles, vec3_t forward, vec3_t right, vec3_t up)
{
    float angle;
    float sr, sp, sy, cr, cp, cy;

    angle = angles[YAW] * (M_PI * 2 / 360);
    sy = sin(angle);
    cy = cos(angle);
    angle = angles[PITCH] * (M_PI * 2 / 360);
    sp = sin(angle);
    cp = cos(angle);
    angle = angles[ROLL] * (M_PI * 2 / 360);
    sr = sin(angle);
    cr = cos(angle);

    forward[0] = cp * cy;
    forward[1] = cp * sy;
    forward[2] = -sp;
    right[0] = (-1 * sr * sp * cy + -1 * cr * -sy);
    right[1] = (-1 * sr * sp * sy + -1 * cr * cy);
    right[2] = -1 * sr * cp;
    up[0] = (cr * sp * cy + -sr * -sy);
    up[1] = (cr * sp * sy + -sr * cy);
    up[2] = cr * cp;
}

int VectorCompare(vec3_t v1, vec3_t v2)
{
    return glm::make_vec3(v1) == glm::make_vec3(v2);
}

void VectorMA(vec3_t veca, float scale, vec3_t vecb, vec3_t vecc)
{
    glm::vec3 result = glm::make_vec3(veca) + scale * glm::make_vec3(vecb);
    vecc[0] = result.x;
    vecc[1] = result.y;
    vecc[2] = result.z;
}

vec_t _DotProduct(vec3_t v1, vec3_t v2)
{
    return glm::dot(glm::make_vec3(v1), glm::make_vec3(v2));
}

void _VectorSubtract(vec3_t veca, vec3_t vecb, vec3_t out)
{
    glm::vec3 result = glm::make_vec3(veca) - glm::make_vec3(vecb);
    out[0] = result.x;
    out[1] = result.y;
    out[2] = result.z;
}

void _VectorAdd(vec3_t veca, vec3_t vecb, vec3_t out)
{
    glm::vec3 result = glm::make_vec3(veca) + glm::make_vec3(vecb);
    out[0] = result.x;
    out[1] = result.y;
    out[2] = result.z;
}

void _VectorCopy(vec3_t in, vec3_t out)
{
    out[0] = in[0];
    out[1] = in[1];
    out[2] = in[2];
}

void CrossProduct(vec3_t v1, vec3_t v2, vec3_t cross)
{
    glm::vec3 result = glm::cross(glm::make_vec3(v1), glm::make_vec3(v2));
    cross[0] = result.x;
    cross[1] = result.y;
    cross[2] = result.z;
}

vec_t Length(vec3_t v)
{
    return glm::length(glm::make_vec3(v));
}

float VectorNormalize(vec3_t v)
{
    glm::vec3 vv = glm::make_vec3(v);
    float length = glm::length(vv);

    if (length)
    {
        vv /= length;
        v[0] = vv.x;
        v[1] = vv.y;
        v[2] = vv.z;
    }

    return length;
}

void VectorInverse(vec3_t v)
{
    glm::vec3 result = -glm::make_vec3(v);
    v[0] = result.x;
    v[1] = result.y;
    v[2] = result.z;
}

void VectorScale(vec3_t in, vec_t scale, vec3_t out)
{
    glm::vec3 result = glm::make_vec3(in) * scale;
    out[0] = result.x;
    out[1] = result.y;
    out[2] = result.z;
}

int Q_log2(int val)
{
    int answer = 0;
    while (val >>= 1)
    {
        answer++;
    }
    return answer;
}

/*
===================
FloorDivMod

Returns mathematically correct (floor-based) quotient and remainder for
numer and denom, both of which should contain no fractional part. The
quotient must fit in 32 bits.
====================
*/

void FloorDivMod(double numer, double denom, int *quotient, int *rem)
{
    int q, r;
    double x;

    if (denom <= 0.0)
    {
        Sys_Error("FloorDivMod: bad denominator %d\n", denom);
    }

//	if ((floor(numer) != numer) || (floor(denom) != denom))
//		Sys_Error ("FloorDivMod: non-integer numer or denom %f %f\n",
//				numer, denom);

    if (numer >= 0.0)
    {

        x = floor(numer / denom);
        q = (int)x;
        r = (int)floor(numer - (x * denom));
    }
    else
    {
        //
        // perform operations with positive values, and fix mod to make floor-based
        //
        x = floor(-numer / denom);
        q = -(int)x;
        r = (int)floor(-numer - (x * denom));
        if (r != 0)
        {
            q--;
            r = (int)denom - r;
        }
    }

    *quotient = q;
    *rem = r;
}

/*
===================
GreatestCommonDivisor
====================
*/
int GreatestCommonDivisor(int i1, int i2)
{
    if (i1 > i2)
    {
        if (i2 == 0)
        {
            return (i1);
        }
        return GreatestCommonDivisor(i2, i1 % i2);
    }
    else
    {
        if (i1 == 0)
        {
            return (i2);
        }
        return GreatestCommonDivisor(i1, i2 % i1);
    }
}

// TODO: move to nonintel.c

/*
===================
Invert24To16

Inverts an 8.24 value to a 16.16 value
====================
*/

fixed16_t Invert24To16(fixed16_t val)
{
    if (val < 256)
    {
        return (0xFFFFFFFF);
    }

    return (fixed16_t)(((double)0x10000 * (double)0x1000000 / (double)val) + 0.5);
}
