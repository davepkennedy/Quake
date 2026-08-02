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

#include "qlimits.h" // MAX_QPATH
#include "common.h"  // byte, qboolean
#include "mathlib.h" // vec3_t
#include "zone.h"    // cache_user_t
#include "bspfile.h" // MIPLEVELS, MAXLIGHTMAPS, NUM_AMBIENTS, MAX_MAP_HULLS

#ifndef __MODEL__
#define __MODEL__

#include "modelgen.h"
#include "spritegn.h"

/*

d*_t structures are on-disk representations
m*_t structures are in-memory

*/

// entity effects (EF_BRIGHTFIELD/EF_MUZZLEFLASH/EF_BRIGHTLIGHT/EF_DIMLIGHT) are
// declared once in server.h, included before this header from quakedef.h

/*
==============================================================================

BRUSH MODELS

==============================================================================
*/

//
// in memory representation
//
// !!! if this is changed, it must be changed in asm_draw.h too !!!
struct mvertex_t
{
    vec3_t position;
};

constexpr int SIDE_FRONT = 0;
constexpr int SIDE_BACK = 1;
constexpr int SIDE_ON = 2;

// plane_t structure
// !!! if this is changed, it must be changed in asm_i386.h too !!!
struct mplane_t
{
    vec3_t normal;
    float dist;
    byte type;     // for texture axis selection and fast side tests
    byte signbits; // signx + signy<<1 + signz<<1
    byte pad[2];
};

struct texture_t
{
    char name[16];
    unsigned width, height;
    int gl_texturenum;
    struct msurface_t *texturechain;   // for gl_texsort drawing
    int anim_total;                    // total tenths in sequence ( 0 = no)
    int anim_min, anim_max;            // time for this frame min <=time< max
    struct texture_t *anim_next;       // in the animation sequence
    struct texture_t *alternate_anims; // bmodels in frmae 1 use these
    unsigned offsets[MIPLEVELS];       // four mip maps stored
};

constexpr int SURF_PLANEBACK = 2;
constexpr int SURF_DRAWSKY = 4;
constexpr int SURF_DRAWSPRITE = 8;
constexpr int SURF_DRAWTURB = 0x10;
constexpr int SURF_DRAWTILED = 0x20;
constexpr int SURF_DRAWBACKGROUND = 0x40;
constexpr int SURF_UNDERWATER = 0x80;

// !!! if this is changed, it must be changed in asm_draw.h too !!!
struct medge_t
{
    unsigned short v[2];
    unsigned int cachededgeoffset;
};

struct mtexinfo_t
{
    float vecs[2][4];
    float mipadjust;
    texture_t *texture;
    int flags;
};

constexpr int VERTEXSIZE = 7;

struct glpoly_t
{
    struct glpoly_t *next;
    struct glpoly_t *chain;
    int numverts;
    int flags;                  // for SURF_UNDERWATER
    float verts[4][VERTEXSIZE]; // variable sized (xyz s1t1 s2t2)
};

struct msurface_t
{
    int visframe; // should be drawn when node is crossed

    mplane_t *plane;
    int flags;

    int firstedge; // look up in model->surfedges[], negative numbers
    int numedges;  // are backwards edges

    short texturemins[2];
    short extents[2];

    int light_s, light_t; // gl lightmap coordinates

    glpoly_t *polys; // multiple if warped
    struct msurface_t *texturechain;

    mtexinfo_t *texinfo;

    // lighting info
    int dlightframe;
    int dlightbits;

    int lightmaptexturenum;
    byte styles[MAXLIGHTMAPS];
    int cached_light[MAXLIGHTMAPS]; // values currently used in lightmap
    qboolean cached_dlight;         // true if dynamic light in cache
    byte *samples;                  // [numstyles*surfsize]
};

struct mnode_t
{
    // common with leaf
    int contents; // 0, to differentiate from leafs
    int visframe; // node needs to be traversed if current

    float minmaxs[6]; // for bounding box culling

    struct mnode_t *parent;

    // node specific
    mplane_t *plane;
    struct mnode_t *children[2];

    unsigned short firstsurface;
    unsigned short numsurfaces;
};

struct mleaf_t
{
    // common with node
    int contents; // wil be a negative contents number
    int visframe; // node needs to be traversed if current

    float minmaxs[6]; // for bounding box culling

    struct mnode_t *parent;

    // leaf specific
    byte *compressed_vis;
    efrag_t *efrags;

    msurface_t **firstmarksurface;
    int nummarksurfaces;
    int key; // BSP sequence number for leaf's contents
    byte ambient_sound_level[NUM_AMBIENTS];
};

// !!! if this is changed, it must be changed in asm_i386.h too !!!
struct hull_t
{
    dclipnode_t *clipnodes;
    mplane_t *planes;
    int firstclipnode;
    int lastclipnode;
    vec3_t clip_mins;
    vec3_t clip_maxs;
};

/*
==============================================================================

SPRITE MODELS

==============================================================================
*/

// FIXME: shorten these?
struct mspriteframe_t
{
    int width;
    int height;
    float up, down, left, right;
    int gl_texturenum;
};

struct mspritegroup_t
{
    int numframes;
    float *intervals;
    mspriteframe_t *frames[1];
};

struct mspriteframedesc_t
{
    spriteframetype_t type;
    mspriteframe_t *frameptr;
};

struct msprite_t
{
    int type;
    int maxwidth;
    int maxheight;
    int numframes;
    float beamlength; // remove?
    void *cachespot;  // remove?
    mspriteframedesc_t frames[1];
};

/*
==============================================================================

ALIAS MODELS

Alias models are position independent, so the cache manager can move them.
==============================================================================
*/

struct maliasframedesc_t
{
    int firstpose;
    int numposes;
    float interval;
    trivertx_t bboxmin;
    trivertx_t bboxmax;
    int frame;
    char name[16];
};

struct maliasgroupframedesc_t
{
    trivertx_t bboxmin;
    trivertx_t bboxmax;
    int frame;
};

struct maliasgroup_t
{
    int numframes;
    int intervals;
    maliasgroupframedesc_t frames[1];
};

// !!! if this is changed, it must be changed in asm_draw.h too !!!
struct mtriangle_t
{
    int facesfront;
    int vertindex[3];
};

constexpr int MAX_SKINS = 32;
struct aliashdr_t
{
    int ident;
    int version;
    vec3_t scale;
    vec3_t scale_origin;
    float boundingradius;
    vec3_t eyeposition;
    int numskins;
    int skinwidth;
    int skinheight;
    int numverts;
    int numtris;
    int numframes;
    synctype_t synctype;
    int flags;
    float size;

    int numposes;
    int poseverts;
    int posedata; // numposes*poseverts trivert_t
    int commands; // gl command list with embedded s/t
    int gl_texturenum[MAX_SKINS][4];
    int texels[MAX_SKINS];       // only for player skins
    maliasframedesc_t frames[1]; // variable sized
};

constexpr int MAXALIASVERTS = 1024;
constexpr int MAXALIASFRAMES = 256;
constexpr int MAXALIASTRIS = 2048;
extern aliashdr_t *pheader;
extern stvert_t stverts[MAXALIASVERTS];
extern mtriangle_t triangles[MAXALIASTRIS];
extern trivertx_t *poseverts[MAXALIASFRAMES];

//===================================================================

//
// Whole model
//

enum class modtype_t
{
    mod_brush,
    mod_sprite,
    mod_alias
};

constexpr int EF_ROCKET = 1;    // leave a trail
constexpr int EF_GRENADE = 2;   // leave a trail
constexpr int EF_GIB = 4;       // leave a trail
constexpr int EF_ROTATE = 8;    // rotate (bonus items)
constexpr int EF_TRACER = 16;   // green split trail
constexpr int EF_ZOMGIB = 32;   // small blood trail
constexpr int EF_TRACER2 = 64;  // orange split trail + rotate
constexpr int EF_TRACER3 = 128; // purple trail

struct model_t
{
    char name[MAX_QPATH];
    qboolean needload; // bmodels and sprites don't cache normally

    modtype_t type;
    int numframes;
    synctype_t synctype;

    int flags;

    //
    // volume occupied by the model graphics
    //
    vec3_t mins, maxs;
    float radius;

    //
    // solid volume for clipping
    //
    qboolean clipbox;
    vec3_t clipmins, clipmaxs;

    //
    // brush model
    //
    int firstmodelsurface, nummodelsurfaces;

    int numsubmodels;
    dmodel_t *submodels;

    int numplanes;
    mplane_t *planes;

    int numleafs; // number of visible leafs, not counting 0
    mleaf_t *leafs;

    int numvertexes;
    mvertex_t *vertexes;

    int numedges;
    medge_t *edges;

    int numnodes;
    mnode_t *nodes;

    int numtexinfo;
    mtexinfo_t *texinfo;

    int numsurfaces;
    msurface_t *surfaces;

    int numsurfedges;
    int *surfedges;

    int numclipnodes;
    dclipnode_t *clipnodes;

    int nummarksurfaces;
    msurface_t **marksurfaces;

    hull_t hulls[MAX_MAP_HULLS];

    int numtextures;
    texture_t **textures;

    byte *visdata;
    byte *lightdata;
    char *entities;

    //
    // additional model data
    //
    cache_user_t cache; // only access through Mod_Extradata
};

//============================================================================

void Mod_Init(void);
void Mod_ClearAll(void);
model_t *Mod_FindName(const char *name);
model_t *Mod_ForName(const char *name, qboolean crash);
void *Mod_Extradata(model_t *mod); // handles caching
void Mod_TouchModel(const char *name);

mleaf_t *Mod_PointInLeaf(float *p, model_t *model);
byte *Mod_LeafPVS(mleaf_t *leaf, model_t *model);

#endif // __MODEL__
