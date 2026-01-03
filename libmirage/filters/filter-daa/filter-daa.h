/*
 *  libMirage: DAA filter
 *  Copyright (C) 2008-2014 Rok Mandeljc
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#pragma once

#include "mirage/config.h"
#include <mirage/mirage.h>

#include <glib/gi18n-lib.h>
#include <errno.h>

#include "filter-stream.h"

G_BEGIN_DECLS


#pragma pack(1)

typedef enum
{
    FORMAT_VERSION1 = 0x100,
    FORMAT_VERSION2 = 0x110,
} DAA_FormatVersion;

typedef struct
{
    guint8 profile; /* PowerISO compression setting: 1 = "better", 2 = "best" */
    guint32 chunk_table_compressed; /*  */
    guint8 chunk_table_bit_settings; /* Bit sizes for chunk table entries */
    guint8 lzma_filter; /* LZMA filter type: 0 = no filter, 1 = BCJ x86 */
    guint8 lzma_props[5]; /* LZMA props: 4-byte dictionary size, 1-byte lc/lp/pb */
    guint8 reserved[4];
} DAA_Format2Header; /* size: 16 bytes */

typedef struct
{
    gchar signature[16]; /* Signature */
    guint32 chunk_table_offset; /* Offset of chunk table */
    guint32 format_version; /* Format version */
    guint32 chunk_data_offset; /* Offset of chunk data */
    guint32 __dummy__1; /* Always 0x00000001? */
    guint32 __dummy__2; /* Always 0x00000000? */
    guint32 chunk_size; /* Uncompressed size of each chunk */
    guint64 iso_size; /* Size of the ISO file */
    guint64 daa_size; /* Size of the DAA file */
    DAA_Format2Header format2; /* Additional data, used in format 2 */
    guint32 crc; /* CRC32 over the first 72 bytes of main file */
} DAA_MainHeader; /* size: 76 bytes */

typedef struct
{
    gchar signature[16]; /* Signature */
    guint32 chunk_data_offset; /* Offset of zipped chunks */
    DAA_Format2Header format2; /* Additional data, used in format 2 */
    guint32 crc; /* CRC32 over the first 36 bytes of part file */
} DAA_PartHeader; /* size: 40 bytes */


/* Descriptor blocks */
typedef enum
{
    DESCRIPTOR_PART = 1, /* Part information */
    DESCRIPTOR_SPLIT = 2, /* Split archive information */
    DESCRIPTOR_ENCRYPTION = 3, /* Encryption information */
    DESCRIPTOR_COMMENT = 4, /* Comment */
} DAA_DescriptorType;


typedef struct
{
    guint32 type; /* Descriptor type */
    guint32 length; /* Descriptor length */
} DAA_DescriptorHeader;

typedef struct
{
    guint32 num_parts; /* Number of parts */
    guint32 __dummy__; /* Always 1? */
    /* + variable amount of 5-byte blocks */
} DAA_DescriptorSplit;

typedef struct
{
    guint32 encryption_type; /* Encryption type */
    guint32 password_crc; /* Password CRC */
    guint8 daa_key[128]; /* Stored DAA key */
} DAA_DescriptorEncryption;

#pragma pack()


G_END_DECLS
