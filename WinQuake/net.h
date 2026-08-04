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
// net.h -- quake's interface to the networking layer
#pragma once

#include "qlimits.h" // MAX_DATAGRAM
#include "common.h"  // byte, qboolean, sizebuf_t
#include "cvar.h"    // cvar_t
#include "hunk_resource.h"
#include <vector>

struct qsockaddr
{
    short sa_family;
    unsigned char sa_data[14];
};

constexpr int NET_NAMELEN = 64;

constexpr int NET_MAXMESSAGE = 8192;
constexpr int NET_HEADERSIZE = (2 * sizeof(unsigned int));
constexpr int NET_DATAGRAMSIZE = (MAX_DATAGRAM + NET_HEADERSIZE);

// NetHeader flags
constexpr int NETFLAG_LENGTH_MASK = 0x0000ffff;
constexpr int NETFLAG_DATA = 0x00010000;
constexpr int NETFLAG_ACK = 0x00020000;
constexpr int NETFLAG_NAK = 0x00040000;
constexpr int NETFLAG_EOM = 0x00080000;
constexpr int NETFLAG_UNRELIABLE = 0x00100000;
constexpr int NETFLAG_CTL = 0x80000000;

constexpr int NET_PROTOCOL_VERSION = 3;

// This is the network info/connection protocol.  It is used to find Quake
// servers, get info about them, and connect to them.  Once connected, the
// Quake game protocol (documented elsewhere) is used.
//
//
// General notes:
//	game_name is currently always "QUAKE", but is there so this same protocol
//		can be used for future games as well; can you say Quake2?
//
// CCREQ_CONNECT
//		string	game_name				"QUAKE"
//		byte	net_protocol_version	NET_PROTOCOL_VERSION
//
// CCREQ_SERVER_INFO
//		string	game_name				"QUAKE"
//		byte	net_protocol_version	NET_PROTOCOL_VERSION
//
// CCREQ_PLAYER_INFO
//		byte	player_number
//
// CCREQ_RULE_INFO
//		string	rule
//
//
//
// CCREP_ACCEPT
//		long	port
//
// CCREP_REJECT
//		string	reason
//
// CCREP_SERVER_INFO
//		string	server_address
//		string	host_name
//		string	level_name
//		byte	current_players
//		byte	max_players
//		byte	protocol_version	NET_PROTOCOL_VERSION
//
// CCREP_PLAYER_INFO
//		byte	player_number
//		string	name
//		long	colors
//		long	frags
//		long	connect_time
//		string	address
//
// CCREP_RULE_INFO
//		string	rule
//		string	value

//	note:
//		There are two address forms used above.  The short form is just a
//		port number.  The address that goes along with the port is defined as
//		"whatever address you receive this reponse from".  This lets us use
//		the host OS to solve the problem of multiple host addresses (possibly
//		with no routing between them); the host will use the right address
//		when we reply to the inbound connection request.  The long from is
//		a full address and port in a string.  It is used for returning the
//		address of a server that is not running locally.

constexpr int CCREQ_CONNECT = 0x01;
constexpr int CCREQ_SERVER_INFO = 0x02;
constexpr int CCREQ_PLAYER_INFO = 0x03;
constexpr int CCREQ_RULE_INFO = 0x04;

constexpr int CCREP_ACCEPT = 0x81;
constexpr int CCREP_REJECT = 0x82;
constexpr int CCREP_SERVER_INFO = 0x83;
constexpr int CCREP_PLAYER_INFO = 0x84;
constexpr int CCREP_RULE_INFO = 0x85;

struct qsocket_t
{
    struct qsocket_t *next;
    double connecttime;
    double lastMessageTime;
    double lastSendTime;

    qboolean disconnected;
    qboolean canSend;
    qboolean sendNext;

    int driver;
    int landriver;
    int socket;
    void *driverdata;

    unsigned int ackSequence;
    unsigned int sendSequence;
    unsigned int unreliableSendSequence;
    int sendMessageLength;
    byte sendMessage[NET_MAXMESSAGE];

    unsigned int receiveSequence;
    unsigned int unreliableReceiveSequence;
    int receiveMessageLength;
    byte receiveMessage[NET_MAXMESSAGE];

    struct qsockaddr addr;
    char address[NET_NAMELEN];
};

// Core network connection state -- consolidated per an explicit scoping
// decision (this subsystem is much larger/more cross-cutting than
// console/sound/zone, and much harder to verify headlessly since there's
// no way to test a real multiplayer connection in this environment).
// This holds just the actively-used cross-cutting state. Left as
// standalone globals: the driver-selection tables (net_landrivers/
// net_drivers), the LAN server-browser hostcache, and VCR record/playback.
struct net_state_t
{
    qsocket_t *activeSockets = nullptr;
    qsocket_t *freeSockets = nullptr;
    int numsockets = 0;
    std::pmr::vector<qsocket_t> socketPool; // backing storage for freeSockets' initial pool

    qboolean ipxAvailable = false;
    qboolean tcpipAvailable = false;

    sizebuf_t message;
    int activeconnections = 0;
    double time = 0;

    int messagesSent = 0;
    int messagesReceived = 0;
    int unreliableMessagesSent = 0;
    int unreliableMessagesReceived = 0;
};

extern net_state_t net;

struct net_landriver_t
{
    const char *name;
    qboolean initialized;
    int controlSock;
    int (*Init)(void);
    void (*Shutdown)(void);
    void (*Listen)(qboolean state);
    int (*OpenSocket)(int port);
    int (*CloseSocket)(int socket);
    int (*Connect)(int socket, struct qsockaddr *addr);
    int (*CheckNewConnections)(void);
    int (*Read)(int socket, byte *buf, int len, struct qsockaddr *addr);
    int (*Write)(int socket, byte *buf, int len, struct qsockaddr *addr);
    int (*Broadcast)(int socket, byte *buf, int len);
    std::string (*AddrToString)(struct qsockaddr *addr);
    int (*StringToAddr)(const char *string, struct qsockaddr *addr);
    int (*GetSocketAddr)(int socket, struct qsockaddr *addr);
    int (*GetNameFromAddr)(struct qsockaddr *addr, char *name);
    int (*GetAddrFromName)(const char *name, struct qsockaddr *addr);
    int (*AddrCompare)(struct qsockaddr *addr1, struct qsockaddr *addr2);
    int (*GetSocketPort)(struct qsockaddr *addr);
    int (*SetSocketPort)(struct qsockaddr *addr, int port);
};

constexpr int MAX_NET_DRIVERS = 8;
extern int net_numlandrivers;
extern net_landriver_t net_landrivers[MAX_NET_DRIVERS];

struct net_driver_t
{
    const char *name;
    qboolean initialized;
    int (*Init)(void);
    void (*Listen)(qboolean state);
    void (*SearchForHosts)(qboolean xmit);
    qsocket_t *(*Connect)(const char *host);
    qsocket_t *(*CheckNewConnections)(void);
    int (*QGetMessage)(qsocket_t *sock);
    int (*QSendMessage)(qsocket_t *sock, sizebuf_t *data);
    int (*SendUnreliableMessage)(qsocket_t *sock, sizebuf_t *data);
    qboolean (*CanSendMessage)(qsocket_t *sock);
    qboolean (*CanSendUnreliableMessage)(qsocket_t *sock);
    void (*Close)(qsocket_t *sock);
    void (*Shutdown)(void);
    int controlSock;
};

extern int net_numdrivers;
extern net_driver_t net_drivers[MAX_NET_DRIVERS];

extern int DEFAULTnet_hostport;
extern int net_hostport;

extern int net_driverlevel;
extern cvar_t hostname;
extern char playername[];
extern int playercolor;

qsocket_t *NET_NewQSocket(void);
void NET_FreeQSocket(qsocket_t *);
double SetNetTime(void);

constexpr int HOSTCACHESIZE = 8;

struct hostcache_t
{
    char name[16];
    char map[16];
    char cname[32];
    int users;
    int maxusers;
    int driver;
    int ldriver;
    struct qsockaddr addr;
};

extern int hostCacheCount;
extern hostcache_t hostcache[HOSTCACHESIZE];

//============================================================================
//
// public network functions
//
//============================================================================

void NET_Init(void);
void NET_Shutdown(void);

struct qsocket_t *NET_CheckNewConnections(void);
// returns a new connection number if there is one pending, else -1

struct qsocket_t *NET_Connect(const char *host);
// called by client to connect to a host.  Returns -1 if not able to

qboolean NET_CanSendMessage(qsocket_t *sock);
// Returns true or false if the given qsocket can currently accept a
// message to be transmitted.

int NET_GetMessage(struct qsocket_t *sock);
// returns data in net_message sizebuf
// returns 0 if no data is waiting
// returns 1 if a message was received
// returns 2 if an unreliable message was received
// returns -1 if the connection died

int NET_SendMessage(struct qsocket_t *sock, sizebuf_t *data);
int NET_SendUnreliableMessage(struct qsocket_t *sock, sizebuf_t *data);
// returns 0 if the message connot be delivered reliably, but the connection
//		is still considered valid
// returns 1 if the message was sent properly
// returns -1 if the connection died

int NET_SendToAll(sizebuf_t *data, int blocktime);
// This is a reliable *blocking* send to all attached clients.

void NET_Close(struct qsocket_t *sock);
// if a dead connection is returned by a get or send function, this function
// should be called when it is convenient

// Server calls when a client is kicked off for a game related misbehavior
// like an illegal protocal conversation.  Client calls when disconnecting
// from a server.
// A netcon_t number will not be reused until this function is called for it

void NET_Poll(void);

struct PollProcedure
{
    struct PollProcedure *next;
    double nextTime;
    void (*procedure)();
    void *arg;
};

void SchedulePollProcedure(PollProcedure *pp, double timeOffset);

std::string NET_IPXAddressString(void);
std::string NET_TCPIPAddressString(void);

extern qboolean slistInProgress;
extern qboolean slistSilent;
extern qboolean slistLocal;

void NET_Slist_f(void);
