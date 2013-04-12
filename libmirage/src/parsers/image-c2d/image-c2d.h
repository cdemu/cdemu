/*
 *  libMirage: C2D image parser
 *  Copyright (C) 2008-2012 Henrik Stokseth
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __IMAGE_C2D_H__
#define __IMAGE_C2D_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"
#include "image-c2d-parser.h"


G_BEGIN_DECLS

typedef enum {
    COPYRIGHT   = 0x01, /* Copyright */
    PREEMPHASIS = 0x02, /* Pre-emphasis */
    DATA        = 0x04, /* Set on data tracks. */
    UNKNOWN     = 0x08, /* ? */
    O           = 0x10 /* WinOnCD says it is the "O" flag. */
} C2D_Flag;

typedef enum {
    AUDIO  = 0x00, /* CD-DA */
    MODE1  = 0x01, /* CD-ROM */
    MODE2  = 0x02, /* CD-ROM XA */
    AUDIO2 = 0xFF /* CD-DA */
} C2D_Mode;


#pragma pack(1)

typedef struct
{
    gchar signature[32]; /* Signature string: "Roxio Image File Format 3.0" || "Adaptec CeQuadrat VirtualCD File" */
    guint16 header_size; /* Length of header block */
    guint16 has_upc_ean; /* Boolean flag */
    gchar upc_ean[13]; /* UPC / EAN string */
    guint8 dummy1; /* (unknown) */
    guint16 num_track_blocks; /* Number of track blocks  */
    guint32 size_cdtext; /* Size of CD-Text blocks. CD-Text blocks follows header. */
    guint32 offset_tracks; /* Offset to track blocks  */
    guint32 dummy2; /* Offset to something? || 0x00000000 */
    gchar description[128]; /* Description string. Zero terminated. */
    guint32 offset_c2ck; /* Offset to "c2ck" block || 0x00000000 */
} C2D_HeaderBlock; /* length: as given in header block */

typedef struct
{
    guint8 pack_type;
    guint8 track_number;
    guint8 seq_number;
    guint8 block_number;
    guint8 data[12];
    guint8 crc[2];
} C2D_CDTextBlock; /* length: 18 bytes */

typedef struct
{
    guint32 block_size; /* Length of this c2ck block (32) */
    gchar   signature[4]; /* Signature string: "C2CK" */
    guint32 dummy1[2]; /* (unknown) */
    guint64 next_offset; /* Offset to the blocks after track data: WOCD, C2AW etc. */
    guint32 dummy2[2]; /* (unknown) */
} C2D_C2CKBlock; /* length: 32 bytes */

typedef struct
{
    guint32 block_size; /* Length of this track block (44) */
    guint32 first_sector; /* First sector in track */
    guint32 last_sector; /* Last sector in track */
    guint64 image_offset; /* Image offset of track || 0xFFFFFFFF if index > 1 */
    guint32 sector_size; /* Bytes per sector */
    gchar isrc[12]; /* ISRC string if index == 1 */
    guint8 flags; /* Track flags */
    guint8 session; /* Track session */
    guint8 point; /* Track point */
    guint8 index; /* Index */
    guint8 mode; /* Track mode */
    guint8 compressed; /* Boolean flag */
    guint16 dummy; /* (unknown) */
} C2D_TrackBlock; /* length: 44 bytes */

typedef struct
{
    guint32 dummy; /* (unknown) */
} C2D_Z_Info_Header; /* length: 4 bytes */

typedef struct
{
    guint32 compressed_size; /* Size of compressed data */
    guint64 image_offset; /* Offset of compressed data */
} C2D_Z_Info; /* length: 12  bytes */

typedef struct
{
    guint32 block_size; /* Length of this c2aw block (32) */
    gchar signature[4]; /* Signature string: "C2AW" */
    guint64 info_size; /* size of artwork info; follows this block */
    guint64 next_offset; /* Offset to next block */
    guint32 dummy[2]; /* (unknown) */
} C2D_C2AWBlock; /* length: 32 bytes */

typedef struct
{
    guint32 block_size; /* Length of this wocd block (32) */
    gchar signature[4]; /* Signature string: "WOCD" */
    guint32 dummy[6]; /* (unknown) */
} C2D_WOCDBlock; /* length: 32 bytes */

#pragma pack()

G_END_DECLS


#endif /* __IMAGE_C2D_H__ */
