/*
 *  libMirage: C2D image parser
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

#ifndef __IMAGE_C2D_H__
#define __IMAGE_C2D_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"
#include "image-c2d-disc.h"


G_BEGIN_DECLS

#define C2D_SIGNATURE_1 "Adaptec CeQuadrat VirtualCD File"
#define C2D_SIGNATURE_2 "Roxio Image File Format 3.0"

/*
 * I've found the following mode values: 
 * 0x00 0x10 = Audio, 0x04 0x14 = CDROM mode 1.
 */

#define C2D_MODE_COPYRIGHT   0x01
#define C2D_MODE_PREEMPHASIS 0x02
#define C2D_MODE_DATA        0x04 /* Set if data track, unset if audio track */
#define C2D_MODE_UNKNOWN     0x08 /* I don't know */
#define C2D_MODE_O           0x10 /* WinOnCD says it is the "O" flag. Usually set. */

#pragma pack(1)

typedef struct {
    gchar   signature[32];   /* Signature string: "Roxio Image File Format 3.0" || "Adaptec CeQuadrat VirtualCD File" */
    guint16 header_size;     /* Length of header block? */
    guint16 has_upc_ean;     /* Boolean flag */
    gchar   upc_ean[13];     /* UPC / EAN string */
    guint8  dummy1;          /* (unknown) */
    guint16 track_blocks;    /* Number of track blocks  */
    guint32 size_cdtext;     /* Size of CD-Text block. CD-Text block follows header. */
    guint32 offset_tracks;   /* Offset to track blocks  */
    guint32 dummy2;          /* (unknown) */
    gchar   description[80]; /* Description string. Zero terminated. */
    guint32 offset_c2ck;     /* Offset to "c2ck" block */
} C2D_HeaderBlock;  /* length: 196 bytes */

typedef struct {
    guint32 block_size;   /* Length of this c2ck block (32) */
    guint8  signature;    /* Signature string: "C2CK" */
    guint32 dummy1[2];    /* (unknown) */
    guint32 offset_art;   /* Offset to the artwork block */
} C2D_C2CKBlock;  /* length: 32 bytes */

typedef struct {
    guint32 dummy1;        /* (unknown) */
    guint8  cdtext[12];    /* CD-Text */
    guint8  dummy2[6];     /* (misc. stuff) */
    /* etc. */
} C2D_CDTextBlock; /* length: variable */

typedef struct {
    guint32 block_size;   /* Length of this track block (44) */
    guint32 first_sector; /* First sector in track */   
    guint32 last_sector;  /* Last sector in track */
    guint64 image_offset; /* Image offset of track */
    guint32 sector_size;  /* Bytes per sector */
    gchar   isrc[12];     /* ISRC string */
    guint8  mode_flags;   /* Track mode and flags */
    guint8  session;      /* Track session */
    guint8  point;        /* Track point */
    guint16 dummy1;       /* (unknown) */
    guint8  compressed;   /* Boolean flag */
    guint16 dummy2;       /* (unknown) */
} C2D_TrackBlock;  /* length: 44 bytes */

#pragma pack()

GTypeModule *global_module;

G_END_DECLS


#endif /* __IMAGE_C2D_H__ */
