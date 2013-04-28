/*
 *  libMirage: Partition table routines.
 *  Copyright (C) 2013 Henrik Stokseth
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


#ifndef __PARTITION_TABLE_H__
#define __PARTITION_TABLE_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"


G_BEGIN_DECLS

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
} APM_part_map_flag;

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
    guint32 flags; /* partition flags (one of APM_part_map_flag)*/
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

#pragma pack()

/* Forward declarations */
void mirage_ddm_block_fix_endian (driver_descriptor_map_t *ddm_block);
void mirage_pme_block_fix_endian (part_map_entry_t *pme_block);

void mirage_print_ddm_block(MirageContextual *self, driver_descriptor_map_t *ddm_block);
void mirage_print_pme_block(MirageContextual *self, part_map_entry_t *pme_block);

G_END_DECLS

#endif /* __PARTITION_TABLE_H__ */

