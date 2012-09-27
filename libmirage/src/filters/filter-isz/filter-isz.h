/*
 *  libMirage: ISZ file filter
 *  Copyright (C) 2012 Henrik Stokseth
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

#ifndef __FILTER_ISZ_H__
#define __FILTER_ISZ_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <zlib.h>
#include <bzlib.h>

#include "mirage.h"
#include "filter-isz-file-filter.h"


G_BEGIN_DECLS

#define ISZ_SIGNATURE "IsZ!"

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
    guint32 checksum1;        /* checksum of uncompressed data */
    guint32 data_size;        /* data size */
    guint32 unknown;          /* ? */
    guint32 checksum2;        /* checksum of compressed data */
} ISZ_Header; /* length: 64 bytes */

typedef struct ISZ_Footer_s {
    struct  ISZ_Header_s footer; /* Exact duplicate of ISZ header */
    guint32 footer_size;         /* Size of header / footer excluding this variable */
} ISZ_Footer; /* length: 68 bytes */

typedef struct {
    guint64 size;             /* segment size in bytes */
    guint32 num_chunks;       /* number of chunks in current file */
    guint32 first_chunk_num;  /* first chunk number in current file */
    guint32 chunk_offs;       /* offset to first chunk in current file */
    guint32 left_size;        /* incompltete chunk bytes in next file */
} ISZ_Segment; /* length: 24 bytes */

#pragma pack()

typedef struct {
    /* Read from chunk table */
    guint8  type;     /* One of ISZ_ChunkType */
    guint32 length;   /* Chunk length */
    /* Computed values */
    guint64 offset;   /* Offset to input data */
} ISZ_Chunk; /* length depending on ptr_len */

G_END_DECLS

#endif /* __FILTER_ISZ_H__ */
