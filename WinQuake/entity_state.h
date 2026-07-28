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
// entity_state.h -- a network-protocol snapshot of an entity's visible
// state. Used by value in both render.h's entity_t (as a baseline to fill
// in defaults from) and progs.h's edict-adjacent structs; small enough,
// and shared by exactly those two, to warrant its own header rather than
// being defined inline in quakedef.h the way it used to be.
#pragma once

#include "mathlib.h"	// vec3_t

struct entity_state_t
{
	vec3_t	origin;
	vec3_t	angles;
	int		modelindex;
	int		frame;
	int		colormap;
	int		skin;
	int		effects;
};
