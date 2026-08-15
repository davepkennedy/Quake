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
#pragma once

#include "common.h" // byte

// upper design bounds

constexpr int MAX_MAP_HULLS = 4;

constexpr int MAX_MAP_MODELS = 256;
constexpr int MAX_MAP_BRUSHES = 4096;
constexpr int MAX_MAP_ENTITIES = 1024;
constexpr int MAX_MAP_ENTSTRING = 65536;

constexpr int MAX_MAP_PLANES = 32767;
constexpr int MAX_MAP_NODES = 32767;     // because negative shorts are contents
constexpr int MAX_MAP_CLIPNODES = 32767; //
constexpr int MAX_MAP_LEAFS = 8192;
constexpr int MAX_MAP_VERTS = 65535;
constexpr int MAX_MAP_FACES = 65535;
constexpr int MAX_MAP_MARKSURFACES = 65535;
constexpr int MAX_MAP_TEXINFO = 4096;
constexpr int MAX_MAP_EDGES = 256000;
constexpr int MAX_MAP_SURFEDGES = 512000;
constexpr int MAX_MAP_TEXTURES = 512;
constexpr int MAX_MAP_MIPTEX = 0x200000;
constexpr int MAX_MAP_LIGHTING = 0x100000;
constexpr int MAX_MAP_VISIBILITY = 0x100000;

constexpr int MAX_MAP_PORTALS = 65536;

// key / value pair sizes

constexpr int MAX_KEY = 32;
constexpr int MAX_VALUE = 1024;

//=============================================================================

constexpr int BSPVERSION = 29;
constexpr int TOOLVERSION = 2;

struct lump_t
{
    int fileofs, filelen;
};

constexpr int LUMP_ENTITIES = 0;
constexpr int LUMP_PLANES = 1;
constexpr int LUMP_TEXTURES = 2;
constexpr int LUMP_VERTEXES = 3;
constexpr int LUMP_VISIBILITY = 4;
constexpr int LUMP_NODES = 5;
constexpr int LUMP_TEXINFO = 6;
constexpr int LUMP_FACES = 7;
constexpr int LUMP_LIGHTING = 8;
constexpr int LUMP_CLIPNODES = 9;
constexpr int LUMP_LEAFS = 10;
constexpr int LUMP_MARKSURFACES = 11;
constexpr int LUMP_EDGES = 12;
constexpr int LUMP_SURFEDGES = 13;
constexpr int LUMP_MODELS = 14;

constexpr int HEADER_LUMPS = 15;

struct dmodel_t
{
    float mins[3], maxs[3];
    float origin[3];
    int headnode[MAX_MAP_HULLS];
    int visleafs; // not including the solid leaf 0
    int firstface, numfaces;
};

struct dheader_t
{
    int version;
    lump_t lumps[HEADER_LUMPS];
};

struct dmiptexlump_t
{
    int nummiptex;
    int dataofs[4]; // [nummiptex]
};

constexpr int MIPLEVELS = 4;
struct miptex_t
{
    char name[16];
    unsigned width, height;
    unsigned offsets[MIPLEVELS]; // four mip maps stored
};

struct dvertex_t
{
    float point[3];
};

// 0-2 are axial planes
constexpr int PLANE_X = 0;
constexpr int PLANE_Y = 1;
constexpr int PLANE_Z = 2;

// 3-5 are non-axial planes snapped to the nearest
constexpr int PLANE_ANYX = 3;
constexpr int PLANE_ANYY = 4;
constexpr int PLANE_ANYZ = 5;

struct dplane_t
{
    float normal[3];
    float dist;
    int type; // PLANE_X - PLANE_ANYZ ?remove? trivial to regenerate
};

constexpr int CONTENTS_EMPTY = -1;
constexpr int CONTENTS_SOLID = -2;
constexpr int CONTENTS_WATER = -3;
constexpr int CONTENTS_SLIME = -4;
constexpr int CONTENTS_LAVA = -5;
constexpr int CONTENTS_SKY = -6;
constexpr int CONTENTS_ORIGIN = -7; // removed at csg time
constexpr int CONTENTS_CLIP = -8;   // changed to contents_solid

constexpr int CONTENTS_CURRENT_0 = -9;
constexpr int CONTENTS_CURRENT_90 = -10;
constexpr int CONTENTS_CURRENT_180 = -11;
constexpr int CONTENTS_CURRENT_270 = -12;
constexpr int CONTENTS_CURRENT_UP = -13;
constexpr int CONTENTS_CURRENT_DOWN = -14;

struct dnode_t
{
    int planenum;
    short children[2]; // negative numbers are -(leafs+1), not nodes
    short mins[3];     // for sphere culling
    short maxs[3];
    unsigned short firstface;
    unsigned short numfaces; // counting both sides
};

struct dclipnode_t
{
    int planenum;
    short children[2]; // negative numbers are contents
};

struct texinfo_t
{
    float vecs[2][4]; // [s/t][xyz offset]
    int miptex;
    int flags;
};
constexpr int TEX_SPECIAL = 1; // sky or slime, no lightmap or 256 subdivision

// note that edge 0 is never used, because negative edge nums are used for
// counterclockwise use of the edge in a face
struct dedge_t
{
    unsigned short v[2]; // vertex numbers
};

constexpr int MAXLIGHTMAPS = 4;
struct dface_t
{
    short planenum;
    short side;

    int firstedge; // we must support > 64k edges
    short numedges;
    short texinfo;

    // lighting info
    byte styles[MAXLIGHTMAPS];
    int lightofs; // start of [numstyles*surfsize] samples
};

constexpr int AMBIENT_WATER = 0;
constexpr int AMBIENT_SKY = 1;
constexpr int AMBIENT_SLIME = 2;
constexpr int AMBIENT_LAVA = 3;

constexpr int NUM_AMBIENTS = 4; // automatic ambient sounds

// leaf 0 is the generic CONTENTS_SOLID leaf, used for all solid areas
// all other leafs need visibility info
struct dleaf_t
{
    int contents;
    int visofs; // -1 = no visibility info

    short mins[3]; // for frustum culling
    short maxs[3];

    unsigned short firstmarksurface;
    unsigned short nummarksurfaces;

    byte ambient_level[NUM_AMBIENTS];
};

//============================================================================
