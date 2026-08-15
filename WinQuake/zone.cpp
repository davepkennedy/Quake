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
// Z_zone.c

#include "quakedef.h"
#include "hunk_resource.h"
#include <list>

#define DYNAMIC_SIZE 0xc000

#define ZONEID 0x1d4a11
#define MINFRAGMENT 64

struct memblock_t
{
    int size; // including the header and possibly tiny fragments
    int tag;  // a tag of 0 is a free block
    int id;   // should be ZONEID
    struct memblock_t *next, *prev;
    int pad; // pad to 64 bit boundary
};

struct memzone_t
{
    int size;             // total bytes malloced, including header
    memblock_t blocklist; // start / end cap for linked list
    memblock_t *rover;
};

// moved up from the CACHE MEMORY section below so zone_state_t can hold
// cache_head by value.
struct cache_system_t
{
    int size; // including this header
    cache_user_t *user;
    char name[16];
    struct cache_system_t *prev, *next;
    bool inLRU; // tracked in zone_state_t::cacheLRU instead of an
                // intrusive list -- see Cache_MakeLRU/Cache_UnlinkLRU
};

// State for all three of this file's allocators (zone/hunk/cache) --
// entirely private to zone.cpp, nothing outside this file references any
// of it (confirmed via a whole-codebase grep; the one exception, a stray
// `extern byte *hunk_base;` in gl_rmisc.cpp's R_Init, was dead -- declared
// but never actually used there -- and has been removed).
struct zone_state_t
{
    memzone_t *mainzone = nullptr;

    byte *hunk_base = nullptr;
    size_t hunk_size = 0;

    size_t hunk_low_used = 0;
    size_t hunk_high_used = 0;

    qboolean hunk_tempactive = false;
    size_t hunk_tempmark = 0;

    cache_system_t cache_head;
    std::list<cache_system_t *> cacheLRU; // recency order, most-recent at front
};

static zone_state_t mem;

void Cache_FreeLow(int new_low_hunk);
void Cache_FreeHigh(int new_high_hunk);

/*
==============================================================================

                        ZONE MEMORY ALLOCATION

There is never any space between memblocks, and there will never be two
contiguous free memblocks.

The rover can be left pointing at a non-empty block

The zone calls are pretty much only used for small strings and structures,
all big things are allocated on the hunk.
==============================================================================
*/

void Z_ClearZone(memzone_t *zone, int size);

/*
========================
Z_ClearZone
========================
*/
void Z_ClearZone(memzone_t *zone, int size)
{
    memblock_t *block;

    // set the entire zone to one free block

    zone->blocklist.next = zone->blocklist.prev = block =
        reinterpret_cast<memblock_t *>(reinterpret_cast<byte *>(zone) + sizeof(memzone_t));
    zone->blocklist.tag = 1; // in use block
    zone->blocklist.id = 0;
    zone->blocklist.size = 0;
    zone->rover = block;

    block->prev = block->next = &zone->blocklist;
    block->tag = 0; // free block
    block->id = ZONEID;
    block->size = size - sizeof(memzone_t);
}

/*
========================
Z_Free
========================
*/
void Z_Free(void *ptr)
{
    memblock_t *block, *other;

    if (!ptr)
    {
        Sys_Error("Z_Free: nullptr pointer");
    }

    block = reinterpret_cast<memblock_t *>((static_cast<byte *>(ptr) - sizeof(memblock_t)));
    if (block->id != ZONEID)
    {
        Sys_Error("Z_Free: freed a pointer without ZONEID");
    }
    if (block->tag == 0)
    {
        Sys_Error("Z_Free: freed a freed pointer");
    }

    block->tag = 0; // mark as free

    other = block->prev;
    if (!other->tag)
    { // merge with previous free block
        other->size += block->size;
        other->next = block->next;
        other->next->prev = other;
        if (block == mem.mainzone->rover)
        {
            mem.mainzone->rover = other;
        }
        block = other;
    }

    other = block->next;
    if (!other->tag)
    { // merge the next free block onto the end
        block->size += other->size;
        block->next = other->next;
        block->next->prev = block;
        if (other == mem.mainzone->rover)
        {
            mem.mainzone->rover = block;
        }
    }
}

/*
========================
Z_Malloc
========================
*/
void *Z_Malloc(int size)
{
    void *buf;

    Z_CheckHeap(); // DEBUG
    buf = Z_TagMalloc(size, 1);
    if (!buf)
    {
        Sys_Error("Z_Malloc: failed on allocation of {} bytes", size);
    }
    Q_memset(buf, 0, size);

    return buf;
}

void *Z_TagMalloc(int size, int tag)
{
    int extra;
    memblock_t *start, *rover, *newblock, *base;

    if (!tag)
    {
        Sys_Error("Z_TagMalloc: tried to use a 0 tag");
    }

    //
    // scan through the block list looking for the first free block
    // of sufficient size
    //
    size += sizeof(memblock_t); // account for size of block header
    size += 4;                  // space for memory trash tester
    size = (size + 7) & ~7;     // align to 8-byte boundary

    base = rover = mem.mainzone->rover;
    start = base->prev;

    do
    {
        if (rover == start) // scaned all the way around the list
        {
            return nullptr;
        }
        if (rover->tag)
        {
            base = rover = rover->next;
        }
        else
        {
            rover = rover->next;
        }
    } while (base->tag || base->size < size);

    //
    // found a block big enough
    //
    extra = base->size - size;
    if (extra > MINFRAGMENT)
    { // there will be a free fragment after the allocated block
        newblock = reinterpret_cast<memblock_t *>((reinterpret_cast<byte *>(base) + size));
        newblock->size = extra;
        newblock->tag = 0; // free block
        newblock->prev = base;
        newblock->id = ZONEID;
        newblock->next = base->next;
        newblock->next->prev = newblock;
        base->next = newblock;
        base->size = size;
    }

    base->tag = tag; // no longer a free block

    mem.mainzone->rover = base->next; // next allocation will start looking here

    base->id = ZONEID;

    // marker for memory trash testing
    *reinterpret_cast<int *>(reinterpret_cast<byte *>(base) + base->size - 4) = ZONEID;

    return static_cast<void *>(reinterpret_cast<byte *>(base) + sizeof(memblock_t));
}

/*
========================
Z_Print
========================
*/
void Z_Print(memzone_t *zone)
{
    memblock_t *block;

    Con_Printf("zone size: {}  location: {}\n", mem.mainzone->size, static_cast<void *>(mem.mainzone));

    for (block = zone->blocklist.next;; block = block->next)
    {
        Con_Printf("block:{}    size:{:7}    tag:{:3}\n", static_cast<void *>(block), block->size, block->tag);

        if (block->next == &zone->blocklist)
        {
            break; // all blocks have been hit
        }
        if (reinterpret_cast<byte *>(block) + block->size != reinterpret_cast<byte *>(block->next))
        {
            Con_Printf("ERROR: block size does not touch the next block\n");
        }
        if (block->next->prev != block)
        {
            Con_Printf("ERROR: next block doesn't have proper back link\n");
        }
        if (!block->tag && !block->next->tag)
        {
            Con_Printf("ERROR: two consecutive free blocks\n");
        }
    }
}

/*
========================
Z_CheckHeap
========================
*/
void Z_CheckHeap(void)
{
    memblock_t *block;

    for (block = mem.mainzone->blocklist.next;; block = block->next)
    {
        if (block->next == &mem.mainzone->blocklist)
        {
            break; // all blocks have been hit
        }
        if (reinterpret_cast<byte *>(block) + block->size != reinterpret_cast<byte *>(block->next))
        {
            Sys_Error("Z_CheckHeap: block size does not touch the next block\n");
        }
        if (block->next->prev != block)
        {
            Sys_Error("Z_CheckHeap: next block doesn't have proper back link\n");
        }
        if (!block->tag && !block->next->tag)
        {
            Sys_Error("Z_CheckHeap: two consecutive free blocks\n");
        }
    }
}

//============================================================================

#define HUNK_SENTINAL 0x1df001ed

struct hunk_t
{
    int sentinal;
    int size; // including sizeof(hunk_t), -1 = not allocated
    char name[8];
};

void R_FreeTextures(void);

/*
==============
Hunk_Check

Run consistancy and sentinal trahing checks
==============
*/
void Hunk_Check(void)
{
    hunk_t *h;

    for (h = reinterpret_cast<hunk_t *>(mem.hunk_base);
         reinterpret_cast<byte *>(h) != mem.hunk_base + mem.hunk_low_used;)
    {
        if (h->sentinal != HUNK_SENTINAL)
        {
            Sys_Error("Hunk_Check: trahsed sentinal");
        }
        if (h->size < 16 || (size_t)(h->size + reinterpret_cast<byte *>(h) - mem.hunk_base) > mem.hunk_size)
        {
            Sys_Error("Hunk_Check: bad size");
        }
        h = reinterpret_cast<hunk_t *>(reinterpret_cast<byte *>(h) + h->size);
    }
}

/*
==============
Hunk_Print

If "all" is specified, every single allocation is printed.
Otherwise, allocations with the same name will be totaled up before printing.
==============
*/
void Hunk_Print(qboolean all)
{
    hunk_t *h, *next, *endlow, *starthigh, *endhigh;
    int count, sum;
    int totalblocks;
    char name[9];

    name[8] = 0;
    count = 0;
    sum = 0;
    totalblocks = 0;

    h = reinterpret_cast<hunk_t *>(mem.hunk_base);
    endlow = reinterpret_cast<hunk_t *>(mem.hunk_base + mem.hunk_low_used);
    starthigh = reinterpret_cast<hunk_t *>(mem.hunk_base + mem.hunk_size - mem.hunk_high_used);
    endhigh = reinterpret_cast<hunk_t *>(mem.hunk_base + mem.hunk_size);

    Con_Printf("          :{:8} total hunk size\n", mem.hunk_size);
    Con_Printf("-------------------------\n");

    while (1)
    {
        //
        // skip to the high hunk if done with low hunk
        //
        if (h == endlow)
        {
            Con_Printf("-------------------------\n");
            Con_Printf("          :{:8} REMAINING\n", mem.hunk_size - mem.hunk_low_used - mem.hunk_high_used);
            Con_Printf("-------------------------\n");
            h = starthigh;
        }

        //
        // if totally done, break
        //
        if (h == endhigh)
        {
            break;
        }

        //
        // run consistancy checks
        //
        if (h->sentinal != HUNK_SENTINAL)
        {
            Sys_Error("Hunk_Check: trahsed sentinal");
        }
        if (h->size < 16 || (size_t)(h->size + reinterpret_cast<byte *>(h) - mem.hunk_base) > mem.hunk_size)
        {
            Sys_Error("Hunk_Check: bad size");
        }

        next = reinterpret_cast<hunk_t *>(reinterpret_cast<byte *>(h) + h->size);
        count++;
        totalblocks++;
        sum += h->size;

        //
        // print the single block
        //
        memcpy(name, h->name, 8);
        if (all)
        {
            Con_Printf("{:8} :{:8} {:>8}\n", static_cast<void *>(h), h->size, name);
        }

        //
        // print the total
        //
        if (next == endlow || next == endhigh || strncmp(h->name, next->name, 8))
        {
            if (!all)
            {
                Con_Printf("          :{:8} {:>8} (TOTAL)\n", sum, name);
            }
            count = 0;
            sum = 0;
        }

        h = next;
    }

    Con_Printf("-------------------------\n");
    Con_Printf("{:8} total blocks\n", totalblocks);
}

/*
===================
Hunk_AllocName
===================
*/
void *Hunk_AllocName(int size, const char *name)
{
    hunk_t *h;

    if (size < 0)
    {
        Sys_Error("Hunk_Alloc: bad size: {}", size);
    }

    size = sizeof(hunk_t) + ((size + 15) & ~15);

    if (mem.hunk_size - mem.hunk_low_used - mem.hunk_high_used < size)
    {
        Sys_Error("Hunk_Alloc: failed on {} bytes", size);
    }

    h = reinterpret_cast<hunk_t *>(mem.hunk_base + mem.hunk_low_used);
    mem.hunk_low_used += size;

    Cache_FreeLow((int)mem.hunk_low_used);

    memset(h, 0, size);

    h->size = size;
    h->sentinal = HUNK_SENTINAL;
    Q_strncpy(h->name, name, 8);

    return static_cast<void *>(h + 1);
}

/*
===================
Hunk_Alloc
===================
*/
void *Hunk_Alloc(int size)
{
    return Hunk_AllocName(size, "unknown");
}

/*
===================
HunkMemoryResource

std::pmr facade over the low-hunk bump allocator -- see hunk_resource.h.
===================
*/
void *HunkMemoryResource::do_allocate(size_t bytes, size_t alignment)
{
    // Hunk_AllocName already rounds every allocation to a 16-byte boundary
    // and hands back memory right after its own 16-byte header, so every
    // allocation here is naturally 16-byte aligned already.
    if (alignment > 16)
    {
        Sys_Error("HunkMemoryResource::do_allocate: alignment {} not supported", alignment);
    }
    return Hunk_AllocName((int)bytes, "pmr");
}

void HunkMemoryResource::do_deallocate(void *, size_t, size_t)
{
    // Hunk memory is never freed individually, only bulk-reset via
    // Hunk_FreeToLowMark -- matches Hunk_Alloc's own contract exactly.
}

bool HunkMemoryResource::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    return this == &other;
}

static HunkMemoryResource hunkResourceInstance;

HunkMemoryResource *Hunk_GetResource(void)
{
    return &hunkResourceInstance;
}

size_t Hunk_LowMark(void)
{
    return mem.hunk_low_used;
}

void Hunk_FreeToLowMark(size_t mark)
{
    if (mark > mem.hunk_low_used)
    {
        Sys_Error("Hunk_FreeToLowMark: bad mark {}", mark);
    }
    memset(mem.hunk_base + mark, 0, mem.hunk_low_used - mark);
    mem.hunk_low_used = mark;
}

size_t Hunk_HighMark(void)
{
    if (mem.hunk_tempactive)
    {
        mem.hunk_tempactive = false;
        Hunk_FreeToHighMark(mem.hunk_tempmark);
    }

    return mem.hunk_high_used;
}

void Hunk_FreeToHighMark(size_t mark)
{
    if (mem.hunk_tempactive)
    {
        mem.hunk_tempactive = false;
        Hunk_FreeToHighMark(mem.hunk_tempmark);
    }
    if (mark > mem.hunk_high_used)
    {
        Sys_Error("Hunk_FreeToHighMark: bad mark {}", mark);
    }
    memset(mem.hunk_base + mem.hunk_size - mem.hunk_high_used, 0, mem.hunk_high_used - mark);
    mem.hunk_high_used = mark;
}

/*
===================
Hunk_HighAllocName
===================
*/
void *Hunk_HighAllocName(int size, const char *name)
{
    hunk_t *h;

    if (size < 0)
    {
        Sys_Error("Hunk_HighAllocName: bad size: {}", size);
    }

    if (mem.hunk_tempactive)
    {
        Hunk_FreeToHighMark(mem.hunk_tempmark);
        mem.hunk_tempactive = false;
    }

    size = sizeof(hunk_t) + ((size + 15) & ~15);

    if (mem.hunk_size - mem.hunk_low_used - mem.hunk_high_used < size)
    {
        Con_Printf("Hunk_HighAlloc: failed on {} bytes\n", size);
        return nullptr;
    }

    mem.hunk_high_used += size;
    Cache_FreeHigh((int)mem.hunk_high_used);

    h = reinterpret_cast<hunk_t *>(mem.hunk_base + mem.hunk_size - mem.hunk_high_used);

    memset(h, 0, size);
    h->size = size;
    h->sentinal = HUNK_SENTINAL;
    Q_strncpy(h->name, name, 8);

    return static_cast<void *>(h + 1);
}

/*
=================
Hunk_TempAlloc

Return space from the top of the hunk
=================
*/
void *Hunk_TempAlloc(int size)
{
    void *buf;

    size = (size + 15) & ~15;

    if (mem.hunk_tempactive)
    {
        Hunk_FreeToHighMark(mem.hunk_tempmark);
        mem.hunk_tempactive = false;
    }

    mem.hunk_tempmark = Hunk_HighMark();

    buf = Hunk_HighAllocName(size, "temp");

    mem.hunk_tempactive = true;

    return buf;
}

/*
===============================================================================

CACHE MEMORY

===============================================================================
*/

cache_system_t *Cache_TryAlloc(int size, qboolean nobottom);

/*
===========
Cache_Move
===========
*/
void Cache_Move(cache_system_t *c)
{
    cache_system_t *newcs;

    // we are clearing up space at the bottom, so only allocate it late
    newcs = Cache_TryAlloc(c->size, true);
    if (newcs)
    {
        //		Con_Printf ("cache_move ok\n");

        Q_memcpy(newcs + 1, c + 1, c->size - sizeof(cache_system_t));
        newcs->user = c->user;
        Q_memcpy(newcs->name, c->name, sizeof(newcs->name));
        Cache_Free(c->user);
        newcs->user->data = static_cast<void *>(newcs + 1);
    }
    else
    {
        //		Con_Printf ("cache_move failed\n");

        Cache_Free(c->user); // tough luck...
    }
}

/*
============
Cache_FreeLow

Throw things out until the hunk can be expanded to the given point
============
*/
void Cache_FreeLow(int new_low_hunk)
{
    cache_system_t *c;

    while (1)
    {
        c = mem.cache_head.next;
        if (c == &mem.cache_head)
        {
            return; // nothing in cache at all
        }
        if (reinterpret_cast<byte *>(c) >= mem.hunk_base + new_low_hunk)
        {
            return; // there is space to grow the hunk
        }
        Cache_Move(c); // reclaim the space
    }
}

/*
============
Cache_FreeHigh

Throw things out until the hunk can be expanded to the given point
============
*/
void Cache_FreeHigh(int new_high_hunk)
{
    cache_system_t *c, *prev;

    prev = nullptr;
    while (1)
    {
        c = mem.cache_head.prev;
        if (c == &mem.cache_head)
        {
            return; // nothing in cache at all
        }
        if (reinterpret_cast<byte *>(c) + c->size <= mem.hunk_base + mem.hunk_size - new_high_hunk)
        {
            return; // there is space to grow the hunk
        }
        if (c == prev)
        {
            Cache_Free(c->user); // didn't move out of the way
        }
        else
        {
            Cache_Move(c); // try to move it
            prev = c;
        }
    }
}

void Cache_UnlinkLRU(cache_system_t *cs)
{
    if (!cs->inLRU)
    {
        Sys_Error("Cache_UnlinkLRU: nullptr link");
    }

    mem.cacheLRU.remove(cs);
    cs->inLRU = false;
}

void Cache_MakeLRU(cache_system_t *cs)
{
    if (cs->inLRU)
    {
        Sys_Error("Cache_MakeLRU: active link");
    }

    mem.cacheLRU.push_front(cs);
    cs->inLRU = true;
}

/*
============
Cache_TryAlloc

Looks for a free block of memory between the high and low hunk marks
Size should already include the header and padding
============
*/
cache_system_t *Cache_TryAlloc(int size, qboolean nobottom)
{
    cache_system_t *cs, *newcs;

    // is the cache completely empty?

    if (!nobottom && mem.cache_head.prev == &mem.cache_head)
    {
        if (mem.hunk_size - mem.hunk_high_used - mem.hunk_low_used < size)
        {
            Sys_Error("Cache_TryAlloc: {} is greater then free hunk", size);
        }

        newcs = reinterpret_cast<cache_system_t *>((mem.hunk_base + mem.hunk_low_used));
        memset(newcs, 0, sizeof(*newcs));
        newcs->size = size;

        mem.cache_head.prev = mem.cache_head.next = newcs;
        newcs->prev = newcs->next = &mem.cache_head;

        Cache_MakeLRU(newcs);
        return newcs;
    }

    // search from the bottom up for space

    newcs = reinterpret_cast<cache_system_t *>((mem.hunk_base + mem.hunk_low_used));
    cs = mem.cache_head.next;

    do
    {
        if (!nobottom || cs != mem.cache_head.next)
        {
            if (reinterpret_cast<byte *>(cs) - reinterpret_cast<byte *>(newcs) >= size)
            { // found space
                memset(newcs, 0, sizeof(*newcs));
                newcs->size = size;

                newcs->next = cs;
                newcs->prev = cs->prev;
                cs->prev->next = newcs;
                cs->prev = newcs;

                Cache_MakeLRU(newcs);

                return newcs;
            }
        }

        // continue looking
        newcs = reinterpret_cast<cache_system_t *>(reinterpret_cast<byte *>(cs) + cs->size);
        cs = cs->next;

    } while (cs != &mem.cache_head);

    // try to allocate one at the very end
    if (mem.hunk_base + mem.hunk_size - mem.hunk_high_used - reinterpret_cast<byte *>(newcs) >= size)
    {
        memset(newcs, 0, sizeof(*newcs));
        newcs->size = size;

        newcs->next = &mem.cache_head;
        newcs->prev = mem.cache_head.prev;
        mem.cache_head.prev->next = newcs;
        mem.cache_head.prev = newcs;

        Cache_MakeLRU(newcs);

        return newcs;
    }

    return nullptr; // couldn't allocate
}

/*
============
Cache_Flush

Throw everything out, so new data will be demand cached
============
*/
void Cache_Flush(void)
{
    while (mem.cache_head.next != &mem.cache_head)
    {
        Cache_Free(mem.cache_head.next->user); // reclaim the space
    }
}

/*
============
Cache_Print

============
*/
void Cache_Print(void)
{
    cache_system_t *cd;

    for (cd = mem.cache_head.next; cd != &mem.cache_head; cd = cd->next)
    {
        Con_Printf("{:8} : {}\n", cd->size, cd->name);
    }
}

/*
============
Cache_Report

============
*/
void Cache_Report(void)
{
    Con_DPrintf("{:4.1f} megabyte data cache\n",
                (mem.hunk_size - mem.hunk_high_used - mem.hunk_low_used) / (float)(1024 * 1024));
}

/*
============
Cache_Compact

============
*/
void Cache_Compact(void)
{
}

/*
============
Cache_Init

============
*/
void Cache_Init(void)
{
    mem.cache_head.next = mem.cache_head.prev = &mem.cache_head;
    mem.cacheLRU.clear();

    Cmd_AddCommand("flush", Cache_Flush);
}

/*
==============
Cache_Free

Frees the memory and removes it from the LRU list
==============
*/
void Cache_Free(cache_user_t *c)
{
    cache_system_t *cs;

    if (!c->data)
    {
        Sys_Error("Cache_Free: not allocated");
    }

    cs = (static_cast<cache_system_t *>(c->data)) - 1;

    cs->prev->next = cs->next;
    cs->next->prev = cs->prev;
    cs->next = cs->prev = nullptr;

    c->data = nullptr;

    Cache_UnlinkLRU(cs);
}

/*
==============
Cache_Check
==============
*/
void *Cache_Check(cache_user_t *c)
{
    cache_system_t *cs;

    if (!c->data)
    {
        return nullptr;
    }

    cs = (static_cast<cache_system_t *>(c->data)) - 1;

    // move to head of LRU
    Cache_UnlinkLRU(cs);
    Cache_MakeLRU(cs);

    return c->data;
}

/*
==============
Cache_Alloc
==============
*/
void *Cache_Alloc(cache_user_t *c, int size, const char *name)
{
    cache_system_t *cs;

    if (c->data)
    {
        Sys_Error("Cache_Alloc: allready allocated");
    }

    if (size <= 0)
    {
        Sys_Error("Cache_Alloc: size {}", size);
    }

    size = (size + sizeof(cache_system_t) + 15) & ~15;

    // find memory for it
    while (1)
    {
        cs = Cache_TryAlloc(size, false);
        if (cs)
        {
            strncpy(cs->name, name, sizeof(cs->name) - 1);
            c->data = static_cast<void *>(cs + 1);
            cs->user = c;
            break;
        }

        // free the least recently used cahedat
        if (mem.cacheLRU.empty())
        {
            Sys_Error("Cache_Alloc: out of memory");
        }
        // not enough memory at all
        Cache_Free(mem.cacheLRU.back()->user);
    }

    return Cache_Check(c);
}

//============================================================================

/*
========================
Memory_Init
========================
*/
void Memory_Init(void *buf, size_t size)
{
    int p;
    int zonesize = DYNAMIC_SIZE;

    mem.hunk_base = static_cast<byte *>(buf);
    mem.hunk_size = size;
    mem.hunk_low_used = 0;
    mem.hunk_high_used = 0;

    Cache_Init();
    p = COM_CheckParm("-zone");
    if (p)
    {
        if (p < com_argc - 1)
        {
            zonesize = Q_atoi(com_argv[p + 1]) * 1024;
        }
        else
        {
            Sys_Error("Memory_Init: you must specify a size in KB after -zone");
        }
    }
    mem.mainzone = static_cast<memzone_t *>(Hunk_AllocName(zonesize, "zone"));
    Z_ClearZone(mem.mainzone, zonesize);
}
