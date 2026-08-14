#include "test_stubs.h"
#include "quakedef.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

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
// Shared cross-file test setup
// ===========================================================================

// zone.cpp's Memory_Init must run before any Z_/Hunk_/Cache_ call is safe.
// Called from more than one test file (see test_stubs.h) since doctest's
// TEST_CASE execution order across files isn't something to rely on.
void EnsureMemoryInit ()
{
	static bool initialized = false;
	if (!initialized)
	{
		static char buffer[4 * 1024 * 1024];
		Memory_Init (buffer, sizeof (buffer));
		initialized = true;
	}
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

void Con_DPrintf (const char *fmt, ...)
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
void SV_BroadcastPrintfImpl (const std::string &text)
{
}

// Only reachable via Cmd_Exec_f (exec a .cfg file), which no test calls.
byte *COM_LoadHunkFile (const char *path)
{
	return nullptr;
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
// cvar.cpp/cmd.cpp/zone.cpp call a small, self-contained cluster of
// common.cpp's string/parsing utilities. Compiling the real common.cpp to
// get them would also pull in filesystem/packfile loading (COM_LoadFile,
// COM_OpenFile, ...) needing ~10 more stub symbols (Sys_FileOpenRead/
// Write/Close/Seek/Time, Sys_mkdir) for functionality with zero bearing on
// any of the three files' correctness. Every function below was checked
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
		return nullptr;

// skip whitespace
skipwhite:
	while ( (c = *data) <= ' ')
	{
		if (c == 0)
			return nullptr;                    // end of file;
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

// zone.cpp needs these four more from the same common.cpp cluster.
void Q_memset (void *dest, int fill, int count)
{
	int             i;

	if ( (((size_t)dest | count) & 3) == 0)
	{
		count >>= 2;
		fill = fill | (fill<<8) | (fill<<16) | (fill<<24);
		for (i=0 ; i<count ; i++)
			((int *)dest)[i] = fill;
	}
	else
		for (i=0 ; i<count ; i++)
			((byte *)dest)[i] = fill;
}

void Q_strncpy (char *dest, const char *src, int count)
{
	while (*src && count--)
	{
		*dest++ = *src++;
	}
	if (count)
		*dest++ = 0;
}

int Q_strcmp (const char *s1, const char *s2)
{
	while (1)
	{
		if (*s1 != *s2)
			return -1;              // strings not equal
		if (!*s1)
			return 0;               // strings are equal
		s1++;
		s2++;
	}

	return -1;
}

int Q_atoi (const char *str)
{
	int             val;
	int             sign;
	int             c;

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
				val = (val<<4) + c - '0';
			else if (c >= 'a' && c <= 'f')
				val = (val<<4) + c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				val = (val<<4) + c - 'A' + 10;
			else
				return val*sign;
		}
	}

//
// check for character
//
	if (str[0] == '\'')
	{
		return sign * str[1];
	}

//
// assume decimal
//
	while (1)
	{
		c = *str++;
		if (c <'0' || c > '9')
			return val*sign;
		val = val*10 + c - '0';
	}

	return 0;
}

int COM_CheckParm (const char *parm)
{
	int             i;

	for (i=1 ; i<com_argc ; i++)
	{
		if (!com_argv[i])
			continue;               // NEXTSTEP sometimes clears appkit vars.
		if (!Q_strcmp (parm,com_argv[i]))
			return i;
	}

	return 0;
}

// ===========================================================================
// Duplicated from world.cpp / sv_phys.cpp for test isolation.
//
// SV_HullPointContents/SV_RecursiveHullCheck (world.cpp) and
// ClipVelocity/SV_WallFriction (sv_phys.cpp) are the actual functions
// under test in test_physics.cpp -- confirmed by direct reading to touch
// nothing beyond hull_t/trace_t/edict_t fields, Sys_Error/Con_Printf/
// Con_DPrintf, GLM, and mathlib macros. Both source files also contain
// the live entity-linking/area-tree system (SV_LinkEdict, SV_TouchLinks,
// ...) and the full physics simulation loop (SV_Physics_*, PR_ExecuteProgram-
// heavy) respectively -- compiling either file wholesale to get these 4
// functions would need a much larger, disproportionate stub investment
// for machinery this test file has no interest in. Copied
// character-for-character; keep in sync if the real versions change.
// AngleVectors (used by SV_WallFriction) is NOT duplicated here -- the
// real mathlib.cpp is already a compiled source in this project.
// ===========================================================================

#define DIST_EPSILON	(0.03125)

int SV_HullPointContents (hull_t *hull, int num, vec3_t p)
{
	float		d;
	dclipnode_t	*node;
	mplane_t	*plane;

	while (num >= 0)
	{
		if (num < hull->firstclipnode || num > hull->lastclipnode)
			Sys_Error ("SV_HullPointContents: bad node number");

		node = hull->clipnodes + num;
		plane = hull->planes + node->planenum;

		if (plane->type < 3)
			d = p[plane->type] - plane->dist;
		else
			d = DotProduct (plane->normal, p) - plane->dist;
		if (d < 0)
			num = node->children[1];
		else
			num = node->children[0];
	}

	return num;
}

qboolean SV_RecursiveHullCheck (hull_t *hull, int num, float p1f, float p2f, vec3_t p1, vec3_t p2, trace_t *trace)
{
	dclipnode_t	*node;
	mplane_t	*plane;
	float		t1, t2;
	float		frac;
	vec3_t		mid;
	int			side;
	float		midf;

// check for empty
	if (num < 0)
	{
		if (num != CONTENTS_SOLID)
		{
			trace->allsolid = false;
			if (num == CONTENTS_EMPTY)
				trace->inopen = true;
			else
				trace->inwater = true;
		}
		else
			trace->startsolid = true;
		return true;		// empty
	}

	if (num < hull->firstclipnode || num > hull->lastclipnode)
		Sys_Error ("SV_RecursiveHullCheck: bad node number");

//
// find the point distances
//
	node = hull->clipnodes + num;
	plane = hull->planes + node->planenum;

	if (plane->type < 3)
	{
		t1 = p1[plane->type] - plane->dist;
		t2 = p2[plane->type] - plane->dist;
	}
	else
	{
		t1 = DotProduct (plane->normal, p1) - plane->dist;
		t2 = DotProduct (plane->normal, p2) - plane->dist;
	}

	if (t1 >= 0 && t2 >= 0)
		return SV_RecursiveHullCheck (hull, node->children[0], p1f, p2f, p1, p2, trace);
	if (t1 < 0 && t2 < 0)
		return SV_RecursiveHullCheck (hull, node->children[1], p1f, p2f, p1, p2, trace);

// put the crosspoint DIST_EPSILON pixels on the near side
	if (t1 < 0)
		frac = (t1 + DIST_EPSILON)/(t1-t2);
	else
		frac = (t1 - DIST_EPSILON)/(t1-t2);
	if (frac < 0)
		frac = 0;
	if (frac > 1)
		frac = 1;

	midf = p1f + (p2f - p1f)*frac;
	{
		glm::vec3 result = glm::mix (glm::make_vec3(p1), glm::make_vec3(p2), frac);
		mid[0] = result.x; mid[1] = result.y; mid[2] = result.z;
	}

	side = (t1 < 0);

// move up to the node
	if (!SV_RecursiveHullCheck (hull, node->children[side], p1f, midf, p1, mid, trace) )
		return false;

	if (SV_HullPointContents (hull, node->children[side^1], mid)
	!= CONTENTS_SOLID)
// go past the node
		return SV_RecursiveHullCheck (hull, node->children[side^1], midf, p2f, mid, p2, trace);

	if (trace->allsolid)
		return false;		// never got out of the solid area

//==================
// the other side of the node is solid, this is the impact point
//==================
	if (!side)
	{
		VectorCopy (plane->normal, trace->plane.normal);
		trace->plane.dist = plane->dist;
	}
	else
	{
		VectorSubtract (vec3_origin, plane->normal, trace->plane.normal);
		trace->plane.dist = -plane->dist;
	}

	while (SV_HullPointContents (hull, hull->firstclipnode, mid)
	== CONTENTS_SOLID)
	{ // shouldn't really happen, but does occasionally
		frac -= 0.1f;
		if (frac < 0)
		{
			trace->fraction = midf;
			VectorCopy (mid, trace->endpos);
			Con_DPrintf ("backup past 0\n");
			return false;
		}
		midf = p1f + (p2f - p1f)*frac;
		glm::vec3 result = glm::mix (glm::make_vec3(p1), glm::make_vec3(p2), frac);
		mid[0] = result.x; mid[1] = result.y; mid[2] = result.z;
	}

	trace->fraction = midf;
	VectorCopy (mid, trace->endpos);

	return false;
}

#define	STOP_EPSILON	0.1

int ClipVelocity (vec3_t in, vec3_t normal, vec3_t out, float overbounce)
{
	float	backoff;
	int		i, blocked;

	blocked = 0;
	if (normal[2] > 0)
		blocked |= 1;		// floor
	if (!normal[2])
		blocked |= 2;		// step

	backoff = DotProduct (in, normal) * overbounce;

	{
		glm::vec3 result = glm::make_vec3(in) - glm::make_vec3(normal) * backoff;
		out[0] = result.x; out[1] = result.y; out[2] = result.z;
	}

	for (i=0 ; i<3 ; i++)
	{
		if (out[i] > -STOP_EPSILON && out[i] < STOP_EPSILON)
			out[i] = 0;
	}

	return blocked;
}

void SV_WallFriction (edict_t *ent, trace_t *trace)
{
	vec3_t		forward, right, up;
	float		d;

	AngleVectors (ent->v.v_angle, forward, right, up);
	d = DotProduct (trace->plane.normal, forward);

	d += 0.5;
	if (d >= 0)
		return;

// cut the tangential velocity
	glm::vec3 normal = glm::make_vec3(trace->plane.normal);
	glm::vec3 side = glm::make_vec3(ent->v.velocity) - normal * glm::dot (normal, glm::make_vec3(ent->v.velocity));

	ent->v.velocity[0] = side.x * (1 + d);
	ent->v.velocity[1] = side.y * (1 + d);
}
