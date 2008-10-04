/*
 *  libMirage: DAA image plugin
 *  Copyright (C) 2008 Rok Mandeljc
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
#include "image-daa-disc.h"
#include "image-daa-fragment.h"


G_BEGIN_DECLS

#pragma pack(1)

enum {
    DAA_FORMAT_100 = 0x100,
    DAA_FORMAT_110 = 0x110,
};

typedef struct {
    guint32 size_offset; /* Offset of sizes of zipped chunk */
    guint32 format; /* Format */
    guint32 data_offset; /* Offset of zipped chunks */
    guint32 b1; /* 1 */
    guint32 b0; /* 0 */
    guint32 chunksize; /* Size of each output chunk */
    guint64 isosize; /* Total size of the ISO file */
    guint64 filesize; /* Total size of the DAA file */
    guint8 hdata[16]; /* Data used in 0x110 format */
    guint32 crc; /* Checksum calculated over the first 72 bytes of main file */ 
} DAA_Main_Header;

typedef struct {
    guint32 data_offset; /* Offset of zipped chunks */
    guint8 hdata[16]; /* Data used in 0x110 format */
    guint32 crc; /* Checksum calculated over the first 36 bytes of part file? */ 
} DAA_Part_Header;

#pragma pack()

/* Luigi Auriemma's bit packing function */
static inline guint read_bits (guint bits, guint8 *in, guint in_bits) {
    gint seek_bits;
    gint rem;
    gint seek = 0;
    gint ret = 0;
    gint mask = 0xFFFFFFFF;

    if (bits > 32) return 0;
    if (bits < 32) mask = (1 << bits) - 1;
    for(;;) {
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

GTypeModule *global_module;

G_END_DECLS

#endif /* __IMAGE_DAA_H__ */
