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

#include "partition-tables.h"

#define __debug__ "Partition-Table"

void mirage_ddm_block_fix_endian (driver_descriptor_map_t *ddm_block)
{
    ddm_block->block_size   = GUINT16_FROM_BE(ddm_block->block_size);
    ddm_block->block_count  = GUINT32_FROM_BE(ddm_block->block_count);
    ddm_block->device_type  = GUINT16_FROM_BE(ddm_block->device_type);
    ddm_block->device_id    = GUINT16_FROM_BE(ddm_block->device_id);
    ddm_block->driver_data  = GUINT32_FROM_BE(ddm_block->driver_data);
    ddm_block->driver_count = GUINT16_FROM_BE(ddm_block->driver_count);

    for (gint i = 0; i < 8; i++) {
        ddm_block->driver_map[i].block = GUINT32_FROM_BE(ddm_block->driver_map[i].block);
        ddm_block->driver_map[i].size  = GUINT16_FROM_BE(ddm_block->driver_map[i].size);
        ddm_block->driver_map[i].type  = GUINT16_FROM_BE(ddm_block->driver_map[i].type);
    }

    /* skip reserved */
}

void mirage_pme_block_fix_endian (part_map_entry_t *pme_block)
{
    pme_block->map_entries   = GUINT32_FROM_BE(pme_block->map_entries);
    pme_block->pblock_start  = GUINT32_FROM_BE(pme_block->pblock_start);
    pme_block->pblock_count  = GUINT32_FROM_BE(pme_block->pblock_count);
    pme_block->lblock_start  = GUINT32_FROM_BE(pme_block->lblock_start);
    pme_block->lblock_count  = GUINT32_FROM_BE(pme_block->lblock_count);
    pme_block->flags         = GUINT32_FROM_BE(pme_block->flags);
    pme_block->boot_block    = GUINT32_FROM_BE(pme_block->boot_block);
    pme_block->boot_bytes    = GUINT32_FROM_BE(pme_block->boot_bytes);
    pme_block->load_address  = GUINT32_FROM_BE(pme_block->load_address);
    pme_block->load_address2 = GUINT32_FROM_BE(pme_block->load_address2);
    pme_block->goto_address  = GUINT32_FROM_BE(pme_block->goto_address);
    pme_block->goto_address2 = GUINT32_FROM_BE(pme_block->goto_address2);
    pme_block->boot_checksum = GUINT32_FROM_BE(pme_block->boot_checksum);

    /* skip reserved1, reserved2 and reserved3 */
}

void mirage_print_ddm_block(MirageContextual *self, driver_descriptor_map_t *ddm_block)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: DDM block:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  signature: %.2s\n", __debug__, ddm_block->signature);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  block_size: %u\n", __debug__, ddm_block->block_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  block_count: %u\n", __debug__, ddm_block->block_count);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  device_type: %u\n", __debug__, ddm_block->device_type);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  device_id: %u\n", __debug__, ddm_block->device_id);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  driver_data: %u\n", __debug__, ddm_block->driver_data);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  driver_count: %u\n", __debug__, ddm_block->driver_count);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
}

void mirage_print_pme_block(MirageContextual *self, part_map_entry_t *pme_block)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: PME block:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  signature: %.2s\n", __debug__, pme_block->signature);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  map_entries: %u\n", __debug__, pme_block->map_entries);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  pblock_start: %u\n", __debug__, pme_block->pblock_start);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  pblock_count: %u\n", __debug__, pme_block->pblock_count);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  part_name: %s\n", __debug__, pme_block->part_name);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  part_type: %s\n", __debug__, pme_block->part_type);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  lblock_start: %u\n", __debug__, pme_block->lblock_start);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  lblock_count: %u\n", __debug__, pme_block->lblock_count);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  flags: %u\n", __debug__, pme_block->flags);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  boot_block: %u\n", __debug__, pme_block->boot_block);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  boot_bytes: %u\n", __debug__, pme_block->boot_bytes);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  load_address: %u\n", __debug__, pme_block->load_address);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  load_address2: %u\n", __debug__, pme_block->load_address2);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  goto_address: %u\n", __debug__, pme_block->goto_address);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  goto_address2: %u\n", __debug__, pme_block->goto_address2);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  boot_checksum: %u\n", __debug__, pme_block->boot_checksum);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  processor_id: %s\n", __debug__, pme_block->processor_id);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
}

