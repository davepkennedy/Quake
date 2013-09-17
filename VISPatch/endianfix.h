//----------------------------------------------------------------------------------------------------------------------------
//
// "endianfix.h" - Audio buffer playback.
//
// Written by:	Axel 'awe' Wefers			[mailto:awe@fruitz-of-dojo.de].
//				©2002-2012 Fruitz Of Dojo 	[http://www.fruitz-of-dojo.de].
//
// Version History:
// v1.0.0: Initial release.
//
//----------------------------------------------------------------------------------------------------------------------------

#ifndef ENDIANFIX_H
#define ENDIANFIX_H

//----------------------------------------------------------------------------------------------------------------------------

#include "swchpal.h"
#include <stdio.h>
#include <stdint.h>

//----------------------------------------------------------------------------------------------------------------------------

inline void Endian32_Swap (uint32_t& val)
{
#if defined (__BIG_ENDIAN__)
    
    val = (((val << 24) & 0xFF000000) | ((val << 8) & 0x00FF0000) | ((val >> 8) & 0x0000FF00) | ((val >> 24) & 0x000000FF));

#else

#pragma unused (val)
    
#endif // __BIG_ENDIAN__
}

//----------------------------------------------------------------------------------------------------------------------------

inline __attribute__((always_inline)) void Endian32_Swap (int32_t& val)
{
    Endian32_Swap (*reinterpret_cast<uint32_t*> (&val));
}

//----------------------------------------------------------------------------------------------------------------------------

#if defined (__BIG_ENDIAN__)

size_t	 fread (pakheader_t* pHeader, size_t elemSize, size_t count, FILE* pFile);
size_t	 fwrite (pakheader_t* pHeader, size_t elemSize, size_t count, FILE* pFile);

size_t	 fread (pakentry_t* pEntry, size_t elemSize, size_t count, FILE* pFile);
size_t	 fwrite (pakentry_t* pEntry, size_t elemSize, size_t count, FILE* pFile);

size_t	 fread (dheader_t* pEntry, size_t elemSize, size_t count, FILE* pFile);
size_t	 fwrite (dheader_t* pEntry, size_t elemSize, size_t count, FILE* pFile);

#endif // __BIG_ENDIAN__

#endif // ENDIANFIX_H

//----------------------------------------------------------------------------------------------------------------------------
