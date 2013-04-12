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
#include <zlib.h>
#include <bzlib.h>

#include "mirage.h"
#include "filter-dmg-file-filter.h"


G_BEGIN_DECLS

#define DMG_SECTOR_SIZE 512

/* Resource types */
typedef enum {
    RT_BLKX = 1,
    RT_PLST = 2
} DMG_res_type;

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

/* Partition Map Flags */
typedef enum {
    PME_VALID         = 0x0001,
    PME_ALLOCATED     = 0x0002,
    PME_IN_USE        = 0x0004,
    PME_BOOTABLE      = 0x0008,
    PME_READABLE      = 0x0010,
    PME_WRITABLE      = 0x0020,
    PME_OS_PIC_CODE   = 0x0040,
    PME_OS_SPECIFIC_2 = 0x0080,
    PME_OS_SPECIFIC_1 = 0x0100,
    /* bits 9..31 are reserved */
} DMG_part_map_flag;

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
    guint32 mish_header_length; /* size of mish header */
    guint32 mish_total_length;  /* size of mish header and mish blocks (but not rsrc blocks) */
    guint32 mish_blocks_length; /* size of "blkx" blocks and "plst" blocks */
    guint32 rsrc_total_length;  /* size of rsrc header, rsrc blocks and rsrc names */
    guint32 reserved[60]; /* zero padding */
} mish_header_t; /* length: 256 bytes */

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

typedef struct {
    guint32 block; /* driver's block start, block_size-blocks */
    guint16 size; /* driver's block count, 512-blocks */
    guint16 type; /* driver's system type */
} driver_descriptor_table_t; /* length: 8 bytes */

typedef struct {
    gchar   signature[2]; /* "ER" */
    guint16 block_size; /* block size for this device */
    guint32 block_count; /* block count for this device */
    guint16 device_type; /* device type */
    guint16 device_id; /* device id */
    guint32 driver_data; /* driver data */
    guint16 driver_count; /* driver descriptor count */

    driver_descriptor_table_t driver_map[8]; /* driver_descriptor_table */

    guint8  reserved[430]; /* reserved for future use */
} driver_descriptor_map_t; /* length: 512 bytes */

typedef struct {
    gchar   signature[2]; /* "PM" */
    guint16 reserved1; /* zero padding */
    guint32 map_entries; /* number of partition entries */
    guint32 pblock_start; /* physical block start of partition */
    guint32 pblock_count; /* physical block count of partition */
    gchar   part_name[32]; /* name of partition */
    gchar   part_type[32]; /* type of partition, eg. Apple_HFS */
    guint32 lblock_start; /* logical block start of partition */
    guint32 lblock_count; /* logical block count of partition */
    guint32 flags; /* partition flags (one of DMG_part_map_flag)*/
    guint32 boot_block; /* logical block start of boot code */
    guint32 boot_bytes; /* byte count of boot code */
    guint32 load_address; /* load address in memory of boot code */
    guint32 load_address2; /* reserved for future use */
    guint32 goto_address; /* jump address in memory of boot code */
    guint32 goto_address2; /* reserved for future use */
    guint32 boot_checksum; /* checksum of boot code */
    gchar   processor_id[16]; /* processor type */
    guint32 reserved2[32]; /* reserved for future use */
    guint32 reserved3[62]; /* reserved for future use */
} part_map_entry_t; /* length: 512 bytes */

typedef struct {
    /* starts with a duplicate of the mish header */
    guint32 mish_header_length;
    guint32 mish_total_length;
    guint32 mish_blocks_length;
    guint32 rsrc_total_length;
    /* then continues ... */
    guint16 unknown1[4];
    guint16 mark_offset; /* offset to marker (or length including rsrc_size) */
    guint16 rsrc_length; /* length of rsrc_header_t + rsrc_block_t * n */
    /* MARKER: blkx and plst rsrc_block_t offsets start from here */
    guint16 unknown2; /* info_version? */
    gchar   blkx_sign[4]; /* "blkx" */
    guint16 last_blkx_rsrc; /* index of last blkx rsrc_block_t */
    guint16 blkx_rsrc_offset; /* offset to blkx rsrc_block_t */
    gchar   plst_sign[4]; /* "plst" */
    guint16 last_plst_rsrc; /* index of last plst rsrc_block_t */
    guint16 plst_rsrc_offset; /* offset to plst rsrc_block_t */
} rsrc_header_t; /* length: 46 bytes */

typedef struct {
    gint16  id; /* matches mish block descriptor */
    gint16  rel_offs_name; /* offset after last blocks to name entry (or -1) */
    guint16 attrs; /* attributes */
    guint16 rel_offs_block; /* offset after mish header to mish block entry */
    guint32 reserved; /* zero padding */
} rsrc_block_t; /* length: 12 bytes */

#pragma pack()

typedef struct {
    gpointer     data;
    guint32      length;
    DMG_res_type type;
} resource_t;

G_END_DECLS

#endif /* __FILTER_DMG_H__ */
