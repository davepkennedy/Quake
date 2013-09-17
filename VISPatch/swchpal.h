// Switch Pal .h 1.0

#ifndef SWCHPAL_H
#define SWCHPAL_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef _UNIX_

#include <io.h>

#endif // !_UNIX_

#define PAK 1
#define WAD 2
#define LMP 3
#define SPR 4
#define BSP 5
#define MDL 6

typedef struct                                          // A Directory entry
{   int32_t         offset;                             // Offset to entry, in bytes, from start of file
    int32_t         size;                               // Size of entry in file, in bytes
} dentry_t;

typedef struct                                          // The BSP file header
{   
    int32_t         version;                            // Model version, must be 0x17 (23).
    dentry_t        entities;                           // List of Entities.
    dentry_t        planes;                             // Map Planes.
                                                        // numplanes = size/sizeof(plane_t)
    dentry_t        miptex;                             // Wall Textures.
    dentry_t        vertices;                           // Map Vertices.
                                                        // numvertices = size/sizeof(vertex_t)
    dentry_t        visilist;                           // Leaves Visibility lists.
    dentry_t        nodes;                              // BSP Nodes.
                                                        // numnodes = size/sizeof(node_t)
    dentry_t        texinfo;                            // Texture Info for faces.
                                                        // numtexinfo = size/sizeof(texinfo_t)
    dentry_t        faces;                              // Faces of each surface.
                                                        // numfaces = size/sizeof(face_t)
    dentry_t        lightmaps;                          // Wall Light Maps.
    dentry_t        clipnodes;                          // clip nodes, for Models.
                                                        // numclips = size/sizeof(clipnode_t)
    dentry_t        leaves;                             // BSP Leaves.
                                                        // numlaves = size/sizeof(leaf_t)
    dentry_t        lface;                              // List of Faces.
    dentry_t        edges;                              // Edges of faces.
                                                        // numedges = Size/sizeof(edge_t)
    dentry_t        ledges;                             // List of Edges.
    dentry_t        models;                             // List of Models.
                                                        // nummodels = Size/sizeof(model_t)
} dheader_t;

typedef struct                                          // Mip Texture
{   
    char            name[16];                           // Name of the texture.
    uint32_t        width;                              // width of picture, must be a multiple of 8
    uint32_t        height;                             // height of picture, must be a multiple of 8
    uint32_t        offset1;                            // offset to u_char Pix[width   * height]
    uint32_t        offset2;                            // offset to u_char Pix[width/2 * height/2]
    uint32_t        offset4;                            // offset to u_char Pix[width/4 * height/4]
    uint32_t        offset8;                            // offset to u_char Pix[width/8 * height/8]
} miptex_t;

typedef struct
{   
    uint8_t         magic[4];                           // Pak Name of the new WAD format
    int32_t         diroffset;                          // Position of WAD directory from start of file
    int32_t         dirsize;                            // Number of entries * 0x40 (64 char)
} pakheader_t;

typedef struct
{   
    uint8_t         filename[0x38];                     // Name of the file, Unix style, with extension,
                                                        // 50 chars, padded with '\0'.
    int32_t         offset;                             // Position of the entry in PACK file
    int32_t         size;                               // Size of the entry in PACK file
} pakentry_t;

/*
typedef struct
{   
    int32_t         width;
    int32_t         height;
    uint8_t         Color[width*height];
} picture_t;
 */

struct vec3_t
{
    int32_t         xyz[3];
};

struct scalar_t 
{
    int32_t         Scale;
};
typedef struct
{   
    char            id[4];                              // "IDPO" for IDPOLYGON
    int32_t         version;                            // Version = 6
    vec3_t          scale;                              // Model scale factors.
    vec3_t          origin;                             // Model origin.
    scalar_t        radius;                             // Model bounding radius.
    vec3_t          offsets;                            // Eye position (useless?)
    int32_t         numskins ;                          // the number of skin textures
    int32_t         skinwidth;                          // Width of skin texture
                                                        //           must be multiple of 8
    int32_t         skinheight;                         // Height of skin texture
                                                        //           must be multiple of 8
    int32_t         numverts;                           // Number of vertices
    int32_t         numtris;                            // Number of triangles surfaces
    int32_t         numframes;                          // Number of frames
    int32_t         synctype;                           // 0= synchron, 1= random
    int32_t         flags;                              // 0 (see Alias models)
    scalar_t        size;                               // average size of triangles
} mdl_t;

/*
typedef struct
{   
    int32_t         group;                              // value = 0
    uint8_t         skin[skinwidth*skinheight];         // the skin picture
} skin_t;


typedef struct
{   
    int32_t         group;                              // value = 1
    int32_t         nb;                                 // number of pictures in group
    float           time[nb];                           // time values, for each picture
    uint8_t         skin[nb][skinwidth*skinheight];     // the pictures
} skingroup_t;
*/

typedef struct
{   
    char            name[4];                            // "IDSP"
    int32_t         ver1;                               // Version = 1
    int32_t         type;                               // See below
    float           radius;                             // Bounding Radius
    int32_t         maxwidth;                           // Width of the largest frame
    int32_t         maxheight;                          // Height of the largest frame
    int32_t         nframes;                            // Number of frames
    float           beamlength;                         //
    int32_t         synchtype;                          // 0=synchron 1=random
} spr_t;

/*
typedef struct
{   
    int32_t         ofsx;                               // horizontal offset, in 3D space
    int32_t         ofsy;                               // vertical offset, in 3D space
    int32_t         width;                              // width of the picture
    int32_t         height;                             // height of the picture
    char            Pixels[width*height];               // array of pixels (flat bitmap)
} picture;
*/

typedef struct
{   
    uint8_t         magic[4];                           // "WAD2", Name of the new WAD format
    int32_t         numentries;                         // Number of entries
    int32_t         diroffset;                          // Position of WAD directory in file
} wadhead_t;

typedef struct
{   
    int32_t         offset;                             // Position of the entry in WAD
    int32_t         dsize;                              // Size of the entry in WAD file
    int32_t         size;                               // Size of the entry in memory
    uint8_t         type;                               // type of entry
    uint8_t         cmprs;                              // Compression. 0 if none.
    uint16_t        dummy;                              // Not used
    uint8_t         name[16];                           // 1 to 16 characters, '\0'-padded
} wadentry_t;

struct wadoentry_t
{
    int32_t         offset;                             //Offset to entry
    int32_t         size;                               //Size of entry
    char            name[8];                            //8 letters long name null filled.

};

/*
typedef struct
{   
    int32_t         width;                              // Picture width
    int32_t         height;                             // Picture height
    uint8_t         Pixels[height][width]
} pichead_t;
*/

typedef struct
{   
    int16_t         Width;                              // Picture width
    int16_t         Height;                             // Picture height
    int16_t         LOffs;
    int16_t         TOffs;
} DoomGFX_t;

struct PNames
{
    FILE*           Wad;
    char            name[9];
    uint32_t        offs;
    int32_t         EntryNum;
    int32_t         flag;
};

struct TexEnt
{
    char            Name[8];
    int16_t         zero1;                              //this is always 0  (we won't need this)
    int16_t         zero2;                              //this is always 0  (we won't need this)
    int16_t         width;
    int16_t         height;
    int16_t         zero3;                              //this is always 0  (we won't need this)
    int16_t         zero4;                              //this is always 0  (we won't need this)
    int16_t         patches;
};

struct Patch
{
    int16_t         xoffs;
    int16_t         yoffs;
    int16_t         pname;
    int16_t         one1;                               //this is always 1  (we won't need it)
    int16_t         zero5;                              //this is always 0  (we won't need this)
};

struct InternalWall
{
    char            Name[16];
//  Rendered Flag in mipinx    int hasmipped;           //when we make it so mipping happens when we read the Wad2;
    int32_t         width;
    int32_t         height;
    int32_t         NumPat;
    Patch*          patches;
};

struct mipinx{
    mipinx*         Prev;
    miptex_t        This;
    mipinx*         Next;
    int32_t         Rendered;
    InternalWall*   Wall;
};

typedef struct
{   
    int32_t         type;                               // Special type of leaf
    int32_t         vislist;                            // Beginning of visibility lists
                                                        //     must be -1 or in [0,numvislist[
    //bboxshort_t   bound;                              // Bounding box of the leaf
    float           lx;
    float           ly;
    float           lz;
    float           hx;
    float           hy;
    float           hz;
    uint16_t        lface_id;                           // First item of the list of faces
                                                        //     must be in [0,numlfaces[
    uint16_t        lface_num;                          // Number of faces in the leaf
    uint16_t        sndwater;                           // level of the four ambient sounds:
    uint16_t        sndsky;                             //   0    is no sound
    uint16_t        sndslime;                           //   0xFF is maximum volume
    uint16_t        sndlava;                            //
} dleaf_t;

typedef struct
{   
    //vec3_t        vectorS;                            // S vector, horizontal in texture space)
    float           ugh[2][4];
    uint32_t        texture_id;                         // Index of Mip Texture
                                                        //           must be in [0,numtex[
    uint32_t        animated;                           // 0 for ordinary textures, 1 for water
} surface_t;

struct MyGFX
{
    int32_t         Width;
    int32_t         Height;
    int32_t         OffsX,OffsY;
    int32_t         Trans;
    uint8_t*        Data;
    int32_t         filler[2];
};

char        Pause ();
int32_t     Convert (uint32_t Data,int32_t length,int dir=1);
int32_t     BSPFix (uint32_t InitOFFS);
int32_t     PakFix (uint32_t Offset);
int32_t     WadFix (uint32_t Offset);
int32_t     LmpFix (uint32_t Offset,int32_t lenght);
int32_t     SprFix (uint32_t Offset);
int32_t     MdlFix (uint32_t Offset);

int32_t     PakNew (uint32_t Offset);
int32_t     WadNew (uint32_t Offset);
int32_t     BSPNew (uint32_t Offset);
int32_t     NewOther (uint32_t Offset,int32_t lenght);
int32_t     ChooseFile (char *FileSpec,uint32_t Offset,int32_t length=0);
int32_t     ChooseLevel (char *FileSpec,uint32_t Offset,int32_t length=0);
int32_t     ChooseSource (char *FileSpec,uint32_t Offset,int32_t length=0);

int32_t     LmpMIP (uint32_t InitOFFS);

int32_t     SetWall (int32_t Mode);
int32_t     MakeWall (mipinx *MipSou);
void        PatchWall (uint32_t Offset, uint8_t* Wall, int32_t xoffs, int32_t yoffs);
int32_t     Buff2Buff (int32_t x, int32_t y, MyGFX* Source, uint8_t*Buffer);
int32_t     CleanUp (mipinx* List);
void        TexNameSwch ();

#endif // SWCHPAL_H
