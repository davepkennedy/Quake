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
// r_surf.c: surface-related refresh code

#include "quakedef.h"

int skytexturenum;

#ifndef GL_RGBA4
#define GL_RGBA4 0
#endif

int lightmap_bytes; // 1, 2, or 4

int lightmap_textures;

unsigned blocklights[18 * 18];

#define BLOCK_WIDTH 128
#define BLOCK_HEIGHT 128

#define MAX_LIGHTMAPS 64
int active_lightmaps;

struct glRect_t
{
    unsigned char l, t, w, h;
};

qboolean lightmap_modified[MAX_LIGHTMAPS];
glRect_t lightmap_rectchange[MAX_LIGHTMAPS];

int allocated[MAX_LIGHTMAPS][BLOCK_WIDTH];

// the lightmap texture data needs to be kept in
// main memory so texsubimage can update properly
byte lightmaps[4 * MAX_LIGHTMAPS * BLOCK_WIDTH * BLOCK_HEIGHT];

// For gl_texsort 0
msurface_t *skychain = nullptr;
msurface_t *waterchain = nullptr;

// -------------------------------------------------------------------------
// World surface renderer state (VAO / VBO / GLSL shader)
// -------------------------------------------------------------------------

static GLVertexArray world_vao;
static GLBuffer world_vbo;
static GLProgram world_prog;
static GLint u_world_mvp = -1;
static GLint u_world_tex = -1;
static GLint u_world_lm = -1;
static GLint u_world_lmonly = -1;
static GLint u_world_alpha = -1;
static GLint u_world_wireframe = -1;
static GLint u_world_wirecolor = -1;

// Current alpha for the world shader -- 1.0 (opaque) except while R_Mirror
// redraws the mirror quad through this same shader with r_mirroralpha.value.
static float world_alpha = 1.0f;

// r_showpvs debug view (see R_DrawWorld/R_RecursiveWorldNode): draws every
// surface the PVS/leaf traversal marks visible as a flat-colored wireframe
// instead of the normal textured+lightmapped fill.
static bool world_wireframe = false;
static const float world_wire_color[3] = {0.4f, 1.0f, 0.4f};

void R_World_SetAlpha(float a)
{
    world_alpha = a;
}

#define WORLD_STREAM_VERTS 4096
static float world_stream[WORLD_STREAM_VERTS * 7];

static const char world_vert_src[] = "#version 450 core\n"
                                     "layout(location = 0) in vec3 a_pos;\n"
                                     "layout(location = 1) in vec2 a_texuv;\n"
                                     "layout(location = 2) in vec2 a_lmuv;\n"
                                     "uniform mat4 u_mvp;\n"
                                     "out vec2 v_texuv;\n"
                                     "out vec2 v_lmuv;\n"
                                     "void main() {\n"
                                     "    v_texuv = a_texuv;\n"
                                     "    v_lmuv  = a_lmuv;\n"
                                     "    gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
                                     "}\n";

static const char world_frag_src[] = "#version 450 core\n"
                                     "in vec2 v_texuv;\n"
                                     "in vec2 v_lmuv;\n"
                                     "uniform sampler2D u_tex;\n"
                                     "uniform sampler2D u_lm;\n"
                                     "uniform int u_lm_only;\n"
                                     "uniform float u_alpha;\n"
                                     "uniform int u_wireframe;\n"
                                     "uniform vec3 u_wire_color;\n"
                                     "out vec4 frag_color;\n"
                                     "void main() {\n"
                                     "    if (u_wireframe != 0) {\n"
                                     "        frag_color = vec4(u_wire_color, 1.0);\n"
                                     "        return;\n"
                                     "    }\n"
                                     "    float lit = 1.0 - texture(u_lm, v_lmuv).r;\n"
                                     "    if (u_lm_only != 0)\n"
                                     "        frag_color = vec4(lit, lit, lit, u_alpha);\n"
                                     "    else\n"
                                     "        frag_color = vec4(texture(u_tex, v_texuv).rgb * lit, u_alpha);\n"
                                     "}\n";

/*
===============
R_AddDynamicLights
===============
*/
void R_AddDynamicLights(msurface_t *surf)
{
    int lnum;
    int sd, td;
    float dist, rad, minlight;
    vec3_t impact, local;
    int s, t;
    int i;
    int smax, tmax;
    mtexinfo_t *tex;

    smax = (surf->extents[0] >> 4) + 1;
    tmax = (surf->extents[1] >> 4) + 1;
    tex = surf->texinfo;

    for (lnum = 0; lnum < MAX_DLIGHTS; lnum++)
    {
        if (!(surf->dlightbits & (1 << lnum)))
        {
            continue; // not lit by this light
        }

        rad = cl_dlights[lnum].radius;
        dist = DotProduct(cl_dlights[lnum].origin, surf->plane->normal) - surf->plane->dist;
        rad -= fabs(dist);
        minlight = cl_dlights[lnum].minlight;
        if (rad < minlight)
        {
            continue;
        }
        minlight = rad - minlight;

        for (i = 0; i < 3; i++)
        {
            impact[i] = cl_dlights[lnum].origin[i] - surf->plane->normal[i] * dist;
        }

        local[0] = DotProduct(impact, tex->vecs[0]) + tex->vecs[0][3];
        local[1] = DotProduct(impact, tex->vecs[1]) + tex->vecs[1][3];

        local[0] -= surf->texturemins[0];
        local[1] -= surf->texturemins[1];

        for (t = 0; t < tmax; t++)
        {
            td = local[1] - t * 16;
            if (td < 0)
            {
                td = -td;
            }
            for (s = 0; s < smax; s++)
            {
                sd = local[0] - s * 16;
                if (sd < 0)
                {
                    sd = -sd;
                }
                if (sd > td)
                {
                    dist = sd + (td >> 1);
                }
                else
                {
                    dist = td + (sd >> 1);
                }
                if (dist < minlight)
                {
                    blocklights[t * smax + s] += (rad - dist) * 256;
                }
            }
        }
    }
}

/*
===============
R_BuildLightMap

Combine and scale multiple lightmaps into the 8.8 format in blocklights
===============
*/
void R_BuildLightMap(msurface_t *surf, byte *dest, int stride)
{
    int smax, tmax;
    int t;
    int i, j, size;
    byte *lightmap;
    unsigned scale;
    int maps;
    unsigned *bl;

    surf->cached_dlight = (surf->dlightframe == r_framecount);

    smax = (surf->extents[0] >> 4) + 1;
    tmax = (surf->extents[1] >> 4) + 1;
    size = smax * tmax;
    lightmap = surf->samples;

    // set to full bright if no light data
    if (r_fullbright.value || !CL_WorldModel()->lightdata)
    {
        for (i = 0; i < size; i++)
        {
            blocklights[i] = 255 * 256;
        }
    }
    else
    {
        // clear to no light
        for (i = 0; i < size; i++)
        {
            blocklights[i] = 0;
        }

        // add all the lightmaps
        if (lightmap)
        {
            for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
            {
                scale = d_lightstylevalue[surf->styles[maps]];
                surf->cached_light[maps] = scale; // 8.8 fraction
                for (i = 0; i < size; i++)
                {
                    blocklights[i] += lightmap[i] * scale;
                }
                lightmap += size; // skip to next lightmap
            }
        }

        // add all the dynamic lights
        if (surf->dlightframe == r_framecount)
        {
            R_AddDynamicLights(surf);
        }
    }

    // bound, invert, and shift
    switch (gl_lightmap_format)
    {
    case GL_RGBA:
        stride -= (smax << 2);
        bl = blocklights;
        for (i = 0; i < tmax; i++, dest += stride)
        {
            for (j = 0; j < smax; j++)
            {
                t = *bl++;
                t >>= 7;
                if (t > 255)
                {
                    t = 255;
                }
                dest[3] = 255 - t;
                dest += 4;
            }
        }
        break;
    case GL_ALPHA:
    case GL_LUMINANCE:
    case GL_INTENSITY:
    case GL_RED:
        bl = blocklights;
        for (i = 0; i < tmax; i++, dest += stride)
        {
            for (j = 0; j < smax; j++)
            {
                t = *bl++;
                t >>= 7;
                if (t > 255)
                {
                    t = 255;
                }
                dest[j] = 255 - t;
            }
        }
        break;
    default:
        Sys_Error("Bad lightmap format");
    }
}

/*
===============
R_TextureAnimation

Returns the proper texture for a given time and base texture
===============
*/
texture_t *R_TextureAnimation(texture_t *base)
{
    int reletive;
    int count;

    if (currententity->frame)
    {
        if (base->alternate_anims)
        {
            base = base->alternate_anims;
        }
    }

    if (!base->anim_total)
    {
        return base;
    }

    reletive = (int)(CL_Time() * 10) % base->anim_total;

    count = 0;
    while (base->anim_min > reletive || base->anim_max <= reletive)
    {
        base = base->anim_next;
        if (!base)
        {
            Sys_Error("R_TextureAnimation: broken cycle");
        }
        if (++count > 100)
        {
            Sys_Error("R_TextureAnimation: infinite cycle");
        }
    }

    return base;
}

/*
=============================================================

    BRUSH MODELS

=============================================================
*/

extern int solidskytexture;
extern int alphaskytexture;
extern float speedscale; // for top sky and bottom sky

lpMTexFUNC qglMTexCoord2fSGIS = nullptr;
lpSelTexFUNC qglSelectTextureSGIS = nullptr;

qboolean mtexenabled = false;

void GL_SelectTexture(GLenum target);

void GL_DisableMultitexture(void)
{
    if (mtexenabled)
    {
        glDisable(GL_TEXTURE_2D);
        GL_SelectTexture(TEXTURE0_SGIS);
        mtexenabled = false;
    }
}

void GL_EnableMultitexture(void)
{
    if (gl_mtexable)
    {
        GL_SelectTexture(TEXTURE1_SGIS);
        glEnable(GL_TEXTURE_2D);
        mtexenabled = true;
    }
}

/*
================
R_DrawSequentialPoly  (legacy stub — gl_texsort 0 fallback now shares
R_RenderBrushPoly with the default texsort path; see call site in
R_RecursiveWorldNode. Formerly used immediate-mode glBegin/glEnd and
SGIS multitexture to draw each surface as it was walked.)
================
*/

/*
================
DrawGLPoly  (legacy stub — replaced by DrawWorldSurfacePoly)
================
*/
void DrawGLPoly(glpoly_t *p)
{
    (void)p;
}

// -------------------------------------------------------------------------
// World surface renderer helpers
// -------------------------------------------------------------------------

void R_World_InitRenderer(void)
{
    if (world_prog)
    {
        return;
    }

    world_prog = GL_BuildProgram(world_vert_src, world_frag_src);
    if (!world_prog)
    {
        Sys_Error("R_World_InitRenderer: shader compile failed");
    }

    qglUseProgram(world_prog);
    u_world_mvp = qglGetUniformLocation(world_prog, "u_mvp");
    u_world_tex = qglGetUniformLocation(world_prog, "u_tex");
    u_world_lm = qglGetUniformLocation(world_prog, "u_lm");
    u_world_lmonly = qglGetUniformLocation(world_prog, "u_lm_only");
    u_world_alpha = qglGetUniformLocation(world_prog, "u_alpha");
    u_world_wireframe = qglGetUniformLocation(world_prog, "u_wireframe");
    u_world_wirecolor = qglGetUniformLocation(world_prog, "u_wire_color");
    qglUseProgram(0);

    world_vao = GLVertexArray::Create();
    world_vbo = GLBuffer::Create();
    qglBindVertexArray(world_vao);
    qglBindBuffer(GL_ARRAY_BUFFER, world_vbo);
    qglBufferData(GL_ARRAY_BUFFER, sizeof(world_stream), nullptr, GL_STREAM_DRAW);
    // location 0: xyz  (3 floats, offset  0, stride 7*4=28)
    qglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), nullptr);
    qglEnableVertexAttribArray(0);
    // location 1: world UV  (2 floats, offset 12)
    qglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 7 * sizeof(float), reinterpret_cast<void *>(3 * sizeof(float)));
    qglEnableVertexAttribArray(1);
    // location 2: lightmap UV  (2 floats, offset 20)
    qglVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 7 * sizeof(float), reinterpret_cast<void *>(5 * sizeof(float)));
    qglEnableVertexAttribArray(2);
    qglBindVertexArray(0);
    qglBindBuffer(GL_ARRAY_BUFFER, 0);
}

// Explicit teardown, called from Host_Shutdown before VID_Shutdown() destroys
// the GL context -- see gl_shader.h's comment on why this can't be left to
// these globals' own (static-duration) destructors.
void R_World_Shutdown(void)
{
    world_vao.Release();
    world_vbo.Release();
    world_prog.Release();
}

static void R_World_SetMVP(void)
{
    float mvp[16];
    GL_GetMVP(mvp);
    qglUniformMatrix4fv(u_world_mvp, 1, GL_FALSE, mvp);
}

static void R_World_BeginDraw(void)
{
    R_World_InitRenderer();
    qglUseProgram(world_prog);
    qglBindVertexArray(world_vao);
    qglBindBuffer(GL_ARRAY_BUFFER, world_vbo);
    qglUniform1i(u_world_tex, 0);
    qglUniform1i(u_world_lm, 1);
    qglUniform1i(u_world_lmonly, (int)r_lightmap.value);
    qglUniform1f(u_world_alpha, world_alpha);
    qglUniform1i(u_world_wireframe, world_wireframe ? 1 : 0);
    qglUniform3fv(u_world_wirecolor, 1, world_wire_color);
}

static void R_World_EndDraw(void)
{
    qglBindVertexArray(0);
    qglUseProgram(0);
    qglActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    qglActiveTexture(GL_TEXTURE0);
}

// Update the CPU-side lightmap block for one surface; does NOT draw or upload.
static void R_UpdateSurfaceLightmap(msurface_t *fa)
{
    int maps, smax, tmax;
    glRect_t *theRect;
    byte *base;

    bool styleChanged = false;
    for (maps = 0; maps < MAXLIGHTMAPS && fa->styles[maps] != 255; maps++)
    {
        if (d_lightstylevalue[fa->styles[maps]] != fa->cached_light[maps])
        {
            styleChanged = true;
            break;
        }
    }

    if (styleChanged || fa->dlightframe == r_framecount || fa->cached_dlight)
    {
        if (r_dynamic.value)
        {
            lightmap_modified[fa->lightmaptexturenum] = true;
            theRect = &lightmap_rectchange[fa->lightmaptexturenum];
            if (fa->light_t < theRect->t)
            {
                if (theRect->h)
                {
                    theRect->h += theRect->t - fa->light_t;
                }
                theRect->t = fa->light_t;
            }
            if (fa->light_s < theRect->l)
            {
                if (theRect->w)
                {
                    theRect->w += theRect->l - fa->light_s;
                }
                theRect->l = fa->light_s;
            }
            smax = (fa->extents[0] >> 4) + 1;
            tmax = (fa->extents[1] >> 4) + 1;
            if ((theRect->w + theRect->l) < (fa->light_s + smax))
            {
                theRect->w = (fa->light_s - theRect->l) + smax;
            }
            if ((theRect->h + theRect->t) < (fa->light_t + tmax))
            {
                theRect->h = (fa->light_t - theRect->t) + tmax;
            }
            base = lightmaps + fa->lightmaptexturenum * lightmap_bytes * BLOCK_WIDTH * BLOCK_HEIGHT;
            base += fa->light_t * BLOCK_WIDTH * lightmap_bytes + fa->light_s * lightmap_bytes;
            R_BuildLightMap(fa, base, BLOCK_WIDTH * lightmap_bytes);
        }
    }
}

// Upload all dirty lightmap atlases to GL texture unit 1.
static void R_UploadLightmaps(void)
{
    glRect_t *r;

    for (int i = 0; i < MAX_LIGHTMAPS; i++)
    {
        if (!lightmap_modified[i])
        {
            continue;
        }
        lightmap_modified[i] = false;
        r = &lightmap_rectchange[i];
        qglActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, lightmap_textures + i);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, r->t, BLOCK_WIDTH, r->h, gl_lightmap_format, GL_UNSIGNED_BYTE,
                        lightmaps + (i * BLOCK_HEIGHT + r->t) * BLOCK_WIDTH * lightmap_bytes);
        r->l = BLOCK_WIDTH;
        r->t = BLOCK_HEIGHT;
        r->h = 0;
        r->w = 0;
        qglActiveTexture(GL_TEXTURE0);
    }
}

// Draw one glpoly_t chain using the active world shader.
// Caller must have already bound the world texture to unit 0.
// lmtex is the lightmap atlas index (bound to unit 1 inside this function).
// warp applies the water-surface sinusoidal displacement to vertex positions.
static void DrawWorldSurfacePoly(glpoly_t *p, int lmtex, bool warp)
{
    for (; p; p = p->next)
    {
        int n = p->numverts;
        int ntri = n - 2;
        if (ntri < 1 || ntri * 3 > WORLD_STREAM_VERTS)
        {
            continue;
        }

        float *out = world_stream;

        for (int i = 1; i < n - 1; i++)
        {
            const float *verts[3] = {p->verts[0], p->verts[i], p->verts[i + 1]};
            for (int vi = 0; vi < 3; vi++)
            {
                const float *v = verts[vi];
                if (warp)
                {
                    out[0] = v[0] + 8 * sin(v[1] * 0.05 + realtime) * sin(v[2] * 0.05 + realtime);
                    out[1] = v[1] + 8 * sin(v[0] * 0.05 + realtime) * sin(v[2] * 0.05 + realtime);
                    out[2] = v[2];
                }
                else
                {
                    out[0] = v[0];
                    out[1] = v[1];
                    out[2] = v[2];
                }
                out[3] = v[3];
                out[4] = v[4];
                out[5] = v[5];
                out[6] = v[6];
                out += 7;
            }
        }

        // Bind this surface's lightmap atlas to unit 1
        qglActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, lightmap_textures + lmtex);
        qglActiveTexture(GL_TEXTURE0);

        int nverts = ntri * 3;
        qglBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(nverts * 7 * sizeof(float)), world_stream, GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, nverts);
        c_brush_polys++;
    }
}

/*
================
R_BlendLightmaps  (no-op — combined world+lightmap shader handles everything)
================
*/
void R_BlendLightmaps(void)
{
}

/*
================
R_RenderBrushPoly  (combined update + upload + draw for mirror/standalone calls)
================
*/
void R_RenderBrushPoly(msurface_t *fa)
{
    if (fa->flags & SURF_DRAWSKY)
    {
        EmitBothSkyLayers(fa);
        return;
    }

    texture_t *t = R_TextureAnimation(fa->texinfo->texture);
    GL_Bind(t->gl_texturenum);

    if (fa->flags & SURF_DRAWTURB)
    {
        EmitWaterPolys(fa);
        return;
    }

    // Update CPU lightmap, upload if dirty, then draw combined.
    R_UpdateSurfaceLightmap(fa);

    if (lightmap_modified[fa->lightmaptexturenum])
    {
        glRect_t *r = &lightmap_rectchange[fa->lightmaptexturenum];
        qglActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, lightmap_textures + fa->lightmaptexturenum);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, r->t, BLOCK_WIDTH, r->h, gl_lightmap_format, GL_UNSIGNED_BYTE,
                        lightmaps + (fa->lightmaptexturenum * BLOCK_HEIGHT + r->t) * BLOCK_WIDTH * lightmap_bytes);
        r->l = BLOCK_WIDTH;
        r->t = BLOCK_HEIGHT;
        r->h = 0;
        r->w = 0;
        lightmap_modified[fa->lightmaptexturenum] = false;
        qglActiveTexture(GL_TEXTURE0);
    }

    R_World_BeginDraw();
    R_World_SetMVP();
    DrawWorldSurfacePoly(fa->polys, fa->lightmaptexturenum, !!(fa->flags & SURF_UNDERWATER));
    R_World_EndDraw();
}

/*
================
R_MirrorChain
================
*/
void R_MirrorChain(msurface_t *s)
{
    if (mirror)
    {
        return;
    }
    mirror = true;
    mirror_plane = s->plane;
}

/*
================
R_DrawWaterSurfaces
================
*/
void R_DrawWaterSurfaces(void)
{
    int i;
    msurface_t *s;
    texture_t *t;

    if (r_wateralpha.value == 1.0 && gl_texsort.value)
    {
        return;
    }

    if (r_wateralpha.value < 1.0)
    {
        glEnable(GL_BLEND);
    }

    if (!gl_texsort.value)
    {
        if (!waterchain)
        {
            return;
        }

        for (s = waterchain; s; s = s->texturechain)
        {
            GL_Bind(s->texinfo->texture->gl_texturenum);
            EmitWaterPolys(s);
        }

        waterchain = nullptr;
    }
    else
    {

        model_t *worldmodel = CL_WorldModel();
        for (i = 0; i < worldmodel->numtextures; i++)
        {
            t = worldmodel->textures[i];
            if (!t)
            {
                continue;
            }
            s = t->texturechain;
            if (!s)
            {
                continue;
            }
            if (!(s->flags & SURF_DRAWTURB))
            {
                continue;
            }

            // set modulate mode explicitly

            GL_Bind(t->gl_texturenum);

            for (; s; s = s->texturechain)
            {
                EmitWaterPolys(s);
            }

            t->texturechain = nullptr;
        }
    }

    if (r_wateralpha.value < 1.0)
    {
        glDisable(GL_BLEND);
    }
}


/*
================
DrawTextureChains  (three-phase: update lightmaps, upload, draw combined)
================
*/
void DrawTextureChains(void)
{
    int i;
    msurface_t *s;
    texture_t *t;

    // gl_texsort 0: regular and underwater surfaces were already drawn
    // immediately by R_RenderBrushPoly as R_RecursiveWorldNode walked the
    // BSP; only the deferred sky chain still needs drawing here. Water is
    // handled separately by R_DrawWaterSurfaces via waterchain.
    if (!gl_texsort.value)
    {
        if (skychain)
        {
            R_DrawSkyChain(skychain);
            skychain = nullptr;
        }
        return;
    }

    // Phase 1: compute all CPU-side lightmap data for every visible surface.
    // Done before any uploads so that surfaces sharing an atlas are all
    // updated before the atlas is sent to the GPU.
    model_t *worldmodel = CL_WorldModel();
    for (i = 0; i < worldmodel->numtextures; i++)
    {
        t = worldmodel->textures[i];
        if (!t)
        {
            continue;
        }
        for (s = t->texturechain; s; s = s->texturechain)
        {
            if (!(s->flags & (SURF_DRAWSKY | SURF_DRAWTURB)))
            {
                R_UpdateSurfaceLightmap(s);
            }
        }
    }

    // Phase 2: upload any dirty lightmap atlas regions.
    R_UploadLightmaps();

    // Phase 3: draw every surface using the combined world+lightmap shader.
    R_World_BeginDraw();
    R_World_SetMVP();

    for (i = 0; i < worldmodel->numtextures; i++)
    {
        t = worldmodel->textures[i];
        if (!t)
        {
            continue;
        }
        s = t->texturechain;
        if (!s)
        {
            continue;
        }

        if (i == skytexturenum)
        {
            R_World_EndDraw();
            R_DrawSkyChain(s);
            R_World_BeginDraw();
            R_World_SetMVP();
        }
        else if (i == mirrortexturenum && r_mirroralpha.value != 1.0)
        {
            R_MirrorChain(s);
            continue; // chain kept for R_Mirror() in gl_rmain.cpp
        }
        else
        {
            if ((s->flags & SURF_DRAWTURB) && r_wateralpha.value != 1.0)
            {
                continue; // transparent water drawn later in R_DrawWaterSurfaces
            }

            for (; s; s = s->texturechain)
            {
                if (s->flags & SURF_DRAWTURB)
                {
                    // Opaque water: EmitWaterPolys uses immediate mode
                    R_World_EndDraw();
                    GL_Bind(s->texinfo->texture->gl_texturenum);
                    EmitWaterPolys(s);
                    R_World_BeginDraw();
                    R_World_SetMVP();
                }
                else
                {
                    texture_t *at = R_TextureAnimation(s->texinfo->texture);
                    GL_Bind(at->gl_texturenum);
                    DrawWorldSurfacePoly(s->polys, s->lightmaptexturenum, !!(s->flags & SURF_UNDERWATER));
                }
            }
        }

        t->texturechain = nullptr;
    }

    R_World_EndDraw();
}

/*
=================
R_DrawBrushModel
=================
*/
void R_DrawBrushModel(entity_t *e)
{
    int k;
    vec3_t mins, maxs;
    int i;
    msurface_t *psurf;
    float dot;
    mplane_t *pplane;
    model_t *clmodel;
    qboolean rotated;

    currententity = e;
    currenttexture = -1;

    clmodel = e->model;

    if (e->angles[0] || e->angles[1] || e->angles[2])
    {
        rotated = true;
        for (i = 0; i < 3; i++)
        {
            mins[i] = e->origin[i] - clmodel->radius;
            maxs[i] = e->origin[i] + clmodel->radius;
        }
    }
    else
    {
        rotated = false;
        VectorAdd(e->origin, clmodel->mins, mins);
        VectorAdd(e->origin, clmodel->maxs, maxs);
    }

    if (R_CullBox(mins, maxs))
    {
        return;
    }

    VectorSubtract(r_refdef.vieworg, e->origin, modelorg);
    if (rotated)
    {
        vec3_t temp;
        vec3_t forward, right, up;

        VectorCopy(modelorg, temp);
        AngleVectors(e->angles, forward, right, up);
        modelorg[0] = DotProduct(temp, forward);
        modelorg[1] = -DotProduct(temp, right);
        modelorg[2] = DotProduct(temp, up);
    }

    psurf = &clmodel->surfaces[clmodel->firstmodelsurface];

    // calculate dynamic lighting for bmodel if it's not an
    // instanced model
    if (clmodel->firstmodelsurface != 0 && !gl_flashblend.value)
    {
        for (k = 0; k < MAX_DLIGHTS; k++)
        {
            if ((cl_dlights[k].die < CL_Time()) || (!cl_dlights[k].radius))
            {
                continue;
            }

            R_MarkLights(&cl_dlights[k], 1 << k, clmodel->nodes + clmodel->hulls[0].firstclipnode);
        }
    }

    e->angles[0] = -e->angles[0]; // stupid quake bug
    R_RotateForEntity(e);
    e->angles[0] = -e->angles[0]; // stupid quake bug

    // Phase 1: update CPU lightmaps for all visible surfaces
    psurf = &clmodel->surfaces[clmodel->firstmodelsurface];
    for (i = 0; i < clmodel->nummodelsurfaces; i++, psurf++)
    {
        pplane = psurf->plane;
        dot = DotProduct(modelorg, pplane->normal) - pplane->dist;
        if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
            (!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
        {
            if (!(psurf->flags & (SURF_DRAWSKY | SURF_DRAWTURB)))
            {
                R_UpdateSurfaceLightmap(psurf);
            }
        }
    }

    // Phase 2: upload dirty lightmap atlases
    R_UploadLightmaps();

    // Phase 3: draw with combined world+lightmap shader
    R_World_BeginDraw();
    R_World_SetMVP();

    psurf = &clmodel->surfaces[clmodel->firstmodelsurface];
    for (i = 0; i < clmodel->nummodelsurfaces; i++, psurf++)
    {
        pplane = psurf->plane;
        dot = DotProduct(modelorg, pplane->normal) - pplane->dist;
        if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
            (!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
        {
            if (psurf->flags & SURF_DRAWSKY)
            {
                R_World_EndDraw();
                EmitBothSkyLayers(psurf);
                R_World_BeginDraw();
                R_World_SetMVP();
            }
            else if (psurf->flags & SURF_DRAWTURB)
            {
                R_World_EndDraw();
                GL_Bind(psurf->texinfo->texture->gl_texturenum);
                EmitWaterPolys(psurf);
                R_World_BeginDraw();
                R_World_SetMVP();
            }
            else
            {
                texture_t *at = R_TextureAnimation(psurf->texinfo->texture);
                GL_Bind(at->gl_texturenum);
                DrawWorldSurfacePoly(psurf->polys, psurf->lightmaptexturenum, !!(psurf->flags & SURF_UNDERWATER));
            }
        }
    }

    R_World_EndDraw();
    r_entity_matrix = glm::mat4(1.0f);
}

/*
=============================================================

    WORLD MODEL

=============================================================
*/

/*
================
R_RecursiveWorldNode
================
*/
void R_RecursiveWorldNode(mnode_t *node)
{
    int c, side;
    mplane_t *plane;
    msurface_t *surf, **mark;
    mleaf_t *pleaf;
    double dot;

    if (node->contents == CONTENTS_SOLID)
    {
        return; // solid
    }

    if (node->visframe != r_visframecount)
    {
        return;
    }
    // r_showpvs: skip the frustum cull too, so the PVS debug view shows the
    // full envelope of what the leaf traversal considers visible regardless
    // of which way the camera is pointed.
    if (!r_showpvs.value && R_CullBox(node->mins, node->maxs))
    {
        return;
    }

    // if a leaf node, draw stuff
    if (node->contents < 0)
    {
        pleaf = (mleaf_t *)node;

        mark = pleaf->firstmarksurface;
        c = pleaf->nummarksurfaces;

        if (c)
        {
            do
            {
                (*mark)->visframe = r_framecount;
                mark++;
            } while (--c);
        }

        // deal with model fragments in this leaf
        if (pleaf->efrags)
        {
            R_StoreEfrags(&pleaf->efrags);
        }

        return;
    }

    // node is just a decision point, so go down the apropriate sides

    // find which side of the node we are on
    plane = node->plane;

    switch (plane->type)
    {
    case PLANE_X:
        dot = modelorg[0] - plane->dist;
        break;
    case PLANE_Y:
        dot = modelorg[1] - plane->dist;
        break;
    case PLANE_Z:
        dot = modelorg[2] - plane->dist;
        break;
    default:
        dot = DotProduct(modelorg, plane->normal) - plane->dist;
        break;
    }

    if (dot >= 0)
    {
        side = 0;
    }
    else
    {
        side = 1;
    }

    // recurse down the children, front side first
    R_RecursiveWorldNode(node->children[side]);

    // draw stuff
    c = node->numsurfaces;

    if (c)
    {
        surf = CL_WorldModel()->surfaces + node->firstsurface;

        if (dot < 0 - BACKFACE_EPSILON)
        {
            side = SURF_PLANEBACK;
        }
        else if (dot > BACKFACE_EPSILON)
        {
            side = 0;
        }
        {
            for (; c; c--, surf++)
            {
                if (surf->visframe != r_framecount)
                {
                    continue;
                }

                if (r_showpvs.value)
                {
                    // PVS debug view: wireframe every surface the leaf
                    // traversal marked visible, front- or back-facing --
                    // deliberately the superset the backface test below
                    // would otherwise partially hide.
                    DrawWorldSurfacePoly(surf->polys, surf->lightmaptexturenum, false);
                    continue;
                }

                // don't backface underwater surfaces, because they warp
                if (!(surf->flags & SURF_UNDERWATER) && ((dot < 0) ^ !!(surf->flags & SURF_PLANEBACK)))
                {
                    continue; // wrong side
                }

                // if sorting by texture, just store it out
                if (gl_texsort.value)
                {
                    if (!mirror || surf->texinfo->texture != CL_WorldModel()->textures[mirrortexturenum])
                    {
                        surf->texturechain = surf->texinfo->texture->texturechain;
                        surf->texinfo->texture->texturechain = surf;
                    }
                }
                else if (surf->flags & SURF_DRAWSKY)
                {
                    surf->texturechain = skychain;
                    skychain = surf;
                }
                else if (surf->flags & SURF_DRAWTURB)
                {
                    surf->texturechain = waterchain;
                    waterchain = surf;
                }
                else
                {
                    R_RenderBrushPoly(surf);
                }
            }
        }
    }

    // recurse down the back side
    R_RecursiveWorldNode(node->children[!side]);
}

/*
=============
R_DrawWorld
=============
*/
void R_DrawWorld(void)
{
    entity_t ent;

    memset(&ent, 0, sizeof(ent));
    ent.model = CL_WorldModel();

    VectorCopy(r_refdef.vieworg, modelorg);

    currententity = &ent;
    currenttexture = -1;

    if (r_showpvs.value)
    {
        // Wireframe the PVS instead of the normal textured+lightmapped
        // world: one shader/VAO bind for the whole traversal, surfaces are
        // drawn immediately as they're walked (see R_RecursiveWorldNode)
        // rather than sorted into per-texture chains.
        world_wireframe = true;
        R_World_BeginDraw();
        R_World_SetMVP();
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

        R_RecursiveWorldNode(CL_WorldModel()->nodes);

        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        R_World_EndDraw();
        world_wireframe = false;
        return;
    }

    R_RecursiveWorldNode(CL_WorldModel()->nodes);

    DrawTextureChains();

    R_BlendLightmaps();

}

/*
===============
R_MarkLeaves
===============
*/
void R_MarkLeaves(void)
{
    byte *vis;
    mnode_t *node;
    int i;
    byte solid[4096];

    if (r_oldviewleaf == r_viewleaf && !r_novis.value)
    {
        return;
    }

    if (mirror)
    {
        return;
    }

    r_visframecount++;
    r_oldviewleaf = r_viewleaf;

    model_t *worldmodel = CL_WorldModel();
    if (r_novis.value)
    {
        vis = solid;
        memset(solid, 0xff, (worldmodel->numleafs + 7) >> 3);
    }
    else
    {
        vis = Mod_LeafPVS(r_viewleaf, worldmodel);
    }

    for (i = 0; i < worldmodel->numleafs; i++)
    {
        if (vis[i >> 3] & (1 << (i & 7)))
        {
            node = (mnode_t *)&worldmodel->leafs[i + 1];
            do
            {
                if (node->visframe == r_visframecount)
                {
                    break;
                }
                node->visframe = r_visframecount;
                node = node->parent;
            } while (node);
        }
    }
}

/*
=============================================================================

  LIGHTMAP ALLOCATION

=============================================================================
*/

// returns a texture number and the position inside it
int AllocBlock(int w, int h, int *x, int *y)
{
    int i, j;
    int best, best2;
    int texnum;

    for (texnum = 0; texnum < MAX_LIGHTMAPS; texnum++)
    {
        best = BLOCK_HEIGHT;

        for (i = 0; i < BLOCK_WIDTH - w; i++)
        {
            best2 = 0;

            for (j = 0; j < w; j++)
            {
                if (allocated[texnum][i + j] >= best)
                {
                    break;
                }
                if (allocated[texnum][i + j] > best2)
                {
                    best2 = allocated[texnum][i + j];
                }
            }
            if (j == w)
            { // this is a valid spot
                *x = i;
                *y = best = best2;
            }
        }

        if (best + h > BLOCK_HEIGHT)
        {
            continue;
        }

        for (i = 0; i < w; i++)
        {
            allocated[texnum][*x + i] = best + h;
        }

        return texnum;
    }

    Sys_Error("AllocBlock: full");
}

mvertex_t *r_pcurrentvertbase;
model_t *currentmodel;

int nColinElim;

/*
================
BuildSurfaceDisplayList
================
*/
void BuildSurfaceDisplayList(msurface_t *fa)
{
    int i, lindex, lnumverts;
    medge_t *pedges, *r_pedge;
    int vertpage;
    float *vec;
    float s, t;
    glpoly_t *poly;

    // reconstruct the polygon
    pedges = currentmodel->edges;
    lnumverts = fa->numedges;
    vertpage = 0;

    //
    // draw texture
    //
    poly = (glpoly_t *)Hunk_Alloc(sizeof(glpoly_t) + (lnumverts - 4) * VERTEXSIZE * sizeof(float));
    poly->next = fa->polys;
    poly->flags = fa->flags;
    fa->polys = poly;
    poly->numverts = lnumverts;

    for (i = 0; i < lnumverts; i++)
    {
        lindex = currentmodel->surfedges[fa->firstedge + i];

        if (lindex > 0)
        {
            r_pedge = &pedges[lindex];
            vec = r_pcurrentvertbase[r_pedge->v[0]].position;
        }
        else
        {
            r_pedge = &pedges[-lindex];
            vec = r_pcurrentvertbase[r_pedge->v[1]].position;
        }
        s = DotProduct(vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
        s /= fa->texinfo->texture->width;

        t = DotProduct(vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
        t /= fa->texinfo->texture->height;

        VectorCopy(vec, poly->verts[i]);
        poly->verts[i][3] = s;
        poly->verts[i][4] = t;

        //
        // lightmap texture coordinates
        //
        s = DotProduct(vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
        s -= fa->texturemins[0];
        s += fa->light_s * 16;
        s += 8;
        s /= BLOCK_WIDTH * 16; // fa->texinfo->texture->width;

        t = DotProduct(vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
        t -= fa->texturemins[1];
        t += fa->light_t * 16;
        t += 8;
        t /= BLOCK_HEIGHT * 16; // fa->texinfo->texture->height;

        poly->verts[i][5] = s;
        poly->verts[i][6] = t;
    }

    //
    // remove co-linear points - Ed
    //
    if (!gl_keeptjunctions.value && !(fa->flags & SURF_UNDERWATER))
    {
        for (i = 0; i < lnumverts; ++i)
        {
            vec3_t v1, v2;
            float *prev, *cur, *next;

            prev = poly->verts[(i + lnumverts - 1) % lnumverts];
            cur = poly->verts[i];
            next = poly->verts[(i + 1) % lnumverts];

            VectorSubtract(cur, prev, v1);
            VectorNormalize(v1);
            VectorSubtract(next, prev, v2);
            VectorNormalize(v2);

// skip co-linear points
#define COLINEAR_EPSILON 0.001
            if ((fabs(v1[0] - v2[0]) <= COLINEAR_EPSILON) && (fabs(v1[1] - v2[1]) <= COLINEAR_EPSILON) &&
                (fabs(v1[2] - v2[2]) <= COLINEAR_EPSILON))
            {
                int j;
                for (j = i + 1; j < lnumverts; ++j)
                {
                    int k;
                    for (k = 0; k < VERTEXSIZE; ++k)
                    {
                        poly->verts[j - 1][k] = poly->verts[j][k];
                    }
                }
                --lnumverts;
                ++nColinElim;
                // retry next vertex next time, which is now current vertex
                --i;
            }
        }
    }
    poly->numverts = lnumverts;
}

/*
========================
GL_CreateSurfaceLightmap
========================
*/
void GL_CreateSurfaceLightmap(msurface_t *surf)
{
    int smax, tmax;
    byte *base;

    if (surf->flags & (SURF_DRAWSKY | SURF_DRAWTURB))
    {
        return;
    }

    smax = (surf->extents[0] >> 4) + 1;
    tmax = (surf->extents[1] >> 4) + 1;

    surf->lightmaptexturenum = AllocBlock(smax, tmax, &surf->light_s, &surf->light_t);
    base = lightmaps + surf->lightmaptexturenum * lightmap_bytes * BLOCK_WIDTH * BLOCK_HEIGHT;
    base += (surf->light_t * BLOCK_WIDTH + surf->light_s) * lightmap_bytes;
    R_BuildLightMap(surf, base, BLOCK_WIDTH * lightmap_bytes);
}

/*
==================
GL_BuildLightmaps

Builds the lightmap texture
with all the surfaces from all brush models
==================
*/
void GL_BuildLightmaps(void)
{
    int i, j;
    model_t *m;
    extern qboolean isPermedia;

    memset(allocated, 0, sizeof(allocated));

    r_framecount = 1; // no dlightcache

    if (!lightmap_textures)
    {
        GL_ReserveTextureNames(MAX_LIGHTMAPS);
        lightmap_textures = texture_extension_number;
        texture_extension_number += MAX_LIGHTMAPS;
    }

    // GL_LUMINANCE/GL_ALPHA/GL_INTENSITY are all removed as texture formats
    // under Core Profile (GL_INVALID_ENUM); the shader-based lightmap path
    // only ever samples the red channel (see world_frag_src's "texture(u_lm,
    // v_lmuv).r"), so GL_RED -- a plain GL 1.0 base format, still valid in
    // core -- is a drop-in single-channel replacement for all three.
    gl_lightmap_format = GL_RED;
    // default differently on the Permedia
    if (isPermedia)
    {
        gl_lightmap_format = GL_RGBA;
    }

    if (COM_CheckParm("-lm_1"))
    {
        gl_lightmap_format = GL_RED;
    }
    if (COM_CheckParm("-lm_a"))
    {
        gl_lightmap_format = GL_RED;
    }
    if (COM_CheckParm("-lm_i"))
    {
        gl_lightmap_format = GL_RED;
    }
    if (COM_CheckParm("-lm_2"))
    {
        gl_lightmap_format = GL_RGBA4;
    }
    if (COM_CheckParm("-lm_4"))
    {
        gl_lightmap_format = GL_RGBA;
    }

    switch (gl_lightmap_format)
    {
    case GL_RGBA:
        lightmap_bytes = 4;
        break;
    case GL_RGBA4:
        lightmap_bytes = 2;
        break;
    case GL_LUMINANCE:
    case GL_INTENSITY:
    case GL_ALPHA:
    case GL_RED:
        lightmap_bytes = 1;
        break;
    }

    for (j = 1; j < MAX_MODELS; j++)
    {
        m = CL_ModelPrecache(j);
        if (!m)
        {
            break;
        }
        if (m->name[0] == '*')
        {
            continue;
        }
        r_pcurrentvertbase = m->vertexes;
        currentmodel = m;
        for (i = 0; i < m->numsurfaces; i++)
        {
            GL_CreateSurfaceLightmap(m->surfaces + i);
            if (m->surfaces[i].flags & SURF_DRAWTURB)
            {
                continue;
            }
            if (m->surfaces[i].flags & SURF_DRAWSKY)
            {
                continue;
            }
            BuildSurfaceDisplayList(m->surfaces + i);
        }
    }

    if (!gl_texsort.value)
    {
        GL_SelectTexture(TEXTURE1_SGIS);
    }

    //
    // upload all lightmaps that were filled
    //
    for (i = 0; i < MAX_LIGHTMAPS; i++)
    {
        if (!allocated[i][0])
        {
            break; // no more used
        }
        lightmap_modified[i] = false;
        lightmap_rectchange[i].l = BLOCK_WIDTH;
        lightmap_rectchange[i].t = BLOCK_HEIGHT;
        lightmap_rectchange[i].w = 0;
        lightmap_rectchange[i].h = 0;
        GL_Bind(lightmap_textures + i);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, gl_lightmap_format, BLOCK_WIDTH, BLOCK_HEIGHT, 0, gl_lightmap_format,
                     GL_UNSIGNED_BYTE, lightmaps + i * BLOCK_WIDTH * BLOCK_HEIGHT * lightmap_bytes);
    }

    if (!gl_texsort.value)
    {
        GL_SelectTexture(TEXTURE0_SGIS);
    }
}
