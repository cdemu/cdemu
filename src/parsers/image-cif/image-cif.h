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

#define CIF_BLOCK_LENGTH_ADJUST    8
#define OFS_OFFSET_ADJUST          -8

#define CIF_IMAGE_AUDIO   0x03
#define CIF_IMAGE_DATA    0x01

#define CIF_MEDIA_AUDIO   0x00
#define CIF_MEDIA_MODE1   0x01
#define CIF_MEDIA_MODE2   0x03

#define CIF_MODE_AUDIO        0x00 /* 2352 bytes/sector */
#define CIF_MODE_MODE1        0x01 /* 2048 bytes/sector */
#define CIF_MODE_MODE2_FORM1  0x02 /* 2048 bytes/sector, stored internally as 2056 bytes/sector */
#define CIF_MODE_MODE2_FORM2  0x04 /* 2324 bytes/sector, stored internally as 2332 bytes/sector */

/* The CIF file format is compatible with the joint IBM/Microsoft 
Resource Interchange File Format (RIFF) standard of 1991, see references:
http://en.wikipedia.org/wiki/Resource_Interchange_File_Format
*/

#pragma pack(1)

typedef struct {
    /* main part */
    gchar   signature[4];  /* "RIFF" */
    guint32 length;        /* Length of block from this point onwards */
    /* actually part of block content */
    gchar   block_id[4];   
} CIF_BlockHeader;  /* length: 8 bytes (+4) */

typedef struct {
    gchar   block_id[4];   /* "imag" */
    guint32 dummy[2];      /* (unknown) */
    guint16 length_rest;   /* length of rest of block from this point onwards */
    guint16 version;       /* file format version? */
    gchar   signature[];   /* zero-terminated string */
} CIF_IMAG_HeaderBlock; /* length: 16 bytes + variable */

typedef struct {
    gchar   block_id[4];   /* "disc" */
    guint32 dummy[2];      /* (unknown) */
} CIF_DISC_HeaderBlock; /* length: 12 bytes */

typedef struct {
    gchar   block_id[4];   /* "adio", "info" */
} CIF_TRACK_HeaderBlock; /* length: 4 bytes */

typedef struct {
    gchar   block_id[4];   /* "ofs " */
    guint32 dummy[2];      /* (unknown) */
    guint16 num_subblocks; /* number of subblocks */
} CIF_OFS_HeaderBlock; /* length: 14 bytes */

typedef struct {
    gchar   signature[4];  /* "RIFF" */
    guint32 length;        /* Length of block from this point onwards */
    gchar   block_id[4];   /* "adio", "info" */
    guint32 ofs_offset;    /* Offset of track block in image */
    guint32 dummy;         /* (unknown) */
} CIF_OFS_SubBlock; /* length: 20 bytes */

typedef struct {
    guint16 length;        /* Length of subblock including this variable */
    guint16 dummy1;
    guint16 tracks;        /* Tracks in image */
    guint16 title_length;  /* Length of title substring */
    guint16 length2;       /* (length2 = length) */
    guint16 dummy2;
    guint16 image_type;    /* 3 = audio, 1 = data */
    guint16 dummy3;
    gchar   title_and_artist[]; /* zero-terminated, use title_length */
} CIF_DISC_FirstSubBlock; /* length: 16 bytes + variable */

typedef struct {
    guint16 length;        /* Length of subblock including this variable */
    guint16 tracks;        /* Tracks in image */
    guint8  dummy1[6];     /* (unknown) */
    guint16 media_type;    /* 0 = cd-da/audio, 1 = cdrom/mode1, 3 = mode2/cdrom-xa */
    guint8  dummy2[6];     /* (unknown) */
} CIF_DISC_SecondSubBlock; /* length: 18 */

typedef struct {
    guint16 length;        /* Length of subblock including this variable */
    guint16 dummy1;
    guint32 sectors;       /* Number of sectors in track */
    guint16 dummy2;
    guint16 mode;          /* 0 = audio, 1 = mode1, 2 = mode2 form1 , 4 = mode2 form2 */
    guint8  dummy3[10];
    guint16 sector_size;   /* Sector size (invalid for cdrom-xa) */
    /* this far things are common between audio and data tracks */
    guint8  dummy4[272];
} CIF_DISC_BinarySubBlock; /* length: 24 bytes + variable */

typedef struct {
    guint16 length;        /* Length of subblock including this variable */
    guint16 dummy1;
    guint32 sectors;       /* Number of sectors in track */
    guint16 dummy2;
    guint16 mode;          /* 0 = audio, 1 = mode1, 2 = mode2 form1 , 4 = mode2 form2 */
    guint8  dummy3[10];
    guint16 sector_size;   /* Sector size (invalid for cdrom-xa) */
    /* this far things are common between audio and data tracks */
    guint8  dummy4[205];
    gchar   isrc[12];      /* ISRC */  
    guint8  dummy5[52];
    gchar   title[];       /* zero-terminated string. */
} CIF_DISC_AudioSubBlock; /* length: 24 bytes + variable */

typedef union {
    CIF_DISC_FirstSubBlock  first;
    CIF_DISC_SecondSubBlock second;
    CIF_DISC_AudioSubBlock  track;  /* NOTE: temporary */
    CIF_DISC_BinarySubBlock binary;
    CIF_DISC_AudioSubBlock  audio;
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
