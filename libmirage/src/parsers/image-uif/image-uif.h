/*
 *  libMirage: UIF image parser
 *  Copyright (C) 2008-2010 Henrik Stokseth
 *
 *  Thanks to Luigi Auriemma for reverse engineering work.
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

#ifndef __IMAGE_UIF_H__
#define __IMAGE_UIF_H__

#include <zlib.h>
/* #include <openssl/des.h> */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"
#include "image-uif-parser.h"

G_BEGIN_DECLS

#define OUT_ISO 0
#define OUT_NRG 1
#define OUT_CUE 2

#define IMAGE_TYPE_ISO      0x08
#define IMAGE_TYPE_MIXED    0x09

#define DATA_TYPE_UNCOMPRESSED  0x01
#define DATA_TYPE_MULTIBYTE     0x03
#define DATA_TYPE_COMPRESSED    0x05

#pragma pack(1)

typedef struct {
    gchar id[4];
    guint32 size;
} nrg_chunk_t; /* size: 8 bytes */

typedef struct {
    gchar sign[4]; /* "blhr" || "bsdr" || "blms" || "blss" */
    guint32 block_size; /* size of the data blocks plus the rest of this struct */
    guint32 compressed; /* true if data blocks are compressed */
    guint32 num_blocks; /* number of blhr_data structures */
} blhr_t; /* size: 16 or 20 bytes */

typedef struct {
    guint64 offset; /* input offset */
    guint32 zsize; /* block size */
    guint32 sector; /* where to place the output */
    guint32 size; /* size in sectors! */
    guint32 data_type; /* 1 = uncompressed, 3 = multibyte, 5 = compressed */
} blhr_data_t; /* size: 24 bytes */

typedef struct {
    gchar sign[4]; /* "bbis" */
    guint32 bbis_size; /* size of the bbis block */
    guint16 ver; /* version, 1 */
    guint16 image_type; /* 8 = ISO, 9 = mixed */
    guint16 unknown1; /* ??? */
    guint16 padding; /* ignored */
    guint32 sectors; /* number of sectors of the ISO */
    guint32 sector_size; /* image sector size */
    guint32 unknown2; /* almost ignored */
    guint64 blhr_ofs; /* where is located the blhr header */
    guint32 blhr_bbis_size; /* size of the blhr, blhr data and bbis areas */
    guint8 hash[16]; /* hash, used with passwords */
    guint32 fixedkey; /* ignored */
    guint32 unknown3; /* ignored */
} bbis_t; /* size: 64 bytes */

#pragma pack()

GTypeModule *global_module;

G_END_DECLS


#endif /* __IMAGE_UIF_H__ */
