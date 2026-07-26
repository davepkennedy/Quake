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

entity_t	r_worldentity;

qboolean	r_cache_thrash;		// compatability

vec3_t		modelorg, r_entorigin;
entity_t	*currententity;

int			r_visframecount;	// bumped when going to a new PVS
int			r_framecount;		// used for dlight push checking

mplane_t	frustum[4];

int			c_brush_polys, c_alias_polys;

qboolean	envmap;				// true during envmap command capture 

int			currenttexture = -1;		// to avoid unnecessary texture sets

int			cnttextures[2] = {-1, -1};     // cached

int			particletexture;	// little dot for particles
int			playertextures;		// up to 16 color translated skins

int			mirrortexturenum;	// quake texturenum, not gltexturenum
qboolean	mirror;
mplane_t	*mirror_plane;

//
// view origin
//
vec3_t	vup;
vec3_t	vpn;
vec3_t	vright;
vec3_t	r_origin;

glm::mat4	r_world_matrix;
glm::mat4	r_base_world_matrix;
glm::mat4	r_proj_matrix;
glm::mat4	r_entity_matrix(1.0f);

//
// screen size info
//
refdef_t	r_refdef;

mleaf_t		*r_viewleaf, *r_oldviewleaf;

texture_t	*r_notexture_mip;

int		d_lightstylevalue[256];	// 8.8 fraction of base light value


void R_MarkLeaves (void);

cvar_t	r_norefresh = {"r_norefresh","0"};
cvar_t	r_drawentities = {"r_drawentities","1"};
cvar_t	r_drawviewmodel = {"r_drawviewmodel","1"};
cvar_t	r_speeds = {"r_speeds","0"};
cvar_t	r_fullbright = {"r_fullbright","0"};
cvar_t	r_lightmap = {"r_lightmap","0"};
cvar_t	r_shadows = {"r_shadows","0"};
cvar_t	r_mirroralpha = {"r_mirroralpha","1"};
cvar_t	r_wateralpha = {"r_wateralpha","1"};
cvar_t	r_dynamic = {"r_dynamic","1"};
cvar_t	r_novis = {"r_novis","0"};

cvar_t	gl_finish = {"gl_finish","0"};
cvar_t	gl_clear = {"gl_clear","0"};
cvar_t	gl_cull = {"gl_cull","1"};
cvar_t	gl_texsort = {"gl_texsort","1"};
cvar_t	gl_smoothmodels = {"gl_smoothmodels","1"};
cvar_t	gl_affinemodels = {"gl_affinemodels","0"};
cvar_t	gl_polyblend = {"gl_polyblend","1"};
cvar_t	gl_flashblend = {"gl_flashblend","1"};
cvar_t	gl_playermip = {"gl_playermip","0"};
cvar_t	gl_nocolors = {"gl_nocolors","0"};
cvar_t	gl_keeptjunctions = {"gl_keeptjunctions","0"};
cvar_t	gl_reporttjunctions = {"gl_reporttjunctions","0"};
cvar_t	gl_doubleeyes = {"gl_doubleeys", "1"};

extern	cvar_t	gl_ztrick;

/*
=================
R_CullBox

Returns true if the box is completely outside the frustom
=================
*/
qboolean R_CullBox (vec3_t mins, vec3_t maxs)
{
	int		i;

	for (i=0 ; i<4 ; i++)
		if (BoxOnPlaneSide (mins, maxs, &frustum[i]) == 2)
			return true;
	return false;
}


void R_RotateForEntity (entity_t *e)
{
	r_entity_matrix = glm::translate (glm::mat4(1.0f), glm::vec3(e->origin[0], e->origin[1], e->origin[2]));

	r_entity_matrix = glm::rotate (r_entity_matrix, glm::radians(e->angles[1]), glm::vec3(0,0,1));
	r_entity_matrix = glm::rotate (r_entity_matrix, glm::radians(-e->angles[0]), glm::vec3(0,1,0));
	r_entity_matrix = glm::rotate (r_entity_matrix, glm::radians(e->angles[2]), glm::vec3(1,0,0));
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
mspriteframe_t *R_GetSpriteFrame (entity_t *currententity)
{
	msprite_t		*psprite;
	mspritegroup_t	*pspritegroup;
	mspriteframe_t	*pspriteframe;
	int				i, numframes, frame;
	float			*pintervals, fullinterval, targettime, time;

	psprite = (msprite_t *)currententity->model->cache.data;
	frame = currententity->frame;

	if ((frame >= psprite->numframes) || (frame < 0))
	{
		Con_Printf ("R_DrawSprite: no such frame %d\n", frame);
		frame = 0;
	}

	if (psprite->frames[frame].type == SPR_SINGLE)
	{
		pspriteframe = psprite->frames[frame].frameptr;
	}
	else
	{
		pspritegroup = (mspritegroup_t *)psprite->frames[frame].frameptr;
		pintervals = pspritegroup->intervals;
		numframes = pspritegroup->numframes;
		fullinterval = pintervals[numframes-1];

		time = CL_Time() + currententity->syncbase;

	// when loading in Mod_LoadSpriteGroup, we guaranteed all interval values
	// are positive, so we don't have to worry about division by 0
		targettime = time - ((int)(time / fullinterval)) * fullinterval;

		for (i=0 ; i<(numframes-1) ; i++)
		{
			if (pintervals[i] > targettime)
				break;
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
static GLBuffer      billboard_vbo;
static GLProgram     billboard_prog;
static GLint  u_billboard_mvp   = -1;
static GLint  u_billboard_tex   = -1;
static GLint  u_billboard_color = -1;
static GLint  u_billboard_flat  = -1;

static const char billboard_vert_src[] =
    "#version 450 core\n"
    "layout(location = 0) in vec3 a_pos;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "uniform mat4 u_mvp;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = a_uv;\n"
    "    gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
    "}\n";

static const char billboard_frag_src[] =
    "#version 450 core\n"
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

static void Billboard_InitRenderer (void)
{
	if (billboard_prog)
		return;

	billboard_prog = GL_BuildProgram (billboard_vert_src, billboard_frag_src);
	if (!billboard_prog)
		Sys_Error ("Billboard_InitRenderer: shader compile failed");

	qglUseProgram (billboard_prog);
	u_billboard_mvp   = qglGetUniformLocation (billboard_prog, "u_mvp");
	u_billboard_tex   = qglGetUniformLocation (billboard_prog, "u_tex");
	u_billboard_color = qglGetUniformLocation (billboard_prog, "u_color");
	u_billboard_flat  = qglGetUniformLocation (billboard_prog, "u_flat");
	qglUseProgram (0);

	billboard_vao = GLVertexArray::Create ();
	billboard_vbo = GLBuffer::Create ();
	qglBindVertexArray (billboard_vao);
	qglBindBuffer (GL_ARRAY_BUFFER, billboard_vbo);
	qglBufferData (GL_ARRAY_BUFFER, 6 * 5 * sizeof(float), nullptr, GL_STREAM_DRAW);
	// location 0: xyz  (3 floats, offset 0, stride 5*4=20)
	qglVertexAttribPointer (0, 3, GL_FLOAT, GL_FALSE, 5*sizeof(float), (void*)0);
	qglEnableVertexAttribArray (0);
	// location 1: uv  (2 floats, offset 12)
	qglVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, 5*sizeof(float), (void*)(3*sizeof(float)));
	qglEnableVertexAttribArray (1);
	qglBindVertexArray (0);
	qglBindBuffer (GL_ARRAY_BUFFER, 0);
}

static void Billboard_BeginDraw (void)
{
	Billboard_InitRenderer ();
	qglUseProgram (billboard_prog);
	qglBindVertexArray (billboard_vao);
	qglBindBuffer (GL_ARRAY_BUFFER, billboard_vbo);
	qglUniform1i (u_billboard_tex, 0);
}

static void Billboard_SetMVP (void)
{
	float mvp[16];
	GL_GetMVP (mvp);
	qglUniformMatrix4fv (u_billboard_mvp, 1, GL_FALSE, mvp);
}

static void Billboard_EndDraw (void)
{
	qglBindVertexArray (0);
	qglUseProgram (0);
}

// quad[4] holds the corners in original GL_QUADS order (pos3+uv2 each);
// split into two triangles the same way GL_QUADS was always filled.
static void Billboard_DrawQuad (const float quad[4][5])
{
	float tris[6][5];
	memcpy (tris[0], quad[0], sizeof(float)*5);
	memcpy (tris[1], quad[1], sizeof(float)*5);
	memcpy (tris[2], quad[2], sizeof(float)*5);
	memcpy (tris[3], quad[0], sizeof(float)*5);
	memcpy (tris[4], quad[2], sizeof(float)*5);
	memcpy (tris[5], quad[3], sizeof(float)*5);

	qglBufferData (GL_ARRAY_BUFFER, sizeof(tris), tris, GL_STREAM_DRAW);
	glDrawArrays (GL_TRIANGLES, 0, 6);
}

/*
=================
R_DrawSpriteModel

=================
*/
void R_DrawSpriteModel (entity_t *e)
{
	vec3_t	point;
	mspriteframe_t	*frame;
	float		*up, *right;
	vec3_t		v_forward, v_right, v_up;
	msprite_t		*psprite;
	float		quad[4][5];

	// don't even bother culling, because it's just a single
	// polygon without a surface cache
	frame = R_GetSpriteFrame (e);
	psprite = (msprite_t *)currententity->model->cache.data;

	if (psprite->type == SPR_ORIENTED)
	{	// bullet marks on walls
		AngleVectors (currententity->angles, v_forward, v_right, v_up);
		up = v_up;
		right = v_right;
	}
	else
	{	// normal sprite
		up = vup;
		right = vright;
	}

	GL_DisableMultitexture();

    GL_Bind(frame->gl_texturenum);

	VectorMA (e->origin, frame->down, up, point);
	VectorMA (point, frame->left, right, point);
	quad[0][0] = point[0]; quad[0][1] = point[1]; quad[0][2] = point[2]; quad[0][3] = 0; quad[0][4] = 1;

	VectorMA (e->origin, frame->up, up, point);
	VectorMA (point, frame->left, right, point);
	quad[1][0] = point[0]; quad[1][1] = point[1]; quad[1][2] = point[2]; quad[1][3] = 0; quad[1][4] = 0;

	VectorMA (e->origin, frame->up, up, point);
	VectorMA (point, frame->right, right, point);
	quad[2][0] = point[0]; quad[2][1] = point[1]; quad[2][2] = point[2]; quad[2][3] = 1; quad[2][4] = 0;

	VectorMA (e->origin, frame->down, up, point);
	VectorMA (point, frame->right, right, point);
	quad[3][0] = point[0]; quad[3][1] = point[1]; quad[3][2] = point[2]; quad[3][3] = 1; quad[3][4] = 1;

	Billboard_BeginDraw ();
	Billboard_SetMVP ();
	qglUniform1i (u_billboard_flat, 0);
	Billboard_DrawQuad (quad);
	Billboard_EndDraw ();
}

/*
=============================================================

  ALIAS MODELS

=============================================================
*/


#define NUMVERTEXNORMALS	162

float	r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};

vec3_t	shadevector;
float	shadelight, ambientlight;

// precalculated dot products for quantized angles
#define SHADEDOT_QUANT 16
float	r_avertexnormal_dots[SHADEDOT_QUANT][256] =
#include "anorm_dots.h"
;

float	*shadedots = r_avertexnormal_dots[0];

int	lastposenum;

// -------------------------------------------------------------------------
// Alias model renderer state (VAO / VBO / GLSL shader)
//
// Shared by the skin-textured model draw (GL_DrawAliasFrame) and the flat
// blob-shadow draw (GL_DrawAliasShadow): both walk the same precomputed
// triangle-strip/fan "command" stream from gl_mesh.cpp (count, then that
// many (u,v) pairs interleaved with the pose's trivertx_t stream), just
// with different per-vertex data and, for shadows, a flat uniform color
// instead of the sampled+shaded skin texture.
// -------------------------------------------------------------------------

static GLVertexArray alias_vao;
static GLBuffer      alias_vbo;
static GLProgram     alias_prog;
static GLint  u_alias_mvp   = -1;
static GLint  u_alias_tex   = -1;
static GLint  u_alias_color = -1;
static GLint  u_alias_flat  = -1;

// gl_mesh.cpp's StripLength/FanLength cap any single command at 128 verts
// (fixed-size stripverts[128]/striptris[128]); 256 leaves headroom.
#define ALIAS_MAX_CMD_VERTS 256
#define ALIAS_STREAM_VERTS  ((ALIAS_MAX_CMD_VERTS - 2) * 3)
static float alias_stream[ALIAS_STREAM_VERTS * 6];  // pos3 + uv2 + intensity1

static const char alias_vert_src[] =
    "#version 450 core\n"
    "layout(location = 0) in vec3 a_pos;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "layout(location = 2) in float a_intensity;\n"
    "uniform mat4 u_mvp;\n"
    "out vec2 v_uv;\n"
    "out float v_intensity;\n"
    "void main() {\n"
    "    v_uv = a_uv;\n"
    "    v_intensity = a_intensity;\n"
    "    gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
    "}\n";

static const char alias_frag_src[] =
    "#version 450 core\n"
    "in vec2 v_uv;\n"
    "in float v_intensity;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec4 u_color;\n"
    "uniform int u_flat;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    if (u_flat != 0)\n"
    "        frag_color = u_color;\n"
    "    else\n"
    "        frag_color = vec4(texture(u_tex, v_uv).rgb * v_intensity, 1.0);\n"
    "}\n";

static void Alias_InitRenderer (void)
{
	if (alias_prog)
		return;

	alias_prog = GL_BuildProgram (alias_vert_src, alias_frag_src);
	if (!alias_prog)
		Sys_Error ("Alias_InitRenderer: shader compile failed");

	qglUseProgram (alias_prog);
	u_alias_mvp   = qglGetUniformLocation (alias_prog, "u_mvp");
	u_alias_tex   = qglGetUniformLocation (alias_prog, "u_tex");
	u_alias_color = qglGetUniformLocation (alias_prog, "u_color");
	u_alias_flat  = qglGetUniformLocation (alias_prog, "u_flat");
	qglUseProgram (0);

	alias_vao = GLVertexArray::Create ();
	alias_vbo = GLBuffer::Create ();
	qglBindVertexArray (alias_vao);
	qglBindBuffer (GL_ARRAY_BUFFER, alias_vbo);
	qglBufferData (GL_ARRAY_BUFFER, sizeof(alias_stream), nullptr, GL_STREAM_DRAW);
	// location 0: xyz  (3 floats, offset 0, stride 6*4=24)
	qglVertexAttribPointer (0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
	qglEnableVertexAttribArray (0);
	// location 1: uv  (2 floats, offset 12)
	qglVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
	qglEnableVertexAttribArray (1);
	// location 2: intensity  (1 float, offset 20)
	qglVertexAttribPointer (2, 1, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(5*sizeof(float)));
	qglEnableVertexAttribArray (2);
	qglBindVertexArray (0);
	qglBindBuffer (GL_ARRAY_BUFFER, 0);
}

// Explicit teardown, called from Host_Shutdown before VID_Shutdown() destroys
// the GL context -- see gl_shader.h's comment on why this can't be left to
// these globals' own (static-duration) destructors. Covers both the
// billboard and alias-model renderers, this file's two trios.
void GL_RMain_Shutdown (void)
{
	billboard_vao.Release ();
	billboard_vbo.Release ();
	billboard_prog.Release ();

	alias_vao.Release ();
	alias_vbo.Release ();
	alias_prog.Release ();
}

static void Alias_BeginDraw (void)
{
	Alias_InitRenderer ();
	GL_DisableMultitexture ();
	qglUseProgram (alias_prog);
	qglBindVertexArray (alias_vao);
	qglBindBuffer (GL_ARRAY_BUFFER, alias_vbo);
	qglUniform1i (u_alias_tex, 0);
}

static void Alias_SetMVP (void)
{
	float mvp[16];
	GL_GetMVP (mvp);
	qglUniformMatrix4fv (u_alias_mvp, 1, GL_FALSE, mvp);
}

static void Alias_EndDraw (void)
{
	qglBindVertexArray (0);
	qglUseProgram (0);
}

// Triangulates one command's worth of vertices (a GL_TRIANGLE_FAN or
// GL_TRIANGLE_STRIP, per the original fixed-function primitive type) into
// the stream buffer and draws it. cmdverts holds n vertices of
// (x,y,z, u,v, intensity). Mirrors the standard OpenGL strip winding rule
// (alternating vertex order every other triangle) since we no longer have
// glBegin(GL_TRIANGLE_STRIP) doing that for us.
static void Alias_EmitPrimitive (const float cmdverts[][6], int n, qboolean fan)
{
	if (n < 3)
		return;

	int ntri = n - 2;
	float *out = alias_stream;

	for (int i = 0; i < ntri; i++)
	{
		int i0, i1, i2;
		if (fan)
		{
			i0 = 0; i1 = i+1; i2 = i+2;
		}
		else if (i & 1)
		{
			i0 = i+1; i1 = i; i2 = i+2;
		}
		else
		{
			i0 = i; i1 = i+1; i2 = i+2;
		}

		memcpy (out, cmdverts[i0], 6*sizeof(float)); out += 6;
		memcpy (out, cmdverts[i1], 6*sizeof(float)); out += 6;
		memcpy (out, cmdverts[i2], 6*sizeof(float)); out += 6;
	}

	int nverts = ntri * 3;
	qglBufferData (GL_ARRAY_BUFFER,
	    (GLsizeiptr)(nverts * 6 * sizeof(float)),
	    alias_stream, GL_STREAM_DRAW);
	glDrawArrays (GL_TRIANGLES, 0, nverts);
}

/*
=============
GL_DrawAliasFrame
=============
*/
void GL_DrawAliasFrame (aliashdr_t *paliashdr, int posenum)
{
	trivertx_t	*verts;
	int		*order;
	int		count;
	float		cmdverts[ALIAS_MAX_CMD_VERTS][6];

lastposenum = posenum;

	verts = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
	verts += posenum * paliashdr->poseverts;
	order = (int *)((byte *)paliashdr + paliashdr->commands);

	Alias_BeginDraw ();
	Alias_SetMVP ();
	qglUniform1i (u_alias_flat, 0);

	while (1)
	{
		// get the vertex count and primitive type
		count = *order++;
		if (!count)
			break;		// done
		qboolean fan = count < 0;
		if (fan)
			count = -count;

		int n = 0;
		do
		{
			if (n < ALIAS_MAX_CMD_VERTS)
			{
				// texture coordinates come from the draw list
				cmdverts[n][3] = ((float *)order)[0];
				cmdverts[n][4] = ((float *)order)[1];

				// normals and vertexes come from the frame list
				cmdverts[n][0] = verts->v[0];
				cmdverts[n][1] = verts->v[1];
				cmdverts[n][2] = verts->v[2];
				cmdverts[n][5] = shadedots[verts->lightnormalindex] * shadelight;
				n++;
			}
			order += 2;
			verts++;
		} while (--count);

		Alias_EmitPrimitive (cmdverts, n, fan);
	}

	Alias_EndDraw ();
}


/*
=============
GL_DrawAliasShadow
=============
*/
extern	vec3_t			lightspot;

void GL_DrawAliasShadow (aliashdr_t *paliashdr, int posenum)
{
	trivertx_t	*verts;
	int		*order;
	float		height, lheight;
	int		count;
	float		cmdverts[ALIAS_MAX_CMD_VERTS][6];

	lheight = currententity->origin[2] - lightspot[2];

	verts = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
	verts += posenum * paliashdr->poseverts;
	order = (int *)((byte *)paliashdr + paliashdr->commands);

	height = -lheight + 1.0;

	Alias_BeginDraw ();
	Alias_SetMVP ();
	qglUniform1i (u_alias_flat, 1);
	{
		const float shadow_color[4] = {0.f, 0.f, 0.f, 0.5f};
		qglUniform4fv (u_alias_color, 1, shadow_color);
	}

	while (1)
	{
		// get the vertex count and primitive type
		count = *order++;
		if (!count)
			break;		// done
		qboolean fan = count < 0;
		if (fan)
			count = -count;

		int n = 0;
		do
		{
			// texture coordinates come from the draw list
			// (skipped for shadows)
			order += 2;

			if (n < ALIAS_MAX_CMD_VERTS)
			{
				vec3_t point;
				point[0] = verts->v[0] * paliashdr->scale[0] + paliashdr->scale_origin[0];
				point[1] = verts->v[1] * paliashdr->scale[1] + paliashdr->scale_origin[1];
				point[2] = verts->v[2] * paliashdr->scale[2] + paliashdr->scale_origin[2];

				point[0] -= shadevector[0]*(point[2]+lheight);
				point[1] -= shadevector[1]*(point[2]+lheight);
				point[2] = height;

				cmdverts[n][0] = point[0];
				cmdverts[n][1] = point[1];
				cmdverts[n][2] = point[2];
				cmdverts[n][3] = 0.f;
				cmdverts[n][4] = 0.f;
				cmdverts[n][5] = 0.f;
				n++;
			}

			verts++;
		} while (--count);

		Alias_EmitPrimitive (cmdverts, n, fan);
	}

	Alias_EndDraw ();
}



/*
=================
R_SetupAliasFrame

=================
*/
void R_SetupAliasFrame (int frame, aliashdr_t *paliashdr)
{
	int				pose, numposes;
	float			interval;

	if ((frame >= paliashdr->numframes) || (frame < 0))
	{
		Con_DPrintf ("R_AliasSetupFrame: no such frame %d\n", frame);
		frame = 0;
	}

	pose = paliashdr->frames[frame].firstpose;
	numposes = paliashdr->frames[frame].numposes;

	if (numposes > 1)
	{
		interval = paliashdr->frames[frame].interval;
		pose += (int)(CL_Time() / interval) % numposes;
	}

	GL_DrawAliasFrame (paliashdr, pose);
}



/*
=================
R_DrawAliasModel

=================
*/
void R_DrawAliasModel (entity_t *e)
{
	int			i;
	int			lnum;
	vec3_t		dist;
	float		add;
	model_t		*clmodel;
	vec3_t		mins, maxs;
	aliashdr_t	*paliashdr;
	float		an;
	int			anim;

	clmodel = currententity->model;

	VectorAdd (currententity->origin, clmodel->mins, mins);
	VectorAdd (currententity->origin, clmodel->maxs, maxs);

	if (R_CullBox (mins, maxs))
		return;


	VectorCopy (currententity->origin, r_entorigin);
	VectorSubtract (r_origin, r_entorigin, modelorg);

	//
	// get lighting information
	//

	ambientlight = shadelight = R_LightPoint (currententity->origin);

	// allways give the gun some light
	if (e == CL_ViewEnt() && ambientlight < 24)
		ambientlight = shadelight = 24;

	for (lnum=0 ; lnum<MAX_DLIGHTS ; lnum++)
	{
		if (cl_dlights[lnum].die >= CL_Time())
		{
			VectorSubtract (currententity->origin,
							cl_dlights[lnum].origin,
							dist);
			add = cl_dlights[lnum].radius - Length(dist);

			if (add > 0) {
				ambientlight += add;
				//ZOID models should be affected by dlights as well
				shadelight += add;
			}
		}
	}

	// clamp lighting so it doesn't overbright as much
	if (ambientlight > 128)
		ambientlight = 128;
	if (ambientlight + shadelight > 192)
		shadelight = 192 - ambientlight;

	// ZOID: never allow players to go totally black
	i = currententity - cl_entities;
	if (i >= 1 && i<=CL_MaxClients() /* && !strcmp (currententity->model->name, "progs/player.mdl") */)
		if (ambientlight < 8)
			ambientlight = shadelight = 8;

	// HACK HACK HACK -- no fullbright colors, so make torches full light
	if (!strcmp (clmodel->name, "progs/flame2.mdl")
		|| !strcmp (clmodel->name, "progs/flame.mdl") )
		ambientlight = shadelight = 256;

	shadedots = r_avertexnormal_dots[((int)(e->angles[1] * (SHADEDOT_QUANT / 360.0))) & (SHADEDOT_QUANT - 1)];
	shadelight = shadelight / 200.0;
	
	an = e->angles[1]/180*M_PI;
	shadevector[0] = cos(-an);
	shadevector[1] = sin(-an);
	shadevector[2] = 1;
	VectorNormalize (shadevector);

	//
	// locate the proper data
	//
	paliashdr = (aliashdr_t *)Mod_Extradata (currententity->model);

	c_alias_polys += paliashdr->numtris;

	//
	// draw all the triangles
	//

	GL_DisableMultitexture();

	R_RotateForEntity (e);

	if (!strcmp (clmodel->name, "progs/eyes.mdl") && gl_doubleeyes.value) {
		r_entity_matrix = glm::translate (r_entity_matrix, glm::vec3(paliashdr->scale_origin[0], paliashdr->scale_origin[1], paliashdr->scale_origin[2] - (22 + 8)));
// double size of eyes, since they are really hard to see in gl
		r_entity_matrix = glm::scale (r_entity_matrix, glm::vec3(paliashdr->scale[0]*2, paliashdr->scale[1]*2, paliashdr->scale[2]*2));
	} else {
		r_entity_matrix = glm::translate (r_entity_matrix, glm::vec3(paliashdr->scale_origin[0], paliashdr->scale_origin[1], paliashdr->scale_origin[2]));
		r_entity_matrix = glm::scale (r_entity_matrix, glm::vec3(paliashdr->scale[0], paliashdr->scale[1], paliashdr->scale[2]));
	}

	anim = (int)(CL_Time()*10) & 3;
    GL_Bind(paliashdr->gl_texturenum[currententity->skinnum][anim]);

	// we can't dynamically colormap textures, so they are cached
	// seperately for the players.  Heads are just uncolored.
	if (currententity->colormap != vid.colormap && !gl_nocolors.value)
	{
		i = currententity - cl_entities;
		if (i >= 1 && i<=CL_MaxClients() /* && !strcmp (currententity->model->name, "progs/player.mdl") */)
		    GL_Bind(playertextures - 1 + i);
	}

	R_SetupAliasFrame (currententity->frame, paliashdr);

	r_entity_matrix = glm::mat4(1.0f);

	if (r_shadows.value)
	{
		R_RotateForEntity (e);
		glEnable (GL_BLEND);
		GL_DrawAliasShadow (paliashdr, lastposenum);
		glDisable (GL_BLEND);
		r_entity_matrix = glm::mat4(1.0f);
	}

}

//==================================================================================

/*
=============
R_DrawEntitiesOnList
=============
*/
void R_DrawEntitiesOnList (void)
{
	int		i;

	if (!r_drawentities.value)
		return;

	// draw sprites seperately, because of alpha blending
	for (i=0 ; i<cl_numvisedicts ; i++)
	{
		currententity = cl_visedicts[i];

		switch (currententity->model->type)
		{
		case modtype_t::mod_alias:
			R_DrawAliasModel (currententity);
			break;

		case modtype_t::mod_brush:
			R_DrawBrushModel (currententity);
			break;

		default:
			break;
		}
	}

	for (i=0 ; i<cl_numvisedicts ; i++)
	{
		currententity = cl_visedicts[i];

		switch (currententity->model->type)
		{
		case modtype_t::mod_sprite:
			R_DrawSpriteModel (currententity);
			break;
		}
	}
}

/*
=============
R_DrawViewModel
=============
*/
void R_DrawViewModel (void)
{
	float		ambient[4], diffuse[4];
	int			j;
	int			lnum;
	vec3_t		dist;
	float		add;
	dlight_t	*dl;
	int			ambientlight, shadelight;

	if (!r_drawviewmodel.value)
		return;

	if (chase_active.value)
		return;

	if (envmap)
		return;

	if (!r_drawentities.value)
		return;

	if (CL_Items() & IT_INVISIBILITY)
		return;

	if (CL_Stat(STAT_HEALTH) <= 0)
		return;

	currententity = CL_ViewEnt();
	if (!currententity->model)
		return;

	j = R_LightPoint (currententity->origin);

	if (j < 24)
		j = 24;		// allways give some light on gun
	ambientlight = j;
	shadelight = j;

// add dynamic lights		
	for (lnum=0 ; lnum<MAX_DLIGHTS ; lnum++)
	{
		dl = &cl_dlights[lnum];
		if (!dl->radius)
			continue;
		if (!dl->radius)
			continue;
		if (dl->die < CL_Time())
			continue;

		VectorSubtract (currententity->origin, dl->origin, dist);
		add = dl->radius - Length(dist);
		if (add > 0)
			ambientlight += add;
	}

	ambient[0] = ambient[1] = ambient[2] = ambient[3] = (float)ambientlight / 128;
	diffuse[0] = diffuse[1] = diffuse[2] = diffuse[3] = (float)shadelight / 128;

	// hack the depth range to prevent view model from poking into walls
	glDepthRange (gldepthmin, gldepthmin + 0.3*(gldepthmax-gldepthmin));
	R_DrawAliasModel (currententity);
	glDepthRange (gldepthmin, gldepthmax);
}


/*
============
R_PolyBlend
============
*/
void R_PolyBlend (void)
{
	if (!gl_polyblend.value)
		return;
	if (!v_blend[3])
		return;

	GL_DisableMultitexture();

	glEnable (GL_BLEND);
	glDisable (GL_DEPTH_TEST);

	{
		// Fixed "Z going up" view, independent of the player's actual
		// view angles -- this is a full-screen tint quad, not a world object.
		glm::mat4 view(1.0f);
		view = glm::rotate (view, glm::radians(-90.0f), glm::vec3(1,0,0));
		view = glm::rotate (view, glm::radians(90.0f), glm::vec3(0,0,1));
		glm::mat4 mvp = r_proj_matrix * view;

		float quad[4][5] = {
			{ 10,  100,  100, 0, 0 },
			{ 10, -100,  100, 0, 0 },
			{ 10, -100, -100, 0, 0 },
			{ 10,  100, -100, 0, 0 },
		};

		Billboard_BeginDraw ();
		qglUniformMatrix4fv (u_billboard_mvp, 1, GL_FALSE, glm::value_ptr(mvp));
		qglUniform1i (u_billboard_flat, 1);
		qglUniform4fv (u_billboard_color, 1, v_blend);
		Billboard_DrawQuad (quad);
		Billboard_EndDraw ();
	}

	glDisable (GL_BLEND);
}


int SignbitsForPlane (mplane_t *out)
{
	int	bits, j;

	// for fast box on planeside test

	bits = 0;
	for (j=0 ; j<3 ; j++)
	{
		if (out->normal[j] < 0)
			bits |= 1<<j;
	}
	return bits;
}


void R_SetFrustum (void)
{
	int		i;

	if (r_refdef.fov_x == 90) 
	{
		// front side is visible

		VectorAdd (vpn, vright, frustum[0].normal);
		VectorSubtract (vpn, vright, frustum[1].normal);

		VectorAdd (vpn, vup, frustum[2].normal);
		VectorSubtract (vpn, vup, frustum[3].normal);
	}
	else
	{
		// rotate VPN right by FOV_X/2 degrees
		RotatePointAroundVector( frustum[0].normal, vup, vpn, -(90-r_refdef.fov_x / 2 ) );
		// rotate VPN left by FOV_X/2 degrees
		RotatePointAroundVector( frustum[1].normal, vup, vpn, 90-r_refdef.fov_x / 2 );
		// rotate VPN up by FOV_X/2 degrees
		RotatePointAroundVector( frustum[2].normal, vright, vpn, 90-r_refdef.fov_y / 2 );
		// rotate VPN down by FOV_X/2 degrees
		RotatePointAroundVector( frustum[3].normal, vright, vpn, -( 90 - r_refdef.fov_y / 2 ) );
	}

	for (i=0 ; i<4 ; i++)
	{
		frustum[i].type = PLANE_ANYZ;
		frustum[i].dist = DotProduct (r_origin, frustum[i].normal);
		frustum[i].signbits = SignbitsForPlane (&frustum[i]);
	}
}



/*
===============
R_SetupFrame
===============
*/
void R_SetupFrame (void)
{
// don't allow cheats in multiplayer
	if (CL_MaxClients() > 1)
		Cvar_Set ("r_fullbright", "0");

	R_AnimateLight ();

	r_framecount++;

// build the transformation matrix for the given view angles
	VectorCopy (r_refdef.vieworg, r_origin);

	AngleVectors (r_refdef.viewangles, vpn, vright, vup);

// current viewleaf
	r_oldviewleaf = r_viewleaf;
	r_viewleaf = Mod_PointInLeaf (r_origin, CL_WorldModel());

	V_SetContentsColor (r_viewleaf->contents);
	V_CalcBlend ();

	r_cache_thrash = false;

	c_brush_polys = 0;
	c_alias_polys = 0;

}


void MYgluPerspective( GLdouble fovy, GLdouble aspect,
		     GLdouble zNear, GLdouble zFar )
{
   GLdouble xmin, xmax, ymin, ymax;

   ymax = zNear * tan( fovy * M_PI / 360.0 );
   ymin = -ymax;

   xmin = ymin * aspect;
   xmax = ymax * aspect;

   r_proj_matrix = glm::frustum( (float)xmin, (float)xmax, (float)ymin, (float)ymax, (float)zNear, (float)zFar );
}


/*
=============
R_SetupGL
=============
*/
void R_SetupGL (void)
{
	float	screenaspect;
	extern	int glwidth, glheight;
	int		x, x2, y2, y, w, h;

	//
	// set up viewpoint
	//
	x = r_refdef.vrect.x * glwidth/vid.width;
	x2 = (r_refdef.vrect.x + r_refdef.vrect.width) * glwidth/vid.width;
	y = (vid.height-r_refdef.vrect.y) * glheight/vid.height;
	y2 = (vid.height - (r_refdef.vrect.y + r_refdef.vrect.height)) * glheight/vid.height;

	// fudge around because of frac screen scale
	if (x > 0)
		x--;
	if (x2 < glwidth)
		x2++;
	if (y2 < 0)
		y2--;
	if (y < glheight)
		y++;

	w = x2 - x;
	h = y - y2;

	if (envmap)
	{
		x = y2 = 0;
		w = h = 256;
	}

	glViewport (glx + x, gly + y2, w, h);
    screenaspect = (float)r_refdef.vrect.width/r_refdef.vrect.height;
//	yfov = 2*atan((float)r_refdef.vrect.height/r_refdef.vrect.width)*180/M_PI;
    MYgluPerspective (r_refdef.fov_y,  screenaspect,  4,  4096);

	if (mirror)
	{
		if (mirror_plane->normal[2])
			r_proj_matrix = glm::scale (r_proj_matrix, glm::vec3(1, -1, 1));
		else
			r_proj_matrix = glm::scale (r_proj_matrix, glm::vec3(-1, 1, 1));
		glCullFace(GL_BACK);
	}
	else
		glCullFace(GL_FRONT);

	r_world_matrix = glm::mat4(1.0f);

	r_world_matrix = glm::rotate (r_world_matrix, glm::radians(-90.0f), glm::vec3(1,0,0));	    // put Z going up
	r_world_matrix = glm::rotate (r_world_matrix, glm::radians(90.0f), glm::vec3(0,0,1));	    // put Z going up
	r_world_matrix = glm::rotate (r_world_matrix, glm::radians(-r_refdef.viewangles[2]), glm::vec3(1,0,0));
	r_world_matrix = glm::rotate (r_world_matrix, glm::radians(-r_refdef.viewangles[0]), glm::vec3(0,1,0));
	r_world_matrix = glm::rotate (r_world_matrix, glm::radians(-r_refdef.viewangles[1]), glm::vec3(0,0,1));
	r_world_matrix = glm::translate (r_world_matrix, glm::vec3(-r_refdef.vieworg[0], -r_refdef.vieworg[1], -r_refdef.vieworg[2]));

	r_entity_matrix = glm::mat4(1.0f);

	//
	// set drawing parms
	//
	if (gl_cull.value)
		glEnable(GL_CULL_FACE);
	else
		glDisable(GL_CULL_FACE);

	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
}

/*
================
R_RenderScene

r_refdef must be set before the first call
================
*/
void R_RenderScene (void)
{
	R_SetupFrame ();

	R_SetFrustum ();

	R_SetupGL ();

	R_MarkLeaves ();	// done here so we know if we're in water

	R_DrawWorld ();		// adds static entities to the list

	S_ExtraUpdate ();	// don't let sound get messed up if going slow

	R_DrawEntitiesOnList ();

	GL_DisableMultitexture();

	R_RenderDlights ();

	R_DrawParticles ();

#ifdef GLTEST
	Test_Draw ();
#endif

}


/*
=============
R_Clear
=============
*/
void R_Clear (void)
{
	if (r_mirroralpha.value != 1.0)
	{
		if (gl_clear.value)
			glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		else
			glClear (GL_DEPTH_BUFFER_BIT);
		gldepthmin = 0;
		gldepthmax = 0.5;
		glDepthFunc (GL_LEQUAL);
	}
	else if (gl_ztrick.value)
	{
		static int trickframe;

		if (gl_clear.value)
			glClear (GL_COLOR_BUFFER_BIT);

		trickframe++;
		if (trickframe & 1)
		{
			gldepthmin = 0;
			gldepthmax = 0.49999f;
			glDepthFunc (GL_LEQUAL);
		}
		else
		{
			gldepthmin = 1;
			gldepthmax = 0.5;
			glDepthFunc (GL_GEQUAL);
		}
	}
	else
	{
		if (gl_clear.value)
			glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		else
			glClear (GL_DEPTH_BUFFER_BIT);
		gldepthmin = 0;
		gldepthmax = 1;
		glDepthFunc (GL_LEQUAL);
	}

	glDepthRange (gldepthmin, gldepthmax);
}

/*
=============
R_Mirror
=============
*/
void R_Mirror (void)
{
	float		d;
	msurface_t	*s;
	entity_t	*ent;

	if (!mirror)
		return;

	r_base_world_matrix = r_world_matrix;

	d = DotProduct (r_refdef.vieworg, mirror_plane->normal) - mirror_plane->dist;
	VectorMA (r_refdef.vieworg, -2*d, mirror_plane->normal, r_refdef.vieworg);

	d = DotProduct (vpn, mirror_plane->normal);
	VectorMA (vpn, -2*d, mirror_plane->normal, vpn);

	r_refdef.viewangles[0] = -asin (vpn[2])/M_PI*180;
	r_refdef.viewangles[1] = atan2 (vpn[1], vpn[0])/M_PI*180;
	r_refdef.viewangles[2] = -r_refdef.viewangles[2];

	ent = &cl_entities[CL_ViewEntity()];
	if (cl_numvisedicts < MAX_VISEDICTS)
	{
		cl_visedicts[cl_numvisedicts] = ent;
		cl_numvisedicts++;
	}

	gldepthmin = 0.5;
	gldepthmax = 1;
	glDepthRange (gldepthmin, gldepthmax);
	glDepthFunc (GL_LEQUAL);

	R_RenderScene ();
	R_DrawWaterSurfaces ();

	gldepthmin = 0;
	gldepthmax = 0.5;
	glDepthRange (gldepthmin, gldepthmax);
	glDepthFunc (GL_LEQUAL);

	// blend on top
	glEnable (GL_BLEND);
	// undoes the scale R_SetupGL applied for the recursive (reflected)
	// R_RenderScene() call above, since the mirror quad itself must be
	// drawn un-mirrored, from the original (pre-reflection) viewpoint.
	if (mirror_plane->normal[2])
		r_proj_matrix = glm::scale (r_proj_matrix, glm::vec3(1,-1,1));
	else
		r_proj_matrix = glm::scale (r_proj_matrix, glm::vec3(-1,1,1));
	glCullFace(GL_FRONT);

	r_world_matrix = r_base_world_matrix;

	R_World_SetAlpha (r_mirroralpha.value);
	s = CL_WorldModel()->textures[mirrortexturenum]->texturechain;
	for ( ; s ; s=s->texturechain)
		R_RenderBrushPoly (s);
	CL_WorldModel()->textures[mirrortexturenum]->texturechain = NULL;
	R_World_SetAlpha (1.0f);
	glDisable (GL_BLEND);
}

/*
================
R_RenderView

r_refdef must be set before the first call
================
*/
void R_RenderView (void)
{
	double	time1, time2;
	GLfloat colors[4] = {(GLfloat) 0.0, (GLfloat) 0.0, (GLfloat) 1, (GLfloat) 0.20};

	if (r_norefresh.value)
		return;

	if (!r_worldentity.model || !CL_WorldModel())
		Sys_Error ("R_RenderView: NULL worldmodel");

	if (r_speeds.value)
	{
		glFinish ();
		time1 = Sys_FloatTime ();
		c_brush_polys = 0;
		c_alias_polys = 0;
	}

	mirror = false;

	if (gl_finish.value)
		glFinish ();

	R_Clear ();

	// render normal view

/***** Experimental silly looking fog ******
****** Use r_fullbright if you enable ******
	glFogi(GL_FOG_MODE, GL_LINEAR);
	glFogfv(GL_FOG_COLOR, colors);
	glFogf(GL_FOG_END, 512.0);
	glEnable(GL_FOG);
********************************************/

	R_RenderScene ();
	R_DrawViewModel ();
	R_DrawWaterSurfaces ();

//  More fog right here :)
//	glDisable(GL_FOG);
//  End of all fog code...

	// render mirror view
	R_Mirror ();

	R_PolyBlend ();

	if (r_speeds.value)
	{
//		glFinish ();
		time2 = Sys_FloatTime ();
		Con_Printf ("%3i ms  %4i wpoly %4i epoly\n", (int)((time2-time1)*1000), c_brush_polys, c_alias_polys); 
	}
}
