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
// r_light.c

#include "quakedef.h"

int	r_dlightframecount;


/*
==================
R_AnimateLight
==================
*/
void R_AnimateLight (void)
{
	int			i,j,k;
	
//
// light animations
// 'm' is normal light, 'a' is no light, 'z' is double bright
	i = (int)(CL_Time()*10);
	for (j=0 ; j<MAX_LIGHTSTYLES ; j++)
	{
		if (!cl_lightstyle[j].length)
		{
			d_lightstylevalue[j] = 256;
			continue;
		}
		k = i % cl_lightstyle[j].length;
		k = cl_lightstyle[j].map[k] - 'a';
		k = k*22;
		d_lightstylevalue[j] = k;
	}	
}

/*
=============================================================================

DYNAMIC LIGHTS BLEND RENDERING

=============================================================================
*/

void AddLightBlend (float r, float g, float b, float a2)
{
	float	a;

	v_blend[3] = a = v_blend[3] + a2*(1-v_blend[3]);

	a2 = a2/a;

	v_blend[0] = v_blend[1]*(1-a2) + r*a2;
	v_blend[1] = v_blend[1]*(1-a2) + g*a2;
	v_blend[2] = v_blend[2]*(1-a2) + b*a2;
}

// -------------------------------------------------------------------------
// "flashblend" dlight renderer (VAO/VBO/GLSL) — each dlight is a
// camera-facing radial-gradient triangle fan: bright center, black rim.
// -------------------------------------------------------------------------

static GLVertexArray dlight_vao;
static GLBuffer      dlight_vbo;
static GLProgram     dlight_prog;
static GLint  u_dlight_mvp = -1;

#define DLIGHT_FAN_VERTS 18   // 1 center + 17 rim verts

static const char dlight_vert_src[] =
    "#version 450 core\n"
    "layout(location = 0) in vec3 a_pos;\n"
    "layout(location = 1) in vec3 a_color;\n"
    "uniform mat4 u_mvp;\n"
    "out vec3 v_color;\n"
    "void main() {\n"
    "    v_color = a_color;\n"
    "    gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
    "}\n";

static const char dlight_frag_src[] =
    "#version 450 core\n"
    "in vec3 v_color;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    frag_color = vec4(v_color, 1.0);\n"
    "}\n";

static void Dlight_InitRenderer (void)
{
	if (dlight_prog)
		return;

	dlight_prog = GL_BuildProgram (dlight_vert_src, dlight_frag_src);
	if (!dlight_prog)
		Sys_Error ("Dlight_InitRenderer: shader compile failed");

	qglUseProgram (dlight_prog);
	u_dlight_mvp = qglGetUniformLocation (dlight_prog, "u_mvp");
	qglUseProgram (0);

	dlight_vao = GLVertexArray::Create ();
	dlight_vbo = GLBuffer::Create ();
	qglBindVertexArray (dlight_vao);
	qglBindBuffer (GL_ARRAY_BUFFER, dlight_vbo);
	qglBufferData (GL_ARRAY_BUFFER, DLIGHT_FAN_VERTS*6*sizeof(float), nullptr, GL_STREAM_DRAW);
	// location 0: xyz  (3 floats, offset 0, stride 6*4=24)
	qglVertexAttribPointer (0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
	qglEnableVertexAttribArray (0);
	// location 1: color  (3 floats, offset 12)
	qglVertexAttribPointer (1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
	qglEnableVertexAttribArray (1);
	qglBindVertexArray (0);
	qglBindBuffer (GL_ARRAY_BUFFER, 0);
}

// Explicit teardown, called from Host_Shutdown before VID_Shutdown() destroys
// the GL context -- see gl_shader.h's comment on why this can't be left to
// these globals' own (static-duration) destructors.
void GL_Dlight_Shutdown (void)
{
	dlight_vao.Release ();
	dlight_vbo.Release ();
	dlight_prog.Release ();
}

void R_RenderDlight (dlight_t *light)
{
	int		i, j;
	float	a;
	vec3_t	v;
	float	rad;
	float	verts[DLIGHT_FAN_VERTS * 6];
	float	*out;

	rad = light->radius * 0.35;

	VectorSubtract (light->origin, r_origin, v);
	if (Length (v) < rad)
	{	// view is inside the dlight
		AddLightBlend (1, 0.5, 0, light->radius * 0.0003);
		return;
	}

	out = verts;
	for (i=0 ; i<3 ; i++)
		v[i] = light->origin[i] - vpn[i]*rad;
	out[0] = v[0]; out[1] = v[1]; out[2] = v[2];
	out[3] = 0.2f; out[4] = 0.1f; out[5] = 0.0f;
	out += 6;

	for (i=16 ; i>=0 ; i--)
	{
		a = i/16.0 * M_PI*2;
		for (j=0 ; j<3 ; j++)
			v[j] = light->origin[j] + vright[j]*cos(a)*rad
				+ vup[j]*sin(a)*rad;
		out[0] = v[0]; out[1] = v[1]; out[2] = v[2];
		out[3] = 0.0f; out[4] = 0.0f; out[5] = 0.0f;
		out += 6;
	}

	qglBufferData (GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
	glDrawArrays (GL_TRIANGLE_FAN, 0, DLIGHT_FAN_VERTS);
}

/*
=============
R_RenderDlights
=============
*/
void R_RenderDlights (void)
{
	int		i;
	dlight_t	*l;
	float		mvp[16];

	if (!gl_flashblend.value)
		return;

	r_dlightframecount = r_framecount + 1;	// because the count hasn't
											//  advanced yet for this frame

	Dlight_InitRenderer ();
	GL_GetMVP (mvp);

	glDepthMask (0);
	glEnable (GL_BLEND);
	glBlendFunc (GL_ONE, GL_ONE);

	qglUseProgram (dlight_prog);
	qglBindVertexArray (dlight_vao);
	qglBindBuffer (GL_ARRAY_BUFFER, dlight_vbo);
	qglUniformMatrix4fv (u_dlight_mvp, 1, GL_FALSE, mvp);

	l = cl_dlights;
	for (i=0 ; i<MAX_DLIGHTS ; i++, l++)
	{
		if (l->die < CL_Time() || !l->radius)
			continue;
		R_RenderDlight (l);
	}

	qglBindVertexArray (0);
	qglUseProgram (0);
	glDisable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask (1);
}


/*
=============================================================================

DYNAMIC LIGHTS

=============================================================================
*/

/*
=============
R_MarkLights
=============
*/
void R_MarkLights (dlight_t *light, int bit, mnode_t *node)
{
	mplane_t	*splitplane;
	float		dist;
	msurface_t	*surf;
	int			i;
	
	if (node->contents < 0)
		return;

	splitplane = node->plane;
	dist = DotProduct (light->origin, splitplane->normal) - splitplane->dist;
	
	if (dist > light->radius)
	{
		R_MarkLights (light, bit, node->children[0]);
		return;
	}
	if (dist < -light->radius)
	{
		R_MarkLights (light, bit, node->children[1]);
		return;
	}
		
// mark the polygons
	surf = CL_WorldModel()->surfaces + node->firstsurface;
	for (i=0 ; i<node->numsurfaces ; i++, surf++)
	{
		if (surf->dlightframe != r_dlightframecount)
		{
			surf->dlightbits = 0;
			surf->dlightframe = r_dlightframecount;
		}
		surf->dlightbits |= bit;
	}

	R_MarkLights (light, bit, node->children[0]);
	R_MarkLights (light, bit, node->children[1]);
}


/*
=============
R_PushDlights
=============
*/
void R_PushDlights (void)
{
	int		i;
	dlight_t	*l;

	if (gl_flashblend.value)
		return;

	r_dlightframecount = r_framecount + 1;	// because the count hasn't
											//  advanced yet for this frame
	l = cl_dlights;

	for (i=0 ; i<MAX_DLIGHTS ; i++, l++)
	{
		if (l->die < CL_Time() || !l->radius)
			continue;
		R_MarkLights ( l, 1<<i, CL_WorldModel()->nodes );
	}
}


/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

mplane_t		*lightplane;
vec3_t			lightspot;

int RecursiveLightPoint (mnode_t *node, vec3_t start, vec3_t end)
{
	int			r;
	float		front, back, frac;
	int			side;
	mplane_t	*plane;
	vec3_t		mid;
	msurface_t	*surf;
	int			s, t, ds, dt;
	int			i;
	mtexinfo_t	*tex;
	byte		*lightmap;
	unsigned	scale;
	int			maps;

	if (node->contents < 0)
		return -1;		// didn't hit anything
	
// calculate mid point

// FIXME: optimize for axial
	plane = node->plane;
	front = DotProduct (start, plane->normal) - plane->dist;
	back = DotProduct (end, plane->normal) - plane->dist;
	side = front < 0;
	
	if ( (back < 0) == side)
		return RecursiveLightPoint (node->children[side], start, end);
	
	frac = front / (front-back);
	mid[0] = start[0] + (end[0] - start[0])*frac;
	mid[1] = start[1] + (end[1] - start[1])*frac;
	mid[2] = start[2] + (end[2] - start[2])*frac;
	
// go down front side	
	r = RecursiveLightPoint (node->children[side], start, mid);
	if (r >= 0)
		return r;		// hit something
		
	if ( (back < 0) == side )
		return -1;		// didn't hit anuthing
		
// check for impact on this node
	VectorCopy (mid, lightspot);
	lightplane = plane;

	surf = CL_WorldModel()->surfaces + node->firstsurface;
	for (i=0 ; i<node->numsurfaces ; i++, surf++)
	{
		if (surf->flags & SURF_DRAWTILED)
			continue;	// no lightmaps

		tex = surf->texinfo;
		
		s = DotProduct (mid, tex->vecs[0]) + tex->vecs[0][3];
		t = DotProduct (mid, tex->vecs[1]) + tex->vecs[1][3];;

		if (s < surf->texturemins[0] ||
		t < surf->texturemins[1])
			continue;
		
		ds = s - surf->texturemins[0];
		dt = t - surf->texturemins[1];
		
		if ( ds > surf->extents[0] || dt > surf->extents[1] )
			continue;

		if (!surf->samples)
			return 0;

		ds >>= 4;
		dt >>= 4;

		lightmap = surf->samples;
		r = 0;
		if (lightmap)
		{

			lightmap += dt * ((surf->extents[0]>>4)+1) + ds;

			for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
					maps++)
			{
				scale = d_lightstylevalue[surf->styles[maps]];
				r += *lightmap * scale;
				lightmap += ((surf->extents[0]>>4)+1) *
						((surf->extents[1]>>4)+1);
			}
			
			r >>= 8;
		}
		
		return r;
	}

// go down back side
	return RecursiveLightPoint (node->children[!side], mid, end);
}

int R_LightPoint (vec3_t p)
{
	vec3_t		end;
	int			r;
	
	if (!CL_WorldModel()->lightdata)
		return 255;

	end[0] = p[0];
	end[1] = p[1];
	end[2] = p[2] - 2048;

	r = RecursiveLightPoint (CL_WorldModel()->nodes, p, end);
	
	if (r == -1)
		r = 0;

	return r;
}

