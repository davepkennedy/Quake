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
// qc_backend.h -- the engine's interface to its game-logic backend.
//
// Mirrors net.h's net_driver_t/net_landriver_t: a plain struct of C
// function pointers, not a C++ virtual base class, because the whole
// point is a stable, language-neutral ABI a future non-C++ backend
// (Lua/Python/etc via FFI) could implement without linking against this
// engine's C++ toolchain at all.
//
// entvars_t's field layout (progdefs.q1) is NOT part of this interface --
// it's a separate, lower-level contract every backend shares directly:
// any backend reads/writes the same edict_t/entvars_t struct the rest of
// the engine does (624 direct ->v.field accesses across the engine as of
// this writing, sv_phys.cpp alone accounting for over a third -- rewriting
// those to go through this interface would be a much larger, riskier, and
// lower-value project than the one this interface actually solves).
// func_t/string_t values stored in those fields are opaque 32-bit
// integers whose meaning is defined entirely by whichever backend is
// active; nothing outside this interface (and the backend itself)
// interprets them.
//
// See project_quake_vm_separation.md, phase 8, for the survey this was
// designed from.

struct qc_backend_t
{
    const char *name;

    // ------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------
    // Called once at engine startup (Host_Init) -- registers this
    // backend's own console commands/cvars. Distinct from LoadGameLogic,
    // which runs once per level spawn.
    void (*Init)(void);
    void (*LoadGameLogic)(void);
    // Per-entity storage this backend needs beyond sizeof(entvars_t) --
    // e.g. QC mods that declare extra fields past progdefs.q1's standard
    // set. Queried once per level spawn, after LoadGameLogic, to size the
    // edict array's stride.
    int (*GetEdictExtraSize)(void);
    // Parses a .bsp's entity lump and spawns every entity in it (looks up
    // and calls each one's spawn function). isDeathmatch/skill decide
    // which entities are inhibited (SPAWNFLAG_NOT_*) -- passed in rather
    // than read from the deathmatch cvar/current_skill directly, so this
    // doesn't reach into engine state on its own.
    void (*SpawnEntitiesForLevel)(const char *entityLumpText, qboolean isDeathmatch, int skill);

    // ------------------------------------------------------------------
    // Entity lifecycle
    // ------------------------------------------------------------------
    void (*ClearEdict)(edict_t *e); // zeroes e->v (this backend's full per-entity size) and e->free
    // Optional/mod-specific field lookup by name (e.g. rogue mission pack
    // fields, or fields a specific compiled game-logic build declares but
    // this engine build doesn't know about at compile time).
    std::optional<eval_t *> (*GetEdictFieldValue)(edict_t *ed, const char *fieldName);

    // ------------------------------------------------------------------
    // Calling convention
    // ------------------------------------------------------------------
    void (*ExecuteFunction)(func_t fnum);                                     // self/other untouched
    void (*ExecuteEntityFunction)(edict_t *self, func_t fnum);                 // other reset to the world entity
    void (*ExecuteEntityFunctionWithOther)(edict_t *self, edict_t *other, func_t fnum);
    edict_t *(*GetSelf)(void);
    void (*SetSelf)(edict_t *self);
    edict_t *(*GetOther)(void);
    void (*SetOther)(edict_t *other);
    void (*SetGlobalTime)(double time);
    void (*SetFrameTime)(float frametime);

    // ------------------------------------------------------------------
    // Well-known entry points -- the fixed set of top-level callbacks the
    // engine invokes by name rather than through an entity field
    // (progdefs.q1's globalvars_t function slots).
    // ------------------------------------------------------------------
    func_t (*StartFrameFunc)(void);
    func_t (*PlayerPreThinkFunc)(void);
    func_t (*PlayerPostThinkFunc)(void);
    func_t (*ClientKillFunc)(void);
    func_t (*ClientConnectFunc)(void);
    func_t (*PutClientInServerFunc)(void);
    func_t (*ClientDisconnectFunc)(void);
    func_t (*SetNewParmsFunc)(void);
    func_t (*SetChangeParmsFunc)(void);

    // ------------------------------------------------------------------
    // Game-mode / level config
    // ------------------------------------------------------------------
    qboolean (*IsDeathmatch)(void);
    void (*SetGameMode)(qboolean isDeathmatch, qboolean isCoop);
    void (*SetMapName)(const char *name);
    int (*GetServerFlags)(void);
    void (*SetServerFlags)(int flags);

    // ------------------------------------------------------------------
    // Spawn parms (carries a client's inventory/stats across a changelevel)
    // ------------------------------------------------------------------
    float (*GetSpawnParm)(int index); // 0-based, 0..NUM_SPAWN_PARMS-1
    void (*SetSpawnParm)(int index, float value);

    // ------------------------------------------------------------------
    // Intermission stats -- read-only from the engine side; the backend's
    // own game logic increments these itself.
    // ------------------------------------------------------------------
    int (*TotalSecrets)(void);
    int (*TotalMonsters)(void);
    int (*FoundSecrets)(void);
    int (*KilledMonsters)(void);

    // ------------------------------------------------------------------
    // Per-frame forced-relink flag
    // ------------------------------------------------------------------
    qboolean (*ForceRetouchPending)(void);
    void (*DecrementForceRetouch)(void);

    // ------------------------------------------------------------------
    // String resolution -- entvars_t string_t fields are opaque handles
    // this backend defines the encoding for.
    // ------------------------------------------------------------------
    char *(*GetString)(string_t num);
    string_t (*SetString)(const char *s);

    // ------------------------------------------------------------------
    // Savegame read/write -- the engine owns the save file's envelope
    // (version/comment/spawn parms/skill/mapname/time/lightstyles, all
    // engine-level concerns); the backend owns serializing its own
    // globals and per-entity state into/out of that envelope.
    // ------------------------------------------------------------------
    void (*WriteGlobals)(std::ofstream &f);
    void (*WriteEdict)(std::ofstream &f, edict_t *ed);
    void (*ReadGlobals)(const char *data);
    const char *(*ReadEdict)(const char *data, edict_t *ent); // returns the position past the parsed edict
};

// The active backend. Exactly one populated instance exists today (the
// QuakeC interpreter, pr_edict.cpp) -- swapping in a second backend means
// populating a second qc_backend_t and pointing this at it, not changing
// any of this interface's callers.
extern qc_backend_t *g_qcBackend;
