/*
 *  libMirage: CIF image parser
 *  Copyright (C) 2006-2008 Henrik Stokseth
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __IMAGE_CIF_H__
#define __IMAGE_CIF_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"
#include "image-cif-disc.h"


G_BEGIN_DECLS

#define CIF_BLOCK_HEADER_SIZE      20
#define CIF_BLOCK_LENGTH_ADJUST    8
#define OFS_OFFSET_ADJUST          -8

#define CIF_TRACK_BINARY    0x6f666e69 /* "info" */
#define CIF_TRACK_AUDIO     0x6f696461 /* "adio" */

/* The CIF file format is compatible with the joint IBM/Microsoft 
Resource Interchange File Format (RIFF) standard of 1991, see references:
http://en.wikipedia.org/wiki/Resource_Interchange_File_Format
*/

#pragma pack(1)

typedef struct {
    /* main part */
    guint8  signature[4];  /* "RIFF" */
    guint32 length;        /* Length of block from this point onwards */
    /* actually part of block content */
    guint8  block_id[4];   /* "imag", "disc", "adio", "info", "ofs " */
    /* TODO: Get rid of this eventually */
    guint32 dummy[2];      /* (unknown) */
} CIF_BlockHeader;  /* length: 20 bytes */

typedef struct {
    guint8  block_id[4];   /* "imag", "disc", "adio", "info", "ofs " */
    guint32 dummy1[2];     /* (unknown) */
} CIF_General_HeaderBlock; /* length: 12 bytes */

typedef struct {
    guint8  block_id[4];   /* "ofs " */
    guint32 dummy1[2];     /* (unknown) */
    guint16 num_subblocks; /* number of subblocks */
} CIF_OFS_HeaderBlock; /* length: 14 bytes */

typedef struct {
    guint8  signature[4];  /* "RIFF" */
    guint32 length;        /* Length of block from this point onwards */
    guint8  block_id[4];   /* "imag", "disc", "adio", "info", "ofs " */
    guint32 ofs_offset;    /* Offset of track block in image */
    guint32 dummy;         /* (unknown) */
} CIF_OFS_SubBlock;  /* length: 20 bytes */

typedef struct {
    guint16 length;        /* Length of subblock including this variable */
    /* ... */
} CIF_DISC_SubBlock; /* length: variable */

#pragma pack()

typedef struct {
    gint            block_offset; /* offset in image */
    CIF_BlockHeader *block_header;

    GList           *subblock_index;
    guint           num_subblocks;    
} CIFBlockIndexEntry;

typedef struct {
    guint8          *start;
    guint           length;
} CIFSubBlockIndexEntry;


GTypeModule *global_module;

G_END_DECLS

#endif /* __IMAGE_CIF_H__ */
