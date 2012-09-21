/*
 *  libMirage: DAA image plugin
 *  Copyright (C) 2008-2012 Rok Mandeljc
 *
 *  Derived from code of GPLed utility daa2iso, written by Luigi Auriemma:
 *  http://aluigi.altervista.org/mytoolz.htm
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

#ifndef __IMAGE_DAA_H__
#define __IMAGE_DAA_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <zlib.h>
#include "LzmaDec.h"
#include "Bra.h"

#include "mirage.h"
#include "image-daa-parser.h"
#include "image-daa-fragment.h"


G_BEGIN_DECLS

#pragma pack(1)

static const gchar daa_main_signature[16] = "DAA";
static const gchar daa_part_signature[16] = "DAA VOL";

typedef enum
{
    FORMAT_VERSION1 = 0x100,
    FORMAT_VERSION2 = 0x110,
} DAA_FormatVersion;

typedef struct
{
    guint32 chunk_table_offset; /* Offset of chunk table */
    guint32 format_version; /* Format version */
    guint32 chunk_data_offset; /* Offset of chunk data */
    guint32 __dummy__1; /* Always 0x00000001? */
    guint32 __dummy__2; /* Always 0x00000000? */
    guint32 chunk_size; /* Uncompressed size of each chunk */
    guint64 iso_size; /* Size of the ISO file */
    guint64 daa_size; /* Size of the DAA file */
    guint8 hdata[16]; /* Data used in 0x110 format */
    guint32 crc; /* Checksum calculated over the first 72 bytes of main file */
} DAA_Main_Header;

typedef struct
{
    guint32 chunk_data_offset; /* Offset of zipped chunks */
    guint8 hdata[16]; /* Data used in 0x110 format */
    guint32 crc; /* Checksum calculated over the first 36 bytes of part file? */
} DAA_Part_Header;

#pragma pack()

/* Luigi Auriemma's bit packing function */
static inline guint read_bits (guint bits, guint8 *in, guint in_bits)
{
    gint seek_bits;
    gint rem;
    gint seek = 0;
    gint ret = 0;
    gint mask = 0xFFFFFFFF;

    if (bits > 32) return 0;
    if (bits < 32) mask = (1 << bits) - 1;
    for (;;) {
        seek_bits = in_bits & 7;
        ret |= ((in[in_bits >> 3] >> seek_bits) & mask) << seek;
        rem = 8 - seek_bits;
        if (rem >= bits)
            break;
        bits -= rem;
        in_bits += rem;
        seek += rem;
        mask = (1 << bits) - 1;
    }

    return ret;
}

G_END_DECLS

#endif /* __IMAGE_DAA_H__ */
