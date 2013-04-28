/*
 *  libMirage: DMG file filter.
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __FILTER_DMG_H__
#define __FILTER_DMG_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "adc.h"
#include "resource-fork.h"
#include <zlib.h>
#include <bzlib.h>

#include "mirage.h"
#include "filter-dmg-file-filter.h"


G_BEGIN_DECLS

#define DMG_SECTOR_SIZE 512

/* Block types */
typedef enum {
    ADC     = G_MININT32+4,
    ZLIB    = G_MININT32+5,
    BZLIB   = G_MININT32+6,
    TERM    = -1,
    ZERO    = 0,
    RAW     = 1,
    IGNORE  = 2,
    COMMENT = G_MAXINT32-1,
} DMG_block_type;

/* Checksum types */
typedef enum {
    CT_NONE  = 0,
    CT_CRC32 = 2
} DMG_checksum_type;

#pragma pack(1)

typedef struct {
    guint32 type; /* One of DMG_checksum_type */
    guint32 size; /* should be 32 */
    guint32 data[32]; /* checksum */
} checksum_t; /* length: 136 bytes */

typedef struct {
    gchar      signature[4]; /* "koly" */
    guint32    version;
    guint32    header_size; /* 512 */
    guint32    flags;
    guint64    running_data_fork_offset; /* image data segment start offset */
    guint64    data_fork_offset; /* image data segment */
    guint64    data_fork_length;
    guint64    rsrc_fork_offset; /* binary descriptors */
    guint64    rsrc_fork_length;
    guint32    segment_number; /* this segment (starts at 1) */
    guint32    segment_count; /* total number of segments */
    guint32    segment_id[4];
    checksum_t data_fork_checksum; /* checksum for image data segment (compressed) */
    guint64    xml_offset; /* xml descriptors */
    guint64    xml_length;
    guint32    reserved1[30];
    checksum_t master_checksum; /* checksum of all the blkx_block_t checksums */
    guint32    image_variant;
    guint64    sector_count; /* total number of sectors in image */
    guint32    reserved2[3];
} koly_block_t; /* length: 512 bytes */

typedef struct {
    gchar      signature[4]; /* "mish" */
    guint32    info_version;
    guint64    first_sector_number; /* first sector in partition */
    guint64    sector_count; /* number of sectors in partition */
    guint64    data_start; /* input offset */
    guint32    decompressed_buffer_requested;
    gint32     blocks_descriptor; /* partition ID */
    guint32    reserved[6];
    checksum_t checksum; /* checksum of partition data (decompressed) */
    guint32    blocks_run_count; /* number of parts */
} blkx_block_t; /* length: 204 bytes */

typedef struct {
    gint32  block_type; /* One of DMG_block_type */
    guint32 reserved; /* zero padding */
    guint64 sector_offset; /* starting sector */
    guint64 sector_count; /* number of sectors */
    guint64 compressed_offset; /* input offset */
    guint64 compressed_length; /* input length */
} blkx_data_t; /* length: 40 bytes */

#pragma pack()

G_END_DECLS

#endif /* __FILTER_DMG_H__ */

