//----------------------------------------------------------------------------------------------------------------------------
//
// "endianfix.cpp" - Audio buffer playback.
//
// Written by:	Axel 'awe' Wefers			[mailto:awe@fruitz-of-dojo.de].
//				©2002-2012 Fruitz Of Dojo 	[http://www.fruitz-of-dojo.de].
//
// Version History:
// v1.0.0: Initial release.
//
//----------------------------------------------------------------------------------------------------------------------------

#if defined (__BIG_ENDIAN__)

#include "endianfix.h"

//----------------------------------------------------------------------------------------------------------------------------

static void	Endian_Swap (pakentry_t* pEntry, size_t count);
static void	Endian_Swap (pakheader_t* pHeader, size_t count);
static void	Endian_Swap (dentry_t* pEntry);
static void	Endian_Swap (dentry_t* pEntry, size_t count);
static void	Endian_Swap (dheader_t* pHeader, size_t count);

//----------------------------------------------------------------------------------------------------------------------------

void	Endian_Swap (pakentry_t* pEntry, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        Endian32_Swap (pEntry[i].offset);
        Endian32_Swap (pEntry[i].size);
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void	Endian_Swap (pakheader_t* pHeader, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        Endian32_Swap (pHeader[i].diroffset);
        Endian32_Swap (pHeader[i].dirsize);
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void	Endian_Swap (dentry_t* pEntry)
{
    Endian32_Swap (pEntry->offset);
    Endian32_Swap (pEntry->size);
}

//----------------------------------------------------------------------------------------------------------------------------

void	Endian_Swap (dentry_t* pEntry, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        Endian_Swap (&pEntry[i]);
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void	Endian_Swap (dheader_t* pHeader, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        Endian32_Swap (pHeader[i].version);
    
        Endian_Swap (&pHeader[i].entities);
        Endian_Swap (&pHeader[i].planes);
        Endian_Swap (&pHeader[i].miptex);
        Endian_Swap (&pHeader[i].vertices);
        Endian_Swap (&pHeader[i].visilist);
        Endian_Swap (&pHeader[i].nodes);
        Endian_Swap (&pHeader[i].texinfo);
        Endian_Swap (&pHeader[i].faces);
        Endian_Swap (&pHeader[i].lightmaps);
        Endian_Swap (&pHeader[i].clipnodes);
        Endian_Swap (&pHeader[i].leaves);
        Endian_Swap (&pHeader[i].lface);
        Endian_Swap (&pHeader[i].edges);
        Endian_Swap (&pHeader[i].ledges);
        Endian_Swap (&pHeader[i].models);
    }
}

//----------------------------------------------------------------------------------------------------------------------------

size_t	 fread (pakheader_t* pHeader, size_t elemSize, size_t count, FILE* pFile)
{
    size_t	numBytes = fread (pHeader, elemSize, count, pFile);

    Endian_Swap (pHeader, count);
    
    return numBytes;
}

//----------------------------------------------------------------------------------------------------------------------------

size_t	 fwrite (pakheader_t* pHeader, size_t elemSize, size_t count, FILE* pFile)
{
    Endian_Swap (pHeader, count);
    
    size_t  numBytes = fwrite (pHeader, elemSize, count, pFile);
    
    Endian_Swap (pHeader, count);
    
    return numBytes;
}

//----------------------------------------------------------------------------------------------------------------------------

size_t	 fread (pakentry_t* pHeader, size_t elemSize, size_t count, FILE* pFile)
{
    size_t	numBytes = fread (pHeader, elemSize, count, pFile);
    
    Endian_Swap (pHeader, count);
    
    return numBytes;
}

//----------------------------------------------------------------------------------------------------------------------------

size_t	 fwrite (pakentry_t* pHeader, size_t elemSize, size_t count, FILE* pFile)
{
    Endian_Swap (pHeader, count);
    
    size_t  numBytes = fwrite (pHeader, elemSize, count, pFile);
    
    Endian_Swap (pHeader, count);
    
    return numBytes;
}

//----------------------------------------------------------------------------------------------------------------------------

size_t	 fread (dheader_t* pHeader, size_t elemSize, size_t count, FILE* pFile)
{
    size_t  numBytes = fread (pHeader, elemSize, count, pFile);
    
    Endian_Swap (pHeader, count);
    
    return numBytes;
}

//----------------------------------------------------------------------------------------------------------------------------

size_t	 fwrite (dheader_t* pHeader, size_t elemSize, size_t count, FILE* pFile)
{
    Endian_Swap (pHeader, count);
    
    size_t numBytes = fwrite (pHeader, elemSize, count, pFile);
    
    Endian_Swap (pHeader, count);

    return numBytes;
}

#endif // __BIG_ENDIAN__

//----------------------------------------------------------------------------------------------------------------------------
