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
// net_dgrm.c

// This is enables a simple IP banning mechanism
#define BAN_TEST

#ifdef BAN_TEST
#if defined(_WIN32)
#include <windows.h>
#elif defined(NeXT)
#include <sys/socket.h>
#include <arpa/inet.h>
#else
#define AF_INET 2          /* internet */
struct in_addr
{
    union {
        struct
        {
            unsigned char s_b1, s_b2, s_b3, s_b4;
        } S_un_b;
        struct
        {
            unsigned short s_w1, s_w2;
        } S_un_w;
        unsigned long S_addr;
    } S_un;
};
#define s_addr S_un.S_addr /* can be used for most tcp & ip code */
struct sockaddr_in
{
    short sin_family;
    unsigned short sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
};
char *inet_ntoa(struct in_addr in);
unsigned long inet_addr(const char *cp);
#endif
#endif // BAN_TEST

#include "quakedef.h"
#include "net_dgrm.h"

// these two macros are to make the code more readable
#define sfunc net_landrivers[sock->landriver]
#define dfunc net_landrivers[net_landriverlevel]

static int net_landriverlevel;

/* statistic counters */
int packetsSent = 0;
int packetsReSent = 0;
int packetsReceived = 0;
int receivedDuplicateCount = 0;
int shortPacketCount = 0;
int droppedDatagrams;

static int myDriverLevel;

struct
{
    unsigned int length;
    unsigned int sequence;
    byte data[MAX_DATAGRAM];
} packetBuffer;

extern int m_return_state;
extern m_state_t m_state;
extern qboolean m_return_onerror;
extern char m_return_reason[32];

#ifdef DEBUG
std::string StrAddr(struct qsockaddr *addr)
{
    char buf[34];
    byte *p = reinterpret_cast<byte *>(addr);
    int n;

    for (n = 0; n < 16; n++)
    {
        auto result = std::format_to_n(buf + n * 2, 2, "{:02x}", *p++);
        *result.out = '\0';
    }
    return buf;
}
#endif

#ifdef BAN_TEST
unsigned long banAddr = 0x00000000;
unsigned long banMask = 0xffffffff;

void NET_Ban_f(void)
{
    char addrStr[32];
    char maskStr[32];
    void (*print)(const char *fmt, ...);

    if (cmd_source == cmd_source_t::src_command)
    {
        if (!sv.active)
        {
            Cmd_ForwardToServer();
            return;
        }
        print = Con_Printf;
    }
    else
    {
        if (deathmatch.value && !host_client->privileged)
        {
            return;
        }
        print = SV_ClientPrintf;
    }

    switch (Cmd_Argc())
    {
    case 1:
        if (((struct in_addr *)&banAddr)->s_addr)
        {
            Q_strlcpy(addrStr, inet_ntoa(*(struct in_addr *)&banAddr), sizeof(addrStr));
            Q_strlcpy(maskStr, inet_ntoa(*(struct in_addr *)&banMask), sizeof(maskStr));
            print("Banning %s [%s]\n", addrStr, maskStr);
        }
        else
        {
            print("Banning not active\n");
        }
        break;

    case 2:
        if (Q_strcasecmp(Cmd_Argv(1), "off") == 0)
        {
            banAddr = 0x00000000;
        }
        else
        {
            banAddr = inet_addr(Cmd_Argv(1));
        }
        banMask = 0xffffffff;
        break;

    case 3:
        banAddr = inet_addr(Cmd_Argv(1));
        banMask = inet_addr(Cmd_Argv(2));
        break;

    default:
        print("BAN ip_address [mask]\n");
        break;
    }
}
#endif

int Datagram_SendMessage(qsocket_t *sock, sizebuf_t *data)
{
    unsigned int packetLen;
    unsigned int dataLen;
    unsigned int eom;

#ifdef DEBUG
    if (data->cursize == 0)
    {
        Sys_Error("Datagram_SendMessage: zero length message\n");
    }

    if (data->cursize > NET_MAXMESSAGE)
    {
        Sys_Error("Datagram_SendMessage: message too big %u\n", data->cursize);
    }

    if (sock->canSend == false)
    {
        Sys_Error("SendMessage: called with canSend == false\n");
    }
#endif

    Q_memcpy(sock->sendMessage, data->data, data->cursize);
    sock->sendMessageLength = data->cursize;

    if (data->cursize <= MAX_DATAGRAM)
    {
        dataLen = data->cursize;
        eom = NETFLAG_EOM;
    }
    else
    {
        dataLen = MAX_DATAGRAM;
        eom = 0;
    }
    packetLen = NET_HEADERSIZE + dataLen;

    packetBuffer.length = BigLong(packetLen | (NETFLAG_DATA | eom));
    packetBuffer.sequence = BigLong(sock->sendSequence++);
    Q_memcpy(packetBuffer.data, sock->sendMessage, dataLen);

    sock->canSend = false;

    if (sfunc.Write(sock->socket, reinterpret_cast<byte *>(&packetBuffer), packetLen, &sock->addr) == -1)
    {
        return -1;
    }

    sock->lastSendTime = net.time;
    packetsSent++;
    return 1;
}

int SendMessageNext(qsocket_t *sock)
{
    unsigned int packetLen;
    unsigned int dataLen;
    unsigned int eom;

    if (sock->sendMessageLength <= MAX_DATAGRAM)
    {
        dataLen = sock->sendMessageLength;
        eom = NETFLAG_EOM;
    }
    else
    {
        dataLen = MAX_DATAGRAM;
        eom = 0;
    }
    packetLen = NET_HEADERSIZE + dataLen;

    packetBuffer.length = BigLong(packetLen | (NETFLAG_DATA | eom));
    packetBuffer.sequence = BigLong(sock->sendSequence++);
    Q_memcpy(packetBuffer.data, sock->sendMessage, dataLen);

    sock->sendNext = false;

    if (sfunc.Write(sock->socket, reinterpret_cast<byte *>(&packetBuffer), packetLen, &sock->addr) == -1)
    {
        return -1;
    }

    sock->lastSendTime = net.time;
    packetsSent++;
    return 1;
}

int ReSendMessage(qsocket_t *sock)
{
    unsigned int packetLen;
    unsigned int dataLen;
    unsigned int eom;

    if (sock->sendMessageLength <= MAX_DATAGRAM)
    {
        dataLen = sock->sendMessageLength;
        eom = NETFLAG_EOM;
    }
    else
    {
        dataLen = MAX_DATAGRAM;
        eom = 0;
    }
    packetLen = NET_HEADERSIZE + dataLen;

    packetBuffer.length = BigLong(packetLen | (NETFLAG_DATA | eom));
    packetBuffer.sequence = BigLong(sock->sendSequence - 1);
    Q_memcpy(packetBuffer.data, sock->sendMessage, dataLen);

    sock->sendNext = false;

    if (sfunc.Write(sock->socket, reinterpret_cast<byte *>(&packetBuffer), packetLen, &sock->addr) == -1)
    {
        return -1;
    }

    sock->lastSendTime = net.time;
    packetsReSent++;
    return 1;
}

qboolean Datagram_CanSendMessage(qsocket_t *sock)
{
    if (sock->sendNext)
    {
        SendMessageNext(sock);
    }

    return sock->canSend;
}

qboolean Datagram_CanSendUnreliableMessage(qsocket_t *sock)
{
    return true;
}

int Datagram_SendUnreliableMessage(qsocket_t *sock, sizebuf_t *data)
{
    int packetLen;

#ifdef DEBUG
    if (data->cursize == 0)
    {
        Sys_Error("Datagram_SendUnreliableMessage: zero length message\n");
    }

    if (data->cursize > MAX_DATAGRAM)
    {
        Sys_Error("Datagram_SendUnreliableMessage: message too big %u\n", data->cursize);
    }
#endif

    packetLen = NET_HEADERSIZE + data->cursize;

    packetBuffer.length = BigLong(packetLen | NETFLAG_UNRELIABLE);
    packetBuffer.sequence = BigLong(sock->unreliableSendSequence++);
    Q_memcpy(packetBuffer.data, data->data, data->cursize);

    if (sfunc.Write(sock->socket, reinterpret_cast<byte *>(&packetBuffer), packetLen, &sock->addr) == -1)
    {
        return -1;
    }

    packetsSent++;
    return 1;
}

int Datagram_GetMessage(qsocket_t *sock)
{
    unsigned int length;
    unsigned int flags;
    int ret = 0;
    struct qsockaddr readaddr;
    unsigned int sequence;
    unsigned int count;

    if (!sock->canSend)
    {
        if ((net.time - sock->lastSendTime) > 1.0)
        {
            ReSendMessage(sock);
        }
    }

    while (1)
    {
        length = sfunc.Read(sock->socket, reinterpret_cast<byte *>(&packetBuffer), NET_DATAGRAMSIZE, &readaddr);

        //	if ((rand() & 255) > 220)
        //		continue;

        if (length == 0)
        {
            break;
        }

        if (length == -1)
        {
            Con_Printf("Read error\n");
            return -1;
        }

        if (sfunc.AddrCompare(&readaddr, &sock->addr) != 0)
        {
#ifdef DEBUG
            Con_DPrintf("Forged packet received\n");
            Con_DPrintf("Expected: %s\n", StrAddr(&sock->addr).c_str());
            Con_DPrintf("Received: %s\n", StrAddr(&readaddr).c_str());
#endif
            continue;
        }

        if (length < NET_HEADERSIZE)
        {
            shortPacketCount++;
            continue;
        }

        length = BigLong(packetBuffer.length);
        flags = length & (~NETFLAG_LENGTH_MASK);
        length &= NETFLAG_LENGTH_MASK;

        if (flags & NETFLAG_CTL)
        {
            continue;
        }

        sequence = BigLong(packetBuffer.sequence);
        packetsReceived++;

        if (flags & NETFLAG_UNRELIABLE)
        {
            if (sequence < sock->unreliableReceiveSequence)
            {
                Con_DPrintf("Got a stale datagram\n");
                ret = 0;
                break;
            }
            if (sequence != sock->unreliableReceiveSequence)
            {
                count = sequence - sock->unreliableReceiveSequence;
                droppedDatagrams += count;
                Con_DPrintf("Dropped %u datagram(s)\n", count);
            }
            sock->unreliableReceiveSequence = sequence + 1;

            length -= NET_HEADERSIZE;

            SZ_Clear(&net.message);
            SZ_Write(&net.message, packetBuffer.data, length);

            ret = 2;
            break;
        }

        if (flags & NETFLAG_ACK)
        {
            if (sequence != (sock->sendSequence - 1))
            {
                Con_DPrintf("Stale ACK received\n");
                continue;
            }
            if (sequence == sock->ackSequence)
            {
                sock->ackSequence++;
                if (sock->ackSequence != sock->sendSequence)
                {
                    Con_DPrintf("ack sequencing error\n");
                }
            }
            else
            {
                Con_DPrintf("Duplicate ACK received\n");
                continue;
            }
            sock->sendMessageLength -= MAX_DATAGRAM;
            if (sock->sendMessageLength > 0)
            {
                Q_memcpy(sock->sendMessage, sock->sendMessage + MAX_DATAGRAM, sock->sendMessageLength);
                sock->sendNext = true;
            }
            else
            {
                sock->sendMessageLength = 0;
                sock->canSend = true;
            }
            continue;
        }

        if (flags & NETFLAG_DATA)
        {
            packetBuffer.length = BigLong(NET_HEADERSIZE | NETFLAG_ACK);
            packetBuffer.sequence = BigLong(sequence);
            sfunc.Write(sock->socket, reinterpret_cast<byte *>(&packetBuffer), NET_HEADERSIZE, &readaddr);

            if (sequence != sock->receiveSequence)
            {
                receivedDuplicateCount++;
                continue;
            }
            sock->receiveSequence++;

            length -= NET_HEADERSIZE;

            if (flags & NETFLAG_EOM)
            {
                SZ_Clear(&net.message);
                SZ_Write(&net.message, sock->receiveMessage, sock->receiveMessageLength);
                SZ_Write(&net.message, packetBuffer.data, length);
                sock->receiveMessageLength = 0;

                ret = 1;
                break;
            }

            Q_memcpy(sock->receiveMessage + sock->receiveMessageLength, packetBuffer.data, length);
            sock->receiveMessageLength += length;
            continue;
        }
    }

    if (sock->sendNext)
    {
        SendMessageNext(sock);
    }

    return ret;
}

void PrintStats(qsocket_t *s)
{
    Con_Printf("canSend = %4u   \n", s->canSend);
    Con_Printf("sendSeq = %4u   ", s->sendSequence);
    Con_Printf("recvSeq = %4u   \n", s->receiveSequence);
    Con_Printf("\n");
}

void NET_Stats_f(void)
{
    qsocket_t *s;

    if (Cmd_Argc() == 1)
    {
        Con_Printf("unreliable messages sent   = %i\n", net.unreliableMessagesSent);
        Con_Printf("unreliable messages recv   = %i\n", net.unreliableMessagesReceived);
        Con_Printf("reliable messages sent     = %i\n", net.messagesSent);
        Con_Printf("reliable messages received = %i\n", net.messagesReceived);
        Con_Printf("packetsSent                = %i\n", packetsSent);
        Con_Printf("packetsReSent              = %i\n", packetsReSent);
        Con_Printf("packetsReceived            = %i\n", packetsReceived);
        Con_Printf("receivedDuplicateCount     = %i\n", receivedDuplicateCount);
        Con_Printf("shortPacketCount           = %i\n", shortPacketCount);
        Con_Printf("droppedDatagrams           = %i\n", droppedDatagrams);
    }
    else if (Q_strcmp(Cmd_Argv(1), "*") == 0)
    {
        for (s = net.activeSockets; s; s = s->next)
        {
            PrintStats(s);
        }
        for (s = net.freeSockets; s; s = s->next)
        {
            PrintStats(s);
        }
    }
    else
    {
        for (s = net.activeSockets; s; s = s->next)
        {
            if (Q_strcasecmp(Cmd_Argv(1), s->address) == 0)
            {
                break;
            }
        }
        if (s == nullptr)
        {
            for (s = net.freeSockets; s; s = s->next)
            {
                if (Q_strcasecmp(Cmd_Argv(1), s->address) == 0)
                {
                    break;
                }
            }
        }
        if (s == nullptr)
        {
            return;
        }
        PrintStats(s);
    }
}

static qboolean testInProgress = false;
static int testPollCount;
static int testDriver;
static int testSocket;

static void Test_Poll(void);
PollProcedure testPollProcedure = {nullptr, 0.0, Test_Poll};

static void Test_Poll(void)
{
    struct qsockaddr clientaddr;
    int control;
    int len;
    char name[32];
    char address[64];
    int colors;
    int frags;
    int connectTime;
    byte playerNumber;

    net_landriverlevel = testDriver;

    while (1)
    {
        len = dfunc.Read(testSocket, net.message.data, net.message.maxsize, &clientaddr);
        if (len < sizeof(int))
        {
            break;
        }

        net.message.cursize = len;

        MSG_BeginReading();
        control = BigLong(*(reinterpret_cast<int *>(net.message.data)));
        MSG_ReadLong();
        if (control == -1)
        {
            break;
        }
        if ((control & (~NETFLAG_LENGTH_MASK)) != NETFLAG_CTL)
        {
            break;
        }
        if ((control & NETFLAG_LENGTH_MASK) != len)
        {
            break;
        }

        if (MSG_ReadByte() != CCREP_PLAYER_INFO)
        {
            Sys_Error("Unexpected repsonse to Player Info request\n");
        }

        playerNumber = MSG_ReadByte();
        Q_strlcpy(name, MSG_ReadString().c_str(), sizeof(name));
        colors = MSG_ReadLong();
        frags = MSG_ReadLong();
        connectTime = MSG_ReadLong();
        Q_strlcpy(address, MSG_ReadString().c_str(), sizeof(address));

        Con_Printf("%s\n  frags:%3i  colors:%u %u  time:%u\n  %s\n", name, frags, colors >> 4, colors & 0x0f,
                   connectTime / 60, address);
    }

    testPollCount--;
    if (testPollCount)
    {
        SchedulePollProcedure(&testPollProcedure, 0.1);
    }
    else
    {
        dfunc.CloseSocket(testSocket);
        testInProgress = false;
    }
}

static void Test_f(void)
{
    const char *host;
    int n;
    int max = MAX_SCOREBOARD;
    struct qsockaddr sendaddr;

    if (testInProgress)
    {
        return;
    }

    host = Cmd_Argv(1);

    bool foundCached = false;
    if (host && hostCacheCount)
    {
        for (n = 0; n < hostCacheCount; n++)
        {
            if (Q_strcasecmp(host, hostcache[n].name) == 0)
            {
                if (hostcache[n].driver != myDriverLevel)
                {
                    continue;
                }
                net_landriverlevel = hostcache[n].ldriver;
                max = hostcache[n].maxusers;
                Q_memcpy(&sendaddr, &hostcache[n].addr, sizeof(struct qsockaddr));
                break;
            }
        }
        foundCached = (n < hostCacheCount);
    }

    if (!foundCached)
    {
        for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
        {
            if (!net_landrivers[net_landriverlevel].initialized)
            {
                continue;
            }

            // see if we can resolve the host name
            if (dfunc.GetAddrFromName(host, &sendaddr) != -1)
            {
                break;
            }
        }
        if (net_landriverlevel == net_numlandrivers)
        {
            return;
        }
    }

    testSocket = dfunc.OpenSocket(0);
    if (testSocket == -1)
    {
        return;
    }

    testInProgress = true;
    testPollCount = 20;
    testDriver = net_landriverlevel;

    for (n = 0; n < max; n++)
    {
        SZ_Clear(&net.message);
        // save space for the header, filled in later
        MSG_WriteLong(&net.message, 0);
        MSG_WriteByte(&net.message, CCREQ_PLAYER_INFO);
        MSG_WriteByte(&net.message, n);
        *(reinterpret_cast<int *>(net.message.data)) =
            BigLong(NETFLAG_CTL | (net.message.cursize & NETFLAG_LENGTH_MASK));
        dfunc.Write(testSocket, net.message.data, net.message.cursize, &sendaddr);
    }
    SZ_Clear(&net.message);
    SchedulePollProcedure(&testPollProcedure, 0.1);
}

static qboolean test2InProgress = false;
static int test2Driver;
static int test2Socket;

static void Test2_Poll(void);
PollProcedure test2PollProcedure = {nullptr, 0.0, Test2_Poll};

static void Test2_Poll(void)
{
    struct qsockaddr clientaddr;
    int control;
    int len;
    char name[256];
    char value[256];

    net_landriverlevel = test2Driver;
    name[0] = 0;

    len = dfunc.Read(test2Socket, net.message.data, net.message.maxsize, &clientaddr);
    if (len >= sizeof(int))
    {
        net.message.cursize = len;

        MSG_BeginReading();
        control = BigLong(*(reinterpret_cast<int *>(net.message.data)));
        MSG_ReadLong();
        if (control == -1 || (control & (~NETFLAG_LENGTH_MASK)) != NETFLAG_CTL || (control & NETFLAG_LENGTH_MASK) != len)
        {
            Con_Printf("Unexpected repsonse to Rule Info request\n");
            dfunc.CloseSocket(test2Socket);
            test2InProgress = false;
            return;
        }

        if (MSG_ReadByte() != CCREP_RULE_INFO)
        {
            Con_Printf("Unexpected repsonse to Rule Info request\n");
            dfunc.CloseSocket(test2Socket);
            test2InProgress = false;
            return;
        }

        Q_strlcpy(name, MSG_ReadString().c_str(), sizeof(name));
        if (name[0] == 0)
        {
            dfunc.CloseSocket(test2Socket);
            test2InProgress = false;
            return;
        }
        Q_strlcpy(value, MSG_ReadString().c_str(), sizeof(value));

        Con_Printf("%-16.16s  %-16.16s\n", name, value);

        SZ_Clear(&net.message);
        // save space for the header, filled in later
        MSG_WriteLong(&net.message, 0);
        MSG_WriteByte(&net.message, CCREQ_RULE_INFO);
        MSG_WriteString(&net.message, name);
        *(reinterpret_cast<int *>(net.message.data)) =
            BigLong(NETFLAG_CTL | (net.message.cursize & NETFLAG_LENGTH_MASK));
        dfunc.Write(test2Socket, net.message.data, net.message.cursize, &clientaddr);
        SZ_Clear(&net.message);
    }

    SchedulePollProcedure(&test2PollProcedure, 0.05);
}

static void Test2_f(void)
{
    const char *host;
    int n;
    struct qsockaddr sendaddr;

    if (test2InProgress)
    {
        return;
    }

    host = Cmd_Argv(1);

    bool foundCached = false;
    if (host && hostCacheCount)
    {
        for (n = 0; n < hostCacheCount; n++)
        {
            if (Q_strcasecmp(host, hostcache[n].name) == 0)
            {
                if (hostcache[n].driver != myDriverLevel)
                {
                    continue;
                }
                net_landriverlevel = hostcache[n].ldriver;
                Q_memcpy(&sendaddr, &hostcache[n].addr, sizeof(struct qsockaddr));
                break;
            }
        }
        foundCached = (n < hostCacheCount);
    }

    if (!foundCached)
    {
        for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
        {
            if (!net_landrivers[net_landriverlevel].initialized)
            {
                continue;
            }

            // see if we can resolve the host name
            if (dfunc.GetAddrFromName(host, &sendaddr) != -1)
            {
                break;
            }
        }
        if (net_landriverlevel == net_numlandrivers)
        {
            return;
        }
    }

    test2Socket = dfunc.OpenSocket(0);
    if (test2Socket == -1)
    {
        return;
    }

    test2InProgress = true;
    test2Driver = net_landriverlevel;

    SZ_Clear(&net.message);
    // save space for the header, filled in later
    MSG_WriteLong(&net.message, 0);
    MSG_WriteByte(&net.message, CCREQ_RULE_INFO);
    MSG_WriteString(&net.message, "");
    *(reinterpret_cast<int *>(net.message.data)) = BigLong(NETFLAG_CTL | (net.message.cursize & NETFLAG_LENGTH_MASK));
    dfunc.Write(test2Socket, net.message.data, net.message.cursize, &sendaddr);
    SZ_Clear(&net.message);
    SchedulePollProcedure(&test2PollProcedure, 0.05);
}

int Datagram_Init(void)
{
    int i;
    int csock;

    myDriverLevel = net_driverlevel;
    Cmd_AddCommand("net_stats", NET_Stats_f);

    if (COM_CheckParm("-nolan"))
    {
        return -1;
    }

    for (i = 0; i < net_numlandrivers; i++)
    {
        csock = net_landrivers[i].Init();
        if (csock == -1)
        {
            continue;
        }
        net_landrivers[i].initialized = true;
        net_landrivers[i].controlSock = csock;
    }

#ifdef BAN_TEST
    Cmd_AddCommand("ban", NET_Ban_f);
#endif
    Cmd_AddCommand("test", Test_f);
    Cmd_AddCommand("test2", Test2_f);

    return 0;
}

void Datagram_Shutdown(void)
{
    int i;

    //
    // shutdown the lan drivers
    //
    for (i = 0; i < net_numlandrivers; i++)
    {
        if (net_landrivers[i].initialized)
        {
            net_landrivers[i].Shutdown();
            net_landrivers[i].initialized = false;
        }
    }
}

void Datagram_Close(qsocket_t *sock)
{
    sfunc.CloseSocket(sock->socket);
}

void Datagram_Listen(qboolean state)
{
    int i;

    for (i = 0; i < net_numlandrivers; i++)
    {
        if (net_landrivers[i].initialized)
        {
            net_landrivers[i].Listen(state);
        }
    }
}

static qsocket_t *_Datagram_CheckNewConnections(void)
{
    struct qsockaddr clientaddr;
    struct qsockaddr newaddr;
    int newsock;
    int acceptsock;
    qsocket_t *sock;
    qsocket_t *s;
    int len;
    int command;
    int control;
    int ret;

    acceptsock = dfunc.CheckNewConnections();
    if (acceptsock == -1)
    {
        return nullptr;
    }

    SZ_Clear(&net.message);

    len = dfunc.Read(acceptsock, net.message.data, net.message.maxsize, &clientaddr);
    if (len < sizeof(int))
    {
        return nullptr;
    }
    net.message.cursize = len;

    MSG_BeginReading();
    control = BigLong(*(reinterpret_cast<int *>(net.message.data)));
    MSG_ReadLong();
    if (control == -1)
    {
        return nullptr;
    }
    if ((control & (~NETFLAG_LENGTH_MASK)) != NETFLAG_CTL)
    {
        return nullptr;
    }
    if ((control & NETFLAG_LENGTH_MASK) != len)
    {
        return nullptr;
    }

    command = MSG_ReadByte();
    if (command == CCREQ_SERVER_INFO)
    {
        if (Q_strcmp(MSG_ReadString().c_str(), "QUAKE") != 0)
        {
            return nullptr;
        }

        SZ_Clear(&net.message);
        // save space for the header, filled in later
        MSG_WriteLong(&net.message, 0);
        MSG_WriteByte(&net.message, CCREP_SERVER_INFO);
        dfunc.GetSocketAddr(acceptsock, &newaddr);
        MSG_WriteString(&net.message, dfunc.AddrToString(&newaddr).c_str());
        MSG_WriteString(&net.message, hostname.string.c_str());
        MSG_WriteString(&net.message, sv.name);
        MSG_WriteByte(&net.message, net.activeconnections);
        MSG_WriteByte(&net.message, svs.maxclients);
        MSG_WriteByte(&net.message, NET_PROTOCOL_VERSION);
        *(reinterpret_cast<int *>(net.message.data)) =
            BigLong(NETFLAG_CTL | (net.message.cursize & NETFLAG_LENGTH_MASK));
        dfunc.Write(acceptsock, net.message.data, net.message.cursize, &clientaddr);
        SZ_Clear(&net.message);
        return nullptr;
    }

    if (command == CCREQ_PLAYER_INFO)
    {
        int playerNumber;
        int activeNumber;
        int clientNumber;
        client_t *client;

        playerNumber = MSG_ReadByte();
        activeNumber = -1;
        for (clientNumber = 0, client = svs.clients; clientNumber < svs.maxclients; clientNumber++, client++)
        {
            if (client->active)
            {
                activeNumber++;
                if (activeNumber == playerNumber)
                {
                    break;
                }
            }
        }
        if (clientNumber == svs.maxclients)
        {
            return nullptr;
        }

        SZ_Clear(&net.message);
        // save space for the header, filled in later
        MSG_WriteLong(&net.message, 0);
        MSG_WriteByte(&net.message, CCREP_PLAYER_INFO);
        MSG_WriteByte(&net.message, playerNumber);
        MSG_WriteString(&net.message, client->name);
        MSG_WriteLong(&net.message, client->colors);
        MSG_WriteLong(&net.message, (int)client->edict->v.frags);
        MSG_WriteLong(&net.message, (int)(net.time - client->netconnection->connecttime));
        MSG_WriteString(&net.message, client->netconnection->address);
        *(reinterpret_cast<int *>(net.message.data)) =
            BigLong(NETFLAG_CTL | (net.message.cursize & NETFLAG_LENGTH_MASK));
        dfunc.Write(acceptsock, net.message.data, net.message.cursize, &clientaddr);
        SZ_Clear(&net.message);

        return nullptr;
    }

    if (command == CCREQ_RULE_INFO)
    {
        std::string prevCvarName;
        cvar_t *var;

        // find the next server cvar after prevCvarName ("" means start
        // from the beginning). An unknown prevCvarName drops the request
        // silently (no response sent) -- distinct from a valid search
        // that simply finds no more server cvars, which still gets an
        // (empty) reply below; Cvar_NextServerVar's own nullptr doesn't
        // distinguish those, so check validity here first.
        prevCvarName = MSG_ReadString();
        if (!prevCvarName.empty() && !Cvar_FindVar(prevCvarName.c_str()))
        {
            return nullptr;
        }
        var = Cvar_NextServerVar(prevCvarName.c_str());

        // send the response

        SZ_Clear(&net.message);
        // save space for the header, filled in later
        MSG_WriteLong(&net.message, 0);
        MSG_WriteByte(&net.message, CCREP_RULE_INFO);
        if (var)
        {
            MSG_WriteString(&net.message, var->name);
            MSG_WriteString(&net.message, var->string.c_str());
        }
        *(reinterpret_cast<int *>(net.message.data)) =
            BigLong(NETFLAG_CTL | (net.message.cursize & NETFLAG_LENGTH_MASK));
        dfunc.Write(acceptsock, net.message.data, net.message.cursize, &clientaddr);
        SZ_Clear(&net.message);

        return nullptr;
    }

    if (command != CCREQ_CONNECT)
    {
        return nullptr;
    }

    if (Q_strcmp(MSG_ReadString().c_str(), "QUAKE") != 0)
    {
        return nullptr;
    }

    if (MSG_ReadByte() != NET_PROTOCOL_VERSION)
    {
        SZ_Clear(&net.message);
        // save space for the header, filled in later
        MSG_WriteLong(&net.message, 0);
        MSG_WriteByte(&net.message, CCREP_REJECT);
        MSG_WriteString(&net.message, "Incompatible version.\n");
        *(reinterpret_cast<int *>(net.message.data)) =
            BigLong(NETFLAG_CTL | (net.message.cursize & NETFLAG_LENGTH_MASK));
        dfunc.Write(acceptsock, net.message.data, net.message.cursize, &clientaddr);
        SZ_Clear(&net.message);
        return nullptr;
    }

#ifdef BAN_TEST
    // check for a ban
    if (clientaddr.sa_family == AF_INET)
    {
        unsigned long testAddr;
        testAddr = ((struct sockaddr_in *)&clientaddr)->sin_addr.s_addr;
        if ((testAddr & banMask) == banAddr)
        {
            SZ_Clear(&net.message);
            // save space for the header, filled in later
            MSG_WriteLong(&net.message, 0);
            MSG_WriteByte(&net.message, CCREP_REJECT);
            MSG_WriteString(&net.message, "You have been banned.\n");
            *(reinterpret_cast<int *>(net.message.data)) =
                BigLong(NETFLAG_CTL | (net.message.cursize & NETFLAG_LENGTH_MASK));
            dfunc.Write(acceptsock, net.message.data, net.message.cursize, &clientaddr);
            SZ_Clear(&net.message);
            return nullptr;
        }
    }
#endif

    // see if this guy is already connected
    for (s = net.activeSockets; s; s = s->next)
    {
        if (s->driver != net_driverlevel)
        {
            continue;
        }
        ret = dfunc.AddrCompare(&clientaddr, &s->addr);
        if (ret >= 0)
        {
            // is this a duplicate connection reqeust?
            if (ret == 0 && net.time - s->connecttime < 2.0)
            {
                // yes, so send a duplicate reply
                SZ_Clear(&net.message);
                // save space for the header, filled in later
                MSG_WriteLong(&net.message, 0);
                MSG_WriteByte(&net.message, CCREP_ACCEPT);
                dfunc.GetSocketAddr(s->socket, &newaddr);
                MSG_WriteLong(&net.message, dfunc.GetSocketPort(&newaddr));
                *(reinterpret_cast<int *>(net.message.data)) =
                    BigLong(NETFLAG_CTL | (net.message.cursize & NETFLAG_LENGTH_MASK));
                dfunc.Write(acceptsock, net.message.data, net.message.cursize, &clientaddr);
                SZ_Clear(&net.message);
                return nullptr;
            }
            // it's somebody coming back in from a crash/disconnect
            // so close the old qsocket and let their retry get them back in
            NET_Close(s);
            return nullptr;
        }
    }

    // allocate a QSocket
    sock = NET_NewQSocket();
    if (sock == nullptr)
    {
        // no room; try to let him know
        SZ_Clear(&net.message);
        // save space for the header, filled in later
        MSG_WriteLong(&net.message, 0);
        MSG_WriteByte(&net.message, CCREP_REJECT);
        MSG_WriteString(&net.message, "Server is full.\n");
        *(reinterpret_cast<int *>(net.message.data)) =
            BigLong(NETFLAG_CTL | (net.message.cursize & NETFLAG_LENGTH_MASK));
        dfunc.Write(acceptsock, net.message.data, net.message.cursize, &clientaddr);
        SZ_Clear(&net.message);
        return nullptr;
    }

    // allocate a network socket
    newsock = dfunc.OpenSocket(0);
    if (newsock == -1)
    {
        NET_FreeQSocket(sock);
        return nullptr;
    }

    // connect to the client
    if (dfunc.Connect(newsock, &clientaddr) == -1)
    {
        dfunc.CloseSocket(newsock);
        NET_FreeQSocket(sock);
        return nullptr;
    }

    // everything is allocated, just fill in the details
    sock->socket = newsock;
    sock->landriver = net_landriverlevel;
    sock->addr = clientaddr;
    Q_strlcpy(sock->address, dfunc.AddrToString(&clientaddr).c_str(), sizeof(sock->address));

    // send him back the info about the server connection he has been allocated
    SZ_Clear(&net.message);
    // save space for the header, filled in later
    MSG_WriteLong(&net.message, 0);
    MSG_WriteByte(&net.message, CCREP_ACCEPT);
    dfunc.GetSocketAddr(newsock, &newaddr);
    MSG_WriteLong(&net.message, dfunc.GetSocketPort(&newaddr));
    //	MSG_WriteString(&net.message, dfunc.AddrToString(&newaddr));
    *(reinterpret_cast<int *>(net.message.data)) = BigLong(NETFLAG_CTL | (net.message.cursize & NETFLAG_LENGTH_MASK));
    dfunc.Write(acceptsock, net.message.data, net.message.cursize, &clientaddr);
    SZ_Clear(&net.message);

    return sock;
}

qsocket_t *Datagram_CheckNewConnections(void)
{
    qsocket_t *ret = nullptr;

    for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
    {
        if (net_landrivers[net_landriverlevel].initialized)
        {
            if ((ret = _Datagram_CheckNewConnections()) != nullptr)
            {
                break;
            }
        }
    }
    return ret;
}

static void _Datagram_SearchForHosts(qboolean xmit)
{
    int ret;
    int n;
    int i;
    struct qsockaddr readaddr;
    struct qsockaddr myaddr;
    int control;

    dfunc.GetSocketAddr(dfunc.controlSock, &myaddr);
    if (xmit)
    {
        SZ_Clear(&net.message);
        // save space for the header, filled in later
        MSG_WriteLong(&net.message, 0);
        MSG_WriteByte(&net.message, CCREQ_SERVER_INFO);
        MSG_WriteString(&net.message, "QUAKE");
        MSG_WriteByte(&net.message, NET_PROTOCOL_VERSION);
        *(reinterpret_cast<int *>(net.message.data)) =
            BigLong(NETFLAG_CTL | (net.message.cursize & NETFLAG_LENGTH_MASK));
        dfunc.Broadcast(dfunc.controlSock, net.message.data, net.message.cursize);
        SZ_Clear(&net.message);
    }

    while ((ret = dfunc.Read(dfunc.controlSock, net.message.data, net.message.maxsize, &readaddr)) > 0)
    {
        if (ret < sizeof(int))
        {
            continue;
        }
        net.message.cursize = ret;

        // don't answer our own query
        if (dfunc.AddrCompare(&readaddr, &myaddr) >= 0)
        {
            continue;
        }

        // is the cache full?
        if (hostCacheCount == HOSTCACHESIZE)
        {
            continue;
        }

        MSG_BeginReading();
        control = BigLong(*(reinterpret_cast<int *>(net.message.data)));
        MSG_ReadLong();
        if (control == -1)
        {
            continue;
        }
        if ((control & (~NETFLAG_LENGTH_MASK)) != NETFLAG_CTL)
        {
            continue;
        }
        if ((control & NETFLAG_LENGTH_MASK) != ret)
        {
            continue;
        }

        if (MSG_ReadByte() != CCREP_SERVER_INFO)
        {
            continue;
        }

        dfunc.GetAddrFromName(MSG_ReadString().c_str(), &readaddr);
        // search the cache for this server
        for (n = 0; n < hostCacheCount; n++)
        {
            if (dfunc.AddrCompare(&readaddr, &hostcache[n].addr) == 0)
            {
                break;
            }
        }

        // is it already there?
        if (n < hostCacheCount)
        {
            continue;
        }

        // add it
        hostCacheCount++;
        Q_strlcpy(hostcache[n].name, MSG_ReadString().c_str(), sizeof(hostcache[n].name));
        Q_strlcpy(hostcache[n].map, MSG_ReadString().c_str(), sizeof(hostcache[n].map));
        hostcache[n].users = MSG_ReadByte();
        hostcache[n].maxusers = MSG_ReadByte();
        if (MSG_ReadByte() != NET_PROTOCOL_VERSION)
        {
            Q_strlcpy(hostcache[n].cname, hostcache[n].name, sizeof(hostcache[n].cname));
            hostcache[n].cname[14] = 0;
            Q_strlcpy(hostcache[n].name, "*", sizeof(hostcache[n].name));
            Q_strlcat(hostcache[n].name, hostcache[n].cname, sizeof(hostcache[n].name));
        }
        Q_memcpy(&hostcache[n].addr, &readaddr, sizeof(struct qsockaddr));
        hostcache[n].driver = net_driverlevel;
        hostcache[n].ldriver = net_landriverlevel;
        Q_strlcpy(hostcache[n].cname, dfunc.AddrToString(&readaddr).c_str(), sizeof(hostcache[n].cname));

        // check for a name conflict
        for (i = 0; i < hostCacheCount; i++)
        {
            if (i == n)
            {
                continue;
            }
            if (Q_strcasecmp(hostcache[n].name, hostcache[i].name) == 0)
            {
                i = Q_strlen(hostcache[n].name);
                if (i < 15 && hostcache[n].name[i - 1] > '8')
                {
                    hostcache[n].name[i] = '0';
                    hostcache[n].name[i + 1] = 0;
                }
                else
                {
                    hostcache[n].name[i - 1]++;
                }
                i = -1;
            }
        }
    }
}

void Datagram_SearchForHosts(qboolean xmit)
{
    for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
    {
        if (hostCacheCount == HOSTCACHESIZE)
        {
            break;
        }
        if (net_landrivers[net_landriverlevel].initialized)
        {
            _Datagram_SearchForHosts(xmit);
        }
    }
}

static qsocket_t *_Datagram_Connect(const char *host)
{
    struct qsockaddr sendaddr;
    struct qsockaddr readaddr;
    qsocket_t *sock;
    int newsock;
    int ret;
    int reps;
    double start_time;
    int control;
    const char *reason;
    std::string rejectReason;

    // see if we can resolve the host name
    if (dfunc.GetAddrFromName(host, &sendaddr) == -1)
    {
        return nullptr;
    }

    newsock = dfunc.OpenSocket(0);
    if (newsock == -1)
    {
        return nullptr;
    }

    auto errorReturn2 = [&]() -> qsocket_t * {
        dfunc.CloseSocket(newsock);
        if (m_return_onerror)
        {
            key_dest = keydest_t::key_menu;
            m_state = static_cast<m_state_t>(m_return_state);
            m_return_onerror = false;
        }
        return nullptr;
    };

    sock = NET_NewQSocket();
    if (sock == nullptr)
    {
        return errorReturn2();
    }
    sock->socket = newsock;
    sock->landriver = net_landriverlevel;

    auto errorReturn = [&]() -> qsocket_t * {
        NET_FreeQSocket(sock);
        return errorReturn2();
    };

    // connect to the host
    if (dfunc.Connect(newsock, &sendaddr) == -1)
    {
        return errorReturn();
    }

    // send the connection request
    Con_Printf("trying...\n");
    SCR_UpdateScreen();
    start_time = net.time;

    for (reps = 0; reps < 3; reps++)
    {
        SZ_Clear(&net.message);
        // save space for the header, filled in later
        MSG_WriteLong(&net.message, 0);
        MSG_WriteByte(&net.message, CCREQ_CONNECT);
        MSG_WriteString(&net.message, "QUAKE");
        MSG_WriteByte(&net.message, NET_PROTOCOL_VERSION);
        *(reinterpret_cast<int *>(net.message.data)) =
            BigLong(NETFLAG_CTL | (net.message.cursize & NETFLAG_LENGTH_MASK));
        dfunc.Write(newsock, net.message.data, net.message.cursize, &sendaddr);
        SZ_Clear(&net.message);
        do
        {
            ret = dfunc.Read(newsock, net.message.data, net.message.maxsize, &readaddr);
            // if we got something, validate it
            if (ret > 0)
            {
                // is it from the right place?
                if (sfunc.AddrCompare(&readaddr, &sendaddr) != 0)
                {
#ifdef DEBUG
                    Con_Printf("wrong reply address\n");
                    Con_Printf("Expected: %s\n", StrAddr(&sendaddr).c_str());
                    Con_Printf("Received: %s\n", StrAddr(&readaddr).c_str());
                    SCR_UpdateScreen();
#endif
                    ret = 0;
                    continue;
                }

                if (ret < sizeof(int))
                {
                    ret = 0;
                    continue;
                }

                net.message.cursize = ret;
                MSG_BeginReading();

                control = BigLong(*(reinterpret_cast<int *>(net.message.data)));
                MSG_ReadLong();
                if (control == -1)
                {
                    ret = 0;
                    continue;
                }
                if ((control & (~NETFLAG_LENGTH_MASK)) != NETFLAG_CTL)
                {
                    ret = 0;
                    continue;
                }
                if ((control & NETFLAG_LENGTH_MASK) != ret)
                {
                    ret = 0;
                    continue;
                }
            }
        } while (ret == 0 && (SetNetTime() - start_time) < 2.5);
        if (ret)
        {
            break;
        }
        Con_Printf("still trying...\n");
        SCR_UpdateScreen();
        start_time = SetNetTime();
    }

    if (ret == 0)
    {
        reason = "No Response";
        Con_Printf("%s\n", reason);
        Q_strlcpy(m_return_reason, reason, sizeof(m_return_reason));
        return errorReturn();
    }

    if (ret == -1)
    {
        reason = "Network Error";
        Con_Printf("%s\n", reason);
        Q_strlcpy(m_return_reason, reason, sizeof(m_return_reason));
        return errorReturn();
    }

    ret = MSG_ReadByte();
    if (ret == CCREP_REJECT)
    {
        rejectReason = MSG_ReadString();
        Con_Printf("%s", rejectReason.c_str());
        Q_strlcpy(m_return_reason, rejectReason.c_str(), sizeof(m_return_reason));
        return errorReturn();
    }

    if (ret == CCREP_ACCEPT)
    {
        Q_memcpy(&sock->addr, &sendaddr, sizeof(struct qsockaddr));
        dfunc.SetSocketPort(&sock->addr, MSG_ReadLong());
    }
    else
    {
        reason = "Bad Response";
        Con_Printf("%s\n", reason);
        Q_strlcpy(m_return_reason, reason, sizeof(m_return_reason));
        return errorReturn();
    }

    dfunc.GetNameFromAddr(&sendaddr, sock->address);

    Con_Printf("Connection accepted\n");
    sock->lastMessageTime = SetNetTime();

    // switch the connection to the specified address
    if (dfunc.Connect(newsock, &sock->addr) == -1)
    {
        reason = "Connect to Game failed";
        Con_Printf("%s\n", reason);
        Q_strlcpy(m_return_reason, reason, sizeof(m_return_reason));
        return errorReturn();
    }

    m_return_onerror = false;
    return sock;
}

qsocket_t *Datagram_Connect(const char *host)
{
    qsocket_t *ret = nullptr;

    for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
    {
        if (net_landrivers[net_landriverlevel].initialized)
        {
            if ((ret = _Datagram_Connect(host)) != nullptr)
            {
                break;
            }
        }
    }
    return ret;
}
