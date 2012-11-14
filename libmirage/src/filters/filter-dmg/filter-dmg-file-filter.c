/*
 *  libMirage: DMG file filter: File filter object
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

#include "filter-dmg.h"

#define __debug__ "DMG-FileFilter"


static const guint8 koly_signature[4] = { 'k', 'o', 'l', 'y' };
static const guint8 mish_signature[4] = { 'm', 'i', 's', 'h' };

typedef struct {
    DMG_block_type type;

    gint     first_sector;
    gint     num_sectors;
    gint     segment;
    goffset  in_offset;
    gsize    in_length;
} DMG_Part;


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_FILE_FILTER_DMG_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FILE_FILTER_DMG, MirageFileFilterDmgPrivate))

struct _MirageFileFilterDmgPrivate
{
    /* koly blocks */
    koly_block_t *koly_block;
    gint num_koly_blocks;

    /* rsrc blocks */
    GArray *rsrc_block;
    gint num_rsrc_blocks;

    /* rsrc names */
    gchar *rsrc_name;
    gint rsrc_name_length;

    GArray *resource;

    /* streams */
    GInputStream **streams;
    gint num_streams;

    /* Part list */
    DMG_Part *parts;
    gint num_parts;

    /* Inflate buffer */
    guint8 *inflate_buffer;
    gint inflate_buffer_size;
    gint cached_part;

    /* I/O buffer */
    guint8 *io_buffer;
    gint io_buffer_size;

    /* Compression streams */
    z_stream  zlib_stream;
    bz_stream bzip2_stream;
};


/**********************************************************************\
 *                    Endian-conversion functions                     *
\**********************************************************************/
static inline void mirage_file_filter_dmg_koly_block_fix_endian (koly_block_t *koly_block)
{
    koly_block->version       = GUINT32_FROM_BE(koly_block->version);
    koly_block->header_size   = GUINT32_FROM_BE(koly_block->header_size);
    koly_block->flags         = GUINT32_FROM_BE(koly_block->flags);
    koly_block->image_variant = GUINT32_FROM_BE(koly_block->image_variant);

    koly_block->running_data_fork_offset = GUINT64_FROM_BE(koly_block->running_data_fork_offset);
    koly_block->data_fork_offset         = GUINT64_FROM_BE(koly_block->data_fork_offset);
    koly_block->data_fork_length         = GUINT64_FROM_BE(koly_block->data_fork_length);
    koly_block->rsrc_fork_offset         = GUINT64_FROM_BE(koly_block->rsrc_fork_offset);
    koly_block->rsrc_fork_length         = GUINT64_FROM_BE(koly_block->rsrc_fork_length);
    koly_block->xml_offset               = GUINT64_FROM_BE(koly_block->xml_offset);
    koly_block->xml_length               = GUINT64_FROM_BE(koly_block->xml_length);
    koly_block->sector_count             = GUINT64_FROM_BE(koly_block->sector_count);

    koly_block->segment_number = GUINT32_FROM_BE(koly_block->segment_number);
    koly_block->segment_count  = GUINT32_FROM_BE(koly_block->segment_count);

    for (gint i = 0; i < 4; i++) {
        koly_block->segment_id[i] = GUINT32_FROM_BE(koly_block->segment_id[i]);
    }

    koly_block->data_fork_checksum.type = GUINT32_FROM_BE(koly_block->data_fork_checksum.type);
    koly_block->data_fork_checksum.size = GUINT32_FROM_BE(koly_block->data_fork_checksum.size);
    koly_block->master_checksum.type    = GUINT32_FROM_BE(koly_block->master_checksum.type);
    koly_block->master_checksum.size    = GUINT32_FROM_BE(koly_block->master_checksum.size);

    for (gint i = 0; i < 32; i++) {
        koly_block->data_fork_checksum.data[i] = GUINT32_FROM_BE(koly_block->data_fork_checksum.data[i]);
        koly_block->master_checksum.data[i]    = GUINT32_FROM_BE(koly_block->master_checksum.data[i]);
    }

    /* skip reserved1 and reserved2 */
}

static inline void mirage_file_filter_dmg_ddm_block_fix_endian (driver_descriptor_map_t *ddm_block)
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

static inline void mirage_file_filter_dmg_pme_block_fix_endian (part_map_entry_t *pme_block)
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

static inline void mirage_file_filter_dmg_mish_header_fix_endian (mish_header_t *mish_header)
{
    mish_header->mish_header_length = GUINT32_FROM_BE(mish_header->mish_header_length);
    mish_header->mish_total_length  = GUINT32_FROM_BE(mish_header->mish_total_length);
    mish_header->mish_blocks_length = GUINT32_FROM_BE(mish_header->mish_blocks_length);
    mish_header->rsrc_total_length  = GUINT32_FROM_BE(mish_header->rsrc_total_length);

    /* skip reserved */
}

static inline void mirage_file_filter_dmg_blkx_block_fix_endian (blkx_block_t *blkx_block)
{
    blkx_block->info_version = GUINT32_FROM_BE(blkx_block->info_version);

    blkx_block->first_sector_number = GUINT64_FROM_BE(blkx_block->first_sector_number);
    blkx_block->sector_count        = GUINT64_FROM_BE(blkx_block->sector_count);
    blkx_block->data_start          = GUINT64_FROM_BE(blkx_block->data_start);

    blkx_block->decompressed_buffer_requested = GUINT32_FROM_BE(blkx_block->decompressed_buffer_requested);

    blkx_block->blocks_descriptor = GINT32_FROM_BE(blkx_block->blocks_descriptor);
    blkx_block->blocks_run_count  = GUINT32_FROM_BE(blkx_block->blocks_run_count);
    blkx_block->checksum.type     = GUINT32_FROM_BE(blkx_block->checksum.type);
    blkx_block->checksum.size     = GUINT32_FROM_BE(blkx_block->checksum.size);

    for (gint i = 0; i < 32; i++) {
        blkx_block->checksum.data[i] = GUINT32_FROM_BE(blkx_block->checksum.data[i]);
    }

    /* skip reserved */
}

static inline void mirage_file_filter_dmg_blkx_data_fix_endian (blkx_data_t *blkx_data)
{
    blkx_data->block_type = GINT32_FROM_BE(blkx_data->block_type);
    /* skip reserved */

    blkx_data->sector_offset     = GUINT64_FROM_BE(blkx_data->sector_offset);
    blkx_data->sector_count      = GUINT64_FROM_BE(blkx_data->sector_count);
    blkx_data->compressed_offset = GUINT64_FROM_BE(blkx_data->compressed_offset);
    blkx_data->compressed_length = GUINT64_FROM_BE(blkx_data->compressed_length);
}

static inline void mirage_file_filter_dmg_rsrc_header_fix_endian (rsrc_header_t *rsrc_header)
{
    rsrc_header->mish_header_length = GINT32_FROM_BE(rsrc_header->mish_header_length);
    rsrc_header->mish_total_length  = GINT32_FROM_BE(rsrc_header->mish_total_length);
    rsrc_header->mish_blocks_length = GINT32_FROM_BE(rsrc_header->mish_blocks_length);
    rsrc_header->rsrc_total_length = GINT32_FROM_BE(rsrc_header->rsrc_total_length);

    /* skip unknown1 */
    rsrc_header->mark_offset = GUINT16_FROM_BE(rsrc_header->mark_offset);
    rsrc_header->rsrc_length = GUINT16_FROM_BE(rsrc_header->rsrc_length);

    rsrc_header->unknown2         = GUINT16_FROM_BE(rsrc_header->unknown2);
    rsrc_header->last_blkx_rsrc   = GUINT16_FROM_BE(rsrc_header->last_blkx_rsrc);
    rsrc_header->blkx_rsrc_offset = GUINT16_FROM_BE(rsrc_header->blkx_rsrc_offset);
    rsrc_header->last_plst_rsrc   = GUINT16_FROM_BE(rsrc_header->last_plst_rsrc);
    rsrc_header->plst_rsrc_offset = GUINT16_FROM_BE(rsrc_header->plst_rsrc_offset);
}

static inline void mirage_file_filter_dmg_rsrc_block_fix_endian (rsrc_block_t *rsrc_block)
{
    rsrc_block->id             = GINT16_FROM_BE(rsrc_block->id);
    rsrc_block->rel_offs_name  = GINT16_FROM_BE(rsrc_block->rel_offs_name);
    rsrc_block->attrs          = GUINT16_FROM_BE(rsrc_block->attrs);
    rsrc_block->rel_offs_block = GUINT16_FROM_BE(rsrc_block->rel_offs_block);

    /* skip reserved */
}


/**********************************************************************\
 *                         Debug functions                            *
\**********************************************************************/
static void mirage_file_filter_dmg_print_koly_block(MirageFileFilterDmg *self, koly_block_t *koly_block)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: DMG trailer:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  signature: %.4s\n", __debug__, koly_block->signature);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  version: %u\n", __debug__, koly_block->version);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  header_size: %u\n", __debug__, koly_block->header_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  flags: 0x%X\n", __debug__, koly_block->flags);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  image_variant: %u\n", __debug__, koly_block->image_variant);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  running_data_fork_offset: 0x%x\n", __debug__, koly_block->running_data_fork_offset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  data_fork_offset: 0x%x\n", __debug__, koly_block->data_fork_offset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  data_fork_length: %u\n", __debug__, koly_block->data_fork_length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  rsrc_fork_offset: 0x%x\n", __debug__, koly_block->rsrc_fork_offset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  rsrc_fork_length: %u\n", __debug__, koly_block->rsrc_fork_length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  xml_offset: 0x%x\n", __debug__, koly_block->xml_offset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  xml_length: %u\n", __debug__, koly_block->xml_length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  sector_count: %u\n", __debug__, koly_block->sector_count);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  segment_number: %u\n", __debug__, koly_block->segment_number);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  segment_count: %u\n", __debug__, koly_block->segment_count);

    for (gint i = 0; i < 4; i++) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  segment_id[%u]: %u\n", __debug__, i, koly_block->segment_id[i]);
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  data_fork_checksum.type: %u\n", __debug__, koly_block->data_fork_checksum.type);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  data_fork_checksum.size: %u\n", __debug__, koly_block->data_fork_checksum.size);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  data_fork_checksum.data:\n", __debug__);
    for (gint c = 0; c < koly_block->data_fork_checksum.size; c ++) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%08x ", koly_block->data_fork_checksum.data[c]);
        if ((c + 1) % 8 == 0) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
        }
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  master_checksum.type: %u\n", __debug__, koly_block->master_checksum.type);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  master_checksum.size: %u\n", __debug__, koly_block->master_checksum.size);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  master_checksum.data:\n", __debug__);
    for (gint c = 0; c < koly_block->master_checksum.size; c ++) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%08x ", koly_block->master_checksum.data[c]);
        if ((c + 1) % 8 == 0) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
        }
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
}

static void mirage_file_filter_dmg_print_mish_header(MirageFileFilterDmg *self, mish_header_t *mish_header)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: mish header:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  mish_header_length: %u\n", __debug__, mish_header->mish_header_length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  mish_total_length: %u\n", __debug__, mish_header->mish_total_length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  mish_blocks_length: %u\n", __debug__, mish_header->mish_blocks_length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  rsrc_total_length: %u\n", __debug__, mish_header->rsrc_total_length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
}

static void mirage_file_filter_dmg_print_blkx_block(MirageFileFilterDmg *self, blkx_block_t *blkx_block)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: blkx resource:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  signature: %.4s\n", __debug__, blkx_block->signature);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  info_version: %u\n", __debug__, blkx_block->info_version);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  first_sector_number: %u\n", __debug__, blkx_block->first_sector_number);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  sector_count: %u\n", __debug__, blkx_block->sector_count);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  data_start: %u\n", __debug__, blkx_block->data_start);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  decompressed_buffer_requested: %u\n", __debug__, blkx_block->decompressed_buffer_requested);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  blocks_descriptor: %i\n", __debug__, blkx_block->blocks_descriptor);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  checksum.type: %u\n", __debug__, blkx_block->checksum.type);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  checksum.size: %u\n", __debug__, blkx_block->checksum.size);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  checksum.data:\n", __debug__);
    for (gint c = 0; c < blkx_block->checksum.size; c ++) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%08x ", blkx_block->checksum.data[c]);
        if ((c + 1) % 8 == 0) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
        }
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  blocks_run_count: %u\n", __debug__, blkx_block->blocks_run_count);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
}

static void mirage_file_filter_dmg_print_ddm_block(MirageFileFilterDmg *self, driver_descriptor_map_t *ddm_block)
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

static void mirage_file_filter_dmg_print_pme_block(MirageFileFilterDmg *self, part_map_entry_t *pme_block)
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


/**********************************************************************\
 *                     Part filename generation                       *
\**********************************************************************/
static gchar *create_filename_func (const gchar *main_filename, gint index)
{
    gint  main_fn_len = strlen(main_filename) - 7;
    gchar *ret_filename = g_malloc(main_fn_len + 12);

    /* Copy base filename without 'NNN.dmg' */
    memcpy(ret_filename, main_filename, main_fn_len);

    /* Replace three characters with index and append '.dmgpart' */
    gchar *position = ret_filename + main_fn_len;
    g_snprintf(position, 12, "%03i.dmgpart", index + 1);

    return ret_filename;
}


/**********************************************************************\
 *                         Parsing functions                          *
\**********************************************************************/
static gboolean mirage_file_filter_dmg_read_bin_descriptor (MirageFileFilterDmg *self, GInputStream *stream, GError **error)
{
    koly_block_t *koly_block = self->priv->koly_block;

    mish_header_t mish_header;
    rsrc_header_t rsrc_header;

    /* Read mish header */
    g_seekable_seek(G_SEEKABLE(stream), koly_block->rsrc_fork_offset, G_SEEK_SET, NULL, NULL);
    if (g_input_stream_read(stream, &mish_header, sizeof(mish_header_t), NULL, NULL) != sizeof(mish_header_t)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read mish header!");
        return FALSE;
    }

    mirage_file_filter_dmg_mish_header_fix_endian(&mish_header);

    mirage_file_filter_dmg_print_mish_header(self, &mish_header);

    /* Read rsrc header */
    g_seekable_seek(G_SEEKABLE(stream), mish_header.mish_blocks_length, G_SEEK_CUR, NULL, NULL);
    if (g_input_stream_read(stream, &rsrc_header, sizeof(rsrc_header_t), NULL, NULL) != sizeof(rsrc_header_t)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read rsrc header!");
        return FALSE;
    }
    mirage_file_filter_dmg_rsrc_header_fix_endian(&rsrc_header);

    /* Read rsrc blocks */
    self->priv->num_rsrc_blocks = rsrc_header.last_blkx_rsrc + rsrc_header.last_plst_rsrc + 2;
    for (gint j = 0; j < self->priv->num_rsrc_blocks; j++) {
        rsrc_block_t cur_rsrc_block;

        if (g_input_stream_read(stream, &cur_rsrc_block, sizeof(rsrc_block_t), NULL, NULL) != sizeof(rsrc_block_t)) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read rsrc block!");
            return FALSE;
        }
        mirage_file_filter_dmg_rsrc_block_fix_endian(&cur_rsrc_block);

        g_array_append_val(self->priv->rsrc_block, cur_rsrc_block);
    }

    /* Read resource names */
    self->priv->rsrc_name_length = rsrc_header.rsrc_total_length - rsrc_header.rsrc_length;
    self->priv->rsrc_name = g_try_malloc(self->priv->rsrc_name_length);
    if (!self->priv->rsrc_name) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to allocate memory!");
        return FALSE;
    }

    if (g_input_stream_read(stream, self->priv->rsrc_name, self->priv->rsrc_name_length, NULL, NULL) != self->priv->rsrc_name_length) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read rsrc names!");
        return FALSE;
    }

    /* Read resource blocks */
    g_seekable_seek(G_SEEKABLE(stream), koly_block->rsrc_fork_offset + mish_header.mish_header_length,
                    G_SEEK_SET, NULL, NULL);
    for (gint j = 0; j < self->priv->num_rsrc_blocks; j++) {
        rsrc_block_t cur_rsrc_block = g_array_index(self->priv->rsrc_block, rsrc_block_t, j);
        resource_t   resource;

        gchar   *rsrc_name = NULL;
        GString *temp_str  = NULL;

        /* Read resource length */
        if (g_input_stream_read(stream, &resource.length, sizeof(guint32), NULL, NULL) != sizeof(guint32)) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read resource length!");
            return FALSE;
        }
        resource.length = GUINT32_FROM_BE(resource.length);

        /* Set resource type */
        if (cur_rsrc_block.rel_offs_name == -1) {
            resource.type = RT_PLST;
        } else {
            resource.type = RT_BLKX;
        }

        /* Read resource data */
        resource.data = g_try_malloc(resource.length);
        if (!resource.data) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to allocate memory!");
            return FALSE;
        }

        if (g_input_stream_read(stream, resource.data, resource.length, NULL, NULL) != resource.length) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read resource data!");
            return FALSE;
        }

        /* Output resource information */
        if (resource.type == RT_PLST) {
            temp_str  = g_string_new("");
        } else {
            rsrc_name = &self->priv->rsrc_name[cur_rsrc_block.rel_offs_name];
            temp_str  = g_string_new_len (rsrc_name + 1, *rsrc_name);
        }

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: ID: %3i Name: %s\n", __debug__, cur_rsrc_block.id, temp_str->str);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  Attrs: 0x%04hx Offset: 0x%04hx Name offset: 0x%04hx\n", __debug__,
                     cur_rsrc_block.attrs, cur_rsrc_block.rel_offs_block, cur_rsrc_block.rel_offs_name);

        g_string_free(temp_str, TRUE);

        /* Convert endianness */
        if (resource.type == RT_PLST) {
            /* plst resource */
            driver_descriptor_map_t *ddm_block = resource.data;
            part_map_entry_t        *part_map  = (part_map_entry_t *) ((guint8 *) resource.data + sizeof(driver_descriptor_map_t) + 8);
            /* Note: There is 8 unknown bytes appending the DDM */

            mirage_file_filter_dmg_ddm_block_fix_endian(ddm_block);

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: plst resource:\n", __debug__);

            mirage_file_filter_dmg_print_ddm_block(self, ddm_block);

            for (gint p = 0;; p++) {
                mirage_file_filter_dmg_pme_block_fix_endian(&part_map[p]);

                mirage_file_filter_dmg_print_pme_block(self, &part_map[p]);

                if (p + 1 >= part_map[p].map_entries) break;
            }
        } else {
            /* blkx resource */
            blkx_block_t *cur_blkx_block = resource.data;
            blkx_data_t  *cur_blkx_data  = (blkx_data_t *) ((guint8 *) resource.data + sizeof(blkx_block_t));

            mirage_file_filter_dmg_blkx_block_fix_endian(cur_blkx_block);
            for (gint r = 0; r < cur_blkx_block->blocks_run_count; r++) {
                mirage_file_filter_dmg_blkx_data_fix_endian(&cur_blkx_data[r]);

                /* Update parts count */
                gint32 block_type = cur_blkx_data[r].block_type;

                if (block_type == ADC || block_type == ZLIB || block_type == BZLIB ||
                    block_type == ZERO || block_type == RAW || block_type == IGNORE)
                {
                    self->priv->num_parts++;
                }
            }

            mirage_file_filter_dmg_print_blkx_block(self, cur_blkx_block);
        }

        g_array_append_val(self->priv->resource, resource);
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Read %u binary descriptors ...\n\n", __debug__, self->priv->num_rsrc_blocks);

    return TRUE;
}

static gboolean in_key    = FALSE;
static gboolean in_string = FALSE;
static gboolean in_data   = FALSE;
static gboolean in_blkx   = FALSE;
static gboolean in_plst   = FALSE;

static gint nesting_level = 0;

static rsrc_block_t xml_rsrc_block = {0};

static void start_element (GMarkupParseContext *context, const gchar *element_name, const gchar **attribute_names,
                           const gchar **attribute_values, gpointer user_data, GError **error)
{
    MirageFileFilterDmg *self = (MirageFileFilterDmg *) user_data;

    nesting_level++;

    if (!strncmp(element_name, "key", strlen(element_name))) {
        in_key = TRUE;
    } else if (!strncmp(element_name, "string", strlen(element_name))) {
        in_string = TRUE;
    } else if (!strncmp(element_name, "data", strlen(element_name))) {
        in_data = TRUE;
    } else if (!strncmp(element_name, "dict", strlen(element_name))) {
        if (nesting_level == 5) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Resource start.\n", __debug__);
            memset(&xml_rsrc_block, 0, sizeof(rsrc_block_t));
        }
    }

    /*MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Start element: %s\n", __debug__, element_name);*/
}

static void end_element (GMarkupParseContext *context, const gchar *element_name, gpointer user_data, GError **error)
{
    MirageFileFilterDmg *self = (MirageFileFilterDmg *) user_data;

    if (!strncmp(element_name, "key", strlen(element_name))) {
        in_key = FALSE;
    } else if (!strncmp(element_name, "string", strlen(element_name))) {
        in_string = FALSE;
    } else if (!strncmp(element_name, "data", strlen(element_name))) {
        in_data = FALSE;
    } else if (!strncmp(element_name, "dict", strlen(element_name))) {
        if (nesting_level == 5) {
            gchar   *rsrc_name = &self->priv->rsrc_name[xml_rsrc_block.rel_offs_name];
            GString *temp_str  = g_string_new_len(rsrc_name + 1, (gchar) *rsrc_name);

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Resource end.\n", __debug__);

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: ID: %3i Name: %s\n", __debug__, xml_rsrc_block.id, temp_str->str);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  Attrs: 0x%04hx Offset: 0x%04hx Name offset: 0x%04hx\n", __debug__,
                         xml_rsrc_block.attrs, xml_rsrc_block.rel_offs_block, xml_rsrc_block.rel_offs_name);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");

            g_string_free(temp_str, TRUE);

            g_array_append_val(self->priv->rsrc_block, xml_rsrc_block);
        }
    }

    nesting_level--;

    /*MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: End element: %s\n", __debug__, element_name);*/
}

static void xml_text (GMarkupParseContext *context, const gchar *text, gsize text_len, gpointer user_data, GError **error)
{
    MirageFileFilterDmg *self = (MirageFileFilterDmg *) user_data;

    GString *text_str = g_string_new_len(text, text_len);

    static gchar last_key[1024] = {0};

    if (!text_str) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to allocate memory!");
        return;
    }

    if (in_key) {
        if (!strncmp(text_str->str, "blkx", text_str->len)) {
            in_blkx = TRUE;
            in_plst = FALSE;
        } else if (!strncmp(text_str->str, "plst", text_str->len)) {
            in_blkx = FALSE;
            in_plst = TRUE;
        }

        /*MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Key: %s nesting level: %u\n", __debug__, text_str->str, nesting_level);*/
        g_strlcpy(last_key, text_str->str, sizeof(last_key));
    }

    if (in_string) {
        if (nesting_level == 6) {
            gint res;

            /*MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  %s: %s\n", __debug__, last_key, text_str->str);*/
            if (!strncmp(last_key, "ID", strlen(last_key))) {
                res = sscanf(text_str->str, "%hi", &xml_rsrc_block.id);
                if (res < 1) {
                    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to convert string to integer!");
                    return;
                }
            } else if (!strncmp(last_key, "Attributes", strlen(last_key))) {
                res = sscanf(text_str->str, "%hx", &xml_rsrc_block.attrs);
                if (res < 1) {
                    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to convert string to integer!");
                    return;
                }
            } else if (!strncmp(last_key, "Name", strlen(last_key))) {
                if (in_blkx) {
                    gint prev_length = self->priv->rsrc_name_length;
                    gint new_length = prev_length + text_str->len + 1;

                    self->priv->rsrc_name = g_try_realloc(self->priv->rsrc_name, new_length);
                    if (!self->priv->rsrc_name) {
                        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to allocate memory!");
                        return;
                    }
                    self->priv->rsrc_name_length = new_length;
                    self->priv->rsrc_name[prev_length] = (gchar) text_str->len;
                    memcpy(&self->priv->rsrc_name[prev_length + 1], text_str->str, text_str->len);
                    xml_rsrc_block.rel_offs_name = prev_length;
                } else {
                    xml_rsrc_block.rel_offs_name = -1;
                }
            } else if (!strncmp(last_key, "CFName", strlen(last_key))) {
                /* Duplicate of Name, so ignore it */
            } else {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  Unrecognized key: %s value: %s\n", __debug__, last_key, text_str->str);
            }
        }
    }

    if (in_data && (in_blkx || in_plst)) {
        GString *dest_str = g_string_sized_new(text_str->len);

        blkx_block_t *cur_blkx_block = NULL;
        blkx_data_t  *cur_blkx_data = NULL;

        driver_descriptor_map_t *ddm_block;
        part_map_entry_t        *part_map;

        resource_t resource;

        if (!dest_str) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to allocate memory!");
            return;
        }

        /* Strip data string */
        for (gchar *source_pos = text_str->str; *source_pos; source_pos++) {
            switch (*source_pos) {
                case 0x0a:
                case 0x0d:
                case 0x09:
                case 0x20:
                    /* Discard CR, LF, TAB and whitespace */
                    break;
                default:
                    /* Save everything else */
                    g_string_append_c(dest_str, *source_pos);
            }
        }

        /* Decode Base-64 string to resource data */
        g_base64_decode_inplace (dest_str->str, &dest_str->len);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Raw length: %zu Decoded length: %zu\n", __debug__,
                     text_str->len, dest_str->len);

        /* Update rsrc_block with relative offset */
        gint16 rel_offs_block = 0;

        for (gint r = 0; r < self->priv->num_rsrc_blocks; r++) {
            resource_t cur_res = g_array_index(self->priv->resource, resource_t, r);

            rel_offs_block += cur_res.length;
        }

        xml_rsrc_block.rel_offs_block = rel_offs_block;

        if (in_blkx) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Found blkx resource!\n", __debug__);

            /* Locate blkx block + blkx data and fix endianness */
            cur_blkx_block = (blkx_block_t *) dest_str->str;
            if (memcmp(cur_blkx_block->signature, mish_signature, 4)) {
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Bad signature in blkx block!");
                return;
            }
            mirage_file_filter_dmg_blkx_block_fix_endian(cur_blkx_block);

            mirage_file_filter_dmg_print_blkx_block(self, cur_blkx_block);

            cur_blkx_data = (blkx_data_t *) ((guint8 *) dest_str->str + sizeof(blkx_block_t));
            for (gint j = 0; j < cur_blkx_block->blocks_run_count; j++) {
                mirage_file_filter_dmg_blkx_data_fix_endian(&cur_blkx_data[j]);

                /* Update parts count */
                gint32 block_type = cur_blkx_data[j].block_type;

                if (block_type == ADC || block_type == ZLIB || block_type == BZLIB ||
                    block_type == ZERO || block_type == RAW || block_type == IGNORE)
                {
                    self->priv->num_parts++;
                }
            }

            /* Append resource */
            resource.data   = g_memdup (dest_str->str, dest_str->len + 1);
            resource.length = dest_str->len;
            resource.type   = RT_BLKX;

            g_array_append_val(self->priv->resource, resource);

            /* Update counts */
            self->priv->num_rsrc_blocks++;
        } else if (in_plst) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Found plst resource!\n", __debug__);

            /* Locate blocks and fix endianness */
            ddm_block = (driver_descriptor_map_t *) dest_str->str;
            part_map  = (part_map_entry_t *) ((guint8 *) dest_str->str + sizeof(driver_descriptor_map_t) + 8);
            /* Note: There is 8 unknown bytes appending the DDM */

            mirage_file_filter_dmg_ddm_block_fix_endian(ddm_block);

            mirage_file_filter_dmg_print_ddm_block(self, ddm_block);

            for (gint p = 0;; p++) {
                mirage_file_filter_dmg_pme_block_fix_endian(&part_map[p]);

                mirage_file_filter_dmg_print_pme_block(self, &part_map[p]);

                if (p + 1 >= part_map[p].map_entries) break;
            }

            /* Append resource */
            resource.data   = g_memdup (dest_str->str, dest_str->len + 1);
            resource.length = dest_str->len;
            resource.type   = RT_PLST;

            g_array_append_val(self->priv->resource, resource);

            /* Update counts */
            self->priv->num_rsrc_blocks++;
        }

        g_string_free(dest_str, TRUE);
    }

    /*if (text_len < 1024) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Text: %s\n", __debug__, text_str->str);
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Text: (text string too long to list...)\n", __debug__);
    }*/

    g_string_free(text_str, TRUE);
}

static const GMarkupParser DMG_XMLParser = {
    start_element,
    end_element,
    xml_text,
    NULL,
    NULL
};

static gboolean mirage_file_filter_dmg_read_xml_descriptor (MirageFileFilterDmg *self, GInputStream *stream, GError **error)
{
    koly_block_t *koly_block = self->priv->koly_block;

    GMarkupParseContext *context = g_markup_parse_context_new (&DMG_XMLParser, 0, self, NULL);

    gchar *plist = g_try_malloc(koly_block->xml_length + 1);

    if (!plist || !context) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to allocate memory!");
        return FALSE;
    }

    /* Read xml descriptor */
    g_seekable_seek(G_SEEKABLE(stream), koly_block->xml_offset, G_SEEK_SET, NULL, NULL);
    if (g_input_stream_read(stream, plist, koly_block->xml_length, NULL, NULL) != koly_block->xml_length) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read XML descriptor!");
        return FALSE;
    }

    /* Terminate xml descriptor buffer */
    plist[koly_block->xml_length] = '\0';

    /* Parse the properties list */
    g_markup_parse_context_parse (context, plist, koly_block->xml_length + 1, NULL);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n%s: Read %u XML descriptors ...\n\n", __debug__, self->priv->num_rsrc_blocks);
    g_free(plist);
    g_markup_parse_context_free(context);

    return TRUE;
}

static gboolean mirage_file_filter_dmg_read_index (MirageFileFilterDmg *self, GError **error)
{
    z_stream  *zlib_stream  = &self->priv->zlib_stream;
    bz_stream *bzip2_stream = &self->priv->bzip2_stream;

    koly_block_t *koly_block = self->priv->koly_block;

    gint cur_part = 0;
    gint ret;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: generating part index\n", __debug__);

    /* Allocate part index */
    self->priv->parts = g_try_new(DMG_Part, self->priv->num_parts);
    if (!self->priv->parts) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to allocate memory for index!");
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of parts: %d\n", __debug__, self->priv->num_parts);

    /* Loop through resources */
    for (gint res = 0; res < self->priv->num_rsrc_blocks; res++) {
        rsrc_block_t cur_rsrc_block = g_array_index (self->priv->rsrc_block, rsrc_block_t, res);
        resource_t   resource       = g_array_index (self->priv->resource, resource_t, res);

        /* Is this resource not a blkx block? */
        if (resource.type != RT_BLKX) {
            continue;
        }

        blkx_block_t *cur_blkx_block = (blkx_block_t *) resource.data;
        blkx_data_t  *cur_blkx_data  = (blkx_data_t *) ((guint8 *) resource.data + sizeof(blkx_block_t));

        gchar   *rsrc_name = &self->priv->rsrc_name[cur_rsrc_block.rel_offs_name];
        GString *temp_str  = g_string_new_len(rsrc_name + 1, *rsrc_name);

        /*MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Resource %3i: %s\n", __debug__, cur_rsrc_block.id, temp_str->str);*/

        g_string_free(temp_str, TRUE);

        /* Loop through blocks */
        for (gint n = 0; n < cur_blkx_block->blocks_run_count; n++) {
            DMG_Part temp_part;

            temp_part.type = cur_blkx_data[n].block_type;
            temp_part.first_sector = cur_blkx_block->first_sector_number + cur_blkx_data[n].sector_offset;
            temp_part.num_sectors = cur_blkx_data[n].sector_count;
            temp_part.in_offset = koly_block->data_fork_offset + cur_blkx_block->data_start + cur_blkx_data[n].compressed_offset;
            temp_part.in_length = cur_blkx_data[n].compressed_length;

            /* Find segment belonging to part */
            temp_part.segment = -1;
            for (gint s = 0; s < self->priv->num_koly_blocks; s++) {
                if (temp_part.in_offset >= koly_block[s].running_data_fork_offset) {
                    temp_part.segment = s;
                } else {
                    break;
                }
            }

            /* Does this block have data? If so then append it. */
            if (temp_part.type == ADC || temp_part.type == ZLIB || temp_part.type == BZLIB ||
                temp_part.type == ZERO || temp_part.type == RAW || temp_part.type == IGNORE)
            {
                self->priv->parts[cur_part] = temp_part;
                cur_part++;

                /* Update buffer sizes */
                if (self->priv->inflate_buffer_size < temp_part.num_sectors * DMG_SECTOR_SIZE) {
                    self->priv->inflate_buffer_size = temp_part.num_sectors * DMG_SECTOR_SIZE;
                }

                if (temp_part.type == ADC || temp_part.type == ZLIB || temp_part.type == BZLIB)
                {
                    if (self->priv->io_buffer_size < temp_part.in_length) {
                        self->priv->io_buffer_size = temp_part.in_length;
                    }
                }
            }

            /*MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%u %u %u %u %d ",
                         temp_part.first_sector, temp_part.num_sectors,
                         temp_part.in_offset, temp_part.in_length, temp_part.segment);
            switch (cur_blkx_data[n].block_type) {
                case ADC:
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "ADC ");
                    break;
                case ZLIB:
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "ZLIB ");
                    break;
                case BZLIB:
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "BZLIB ");
                    break;
                case ZERO:
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "ZERO ");
                    break;
                case RAW:
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "RAW ");
                    break;
                case IGNORE:
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "IGNORE ");
                    break;
                case COMMENT:
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "COMMENT ");
                    break;
                case TERM:
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "TERM ");
                    break;
                default:
                    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Unknown block type: 0x%x (%i)!",
                                cur_blkx_data[n].block_type, cur_blkx_data[n].block_type);
                    return FALSE;
            }*/
        }
        /*MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");*/
    }
    /*MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");*/

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: IO buffer size: %u\n", __debug__, self->priv->io_buffer_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Inflate buffer size: %u\n", __debug__, self->priv->inflate_buffer_size);

    /* Initialize zlib stream */
    zlib_stream->zalloc = Z_NULL;
    zlib_stream->zfree = Z_NULL;
    zlib_stream->opaque = Z_NULL;
    zlib_stream->avail_in = 0;
    zlib_stream->next_in = Z_NULL;

    ret = inflateInit2(zlib_stream, 15);

    if (ret != Z_OK) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to initialize zlib's inflate (error: %d)!", ret);
        return FALSE;
    }

    /* Initialize bzip2 stream */
    bzip2_stream->bzalloc = NULL;
    bzip2_stream->bzfree = NULL;
    bzip2_stream->opaque = NULL;
    bzip2_stream->avail_in = 0;
    bzip2_stream->next_in = NULL;

    ret = BZ2_bzDecompressInit(bzip2_stream, 0, 0);

    if (ret != BZ_OK) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to initialize libbz2's decompress (error: %d)!", ret);
        return FALSE;
    }

    /* Allocate inflate buffer */
    self->priv->inflate_buffer = g_try_malloc(self->priv->inflate_buffer_size);
    if (!self->priv->inflate_buffer) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to allocate memory for inflate buffer!");
        return FALSE;
    }

    /* Allocate I/O buffer */
    if (self->priv->io_buffer_size) {
        self->priv->io_buffer = g_try_malloc(self->priv->io_buffer_size);
        if (!self->priv->io_buffer) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to allocate memory for I/O buffer!");
            return FALSE;
        }
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: successfully generated index\n\n", __debug__);

    return TRUE;
}


/**********************************************************************\
 *                MirageFileFilter methods implementation             *
\**********************************************************************/
static gboolean mirage_file_filter_dmg_open_streams (MirageFileFilterDmg *self, GError **error)
{
    GInputStream **streams;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: opening streams\n", __debug__);

    /* Allocate space for streams */
    self->priv->streams = streams = g_try_new(GInputStream *, self->priv->koly_block->segment_count);
    if (!streams) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to allocate memory for streams!");
        return FALSE;
    }

    /* Fill in existing stream */
    streams[0] = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));
    g_object_ref(streams[0]);
    self->priv->num_streams++;

    const gchar *original_filename = mirage_contextual_get_file_stream_filename (MIRAGE_CONTEXTUAL(self), streams[0]);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  %s\n", __debug__, original_filename);

    /* Create the rest of the streams */
    for (gint s = 1; s < self->priv->koly_block->segment_count; s++) {
        gchar *filename = create_filename_func(original_filename, s);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  %s\n", __debug__, filename);
        streams[s] = mirage_contextual_create_file_stream (MIRAGE_CONTEXTUAL(self), filename, error);
        if (!streams[s]) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to create stream!");
            return FALSE;
        }
        g_free(filename);
        self->priv->num_streams++;
    }

    /* Allocated space for additional koly blocks */
    self->priv->num_koly_blocks = self->priv->koly_block->segment_count;
    self->priv->koly_block = g_try_renew(koly_block_t, self->priv->koly_block, self->priv->num_koly_blocks);
    if (!self->priv->koly_block) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to reallocate memory for koly blocks!");
        return FALSE;
    }

    /* Read the rest of the koly blocks */
    for (gint s = 1; s < self->priv->num_koly_blocks; s++) {
        g_seekable_seek(G_SEEKABLE(streams[s]), -sizeof(koly_block_t), G_SEEK_END, NULL, NULL);
        if (g_input_stream_read(streams[s], &self->priv->koly_block[s], sizeof(koly_block_t), NULL, NULL) != sizeof(koly_block_t)) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read trailer!");
            return FALSE;
        }

        mirage_file_filter_dmg_koly_block_fix_endian(&self->priv->koly_block[s]);

        /* Validate koly block */
        if (memcmp(self->priv->koly_block[s].signature, koly_signature, sizeof(koly_signature))) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "File filter cannot handle given image!");
            return FALSE;
        }

        /* Output koly block info */
        mirage_file_filter_dmg_print_koly_block(self, &self->priv->koly_block[s]);
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: sucessfully opened streams\n\n", __debug__);

    return TRUE;
}

static gboolean mirage_file_filter_dmg_can_handle_data_format (MirageFileFilter *_self, GError **error)
{
    MirageFileFilterDmg *self = MIRAGE_FILE_FILTER_DMG(_self);
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));

    koly_block_t *koly_block = NULL;

    gboolean succeeded = TRUE;
    gint     ret;

    /* Allocate koly block*/
    self->priv->num_koly_blocks = 1;
    self->priv->koly_block = koly_block = g_try_new(koly_block_t, self->priv->num_koly_blocks);
    if (!koly_block) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to allocate memory!");
        return FALSE;
    }

    for (gint try = 0; try < 2; try++) {
        /* Find koly block either on end (most often) or beginning of file */
        if (try == 0) {
            g_seekable_seek(G_SEEKABLE(stream), -sizeof(koly_block_t), G_SEEK_END, NULL, NULL);
        } else {
            g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);
        }

        /* Read koly block */
        if (g_input_stream_read(stream, koly_block, sizeof(koly_block_t), NULL, NULL) != sizeof(koly_block_t)) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read trailer!");
            return FALSE;
        }

        mirage_file_filter_dmg_koly_block_fix_endian(koly_block);

        /* Validate koly block */
        if (memcmp(koly_block->signature, koly_signature, sizeof(koly_signature))) {
            if (try == 1) {
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "File filter cannot handle given image!");
                return FALSE;
            }
        } else {
            /* Found signature */
            break;
        }
    }

    /* Only perform parsing on the first file in a set */
    if (koly_block->segment_number != 1) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "File is a continuating part of a set, aborting!");
        return FALSE;
    }

    /* Output koly block info */
    mirage_file_filter_dmg_print_koly_block(self, koly_block);

    /* Open streams */
    ret = mirage_file_filter_dmg_open_streams (self, error);
    if (!ret) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Failed to open streams!");
        return FALSE;
    }
    /* This have been re-allocated, so update local pointer */
    koly_block = self->priv->koly_block;

    /* Set file size */
    mirage_file_filter_set_file_size(MIRAGE_FILE_FILTER(self), koly_block->sector_count * DMG_SECTOR_SIZE);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: original stream size: %lu\n",
                 __debug__, koly_block->sector_count * DMG_SECTOR_SIZE);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing the underlying stream data...\n", __debug__);

    /* Read descriptors, either the XML or binary one */
    if (koly_block->xml_offset && koly_block->xml_length) {
        succeeded = mirage_file_filter_dmg_read_xml_descriptor(self, stream, error);
        if (!succeeded) goto end;
    } else if (koly_block->rsrc_fork_offset && koly_block->rsrc_fork_length) {
        succeeded = mirage_file_filter_dmg_read_bin_descriptor(self, stream, error);
        if (!succeeded) goto end;
    } else {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Image lacks either an XML or a binary descriptor!");
        return FALSE;
    }

    succeeded = mirage_file_filter_dmg_read_index (self, error);

end:
    /* Return result */
    if (succeeded) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing completed successfully\n\n", __debug__);
        return TRUE;
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing failed!\n\n", __debug__);
        return FALSE;
    }
}

static gssize mirage_file_filter_dmg_read_raw_chunk (MirageFileFilterDmg *self, guint8 *buffer, gint chunk_num)
{
    const DMG_Part *part = &self->priv->parts[chunk_num];
    GInputStream   *stream = self->priv->streams[part->segment];
    koly_block_t   *koly_block = &self->priv->koly_block[part->segment];

    gsize   to_read = part->in_length;
    gsize   have_read = 0;
    goffset part_offs = koly_block->data_fork_offset + part->in_offset - koly_block->running_data_fork_offset;
    gsize   part_avail = koly_block->running_data_fork_offset + koly_block->data_fork_length - part->in_offset;
    gint    ret;

    /* Seek to the position */
    if (!g_seekable_seek(G_SEEKABLE(stream), part_offs, G_SEEK_SET, NULL, NULL)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to %ld in underlying stream!\n", __debug__, part_offs);
        return -1;
    }

    /* Read raw chunk data */
    ret = g_input_stream_read(stream, &buffer[have_read], MIN(to_read, part_avail), NULL, NULL);
    if (ret < 0) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read %d bytes from underlying stream!\n", __debug__, to_read);
        return -1;
    } else if (ret == 0) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unexpectedly reached EOF!\n", __debug__);
        return -1;
    } else if (ret == to_read) {
        have_read += ret;
        to_read -= ret;
    } else if (ret < to_read) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading remaining data!\n", __debug__);
        have_read += ret;
        to_read -= ret;

        koly_block = &self->priv->koly_block[part->segment + 1];
        stream = self->priv->streams[part->segment + 1];
        part_offs = koly_block->data_fork_offset;

        /* Seek to the position */
        if (!g_seekable_seek(G_SEEKABLE(stream), part_offs, G_SEEK_SET, NULL, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to %ld in underlying stream!\n", __debug__, part_offs);
            return -1;
        }

        /* Read raw chunk data */
        ret = g_input_stream_read(stream, &buffer[have_read], to_read, NULL, NULL);
        if (ret < 0) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read %d bytes from underlying stream!\n", __debug__, to_read);
            return -1;
        } else if (ret == 0) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unexpectedly reached EOF!\n", __debug__);
            return -1;
        } else if (ret == to_read) {
            have_read += ret;
            to_read -= ret;
        }
    }

    g_assert(to_read == 0 && have_read == part->in_length);

    return have_read;
}

static gssize mirage_file_filter_dmg_partial_read (MirageFileFilter *_self, void *buffer, gsize count)
{
    MirageFileFilterDmg *self = MIRAGE_FILE_FILTER_DMG(_self);
    goffset position = mirage_file_filter_get_position(MIRAGE_FILE_FILTER(self));
    gint part_idx = -1;

    /* Find part that corresponds to current position */
    for (gint p = 0; p < self->priv->num_parts; p++) {
        DMG_Part *cur_part = &self->priv->parts[p];
        gint req_sector = position / DMG_SECTOR_SIZE;

        if ((cur_part->first_sector <= req_sector) && (cur_part->first_sector + cur_part->num_sectors >= req_sector)) {
            part_idx = p;
        }
    }

    if (part_idx == -1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: failed to find part!\n", __debug__);
        return 0;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: stream position: %ld (0x%lX) -> part #%d (cached: #%d)\n", __debug__, position, position, part_idx, self->priv->cached_part);

    /* If we do not have part in cache, uncompress it */
    if (part_idx != self->priv->cached_part) {
        const DMG_Part *part = &self->priv->parts[part_idx];
        z_stream *zlib_stream = &self->priv->zlib_stream;
        bz_stream *bzip2_stream = &self->priv->bzip2_stream;
        gint ret;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: part not cached, reading...\n", __debug__);

        /* Read a part */
        if (part->type == ZERO || part->type == IGNORE) {
            /* Return a zero-filled buffer */
            /* FIXME: Some zero parts can be huge so avoid allocating them */
            memset (self->priv->inflate_buffer, 0, part->num_sectors * DMG_SECTOR_SIZE);
        } else if (part->type == RAW) {
            /* Read uncompressed part */
            ret = mirage_file_filter_dmg_read_raw_chunk (self, self->priv->inflate_buffer, part_idx);
            if (ret != part->in_length) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read raw chunk!\n", __debug__);
                return -1;
            }
        } else if (part->type == ZLIB) {
            /* Reset inflate engine */
            ret = inflateReset2(zlib_stream, 15);
            if (ret != Z_OK) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to reset inflate engine!\n", __debug__);
                return -1;
            }

            /* Uncompress whole part */
            zlib_stream->avail_in  = part->in_length;
            zlib_stream->next_in   = self->priv->io_buffer;
            zlib_stream->avail_out = self->priv->inflate_buffer_size;
            zlib_stream->next_out  = self->priv->inflate_buffer;

            /* Read some compressed data */
            ret = mirage_file_filter_dmg_read_raw_chunk (self, self->priv->io_buffer, part_idx);
            if (ret != part->in_length) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read raw chunk!\n", __debug__);
                return -1;
            }

            do {
                /* Inflate */
                ret = inflate(zlib_stream, Z_NO_FLUSH);
                if (ret == Z_NEED_DICT || ret == Z_MEM_ERROR || ret == Z_DATA_ERROR) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to inflate part: %s!\n", __debug__, zlib_stream->msg);
                    return -1;
                }
            } while (zlib_stream->avail_in);
        } else if (part->type == BZLIB) {
            /* Reset decompress engine */
            ret = BZ2_bzDecompressInit(bzip2_stream, 0, 0);
            if (ret != BZ_OK) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to initialize decompress engine!\n", __debug__);
                return -1;
            }

            /* Uncompress whole part */
            bzip2_stream->avail_in  = part->in_length;
            bzip2_stream->next_in   = (gchar *) self->priv->io_buffer;
            bzip2_stream->avail_out = self->priv->inflate_buffer_size;
            bzip2_stream->next_out  = (gchar *) self->priv->inflate_buffer;

            /* Read some compressed data */
            ret = mirage_file_filter_dmg_read_raw_chunk (self, self->priv->io_buffer, part_idx);
            if (ret != part->in_length) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read raw chunk!\n", __debug__);
                return -1;
            }

            do {
                /* Inflate */
                ret = BZ2_bzDecompress(bzip2_stream);
                if (ret < 0) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to inflate part: %d!\n", __debug__, ret);
                    return -1;
                }
            } while (bzip2_stream->avail_in);

            /* Uninitialize decompress engine */
            ret = BZ2_bzDecompressEnd(bzip2_stream);
            if (ret != BZ_OK) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to uninitialize decompress engine!\n", __debug__);
                return -1;
            }
        } else if (part->type == ADC) {
            /* FIXME: ADC decompression needs testing */
            gsize written_bytes;

            /* Read some compressed data */
            ret = mirage_file_filter_dmg_read_raw_chunk (self, self->priv->io_buffer, part_idx);
            if (ret != part->in_length) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read raw chunk!\n", __debug__);
                return -1;
            }

            /* Inflate */
            ret = (gint) adc_decompress(part->in_length, self->priv->io_buffer, part->num_sectors * DMG_SECTOR_SIZE,
                           self->priv->inflate_buffer, &written_bytes);

            g_assert (ret == part->in_length);
            g_assert (written_bytes == part->num_sectors * DMG_SECTOR_SIZE);
        } else {
            /* We should never get here... */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Encountered unknown chunk type %u!\n", __debug__, part->type);
            return -1;
        }

        /* Set currently cached part */
        self->priv->cached_part = part_idx;
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: part already cached\n", __debug__);
    }

    /* Copy data */
    const DMG_Part *part = &self->priv->parts[part_idx];

    gsize   part_size = part->num_sectors * DMG_SECTOR_SIZE;
    goffset part_offset = position - (part->first_sector * DMG_SECTOR_SIZE);
    count = MIN(count, part_size - part_offset);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: offset within part: %ld, copying %d bytes\n", __debug__, part_offset, count);

    memcpy(buffer, &self->priv->inflate_buffer[part_offset], count);

    return count;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE(MirageFileFilterDmg, mirage_file_filter_dmg, MIRAGE_TYPE_FILE_FILTER);

void mirage_file_filter_dmg_type_register (GTypeModule *type_module)
{
    return mirage_file_filter_dmg_register_type(type_module);
}


static void mirage_file_filter_dmg_init (MirageFileFilterDmg *self)
{
    self->priv = MIRAGE_FILE_FILTER_DMG_GET_PRIVATE(self);

    mirage_file_filter_generate_info(MIRAGE_FILE_FILTER(self),
        "FILTER-DMG",
        "DMG File Filter",
        1,
        "Apple Disk Image (*.dmg)", "application/x-apple-diskimage"
    );

    self->priv->koly_block = NULL;

    self->priv->streams = NULL;

    self->priv->rsrc_block = g_array_new(FALSE, FALSE, sizeof(rsrc_block_t));
    self->priv->rsrc_name  = NULL;

    self->priv->resource = g_array_new(FALSE, FALSE, sizeof(resource_t));

    self->priv->num_koly_blocks = 0;
    self->priv->num_streams = 0;
    self->priv->num_rsrc_blocks = 0;
    self->priv->rsrc_name_length = 0;

    self->priv->num_parts = 0;
    self->priv->parts = NULL;

    self->priv->cached_part = -1;
    self->priv->inflate_buffer = NULL;
    self->priv->io_buffer = NULL;
}

static void mirage_file_filter_dmg_finalize (GObject *gobject)
{
    MirageFileFilterDmg *self = MIRAGE_FILE_FILTER_DMG(gobject);

    for (gint s = 0; s < self->priv->num_streams; s++) {
        g_object_unref(self->priv->streams[s]);
    }
    g_free(self->priv->streams);

    g_array_free(self->priv->rsrc_block, TRUE);
    g_free(self->priv->rsrc_name);

    g_array_free(self->priv->resource, TRUE);

    g_free(self->priv->parts);
    g_free(self->priv->inflate_buffer);
    g_free(self->priv->io_buffer);

    inflateEnd(&self->priv->zlib_stream);
    BZ2_bzDecompressEnd(&self->priv->bzip2_stream);

    g_free(self->priv->koly_block);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_file_filter_dmg_parent_class)->finalize(gobject);
}

static void mirage_file_filter_dmg_class_init (MirageFileFilterDmgClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageFileFilterClass *file_filter_class = MIRAGE_FILE_FILTER_CLASS(klass);

    gobject_class->finalize = mirage_file_filter_dmg_finalize;

    file_filter_class->can_handle_data_format = mirage_file_filter_dmg_can_handle_data_format;

    file_filter_class->partial_read = mirage_file_filter_dmg_partial_read;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageFileFilterDmgPrivate));
}

static void mirage_file_filter_dmg_class_finalize (MirageFileFilterDmgClass *klass G_GNUC_UNUSED)
{
}
