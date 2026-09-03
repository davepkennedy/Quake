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
// sv_edict.c -- entity dictionary

#include <deque>
#include <unordered_map>

#include "quakedef.h"

dprograms_t *progs;
dfunction_t *pr_functions;
char *pr_strings;
ddef_t *pr_fielddefs;
ddef_t *pr_globaldefs;
dstatement_t *pr_statements;
globalvars_t *pr_global_struct;
float *pr_globals; // same as pr_global_struct
int pr_edict_size; // in bytes

unsigned short pr_crc;

namespace
{
    // Backing store for dynamically-created strings (ED_NewString results --
    // entity string-typed fields set from map/savegame data -- see
    // PR_SetString's one call site in ED_ParseEpair). Encoded as negative
    // string_t values so they can never collide with a real (always >= 0)
    // offset into the static pr_strings blob: index i is exposed as
    // string_t -(i + 1). Cleared at the top of PR_LoadProgs, matching
    // Host_ClearMemory's own per-level hunk reset in SV_SpawnServer -- same
    // lifetime bound the old Hunk_Alloc-based ED_NewString strings already
    // had (freed together at the next level load, never before), not a new
    // leak risk.
    //
    // std::deque, not std::vector: PR_GetString hands out a raw char* into
    // an element's storage, and a short (SSO) std::string's character
    // buffer lives inside the std::string object itself -- a vector
    // reallocation move-constructs each element into new storage, which
    // relocates that inline buffer and silently invalidates every char*
    // PR_GetString has ever returned for a short dynamic string (confirmed
    // the hard way: real crash, "SV_ModelIndex: model <garbage> not
    // precached", entity precaching read a stale post-reallocation
    // pointer). deque's emplace_back never moves existing elements, so
    // every returned char* stays valid for this table's lifetime.
    std::deque<std::string> pr_dynamic_strings;
}

/*
============
PR_GetString

Resolves any string_t (a static pr_strings offset, or a dynamic handle from
PR_SetString) to its text. Returns "<bad string>" for an out-of-range
dynamic handle rather than indexing pr_dynamic_strings out of bounds --
same defense-in-depth spirit as PR_ValidateOperandTypes, for a malformed
progs.dat or savegame that manufactures a bad string_t some other way.
============
*/
char *PR_GetString(string_t num)
{
    if (num >= 0)
    {
        return pr_strings + num;
    }
    size_t index = static_cast<size_t>(-num - 1);
    if (index >= pr_dynamic_strings.size())
    {
        static char bad[] = "<bad string>";
        return bad;
    }
    return pr_dynamic_strings[index].data();
}

/*
============
PR_SetString

Interns a copy of s as a dynamic string and returns its handle. The only
call site today is ED_ParseEpair (entity string fields from map/savegame
data); pr_string_temp's ftos()/vtos() results are deliberately left on
their existing pointer-difference encoding (see pr_cmds.cpp) -- both
encodings are string_t-compatible since PR_GetString's num >= 0 branch
still handles a raw pr_strings-relative offset exactly as before.
============
*/
string_t PR_SetString(const char *s)
{
    pr_dynamic_strings.emplace_back(s);
    return -static_cast<string_t>(pr_dynamic_strings.size());
}

int type_size[8] = {1, sizeof(string_t) / 4, 1, 3, 1, 1, sizeof(func_t) / 4, 1};

std::optional<ddef_t *> ED_FieldAtOfs(int ofs);
qboolean ED_ParseEpair(void *base, ddef_t *key, const char *s);

cvar_t nomonsters = {"nomonsters", "0"};
cvar_t gamecfg = {"gamecfg", "0"};
cvar_t scratch1 = {"scratch1", "0"};
cvar_t scratch2 = {"scratch2", "0"};
cvar_t scratch3 = {"scratch3", "0"};
cvar_t scratch4 = {"scratch4", "0"};
cvar_t savedgamecfg = {"savedgamecfg", "0", true};
cvar_t saved1 = {"saved1", "0", true};
cvar_t saved2 = {"saved2", "0", true};
cvar_t saved3 = {"saved3", "0", true};
cvar_t saved4 = {"saved4", "0", true};

#define MAX_FIELD_LEN 64
#define GEFV_CACHESIZE 2

struct gefv_cache
{
    ddef_t *pcache;
    char field[MAX_FIELD_LEN];
};

static gefv_cache gefvCache[GEFV_CACHESIZE] = {{nullptr, ""}, {nullptr, ""}};

/*
=================
ED_ClearEdict

Sets everything to nullptr
=================
*/
void ED_ClearEdict(edict_t *e)
{
    memset(&e->v, 0, progs->entityfields * 4);
    e->free = false;
}

/*
=================
ED_Alloc

Either finds a free edict, or allocates a new one.
Try to avoid reusing an entity that was recently freed, because it
can cause the client to think the entity morphed into something else
instead of being removed and recreated, which can cause interpolated
angles and bad trails.
=================
*/
edict_t *ED_Alloc(void)
{
    int i;
    edict_t *e;

    for (i = SV_NumClients() + 1; i < SV_NumEdicts(); i++)
    {
        e = EDICT_NUM(i);
        // the first couple seconds of server time can involve a lot of
        // freeing and allocating, so relax the replacement policy
        if (e->free && (e->freetime < 2 || SV_Time() - e->freetime > 0.5))
        {
            ED_ClearEdict(e);
            return e;
        }
    }

    if (i == MAX_EDICTS)
    {
        Sys_Error("ED_Alloc: no free edicts");
    }

    i = SV_ReserveNextEdictSlot();
    e = EDICT_NUM(i);
    ED_ClearEdict(e);

    return e;
}

/*
=================
ED_Free

Marks the edict as free
FIXME: walk all entities and nullptr out references to this entity
=================
*/
void ED_Free(edict_t *ed)
{
    SV_UnlinkEdict(ed); // unlink from world bsp

    ed->free = true;
    ed->v.model = 0;
    ed->v.takedamage = 0;
    ed->v.modelindex = 0;
    ed->v.colormap = 0;
    ed->v.skin = 0;
    ed->v.frame = 0;
    VectorCopy(vec3_origin, ed->v.origin);
    VectorCopy(vec3_origin, ed->v.angles);
    ed->v.nextthink = -1;
    ed->v.solid = 0;

    ed->freetime = SV_Time();
}

//===========================================================================

/*
============
ED_GlobalAtOfs
============
*/
std::optional<ddef_t *> ED_GlobalAtOfs(int ofs)
{
    ddef_t *def;
    int i;

    for (i = 0; i < progs->numglobaldefs; i++)
    {
        def = &pr_globaldefs[i];
        if (def->ofs == ofs)
        {
            return def;
        }
    }
    return std::nullopt;
}

/*
============
ED_FieldAtOfs
============
*/
std::optional<ddef_t *> ED_FieldAtOfs(int ofs)
{
    ddef_t *def;
    int i;

    for (i = 0; i < progs->numfielddefs; i++)
    {
        def = &pr_fielddefs[i];
        if (def->ofs == ofs)
        {
            return def;
        }
    }
    return std::nullopt;
}

/*
============
ED_FindField
============
*/
std::optional<ddef_t *> ED_FindField(const char *name)
{
    ddef_t *def;
    int i;

    for (i = 0; i < progs->numfielddefs; i++)
    {
        def = &pr_fielddefs[i];
        if (!strcmp(PR_GetString(def->s_name), name))
        {
            return def;
        }
    }
    return std::nullopt;
}

/*
============
ED_FindGlobal
============
*/
std::optional<ddef_t *> ED_FindGlobal(char *name)
{
    ddef_t *def;
    int i;

    for (i = 0; i < progs->numglobaldefs; i++)
    {
        def = &pr_globaldefs[i];
        if (!strcmp(PR_GetString(def->s_name), name))
        {
            return def;
        }
    }
    return std::nullopt;
}

/*
============
ED_FindFunction
============
*/
std::optional<dfunction_t *> ED_FindFunction(const char *name)
{
    dfunction_t *func;
    int i;

    for (i = 0; i < progs->numfunctions; i++)
    {
        func = &pr_functions[i];
        if (!strcmp(PR_GetString(func->s_name), name))
        {
            return func;
        }
    }
    return std::nullopt;
}

std::optional<eval_t *> GetEdictFieldValue(edict_t *ed, const char *field)
{
    ddef_t *def = nullptr;
    int i;
    static int rep = 0;

    for (i = 0; i < GEFV_CACHESIZE; i++)
    {
        if (!strcmp(field, gefvCache[i].field))
        {
            def = gefvCache[i].pcache;
            break;
        }
    }

    if (i == GEFV_CACHESIZE)
    {
        def = ED_FindField(field).value_or(nullptr);

        if (strlen(field) < MAX_FIELD_LEN)
        {
            gefvCache[rep].pcache = def;
            Q_strlcpy(gefvCache[rep].field, field, sizeof(gefvCache[rep].field));
            rep ^= 1;
        }
    }

    if (!def)
    {
        return std::nullopt;
    }

    return reinterpret_cast<eval_t *>(reinterpret_cast<char *>(&ed->v) + def->ofs * 4);
}

/*
============
PR_ValueString

Returns a string describing *data in a type specific manner
=============
*/
std::string PR_ValueString(etype_t type, eval_t *val)
{
    std::string line;
    dfunction_t *f;

    type = static_cast<etype_t>(static_cast<int>(type) & ~DEF_SAVEGLOBAL);

    switch (type)
    {
    case etype_t::ev_string:
        line = PR_GetString(val->string);
        break;
    case etype_t::ev_entity:
        line = std::format("entity {}", NUM_FOR_EDICT(PROG_TO_EDICT(val->edict)));
        break;
    case etype_t::ev_function:
        f = pr_functions + val->function;
        line = std::format("{}()", PR_GetString(f->s_name));
        break;
    case etype_t::ev_field: {
        auto fdef = ED_FieldAtOfs(val->_int);
        line = fdef ? std::format(".{}", PR_GetString((*fdef)->s_name)) : ".<bad field>";
    }
    break;
    case etype_t::ev_void:
        line = "void";
        break;
    case etype_t::ev_float:
        line = std::format("{:5.1f}", val->_float);
        break;
    case etype_t::ev_vector:
        line = std::format("'{:5.1f} {:5.1f} {:5.1f}'", val->vector[0], val->vector[1], val->vector[2]);
        break;
    case etype_t::ev_pointer:
        line = "pointer";
        break;
    default:
        line = std::format("bad type {}", static_cast<int>(type));
        break;
    }

    return line;
}

/*
============
PR_UglyValueString

Returns a string describing *data in a type specific manner
Easier to parse than PR_ValueString
=============
*/
std::string PR_UglyValueString(etype_t type, eval_t *val)
{
    std::string line;
    dfunction_t *f;

    type = static_cast<etype_t>(static_cast<int>(type) & ~DEF_SAVEGLOBAL);

    switch (type)
    {
    case etype_t::ev_string:
        line = PR_GetString(val->string);
        break;
    case etype_t::ev_entity:
        line = std::format("{}", NUM_FOR_EDICT(PROG_TO_EDICT(val->edict)));
        break;
    case etype_t::ev_function:
        f = pr_functions + val->function;
        line = PR_GetString(f->s_name);
        break;
    case etype_t::ev_field: {
        auto fdef = ED_FieldAtOfs(val->_int);
        line = fdef ? std::string(PR_GetString((*fdef)->s_name)) : "<bad field>";
    }
    break;
    case etype_t::ev_void:
        line = "void";
        break;
    case etype_t::ev_float:
        line = std::format("{:f}", val->_float);
        break;
    case etype_t::ev_vector:
        line = std::format("{:f} {:f} {:f}", val->vector[0], val->vector[1], val->vector[2]);
        break;
    default:
        line = std::format("bad type {}", static_cast<int>(type));
        break;
    }

    return line;
}

/*
============
PR_GlobalString

Returns a string with a description and the contents of a global,
padded to 20 field width
============
*/
std::string PR_GlobalString(int ofs)
{
    eval_t *val;
    std::string line;

    val = reinterpret_cast<eval_t *>(&pr_globals[ofs]);
    auto def = ED_GlobalAtOfs(ofs);
    if (!def)
    {
        line = std::format("{}(???)", ofs);
    }
    else
    {
        line = std::format("{}({}){}", ofs, PR_GetString((*def)->s_name),
                            PR_ValueString(static_cast<etype_t>((*def)->type), val));
    }

    while (line.size() < 20)
    {
        line += ' ';
    }
    line += ' ';

    return line;
}

std::string PR_GlobalStringNoContents(int ofs)
{
    std::string line;

    auto def = ED_GlobalAtOfs(ofs);
    if (!def)
    {
        line = std::format("{}(???)", ofs);
    }
    else
    {
        line = std::format("{}({})", ofs, PR_GetString((*def)->s_name));
    }

    while (line.size() < 20)
    {
        line += ' ';
    }
    line += ' ';

    return line;
}

/*
=============
ED_Print

For debugging
=============
*/
void ED_Print(edict_t *ed)
{
    int l;
    ddef_t *d;
    int *v;
    int i, j;
    char *name;
    int type;

    if (ed->free)
    {
        Con_Printf("FREE\n");
        return;
    }

    Con_Printf("\nEDICT {}:\n", NUM_FOR_EDICT(ed));
    for (i = 1; i < progs->numfielddefs; i++)
    {
        d = &pr_fielddefs[i];
        name = PR_GetString(d->s_name);
        if (name[strlen(name) - 2] == '_')
        {
            continue; // skip _x, _y, _z vars
        }

        v = reinterpret_cast<int *>(reinterpret_cast<char *>(&ed->v) + d->ofs * 4);

        // if the value is still all 0, skip the field
        type = d->type & ~DEF_SAVEGLOBAL;

        for (j = 0; j < type_size[type]; j++)
        {
            if (v[j])
            {
                break;
            }
        }
        if (j == type_size[type])
        {
            continue;
        }

        Con_Printf("{}", name);
        l = (int)strlen(name);
        while (l++ < 15)
        {
            Con_Printf(" ");
        }

        Con_Printf("{}\n", PR_ValueString(static_cast<etype_t>(d->type), reinterpret_cast<eval_t *>(v)));
    }
}

/*
=============
ED_Write

For savegames
=============
*/
void ED_Write(std::ofstream &f, edict_t *ed)
{
    ddef_t *d;
    int *v;
    int i, j;
    char *name;
    int type;

    f << "{\n";

    if (ed->free)
    {
        f << "}\n";
        return;
    }

    for (i = 1; i < progs->numfielddefs; i++)
    {
        d = &pr_fielddefs[i];
        name = PR_GetString(d->s_name);
        if (name[strlen(name) - 2] == '_')
        {
            continue; // skip _x, _y, _z vars
        }

        v = reinterpret_cast<int *>(reinterpret_cast<char *>(&ed->v) + d->ofs * 4);

        // if the value is still all 0, skip the field
        type = d->type & ~DEF_SAVEGLOBAL;
        for (j = 0; j < type_size[type]; j++)
        {
            if (v[j])
            {
                break;
            }
        }
        if (j == type_size[type])
        {
            continue;
        }

        f << std::format("\"{}\" ", name);
        f << std::format("\"{}\"\n", PR_UglyValueString(static_cast<etype_t>(d->type), reinterpret_cast<eval_t *>(v)));
    }

    f << "}\n";
}

void ED_PrintNum(int ent)
{
    ED_Print(EDICT_NUM(ent));
}

/*
=============
ED_PrintEdicts

For debugging, prints all the entities in the current server
=============
*/
void ED_PrintEdicts(void)
{
    int i;

    Con_Printf("{} entities\n", SV_NumEdicts());
    for (i = 0; i < SV_NumEdicts(); i++)
    {
        ED_PrintNum(i);
    }
}

/*
=============
ED_PrintEdict_f

For debugging, prints a single edicy
=============
*/
void ED_PrintEdict_f(void)
{
    int i;

    i = Q_atoi(Cmd_Argv(1));
    if (i >= SV_NumEdicts())
    {
        Con_Printf("Bad edict number\n");
        return;
    }
    ED_PrintNum(i);
}

/*
=============
ED_Count

For debugging
=============
*/
void ED_Count(void)
{
    int i;
    edict_t *ent;
    int active, models, solid, step;

    active = models = solid = step = 0;
    for (i = 0; i < SV_NumEdicts(); i++)
    {
        ent = EDICT_NUM(i);
        if (ent->free)
        {
            continue;
        }
        active++;
        if (ent->v.solid)
        {
            solid++;
        }
        if (ent->v.model)
        {
            models++;
        }
        if (ent->v.movetype == MOVETYPE_STEP)
        {
            step++;
        }
    }

    Con_Printf("num_edicts:{:3}\n", SV_NumEdicts());
    Con_Printf("active    :{:3}\n", active);
    Con_Printf("view      :{:3}\n", models);
    Con_Printf("touch     :{:3}\n", solid);
    Con_Printf("step      :{:3}\n", step);
}

/*
==============================================================================

                    ARCHIVING GLOBALS

FIXME: need to tag constants, doesn't really work
==============================================================================
*/

/*
=============
ED_WriteGlobals
=============
*/
void ED_WriteGlobals(std::ofstream &f)
{
    ddef_t *def;
    int i;
    char *name;
    int type;

    f << "{\n";
    for (i = 0; i < progs->numglobaldefs; i++)
    {
        def = &pr_globaldefs[i];
        type = def->type;
        if (!(def->type & DEF_SAVEGLOBAL))
        {
            continue;
        }
        type &= ~DEF_SAVEGLOBAL;

        if (static_cast<etype_t>(type) != etype_t::ev_string && static_cast<etype_t>(type) != etype_t::ev_float &&
            static_cast<etype_t>(type) != etype_t::ev_entity)
        {
            continue;
        }

        name = PR_GetString(def->s_name);
        f << std::format("\"{}\" ", name);
        f << std::format("\"{}\"\n",
                          PR_UglyValueString(static_cast<etype_t>(type), reinterpret_cast<eval_t *>(&pr_globals[def->ofs])));
    }
    f << "}\n";
}

/*
=============
ED_ParseGlobals
=============
*/
void ED_ParseGlobals(const char *data)
{
    char keyname[64];
    std::optional<ddef_t *> key;

    while (1)
    {
        // parse key
        data = COM_Parse(data);
        if (com_token[0] == '}')
        {
            break;
        }
        if (!data)
        {
            Sys_Error("ED_ParseEntity: EOF without closing brace");
        }

        Q_strlcpy(keyname, com_token, sizeof(keyname));

        // parse value
        data = COM_Parse(data);
        if (!data)
        {
            Sys_Error("ED_ParseEntity: EOF without closing brace");
        }

        if (com_token[0] == '}')
        {
            Sys_Error("ED_ParseEntity: closing brace without data");
        }

        key = ED_FindGlobal(keyname);
        if (!key)
        {
            Con_Printf("'{}' is not a global\n", keyname);
            continue;
        }

        if (!ED_ParseEpair(static_cast<void *>(pr_globals), *key, com_token))
        {
            Host_Error("ED_ParseGlobals: parse error");
        }
    }
}

//============================================================================

/*
=============
ED_NewString
=============
*/
char *ED_NewString(const char *string)
{
    char *newstr, *new_p;
    int i, l;

    l = (int)strlen(string) + 1;
    newstr = static_cast<char *>(Hunk_Alloc(l));
    new_p = newstr;

    for (i = 0; i < l; i++)
    {
        if (string[i] == '\\' && i < l - 1)
        {
            i++;
            if (string[i] == 'n')
            {
                *new_p++ = '\n';
            }
            else
            {
                *new_p++ = '\\';
            }
        }
        else
        {
            *new_p++ = string[i];
        }
    }

    return newstr;
}

/*
=============
ED_ParseEval

Can parse either fields or globals
returns false if error
=============
*/
qboolean ED_ParseEpair(void *base, ddef_t *key, const char *s)
{
    int i;
    char string[128];
    char *v, *w;
    void *d;

    d = static_cast<void *>(static_cast<int *>(base) + key->ofs);

    switch (static_cast<etype_t>(key->type & ~DEF_SAVEGLOBAL))
    {
    case etype_t::ev_string:
        *static_cast<string_t *>(d) = PR_SetString(ED_NewString(s));
        break;

    case etype_t::ev_float:
        *static_cast<float *>(d) = atof(s);
        break;

    case etype_t::ev_vector:
        Q_strlcpy(string, s, sizeof(string));
        v = string;
        w = string;
        for (i = 0; i < 3; i++)
        {
            while (*v && *v != ' ')
            {
                v++;
            }
            *v = 0;
            (static_cast<float *>(d))[i] = atof(w);
            w = v = v + 1;
        }
        break;

    case etype_t::ev_entity:
        *static_cast<int *>(d) = EDICT_TO_PROG(EDICT_NUM(atoi(s)));
        break;

    case etype_t::ev_field: {
        std::optional<ddef_t *> def = ED_FindField(s);
        if (!def)
        {
            Con_Printf("Can't find field {}\n", s);
            return false;
        }
        *static_cast<int *>(d) = G_INT((*def)->ofs);
    }
    break;

    case etype_t::ev_function: {
        std::optional<dfunction_t *> func = ED_FindFunction(s);
        if (!func)
        {
            Con_Printf("Can't find function {}\n", s);
            return false;
        }
        *static_cast<func_t *>(d) = *func - pr_functions;
    }
    break;

    default:
        break;
    }
    return true;
}

/*
====================
ED_ParseEdict

Parses an edict out of the given string, returning the new position
ed should be a properly initialized empty edict.
Used for initial level load and for savegames.
====================
*/
const char *ED_ParseEdict(const char *data, edict_t *ent)
{
    std::optional<ddef_t *> key;
    qboolean anglehack;
    qboolean init;
    char keyname[256];
    int n;

    init = false;

    // clear it
    if (ent != SV_EdictsBase()) // hack
    {
        memset(&ent->v, 0, progs->entityfields * 4);
    }

    // go through all the dictionary pairs
    while (1)
    {
        // parse key
        data = COM_Parse(data);
        if (com_token[0] == '}')
        {
            break;
        }
        if (!data)
        {
            Sys_Error("ED_ParseEntity: EOF without closing brace");
        }

        // anglehack is to allow QuakeEd to write single scalar angles
        // and allow them to be turned into vectors. (FIXME...)
        if (!strcmp(com_token, "angle"))
        {
            Q_strlcpy(com_token, "angles", sizeof(com_token));
            anglehack = true;
        }
        else
        {
            anglehack = false;
        }

        // FIXME: change light to _light to get rid of this hack
        if (!strcmp(com_token, "light"))
        {
            Q_strlcpy(com_token, "light_lev", sizeof(com_token)); // hack for single light def
        }

        Q_strlcpy(keyname, com_token, sizeof(keyname));

        // another hack to fix heynames with trailing spaces
        n = (int)strlen(keyname);
        while (n && keyname[n - 1] == ' ')
        {
            keyname[n - 1] = 0;
            n--;
        }

        // parse value
        data = COM_Parse(data);
        if (!data)
        {
            Sys_Error("ED_ParseEntity: EOF without closing brace");
        }

        if (com_token[0] == '}')
        {
            Sys_Error("ED_ParseEntity: closing brace without data");
        }

        init = true;

        // keynames with a leading underscore are used for utility comments,
        // and are immediately discarded by quake
        if (keyname[0] == '_')
        {
            continue;
        }

        key = ED_FindField(keyname);
        if (!key)
        {
            Con_Printf("'{}' is not a field\n", keyname);
            continue;
        }

        if (anglehack)
        {
            std::string temp = std::format("0 {} 0", com_token);
            Q_strlcpy(com_token, temp.c_str(), sizeof(com_token));
        }

        if (!ED_ParseEpair(static_cast<void *>(&ent->v), *key, com_token))
        {
            Host_Error("ED_ParseEdict: parse error");
        }
    }

    if (!init)
    {
        ent->free = true;
    }

    return data;
}

/*
================
ED_LoadFromFile

The entities are directly placed in the array, rather than allocated with
ED_Alloc, because otherwise an error loading the map would have entity
number references out of order.

Creates a server's entity / program execution context by
parsing textual entity definitions out of an ent file.

Used for both fresh maps and savegame loads.  A fresh map would also need
to call ED_CallSpawnFunctions () to let the objects initialize themselves.
================
*/
void ED_LoadFromFile(const char *data)
{
    edict_t *ent;
    int inhibit;
    std::optional<dfunction_t *> func;

    ent = nullptr;
    inhibit = 0;
    pr_global_struct->time = SV_Time();

    // parse ents
    while (1)
    {
        // parse the opening brace
        data = COM_Parse(data);
        if (!data)
        {
            break;
        }
        if (com_token[0] != '{')
        {
            Sys_Error("ED_LoadFromFile: found {} when expecting {{", com_token);
        }

        if (!ent)
        {
            ent = EDICT_NUM(0);
        }
        else
        {
            ent = ED_Alloc();
        }
        data = ED_ParseEdict(data, ent);

        // remove things from different skill levels or deathmatch
        if (deathmatch.value)
        {
            if (((int)ent->v.spawnflags & SPAWNFLAG_NOT_DEATHMATCH))
            {
                ED_Free(ent);
                inhibit++;
                continue;
            }
        }
        else if ((current_skill == 0 && ((int)ent->v.spawnflags & SPAWNFLAG_NOT_EASY)) ||
                 (current_skill == 1 && ((int)ent->v.spawnflags & SPAWNFLAG_NOT_MEDIUM)) ||
                 (current_skill >= 2 && ((int)ent->v.spawnflags & SPAWNFLAG_NOT_HARD)))
        {
            ED_Free(ent);
            inhibit++;
            continue;
        }

        //
        // immediately call spawn function
        //
        if (!ent->v.classname)
        {
            Con_Printf("No classname for:\n");
            ED_Print(ent);
            ED_Free(ent);
            continue;
        }

        // look for the spawn function
        func = ED_FindFunction(PR_GetString(ent->v.classname));

        if (!func)
        {
            Con_Printf("No spawn function for:\n");
            ED_Print(ent);
            ED_Free(ent);
            continue;
        }

        pr_global_struct->self = EDICT_TO_PROG(ent);
        PR_ExecuteProgram(*func - pr_functions);
    }

    Con_DPrintf("{} entities inhibited\n", inhibit);
}

namespace
{
    // Global-pool offsets whose etype_t is a trustworthy static fact for the
    // whole program, built from pr_globaldefs. Deliberately covers only
    // entity/string/function/field: a wrong operand of one of these types
    // becomes a bad pointer or a bad lookup (PR_FieldAddress, ED_FindFunction,
    // a raw pr_strings offset) -- the family where a type mismatch is a real
    // safety issue, not just wrong arithmetic.
    //
    // ev_float/ev_vector are deliberately excluded: verified empirically
    // against id1's own compiled progs.dat that qcc's real output reuses a
    // single global-pool offset for an ev_vector def and an unrelated
    // ev_float def at different points in a function's temporary-value
    // lifetime (126 such offsets exist in the shipped file) -- a static
    // single-type map for that family would reject legitimate, unmodified
    // id1 bytecode. No such collision exists for entity/string/function/
    // field in the shipped file (confirmed the same way).
    std::unordered_map<int, etype_t> PR_BuildOperandTypeMap()
    {
        std::unordered_map<int, etype_t> types;
        for (int i = 0; i < progs->numglobaldefs; i++)
        {
            etype_t type = static_cast<etype_t>(pr_globaldefs[i].type & ~DEF_SAVEGLOBAL);
            if (type == etype_t::ev_entity || type == etype_t::ev_string || type == etype_t::ev_function ||
                type == etype_t::ev_field)
            {
                types[pr_globaldefs[i].ofs] = type;
            }
        }
        return types;
    }
} // namespace

/*
===============
PR_ValidateOperandTypes

Defense-in-depth check for a hand-edited/malformed progs.dat: cross-
references every opcode's entity/string/function/field-typed operand
slots against progs.dat's own pr_globaldefs reflection data, and rejects
the file if any disagree. A temp/local slot with no reflection entry
(most of them -- only ~40% of operand slots in id1's real progs.dat have
one at all) is unverifiable and treated as fine; this is a best-effort
net; it doesn't touch PR_ExecuteProgram's hot path at all -- runs once,
here, at load time. Given external linkage (declared in progs.h) rather
than folded into PR_LoadProgs, purely so tests can drive it directly
against hand-built synthetic data instead of a real binary progs.dat file.
===============
*/
void PR_ValidateOperandTypes(void)
{
    auto globalTypes = PR_BuildOperandTypeMap();

    auto check = [&](int statementIndex, int ofs, etype_t expected) {
        auto it = globalTypes.find(ofs);
        if (it == globalTypes.end())
        {
            return;
        }
        if (it->second != expected)
        {
            Sys_Error(
                "PR_LoadProgs: statement {} expects a type-{} operand at global offset {}, but progs.dat's "
                "own reflection data says it's type {} -- progs.dat appears malformed",
                statementIndex, static_cast<int>(expected), ofs, static_cast<int>(it->second));
        }
    };

    for (int i = 0; i < progs->numstatements; i++)
    {
        dstatement_t &st = pr_statements[i];
        switch (st.op)
        {
        case OP_EQ_S:
        case OP_NE_S:
        case OP_STORE_S:
            check(i, st.a, etype_t::ev_string);
            check(i, st.b, etype_t::ev_string);
            break;
        case OP_EQ_E:
        case OP_NE_E:
        case OP_STORE_ENT:
            check(i, st.a, etype_t::ev_entity);
            check(i, st.b, etype_t::ev_entity);
            break;
        case OP_EQ_FNC:
        case OP_NE_FNC:
        case OP_STORE_FNC:
            check(i, st.a, etype_t::ev_function);
            check(i, st.b, etype_t::ev_function);
            break;
        case OP_STORE_FLD:
            check(i, st.a, etype_t::ev_field);
            check(i, st.b, etype_t::ev_field);
            break;
        case OP_LOAD_F:
        case OP_LOAD_V:
        case OP_ADDRESS:
            check(i, st.a, etype_t::ev_entity);
            check(i, st.b, etype_t::ev_field);
            break;
        case OP_LOAD_S:
            check(i, st.a, etype_t::ev_entity);
            check(i, st.b, etype_t::ev_field);
            check(i, st.c, etype_t::ev_string);
            break;
        case OP_LOAD_ENT:
            check(i, st.a, etype_t::ev_entity);
            check(i, st.b, etype_t::ev_field);
            check(i, st.c, etype_t::ev_entity);
            break;
        case OP_LOAD_FLD:
            check(i, st.a, etype_t::ev_entity);
            check(i, st.b, etype_t::ev_field);
            check(i, st.c, etype_t::ev_field);
            break;
        case OP_LOAD_FNC:
            check(i, st.a, etype_t::ev_entity);
            check(i, st.b, etype_t::ev_field);
            check(i, st.c, etype_t::ev_function);
            break;
        case OP_STOREP_S:
            check(i, st.a, etype_t::ev_string);
            break;
        case OP_STOREP_ENT:
            check(i, st.a, etype_t::ev_entity);
            break;
        case OP_STOREP_FLD:
            check(i, st.a, etype_t::ev_field);
            break;
        case OP_STOREP_FNC:
            check(i, st.a, etype_t::ev_function);
            break;
        case OP_NOT_S:
            check(i, st.a, etype_t::ev_string);
            break;
        case OP_NOT_ENT:
            check(i, st.a, etype_t::ev_entity);
            break;
        case OP_NOT_FNC:
            check(i, st.a, etype_t::ev_function);
            break;
        case OP_CALL0:
        case OP_CALL1:
        case OP_CALL2:
        case OP_CALL3:
        case OP_CALL4:
        case OP_CALL5:
        case OP_CALL6:
        case OP_CALL7:
        case OP_CALL8:
            check(i, st.a, etype_t::ev_function);
            break;
        case OP_STATE:
            check(i, st.b, etype_t::ev_function);
            break;
        default:
            // Every other opcode's typed operands are ev_float/ev_vector
            // (arithmetic/comparison -- not worth this check's memory-
            // safety guarantee), or aren't global-pool value slots at all
            // (OP_GOTO's branch offset, OP_RETURN/OP_DONE's polymorphic
            // return value, OP_STORE_V/OP_STOREP_V's use as a generic
            // 3-word bulk copy regardless of nominal type -- a real qcc
            // code-gen quirk, confirmed against id1's progs.dat).
            break;
        }
    }
}

/*
===============
PR_LoadProgs
===============
*/
void PR_LoadProgs(void)
{
    int i;

    // flush the non-C variable lookup cache
    for (i = 0; i < GEFV_CACHESIZE; i++)
    {
        gefvCache[i].field[0] = 0;
    }

    pr_dynamic_strings.clear();

    CRC_Init(&pr_crc);

    progs = reinterpret_cast<dprograms_t *>(COM_LoadHunkFile("progs.dat"));
    if (!progs)
    {
        Sys_Error("PR_LoadProgs: couldn't load progs.dat");
    }
    Con_DPrintf("Programs occupy {}K.\n", com_filesize / 1024);

    for (i = 0; i < com_filesize; i++)
    {
        CRC_ProcessByte(&pr_crc, (reinterpret_cast<byte *>(progs))[i]);
    }

    // byte swap the header
    for (i = 0; i < sizeof(*progs) / 4; i++)
    {
        (reinterpret_cast<int *>(progs))[i] = LittleLong((reinterpret_cast<int *>(progs))[i]);
    }

    if (progs->version != PROG_VERSION)
    {
        Sys_Error("progs.dat has wrong version number ({} should be {})", progs->version, PROG_VERSION);
    }
    if (progs->crc != PROGHEADER_CRC)
    {
        Sys_Error("progs.dat system vars have been modified, progdefs.h is out of date");
    }

    pr_functions = reinterpret_cast<dfunction_t *>(reinterpret_cast<byte *>(progs) + progs->ofs_functions);
    pr_strings = reinterpret_cast<char *>(progs) + progs->ofs_strings;
    pr_globaldefs = reinterpret_cast<ddef_t *>(reinterpret_cast<byte *>(progs) + progs->ofs_globaldefs);
    pr_fielddefs = reinterpret_cast<ddef_t *>(reinterpret_cast<byte *>(progs) + progs->ofs_fielddefs);
    pr_statements = reinterpret_cast<dstatement_t *>(reinterpret_cast<byte *>(progs) + progs->ofs_statements);

    pr_global_struct = reinterpret_cast<globalvars_t *>(reinterpret_cast<byte *>(progs) + progs->ofs_globals);
    pr_globals = reinterpret_cast<float *>(pr_global_struct);

    // Allocate temp string buffer in the hunk so pr_string_temp - pr_strings fits in int on x64.
    // On x64 a static/BSS buffer would be in a different memory region from the hunk, making the
    // pointer difference gigabytes wide and causing truncation when stored as string_t (int).
    pr_string_temp = static_cast<char *>(Hunk_Alloc(128));

    pr_edict_size = progs->entityfields * 4 + sizeof(edict_t) - sizeof(entvars_t);

    // byte swap the lumps
    for (i = 0; i < progs->numstatements; i++)
    {
        pr_statements[i].op = LittleShort(pr_statements[i].op);
        pr_statements[i].a = LittleShort(pr_statements[i].a);
        pr_statements[i].b = LittleShort(pr_statements[i].b);
        pr_statements[i].c = LittleShort(pr_statements[i].c);
    }

    for (i = 0; i < progs->numfunctions; i++)
    {
        pr_functions[i].first_statement = LittleLong(pr_functions[i].first_statement);
        pr_functions[i].parm_start = LittleLong(pr_functions[i].parm_start);
        pr_functions[i].s_name = LittleLong(pr_functions[i].s_name);
        pr_functions[i].s_file = LittleLong(pr_functions[i].s_file);
        pr_functions[i].numparms = LittleLong(pr_functions[i].numparms);
        pr_functions[i].locals = LittleLong(pr_functions[i].locals);
    }

    for (i = 0; i < progs->numglobaldefs; i++)
    {
        pr_globaldefs[i].type = LittleShort(pr_globaldefs[i].type);
        pr_globaldefs[i].ofs = LittleShort(pr_globaldefs[i].ofs);
        pr_globaldefs[i].s_name = LittleLong(pr_globaldefs[i].s_name);
    }

    for (i = 0; i < progs->numfielddefs; i++)
    {
        pr_fielddefs[i].type = LittleShort(pr_fielddefs[i].type);
        if (pr_fielddefs[i].type & DEF_SAVEGLOBAL)
        {
            Sys_Error("PR_LoadProgs: pr_fielddefs[i].type & DEF_SAVEGLOBAL");
        }
        pr_fielddefs[i].ofs = LittleShort(pr_fielddefs[i].ofs);
        pr_fielddefs[i].s_name = LittleLong(pr_fielddefs[i].s_name);
    }

    for (i = 0; i < progs->numglobals; i++)
    {
        (reinterpret_cast<int *>(pr_globals))[i] = LittleLong((reinterpret_cast<int *>(pr_globals))[i]);
    }

    PR_ValidateOperandTypes();
}

/*
===============
PR_Init
===============
*/
void PR_Init(void)
{
    Cmd_AddCommand("edict", ED_PrintEdict_f);
    Cmd_AddCommand("edicts", ED_PrintEdicts);
    Cmd_AddCommand("edictcount", ED_Count);
    Cmd_AddCommand("profile", PR_Profile_f);
    Cvar_RegisterVariable(&nomonsters);
    Cvar_RegisterVariable(&gamecfg);
    Cvar_RegisterVariable(&scratch1);
    Cvar_RegisterVariable(&scratch2);
    Cvar_RegisterVariable(&scratch3);
    Cvar_RegisterVariable(&scratch4);
    Cvar_RegisterVariable(&savedgamecfg);
    Cvar_RegisterVariable(&saved1);
    Cvar_RegisterVariable(&saved2);
    Cvar_RegisterVariable(&saved3);
    Cvar_RegisterVariable(&saved4);
}

edict_t *EDICT_NUM(int n)
{
    if (n < 0 || n >= SV_MaxEdicts())
    {
        Sys_Error("EDICT_NUM: bad number {}", n);
    }
    return reinterpret_cast<edict_t *>(reinterpret_cast<byte *>(SV_EdictsBase()) + (n)*pr_edict_size);
}

int NUM_FOR_EDICT(edict_t *e)
{
    int b;

    b = reinterpret_cast<byte *>(e) - reinterpret_cast<byte *>(SV_EdictsBase());
    b = b / pr_edict_size;

    if (b < 0 || b >= SV_NumEdicts())
    {
        Sys_Error("NUM_FOR_EDICT: bad pointer");
    }
    return b;
}

edict_t *PROG_TO_EDICT(int prog)
{
    if (prog < 0 || prog >= SV_MaxEdicts() * pr_edict_size)
    {
        Sys_Error("PROG_TO_EDICT: bad prog offset {}", prog);
    }
    return reinterpret_cast<edict_t *>(reinterpret_cast<byte *>(SV_EdictsBase()) + prog);
}

int EDICT_TO_PROG(edict_t *e)
{
    int b = reinterpret_cast<byte *>(e) - reinterpret_cast<byte *>(SV_EdictsBase());
    if (b < 0 || b >= SV_MaxEdicts() * pr_edict_size)
    {
        Sys_Error("EDICT_TO_PROG: bad edict pointer");
    }
    return b;
}

edict_t *G_EDICT(int ofs)
{
    return PROG_TO_EDICT(*reinterpret_cast<int *>(&pr_globals[ofs]));
}

int G_EDICTNUM(int ofs)
{
    return NUM_FOR_EDICT(G_EDICT(ofs));
}

edict_t *NEXT_EDICT(edict_t *e)
{
    edict_t *n = reinterpret_cast<edict_t *>(reinterpret_cast<byte *>(e) + pr_edict_size);
    int b = reinterpret_cast<byte *>(n) - reinterpret_cast<byte *>(SV_EdictsBase());
    if (b < 0 || b > SV_MaxEdicts() * pr_edict_size)
    {
        Sys_Error("NEXT_EDICT: walked off the edict array");
    }
    return n;
}
