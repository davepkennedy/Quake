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
// qlimits.h -- shared engine limits and protocol constants.
//
// Pulled out of quakedef.h so headers that need these (common.h, net.h,
// sound.h, client.h, server.h, gl_model.h, ...) can include this directly
// instead of silently relying on quakedef.h having defined them first --
// quakedef.h itself includes this before chaining those headers, so the
// 57 .cpp files that only ever include quakedef.h see no change at all.
#pragma once

constexpr int MAX_QPATH = 64;   // max length of a quake game pathname
constexpr int MAX_OSPATH = 128; // max length of a filesystem pathname

constexpr double ON_EPSILON = 0.1; // point on plane side epsilon

constexpr int MAX_MSGLEN = 8000;   // max length of a reliable message
constexpr int MAX_DATAGRAM = 1024; // max length of unreliable message

//
// per-level limits
//
constexpr int MAX_EDICTS = 600; // FIXME: ouch! ouch! ouch!
constexpr int MAX_LIGHTSTYLES = 64;
constexpr int MAX_MODELS = 256; // these are sent over the net as bytes
constexpr int MAX_SOUNDS = 256; // so they cannot be blindly increased

constexpr int SAVEGAME_COMMENT_LENGTH = 39;

constexpr int MAX_STYLESTRING = 64;

//
// stats are integers communicated to the client by the server
//
constexpr int MAX_CL_STATS = 32;
constexpr int STAT_HEALTH = 0;
constexpr int STAT_FRAGS = 1;
constexpr int STAT_WEAPON = 2;
constexpr int STAT_AMMO = 3;
constexpr int STAT_ARMOR = 4;
constexpr int STAT_WEAPONFRAME = 5;
constexpr int STAT_SHELLS = 6;
constexpr int STAT_NAILS = 7;
constexpr int STAT_ROCKETS = 8;
constexpr int STAT_CELLS = 9;
constexpr int STAT_ACTIVEWEAPON = 10;
constexpr int STAT_TOTALSECRETS = 11;
constexpr int STAT_TOTALMONSTERS = 12;
constexpr int STAT_SECRETS = 13;  // bumped on client side by svc_foundsecret
constexpr int STAT_MONSTERS = 14; // bumped by svc_killedmonster

// stock defines

constexpr int IT_SHOTGUN = 1;
constexpr int IT_SUPER_SHOTGUN = 2;
constexpr int IT_NAILGUN = 4;
constexpr int IT_SUPER_NAILGUN = 8;
constexpr int IT_GRENADE_LAUNCHER = 16;
constexpr int IT_ROCKET_LAUNCHER = 32;
constexpr int IT_LIGHTNING = 64;
constexpr int IT_SUPER_LIGHTNING = 128;
constexpr int IT_SHELLS = 256;
constexpr int IT_NAILS = 512;
constexpr int IT_ROCKETS = 1024;
constexpr int IT_CELLS = 2048;
constexpr int IT_AXE = 4096;
constexpr int IT_ARMOR1 = 8192;
constexpr int IT_ARMOR2 = 16384;
constexpr int IT_ARMOR3 = 32768;
constexpr int IT_SUPERHEALTH = 65536;
constexpr int IT_KEY1 = 131072;
constexpr int IT_KEY2 = 262144;
constexpr int IT_INVISIBILITY = 524288;
constexpr int IT_INVULNERABILITY = 1048576;
constexpr int IT_SUIT = 2097152;
constexpr int IT_QUAD = 4194304;
constexpr int IT_SIGIL1 = (1 << 28);
constexpr int IT_SIGIL2 = (1 << 29);
constexpr int IT_SIGIL3 = (1 << 30);
constexpr int IT_SIGIL4 = (1 << 31);

//===========================================
// rogue changed and added defines

constexpr int RIT_SHELLS = 128;
constexpr int RIT_NAILS = 256;
constexpr int RIT_ROCKETS = 512;
constexpr int RIT_CELLS = 1024;
constexpr int RIT_AXE = 2048;
constexpr int RIT_LAVA_NAILGUN = 4096;
constexpr int RIT_LAVA_SUPER_NAILGUN = 8192;
constexpr int RIT_MULTI_GRENADE = 16384;
constexpr int RIT_MULTI_ROCKET = 32768;
constexpr int RIT_PLASMA_GUN = 65536;
constexpr int RIT_ARMOR1 = 8388608;
constexpr int RIT_ARMOR2 = 16777216;
constexpr int RIT_ARMOR3 = 33554432;
constexpr int RIT_LAVA_NAILS = 67108864;
constexpr int RIT_PLASMA_AMMO = 134217728;
constexpr int RIT_MULTI_ROCKETS = 268435456;
constexpr int RIT_SHIELD = 536870912;
constexpr int RIT_ANTIGRAV = 1073741824;
constexpr unsigned int RIT_SUPERHEALTH = 2147483648u;

// MED 01/04/97 added hipnotic defines
//===========================================
// hipnotic added defines
constexpr int HIT_PROXIMITY_GUN_BIT = 16;
constexpr int HIT_MJOLNIR_BIT = 7;
constexpr int HIT_LASER_CANNON_BIT = 23;
constexpr int HIT_PROXIMITY_GUN = (1 << HIT_PROXIMITY_GUN_BIT);
constexpr int HIT_MJOLNIR = (1 << HIT_MJOLNIR_BIT);
constexpr int HIT_LASER_CANNON = (1 << HIT_LASER_CANNON_BIT);
constexpr int HIT_WETSUIT = (1 << (23 + 2));
constexpr int HIT_EMPATHY_SHIELDS = (1 << (23 + 3));

//===========================================

constexpr int MAX_SCOREBOARD = 16;
constexpr int MAX_SCOREBOARDNAME = 32;

constexpr int SOUND_CHANNELS = 8;
