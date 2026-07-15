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
// net_wins_internal.h -- private to the WinSock-based networking drivers
// (net_wins.cpp, net_wipx.cpp). These are dynamically resolved via
// GetProcAddress against wsock32.dll (see net_wins.cpp's WINS_Init), so
// they're function pointers rather than direct imports -- nothing outside
// these two files needs to touch them.

#ifndef __NET_WINS_INTERNAL__
#define __NET_WINS_INTERNAL__

extern qboolean winsock_lib_initialized;

extern int(PASCAL FAR *pWSAStartup)(WORD wVersionRequired, LPWSADATA lpWSAData);
extern int(PASCAL FAR *pWSACleanup)(void);
extern int(PASCAL FAR *pWSAGetLastError)(void);
extern SOCKET(PASCAL FAR *psocket)(int af, int type, int protocol);
extern int(PASCAL FAR *pioctlsocket)(SOCKET s, long cmd, u_long FAR *argp);
extern int(PASCAL FAR *psetsockopt)(SOCKET s, int level, int optname, const char FAR *optval, int optlen);
extern int(PASCAL FAR *precvfrom)(SOCKET s, char FAR *buf, int len, int flags, struct sockaddr FAR *from,
                                  int FAR *fromlen);
extern int(PASCAL FAR *psendto)(SOCKET s, const char FAR *buf, int len, int flags, const struct sockaddr FAR *to,
                                int tolen);
extern int(PASCAL FAR *pclosesocket)(SOCKET s);
extern int(PASCAL FAR *pgethostname)(char FAR *name, int namelen);
extern struct hostent FAR *(PASCAL FAR *pgethostbyname)(const char FAR *name);
extern struct hostent FAR *(PASCAL FAR *pgethostbyaddr)(const char FAR *addr, int len, int type);
extern int(PASCAL FAR *pgetsockname)(SOCKET s, struct sockaddr FAR *name, int FAR *namelen);

#endif
