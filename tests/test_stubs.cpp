#include "test_stubs.h"
#include "quakedef.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ===========================================================================
// mathlib.cpp's platform-boundary stub
// ===========================================================================

// mathlib.cpp's only external dependency (beyond header declarations) is
// Sys_Error (sys.h). The real implementation (sys_win.cpp) shows a dialog
// and calls ExitProcess -- unusable inside a test binary. This throws
// instead, so BoxOnPlaneSide's and FloorDivMod's error paths can be
// asserted with CHECK_THROWS_AS rather than only testing happy paths.
[[noreturn]] void Sys_Error (const char *error, ...)
{
	char text[1024];
	va_list argptr;
	va_start (argptr, error);
	vsnprintf (text, sizeof(text), error, argptr);
	va_end (argptr);
	throw SysErrorException (text);
}

// ===========================================================================
// cvar.cpp / cmd.cpp's platform-boundary stubs
// ===========================================================================

std::string g_lastConPrint;

void ClearConPrint ()
{
	g_lastConPrint.clear ();
}

void Con_Printf (const char *fmt, ...)
{
	char text[1024];
	va_list argptr;
	va_start (argptr, fmt);
	vsnprintf (text, sizeof(text), fmt, argptr);
	va_end (argptr);
	g_lastConPrint = text;
}

// Only reachable when sv.active (Cvar_Set's "notify players" branch) --
// never true in these tests, since nothing here spins up a real server.
void SV_BroadcastPrintf (const char *fmt, ...)
{
}

// Hunk semantics (mark/rewind) aren't needed for anything under test --
// cvar.cpp/cmd.cpp only ever allocate through this, never free.
void *Hunk_AllocName (int size, const char *name)
{
	return malloc (size);
}

// Only reachable via Cmd_Exec_f (exec a .cfg file), which no test calls.
byte *COM_LoadHunkFile (const char *path)
{
	return NULL;
}

// Only reachable via Cmd_Exec_f, which no test calls.
size_t Hunk_LowMark (void)
{
	return 0;
}

void Hunk_FreeToLowMark (size_t mark)
{
}

// Real dependency of Cbuf_InsertText (used by the alias-dispatch test) --
// mark/rewind semantics aren't needed, just working alloc/free.
void *Z_Malloc (int size)
{
	void *p = malloc (size);
	memset (p, 0, size);
	return p;
}

void Z_Free (void *ptr)
{
	free (ptr);
}

// Only reachable via Cmd_ForwardToServer, which no test calls.
void SZ_Print (sizebuf_t *buf, const char *data)
{
}

void MSG_WriteByte (sizebuf_t *sb, int c)
{
}

// Only reachable via Cmd_StuffCmds_f, which no test calls.
int com_argc = 0;
const char **com_argv = nullptr;

// Satisfy the linker for Cvar_Set's sv.active read and Cmd_ForwardToServer's
// cls.message write -- neither path is exercised by any test (sv.active is
// false by construction; nothing here calls Cmd_ForwardToServer).
server_t sv{};
client_static_t cls{};

// Cmd_AddCommand refuses to register after this is set (real init is
// host.cpp's Host_Init, near the very end) -- stays false for tests.
qboolean host_initialized = false;

// ===========================================================================
// Duplicated from common.cpp for test isolation.
//
// cvar.cpp/cmd.cpp call a small, self-contained cluster of common.cpp's
// string/parsing utilities. Compiling the real common.cpp to get them would
// also pull in filesystem/packfile loading (COM_LoadFile, COM_OpenFile, ...)
// needing ~10 more stub symbols (Hunk_Alloc, Cache_Alloc, Z_Malloc,
// Sys_FileOpenRead/Write/Close/Seek/Time, Sys_mkdir) for functionality with
// zero bearing on cvar/cmd correctness. Every function below was checked
// directly against common.cpp and confirmed to call only each other or
// plain C library functions -- nothing else in the engine. Copied
// character-for-character; keep in sync if common.cpp's versions change.
// ===========================================================================

char com_token[1024];

void Q_memcpy (void *dest, const void *src, int count)
{
	int             i;

	if (( ( (size_t)dest | (size_t)src | count) & 3) == 0 )
	{
		count>>=2;
		for (i=0 ; i<count ; i++)
			((int *)dest)[i] = ((int *)src)[i];
	}
	else
		for (i=0 ; i<count ; i++)
			((byte *)dest)[i] = ((byte *)src)[i];
}

int Q_strlen (const char *str)
{
	int             count;

	count = 0;
	while (str[count])
		count++;

	return count;
}

void Q_strcpy (char *dest, const char *src)
{
	while (*src)
	{
		*dest++ = *src++;
	}
	*dest++ = 0;
}

void Q_strcat (char *dest, const char *src)
{
	dest += Q_strlen(dest);
	Q_strcpy (dest, src);
}

int Q_strncasecmp (const char *s1, const char *s2, int n)
{
	int             c1, c2;

	while (1)
	{
		c1 = *s1++;
		c2 = *s2++;

		if (!n--)
			return 0;               // strings are equal until end point

		if (c1 != c2)
		{
			if (c1 >= 'a' && c1 <= 'z')
				c1 -= ('a' - 'A');
			if (c2 >= 'a' && c2 <= 'z')
				c2 -= ('a' - 'A');
			if (c1 != c2)
				return -1;              // strings not equal
		}
		if (!c1)
			return 0;               // strings are equal
	}

	return -1;
}

int Q_strcasecmp (const char *s1, const char *s2)
{
	return Q_strncasecmp (s1, s2, 99999);
}

float Q_atof (const char *str)
{
	double			val;
	int             sign;
	int             c;
	int             decimal, total;

	if (*str == '-')
	{
		sign = -1;
		str++;
	}
	else
		sign = 1;

	val = 0;

//
// check for hex
//
	if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X') )
	{
		str += 2;
		while (1)
		{
			c = *str++;
			if (c >= '0' && c <= '9')
				val = (val*16) + c - '0';
			else if (c >= 'a' && c <= 'f')
				val = (val*16) + c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				val = (val*16) + c - 'A' + 10;
			else
				return (float)(val*sign);
		}
	}

//
// check for character
//
	if (str[0] == '\'')
	{
		return (float)(sign * str[1]);
	}

//
// assume decimal
//
	decimal = -1;
	total = 0;
	while (1)
	{
		c = *str++;
		if (c == '.')
		{
			decimal = total;
			continue;
		}
		if (c <'0' || c > '9')
			break;
		val = val*10 + c - '0';
		total++;
	}

	if (decimal == -1)
		return (float)(val*sign);
	while (total > decimal)
	{
		val /= 10;
		total--;
	}
	return (float)(val*sign);
}

const char *COM_Parse (const char *data)
{
	int             c;
	int             len;

	len = 0;
	com_token[0] = 0;

	if (!data)
		return NULL;

// skip whitespace
skipwhite:
	while ( (c = *data) <= ' ')
	{
		if (c == 0)
			return NULL;                    // end of file;
		data++;
	}

// skip // comments
	if (c=='/' && data[1] == '/')
	{
		while (*data && *data != '\n')
			data++;
		goto skipwhite;
	}

// handle quoted strings specially
	if (c == '\"')
	{
		data++;
		while (1)
		{
			c = *data++;
			if (c=='\"' || !c)
			{
				com_token[len] = 0;
				return data;
			}
			com_token[len] = (char)c;
			len++;
		}
	}

// parse single characters
	if (c=='{' || c=='}'|| c==')'|| c=='(' || c=='\'' || c==':')
	{
		com_token[len] = (char)c;
		len++;
		com_token[len] = 0;
		return data+1;
	}

// parse a regular word
	do
	{
		com_token[len] = (char)c;
		data++;
		len++;
		c = *data;
	if (c=='{' || c=='}'|| c==')'|| c=='(' || c=='\'' || c==':')
			break;
	} while (c>32);

	com_token[len] = 0;
	return data;
}

std::string COM_FormatVA (const char *fmt, va_list argptr)
{
	va_list measure;
	va_copy (measure, argptr);
	int need = vsnprintf (nullptr, 0, fmt, measure);
	va_end (measure);

	if (need <= 0)
		return std::string ();

	std::string msg (need, '\0');
	vsnprintf (msg.data (), need + 1, fmt, argptr);
	return msg;
}

const char *va (const char *format, ...)
{
	va_list         argptr;
	static std::string      string;

	va_start (argptr, format);
	string = COM_FormatVA (format, argptr);
	va_end (argptr);

	return string.c_str();
}

void SZ_Alloc (sizebuf_t *buf, int startsize)
{
	if (startsize < 256)
		startsize = 256;
	buf->data = (byte *)Hunk_AllocName (startsize, "sizebuf");
	buf->maxsize = startsize;
	buf->cursize = 0;
}

void SZ_Clear (sizebuf_t *buf)
{
	buf->cursize = 0;
}

void *SZ_GetSpace (sizebuf_t *buf, int length)
{
	void    *data;

	if (buf->cursize + length > buf->maxsize)
	{
		if (!buf->allowoverflow)
			Sys_Error ("SZ_GetSpace: overflow without allowoverflow set");

		if (length > buf->maxsize)
			Sys_Error ("SZ_GetSpace: %i is > full buffer size", length);

		buf->overflowed = true;
		Con_Printf ("SZ_GetSpace: overflow");
		SZ_Clear (buf);
	}

	data = buf->data + buf->cursize;
	buf->cursize += length;

	return data;
}

void SZ_Write (sizebuf_t *buf, const void *data, int length)
{
	Q_memcpy (SZ_GetSpace(buf,length),data,length);
}
