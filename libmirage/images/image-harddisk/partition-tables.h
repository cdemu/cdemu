/*
 *  libMirage: hard-disk image: partition table routines
 *  Copyright (C) 2013-2014 Henrik Stokseth
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


#ifndef __IMAGE_HD_PARTITION_TABLE_H__
#define __IMAGE_HD_PARTITION_TABLE_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <mirage/mirage.h>


G_BEGIN_DECLS

/* APM Partition Map Flags */
typedef enum {
    APM_VALID         = 0x00000001,
    APM_ALLOCATED     = 0x00000002,
    APM_IN_USE        = 0x00000004,
    APM_BOOTABLE      = 0x00000008,
    APM_READABLE      = 0x00000010,
    APM_WRITABLE      = 0x00000020,
    APM_OS_PIC_CODE   = 0x00000040,
    APM_OS_SPECIFIC_2 = 0x00000080,
    APM_OS_SPECIFIC_1 = 0x00000100,
    /* bits 9..31 are reserved */
} apm_flag_t;

/* GPT Partition Map Attributes */
typedef enum {
    GPT_SYSTEM_PARTITION     = 0x0000000000000001,
    GPT_LEGACY_BIOS_BOOTABLE = 0x0000000000000004,
    GPT_READ_ONLY            = 0x1000000000000000,
    GPT_SHADOW_COPY          = 0x2000000000000000,
    GPT_HIDDEN               = 0x4000000000000000,
    GPT_DO_NOT_AUTOMOUNT     = 0x8000000000000000
} gpt_attr_t;

#pragma pack(1)

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
    guint32 flags; /* One or more of apm_flag_t */
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
} apm_entry_t; /* length: 512 bytes */

typedef struct {
	guint32 time_low;
	guint16 time_mid;
	guint16 time_hi_and_version;
	guint8  clock_seq_hi_and_reserved;
	guint8  clock_seq_low;
	guint8  node[6];
} guid_t; /* length: 16 bytes */

typedef struct {
	gchar   signature[8]; /* "EFI PART" */
	guint32 revision;
	guint32 header_size;
	guint32 header_crc;
	guint32 reserved;
	guint64 lba_header;
	guint64 lba_backup;
	guint64 lba_start;
	guint64 lba_end;
	union {
	    guid_t  as_guid;
	    guint64 as_int[2];
    } guid;
	guint64 lba_gpt_table;
	guint32 gpt_entries;
	guint32 gpt_entry_size;
	guint32 crc_gpt_table;
} gpt_header_t; /* length: 92 bytes */

typedef struct {
    union {
        guid_t  as_guid; /* Zero indicates unused entry */
        guint64 as_int[2];
    } type;
    union {
    	guid_t  as_guid;
    	guint64 as_int[2];
    } guid;
	guint64 lba_start;
	guint64 lba_end;
	guint64 attributes; /* One or more of gpt_attr_t */
	guint16 name[36]; /* UTF-16 */
} gpt_entry_t; /* length: 128 bytes */

#pragma pack()

/* Forward declarations */
void mirage_ddm_block_fix_endian (driver_descriptor_map_t *ddm_block);
void mirage_apm_entry_block_fix_endian (apm_entry_t *pme_block);

void mirage_gpt_header_fix_endian (gpt_header_t *gpt_header);
void mirage_gpt_entry_fix_endian (gpt_entry_t *gpt_entry);

void mirage_print_ddm_block(MirageContextual *self, driver_descriptor_map_t *ddm_block);
void mirage_print_apm_entry_block(MirageContextual *self, apm_entry_t *pme_block);

void mirage_print_gpt_header(MirageContextual *self, gpt_header_t *gpt_header);
void mirage_print_gpt_entry(MirageContextual *self, gpt_entry_t *gpt_entry);

G_END_DECLS

#endif /* __IMAGE_HD_PARTITION_TABLE_H__ */

