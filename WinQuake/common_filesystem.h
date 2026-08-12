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
// common_filesystem.h -- the pak-aware game filesystem: search path list,
// .pak archive reading, and the byte-buffer loaders built on top of them.
#pragma once

#include <fstream>
#include <optional>
#include <string>

struct cache_user_t;

// A file opened through the search path -- may be a loose file on disk or a
// byte range inside a .pak archive; either way the stream is already
// positioned at the start of the data and length is the entry's exact size.
struct OpenedFile
{
    std::ifstream stream;
    int length;
};

// Searches the path (loose directories and .pak archives, highest-priority
// first) and opens filename if found. filename never has a leading slash,
// but may contain directory walks.
std::optional<OpenedFile> COM_FOpenFile(const std::string &filename);

// The filename will be prefixed by the current game directory.
void COM_WriteFile(const char *filename, const void *data, int len);

// Filenames are relative to the quake directory. Always appends a 0 byte.
byte *COM_LoadStackFile(const char *path, void *buffer, int bufsize); // uses temp hunk if larger than bufsize
byte *COM_LoadTempFile(const char *path);
byte *COM_LoadHunkFile(const char *path);
void COM_LoadCacheFile(const char *path, cache_user_t *cu);

void COM_InitFilesystem(void);
