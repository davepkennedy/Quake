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
// cvar.c -- dynamic variable tracking

#include "quakedef.h"

// Ordered so Cvar_CompleteVariable can prefix-search with lower_bound
// instead of a linear scan; holds non-owning pointers to the cvar_t
// globals declared throughout the codebase (same ownership model as the
// linked list this replaced -- nothing here allocates or frees a cvar_t).
std::map<std::string, cvar_t *> cvar_vars;
const char *cvar_null_string = "";

/*
============
Cvar_FindVar
============
*/
std::optional<cvar_t *> Cvar_FindVar(const char *var_name)
{
    auto it = cvar_vars.find(var_name);
    if (it == cvar_vars.end())
    {
        return std::nullopt;
    }
    return it->second;
}

/*
============
Cvar_VariableValue
============
*/
float Cvar_VariableValue(const char *var_name)
{
    auto var = Cvar_FindVar(var_name);
    if (!var)
    {
        return 0;
    }
    return Q_atof((*var)->string.c_str());
}

/*
============
Cvar_VariableString
============
*/
std::string Cvar_VariableString(const char *var_name)
{
    auto var = Cvar_FindVar(var_name);
    if (!var)
    {
        return cvar_null_string;
    }
    return (*var)->string;
}

/*
============
Cvar_CompleteVariable
============
*/
std::optional<std::string> Cvar_CompleteVariable(const char *partial)
{
    size_t len = Q_strlen(partial);

    if (!len)
    {
        return std::nullopt;
    }

    auto it = cvar_vars.lower_bound(partial);
    if (it != cvar_vars.end() && it->first.compare(0, len, partial) == 0)
    {
        return it->second->name;
    }

    return std::nullopt;
}

/*
============
Cvar_NextServerVar

Iterator-based replacement for net_dgrm.cpp's old raw cvar_vars/->next walk
(the CCREQ_RULE_INFO LAN "server rules" query, which enumerates .server
cvars one at a time by asking "give me the one after this name"). Passing
nullptr or "" starts from the beginning; returns nullptr once exhausted.
============
*/
cvar_t *Cvar_NextServerVar(const char *afterName)
{
    auto it = cvar_vars.begin();

    if (afterName && afterName[0])
    {
        it = cvar_vars.find(afterName);
        if (it == cvar_vars.end())
        {
            return nullptr; // unknown name -- matches the old Cvar_FindVar-fails-returns-nullptr behavior
        }
        ++it;
    }

    for (; it != cvar_vars.end(); ++it)
    {
        if (it->second->server)
        {
            return it->second;
        }
    }

    return nullptr;
}

/*
============
Cvar_Set
============
*/
void Cvar_Set(const char *var_name, const char *value)
{
    qboolean changed;

    auto found = Cvar_FindVar(var_name);
    if (!found)
    { // there is an error in C code if this happens
        Con_Printf("Cvar_Set: variable %s not found\n", var_name);
        return;
    }
    cvar_t *var = *found;

    changed = var->string != value;

    var->string = value;
    var->value = Q_atof(var->string.c_str());
    if (var->server && changed)
    {
        if (sv.active)
        {
            SV_BroadcastPrintf("\"%s\" changed to \"%s\"\n", var->name, var->string.c_str());
        }
    }
}

/*
============
Cvar_SetValue
============
*/
void Cvar_SetValue(const char *var_name, float value)
{
    // {:f} matches printf's %f exactly (fixed 6 decimal places); a bare {}
    // would use std::format's shortest-round-trip float representation
    // instead ("200" instead of "200.000000"), silently changing what
    // gets written into this cvar's serialized string value.
    Cvar_Set(var_name, va("{:f}", value));
}

/*
============
Cvar_RegisterVariable

Adds a freestanding variable to the variable list.
============
*/
void Cvar_RegisterVariable(cvar_t *variable)
{
    // first check to see if it has allready been defined
    if (Cvar_FindVar(variable->name))
    {
        Con_Printf("Can't register variable %s, allready defined\n", variable->name);
        return;
    }

    // check for overlap with a command
    if (Cmd_Exists(variable->name))
    {
        Con_Printf("Cvar_RegisterVariable: %s is a command\n", variable->name);
        return;
    }

    variable->value = Q_atof(variable->string.c_str());

    cvar_vars[variable->name] = variable;
}

/*
============
Cvar_Command

Handles variable inspection and changing from the console
============
*/
qboolean Cvar_Command(void)
{
    // check variables
    auto found = Cvar_FindVar(Cmd_Argv(0));
    if (!found)
    {
        return false;
    }
    cvar_t *v = *found;

    // perform a variable print or set
    if (Cmd_Argc() == 1)
    {
        Con_Printf("\"%s\" is \"%s\"\n", v->name, v->string.c_str());
        return true;
    }

    Cvar_Set(v->name, Cmd_Argv(1));
    return true;
}

/*
============
Cvar_WriteVariables

Writes lines containing "set variable value" for all variables
with the archive flag set to true.
============
*/
void Cvar_WriteVariables(FILE *f)
{
    for (const auto &[name, var] : cvar_vars)
    {
        if (var->archive)
        {
            fprintf(f, "%s \"%s\"\n", var->name, var->string.c_str());
        }
    }
}
