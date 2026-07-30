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
#pragma once

#include <memory_resource>

// A std::pmr::memory_resource facade over zone.cpp's existing low-hunk
// bump allocator. do_allocate forwards straight to Hunk_AllocName, so every
// invariant that function already maintains (16-byte-aligned blocks, the
// hunk_t sentinel header Hunk_Check() walks, Cache_FreeLow() eviction) is
// preserved automatically -- this is a thin adapter, not a second
// implementation of the bump allocator.
//
// Only safe for trivially-destructible data (byte buffers, POD structs) --
// exactly what Hunk_Alloc is used for today. Hunk_FreeToLowMark() rewinds
// a pointer; it does not run destructors, so housing anything with owned
// resources here and later rewinding past it would skip its destructor.
//
// Mark/rewind is done through the existing Hunk_LowMark()/Hunk_FreeToLowMark()
// free functions -- both correctly bound anything allocated through this
// resource, since it shares the same underlying low-hunk pointer as every
// legacy Hunk_AllocName caller.
class HunkMemoryResource : public std::pmr::memory_resource
{
  protected:
    void *do_allocate(size_t bytes, size_t alignment) override;
    void do_deallocate(void *p, size_t bytes, size_t alignment) override;
    bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override;
};

HunkMemoryResource *Hunk_GetResource(void);
