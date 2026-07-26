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
// cmd.c -- Quake script command processing module

#include "quakedef.h"
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cctype>

void Cmd_ForwardToServer (void);

#define	MAX_ALIAS_NAME	32

// Command and alias dispatch has always matched case-insensitively
// (Cmd_ExecuteString used Q_strcasecmp) -- normalizing keys to lowercase
// at both registration and lookup preserves that with a plain
// std::map<std::string, T> (ordered, so prefix search stays O(log n) for
// completion) instead of needing a custom case-insensitive comparator.
static std::string ToLower (const std::string &s)
{
	std::string out = s;
	std::transform (out.begin (), out.end (), out.begin (),
		[] (unsigned char c) { return (char)std::tolower (c); });
	return out;
}

// name -> expansion text. Was a hand-rolled cmdalias_t linked list;
// nothing outside this file ever touched it (confirmed via grep), so the
// struct (name/value/next, both already std::string since Milestone 4)
// simply isn't needed once the map holds the strings directly.
static std::map<std::string, std::string> cmd_alias;

int trashtest;
int *trashspot;

qboolean	cmd_wait;

//=============================================================================

/*
============
Cmd_Wait_f

Causes execution of the remainder of the command buffer to be delayed until
next frame.  This allows commands like:
bind g "impulse 5 ; +attack ; wait ; -attack ; impulse 2"
============
*/
void Cmd_Wait_f (void)
{
	cmd_wait = true;
}

/*
=============================================================================

						COMMAND BUFFER

=============================================================================
*/

sizebuf_t	cmd_text;

/*
============
Cbuf_Init
============
*/
void Cbuf_Init (void)
{
	SZ_Alloc (&cmd_text, 8192);		// space for commands and script files
}


/*
============
Cbuf_AddText

Adds command text at the end of the buffer
============
*/
void Cbuf_AddText (const char *text)
{
	int		l;
	
	l = Q_strlen (text);

	if (cmd_text.cursize + l >= cmd_text.maxsize)
	{
		Con_Printf ("Cbuf_AddText: overflow\n");
		return;
	}

	SZ_Write (&cmd_text, text, Q_strlen (text));
}


/*
============
Cbuf_InsertText

Adds command text immediately after the current command
Adds a \n to the text
FIXME: actually change the command buffer to do less copying
============
*/
void Cbuf_InsertText (const char *text)
{
	std::vector<char>	temp;

// copy off any commands still remaining in the exec buffer
	if (cmd_text.cursize)
	{
		temp.assign (cmd_text.data, cmd_text.data + cmd_text.cursize);
		SZ_Clear (&cmd_text);
	}

// add the entire text of the file
	Cbuf_AddText (text);

// add the copied off data
	if (!temp.empty ())
		SZ_Write (&cmd_text, temp.data (), (int)temp.size ());
}

/*
============
Cbuf_Execute
============
*/
void Cbuf_Execute (void)
{
	int		i;
	char	*text;
	char	line[1024];
	int		quotes;
	
	while (cmd_text.cursize)
	{
// find a \n or ; line break
		text = (char *)cmd_text.data;

		quotes = 0;
		for (i=0 ; i< cmd_text.cursize ; i++)
		{
			if (text[i] == '"')
				quotes++;
			if ( !(quotes&1) &&  text[i] == ';')
				break;	// don't break if inside a quoted string
			if (text[i] == '\n')
				break;
		}
			
				
		memcpy (line, text, i);
		line[i] = 0;
		
// delete the text from the command buffer and move remaining commands down
// this is necessary because commands (exec, alias) can insert data at the
// beginning of the text buffer

		if (i == cmd_text.cursize)
			cmd_text.cursize = 0;
		else
		{
			i++;
			cmd_text.cursize -= i;
			Q_memcpy (text, text+i, cmd_text.cursize);
		}

// execute the command line
		Cmd_ExecuteString (line, cmd_source_t::src_command);
		
		if (cmd_wait)
		{	// skip out while text still remains in buffer, leaving it
			// for next frame
			cmd_wait = false;
			break;
		}
	}
}

/*
==============================================================================

						SCRIPT COMMANDS

==============================================================================
*/

/*
===============
Cmd_StuffCmds_f

Adds command line parameters as script statements
Commands lead with a +, and continue until a - or another +
quake +prog jctest.qp +cmd amlev1
quake -nosound +cmd amlev1
===============
*/
void Cmd_StuffCmds_f (void)
{
	int		i, j;
	std::string	text, build;

	if (Cmd_Argc () != 1)
	{
		Con_Printf ("stuffcmds : execute command line parameters\n");
		return;
	}

// build the combined string to parse from
	for (i=1 ; i<com_argc ; i++)
	{
		if (!com_argv[i])
			continue;		// NEXTSTEP nulls out -NXHost
		if (!text.empty ())
			text += " ";
		text += com_argv[i];
	}
	if (text.empty ())
		return;

// pull out the commands
	for (i=0 ; i<(int)text.size () ; i++)
	{
		if (text[i] == '+')
		{
			i++;

			for (j=i ; j < (int)text.size () && text[j] != '+' && text[j] != '-' ; j++)
				;

			build += text.substr (i, j-i);
			build += "\n";
			i = j-1;
		}
	}

	if (!build.empty ())
		Cbuf_InsertText (build.c_str ());
}


/*
===============
Cmd_Exec_f
===============
*/
void Cmd_Exec_f (void)
{
	char	*f;
	size_t	mark;

	if (Cmd_Argc () != 2)
	{
		Con_Printf ("exec <filename> : execute a script file\n");
		return;
	}

	mark = Hunk_LowMark ();
	f = (char *)COM_LoadHunkFile (Cmd_Argv(1));
	if (!f)
	{
		Con_Printf ("couldn't exec %s\n",Cmd_Argv(1));
		return;
	}
	Con_Printf ("execing %s\n",Cmd_Argv(1));
	
	Cbuf_InsertText (f);
	Hunk_FreeToLowMark (mark);
}


/*
===============
Cmd_Echo_f

Just prints the rest of the line to the console
===============
*/
void Cmd_Echo_f (void)
{
	int		i;
	
	for (i=1 ; i<Cmd_Argc() ; i++)
		Con_Printf ("%s ",Cmd_Argv(i));
	Con_Printf ("\n");
}

/*
===============
Cmd_Alias_f

Creates a new command that executes a command string (possibly ; seperated)
===============
*/

void Cmd_Alias_f (void)
{
	int			i, c;
	const char	*s;

	if (Cmd_Argc() == 1)
	{
		Con_Printf ("Current alias commands:\n");
		for (const auto &[name, value] : cmd_alias)
			Con_Printf ("%s : %s\n", name.c_str(), value.c_str());
		return;
	}

	s = Cmd_Argv(1);
	if (strlen(s) >= MAX_ALIAS_NAME)
	{
		Con_Printf ("Alias name is too long\n");
		return;
	}

// copy the rest of the command line
	std::string cmd;
	c = Cmd_Argc();
	for (i=2 ; i< c ; i++)
	{
		cmd += Cmd_Argv(i);
		if (i != c)
			cmd += " ";
	}
	cmd += "\n";

	// map::operator[] creates the entry if the alias doesn't exist yet,
	// or overwrites it in place if it does -- absorbs the old find-or-add
	cmd_alias[ToLower (s)] = cmd;
}

/*
=============================================================================

					COMMAND EXECUTION

=============================================================================
*/

#define	MAX_ARGS		80

static	int			cmd_argc;
static	std::string	cmd_argv[MAX_ARGS];
static	const char	*cmd_null_string = "";
static	const char	*cmd_args = NULL;

cmd_source_t	cmd_source;


// name -> handler. Was a hand-rolled cmd_function_t linked list with an
// O(n) scan on every lookup -- Cmd_ExecuteString (the single most
// frequently called function in the engine) used to do up to three of
// these scans per command. Nothing outside this file ever read a
// registered command's name back (error messages print the input
// argument, not a stored copy), so the map value can just be the handler
// itself.
static	std::map<std::string, xcommand_t>	cmd_functions;

/*
============
Cmd_Init
============
*/
void Cmd_Init (void)
{
//
// register our commands
//
	Cmd_AddCommand ("stuffcmds",Cmd_StuffCmds_f);
	Cmd_AddCommand ("exec",Cmd_Exec_f);
	Cmd_AddCommand ("echo",Cmd_Echo_f);
	Cmd_AddCommand ("alias",Cmd_Alias_f);
	Cmd_AddCommand ("cmd", Cmd_ForwardToServer);
	Cmd_AddCommand ("wait", Cmd_Wait_f);
}

/*
============
Cmd_Argc
============
*/
int		Cmd_Argc (void)
{
	return cmd_argc;
}

/*
============
Cmd_Argv
============
*/
const char	*Cmd_Argv (int arg)
{
	if ( (unsigned)arg >= (unsigned)cmd_argc )
		return cmd_null_string;
	return cmd_argv[arg].c_str();
}

/*
============
Cmd_Args
============
*/
std::string	Cmd_Args (void)
{
	return cmd_args ? cmd_args : "";
}


/*
============
Cmd_TokenizeString

Parses the given string into command line tokens.
============
*/
void Cmd_TokenizeString (const char *text)
{
	cmd_argc = 0;
	cmd_args = NULL;
	
	while (1)
	{
// skip whitespace up to a /n
		while (*text && *text <= ' ' && *text != '\n')
		{
			text++;
		}
		
		if (*text == '\n')
		{	// a newline seperates commands in the buffer
			text++;
			break;
		}

		if (!*text)
			return;
	
		if (cmd_argc == 1)
			 cmd_args = text;
			
		text = COM_Parse (text);
		if (!text)
			return;

		if (cmd_argc < MAX_ARGS)
		{
			cmd_argv[cmd_argc] = com_token;
			cmd_argc++;
		}
	}
	
}


/*
============
Cmd_AddCommand
============
*/
void	Cmd_AddCommand (const char *cmd_name, xcommand_t function)
{
	if (host_initialized)	// because hunk allocation would get stomped
		Sys_Error ("Cmd_AddCommand after host_initialized");

// fail if the command is a variable name
	if (Cvar_VariableString(cmd_name)[0])
	{
		Con_Printf ("Cmd_AddCommand: %s already defined as a var\n", cmd_name);
		return;
	}

	std::string key = ToLower (cmd_name);

// fail if the command already exists
	if (cmd_functions.find (key) != cmd_functions.end ())
	{
		Con_Printf ("Cmd_AddCommand: %s already defined\n", cmd_name);
		return;
	}

	cmd_functions[key] = function;
}

/*
============
Cmd_Exists
============
*/
qboolean	Cmd_Exists (const char *cmd_name)
{
	return cmd_functions.find (ToLower (cmd_name)) != cmd_functions.end ();
}



/*
============
Cmd_CompleteCommand
============
*/
std::optional<std::string> Cmd_CompleteCommand (const char *partial)
{
	size_t len = Q_strlen (partial);

	if (!len)
		return std::nullopt;

	std::string lowerPartial = ToLower (partial);
	auto it = cmd_functions.lower_bound (lowerPartial);
	if (it != cmd_functions.end () && it->first.compare (0, len, lowerPartial) == 0)
		return it->first;

	return std::nullopt;
}

/*
============
Cmd_ExecuteString

A complete command line has been parsed, so try to execute it
FIXME: lookupnoadd the token to speed search?
============
*/
void	Cmd_ExecuteString (const char *text, cmd_source_t src)
{
	cmd_source = src;
	Cmd_TokenizeString (text);

// execute the command line
	if (!Cmd_Argc())
		return;		// no tokens

	std::string key = ToLower (cmd_argv[0]);

// check functions
	auto cmdIt = cmd_functions.find (key);
	if (cmdIt != cmd_functions.end ())
	{
		cmdIt->second ();
		return;
	}

// check alias
	auto aliasIt = cmd_alias.find (key);
	if (aliasIt != cmd_alias.end ())
	{
		Cbuf_InsertText (aliasIt->second.c_str());
		return;
	}

// check cvars
	if (!Cvar_Command ())
		Con_Printf ("Unknown command \"%s\"\n", Cmd_Argv(0));

}


/*
===================
Cmd_ForwardToServer

Sends the entire command line over to the server
===================
*/
void Cmd_ForwardToServer (void)
{
	if (cls.state != ca_connected)
	{
		Con_Printf ("Can't \"%s\", not connected\n", Cmd_Argv(0));
		return;
	}
	
	if (cls.demoplayback)
		return;		// not really connected

	MSG_WriteByte (&cls.message, clc_stringcmd);
	if (Q_strcasecmp(Cmd_Argv(0), "cmd") != 0)
	{
		SZ_Print (&cls.message, Cmd_Argv(0));
		SZ_Print (&cls.message, " ");
	}
	if (Cmd_Argc() > 1)
		SZ_Print (&cls.message, Cmd_Args().c_str());
	else
		SZ_Print (&cls.message, "\n");
}


/*
================
Cmd_CheckParm

Returns the position (1 to argc-1) in the command's argument list
where the given parameter apears, or 0 if not present
================
*/

int Cmd_CheckParm (const char *parm)
{
	int i;
	
	if (!parm)
		Sys_Error ("Cmd_CheckParm: NULL");

	for (i = 1; i < Cmd_Argc (); i++)
		if (! Q_strcasecmp (parm, Cmd_Argv (i)))
			return i;
			
	return 0;
}
