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
// common_filesystem.cpp -- the pak-aware game filesystem

#include "quakedef.h"

#include <filesystem>
#include <memory>
#include <vector>

// if a packfile directory differs from this, it is assumed to be hacked
#define PAK0_COUNT 339
#define PAK0_CRC 32981

#define MAX_FILES_IN_PACK 2048

qboolean com_modified; // set true if using non-id files
qboolean proghack;
int static_registered = 1; // only for startup check, then set
char com_cachedir[MAX_OSPATH];
char com_gamedir[MAX_OSPATH];
int com_filesize;

//
// on disk .pak format
//
struct dpackfile_t
{
    char name[56];
    int filepos, filelen;
};

struct dpackheader_t
{
    char id[4];
    int dirofs;
    int dirlen;
};

//
// one search path entry -- either a loose directory or an open .pak archive
//
class SearchPathEntry
{
public:
    virtual ~SearchPathEntry() = default;
    virtual std::optional<OpenedFile> Open(const std::string &filename) const = 0;
    virtual std::string Describe() const = 0;
};

class DirectorySearchPath : public SearchPathEntry
{
    std::string dir;

public:
    explicit DirectorySearchPath(std::string dir) : dir(std::move(dir)) {}

    std::optional<OpenedFile> Open(const std::string &filename) const override
    {
        // if not a registered version, don't ever go beyond base
        if (!static_registered)
        {
            if (filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos)
            {
                return std::nullopt;
            }
        }

        std::string netpath = std::format("{}/{}", dir, filename);

        if (Sys_FileTime(netpath.c_str()) == -1)
        {
            return std::nullopt;
        }

        // see if the file needs to be updated in the cache
        std::string openpath = netpath;
        if (com_cachedir[0])
        {
            std::string cachepath;
#if defined(_WIN32)
            if ((netpath.size() < 2) || (netpath[1] != ':'))
            {
                cachepath = std::format("{}{}", com_cachedir, netpath);
            }
            else
            {
                cachepath = std::format("{}{}", com_cachedir, netpath.substr(2));
            }
#else
            cachepath = std::format("{}{}", com_cachedir, netpath);
#endif

            if (Sys_FileTime(cachepath.c_str()) < Sys_FileTime(netpath.c_str()))
            {
                COM_CopyFile(netpath, cachepath);
            }
            openpath = cachepath;
        }

        Sys_Printf("FindFile: {}\n", openpath);

        std::ifstream stream(openpath, std::ios::binary);
        if (!stream)
        {
            return std::nullopt;
        }
        int length = static_cast<int>(std::filesystem::file_size(openpath));
        return OpenedFile{std::move(stream), length};
    }

    std::string Describe() const override { return dir; }

private:
    static void COM_CopyFile(const std::string &netpath, const std::string &cachepath);
};

struct PackEntry
{
    std::string name;
    int filepos;
    int filelen;
};

class PackArchive : public SearchPathEntry
{
    std::string filename;
    std::vector<PackEntry> entries;

public:
    static std::unique_ptr<PackArchive> Load(const std::string &packfile)
    {
        std::ifstream f(packfile, std::ios::binary);
        if (!f)
        {
            return nullptr;
        }

        dpackheader_t header;
        f.read(reinterpret_cast<char *>(&header), sizeof(header));
        if (header.id[0] != 'P' || header.id[1] != 'A' || header.id[2] != 'C' || header.id[3] != 'K')
        {
            Sys_Error("%s is not a packfile", packfile.c_str());
        }
        header.dirofs = LittleLong(header.dirofs);
        header.dirlen = LittleLong(header.dirlen);

        int numpackfiles = header.dirlen / sizeof(dpackfile_t);

        if (numpackfiles > MAX_FILES_IN_PACK)
        {
            Sys_Error("%s has %i files", packfile.c_str(), numpackfiles);
        }

        if (numpackfiles != PAK0_COUNT)
        {
            com_modified = true; // not the original file
        }

        // Sized to the raw byte count actually read, not numpackfiles *
        // sizeof(dpackfile_t) -- those only match for a well-formed pak; a
        // dirlen that isn't an exact multiple of the struct size (a
        // malformed pak) must not read past the buffer while CRC-checking.
        f.seekg(header.dirofs);
        std::vector<char> rawDir(header.dirlen);
        f.read(rawDir.data(), header.dirlen);

        // crc the directory to check for modifications
        unsigned short crc;
        CRC_Init(&crc);
        for (int i = 0; i < header.dirlen; i++)
        {
            CRC_ProcessByte(&crc, static_cast<byte>(rawDir[i]));
        }
        if (crc != PAK0_CRC)
        {
            com_modified = true;
        }

        const dpackfile_t *info = reinterpret_cast<const dpackfile_t *>(rawDir.data());
        auto pack = std::make_unique<PackArchive>();
        pack->filename = packfile;
        pack->entries.reserve(numpackfiles);
        for (int i = 0; i < numpackfiles; i++)
        {
            PackEntry entry;
            entry.name.assign(info[i].name, strnlen(info[i].name, sizeof(info[i].name)));
            entry.filepos = LittleLong(info[i].filepos);
            entry.filelen = LittleLong(info[i].filelen);
            pack->entries.push_back(std::move(entry));
        }

        Con_Printf("Added packfile %s (%i files)\n", packfile.c_str(), numpackfiles);
        return pack;
    }

    std::optional<OpenedFile> Open(const std::string &filename) const override
    {
        for (const auto &entry : entries)
        {
            if (entry.name == filename)
            {
                std::ifstream stream(this->filename, std::ios::binary);
                if (!stream)
                {
                    return std::nullopt;
                }
                stream.seekg(entry.filepos);
                Sys_Printf("PackFile: {} : {}\n", this->filename, filename);
                return OpenedFile{std::move(stream), entry.filelen};
            }
        }
        return std::nullopt;
    }

    std::string Describe() const override { return std::format("{} ({} files)", filename, entries.size()); }
};

// Copies a file over from the net to the local cache, creating any
// directories needed. This is for the convenience of developers using ISDN
// from home.
void DirectorySearchPath::COM_CopyFile(const std::string &netpath, const std::string &cachepath)
{
    std::filesystem::create_directories(std::filesystem::path(cachepath).parent_path());
    std::error_code ec;
    std::filesystem::copy_file(netpath, cachepath, std::filesystem::copy_options::overwrite_existing, ec);
}

std::vector<std::unique_ptr<SearchPathEntry>> com_searchpaths;

/*
============
COM_Path_f
============
*/
static void COM_Path_f(void)
{
    Con_Printf("Current search path:\n");
    for (const auto &entry : com_searchpaths)
    {
        Con_Printf("%s\n", entry->Describe().c_str());
    }
}

/*
============
COM_WriteFile

The filename will be prefixed by the current game directory
============
*/
void COM_WriteFile(const char *filename, const void *data, int len)
{
    std::string name = std::format("{}/{}", com_gamedir, filename);

    std::ofstream f(name, std::ios::binary);
    if (!f)
    {
        Sys_Printf("COM_WriteFile: failed on {}\n", name);
        return;
    }

    Sys_Printf("COM_WriteFile: {}\n", name);
    f.write(static_cast<const char *>(data), len);
}

/*
===========
COM_FOpenFile

Finds filename in the search path and opens it, honoring -proghack's
skip-the-first-search-path special case.
===========
*/
std::optional<OpenedFile> COM_FOpenFile(const std::string &filename)
{
    size_t start = 0;
    if (proghack)
    { // gross hack to use quake 1 progs with quake 2 maps
        if (filename == "progs.dat")
        {
            start = 1;
        }
    }

    for (size_t i = start; i < com_searchpaths.size(); i++)
    {
        if (auto opened = com_searchpaths[i]->Open(filename))
        {
            com_filesize = opened->length;
            return opened;
        }
    }

    Sys_Printf("FindFile: can't find {}\n", filename);
    com_filesize = -1;
    return std::nullopt;
}

/*
============
COM_LoadFile

Filename are reletive to the quake directory.
Allways appends a 0 byte.
============
*/
cache_user_t *loadcache;
byte *loadbuf;
int loadsize;
static byte *COM_LoadFile(const char *path, int usehunk)
{
    byte *buf;
    char base[32];
    int len;

    buf = nullptr; // quiet compiler warning

    auto opened = COM_FOpenFile(path);
    if (!opened)
    {
        return nullptr;
    }
    len = opened->length;

    // extract the filename base name for hunk tag
    COM_FileBase(path, base, sizeof(base));

    if (usehunk == 1)
    {
        buf = static_cast<byte *>(Hunk_AllocName(len + 1, base));
    }
    else if (usehunk == 2)
    {
        buf = static_cast<byte *>(Hunk_TempAlloc(len + 1));
    }
    else if (usehunk == 0)
    {
        buf = static_cast<byte *>(Z_Malloc(len + 1));
    }
    else if (usehunk == 3)
    {
        buf = static_cast<byte *>(Cache_Alloc(loadcache, len + 1, base));
    }
    else if (usehunk == 4)
    {
        if (len + 1 > loadsize)
        {
            buf = static_cast<byte *>(Hunk_TempAlloc(len + 1));
        }
        else
        {
            buf = loadbuf;
        }
    }
    else
    {
        Sys_Error("COM_LoadFile: bad usehunk");
    }

    if (!buf)
    {
        Sys_Error("COM_LoadFile: not enough space for %s", path);
    }

    buf[len] = 0;

    Draw_BeginDisc();
    opened->stream.read(reinterpret_cast<char *>(buf), len);
    Draw_EndDisc();

    return buf;
}

byte *COM_LoadHunkFile(const char *path)
{
    return COM_LoadFile(path, 1);
}

byte *COM_LoadTempFile(const char *path)
{
    return COM_LoadFile(path, 2);
}

void COM_LoadCacheFile(const char *path, cache_user_t *cu)
{
    loadcache = cu;
    COM_LoadFile(path, 3);
}

// uses temp hunk if larger than bufsize
byte *COM_LoadStackFile(const char *path, void *buffer, int bufsize)
{
    byte *buf;

    loadbuf = static_cast<byte *>(buffer);
    loadsize = bufsize;
    buf = COM_LoadFile(path, 4);

    return buf;
}

/*
================
COM_AddGameDirectory

Sets com_gamedir, adds the directory to the head of the path,
then loads and adds pak1.pak pak2.pak ...
================
*/
static void COM_AddGameDirectory(const char *dir)
{
    Q_strlcpy(com_gamedir, dir, sizeof(com_gamedir));

    //
    // add the directory to the search path
    //
    com_searchpaths.insert(com_searchpaths.begin(), std::make_unique<DirectorySearchPath>(dir));

    //
    // add any pak files in the format pak0.pak pak1.pak, ...
    //
    for (int i = 0;; i++)
    {
        std::string pakfile = std::format("{}/pak{}.pak", dir, i);
        auto pack = PackArchive::Load(pakfile);
        if (!pack)
        {
            break;
        }
        com_searchpaths.insert(com_searchpaths.begin(), std::move(pack));
    }
}

/*
================
COM_InitFilesystem
================
*/
void COM_InitFilesystem(void)
{
    int i, j;
    char basedir[MAX_OSPATH];

    //
    // -basedir <path>
    // Overrides the system supplied base directory (under GAMENAME)
    //
    i = COM_CheckParm("-basedir");
    if (i && i < com_argc - 1)
    {
        Q_strlcpy(basedir, com_argv[i + 1], sizeof(basedir));
    }
    else
    {
        Q_strlcpy(basedir, host_parms.basedir, sizeof(basedir));
    }

    j = (int)strlen(basedir);

    if (j > 0)
    {
        if ((basedir[j - 1] == '\\') || (basedir[j - 1] == '/'))
        {
            basedir[j - 1] = 0;
        }
    }

    //
    // -cachedir <path>
    // Overrides the system supplied cache directory (nullptr or /qcache)
    // -cachedir - will disable caching.
    //
    i = COM_CheckParm("-cachedir");
    if (i && i < com_argc - 1)
    {
        if (com_argv[i + 1][0] == '-')
        {
            com_cachedir[0] = 0;
        }
        else
        {
            Q_strlcpy(com_cachedir, com_argv[i + 1], sizeof(com_cachedir));
        }
    }
    else if (host_parms.cachedir)
    {
        Q_strlcpy(com_cachedir, host_parms.cachedir, sizeof(com_cachedir));
    }
    else
    {
        com_cachedir[0] = 0;
    }

    //
    // start up with GAMENAME by default (id1)
    //
    COM_AddGameDirectory(va("{}/" GAMENAME, basedir));

    if (COM_CheckParm("-rogue"))
    {
        COM_AddGameDirectory(va("{}/rogue", basedir));
    }
    if (COM_CheckParm("-hipnotic"))
    {
        COM_AddGameDirectory(va("{}/hipnotic", basedir));
    }

    //
    // -game <gamedir>
    // Adds basedir/gamedir as an override game
    //
    i = COM_CheckParm("-game");
    if (i && i < com_argc - 1)
    {
        com_modified = true;
        COM_AddGameDirectory(va("{}/{}", basedir, com_argv[i + 1]));
    }

    //
    // -path <dir or packfile> [<dir or packfile>] ...
    // Fully specifies the exact serach path, overriding the generated one
    //
    i = COM_CheckParm("-path");
    if (i)
    {
        com_modified = true;
        com_searchpaths.clear();
        while (++i < com_argc)
        {
            if (!com_argv[i] || com_argv[i][0] == '+' || com_argv[i][0] == '-')
            {
                break;
            }

            if (COM_FileExtension(com_argv[i]) == "pak")
            {
                auto pack = PackArchive::Load(com_argv[i]);
                if (!pack)
                {
                    Sys_Error("Couldn't load packfile: %s", com_argv[i]);
                }
                com_searchpaths.insert(com_searchpaths.begin(), std::move(pack));
            }
            else
            {
                com_searchpaths.insert(com_searchpaths.begin(), std::make_unique<DirectorySearchPath>(com_argv[i]));
            }
        }
    }

    if (COM_CheckParm("-proghack"))
    {
        proghack = true;
    }

    Cmd_AddCommand("path", COM_Path_f);
}
