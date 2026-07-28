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
// gl_warp.c -- sky and water polygons

#include "quakedef.h"

extern	model_t	*loadmodel;

extern int	skytexturenum;

int		solidskytexture;
int		alphaskytexture;
float	speedscale;		// for top sky and bottom sky

msurface_t	*warpface;

extern cvar_t gl_subdivide_size;

void BoundPoly (int numverts, float *verts, vec3_t mins, vec3_t maxs)
{
	int		i, j;
	float	*v;

	mins[0] = mins[1] = mins[2] = 9999;
	maxs[0] = maxs[1] = maxs[2] = -9999;
	v = verts;
	for (i=0 ; i<numverts ; i++)
		for (j=0 ; j<3 ; j++, v++)
		{
			if (*v < mins[j])
				mins[j] = *v;
			if (*v > maxs[j])
				maxs[j] = *v;
		}
}

void SubdividePolygon (int numverts, float *verts)
{
	int		i, j, k;
	vec3_t	mins, maxs;
	float	m;
	float	*v;
	vec3_t	front[64], back[64];
	int		f, b;
	float	dist[64];
	float	frac;
	glpoly_t	*poly;
	float	s, t;

	if (numverts > 60)
		Sys_Error ("numverts = %i", numverts);

	BoundPoly (numverts, verts, mins, maxs);

	for (i=0 ; i<3 ; i++)
	{
		m = (mins[i] + maxs[i]) * 0.5;
		m = gl_subdivide_size.value * floor (m/gl_subdivide_size.value + 0.5);
		if (maxs[i] - m < 8)
			continue;
		if (m - mins[i] < 8)
			continue;

		// cut it
		v = verts + i;
		for (j=0 ; j<numverts ; j++, v+= 3)
			dist[j] = *v - m;

		// wrap cases
		dist[j] = dist[0];
		v-=i;
		VectorCopy (verts, v);

		f = b = 0;
		v = verts;
		for (j=0 ; j<numverts ; j++, v+= 3)
		{
			if (dist[j] >= 0)
			{
				VectorCopy (v, front[f]);
				f++;
			}
			if (dist[j] <= 0)
			{
				VectorCopy (v, back[b]);
				b++;
			}
			if (dist[j] == 0 || dist[j+1] == 0)
				continue;
			if ( (dist[j] > 0) != (dist[j+1] > 0) )
			{
				// clip point
				frac = dist[j] / (dist[j] - dist[j+1]);
				for (k=0 ; k<3 ; k++)
					front[f][k] = back[b][k] = v[k] + frac*(v[3+k] - v[k]);
				f++;
				b++;
			}
		}

		SubdividePolygon (f, front[0]);
		SubdividePolygon (b, back[0]);
		return;
	}

	poly = (glpoly_t *)Hunk_Alloc (sizeof(glpoly_t) + (numverts-4) * VERTEXSIZE*sizeof(float));
	poly->next = warpface->polys;
	warpface->polys = poly;
	poly->numverts = numverts;
	for (i=0 ; i<numverts ; i++, verts+= 3)
	{
		VectorCopy (verts, poly->verts[i]);
		s = DotProduct (verts, warpface->texinfo->vecs[0]);
		t = DotProduct (verts, warpface->texinfo->vecs[1]);
		poly->verts[i][3] = s;
		poly->verts[i][4] = t;
	}
}

/*
================
GL_SubdivideSurface

Breaks a polygon up along axial 64 unit
boundaries so that turbulent and sky warps
can be done reasonably.
================
*/
void GL_SubdivideSurface (msurface_t *fa)
{
	vec3_t		verts[64];
	int			numverts;
	int			i;
	int			lindex;
	float		*vec;

	warpface = fa;

	//
	// convert edges back to a normal polygon
	//
	numverts = 0;
	for (i=0 ; i<fa->numedges ; i++)
	{
		lindex = loadmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
			vec = loadmodel->vertexes[loadmodel->edges[lindex].v[0]].position;
		else
			vec = loadmodel->vertexes[loadmodel->edges[-lindex].v[1]].position;
		VectorCopy (vec, verts[numverts]);
		numverts++;
	}

	SubdividePolygon (numverts, verts[0]);
}

//=========================================================



// speed up sin calculations - Ed
float	turbsin[] =
{
	#include "gl_warp_sin.h"
};
#define TURBSCALE (256.0 / (2 * M_PI))

// -------------------------------------------------------------------------
// Warp surface renderer state (VAO / VBO / GLSL shader)
//
// Shared by water/lava/slime (EmitWaterPolys) and sky (EmitSkyPolys and
// friends): both are unlit, single-texture, per-vertex-UV-warped triangle
// fans built from pre-subdivided glpoly_t chains. Position is passed through
// unmodified; only the texture coordinates vary per effect.
// -------------------------------------------------------------------------

static GLVertexArray warp_vao;
static GLBuffer      warp_vbo;
static GLProgram     warp_prog;
static GLint  u_warp_mvp   = -1;
static GLint  u_warp_tex   = -1;
static GLint  u_warp_alpha = -1;

#define WARP_STREAM_VERTS 4096
static float  warp_stream[WARP_STREAM_VERTS * 5];

static const char warp_vert_src[] =
    "#version 450 core\n"
    "layout(location = 0) in vec3 a_pos;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "uniform mat4 u_mvp;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = a_uv;\n"
    "    gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
    "}\n";

static const char warp_frag_src[] =
    "#version 450 core\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "uniform float u_alpha;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    vec4 c = texture(u_tex, v_uv);\n"
    "    frag_color = vec4(c.rgb, c.a * u_alpha);\n"
    "}\n";

static void Warp_InitRenderer (void)
{
	if (warp_prog)
		return;

	warp_prog = GL_BuildProgram (warp_vert_src, warp_frag_src);
	if (!warp_prog)
		Sys_Error ("Warp_InitRenderer: shader compile failed");

	qglUseProgram (warp_prog);
	u_warp_mvp   = qglGetUniformLocation (warp_prog, "u_mvp");
	u_warp_tex   = qglGetUniformLocation (warp_prog, "u_tex");
	u_warp_alpha = qglGetUniformLocation (warp_prog, "u_alpha");
	qglUseProgram (0);

	warp_vao = GLVertexArray::Create ();
	warp_vbo = GLBuffer::Create ();
	qglBindVertexArray (warp_vao);
	qglBindBuffer (GL_ARRAY_BUFFER, warp_vbo);
	qglBufferData (GL_ARRAY_BUFFER, sizeof(warp_stream), nullptr, GL_STREAM_DRAW);
	// location 0: xyz  (3 floats, offset 0, stride 5*4=20)
	qglVertexAttribPointer (0, 3, GL_FLOAT, GL_FALSE, 5*sizeof(float), nullptr);
	qglEnableVertexAttribArray (0);
	// location 1: uv  (2 floats, offset 12)
	qglVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, 5*sizeof(float), reinterpret_cast<void*>(3*sizeof(float)));
	qglEnableVertexAttribArray (1);
	qglBindVertexArray (0);
	qglBindBuffer (GL_ARRAY_BUFFER, 0);
}

// Explicit teardown, called from Host_Shutdown before VID_Shutdown() destroys
// the GL context -- see gl_shader.h's comment on why this can't be left to
// these globals' own (static-duration) destructors.
void GL_Warp_Shutdown (void)
{
	warp_vao.Release ();
	warp_vbo.Release ();
	warp_prog.Release ();
}

static void Warp_BeginDraw (void)
{
	Warp_InitRenderer ();
	GL_DisableMultitexture ();
	qglUseProgram (warp_prog);
	qglBindVertexArray (warp_vao);
	qglBindBuffer (GL_ARRAY_BUFFER, warp_vbo);
	qglUniform1i (u_warp_tex, 0);
	qglUniform1f (u_warp_alpha, 1.0f);
}

static void Warp_SetMVP (void)
{
	float mvp[16];
	GL_GetMVP (mvp);
	qglUniformMatrix4fv (u_warp_mvp, 1, GL_FALSE, mvp);
}

static void Warp_EndDraw (void)
{
	qglBindVertexArray (0);
	qglUseProgram (0);
}

/*
=============
EmitWaterPolys

Does a water warp on the pre-fragmented glpoly_t chain
=============
*/
void EmitWaterPolys (msurface_t *fa)
{
	glpoly_t	*p;

	Warp_BeginDraw ();
	Warp_SetMVP ();
	qglUniform1f (u_warp_alpha, r_wateralpha.value);

	for (p=fa->polys ; p ; p=p->next)
	{
		int n    = p->numverts;
		int ntri = n - 2;
		if (ntri < 1 || ntri * 3 > WARP_STREAM_VERTS)
			continue;

		float *out = warp_stream;

		for (int i = 1; i < n - 1; i++)
		{
			const float *verts[3] = { p->verts[0], p->verts[i], p->verts[i+1] };
			for (int vi = 0; vi < 3; vi++)
			{
				const float *v = verts[vi];
				float os = v[3], ot = v[4];
				float s = os + turbsin[(int)((ot*0.125+realtime) * TURBSCALE) & 255];
				s *= (1.0/64);
				float t = ot + turbsin[(int)((os*0.125+realtime) * TURBSCALE) & 255];
				t *= (1.0/64);

				out[0] = v[0]; out[1] = v[1]; out[2] = v[2];
				out[3] = s;    out[4] = t;
				out += 5;
			}
		}

		int nverts = ntri * 3;
		qglBufferData (GL_ARRAY_BUFFER,
		    (GLsizeiptr)(nverts * 5 * sizeof(float)),
		    warp_stream, GL_STREAM_DRAW);
		glDrawArrays (GL_TRIANGLES, 0, nverts);
	}

	Warp_EndDraw ();
}




/*
=============
EmitSkyPolys

Assumes the warp shader/VAO/VBO are already bound (see EmitBothSkyLayers
and R_DrawSkyChain, which wrap calls to this with Warp_BeginDraw/EndDraw)
and that the caller has already bound the desired sky texture.
=============
*/
void EmitSkyPolys (msurface_t *fa)
{
	glpoly_t	*p;
	vec3_t		dir;
	float		length;

	for (p=fa->polys ; p ; p=p->next)
	{
		int n    = p->numverts;
		int ntri = n - 2;
		if (ntri < 1 || ntri * 3 > WARP_STREAM_VERTS)
			continue;

		float *out = warp_stream;

		for (int i = 1; i < n - 1; i++)
		{
			const float *verts[3] = { p->verts[0], p->verts[i], p->verts[i+1] };
			for (int vi = 0; vi < 3; vi++)
			{
				const float *v = verts[vi];

				VectorSubtract (v, r_origin, dir);
				dir[2] *= 3;	// flatten the sphere

				length = dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2];
				length = sqrt (length);
				length = 6*63/length;

				dir[0] *= length;
				dir[1] *= length;

				float s = (speedscale + dir[0]) * (1.0/128);
				float t = (speedscale + dir[1]) * (1.0/128);

				out[0] = v[0]; out[1] = v[1]; out[2] = v[2];
				out[3] = s;    out[4] = t;
				out += 5;
			}
		}

		int nverts = ntri * 3;
		qglBufferData (GL_ARRAY_BUFFER,
		    (GLsizeiptr)(nverts * 5 * sizeof(float)),
		    warp_stream, GL_STREAM_DRAW);
		glDrawArrays (GL_TRIANGLES, 0, nverts);
	}
}

/*
===============
EmitBothSkyLayers

Does a sky warp on the pre-fragmented glpoly_t chain
This will be called for brushmodels, the world
will have them chained together.
===============
*/
void EmitBothSkyLayers (msurface_t *fa)
{
	Warp_BeginDraw ();
	Warp_SetMVP ();

	GL_Bind (solidskytexture);
	speedscale = realtime*8;
	speedscale -= (int)speedscale & ~127 ;

	EmitSkyPolys (fa);

	glEnable (GL_BLEND);
	GL_Bind (alphaskytexture);
	speedscale = realtime*16;
	speedscale -= (int)speedscale & ~127 ;

	EmitSkyPolys (fa);

	glDisable (GL_BLEND);

	Warp_EndDraw ();
}

#ifndef QUAKE2
/*
=================
R_DrawSkyChain
=================
*/
void R_DrawSkyChain (msurface_t *s)
{
	msurface_t	*fa;

	Warp_BeginDraw ();
	Warp_SetMVP ();

	// used when gl_texsort is on
	GL_Bind(solidskytexture);
	speedscale = realtime*8;
	speedscale -= (int)speedscale & ~127 ;

	for (fa=s ; fa ; fa=fa->texturechain)
		EmitSkyPolys (fa);

	glEnable (GL_BLEND);
	GL_Bind (alphaskytexture);
	speedscale = realtime*16;
	speedscale -= (int)speedscale & ~127 ;

	for (fa=s ; fa ; fa=fa->texturechain)
		EmitSkyPolys (fa);

	glDisable (GL_BLEND);

	Warp_EndDraw ();
}

#endif

//===============================================================

/*
=============
R_InitSky

A sky texture is 256*128, with the right side being a masked overlay
==============
*/
void R_InitSky (texture_t *mt)
{
	int			i, j, p;
	byte		*src;
	unsigned	trans[128*128];
	unsigned	transpix;
	int			r, g, b;
	unsigned	*rgba;
	extern	int			skytexturenum;

	src = reinterpret_cast<byte*>(mt) + mt->offsets[0];

	// make an average value for the back to avoid
	// a fringe on the top level

	r = g = b = 0;
	for (i=0 ; i<128 ; i++)
		for (j=0 ; j<128 ; j++)
		{
			p = src[i*256 + j + 128];
			rgba = &d_8to24table[p];
			trans[(i*128) + j] = *rgba;
			r += (reinterpret_cast<byte*>(rgba))[0];
			g += (reinterpret_cast<byte*>(rgba))[1];
			b += (reinterpret_cast<byte*>(rgba))[2];
		}

	(reinterpret_cast<byte*>(&transpix))[0] = r/(128*128);
	(reinterpret_cast<byte*>(&transpix))[1] = g/(128*128);
	(reinterpret_cast<byte*>(&transpix))[2] = b/(128*128);
	(reinterpret_cast<byte*>(&transpix))[3] = 0;


	if (!solidskytexture)
	{
		GL_ReserveTextureNames (1);
		solidskytexture = texture_extension_number++;
	}
	GL_Bind (solidskytexture );
	glTexImage2D (GL_TEXTURE_2D, 0, gl_solid_format, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, trans);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);


	for (i=0 ; i<128 ; i++)
		for (j=0 ; j<128 ; j++)
		{
			p = src[i*256 + j];
			if (p == 0)
				trans[(i*128) + j] = transpix;
			else
				trans[(i*128) + j] = d_8to24table[p];
		}

	if (!alphaskytexture)
	{
		GL_ReserveTextureNames (1);
		alphaskytexture = texture_extension_number++;
	}
	GL_Bind(alphaskytexture);
	glTexImage2D (GL_TEXTURE_2D, 0, gl_alpha_format, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, trans);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

