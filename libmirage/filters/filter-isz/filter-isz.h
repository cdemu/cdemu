/*
 *  libMirage: ISZ filter
 *  Copyright (C) 2012-2014 Henrik Stokseth
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

#pragma once

#include "mirage/config.h"
#include <mirage/mirage.h>

#include <glib/gi18n-lib.h>
#include <zlib.h>
#include <bzlib.h>

#include "filter-stream.h"

G_BEGIN_DECLS


typedef enum {
    NONE     = 0,
    PASSWORD = 1,
    AES128   = 2,
    AES192   = 3,
    AES256   = 4
} ISZ_Encryption;

typedef enum {
    ZERO = 0,
    DATA = 1,
    ZLIB = 2,
    BZ2  = 3
} ISZ_ChunkType;

#pragma pack(1)

typedef struct ISZ_Header_s {
    gchar   signature[4];     /* 'IsZ!' */
    guint8  header_size;      /* header size in bytes */
    guint8  version;          /* version number */
    guint32 vol_sn;           /* volume serial number */
    guint16 sect_size;        /* sector size in bytes */
    guint32 total_sectors;    /* total sectors of image */
    guint8  encryption_type;  /* is Password protected? */
    guint64 segment_size;     /* size of segments in bytes */
    guint32 num_blocks;       /* number of chunks in image */
    guint32 block_size;       /* chunk size in bytes (must be multiple of sector_size) */
    guint8  ptr_len;          /* chunk pointer length */
    guint8  seg_num;          /* segment number of this segment file, max 99 */
    guint32 chunk_offs;       /* offset of chunk pointers, zero = none */
    guint32 seg_offs;         /* offset of segment pointers, zero = none */
    guint32 data_offs;        /* data offset */
    guint8  reserved;         /* reserved */
    /* Additional data not mentioned in the specification */
    guint32 checksum1;        /* CRC32 of uncompressed data (apply binary NOT) */
    guint32 data_size;        /* total input data size (UltraISO: checksum2)*/
    guint32 unknown;          /* ? (UltraISO: checksum2 + 4) */
    guint32 checksum2;        /* CRC32 of compressed data (apply binary NOT) */
} ISZ_Header; /* length: 48 or 64 bytes */

typedef struct ISZ_Footer_s {
    struct  ISZ_Header_s footer; /* Exact duplicate of ISZ header */
    guint32 footer_size;         /* Size of header / footer excluding this variable */
} ISZ_Footer; /* length: 68 bytes */

typedef struct {
    guint64 size;             /* segment size in bytes */
    guint32 num_chunks;       /* number of chunks in segment */
    guint32 first_chunk_num;  /* first chunk number in segment */
    guint32 chunk_offs;       /* offset to first chunk data in segment */
    guint32 left_size;        /* incomplete chunk data bytes in next segment */
} ISZ_Segment; /* length: 24 bytes */

#pragma pack()

typedef struct {
    /* Read from chunk table */
    guint8  type;       /* One of ISZ_ChunkType */
    guint32 length;     /* Chunk length */
    /* Computed values */
    guint8  segment;    /* Segment that has (start of) chunk */
    guint64 offset;     /* Offset to input data */
    guint64 adj_offset; /* Offset adjusted relative to start of segment data */
} ISZ_Chunk; /* length depending on ptr_len */


G_END_DECLS
