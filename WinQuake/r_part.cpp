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

#include "quakedef.h"
#include "hunk_resource.h"
#include <vector>

#define MAX_PARTICLES                                                                                                  \
    2048 // default max # of particles at one
         //  time
#define ABSOLUTE_MIN_PARTICLES                                                                                         \
    512 // no fewer than this no matter what's
        //  on the command line

int ramp1[8] = {0x6f, 0x6d, 0x6b, 0x69, 0x67, 0x65, 0x63, 0x61};
int ramp2[8] = {0x6f, 0x6e, 0x6d, 0x6c, 0x6b, 0x6a, 0x68, 0x66};
int ramp3[8] = {0x6d, 0x6b, 6, 5, 4, 3};

particle_t *active_particles, *free_particles;

std::pmr::vector<particle_t> particles;
int r_numparticles;

vec3_t r_pright, r_pup, r_ppn;

#ifdef GLQUAKE
// -------------------------------------------------------------------------
// Particle renderer state (VAO / VBO / GLSL shader)
//
// Particles are unlit, camera-facing billboard triangles: one textured tri
// per particle (org, org+up*scale, org+right*scale), vertex-colored per
// particle, modulated with the shared dot-texture's alpha channel.
// -------------------------------------------------------------------------

static GLVertexArray particle_vao;
static GLBuffer particle_vbo;
static GLProgram particle_prog;
static GLint u_particle_mvp = -1;
static GLint u_particle_tex = -1;

#define PARTICLE_STREAM_VERTS 6144 // flushed mid-frame if exceeded
static float particle_stream[PARTICLE_STREAM_VERTS * 8];
static int particle_stream_n = 0; // verts currently buffered

static const char particle_vert_src[] = "#version 450 core\n"
                                        "layout(location = 0) in vec3 a_pos;\n"
                                        "layout(location = 1) in vec2 a_uv;\n"
                                        "layout(location = 2) in vec3 a_color;\n"
                                        "uniform mat4 u_mvp;\n"
                                        "out vec2 v_uv;\n"
                                        "out vec3 v_color;\n"
                                        "void main() {\n"
                                        "    v_uv = a_uv;\n"
                                        "    v_color = a_color;\n"
                                        "    gl_Position = u_mvp * vec4(a_pos, 1.0);\n"
                                        "}\n";

static const char particle_frag_src[] = "#version 450 core\n"
                                        "in vec2 v_uv;\n"
                                        "in vec3 v_color;\n"
                                        "uniform sampler2D u_tex;\n"
                                        "out vec4 frag_color;\n"
                                        "void main() {\n"
                                        "    vec4 c = texture(u_tex, v_uv);\n"
                                        "    frag_color = vec4(c.rgb * v_color, c.a);\n"
                                        "}\n";

static void Particle_InitRenderer(void)
{
    if (particle_prog)
    {
        return;
    }

    particle_prog = GL_BuildProgram(particle_vert_src, particle_frag_src);
    if (!particle_prog)
    {
        Sys_Error("Particle_InitRenderer: shader compile failed");
    }

    qglUseProgram(particle_prog);
    u_particle_mvp = qglGetUniformLocation(particle_prog, "u_mvp");
    u_particle_tex = qglGetUniformLocation(particle_prog, "u_tex");
    qglUseProgram(0);

    particle_vao = GLVertexArray::Create();
    particle_vbo = GLBuffer::Create();
    qglBindVertexArray(particle_vao);
    qglBindBuffer(GL_ARRAY_BUFFER, particle_vbo);
    qglBufferData(GL_ARRAY_BUFFER, sizeof(particle_stream), nullptr, GL_STREAM_DRAW);
    // location 0: xyz  (3 floats, offset 0, stride 8*4=32)
    qglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), nullptr);
    qglEnableVertexAttribArray(0);
    // location 1: uv  (2 floats, offset 12)
    qglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void *>(3 * sizeof(float)));
    qglEnableVertexAttribArray(1);
    // location 2: color  (3 floats, offset 20)
    qglVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void *>(5 * sizeof(float)));
    qglEnableVertexAttribArray(2);
    qglBindVertexArray(0);
    qglBindBuffer(GL_ARRAY_BUFFER, 0);
}

// Explicit teardown, called from Host_Shutdown before VID_Shutdown() destroys
// the GL context -- see gl_shader.h's comment on why this can't be left to
// these globals' own (static-duration) destructors.
void R_Part_Shutdown(void)
{
    particle_vao.Release();
    particle_vbo.Release();
    particle_prog.Release();
}

static void Particle_BeginDraw(void)
{
    Particle_InitRenderer();

    float mvp[16];
    GL_GetMVP(mvp);

    GL_Bind(particletexture);
    glEnable(GL_BLEND);

    qglUseProgram(particle_prog);
    qglBindVertexArray(particle_vao);
    qglBindBuffer(GL_ARRAY_BUFFER, particle_vbo);
    qglUniform1i(u_particle_tex, 0);
    qglUniformMatrix4fv(u_particle_mvp, 1, GL_FALSE, mvp);

    particle_stream_n = 0;
}

static void Particle_Flush(void)
{
    if (!particle_stream_n)
    {
        return;
    }
    qglBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(particle_stream_n * 8 * sizeof(float)), particle_stream,
                  GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, particle_stream_n);
    particle_stream_n = 0;
}

// Appends one particle's billboard triangle to the stream buffer, flushing
// and re-issuing the draw call first if the buffer is full.
static void Particle_AddTri(const vec3_t org, const vec3_t up, const vec3_t right, float scale, int color)
{
    if (particle_stream_n + 3 > PARTICLE_STREAM_VERTS)
    {
        Particle_Flush();
    }

    byte *rgba = reinterpret_cast<byte *>(&d_8to24table[color]);
    float col[3] = {rgba[0] / 255.f, rgba[1] / 255.f, rgba[2] / 255.f};

    float *out = particle_stream + particle_stream_n * 8;

    out[0] = org[0];
    out[1] = org[1];
    out[2] = org[2];
    out[3] = 0.f;
    out[4] = 0.f;
    out[5] = col[0];
    out[6] = col[1];
    out[7] = col[2];
    out += 8;

    out[0] = org[0] + up[0] * scale;
    out[1] = org[1] + up[1] * scale;
    out[2] = org[2] + up[2] * scale;
    out[3] = 1.f;
    out[4] = 0.f;
    out[5] = col[0];
    out[6] = col[1];
    out[7] = col[2];
    out += 8;

    out[0] = org[0] + right[0] * scale;
    out[1] = org[1] + right[1] * scale;
    out[2] = org[2] + right[2] * scale;
    out[3] = 0.f;
    out[4] = 1.f;
    out[5] = col[0];
    out[6] = col[1];
    out[7] = col[2];

    particle_stream_n += 3;
}

static void Particle_EndDraw(void)
{
    Particle_Flush();
    qglBindVertexArray(0);
    qglUseProgram(0);
    glDisable(GL_BLEND);
}
#endif // GLQUAKE

/*
===============
R_InitParticles
===============
*/
void R_InitParticles(void)
{
    int i;

    i = COM_CheckParm("-particles");

    if (i)
    {
        r_numparticles = (int)(Q_atoi(com_argv[i + 1]));
        if (r_numparticles < ABSOLUTE_MIN_PARTICLES)
        {
            r_numparticles = ABSOLUTE_MIN_PARTICLES;
        }
    }
    else
    {
        r_numparticles = MAX_PARTICLES;
    }

    particles = std::pmr::vector<particle_t>(r_numparticles, Hunk_GetResource());
}


/*
===============
R_EntityParticles
===============
*/

#define NUMVERTEXNORMALS 162
extern float r_avertexnormals[NUMVERTEXNORMALS][3];
vec3_t avelocities[NUMVERTEXNORMALS];
float beamlength = 16;
vec3_t avelocity = {23, 7, 3};
float partstep = 0.01f;
float timescale = 0.01f;

void R_EntityParticles(entity_t *ent)
{
    int count;
    int i;
    particle_t *p;
    float angle;
    float sr, sp, sy, cr, cp, cy;
    vec3_t forward;
    float dist;

    dist = 64;
    count = 50;

    if (!avelocities[0][0])
    {
        for (i = 0; i < NUMVERTEXNORMALS * 3; i++)
        {
            avelocities[0][i] = (rand() & 255) * 0.01;
        }
    }

    for (i = 0; i < NUMVERTEXNORMALS; i++)
    {
        angle = cl.time * avelocities[i][0];
        sy = sin(angle);
        cy = cos(angle);
        angle = cl.time * avelocities[i][1];
        sp = sin(angle);
        cp = cos(angle);
        angle = cl.time * avelocities[i][2];
        sr = sin(angle);
        cr = cos(angle);

        forward[0] = cp * cy;
        forward[1] = cp * sy;
        forward[2] = -sp;

        if (!free_particles)
        {
            return;
        }
        p = free_particles;
        free_particles = p->next;
        p->next = active_particles;
        active_particles = p;

        p->die = cl.time + 0.01;
        p->color = 0x6f;
        p->type = ptype_t::pt_explode;

        p->org[0] = ent->origin[0] + r_avertexnormals[i][0] * dist + forward[0] * beamlength;
        p->org[1] = ent->origin[1] + r_avertexnormals[i][1] * dist + forward[1] * beamlength;
        p->org[2] = ent->origin[2] + r_avertexnormals[i][2] * dist + forward[2] * beamlength;
    }
}

/*
===============
R_ClearParticles
===============
*/
void R_ClearParticles(void)
{
    int i;

    free_particles = &particles[0];
    active_particles = nullptr;

    // particles.data() + (i+1) rather than &particles[i+1]: the last
    // iteration computes a one-past-the-end address (immediately
    // overwritten below, never dereferenced) -- fine for raw pointer
    // arithmetic, but std::vector::operator[] bounds-checks in debug
    // builds and would assert on the out-of-range index.
    for (i = 0; i < r_numparticles; i++)
    {
        particles[i].next = particles.data() + (i + 1);
    }
    particles[r_numparticles - 1].next = nullptr;
}

void R_ReadPointFile_f(void)
{
    FILE *f;
    vec3_t org;
    int r;
    int c;
    particle_t *p;
    std::string name;

    name = std::format("maps/{}.pts", sv.name);

    COM_FOpenFile(name.c_str(), &f);
    if (!f)
    {
        Con_Printf("couldn't open %s\n", name.c_str());
        return;
    }

    Con_Printf("Reading %s...\n", name.c_str());
    c = 0;
    for (;;)
    {
        r = fscanf(f, "%f %f %f\n", &org[0], &org[1], &org[2]);
        if (r != 3)
        {
            break;
        }
        c++;

        if (!free_particles)
        {
            Con_Printf("Not enough free particles\n");
            break;
        }
        p = free_particles;
        free_particles = p->next;
        p->next = active_particles;
        active_particles = p;

        p->die = 99999;
        p->color = (-c) & 15;
        p->type = ptype_t::pt_static;
        VectorCopy(vec3_origin, p->vel);
        VectorCopy(org, p->org);
    }

    fclose(f);
    Con_Printf("%i points read\n", c);
}

/*
===============
R_ParseParticleEffect

Parse an effect out of the server message
===============
*/
void R_ParseParticleEffect(void)
{
    vec3_t org, dir;
    int i, count, msgcount, color;

    for (i = 0; i < 3; i++)
    {
        org[i] = MSG_ReadCoord();
    }
    for (i = 0; i < 3; i++)
    {
        dir[i] = MSG_ReadChar() * (1.0 / 16);
    }
    msgcount = MSG_ReadByte();
    color = MSG_ReadByte();

    if (msgcount == 255)
    {
        count = 1024;
    }
    else
    {
        count = msgcount;
    }

    R_RunParticleEffect(org, dir, color, count);
}

/*
===============
R_ParticleExplosion

===============
*/
void R_ParticleExplosion(vec3_t org)
{
    int i, j;
    particle_t *p;

    for (i = 0; i < 1024; i++)
    {
        if (!free_particles)
        {
            return;
        }
        p = free_particles;
        free_particles = p->next;
        p->next = active_particles;
        active_particles = p;

        p->die = cl.time + 5;
        p->color = ramp1[0];
        p->ramp = rand() & 3;
        if (i & 1)
        {
            p->type = ptype_t::pt_explode;
            for (j = 0; j < 3; j++)
            {
                p->org[j] = org[j] + ((rand() % 32) - 16);
                p->vel[j] = (rand() % 512) - 256;
            }
        }
        else
        {
            p->type = ptype_t::pt_explode2;
            for (j = 0; j < 3; j++)
            {
                p->org[j] = org[j] + ((rand() % 32) - 16);
                p->vel[j] = (rand() % 512) - 256;
            }
        }
    }
}

/*
===============
R_ParticleExplosion2

===============
*/
void R_ParticleExplosion2(vec3_t org, int colorStart, int colorLength)
{
    int i, j;
    particle_t *p;
    int colorMod = 0;

    for (i = 0; i < 512; i++)
    {
        if (!free_particles)
        {
            return;
        }
        p = free_particles;
        free_particles = p->next;
        p->next = active_particles;
        active_particles = p;

        p->die = cl.time + 0.3;
        p->color = colorStart + (colorMod % colorLength);
        colorMod++;

        p->type = ptype_t::pt_blob;
        for (j = 0; j < 3; j++)
        {
            p->org[j] = org[j] + ((rand() % 32) - 16);
            p->vel[j] = (rand() % 512) - 256;
        }
    }
}

/*
===============
R_BlobExplosion

===============
*/
void R_BlobExplosion(vec3_t org)
{
    int i, j;
    particle_t *p;

    for (i = 0; i < 1024; i++)
    {
        if (!free_particles)
        {
            return;
        }
        p = free_particles;
        free_particles = p->next;
        p->next = active_particles;
        active_particles = p;

        p->die = cl.time + 1 + (rand() & 8) * 0.05;

        if (i & 1)
        {
            p->type = ptype_t::pt_blob;
            p->color = 66 + rand() % 6;
            for (j = 0; j < 3; j++)
            {
                p->org[j] = org[j] + ((rand() % 32) - 16);
                p->vel[j] = (rand() % 512) - 256;
            }
        }
        else
        {
            p->type = ptype_t::pt_blob2;
            p->color = 150 + rand() % 6;
            for (j = 0; j < 3; j++)
            {
                p->org[j] = org[j] + ((rand() % 32) - 16);
                p->vel[j] = (rand() % 512) - 256;
            }
        }
    }
}

/*
===============
R_RunParticleEffect

===============
*/
void R_RunParticleEffect(vec3_t org, vec3_t dir, int color, int count)
{
    int i, j;
    particle_t *p;

    for (i = 0; i < count; i++)
    {
        if (!free_particles)
        {
            return;
        }
        p = free_particles;
        free_particles = p->next;
        p->next = active_particles;
        active_particles = p;

        if (count == 1024)
        { // rocket explosion
            p->die = cl.time + 5;
            p->color = ramp1[0];
            p->ramp = rand() & 3;
            if (i & 1)
            {
                p->type = ptype_t::pt_explode;
                for (j = 0; j < 3; j++)
                {
                    p->org[j] = org[j] + ((rand() % 32) - 16);
                    p->vel[j] = (rand() % 512) - 256;
                }
            }
            else
            {
                p->type = ptype_t::pt_explode2;
                for (j = 0; j < 3; j++)
                {
                    p->org[j] = org[j] + ((rand() % 32) - 16);
                    p->vel[j] = (rand() % 512) - 256;
                }
            }
        }
        else
        {
            p->die = cl.time + 0.1 * (rand() % 5);
            p->color = (color & ~7) + (rand() & 7);
            p->type = ptype_t::pt_slowgrav;
            for (j = 0; j < 3; j++)
            {
                p->org[j] = org[j] + ((rand() & 15) - 8);
                p->vel[j] = dir[j] * 15; // + (rand()%300)-150;
            }
        }
    }
}

/*
===============
R_LavaSplash

===============
*/
void R_LavaSplash(vec3_t org)
{
    int i, j, k;
    particle_t *p;
    float vel;
    vec3_t dir;

    for (i = -16; i < 16; i++)
    {
        for (j = -16; j < 16; j++)
        {
            for (k = 0; k < 1; k++)
            {
                if (!free_particles)
                {
                    return;
                }
                p = free_particles;
                free_particles = p->next;
                p->next = active_particles;
                active_particles = p;

                p->die = cl.time + 2 + (rand() & 31) * 0.02;
                p->color = 224 + (rand() & 7);
                p->type = ptype_t::pt_slowgrav;

                dir[0] = j * 8 + (rand() & 7);
                dir[1] = i * 8 + (rand() & 7);
                dir[2] = 256;

                p->org[0] = org[0] + dir[0];
                p->org[1] = org[1] + dir[1];
                p->org[2] = org[2] + (rand() & 63);

                VectorNormalize(dir);
                vel = 50 + (rand() & 63);
                VectorScale(dir, vel, p->vel);
            }
        }
    }
}

/*
===============
R_TeleportSplash

===============
*/
void R_TeleportSplash(vec3_t org)
{
    int i, j, k;
    particle_t *p;
    float vel;
    vec3_t dir;

    for (i = -16; i < 16; i += 4)
    {
        for (j = -16; j < 16; j += 4)
        {
            for (k = -24; k < 32; k += 4)
            {
                if (!free_particles)
                {
                    return;
                }
                p = free_particles;
                free_particles = p->next;
                p->next = active_particles;
                active_particles = p;

                p->die = cl.time + 0.2 + (rand() & 7) * 0.02;
                p->color = 7 + (rand() & 7);
                p->type = ptype_t::pt_slowgrav;

                dir[0] = j * 8;
                dir[1] = i * 8;
                dir[2] = k * 8;

                p->org[0] = org[0] + i + (rand() & 3);
                p->org[1] = org[1] + j + (rand() & 3);
                p->org[2] = org[2] + k + (rand() & 3);

                VectorNormalize(dir);
                vel = 50 + (rand() & 63);
                VectorScale(dir, vel, p->vel);
            }
        }
    }
}

void R_RocketTrail(vec3_t start, vec3_t end, int type)
{
    vec3_t vec;
    float len;
    int j;
    particle_t *p;
    int dec;
    static int tracercount;

    VectorSubtract(end, start, vec);
    len = VectorNormalize(vec);
    if (type < 128)
    {
        dec = 3;
    }
    else
    {
        dec = 1;
        type -= 128;
    }

    while (len > 0)
    {
        len -= dec;

        if (!free_particles)
        {
            return;
        }
        p = free_particles;
        free_particles = p->next;
        p->next = active_particles;
        active_particles = p;

        VectorCopy(vec3_origin, p->vel);
        p->die = cl.time + 2;

        switch (type)
        {
        case 0: // rocket trail
            p->ramp = (rand() & 3);
            p->color = ramp3[(int)p->ramp];
            p->type = ptype_t::pt_fire;
            for (j = 0; j < 3; j++)
            {
                p->org[j] = start[j] + ((rand() % 6) - 3);
            }
            break;

        case 1: // smoke smoke
            p->ramp = (rand() & 3) + 2;
            p->color = ramp3[(int)p->ramp];
            p->type = ptype_t::pt_fire;
            for (j = 0; j < 3; j++)
            {
                p->org[j] = start[j] + ((rand() % 6) - 3);
            }
            break;

        case 2: // blood
            p->type = ptype_t::pt_grav;
            p->color = 67 + (rand() & 3);
            for (j = 0; j < 3; j++)
            {
                p->org[j] = start[j] + ((rand() % 6) - 3);
            }
            break;

        case 3:
        case 5: // tracer
            p->die = cl.time + 0.5;
            p->type = ptype_t::pt_static;
            if (type == 3)
            {
                p->color = 52 + ((tracercount & 4) << 1);
            }
            else
            {
                p->color = 230 + ((tracercount & 4) << 1);
            }

            tracercount++;

            VectorCopy(start, p->org);
            if (tracercount & 1)
            {
                p->vel[0] = 30 * vec[1];
                p->vel[1] = 30 * -vec[0];
            }
            else
            {
                p->vel[0] = 30 * -vec[1];
                p->vel[1] = 30 * vec[0];
            }
            break;

        case 4: // slight blood
            p->type = ptype_t::pt_grav;
            p->color = 67 + (rand() & 3);
            for (j = 0; j < 3; j++)
            {
                p->org[j] = start[j] + ((rand() % 6) - 3);
            }
            len -= 3;
            break;

        case 6: // voor trail
            p->color = 9 * 16 + 8 + (rand() & 3);
            p->type = ptype_t::pt_static;
            p->die = cl.time + 0.3;
            for (j = 0; j < 3; j++)
            {
                p->org[j] = start[j] + ((rand() & 15) - 8);
            }
            break;
        }

        VectorAdd(start, vec, start);
    }
}

/*
===============
R_DrawParticles
===============
*/
extern cvar_t sv_gravity;

void R_DrawParticles(void)
{
    particle_t *p, *kill;
    float grav;
    int i;
    float time2, time3;
    float time1;
    float dvel;
    float frametime;

#ifdef GLQUAKE
    vec3_t up, right;
    float scale;

    Particle_BeginDraw();

    VectorScale(vup, 1.5, up);
    VectorScale(vright, 1.5, right);
#else
    D_StartParticles();

    VectorScale(vright, xscaleshrink, r_pright);
    VectorScale(vup, yscaleshrink, r_pup);
    VectorCopy(vpn, r_ppn);
#endif
    frametime = cl.time - cl.oldtime;
    time3 = frametime * 15;
    time2 = frametime * 10; // 15;
    time1 = frametime * 5;
    grav = frametime * sv_gravity.value * 0.05;
    dvel = 4 * frametime;

    for (;;)
    {
        kill = active_particles;
        if (kill && kill->die < cl.time)
        {
            active_particles = kill->next;
            kill->next = free_particles;
            free_particles = kill;
            continue;
        }
        break;
    }

    for (p = active_particles; p; p = p->next)
    {
        for (;;)
        {
            kill = p->next;
            if (kill && kill->die < cl.time)
            {
                p->next = kill->next;
                kill->next = free_particles;
                free_particles = kill;
                continue;
            }
            break;
        }

#ifdef GLQUAKE
        // hack a scale up to keep particles from disapearing
        scale = (p->org[0] - r_origin[0]) * vpn[0] + (p->org[1] - r_origin[1]) * vpn[1] +
                (p->org[2] - r_origin[2]) * vpn[2];
        if (scale < 20)
        {
            scale = 1;
        }
        else
        {
            scale = 1 + scale * 0.004;
        }
        Particle_AddTri(p->org, up, right, scale, (int)p->color);
#else
        D_DrawParticle(p);
#endif
        p->org[0] += p->vel[0] * frametime;
        p->org[1] += p->vel[1] * frametime;
        p->org[2] += p->vel[2] * frametime;

        switch (p->type)
        {
        case ptype_t::pt_static:
            break;
        case ptype_t::pt_fire:
            p->ramp += time1;
            if (p->ramp >= 6)
            {
                p->die = -1;
            }
            else
            {
                p->color = ramp3[(int)p->ramp];
            }
            p->vel[2] += grav;
            break;

        case ptype_t::pt_explode:
            p->ramp += time2;
            if (p->ramp >= 8)
            {
                p->die = -1;
            }
            else
            {
                p->color = ramp1[(int)p->ramp];
            }
            for (i = 0; i < 3; i++)
            {
                p->vel[i] += p->vel[i] * dvel;
            }
            p->vel[2] -= grav;
            break;

        case ptype_t::pt_explode2:
            p->ramp += time3;
            if (p->ramp >= 8)
            {
                p->die = -1;
            }
            else
            {
                p->color = ramp2[(int)p->ramp];
            }
            for (i = 0; i < 3; i++)
            {
                p->vel[i] -= p->vel[i] * frametime;
            }
            p->vel[2] -= grav;
            break;

        case ptype_t::pt_blob:
            for (i = 0; i < 3; i++)
            {
                p->vel[i] += p->vel[i] * dvel;
            }
            p->vel[2] -= grav;
            break;

        case ptype_t::pt_blob2:
            for (i = 0; i < 2; i++)
            {
                p->vel[i] -= p->vel[i] * dvel;
            }
            p->vel[2] -= grav;
            break;

        case ptype_t::pt_grav:
        case ptype_t::pt_slowgrav:
            p->vel[2] -= grav;
            break;
        }
    }

#ifdef GLQUAKE
    Particle_EndDraw();
#else
    D_EndParticles();
#endif
}
