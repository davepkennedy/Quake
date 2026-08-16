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
// r_main.c

#include "quakedef.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

entity_t r_worldentity;

qboolean r_cache_thrash; // compatability

vec3_t modelorg, r_entorigin;
entity_t *currententity;

int r_visframecount; // bumped when going to a new PVS
int r_framecount;    // used for dlight push checking

mplane_t frustum[4];

int c_brush_polys, c_alias_polys;

qboolean envmap; // true during envmap command capture

int currenttexture = -1; // to avoid unnecessary texture sets

int cnttextures[2] = {-1, -1}; // cached

int particletexture; // little dot for particles
int playertextures;  // up to 16 color translated skins

int mirrortexturenum; // quake texturenum, not gltexturenum
qboolean mirror;
mplane_t *mirror_plane;

//
// view origin
//
vec3_t vup;
vec3_t vpn;
vec3_t vright;
vec3_t r_origin;

glm::mat4 r_world_matrix;
glm::mat4 r_base_world_matrix;
glm::mat4 r_proj_matrix;
glm::mat4 r_entity_matrix(1.0f);

//
// screen size info
//
refdef_t r_refdef;

mleaf_t *r_viewleaf, *r_oldviewleaf;

texture_t *r_notexture_mip;

int d_lightstylevalue[256]; // 8.8 fraction of base light value

void R_MarkLeaves(void);

cvar_t r_norefresh = {"r_norefresh", "0"};
cvar_t r_drawentities = {"r_drawentities", "1"};
cvar_t r_drawviewmodel = {"r_drawviewmodel", "1"};
cvar_t r_speeds = {"r_speeds", "0"};
cvar_t r_fullbright = {"r_fullbright", "0"};
cvar_t r_lightmap = {"r_lightmap", "0"};
cvar_t r_shadows = {"r_shadows", "0"};
cvar_t r_tessellation = {"r_tessellation", "0"};
cvar_t r_mirroralpha = {"r_mirroralpha", "1"};
cvar_t r_wateralpha = {"r_wateralpha", "1"};
cvar_t r_dynamic = {"r_dynamic", "1"};
cvar_t r_novis = {"r_novis", "0"};

cvar_t gl_finish = {"gl_finish", "0"};
cvar_t gl_clear = {"gl_clear", "0"};
cvar_t gl_cull = {"gl_cull", "1"};
cvar_t gl_texsort = {"gl_texsort", "1"};
cvar_t gl_smoothmodels = {"gl_smoothmodels", "1"};
cvar_t gl_affinemodels = {"gl_affinemodels", "0"};
cvar_t gl_polyblend = {"gl_polyblend", "1"};
cvar_t gl_flashblend = {"gl_flashblend", "1"};
cvar_t gl_playermip = {"gl_playermip", "0"};
cvar_t gl_nocolors = {"gl_nocolors", "0"};
cvar_t gl_keeptjunctions = {"gl_keeptjunctions", "0"};
cvar_t gl_reporttjunctions = {"gl_reporttjunctions", "0"};
cvar_t gl_doubleeyes = {"gl_doubleeys", "1"};

extern cvar_t gl_ztrick;

/*
=================
R_CullBox

Returns true if the box is completely outside the frustom
=================
*/
qboolean R_CullBox(vec3_t mins, vec3_t maxs)
{
    int i;

    for (i = 0; i < 4; i++)
    {
        if (BoxOnPlaneSide(mins, maxs, &frustum[i]) == 2)
        {
            return true;
        }
    }
    return false;
}

void R_RotateForEntity(entity_t *e)
{
    r_entity_matrix = glm::translate(glm::mat4(1.0f), glm::vec3(e->origin[0], e->origin[1], e->origin[2]));

    r_entity_matrix = glm::rotate(r_entity_matrix, glm::radians(e->angles[1]), glm::vec3(0, 0, 1));
    r_entity_matrix = glm::rotate(r_entity_matrix, glm::radians(-e->angles[0]), glm::vec3(0, 1, 0));
    r_entity_matrix = glm::rotate(r_entity_matrix, glm::radians(e->angles[2]), glm::vec3(1, 0, 0));
}

/*
=============================================================

  SPRITE MODELS

=============================================================
*/

/*
================
R_GetSpriteFrame
================
*/
mspriteframe_t *R_GetSpriteFrame(entity_t *currententity)
{
    msprite_t *psprite;
    mspritegroup_t *pspritegroup;
    mspriteframe_t *pspriteframe;
    int i, numframes, frame;
    float *pintervals, fullinterval, targettime, time;

    psprite = static_cast<msprite_t *>(currententity->model->cache.data);
    frame = currententity->frame;

    if ((frame >= psprite->numframes) || (frame < 0))
    {
        Con_Printf("R_DrawSprite: no such frame {}\n", frame);
        frame = 0;
    }

    if (psprite->frames[frame].type == spriteframetype_t::SPR_SINGLE)
    {
        pspriteframe = psprite->frames[frame].frameptr;
    }
    else
    {
        pspritegroup = reinterpret_cast<mspritegroup_t *>(psprite->frames[frame].frameptr);
        pintervals = pspritegroup->intervals;
        numframes = pspritegroup->numframes;
        fullinterval = pintervals[numframes - 1];

        time = CL_Time() + currententity->syncbase;

        // when loading in Mod_LoadSpriteGroup, we guaranteed all interval values
        // are positive, so we don't have to worry about division by 0
        targettime = time - ((int)(time / fullinterval)) * fullinterval;

        for (i = 0; i < (numframes - 1); i++)
        {
            if (pintervals[i] > targettime)
            {
                break;
            }
        }

        pspriteframe = pspritegroup->frames[i];
    }

    return pspriteframe;
}

// -------------------------------------------------------------------------
// Billboard renderer state (VAO / VBO / GLSL shader)
//
// Shared by sprite entities (R_DrawSpriteModel: alpha-tested textured quad
// — explosions, muzzleflashes, bullet marks) and the fullscreen damage/
// underwater tint (R_PolyBlend: flat blended quad) — both are just a
// single MVP-transformed quad, textured or not. 0.666 matches the original
// fixed-function glAlphaFunc(GL_GREATER, 0.666) cutoff (now removed —
// the in-shader discard is the only alpha test left).
// -------------------------------------------------------------------------

static GLVertexArray billboard_vao;
static GLBuffer billboard_vbo;
static GLProgram billboard_prog;
static GLint u_billboard_mvp = -1;
static GLint u_billboard_tex = -1;
static GLint u_billboard_color = -1;
static GLint u_billboard_flat = -1;

static const char billboard_vert_src[] = "#version 450 core\n"
                                         "layout(location = 0) in vec3 a_pos;\n"
                                         "layout(location = 1) in vec2 a_uv;\n"
                                         "uniform mat4 u_mvp;\n"
                                         "out vec2 v_uv;\n"
                                         "void main() {\n"
                                         "    v_uv = a_uv;\n"
                                         "    gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
                                         "}\n";

static const char billboard_frag_src[] = "#version 450 core\n"
                                         "in vec2 v_uv;\n"
                                         "uniform sampler2D u_tex;\n"
                                         "uniform vec4 u_color;\n"
                                         "uniform int u_flat;\n"
                                         "out vec4 frag_color;\n"
                                         "void main() {\n"
                                         "    if (u_flat != 0) {\n"
                                         "        frag_color = u_color;\n"
                                         "        return;\n"
                                         "    }\n"
                                         "    vec4 c = texture(u_tex, v_uv);\n"
                                         "    if (c.a < 0.666)\n"
                                         "        discard;\n"
                                         "    frag_color = c;\n"
                                         "}\n";

static void Billboard_InitRenderer(void)
{
    if (billboard_prog)
    {
        return;
    }

    billboard_prog = GL_BuildProgram(billboard_vert_src, billboard_frag_src);
    if (!billboard_prog)
    {
        Sys_Error("Billboard_InitRenderer: shader compile failed");
    }

    qglUseProgram(billboard_prog);
    u_billboard_mvp = qglGetUniformLocation(billboard_prog, "u_mvp");
    u_billboard_tex = qglGetUniformLocation(billboard_prog, "u_tex");
    u_billboard_color = qglGetUniformLocation(billboard_prog, "u_color");
    u_billboard_flat = qglGetUniformLocation(billboard_prog, "u_flat");
    qglUseProgram(0);

    billboard_vao = GLVertexArray::Create();
    billboard_vbo = GLBuffer::Create();
    qglBindVertexArray(billboard_vao);
    qglBindBuffer(GL_ARRAY_BUFFER, billboard_vbo);
    qglBufferData(GL_ARRAY_BUFFER, 6 * 5 * sizeof(float), nullptr, GL_STREAM_DRAW);
    // location 0: xyz  (3 floats, offset 0, stride 5*4=20)
    qglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    qglEnableVertexAttribArray(0);
    // location 1: uv  (2 floats, offset 12)
    qglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void *>(3 * sizeof(float)));
    qglEnableVertexAttribArray(1);
    qglBindVertexArray(0);
    qglBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void Billboard_BeginDraw(void)
{
    Billboard_InitRenderer();
    qglUseProgram(billboard_prog);
    qglBindVertexArray(billboard_vao);
    qglBindBuffer(GL_ARRAY_BUFFER, billboard_vbo);
    qglUniform1i(u_billboard_tex, 0);
}

static void Billboard_SetMVP(void)
{
    float mvp[16];
    GL_GetMVP(mvp);
    qglUniformMatrix4fv(u_billboard_mvp, 1, GL_FALSE, mvp);
}

static void Billboard_EndDraw(void)
{
    qglBindVertexArray(0);
    qglUseProgram(0);
}

// quad[4] holds the corners in original GL_QUADS order (pos3+uv2 each);
// split into two triangles the same way GL_QUADS was always filled.
static void Billboard_DrawQuad(const float quad[4][5])
{
    float tris[6][5];
    memcpy(tris[0], quad[0], sizeof(float) * 5);
    memcpy(tris[1], quad[1], sizeof(float) * 5);
    memcpy(tris[2], quad[2], sizeof(float) * 5);
    memcpy(tris[3], quad[0], sizeof(float) * 5);
    memcpy(tris[4], quad[2], sizeof(float) * 5);
    memcpy(tris[5], quad[3], sizeof(float) * 5);

    qglBufferData(GL_ARRAY_BUFFER, sizeof(tris), tris, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

/*
=================
R_DrawSpriteModel

=================
*/
void R_DrawSpriteModel(entity_t *e)
{
    vec3_t point;
    mspriteframe_t *frame;
    float *up, *right;
    vec3_t v_forward, v_right, v_up;
    msprite_t *psprite;
    float quad[4][5];

    // don't even bother culling, because it's just a single
    // polygon without a surface cache
    frame = R_GetSpriteFrame(e);
    psprite = static_cast<msprite_t *>(currententity->model->cache.data);

    if (psprite->type == SPR_ORIENTED)
    { // bullet marks on walls
        AngleVectors(currententity->angles, v_forward, v_right, v_up);
        up = v_up;
        right = v_right;
    }
    else
    { // normal sprite
        up = vup;
        right = vright;
    }

    GL_DisableMultitexture();

    GL_Bind(frame->gl_texturenum);

    VectorMA(e->origin, frame->down, up, point);
    VectorMA(point, frame->left, right, point);
    quad[0][0] = point[0];
    quad[0][1] = point[1];
    quad[0][2] = point[2];
    quad[0][3] = 0;
    quad[0][4] = 1;

    VectorMA(e->origin, frame->up, up, point);
    VectorMA(point, frame->left, right, point);
    quad[1][0] = point[0];
    quad[1][1] = point[1];
    quad[1][2] = point[2];
    quad[1][3] = 0;
    quad[1][4] = 0;

    VectorMA(e->origin, frame->up, up, point);
    VectorMA(point, frame->right, right, point);
    quad[2][0] = point[0];
    quad[2][1] = point[1];
    quad[2][2] = point[2];
    quad[2][3] = 1;
    quad[2][4] = 0;

    VectorMA(e->origin, frame->down, up, point);
    VectorMA(point, frame->right, right, point);
    quad[3][0] = point[0];
    quad[3][1] = point[1];
    quad[3][2] = point[2];
    quad[3][3] = 1;
    quad[3][4] = 1;

    Billboard_BeginDraw();
    Billboard_SetMVP();
    qglUniform1i(u_billboard_flat, 0);
    Billboard_DrawQuad(quad);
    Billboard_EndDraw();
}

/*
=============================================================

  ALIAS MODELS

=============================================================
*/

#define NUMVERTEXNORMALS 162

float r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};

vec3_t shadevector;
float shadelight, ambientlight;

int lastposenum_old, lastposenum_new;
float lastpose_blend;

// -------------------------------------------------------------------------
// Alias model renderer state (VAO / VBO / GLSL shaders)
//
// Two separate programs share one VAO/VBO (same pos3+uv2+normal3 vertex
// layout): alias_prog is the skin-textured draw (GL_DrawAliasFrame), a full
// 4-stage tessellation pipeline; alias_shadow_prog is the flat blob-shadow
// draw (GL_DrawAliasShadow), a plain 2-stage pipeline with no tessellation
// and no lighting. They used to be one program with a runtime u_flat
// branch, but a tessellated program can only be drawn with GL_PATCHES, and
// the shadow's silhouette is deliberately left untessellated (simpler, and
// a rounded shadow blob isn't worth the complexity) -- so they're separate
// now, each with only the uniforms it actually needs.
// -------------------------------------------------------------------------

static GLVertexArray alias_vao;
static GLBuffer alias_vbo;
static GLProgram alias_prog;
static GLint u_alias_mvp = -1;
static GLint u_alias_tex = -1;
static GLint u_alias_shadevector = -1;
static GLint u_alias_shadelight = -1;
static GLint u_alias_tess_level = -1;

static GLProgram alias_shadow_prog;
static GLint u_alias_shadow_mvp = -1;
static GLint u_alias_shadow_color = -1;

// gl_mesh.cpp's StripLength/FanLength cap any single command at 128 verts
// (fixed-size stripverts[128]/striptris[128]); 256 leaves headroom.
#define ALIAS_MAX_CMD_VERTS 256
#define ALIAS_STREAM_VERTS ((ALIAS_MAX_CMD_VERTS - 2) * 3)
#define ALIAS_VERT_FLOATS 8 // pos3 + uv2 + normal3
static float alias_stream[ALIAS_STREAM_VERTS * ALIAS_VERT_FLOATS];

// Vertex stage just passes the raw model-space control-point data through
// to the tessellation control shader -- no MVP transform here. PN-triangle
// curving (in the TES below) has to happen before the perspective
// transform, in the same affine space the vertex positions/normals are
// already in.
static const char alias_vert_src[] = "#version 450 core\n"
                                     "layout(location = 0) in vec3 a_pos;\n"
                                     "layout(location = 1) in vec2 a_uv;\n"
                                     "layout(location = 2) in vec3 a_normal;\n"
                                     "out vec3 vs_pos;\n"
                                     "out vec2 vs_uv;\n"
                                     "out vec3 vs_normal;\n"
                                     "void main() {\n"
                                     "    vs_pos = a_pos;\n"
                                     "    vs_uv = a_uv;\n"
                                     "    vs_normal = a_normal;\n"
                                     "}\n";

// One triangle in, one patch (3 control points) out. u_tess_level drives
// both the inner and all three outer tessellation levels uniformly --
// gl_TessLevelOuter/Inner must be set to >=1 or the patch is discarded
// entirely, so r_tessellation 0 (its default) is clamped to 1, which
// evaluates the PN-triangle patch at just its 3 corners -- exactly the
// original flat triangle, byte-for-byte, so tessellation is fully off by
// default rather than just "very subtle."
static const char alias_tcs_src[] = "#version 450 core\n"
                                    "layout(vertices = 3) out;\n"
                                    "in vec3 vs_pos[];\n"
                                    "in vec2 vs_uv[];\n"
                                    "in vec3 vs_normal[];\n"
                                    "out vec3 tcs_pos[];\n"
                                    "out vec2 tcs_uv[];\n"
                                    "out vec3 tcs_normal[];\n"
                                    "uniform float u_tess_level;\n"
                                    "void main() {\n"
                                    "    tcs_pos[gl_InvocationID] = vs_pos[gl_InvocationID];\n"
                                    "    tcs_uv[gl_InvocationID] = vs_uv[gl_InvocationID];\n"
                                    "    tcs_normal[gl_InvocationID] = vs_normal[gl_InvocationID];\n"
                                    "    if (gl_InvocationID == 0) {\n"
                                    "        float level = max(u_tess_level, 1.0);\n"
                                    "        gl_TessLevelOuter[0] = level;\n"
                                    "        gl_TessLevelOuter[1] = level;\n"
                                    "        gl_TessLevelOuter[2] = level;\n"
                                    "        gl_TessLevelInner[0] = level;\n"
                                    "    }\n"
                                    "}\n";

// Curved PN-triangles (Vlachos et al., "Curved PN Triangles", 2001): bulges
// each flat triangle into a cubic Bezier patch using only its own 3 corner
// positions/normals -- no new geometry data -- while still passing exactly
// through those 3 corners, so adjacent triangles stay seamlessly joined at
// shared edges. Barycentric (u,v,w) = gl_TessCoord.xyz map to corners
// 0/1/2 respectively (u=1,v=w=0 evaluates to exactly tcs_pos[0], etc).
// UV and the normal (later renormalized per pixel in the fragment shader,
// same as the un-tessellated per-pixel lighting this feeds into) are
// blended with plain barycentric linear interpolation -- only position
// needs the curved treatment.
static const char alias_tes_src[] = "#version 450 core\n"
                                    "layout(triangles, equal_spacing, ccw) in;\n"
                                    "in vec3 tcs_pos[];\n"
                                    "in vec2 tcs_uv[];\n"
                                    "in vec3 tcs_normal[];\n"
                                    "out vec2 v_uv;\n"
                                    "out vec3 v_normal;\n"
                                    "uniform mat4 u_mvp;\n"
                                    "void main() {\n"
                                    "    float u = gl_TessCoord.x, v = gl_TessCoord.y, w = gl_TessCoord.z;\n"
                                    "    vec3 p0 = tcs_pos[0], p1 = tcs_pos[1], p2 = tcs_pos[2];\n"
                                    "    vec3 n0 = normalize(tcs_normal[0]);\n"
                                    "    vec3 n1 = normalize(tcs_normal[1]);\n"
                                    "    vec3 n2 = normalize(tcs_normal[2]);\n"
                                    "    vec3 b300 = p0;\n"
                                    "    vec3 b030 = p1;\n"
                                    "    vec3 b003 = p2;\n"
                                    "    vec3 b210 = (2.0*p0 + p1 - dot(p1-p0, n0)*n0) / 3.0;\n"
                                    "    vec3 b120 = (2.0*p1 + p0 - dot(p0-p1, n1)*n1) / 3.0;\n"
                                    "    vec3 b021 = (2.0*p1 + p2 - dot(p2-p1, n1)*n1) / 3.0;\n"
                                    "    vec3 b012 = (2.0*p2 + p1 - dot(p1-p2, n2)*n2) / 3.0;\n"
                                    "    vec3 b102 = (2.0*p2 + p0 - dot(p0-p2, n2)*n2) / 3.0;\n"
                                    "    vec3 b201 = (2.0*p0 + p2 - dot(p2-p0, n0)*n0) / 3.0;\n"
                                    "    vec3 centerE = (b210+b120+b021+b012+b102+b201) / 6.0;\n"
                                    "    vec3 centerV = (p0+p1+p2) / 3.0;\n"
                                    "    vec3 b111 = centerE + (centerE - centerV) / 2.0;\n"
                                    "    float uu = u*u, vv = v*v, ww = w*w;\n"
                                    "    vec3 curvedPos = b300*uu*u + b030*vv*v + b003*ww*w\n"
                                    "        + b210*3.0*uu*v + b120*3.0*u*vv\n"
                                    "        + b021*3.0*vv*w + b012*3.0*v*ww\n"
                                    "        + b102*3.0*ww*u + b201*3.0*w*uu\n"
                                    "        + b111*6.0*u*v*w;\n"
                                    "    v_uv = tcs_uv[0]*u + tcs_uv[1]*v + tcs_uv[2]*w;\n"
                                    "    v_normal = n0*u + n1*v + n2*w;\n"
                                    "    gl_Position = u_mvp * vec4(curvedPos, 1.0);\n"
                                    "}\n";

// Reproduces the original shadedots formula (dot(normal, shadevector) + 1,
// scaled by shadelight -- see r_avertexnormal_dots' generation, closed out
// when this went per-pixel) per pixel rather than per vertex. No u_flat
// branch anymore -- this program is only ever used for the lit, textured
// skin draw; the shadow draw uses alias_shadow_prog instead.
static const char alias_frag_src[] = "#version 450 core\n"
                                     "in vec2 v_uv;\n"
                                     "in vec3 v_normal;\n"
                                     "uniform sampler2D u_tex;\n"
                                     "uniform vec3 u_shadevector;\n"
                                     "uniform float u_shadelight;\n"
                                     "out vec4 frag_color;\n"
                                     "void main() {\n"
                                     "    float intensity = (dot(normalize(v_normal), u_shadevector) + 1.0) * u_shadelight;\n"
                                     "    frag_color = vec4(texture(u_tex, v_uv).rgb * intensity, 1.0);\n"
                                     "}\n";

// Shadow pipeline: plain, untessellated, unlit. The shadow's already-
// projected-flat position is computed on the CPU (see GL_DrawAliasShadow)
// exactly as before tessellation existed; this just transforms and
// flat-colors it.
static const char alias_shadow_vert_src[] = "#version 450 core\n"
                                            "layout(location = 0) in vec3 a_pos;\n"
                                            "uniform mat4 u_mvp;\n"
                                            "void main() {\n"
                                            "    gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
                                            "}\n";

static const char alias_shadow_frag_src[] = "#version 450 core\n"
                                            "uniform vec4 u_color;\n"
                                            "out vec4 frag_color;\n"
                                            "void main() {\n"
                                            "    frag_color = u_color;\n"
                                            "}\n";

static void Alias_InitRenderer(void)
{
    if (alias_prog)
    {
        return;
    }

    alias_prog = GL_BuildProgram(alias_vert_src, alias_tcs_src, alias_tes_src, alias_frag_src);
    if (!alias_prog)
    {
        Sys_Error("Alias_InitRenderer: shader compile failed");
    }
    alias_shadow_prog = GL_BuildProgram(alias_shadow_vert_src, alias_shadow_frag_src);
    if (!alias_shadow_prog)
    {
        Sys_Error("Alias_InitRenderer: shadow shader compile failed");
    }

    qglUseProgram(alias_prog);
    u_alias_mvp = qglGetUniformLocation(alias_prog, "u_mvp");
    u_alias_tex = qglGetUniformLocation(alias_prog, "u_tex");
    u_alias_shadevector = qglGetUniformLocation(alias_prog, "u_shadevector");
    u_alias_shadelight = qglGetUniformLocation(alias_prog, "u_shadelight");
    u_alias_tess_level = qglGetUniformLocation(alias_prog, "u_tess_level");

    qglUseProgram(alias_shadow_prog);
    u_alias_shadow_mvp = qglGetUniformLocation(alias_shadow_prog, "u_mvp");
    u_alias_shadow_color = qglGetUniformLocation(alias_shadow_prog, "u_color");
    qglUseProgram(0);

    // The only patch shape this renderer ever draws (GL_DrawAliasFrame's
    // triangles); global GL state, so set once rather than per draw call.
    qglPatchParameteri(GL_PATCH_VERTICES, 3);

    alias_vao = GLVertexArray::Create();
    alias_vbo = GLBuffer::Create();
    qglBindVertexArray(alias_vao);
    qglBindBuffer(GL_ARRAY_BUFFER, alias_vbo);
    qglBufferData(GL_ARRAY_BUFFER, sizeof(alias_stream), nullptr, GL_STREAM_DRAW);
    // location 0: xyz  (3 floats, offset 0, stride 8*4=32)
    qglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, ALIAS_VERT_FLOATS * sizeof(float), nullptr);
    qglEnableVertexAttribArray(0);
    // location 1: uv  (2 floats, offset 12)
    qglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, ALIAS_VERT_FLOATS * sizeof(float),
                            reinterpret_cast<void *>(3 * sizeof(float)));
    qglEnableVertexAttribArray(1);
    // location 2: normal  (3 floats, offset 20)
    qglVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, ALIAS_VERT_FLOATS * sizeof(float),
                            reinterpret_cast<void *>(5 * sizeof(float)));
    qglEnableVertexAttribArray(2);
    qglBindVertexArray(0);
    qglBindBuffer(GL_ARRAY_BUFFER, 0);
}

// Explicit teardown, called from Host_Shutdown before VID_Shutdown() destroys
// the GL context -- see gl_shader.h's comment on why this can't be left to
// these globals' own (static-duration) destructors. Covers both the
// billboard and alias-model renderers, this file's two trios (plus the
// alias shadow program).
void GL_RMain_Shutdown(void)
{
    billboard_vao.Release();
    billboard_vbo.Release();
    billboard_prog.Release();

    alias_vao.Release();
    alias_vbo.Release();
    alias_prog.Release();
    alias_shadow_prog.Release();
}

static void Alias_BeginDraw(GLuint program)
{
    Alias_InitRenderer();
    GL_DisableMultitexture();
    qglUseProgram(program);
    qglBindVertexArray(alias_vao);
    qglBindBuffer(GL_ARRAY_BUFFER, alias_vbo);
}

static void Alias_SetMVP(GLint mvp_location)
{
    float mvp[16];
    GL_GetMVP(mvp);
    qglUniformMatrix4fv(mvp_location, 1, GL_FALSE, mvp);
}

static void Alias_EndDraw(void)
{
    qglBindVertexArray(0);
    qglUseProgram(0);
}

// Triangulates one command's worth of vertices (a GL_TRIANGLE_FAN or
// GL_TRIANGLE_STRIP, per the original fixed-function primitive type) into
// the stream buffer and draws it. cmdverts holds n vertices of
// (x,y,z, u,v, nx,ny,nz). Mirrors the standard OpenGL strip winding rule
// (alternating vertex order every other triangle) since we no longer have
// glBegin(GL_TRIANGLE_STRIP) doing that for us. mode is GL_PATCHES for the
// tessellated skin draw or GL_TRIANGLES for the untessellated shadow draw.
static void Alias_EmitPrimitive(const float cmdverts[][ALIAS_VERT_FLOATS], int n, qboolean fan, GLenum mode)
{
    if (n < 3)
    {
        return;
    }

    int ntri = n - 2;
    float *out = alias_stream;

    for (int i = 0; i < ntri; i++)
    {
        int i0, i1, i2;
        if (fan)
        {
            i0 = 0;
            i1 = i + 1;
            i2 = i + 2;
        }
        else if (i & 1)
        {
            i0 = i + 1;
            i1 = i;
            i2 = i + 2;
        }
        else
        {
            i0 = i;
            i1 = i + 1;
            i2 = i + 2;
        }

        memcpy(out, cmdverts[i0], ALIAS_VERT_FLOATS * sizeof(float));
        out += ALIAS_VERT_FLOATS;
        memcpy(out, cmdverts[i1], ALIAS_VERT_FLOATS * sizeof(float));
        out += ALIAS_VERT_FLOATS;
        memcpy(out, cmdverts[i2], ALIAS_VERT_FLOATS * sizeof(float));
        out += ALIAS_VERT_FLOATS;
    }

    int nverts = ntri * 3;
    qglBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(nverts * ALIAS_VERT_FLOATS * sizeof(float)), alias_stream,
                  GL_STREAM_DRAW);
    glDrawArrays(mode, 0, nverts);
}

/*
=============
GL_DrawAliasFrame
=============
*/
void GL_DrawAliasFrame(aliashdr_t *paliashdr, int posenum0, int posenum1, float blend)
{
    trivertx_t *verts0, *verts1;
    int *order;
    int count;
    float cmdverts[ALIAS_MAX_CMD_VERTS][ALIAS_VERT_FLOATS];

    lastposenum_old = posenum0;
    lastposenum_new = posenum1;
    lastpose_blend = blend;

    verts0 = reinterpret_cast<trivertx_t *>(reinterpret_cast<byte *>(paliashdr) + paliashdr->posedata);
    verts0 += posenum0 * paliashdr->poseverts;
    verts1 = reinterpret_cast<trivertx_t *>(reinterpret_cast<byte *>(paliashdr) + paliashdr->posedata);
    verts1 += posenum1 * paliashdr->poseverts;
    order = reinterpret_cast<int *>(reinterpret_cast<byte *>(paliashdr) + paliashdr->commands);

    Alias_BeginDraw(alias_prog);
    Alias_SetMVP(u_alias_mvp);
    qglUniform1i(u_alias_tex, 0);
    qglUniform3fv(u_alias_shadevector, 1, shadevector);
    qglUniform1f(u_alias_shadelight, shadelight);
    qglUniform1f(u_alias_tess_level, r_tessellation.value);

    while (1)
    {
        // get the vertex count and primitive type
        count = *order++;
        if (!count)
        {
            break; // done
        }
        qboolean fan = count < 0;
        if (fan)
        {
            count = -count;
        }

        int n = 0;
        do
        {
            if (n < ALIAS_MAX_CMD_VERTS)
            {
                // texture coordinates come from the draw list
                cmdverts[n][3] = reinterpret_cast<float *>(order)[0];
                cmdverts[n][4] = reinterpret_cast<float *>(order)[1];

                // vertexes and normals come from the frame list -- blend
                // between the previous and current animation frame's poses
                // rather than snapping straight to the new one. The
                // (unnormalized) blended normal is interpolated again across
                // the triangle and renormalized per pixel in the fragment
                // shader, giving real per-pixel lighting instead of the old
                // flat-shaded-per-vertex look.
                cmdverts[n][0] = verts0->v[0] + (verts1->v[0] - verts0->v[0]) * blend;
                cmdverts[n][1] = verts0->v[1] + (verts1->v[1] - verts0->v[1]) * blend;
                cmdverts[n][2] = verts0->v[2] + (verts1->v[2] - verts0->v[2]) * blend;
                float *normal0 = r_avertexnormals[verts0->lightnormalindex];
                float *normal1 = r_avertexnormals[verts1->lightnormalindex];
                cmdverts[n][5] = normal0[0] + (normal1[0] - normal0[0]) * blend;
                cmdverts[n][6] = normal0[1] + (normal1[1] - normal0[1]) * blend;
                cmdverts[n][7] = normal0[2] + (normal1[2] - normal0[2]) * blend;
                n++;
            }
            order += 2;
            verts0++;
            verts1++;
        } while (--count);

        Alias_EmitPrimitive(cmdverts, n, fan, GL_PATCHES);
    }

    Alias_EndDraw();
}

/*
=============
GL_DrawAliasShadow
=============
*/
extern vec3_t lightspot;

void GL_DrawAliasShadow(aliashdr_t *paliashdr, int posenum0, int posenum1, float blend)
{
    trivertx_t *verts0, *verts1;
    int *order;
    float height, lheight;
    int count;
    float cmdverts[ALIAS_MAX_CMD_VERTS][ALIAS_VERT_FLOATS];

    lheight = currententity->origin[2] - lightspot[2];

    verts0 = reinterpret_cast<trivertx_t *>(reinterpret_cast<byte *>(paliashdr) + paliashdr->posedata);
    verts0 += posenum0 * paliashdr->poseverts;
    verts1 = reinterpret_cast<trivertx_t *>(reinterpret_cast<byte *>(paliashdr) + paliashdr->posedata);
    verts1 += posenum1 * paliashdr->poseverts;
    order = reinterpret_cast<int *>(reinterpret_cast<byte *>(paliashdr) + paliashdr->commands);

    height = -lheight + 1.0;

    Alias_BeginDraw(alias_shadow_prog);
    Alias_SetMVP(u_alias_shadow_mvp);
    {
        const float shadow_color[4] = {0.f, 0.f, 0.f, 0.5f};
        qglUniform4fv(u_alias_shadow_color, 1, shadow_color);
    }

    while (1)
    {
        // get the vertex count and primitive type
        count = *order++;
        if (!count)
        {
            break; // done
        }
        qboolean fan = count < 0;
        if (fan)
        {
            count = -count;
        }

        int n = 0;
        do
        {
            // texture coordinates come from the draw list
            // (skipped for shadows)
            order += 2;

            if (n < ALIAS_MAX_CMD_VERTS)
            {
                vec3_t vert;
                vert[0] = verts0->v[0] + (verts1->v[0] - verts0->v[0]) * blend;
                vert[1] = verts0->v[1] + (verts1->v[1] - verts0->v[1]) * blend;
                vert[2] = verts0->v[2] + (verts1->v[2] - verts0->v[2]) * blend;

                vec3_t point;
                point[0] = vert[0] * paliashdr->scale[0] + paliashdr->scale_origin[0];
                point[1] = vert[1] * paliashdr->scale[1] + paliashdr->scale_origin[1];
                point[2] = vert[2] * paliashdr->scale[2] + paliashdr->scale_origin[2];

                point[0] -= shadevector[0] * (point[2] + lheight);
                point[1] -= shadevector[1] * (point[2] + lheight);
                point[2] = height;

                cmdverts[n][0] = point[0];
                cmdverts[n][1] = point[1];
                cmdverts[n][2] = point[2];
                cmdverts[n][3] = 0.f;
                cmdverts[n][4] = 0.f;
                cmdverts[n][5] = 0.f;
                cmdverts[n][6] = 0.f;
                cmdverts[n][7] = 0.f;
                n++;
            }

            verts0++;
            verts1++;
        } while (--count);

        Alias_EmitPrimitive(cmdverts, n, fan, GL_TRIANGLES);
    }

    Alias_EndDraw();
}

/*
=================
R_PoseForFrame

Resolves a .mdl frame index to a pose index into posedata, handling the
built-in interval-cycled "frame groups" some models use (e.g. torches
cycling through several poses on their own timer within a single logical
frame). Shared by R_SetupAliasFrame's old and new frame lookups below.
=================
*/
static int R_PoseForFrame(aliashdr_t *paliashdr, int frame)
{
    int pose, numposes;
    float interval;

    if ((frame >= paliashdr->numframes) || (frame < 0))
    {
        Con_DPrintf("R_AliasSetupFrame: no such frame {}\n", frame);
        frame = 0;
    }

    pose = paliashdr->frames[frame].firstpose;
    numposes = paliashdr->frames[frame].numposes;

    if (numposes > 1)
    {
        interval = paliashdr->frames[frame].interval;
        pose += (int)(CL_Time() / interval) % numposes;
    }

    return pose;
}

// QuakeC animation typically advances .frame once every 0.1s (the classic
// "self.nextthink = time + 0.1" walk-cycle idiom) regardless of how often
// the server actually simulates -- so even a locally-hosted single-player
// game, whose entity updates otherwise arrive every render frame, still
// only gets a new model frame at 10Hz. Blending position over this fixed
// window (rather than deriving it from network-message timing, which
// collapses to no interpolation at all for listen servers -- see
// CL_LerpPoint's sv.active check) is what actually smooths that out.
constexpr float ALIAS_ANIM_LERP_DURATION = 0.1f;

/*
=================
R_SetupAliasFrame

=================
*/
void R_SetupAliasFrame(int frame, aliashdr_t *paliashdr)
{
    int posenew = R_PoseForFrame(paliashdr, frame);
    int poseold = posenew;
    float blend = 1.0f;

    if (!cl_nolerp.value && currententity->oldframe != frame && currententity->oldframe >= 0 &&
        currententity->oldframe < paliashdr->numframes)
    {
        poseold = R_PoseForFrame(paliashdr, currententity->oldframe);
        blend = (float)((CL_Time() - currententity->frame_start_time) / ALIAS_ANIM_LERP_DURATION);
        if (blend < 0)
        {
            blend = 0;
        }
        if (blend > 1)
        {
            blend = 1;
        }
    }

    GL_DrawAliasFrame(paliashdr, poseold, posenew, blend);
}

/*
=================
R_DrawAliasModel

=================
*/
void R_DrawAliasModel(entity_t *e)
{
    int i;
    int lnum;
    vec3_t dist;
    float add;
    model_t *clmodel;
    vec3_t mins, maxs;
    aliashdr_t *paliashdr;
    float an;
    int anim;

    clmodel = currententity->model;

    VectorAdd(currententity->origin, clmodel->mins, mins);
    VectorAdd(currententity->origin, clmodel->maxs, maxs);

    if (R_CullBox(mins, maxs))
    {
        return;
    }

    VectorCopy(currententity->origin, r_entorigin);
    VectorSubtract(r_origin, r_entorigin, modelorg);

    //
    // get lighting information
    //

    ambientlight = shadelight = R_LightPoint(currententity->origin);

    // allways give the gun some light
    if (e == CL_ViewEnt() && ambientlight < 24)
    {
        ambientlight = shadelight = 24;
    }

    for (lnum = 0; lnum < MAX_DLIGHTS; lnum++)
    {
        if (cl_dlights[lnum].die >= CL_Time())
        {
            VectorSubtract(currententity->origin, cl_dlights[lnum].origin, dist);
            add = cl_dlights[lnum].radius - Length(dist);

            if (add > 0)
            {
                ambientlight += add;
                // ZOID models should be affected by dlights as well
                shadelight += add;
            }
        }
    }

    // clamp lighting so it doesn't overbright as much
    if (ambientlight > 128)
    {
        ambientlight = 128;
    }
    if (ambientlight + shadelight > 192)
    {
        shadelight = 192 - ambientlight;
    }

    // ZOID: never allow players to go totally black
    i = currententity - cl_entities;
    if (i >= 1 && i <= CL_MaxClients() /* && !strcmp (currententity->model->name, "progs/player.mdl") */)
    {
        if (ambientlight < 8)
        {
            ambientlight = shadelight = 8;
        }
    }

    // HACK HACK HACK -- no fullbright colors, so make torches full light
    if (!strcmp(clmodel->name, "progs/flame2.mdl") || !strcmp(clmodel->name, "progs/flame.mdl"))
    {
        ambientlight = shadelight = 256;
    }

    shadelight = shadelight / 200.0;

    an = e->angles[1] / 180 * M_PI;
    shadevector[0] = cos(-an);
    shadevector[1] = sin(-an);
    shadevector[2] = 1;
    VectorNormalize(shadevector);

    //
    // locate the proper data
    //
    paliashdr = static_cast<aliashdr_t *>(Mod_Extradata(currententity->model));

    c_alias_polys += paliashdr->numtris;

    //
    // draw all the triangles
    //

    GL_DisableMultitexture();

    R_RotateForEntity(e);

    if (!strcmp(clmodel->name, "progs/eyes.mdl") && gl_doubleeyes.value)
    {
        r_entity_matrix =
            glm::translate(r_entity_matrix, glm::vec3(paliashdr->scale_origin[0], paliashdr->scale_origin[1],
                                                      paliashdr->scale_origin[2] - (22 + 8)));
        // double size of eyes, since they are really hard to see in gl
        r_entity_matrix = glm::scale(
            r_entity_matrix, glm::vec3(paliashdr->scale[0] * 2, paliashdr->scale[1] * 2, paliashdr->scale[2] * 2));
    }
    else
    {
        r_entity_matrix =
            glm::translate(r_entity_matrix, glm::vec3(paliashdr->scale_origin[0], paliashdr->scale_origin[1],
                                                      paliashdr->scale_origin[2]));
        r_entity_matrix =
            glm::scale(r_entity_matrix, glm::vec3(paliashdr->scale[0], paliashdr->scale[1], paliashdr->scale[2]));
    }

    anim = (int)(CL_Time() * 10) & 3;
    GL_Bind(paliashdr->gl_texturenum[currententity->skinnum][anim]);

    // we can't dynamically colormap textures, so they are cached
    // seperately for the players.  Heads are just uncolored.
    if (currententity->colormap != vid.colormap && !gl_nocolors.value)
    {
        i = currententity - cl_entities;
        if (i >= 1 && i <= CL_MaxClients() /* && !strcmp (currententity->model->name, "progs/player.mdl") */)
        {
            GL_Bind(playertextures - 1 + i);
        }
    }

    R_SetupAliasFrame(currententity->frame, paliashdr);

    r_entity_matrix = glm::mat4(1.0f);

    if (r_shadows.value)
    {
        R_RotateForEntity(e);
        glEnable(GL_BLEND);
        GL_DrawAliasShadow(paliashdr, lastposenum_old, lastposenum_new, lastpose_blend);
        glDisable(GL_BLEND);
        r_entity_matrix = glm::mat4(1.0f);
    }
}

//==================================================================================

/*
=============
R_DrawEntitiesOnList
=============
*/
void R_DrawEntitiesOnList(void)
{
    int i;

    if (!r_drawentities.value)
    {
        return;
    }

    // draw sprites seperately, because of alpha blending
    for (i = 0; i < cl_numvisedicts; i++)
    {
        currententity = cl_visedicts[i];

        switch (currententity->model->type)
        {
        case modtype_t::mod_alias:
            R_DrawAliasModel(currententity);
            break;

        case modtype_t::mod_brush:
            R_DrawBrushModel(currententity);
            break;

        default:
            break;
        }
    }

    for (i = 0; i < cl_numvisedicts; i++)
    {
        currententity = cl_visedicts[i];

        switch (currententity->model->type)
        {
        case modtype_t::mod_sprite:
            R_DrawSpriteModel(currententity);
            break;
        }
    }
}

/*
=============
R_DrawViewModel
=============
*/
void R_DrawViewModel(void)
{
    float ambient[4], diffuse[4];
    int j;
    int lnum;
    vec3_t dist;
    float add;
    dlight_t *dl;
    int ambientlight, shadelight;

    if (!r_drawviewmodel.value)
    {
        return;
    }

    if (chase_active.value)
    {
        return;
    }

    if (envmap)
    {
        return;
    }

    if (!r_drawentities.value)
    {
        return;
    }

    if (CL_Items() & IT_INVISIBILITY)
    {
        return;
    }

    if (CL_Stat(STAT_HEALTH) <= 0)
    {
        return;
    }

    currententity = CL_ViewEnt();
    if (!currententity->model)
    {
        return;
    }

    j = R_LightPoint(currententity->origin);

    if (j < 24)
    {
        j = 24; // allways give some light on gun
    }
    ambientlight = j;
    shadelight = j;

    // add dynamic lights
    for (lnum = 0; lnum < MAX_DLIGHTS; lnum++)
    {
        dl = &cl_dlights[lnum];
        if (!dl->radius)
        {
            continue;
        }
        if (!dl->radius)
        {
            continue;
        }
        if (dl->die < CL_Time())
        {
            continue;
        }

        VectorSubtract(currententity->origin, dl->origin, dist);
        add = dl->radius - Length(dist);
        if (add > 0)
        {
            ambientlight += add;
        }
    }

    ambient[0] = ambient[1] = ambient[2] = ambient[3] = (float)ambientlight / 128;
    diffuse[0] = diffuse[1] = diffuse[2] = diffuse[3] = (float)shadelight / 128;

    // hack the depth range to prevent view model from poking into walls
    glDepthRange(gldepthmin, gldepthmin + 0.3 * (gldepthmax - gldepthmin));
    R_DrawAliasModel(currententity);
    glDepthRange(gldepthmin, gldepthmax);
}

/*
============
R_PolyBlend
============
*/
void R_PolyBlend(void)
{
    if (!gl_polyblend.value)
    {
        return;
    }
    if (!v_blend[3])
    {
        return;
    }

    GL_DisableMultitexture();

    glEnable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);

    {
        // Fixed "Z going up" view, independent of the player's actual
        // view angles -- this is a full-screen tint quad, not a world object.
        glm::mat4 view(1.0f);
        view = glm::rotate(view, glm::radians(-90.0f), glm::vec3(1, 0, 0));
        view = glm::rotate(view, glm::radians(90.0f), glm::vec3(0, 0, 1));
        glm::mat4 mvp = r_proj_matrix * view;

        float quad[4][5] = {
            {10, 100, 100, 0, 0},
            {10, -100, 100, 0, 0},
            {10, -100, -100, 0, 0},
            {10, 100, -100, 0, 0},
        };

        Billboard_BeginDraw();
        qglUniformMatrix4fv(u_billboard_mvp, 1, GL_FALSE, glm::value_ptr(mvp));
        qglUniform1i(u_billboard_flat, 1);
        qglUniform4fv(u_billboard_color, 1, v_blend);
        Billboard_DrawQuad(quad);
        Billboard_EndDraw();
    }

    glDisable(GL_BLEND);
}

int SignbitsForPlane(mplane_t *out)
{
    int bits, j;

    // for fast box on planeside test

    bits = 0;
    for (j = 0; j < 3; j++)
    {
        if (out->normal[j] < 0)
        {
            bits |= 1 << j;
        }
    }
    return bits;
}

void R_SetFrustum(void)
{
    int i;

    if (r_refdef.fov_x == 90)
    {
        // front side is visible

        VectorAdd(vpn, vright, frustum[0].normal);
        VectorSubtract(vpn, vright, frustum[1].normal);

        VectorAdd(vpn, vup, frustum[2].normal);
        VectorSubtract(vpn, vup, frustum[3].normal);
    }
    else
    {
        // rotate VPN right by FOV_X/2 degrees
        RotatePointAroundVector(frustum[0].normal, vup, vpn, -(90 - r_refdef.fov_x / 2));
        // rotate VPN left by FOV_X/2 degrees
        RotatePointAroundVector(frustum[1].normal, vup, vpn, 90 - r_refdef.fov_x / 2);
        // rotate VPN up by FOV_X/2 degrees
        RotatePointAroundVector(frustum[2].normal, vright, vpn, 90 - r_refdef.fov_y / 2);
        // rotate VPN down by FOV_X/2 degrees
        RotatePointAroundVector(frustum[3].normal, vright, vpn, -(90 - r_refdef.fov_y / 2));
    }

    for (i = 0; i < 4; i++)
    {
        frustum[i].type = PLANE_ANYZ;
        frustum[i].dist = DotProduct(r_origin, frustum[i].normal);
        frustum[i].signbits = SignbitsForPlane(&frustum[i]);
    }
}

/*
===============
R_SetupFrame
===============
*/
void R_SetupFrame(void)
{
    // don't allow cheats in multiplayer
    if (CL_MaxClients() > 1)
    {
        Cvar_Set("r_fullbright", "0");
    }

    R_AnimateLight();

    r_framecount++;

    // build the transformation matrix for the given view angles
    VectorCopy(r_refdef.vieworg, r_origin);

    AngleVectors(r_refdef.viewangles, vpn, vright, vup);

    // current viewleaf
    r_oldviewleaf = r_viewleaf;
    r_viewleaf = Mod_PointInLeaf(r_origin, CL_WorldModel());

    V_SetContentsColor(r_viewleaf->contents);
    V_CalcBlend();

    r_cache_thrash = false;

    c_brush_polys = 0;
    c_alias_polys = 0;
}

void MYgluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar)
{
    GLdouble xmin, xmax, ymin, ymax;

    ymax = zNear * tan(fovy * M_PI / 360.0);
    ymin = -ymax;

    xmin = ymin * aspect;
    xmax = ymax * aspect;

    r_proj_matrix = glm::frustum((float)xmin, (float)xmax, (float)ymin, (float)ymax, (float)zNear, (float)zFar);
}

/*
=============
R_SetupGL
=============
*/
void R_SetupGL(void)
{
    float screenaspect;
    extern int glwidth, glheight;
    int x, x2, y2, y, w, h;

    //
    // set up viewpoint
    //
    x = r_refdef.vrect.x * glwidth / vid.width;
    x2 = (r_refdef.vrect.x + r_refdef.vrect.width) * glwidth / vid.width;
    y = (vid.height - r_refdef.vrect.y) * glheight / vid.height;
    y2 = (vid.height - (r_refdef.vrect.y + r_refdef.vrect.height)) * glheight / vid.height;

    // fudge around because of frac screen scale
    if (x > 0)
    {
        x--;
    }
    if (x2 < glwidth)
    {
        x2++;
    }
    if (y2 < 0)
    {
        y2--;
    }
    if (y < glheight)
    {
        y++;
    }

    w = x2 - x;
    h = y - y2;

    if (envmap)
    {
        x = y2 = 0;
        w = h = 256;
    }

    glViewport(glx + x, gly + y2, w, h);
    screenaspect = (float)r_refdef.vrect.width / r_refdef.vrect.height;
    //	yfov = 2*atan((float)r_refdef.vrect.height/r_refdef.vrect.width)*180/M_PI;
    MYgluPerspective(r_refdef.fov_y, screenaspect, 4, 4096);

    if (mirror)
    {
        if (mirror_plane->normal[2])
        {
            r_proj_matrix = glm::scale(r_proj_matrix, glm::vec3(1, -1, 1));
        }
        else
        {
            r_proj_matrix = glm::scale(r_proj_matrix, glm::vec3(-1, 1, 1));
        }
        glCullFace(GL_BACK);
    }
    else
    {
        glCullFace(GL_FRONT);
    }

    r_world_matrix = glm::mat4(1.0f);

    r_world_matrix = glm::rotate(r_world_matrix, glm::radians(-90.0f), glm::vec3(1, 0, 0)); // put Z going up
    r_world_matrix = glm::rotate(r_world_matrix, glm::radians(90.0f), glm::vec3(0, 0, 1));  // put Z going up
    r_world_matrix = glm::rotate(r_world_matrix, glm::radians(-r_refdef.viewangles[2]), glm::vec3(1, 0, 0));
    r_world_matrix = glm::rotate(r_world_matrix, glm::radians(-r_refdef.viewangles[0]), glm::vec3(0, 1, 0));
    r_world_matrix = glm::rotate(r_world_matrix, glm::radians(-r_refdef.viewangles[1]), glm::vec3(0, 0, 1));
    r_world_matrix =
        glm::translate(r_world_matrix, glm::vec3(-r_refdef.vieworg[0], -r_refdef.vieworg[1], -r_refdef.vieworg[2]));

    r_entity_matrix = glm::mat4(1.0f);

    //
    // set drawing parms
    //
    if (gl_cull.value)
    {
        glEnable(GL_CULL_FACE);
    }
    else
    {
        glDisable(GL_CULL_FACE);
    }

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

/*
================
R_RenderScene

r_refdef must be set before the first call
================
*/
void R_RenderScene(void)
{
    R_SetupFrame();

    R_SetFrustum();

    R_SetupGL();

    R_MarkLeaves(); // done here so we know if we're in water

    R_DrawWorld(); // adds static entities to the list

    S_ExtraUpdate(); // don't let sound get messed up if going slow

    R_DrawEntitiesOnList();

    GL_DisableMultitexture();

    R_RenderDlights();

    R_DrawParticles();
}

/*
=============
R_Clear
=============
*/
void R_Clear(void)
{
    if (r_mirroralpha.value != 1.0)
    {
        if (gl_clear.value)
        {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }
        else
        {
            glClear(GL_DEPTH_BUFFER_BIT);
        }
        gldepthmin = 0;
        gldepthmax = 0.5;
        glDepthFunc(GL_LEQUAL);
    }
    else if (gl_ztrick.value)
    {
        static int trickframe;

        if (gl_clear.value)
        {
            glClear(GL_COLOR_BUFFER_BIT);
        }

        trickframe++;
        if (trickframe & 1)
        {
            gldepthmin = 0;
            gldepthmax = 0.49999f;
            glDepthFunc(GL_LEQUAL);
        }
        else
        {
            gldepthmin = 1;
            gldepthmax = 0.5;
            glDepthFunc(GL_GEQUAL);
        }
    }
    else
    {
        if (gl_clear.value)
        {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }
        else
        {
            glClear(GL_DEPTH_BUFFER_BIT);
        }
        gldepthmin = 0;
        gldepthmax = 1;
        glDepthFunc(GL_LEQUAL);
    }

    glDepthRange(gldepthmin, gldepthmax);
}

/*
=============
R_Mirror
=============
*/
void R_Mirror(void)
{
    float d;
    msurface_t *s;
    entity_t *ent;

    if (!mirror)
    {
        return;
    }

    r_base_world_matrix = r_world_matrix;

    d = DotProduct(r_refdef.vieworg, mirror_plane->normal) - mirror_plane->dist;
    VectorMA(r_refdef.vieworg, -2 * d, mirror_plane->normal, r_refdef.vieworg);

    d = DotProduct(vpn, mirror_plane->normal);
    VectorMA(vpn, -2 * d, mirror_plane->normal, vpn);

    r_refdef.viewangles[0] = -asin(vpn[2]) / M_PI * 180;
    r_refdef.viewangles[1] = atan2(vpn[1], vpn[0]) / M_PI * 180;
    r_refdef.viewangles[2] = -r_refdef.viewangles[2];

    ent = &cl_entities[CL_ViewEntity()];
    if (cl_numvisedicts < MAX_VISEDICTS)
    {
        cl_visedicts[cl_numvisedicts] = ent;
        cl_numvisedicts++;
    }

    gldepthmin = 0.5;
    gldepthmax = 1;
    glDepthRange(gldepthmin, gldepthmax);
    glDepthFunc(GL_LEQUAL);

    R_RenderScene();
    R_DrawWaterSurfaces();

    gldepthmin = 0;
    gldepthmax = 0.5;
    glDepthRange(gldepthmin, gldepthmax);
    glDepthFunc(GL_LEQUAL);

    // blend on top
    glEnable(GL_BLEND);
    // undoes the scale R_SetupGL applied for the recursive (reflected)
    // R_RenderScene() call above, since the mirror quad itself must be
    // drawn un-mirrored, from the original (pre-reflection) viewpoint.
    if (mirror_plane->normal[2])
    {
        r_proj_matrix = glm::scale(r_proj_matrix, glm::vec3(1, -1, 1));
    }
    else
    {
        r_proj_matrix = glm::scale(r_proj_matrix, glm::vec3(-1, 1, 1));
    }
    glCullFace(GL_FRONT);

    r_world_matrix = r_base_world_matrix;

    R_World_SetAlpha(r_mirroralpha.value);
    s = CL_WorldModel()->textures[mirrortexturenum]->texturechain;
    for (; s; s = s->texturechain)
    {
        R_RenderBrushPoly(s);
    }
    CL_WorldModel()->textures[mirrortexturenum]->texturechain = nullptr;
    R_World_SetAlpha(1.0f);
    glDisable(GL_BLEND);
}

/*
================
R_RenderView

r_refdef must be set before the first call
================
*/
void R_RenderView(void)
{
    double time1, time2;
    GLfloat colors[4] = {(GLfloat)0.0, (GLfloat)0.0, (GLfloat)1, (GLfloat)0.20};

    if (r_norefresh.value)
    {
        return;
    }

    if (!r_worldentity.model || !CL_WorldModel())
    {
        Sys_Error("R_RenderView: nullptr worldmodel");
    }

    if (r_speeds.value)
    {
        glFinish();
        time1 = Sys_FloatTime();
        c_brush_polys = 0;
        c_alias_polys = 0;
    }

    mirror = false;

    if (gl_finish.value)
    {
        glFinish();
    }

    R_Clear();

    // render normal view

    /***** Experimental silly looking fog ******
    ****** Use r_fullbright if you enable ******
        glFogi(GL_FOG_MODE, GL_LINEAR);
        glFogfv(GL_FOG_COLOR, colors);
        glFogf(GL_FOG_END, 512.0);
        glEnable(GL_FOG);
    ********************************************/

    R_RenderScene();
    R_DrawViewModel();
    R_DrawWaterSurfaces();

    //  More fog right here :)
    //	glDisable(GL_FOG);
    //  End of all fog code...

    // render mirror view
    R_Mirror();

    R_PolyBlend();

    if (r_speeds.value)
    {
        //		glFinish ();
        time2 = Sys_FloatTime();
        Con_Printf("{:3} ms  {:4} wpoly {:4} epoly\n", (int)((time2 - time1) * 1000), c_brush_polys, c_alias_polys);
    }
}
