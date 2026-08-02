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
// server.h
#pragma once

#include "qlimits.h" // MAX_MODELS, MAX_SOUNDS, MAX_LIGHTSTYLES, MAX_DATAGRAM,
                     // MAX_MSGLEN
#include "common.h"  // byte, qboolean, sizebuf_t
#include "mathlib.h" // vec3_t
#include "client.h"  // usercmd_t
#include "progs.h"   // edict_t

struct server_static_t
{
    int maxclients;
    int maxclientslimit;
    struct client_t *clients;    // [maxclients]
    int serverflags;             // episode completion information
    qboolean changelevel_issued; // cleared when at SV_SpawnServer
};

//=============================================================================

enum class server_state_t
{
    ss_loading,
    ss_active
};

struct server_t
{
    // Resets every field to zero, matching the previous memset(&sv, 0,
    // sizeof(sv)) call sites -- kept as an explicit method (rather than
    // leaving memset scattered at each call site) so it stays correct if
    // this struct ever gains a non-trivial member (e.g. std::string).
    void Clear();

    qboolean active; // false if only a net client

    qboolean paused;
    qboolean loadgame; // handle connections specially

    double time;

    int lastcheck; // used by PF_checkclient
    double lastchecktime;

    char name[64]; // map name
    char modelname[64]; // maps/<name>.bsp, for model_precache[0]
    struct model_t *worldmodel;
    char *model_precache[MAX_MODELS]; // nullptr terminated
    struct model_t *models[MAX_MODELS];
    char *sound_precache[MAX_SOUNDS]; // nullptr terminated
    char *lightstyles[MAX_LIGHTSTYLES];
    int num_edicts;
    int max_edicts;
    edict_t *edicts;      // can NOT be array indexed, because
                          // edict_t is variable sized, but can
                          // be used to reference the world ent
    server_state_t state; // some actions are only valid during load

    sizebuf_t datagram;
    byte datagram_buf[MAX_DATAGRAM];

    sizebuf_t reliable_datagram; // copied to all clients at end of frame
    byte reliable_datagram_buf[MAX_DATAGRAM];

    sizebuf_t signon;
    byte signon_buf[8192];
};

constexpr int NUM_PING_TIMES = 16;
constexpr int NUM_SPAWN_PARMS = 16;

struct client_t
{
    qboolean active;     // false = client is free
    qboolean spawned;    // false = don't send datagrams
    qboolean dropasap;   // has been told to go to another level
    qboolean privileged; // can execute any host command
    qboolean sendsignon; // only valid before spawned

    double last_message; // reliable messages must be sent
                         // periodically

    struct qsocket_t *netconnection; // communications handle

    usercmd_t cmd;  // movement
    vec3_t wishdir; // intended motion calced from cmd

    sizebuf_t message; // can be added to at any time,
                       // copied and clear once per frame
    byte msgbuf[MAX_MSGLEN];
    edict_t *edict; // EDICT_NUM(clientnum+1)
    char name[32];  // for printing to other people
    int colors;

    float ping_times[NUM_PING_TIMES];
    int num_pings; // ping_times[num_pings%NUM_PING_TIMES]

    // spawn parms are carried from level to level
    float spawn_parms[NUM_SPAWN_PARMS];

    // client known data for deltas
    int old_frags;
};

//=============================================================================

// edict->movetype values
constexpr int MOVETYPE_NONE = 0; // never moves
constexpr int MOVETYPE_ANGLENOCLIP = 1;
constexpr int MOVETYPE_ANGLECLIP = 2;
constexpr int MOVETYPE_WALK = 3; // gravity
constexpr int MOVETYPE_STEP = 4; // gravity, special edge handling
constexpr int MOVETYPE_FLY = 5;
constexpr int MOVETYPE_TOSS = 6; // gravity
constexpr int MOVETYPE_PUSH = 7; // no clip to world, push and crush
constexpr int MOVETYPE_NOCLIP = 8;
constexpr int MOVETYPE_FLYMISSILE = 9; // extra size to monsters
constexpr int MOVETYPE_BOUNCE = 10;

// edict->solid values
constexpr int SOLID_NOT = 0;      // no interaction with other objects
constexpr int SOLID_TRIGGER = 1;  // touch on edge, but not blocking
constexpr int SOLID_BBOX = 2;     // touch on edge, block
constexpr int SOLID_SLIDEBOX = 3; // touch on edge, but not an onground
constexpr int SOLID_BSP = 4;      // bsp clip, touch on edge, block

// edict->deadflag values
constexpr int DEAD_NO = 0;
constexpr int DEAD_DYING = 1;
constexpr int DEAD_DEAD = 2;

constexpr int DAMAGE_NO = 0;
constexpr int DAMAGE_YES = 1;
constexpr int DAMAGE_AIM = 2;

// edict->flags
constexpr int FL_FLY = 1;
constexpr int FL_SWIM = 2;
// #define	FL_GLIMPSE				4
constexpr int FL_CONVEYOR = 4;
constexpr int FL_CLIENT = 8;
constexpr int FL_INWATER = 16;
constexpr int FL_MONSTER = 32;
constexpr int FL_GODMODE = 64;
constexpr int FL_NOTARGET = 128;
constexpr int FL_ITEM = 256;
constexpr int FL_ONGROUND = 512;
constexpr int FL_PARTIALGROUND = 1024; // not all corners are valid
constexpr int FL_WATERJUMP = 2048;     // player jumping out of water
constexpr int FL_JUMPRELEASED = 4096;  // for jump debouncing

// entity effects

constexpr int EF_BRIGHTFIELD = 1;
constexpr int EF_MUZZLEFLASH = 2;
constexpr int EF_BRIGHTLIGHT = 4;
constexpr int EF_DIMLIGHT = 8;

constexpr int SPAWNFLAG_NOT_EASY = 256;
constexpr int SPAWNFLAG_NOT_MEDIUM = 512;
constexpr int SPAWNFLAG_NOT_HARD = 1024;
constexpr int SPAWNFLAG_NOT_DEATHMATCH = 2048;


//============================================================================

extern cvar_t teamplay;
extern cvar_t skill;
extern cvar_t deathmatch;
extern cvar_t coop;
extern cvar_t fraglimit;
extern cvar_t timelimit;

extern server_static_t svs; // persistant server info
extern server_t sv;         // local server

// Narrow entity/client-pool accessors -- for QuakeC builtins (pr_cmds.cpp)
// that need to iterate or look up by entity number without reaching into
// sv./svs.'s representation directly.
int SV_NumEdicts(void);                   // sv.num_edicts
int SV_NumClients(void);                  // svs.maxclients
client_t *SV_ClientForEntNum(int entnum); // 1-based; nullptr if entnum isn't a client

// Narrow network-buffer accessors -- same idea, for QuakeC builtins that
// write directly to one of the server's outgoing message buffers instead
// of going through WriteDest()'s destination dispatch.
sizebuf_t *SV_SignonBuffer(void);           // &sv.signon
sizebuf_t *SV_DatagramBuffer(void);         // &sv.datagram
sizebuf_t *SV_ReliableDatagramBuffer(void); // &sv.reliable_datagram

// Narrow sim-clock/bookkeeping accessors -- same idea again. SV_Time() is
// a trivial read; the LastCheckClient pair covers PF_checkclient's own
// round-robin PVS-check cursor, still stored in server_t but only ever
// touched from pr_cmds.cpp.
double SV_Time(void);                                // sv.time
int SV_LastCheckClient(void);                        // sv.lastcheck
double SV_LastCheckClientTime(void);                 // sv.lastchecktime
void SV_SetLastCheckClient(int entnum, double time); // sv.lastcheck/lastchecktime

// Narrow lifecycle/precache accessors -- this bucket is more mixed than
// the others: most are simple reads, but SV_TryIssueChangelevel is a
// genuine check-and-set state transition, not a plain getter/setter.
server_state_t SV_State(void);               // sv.state
struct model_t *SV_WorldModel(void);         // sv.worldmodel
int SV_SoundPrecacheIndex(const char *name); // find only; -1 if not precached
int SV_PrecacheSound(char *name);            // find-or-register; -1 if table full
int SV_ModelPrecacheIndex(const char *name); // find only; -1 if not precached
int SV_PrecacheModel(char *name);            // find-or-register (also loads the model); -1 if table full
struct model_t *SV_ModelForIndex(int index); // sv.models[index]
qboolean SV_TryIssueChangelevel(void);       // true if this call issued it, false if already issued this spawn
qboolean SV_Active(void);                    // sv.active
int SV_MaxClientsLimit(void);                // svs.maxclientslimit

extern client_t *host_client;

extern double host_time;

extern edict_t *sv_player;

//===========================================================

void SV_Init(void);

void SV_StartParticle(vec3_t org, vec3_t dir, int color, int count);
void SV_StartSound(edict_t *entity, int channel, const char *sample, int volume, float attenuation);

void SV_DropClient(qboolean crash);

void SV_SendClientMessages(void);
void SV_ClearDatagram(void);

int SV_ModelIndex(const char *name);

void SV_SetIdealPitch(void);

void SV_AddUpdates(void);

void SV_ClientThink(void);
void SV_AddClientToServer(struct qsocket_t *ret);

void SV_ClientPrintf(const char *fmt, ...);
void SV_BroadcastPrintf(const char *fmt, ...);

void SV_Physics(void);

qboolean SV_CheckBottom(edict_t *ent);
qboolean SV_movestep(edict_t *ent, vec3_t move, qboolean relink);

void SV_WriteClientdataToMessage(edict_t *ent, sizebuf_t *msg);

void SV_MoveToGoal(void);

void SV_CheckForNewClients(void);
void SV_RunClients(void);
void SV_SaveSpawnparms();
void SV_SpawnServer(char *server);
