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

/*

*/

pr_vm_state_t pr_vm;

const char *pr_opnames[] = {"DONE",

                            "MUL_F",    "MUL_V",    "MUL_FV",   "MUL_VF",

                            "DIV",

                            "ADD_F",    "ADD_V",

                            "SUB_F",    "SUB_V",

                            "EQ_F",     "EQ_V",     "EQ_S",     "EQ_E",       "EQ_FNC",

                            "NE_F",     "NE_V",     "NE_S",     "NE_E",       "NE_FNC",

                            "LE",       "GE",       "LT",       "GT",

                            "INDIRECT", "INDIRECT", "INDIRECT", "INDIRECT",   "INDIRECT",   "INDIRECT",

                            "ADDRESS",

                            "STORE_F",  "STORE_V",  "STORE_S",  "STORE_ENT",  "STORE_FLD",  "STORE_FNC",

                            "STOREP_F", "STOREP_V", "STOREP_S", "STOREP_ENT", "STOREP_FLD", "STOREP_FNC",

                            "RETURN",

                            "NOT_F",    "NOT_V",    "NOT_S",    "NOT_ENT",    "NOT_FNC",

                            "IF",       "IFNOT",

                            "CALL0",    "CALL1",    "CALL2",    "CALL3",      "CALL4",      "CALL5",
                            "CALL6",    "CALL7",    "CALL8",

                            "STATE",

                            "GOTO",

                            "AND",      "OR",

                            "BITAND",   "BITOR"};

std::string PR_GlobalString(int ofs);
std::string PR_GlobalStringNoContents(int ofs);

//=============================================================================

/*
=================
PR_PrintStatement
=================
*/
void PR_PrintStatement(dstatement_t *s)
{
    int i;

    if ((unsigned)s->op < sizeof(pr_opnames) / sizeof(pr_opnames[0]))
    {
        Con_Printf("{} ", pr_opnames[s->op]);
        i = (int)strlen(pr_opnames[s->op]);
        for (; i < 10; i++)
        {
            Con_Printf(" ");
        }
    }

    if (s->op == OP_IF || s->op == OP_IFNOT)
    {
        Con_Printf("{}branch {}", PR_GlobalString(s->a), s->b);
    }
    else if (s->op == OP_GOTO)
    {
        Con_Printf("branch {}", s->a);
    }
    else if ((unsigned)(s->op - OP_STORE_F) < 6)
    {
        Con_Printf("{}", PR_GlobalString(s->a));
        Con_Printf("{}", PR_GlobalStringNoContents(s->b));
    }
    else
    {
        if (s->a)
        {
            Con_Printf("{}", PR_GlobalString(s->a));
        }
        if (s->b)
        {
            Con_Printf("{}", PR_GlobalString(s->b));
        }
        if (s->c)
        {
            Con_Printf("{}", PR_GlobalStringNoContents(s->c));
        }
    }
    Con_Printf("\n");
}

/*
============
PR_StackTrace
============
*/
void PR_StackTrace(void)
{
    dfunction_t *f;
    int i;

    if (pr_vm.depth == 0)
    {
        Con_Printf("<NO STACK>\n");
        return;
    }

    pr_vm.stack[pr_vm.depth].f = pr_vm.xfunction;
    for (i = pr_vm.depth; i >= 0; i--)
    {
        f = pr_vm.stack[i].f;

        if (!f)
        {
            Con_Printf("<NO FUNCTION>\n");
        }
        else
        {
            Con_Printf("{:>12} : {}\n", PR_GetString(f->s_file), PR_GetString(f->s_name));
        }
    }
}

/*
============
PR_Profile_f

============
*/
void PR_Profile_f(void)
{
    dfunction_t *f, *best;
    int max;
    int num;
    int i;

    num = 0;
    do
    {
        max = 0;
        best = nullptr;
        for (i = 0; i < progs->numfunctions; i++)
        {
            f = &pr_functions[i];
            if (f->profile > max)
            {
                max = f->profile;
                best = f;
            }
        }
        if (best)
        {
            if (num < 10)
            {
                Con_Printf("{:7} {}\n", best->profile, PR_GetString(best->s_name));
            }
            num++;
            best->profile = 0;
        }
    } while (best);
}

/*
============
PR_RunError

Aborts the currently executing function
============
*/
[[noreturn]] void PR_RunErrorImpl(const std::string &error)
{
    PR_PrintStatement(pr_statements + pr_vm.xstatement);
    PR_StackTrace();
    Con_Printf("{}\n", error);

    pr_vm.depth = 0; // dump the stack so host_error can shutdown functions

    Host_Error("Program error");
}

/*
============================================================================
PR_ExecuteProgram

The interpretation main loop
============================================================================
*/

/*
====================
PR_EnterFunction

Returns the new program statement counter
====================
*/
int PR_EnterFunction(dfunction_t *f)
{
    int i, j, c, o;

    pr_vm.stack[pr_vm.depth].s = pr_vm.xstatement;
    pr_vm.stack[pr_vm.depth].f = pr_vm.xfunction;
    pr_vm.depth++;
    if (pr_vm.depth >= pr_vm_state_t::MAX_STACK_DEPTH)
    {
        PR_RunError("stack overflow");
    }

    // save off any locals that the new function steps on
    c = f->locals;
    if (pr_vm.localstack_used + c > pr_vm_state_t::LOCALSTACK_SIZE)
    {
        PR_RunError("PR_ExecuteProgram: locals stack overflow\n");
    }

    for (i = 0; i < c; i++)
    {
        pr_vm.localstack[pr_vm.localstack_used + i] = (reinterpret_cast<int *>(pr_globals))[f->parm_start + i];
    }
    pr_vm.localstack_used += c;

    // copy parameters
    o = f->parm_start;
    for (i = 0; i < f->numparms; i++)
    {
        for (j = 0; j < f->parm_size[i]; j++)
        {
            (reinterpret_cast<int *>(pr_globals))[o] = (reinterpret_cast<int *>(pr_globals))[OFS_PARM0 + i * 3 + j];
            o++;
        }
    }

    pr_vm.xfunction = f;
    return f->first_statement - 1; // offset the s++
}

/*
====================
PR_LeaveFunction
====================
*/
int PR_LeaveFunction(void)
{
    int i, c;

    if (pr_vm.depth <= 0)
    {
        Sys_Error("prog stack underflow");
    }

    // restore locals from the stack
    c = pr_vm.xfunction->locals;
    pr_vm.localstack_used -= c;
    if (pr_vm.localstack_used < 0)
    {
        PR_RunError("PR_ExecuteProgram: locals stack underflow\n");
    }

    for (i = 0; i < c; i++)
    {
        (reinterpret_cast<int *>(pr_globals))[pr_vm.xfunction->parm_start + i] = pr_vm.localstack[pr_vm.localstack_used + i];
    }

    // up stack
    pr_vm.depth--;
    pr_vm.xfunction = pr_vm.stack[pr_vm.depth].f;
    return pr_vm.stack[pr_vm.depth].s;
}

/*
====================
PR_FieldAddress

Bounds-checked resolution of a raw field-slot byte offset (as computed by
OP_ADDRESS) into a pointer, for the OP_STOREP_* indirect-store opcodes.
Not edict-aligned like PROG_TO_EDICT, since it addresses a specific field
within an edict rather than the edict itself.
====================
*/
static eval_t *PR_FieldAddress(int ofs)
{
    if (ofs < 0 || ofs >= sv.max_edicts * pr_edict_size)
    {
        Sys_Error("PR_FieldAddress: bad offset {}", ofs);
    }
    return reinterpret_cast<eval_t *>(reinterpret_cast<byte *>(sv.edicts) + ofs);
}

/*
====================
PR_EdictFieldOffset

Computes an edict-relative field word offset (as read directly from a
dstatement_t's b operand by OP_ADDRESS/OP_LOAD_*) as an absolute byte
offset from sv.edicts, in the same units PR_FieldAddress bounds-checks --
the shared arithmetic OP_ADDRESS, OP_LOAD_F/FLD/ENT/S/FNC, and OP_LOAD_V
all need before that check.
====================
*/
static int PR_EdictFieldOffset(edict_t *ed, int fieldWordOfs)
{
    return static_cast<int>(reinterpret_cast<byte *>(reinterpret_cast<int *>(&ed->v) + fieldWordOfs) -
                             reinterpret_cast<byte *>(sv.edicts));
}

/*
====================
PR_ExecuteProgram
====================
*/
void PR_ExecuteProgram(func_t fnum)
{
    eval_t *a, *b, *c;
    int s;
    dstatement_t *st;
    dfunction_t *f, *newf;
    int runaway;
    int i;
    edict_t *ed;
    int exitdepth;
    eval_t *ptr;

    if (!fnum || fnum >= progs->numfunctions)
    {
        if (pr_global_struct->self)
        {
            ED_Print(PROG_TO_EDICT(pr_global_struct->self));
        }
        Host_Error("PR_ExecuteProgram: nullptr function");
    }

    f = &pr_functions[fnum];

    runaway = 100000;
    pr_vm.trace = false;

    // make a stack frame
    exitdepth = pr_vm.depth;

    s = PR_EnterFunction(f);

    while (1)
    {
        s++; // next statement

        st = &pr_statements[s];
        a = reinterpret_cast<eval_t *>(&pr_globals[st->a]);
        b = reinterpret_cast<eval_t *>(&pr_globals[st->b]);
        c = reinterpret_cast<eval_t *>(&pr_globals[st->c]);

        if (!--runaway)
        {
            PR_RunError("runaway loop error");
        }

        pr_vm.xfunction->profile++;
        pr_vm.xstatement = s;

        if (pr_vm.trace)
        {
            PR_PrintStatement(st);
        }

        switch (st->op)
        {
        case OP_ADD_F:
            c->_float = a->_float + b->_float;
            break;
        case OP_ADD_V:
            c->vector[0] = a->vector[0] + b->vector[0];
            c->vector[1] = a->vector[1] + b->vector[1];
            c->vector[2] = a->vector[2] + b->vector[2];
            break;

        case OP_SUB_F:
            c->_float = a->_float - b->_float;
            break;
        case OP_SUB_V:
            c->vector[0] = a->vector[0] - b->vector[0];
            c->vector[1] = a->vector[1] - b->vector[1];
            c->vector[2] = a->vector[2] - b->vector[2];
            break;

        case OP_MUL_F:
            c->_float = a->_float * b->_float;
            break;
        case OP_MUL_V:
            c->_float = a->vector[0] * b->vector[0] + a->vector[1] * b->vector[1] + a->vector[2] * b->vector[2];
            break;
        case OP_MUL_FV:
            c->vector[0] = a->_float * b->vector[0];
            c->vector[1] = a->_float * b->vector[1];
            c->vector[2] = a->_float * b->vector[2];
            break;
        case OP_MUL_VF:
            c->vector[0] = b->_float * a->vector[0];
            c->vector[1] = b->_float * a->vector[1];
            c->vector[2] = b->_float * a->vector[2];
            break;

        case OP_DIV_F:
            c->_float = a->_float / b->_float;
            break;

        case OP_BITAND:
            c->_float = (int)a->_float & (int)b->_float;
            break;

        case OP_BITOR:
            c->_float = (int)a->_float | (int)b->_float;
            break;

        case OP_GE:
            c->_float = a->_float >= b->_float;
            break;
        case OP_LE:
            c->_float = a->_float <= b->_float;
            break;
        case OP_GT:
            c->_float = a->_float > b->_float;
            break;
        case OP_LT:
            c->_float = a->_float < b->_float;
            break;
        case OP_AND:
            c->_float = a->_float && b->_float;
            break;
        case OP_OR:
            c->_float = a->_float || b->_float;
            break;

        case OP_NOT_F:
            c->_float = !a->_float;
            break;
        case OP_NOT_V:
            c->_float = !a->vector[0] && !a->vector[1] && !a->vector[2];
            break;
        case OP_NOT_S:
            c->_float = !a->string || !PR_GetString(a->string)[0];
            break;
        case OP_NOT_FNC:
            c->_float = !a->function;
            break;
        case OP_NOT_ENT:
            c->_float = (PROG_TO_EDICT(a->edict) == sv.edicts);
            break;

        case OP_EQ_F:
            c->_float = a->_float == b->_float;
            break;
        case OP_EQ_V:
            c->_float =
                (a->vector[0] == b->vector[0]) && (a->vector[1] == b->vector[1]) && (a->vector[2] == b->vector[2]);
            break;
        case OP_EQ_S:
            c->_float = !strcmp(PR_GetString(a->string), PR_GetString(b->string));
            break;
        case OP_EQ_E:
            c->_float = a->_int == b->_int;
            break;
        case OP_EQ_FNC:
            c->_float = a->function == b->function;
            break;

        case OP_NE_F:
            c->_float = a->_float != b->_float;
            break;
        case OP_NE_V:
            c->_float =
                (a->vector[0] != b->vector[0]) || (a->vector[1] != b->vector[1]) || (a->vector[2] != b->vector[2]);
            break;
        case OP_NE_S:
            c->_float = strcmp(PR_GetString(a->string), PR_GetString(b->string));
            break;
        case OP_NE_E:
            c->_float = a->_int != b->_int;
            break;
        case OP_NE_FNC:
            c->_float = a->function != b->function;
            break;

            //==================
        case OP_STORE_F:
        case OP_STORE_ENT:
        case OP_STORE_FLD: // integers
        case OP_STORE_S:
        case OP_STORE_FNC: // pointers
            b->_int = a->_int;
            break;
        case OP_STORE_V:
            b->vector[0] = a->vector[0];
            b->vector[1] = a->vector[1];
            b->vector[2] = a->vector[2];
            break;

        case OP_STOREP_F:
        case OP_STOREP_ENT:
        case OP_STOREP_FLD: // integers
        case OP_STOREP_S:
        case OP_STOREP_FNC: // pointers
            ptr = PR_FieldAddress(b->_int);
            ptr->_int = a->_int;
            break;
        case OP_STOREP_V:
            ptr = PR_FieldAddress(b->_int);
            ptr->vector[0] = a->vector[0];
            ptr->vector[1] = a->vector[1];
            ptr->vector[2] = a->vector[2];
            break;

        case OP_ADDRESS:
            ed = PROG_TO_EDICT(a->edict);
            if (ed == sv.edicts && sv.state == server_state_t::ss_active)
            {
                PR_RunError("assignment to world entity");
            }
            c->_int = PR_EdictFieldOffset(ed, b->_int);
            break;

        case OP_LOAD_F:
        case OP_LOAD_FLD:
        case OP_LOAD_ENT:
        case OP_LOAD_S:
        case OP_LOAD_FNC:
            ed = PROG_TO_EDICT(a->edict);
            a = PR_FieldAddress(PR_EdictFieldOffset(ed, b->_int));
            c->_int = a->_int;
            break;

        case OP_LOAD_V:
            ed = PROG_TO_EDICT(a->edict);
            a = PR_FieldAddress(PR_EdictFieldOffset(ed, b->_int));
            c->vector[0] = a->vector[0];
            c->vector[1] = a->vector[1];
            c->vector[2] = a->vector[2];
            break;

            //==================

        case OP_IFNOT:
            if (!a->_int)
            {
                s += st->b - 1; // offset the s++
            }
            break;

        case OP_IF:
            if (a->_int)
            {
                s += st->b - 1; // offset the s++
            }
            break;

        case OP_GOTO:
            s += st->a - 1; // offset the s++
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
            pr_vm.argc = st->op - OP_CALL0;
            if (!a->function)
            {
                PR_RunError("nullptr function");
            }

            newf = &pr_functions[a->function];

            if (newf->first_statement < 0)
            { // negative statements are built in functions
                i = -newf->first_statement;
                if (i >= pr_numbuiltins)
                {
                    PR_RunError("Bad builtin call number");
                }
                pr_builtins[i]();
                break;
            }

            s = PR_EnterFunction(newf);
            break;

        case OP_DONE:
        case OP_RETURN:
            pr_globals[OFS_RETURN] = pr_globals[st->a];
            pr_globals[OFS_RETURN + 1] = pr_globals[st->a + 1];
            pr_globals[OFS_RETURN + 2] = pr_globals[st->a + 2];

            s = PR_LeaveFunction();
            if (pr_vm.depth == exitdepth)
            {
                return; // all done
            }
            break;

        case OP_STATE:
            ed = PROG_TO_EDICT(pr_global_struct->self);
            ed->v.nextthink = pr_global_struct->time + 0.1;
            if (a->_float != ed->v.frame)
            {
                ed->v.frame = a->_float;
            }
            ed->v.think = b->function;
            break;

        default:
            PR_RunError("Bad opcode {}", st->op);
        }
    }
}
