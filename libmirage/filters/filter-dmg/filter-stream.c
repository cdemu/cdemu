/*
 *  libMirage: DMG filter: filter stream
 *  Copyright (C) 2012-2014 Henrik Stokseth
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

#include "filter-dmg.h"

#define __debug__ "DMG-FilterStream"


static const guint8 koly_signature[4] = { 'k', 'o', 'l', 'y' };
/*static const guint8 mish_signature[4] = { 'm', 'i', 's', 'h' };*/

typedef struct {
    DMG_block_type type;

    guint64  first_sector;
    guint64  num_sectors;
    gint     segment;
    goffset  in_offset;
    gsize    in_length;
} DMG_Part;


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_FILTER_STREAM_DMG_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FILTER_STREAM_DMG, MirageFilterStreamDmgPrivate))

struct _MirageFilterStreamDmgPrivate
{
    /* koly blocks */
    koly_block_t *koly_block;
    guint num_koly_blocks;

    /* resource fork */
    rsrc_fork_t *rsrc_fork;

    /* streams */
    MirageStream **streams;
    guint num_streams;

    /* Part list */
    DMG_Part *parts;
    gint num_parts;

    /* Inflate buffer */
    guint8 *inflate_buffer;
    guint inflate_buffer_size;
    gint cached_part;

    /* I/O buffer */
    guint8 *io_buffer;
    guint io_buffer_size;

    /* Compression streams */
    z_stream  zlib_stream;
    bz_stream bzip2_stream;
};


/**********************************************************************\
 *                    Endian-conversion functions                     *
\**********************************************************************/
static inline void mirage_filter_stream_dmg_koly_block_fix_endian (koly_block_t *koly_block)
{
    g_assert(koly_block);

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

    /* Note: It seems segment_id should not be endian converted */

    koly_block->data_fork_checksum.type = GUINT32_FROM_BE(koly_block->data_fork_checksum.type);
    koly_block->data_fork_checksum.size = GUINT32_FROM_BE(koly_block->data_fork_checksum.size);
    koly_block->master_checksum.type    = GUINT32_FROM_BE(koly_block->master_checksum.type);
    koly_block->master_checksum.size    = GUINT32_FROM_BE(koly_block->master_checksum.size);

    for (guint i = 0; i < 32; i++) {
        koly_block->master_checksum.data[i] = GUINT32_FROM_BE(koly_block->master_checksum.data[i]);
    }

    for (guint i = 0; i < 32; i++) {
        koly_block->data_fork_checksum.data[i] = GUINT32_FROM_BE(koly_block->data_fork_checksum.data[i]);
    }

    /* skip reserved1 and reserved2 */
}


static inline void mirage_filter_stream_dmg_blkx_block_fix_endian (blkx_block_t *blkx_block)
{
    g_assert(blkx_block);

    blkx_block->info_version = GUINT32_FROM_BE(blkx_block->info_version);

    blkx_block->first_sector_number = GUINT64_FROM_BE(blkx_block->first_sector_number);
    blkx_block->sector_count        = GUINT64_FROM_BE(blkx_block->sector_count);
    blkx_block->data_start          = GUINT64_FROM_BE(blkx_block->data_start);

    blkx_block->decompressed_buffer_requested = GUINT32_FROM_BE(blkx_block->decompressed_buffer_requested);

    blkx_block->blocks_descriptor = GINT32_FROM_BE(blkx_block->blocks_descriptor);
    blkx_block->blocks_run_count  = GUINT32_FROM_BE(blkx_block->blocks_run_count);
    blkx_block->checksum.type     = GUINT32_FROM_BE(blkx_block->checksum.type);
    blkx_block->checksum.size     = GUINT32_FROM_BE(blkx_block->checksum.size);

    for (guint i = 0; i < 32; i++) {
        blkx_block->checksum.data[i] = GUINT32_FROM_BE(blkx_block->checksum.data[i]);
    }

    /* skip reserved */
}

static inline void mirage_filter_stream_dmg_blkx_data_fix_endian (blkx_data_t *blkx_data)
{
    g_assert(blkx_data);

    blkx_data->block_type = GINT32_FROM_BE(blkx_data->block_type);

    blkx_data->sector_offset     = GUINT64_FROM_BE(blkx_data->sector_offset);
    blkx_data->sector_count      = GUINT64_FROM_BE(blkx_data->sector_count);
    blkx_data->compressed_offset = GUINT64_FROM_BE(blkx_data->compressed_offset);
    blkx_data->compressed_length = GUINT64_FROM_BE(blkx_data->compressed_length);
}

static inline void mirage_filter_stream_dmg_csum_fix_endian (csum_block_t *csum_block)
{
    g_assert(csum_block);

    csum_block->version = GUINT16_FROM_LE(csum_block->version);
    csum_block->type    = GUINT16_FROM_LE(csum_block->type);
    csum_block->data    = GUINT32_FROM_LE(csum_block->data);
}

static inline void mirage_filter_stream_dmg_size_fix_endian (size_block_t *size_block)
{
    g_assert(size_block);

    size_block->version        = GUINT16_FROM_LE(size_block->version);
    size_block->is_hfs         = GUINT32_FROM_LE(size_block->is_hfs);
    size_block->unknown1       = GUINT32_FROM_LE(size_block->unknown1);
    size_block->unknown2       = GUINT32_FROM_LE(size_block->unknown2);
    size_block->unknown3       = GUINT32_FROM_LE(size_block->unknown3);
    size_block->vol_modified   = GUINT32_FROM_LE(size_block->vol_modified);
    size_block->unknown4       = GUINT32_FROM_LE(size_block->unknown4);
    size_block->vol_sig.as_int = GUINT16_FROM_BE(size_block->vol_sig.as_int);
    size_block->size_present   = GUINT16_FROM_LE(size_block->size_present);
}


/**********************************************************************\
 *                         Debug functions                            *
\**********************************************************************/
static void mirage_filter_stream_dmg_print_koly_block(MirageFilterStreamDmg *self, koly_block_t *koly_block)
{
    g_assert(self && koly_block);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: DMG trailer:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  signature: %.4s\n", __debug__, koly_block->signature);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  version: %u\n", __debug__, koly_block->version);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  header_size: %u\n", __debug__, koly_block->header_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  flags: 0x%X\n", __debug__, koly_block->flags);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  image_variant: %u\n", __debug__, koly_block->image_variant);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  running_data_fork_offset: 0x%" G_GINT64_MODIFIER "x\n", __debug__, koly_block->running_data_fork_offset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  data_fork_offset: 0x%" G_GINT64_MODIFIER "x\n", __debug__, koly_block->data_fork_offset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  data_fork_length: %" G_GINT64_MODIFIER "u\n", __debug__, koly_block->data_fork_length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  rsrc_fork_offset: 0x%" G_GINT64_MODIFIER "x\n", __debug__, koly_block->rsrc_fork_offset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  rsrc_fork_length: %" G_GINT64_MODIFIER "u\n", __debug__, koly_block->rsrc_fork_length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  xml_offset: 0x%" G_GINT64_MODIFIER "x\n", __debug__, koly_block->xml_offset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  xml_length: %" G_GINT64_MODIFIER "u\n", __debug__, koly_block->xml_length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  sector_count: %" G_GINT64_MODIFIER "u\n", __debug__, koly_block->sector_count);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  segment_number: %u\n", __debug__, koly_block->segment_number);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  segment_count: %u\n", __debug__, koly_block->segment_count);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  segment_id: 0x", __debug__);
    for (guint i = 0; i < 4; i++) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%08x", koly_block->segment_id[i]);
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  data_fork_checksum.type: %u\n", __debug__, koly_block->data_fork_checksum.type);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  data_fork_checksum.size: %u\n", __debug__, koly_block->data_fork_checksum.size);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  data_fork_checksum.data:\n", __debug__);
    for (guint c = 0; c < 32; c ++) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%08x ", koly_block->data_fork_checksum.data[c]);
        if ((c + 1) % 8 == 0) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
        }
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  master_checksum.type: %u\n", __debug__, koly_block->master_checksum.type);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  master_checksum.size: %u\n", __debug__, koly_block->master_checksum.size);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  master_checksum.data:\n", __debug__);
    for (guint c = 0; c < 32; c ++) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%08x ", koly_block->master_checksum.data[c]);
        if ((c + 1) % 8 == 0) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
        }
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
}

static void mirage_filter_stream_dmg_print_blkx_block(MirageFilterStreamDmg *self, blkx_block_t *blkx_block)
{
    g_assert(self && blkx_block);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: mish block:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  signature: %.4s\n", __debug__, blkx_block->signature);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  info_version: %u\n", __debug__, blkx_block->info_version);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  first_sector_number: %" G_GINT64_MODIFIER "u\n", __debug__, blkx_block->first_sector_number);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  sector_count: %" G_GINT64_MODIFIER "u\n", __debug__, blkx_block->sector_count);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  data_start: %" G_GINT64_MODIFIER "u\n", __debug__, blkx_block->data_start);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  decompressed_buffer_requested: %u\n", __debug__, blkx_block->decompressed_buffer_requested);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  blocks_descriptor: %i\n", __debug__, blkx_block->blocks_descriptor);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  checksum.type: %u\n", __debug__, blkx_block->checksum.type);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  checksum.size: %u\n", __debug__, blkx_block->checksum.size);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  checksum.data:\n", __debug__);
    for (guint c = 0; c < 32; c ++) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%08x ", blkx_block->checksum.data[c]);
        if ((c + 1) % 8 == 0) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
        }
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  blocks_run_count: %u\n", __debug__, blkx_block->blocks_run_count);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
}

static void mirage_filter_stream_dmg_print_csum_block(MirageFilterStreamDmg *self, csum_block_t *csum_block)
{
    g_assert(self && csum_block);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: cSum block:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  version: %u\n", __debug__, csum_block->version);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  type: %u\n", __debug__, csum_block->type);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  data: 0x%08x\n", __debug__, csum_block->data);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
}

static void mirage_filter_stream_dmg_print_size_block(MirageFilterStreamDmg *self, size_block_t *size_block)
{
    g_assert(self && size_block);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: size block:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  version: %u\n", __debug__, size_block->version);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  is_hfs: %u\n", __debug__, size_block->is_hfs);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  unknown1: 0x%08x\n", __debug__, size_block->unknown1);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  data_length: %u\n", __debug__, size_block->data_length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  unknown2: 0x%08x\n", __debug__, size_block->unknown2);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  unknown3: 0x%08x\n", __debug__, size_block->unknown3);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  vol_modified: 0x%08x\n", __debug__, size_block->vol_modified);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  unknown4: 0x%08x\n", __debug__, size_block->unknown4);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  vol_signature: %.2s\n", __debug__, size_block->vol_sig.as_array);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  size_present: %u\n", __debug__, size_block->size_present);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
}


/**********************************************************************\
 *                     Part filename generation                       *
\**********************************************************************/
static gchar *create_filename_func (const gchar *main_filename, gint index)
{
    if (!main_filename) return NULL;

    gint  main_fn_len = strlen(main_filename) - 7;
    gchar *ret_filename = g_try_malloc(main_fn_len + 12);

    if (!ret_filename) return NULL;

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
static gboolean mirage_filter_stream_dmg_read_descriptor (MirageFilterStreamDmg *self, MirageStream *stream, GError **error)
{
    koly_block_t *koly_block = self->priv->koly_block;
    rsrc_fork_t  *rsrc_fork = NULL;
    gchar        *rsrc_fork_data = NULL;

    /* Read and parse resource-fork */
    if (koly_block->xml_offset && koly_block->xml_length) {
        rsrc_fork_data = g_try_malloc(koly_block->xml_length);
        if (!rsrc_fork_data) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Failed to allocate memory!"));
            return FALSE;
        }

        mirage_stream_seek(stream, koly_block->xml_offset, G_SEEK_SET, NULL);
        if (mirage_stream_read(stream, rsrc_fork_data, koly_block->xml_length, NULL) != koly_block->xml_length) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Failed to read XML resource-fork!"));
            return FALSE;
        }

        rsrc_fork = self->priv->rsrc_fork = rsrc_fork_read_xml(rsrc_fork_data, koly_block->xml_length);
    } else if (koly_block->rsrc_fork_offset && koly_block->rsrc_fork_length) {
        rsrc_fork_data = g_try_malloc(koly_block->rsrc_fork_length);
        if (!rsrc_fork_data) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Failed to allocate memory!"));
            return FALSE;
        }

        mirage_stream_seek(stream, koly_block->rsrc_fork_offset, G_SEEK_SET, NULL);
        if (mirage_stream_read(stream, rsrc_fork_data, koly_block->rsrc_fork_length, NULL) != koly_block->rsrc_fork_length) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Failed to read binary resource-fork!"));
            return FALSE;
        }

        rsrc_fork = self->priv->rsrc_fork = rsrc_fork_read_binary(rsrc_fork_data, koly_block->rsrc_fork_length);
    } else {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, Q_("Image lacks either an XML or a binary descriptor!"));
        return FALSE;
    }

    /* Did all go well? */
    if (!rsrc_fork) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Failed to parse resource-fork!"));
        return FALSE;
    }

    g_free(rsrc_fork_data);
    rsrc_fork_data = NULL;

    /* Loop through resource types */
    for (guint t = 0; t < rsrc_fork->type_list->len; t++) {
        rsrc_type_t *rsrc_type = &g_array_index(rsrc_fork->type_list, rsrc_type_t, t);

        /* Loop through resource refs */
        for (guint r = 0; r < rsrc_type->ref_list->len; r++) {
            rsrc_ref_t *rsrc_ref = &g_array_index(rsrc_type->ref_list, rsrc_ref_t, r);

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Resource Type: %.4s ID: %i Name: %s\n", __debug__,
                         rsrc_type->type.as_array, rsrc_ref->id, rsrc_ref->name->str);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  Attrs: 0x%04hx Data length: %i\n", __debug__,
                         rsrc_ref->attrs, rsrc_ref->data_length);

            /* Convert resource endianness */
            if (!memcmp(rsrc_type->type.as_array, "plst", 4)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n%s: This resource contains partition table information.\n\n", __debug__);
            } else if (!memcmp(rsrc_type->type.as_array, "cSum", 4)) {
                csum_block_t *csum_block = (csum_block_t *) rsrc_ref->data;

                g_assert(rsrc_ref->data_length == sizeof(csum_block_t));
                mirage_filter_stream_dmg_csum_fix_endian(csum_block);
                mirage_filter_stream_dmg_print_csum_block(self, csum_block);
            } else if (!memcmp(rsrc_type->type.as_array, "size", 4)) {
                size_block_t *size_block = (size_block_t *) rsrc_ref->data;

                g_assert(rsrc_ref->data_length == sizeof(size_block_t));
                mirage_filter_stream_dmg_size_fix_endian(size_block);
                mirage_filter_stream_dmg_print_size_block(self, size_block);
            } else if (!memcmp(rsrc_type->type.as_array, "nsiz", 4)) {
                /* Note: There are the same amount of nsiz and the cSum blocks which have the same checksums */
                GString *nsiz_data = NULL;

                nsiz_data = g_string_new_len(rsrc_ref->data, rsrc_ref->data_length);
                g_assert(nsiz_data);

                g_printf("\n%s\n", nsiz_data->str);

                g_string_free(nsiz_data, TRUE);
            } else if (!memcmp(rsrc_type->type.as_array, "blkx", 4)) {
                blkx_block_t *blkx_block = (blkx_block_t *) rsrc_ref->data;
                blkx_data_t  *blkx_data  = (blkx_data_t *) (rsrc_ref->data + sizeof(blkx_block_t));

                mirage_filter_stream_dmg_blkx_block_fix_endian(blkx_block);
                for (guint d = 0; d < blkx_block->blocks_run_count; d++) {
                    mirage_filter_stream_dmg_blkx_data_fix_endian(&blkx_data[d]);

                    /* Update parts count */
                    gint32 block_type = blkx_data[d].block_type;

                    if (block_type == ADC || block_type == ZLIB || block_type == BZLIB ||
                        block_type == ZERO || block_type == RAW || block_type == IGNORE)
                    {
                        self->priv->num_parts++;
                    } else if (block_type == COMMENT || block_type == TERM) {
                        /* Do nothing */
                    } else {
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Encountered unknown block type: %d\n", __debug__, block_type);
                        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Encountered unknown block type: %d!"), block_type);
                        return FALSE;
                    }
                }

                mirage_filter_stream_dmg_print_blkx_block(self, blkx_block);
            } else {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Encountered unknown resource type: %.4s\n", __debug__, rsrc_type->type.as_array);
            }
        }
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Finished reading descriptors ...\n\n", __debug__);

    return TRUE;
}

static gboolean mirage_filter_stream_dmg_read_index (MirageFilterStreamDmg *self, GError **error)
{
    z_stream  *zlib_stream  = &self->priv->zlib_stream;
    bz_stream *bzip2_stream = &self->priv->bzip2_stream;

    koly_block_t *koly_block = self->priv->koly_block;
    rsrc_fork_t  *rsrc_fork = self->priv->rsrc_fork;
    rsrc_type_t  *rsrc_type = NULL;

    gint cur_part = 0;
    gint ret;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: generating part index\n", __debug__);

    /* Allocate part index */
    self->priv->parts = g_try_new(DMG_Part, self->priv->num_parts);
    if (!self->priv->parts) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, Q_("Failed to allocate memory for index!"));
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of parts: %d\n", __debug__, self->priv->num_parts);

    /* Loop through resource refs of type 'blkx' */
    rsrc_type = rsrc_find_type(rsrc_fork, "blkx");
    if (!rsrc_type) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, Q_("Failed to find required 'blkx' type!"));
        return FALSE;
    }

    for (guint r = 0; r < rsrc_type->ref_list->len; r++) {
        rsrc_ref_t   *rsrc_ref = &g_array_index(rsrc_type->ref_list, rsrc_ref_t, r);
        blkx_block_t *blkx_block = (blkx_block_t *) rsrc_ref->data;
        blkx_data_t  *blkx_data  = (blkx_data_t *) (rsrc_ref->data + sizeof(blkx_block_t));

        /*MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Resource %3i: %s\n", __debug__, rsrc_ref->id, rsrc_ref->name->str);*/

        /* Loop through blocks */
        for (guint n = 0; n < blkx_block->blocks_run_count; n++) {
            DMG_Part temp_part;

            temp_part.type = blkx_data[n].block_type;
            temp_part.first_sector = blkx_block->first_sector_number + blkx_data[n].sector_offset;
            temp_part.num_sectors = blkx_data[n].sector_count;
            temp_part.in_offset = blkx_block->data_start + blkx_data[n].compressed_offset;
            temp_part.in_length = blkx_data[n].compressed_length;

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
                if (temp_part.type == ADC || temp_part.type == ZLIB || temp_part.type == BZLIB) {
                    if (self->priv->io_buffer_size < temp_part.in_length) {
                        self->priv->io_buffer_size = temp_part.in_length;
                    }
                    if (self->priv->inflate_buffer_size < temp_part.num_sectors * DMG_SECTOR_SIZE) {
                        self->priv->inflate_buffer_size = temp_part.num_sectors * DMG_SECTOR_SIZE;
                    }
                } else if (temp_part.type == RAW) {
                    if (self->priv->inflate_buffer_size < temp_part.num_sectors * DMG_SECTOR_SIZE) {
                        self->priv->inflate_buffer_size = temp_part.num_sectors * DMG_SECTOR_SIZE;
                    }
                } else if (temp_part.type == ZERO || temp_part.type == IGNORE) {
                    /* Avoid use of buffer for zeros */
                } else {
                    g_assert_not_reached();
                }
            }
        }
    }

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
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, Q_("Failed to initialize zlib's inflate (error: %d)!"), ret);
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
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, Q_("Failed to initialize libbz2's decompress (error: %d)!"), ret);
        return FALSE;
    }

    /* Allocate inflate buffer */
    self->priv->inflate_buffer = g_try_malloc(self->priv->inflate_buffer_size);
    if (!self->priv->inflate_buffer && self->priv->inflate_buffer_size) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, Q_("Failed to allocate memory for inflate buffer!"));
        return FALSE;
    }

    /* Allocate I/O buffer */
    self->priv->io_buffer = g_try_malloc(self->priv->io_buffer_size);
    if (!self->priv->io_buffer && self->priv->io_buffer_size) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, Q_("Failed to allocate memory for I/O buffer!"));
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: successfully generated index\n\n", __debug__);

    return TRUE;
}


/**********************************************************************\
 *               MirageFilterStream methods implementation            *
\**********************************************************************/
static gboolean mirage_filter_stream_dmg_open_streams (MirageFilterStreamDmg *self, GError **error)
{
    MirageStream **streams;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: opening streams\n", __debug__);

    /* Allocate space for streams */
    self->priv->streams = streams = g_try_new(MirageStream *, self->priv->koly_block->segment_count);
    if (!streams) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, Q_("Failed to allocate memory for streams!"));
        return FALSE;
    }

    /* Fill in existing stream */
    streams[0] = g_object_ref(mirage_filter_stream_get_underlying_stream(MIRAGE_FILTER_STREAM(self)));
    self->priv->num_streams++;

    const gchar *original_filename = mirage_stream_get_filename(streams[0]);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  %s\n", __debug__, original_filename);

    /* Create the rest of the streams */
    for (guint s = 1; s < self->priv->koly_block->segment_count; s++) {
        gchar *filename = create_filename_func(original_filename, s);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  %s\n", __debug__, filename);
        streams[s] = mirage_contextual_create_input_stream (MIRAGE_CONTEXTUAL(self), filename, error);
        if (!streams[s]) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, Q_("Failed to create stream!"));
            return FALSE;
        }
        g_free(filename);
        self->priv->num_streams++;
    }

    /* Allocated space for additional koly blocks */
    self->priv->num_koly_blocks = self->priv->koly_block->segment_count;
    self->priv->koly_block = g_try_renew(koly_block_t, self->priv->koly_block, self->priv->num_koly_blocks);
    if (!self->priv->koly_block) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, Q_("Failed to reallocate memory for koly blocks!"));
        return FALSE;
    }

    /* Read the rest of the koly blocks */
    for (guint s = 1; s < self->priv->num_koly_blocks; s++) {
        for (guint try = 0; try < 2; try++) {
            /* Find koly block either on end (most often) or beginning of file */
            if (try == 0) {
                mirage_stream_seek(streams[s], -sizeof(koly_block_t), G_SEEK_END, NULL);
            } else {
                mirage_stream_seek(streams[s], 0, G_SEEK_SET, NULL);
            }

            /* Read koly block */
            if (mirage_stream_read(streams[s], &self->priv->koly_block[s], sizeof(koly_block_t), NULL) != sizeof(koly_block_t)) {
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Failed to read koly block!"));
                return FALSE;
            }

            /* Validate koly block */
            if (memcmp(self->priv->koly_block[s].signature, koly_signature, sizeof(koly_signature))) {
                if (try == 1) {
                    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Invalid koly block!"));
                    return FALSE;
                }
            } else {
                mirage_filter_stream_dmg_koly_block_fix_endian(&self->priv->koly_block[s]);
                break;
            }
        }

        /* Output koly block info */
        mirage_filter_stream_dmg_print_koly_block(self, &self->priv->koly_block[s]);
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: successfully opened streams\n\n", __debug__);

    return TRUE;
}

static gboolean mirage_filter_stream_dmg_open (MirageFilterStream *_self, MirageStream *stream, gboolean writable G_GNUC_UNUSED, GError **error)
{
    MirageFilterStreamDmg *self = MIRAGE_FILTER_STREAM_DMG(_self);

    koly_block_t *koly_block = NULL;

    gboolean succeeded = TRUE;
    gint     ret;

    /* Allocate koly block*/
    self->priv->num_koly_blocks = 1;
    self->priv->koly_block = koly_block = g_try_new(koly_block_t, self->priv->num_koly_blocks);
    if (!koly_block) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, Q_("Failed to allocate memory for koly block!"));
        return FALSE;
    }

    for (guint try = 0; try < 2; try++) {
        /* Find koly block either on end (most often) or beginning of file */
        if (try == 0) {
            mirage_stream_seek(stream, -sizeof(koly_block_t), G_SEEK_END, NULL);
        } else {
            mirage_stream_seek(stream, 0, G_SEEK_SET, NULL);
        }

        /* Read koly block */
        if (mirage_stream_read(stream, koly_block, sizeof(koly_block_t), NULL) != sizeof(koly_block_t)) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Filter stream cannot handle given image: failed to read koly block!"));
            return FALSE;
        }

        /* Validate koly block */
        if (memcmp(koly_block->signature, koly_signature, sizeof(koly_signature))) {
            if (try == 1) {
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Filter stream cannot handle given image: invalid koly block!"));
                return FALSE;
            }
        } else {
            mirage_filter_stream_dmg_koly_block_fix_endian(koly_block);
            break;
        }
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing the underlying stream data...\n", __debug__);

    /* Only perform parsing on the first file in a set */
    if (koly_block->segment_number != 1) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, Q_("File is not the first file of a set!"));
        return FALSE;
    }

    /* Output koly block info */
    mirage_filter_stream_dmg_print_koly_block(self, koly_block);

    /* Open streams */
    ret = mirage_filter_stream_dmg_open_streams(self, error);
    if (!ret) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, Q_("Failed to open streams!"));
        return FALSE;
    }
    /* This have been re-allocated, so update local pointer */
    koly_block = self->priv->koly_block;

    /* Set file size */
    mirage_filter_stream_simplified_set_stream_length(_self, koly_block->sector_count * DMG_SECTOR_SIZE);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: original stream size: %" G_GINT64_MODIFIER "u\n", __debug__, koly_block->sector_count * DMG_SECTOR_SIZE);

    /* Read descriptors */
    succeeded = mirage_filter_stream_dmg_read_descriptor(self, stream, error);
    if (!succeeded) {
        goto end;
    }

    succeeded = mirage_filter_stream_dmg_read_index (self, error);

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

static gssize mirage_filter_stream_dmg_read_raw_chunk (MirageFilterStreamDmg *self, guint8 *buffer, gint chunk_num)
{
    const DMG_Part *part = &self->priv->parts[chunk_num];
    MirageStream   *stream = self->priv->streams[part->segment];
    koly_block_t   *koly_block = &self->priv->koly_block[part->segment];

    gsize   to_read = part->in_length;
    gsize   have_read = 0;
    goffset part_offs = koly_block->data_fork_offset + part->in_offset - koly_block->running_data_fork_offset;
    gsize   part_avail = koly_block->running_data_fork_offset + koly_block->data_fork_length - part->in_offset;
    gint    ret;

    /* Seek to the position */
    if (!mirage_stream_seek(stream, part_offs, G_SEEK_SET, NULL)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to %" G_GOFFSET_MODIFIER "d in underlying stream!\n", __debug__, part_offs);
        return -1;
    }

    /*MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: raw position: %u\n", __debug__, part_offs);*/

    /* Read raw chunk data */
    ret = mirage_stream_read(stream, &buffer[have_read], MIN(to_read, part_avail), NULL);
    if (ret < 0) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read %" G_GSIZE_MODIFIER "d bytes from underlying stream!\n", __debug__, to_read);
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
        if (!mirage_stream_seek(stream, part_offs, G_SEEK_SET, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to %" G_GOFFSET_MODIFIER "d in underlying stream!\n", __debug__, part_offs);
            return -1;
        }

        /* Read raw chunk data */
        ret = mirage_stream_read(stream, &buffer[have_read], to_read, NULL);
        if (ret < 0) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read %" G_GSIZE_MODIFIER "d bytes from underlying stream!\n", __debug__, to_read);
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

static gssize mirage_filter_stream_dmg_partial_read (MirageFilterStream *_self, void *buffer, gsize count)
{
    MirageFilterStreamDmg *self = MIRAGE_FILTER_STREAM_DMG(_self);
    goffset position = mirage_filter_stream_simplified_get_position(MIRAGE_FILTER_STREAM(self));
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
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: failed to find part!\n", __debug__);
        return 0;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: stream position: %" G_GOFFSET_MODIFIER "d (0x%" G_GOFFSET_MODIFIER "X) -> part #%d (cached: #%d)\n", __debug__, position, position, part_idx, self->priv->cached_part);

    /* If we do not have part in cache, uncompress it */
    if (part_idx != self->priv->cached_part) {
        const DMG_Part *part = &self->priv->parts[part_idx];
        z_stream *zlib_stream = &self->priv->zlib_stream;
        bz_stream *bzip2_stream = &self->priv->bzip2_stream;
        gint ret;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: part not cached, reading...\n", __debug__);

        /* Read a part */
        if (part->type == ZERO || part->type == IGNORE) {
            /* We don't use internal buffers for zero data */
        } else if (part->type == RAW) {
            /* Read uncompressed part */
            ret = mirage_filter_stream_dmg_read_raw_chunk (self, self->priv->inflate_buffer, part_idx);
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
            ret = mirage_filter_stream_dmg_read_raw_chunk (self, self->priv->io_buffer, part_idx);
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
            ret = mirage_filter_stream_dmg_read_raw_chunk (self, self->priv->io_buffer, part_idx);
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
            gsize written_bytes;

            /* Read some compressed data */
            ret = mirage_filter_stream_dmg_read_raw_chunk (self, self->priv->io_buffer, part_idx);
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
        if (part->type != ZERO && part->type != IGNORE) {
            self->priv->cached_part = part_idx;
        }
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: part already cached\n", __debug__);
    }

    /* Copy data */
    const DMG_Part *part = &self->priv->parts[part_idx];

    gsize   part_size = part->num_sectors * DMG_SECTOR_SIZE;
    guint64 part_offset = position - (part->first_sector * DMG_SECTOR_SIZE);
    count = MIN(count, part_size - part_offset);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: offset within part: %" G_GINT64_MODIFIER "d, copying %" G_GSIZE_MODIFIER "d bytes\n", __debug__, part_offset, count);

    if (part->type == ZERO || part->type == IGNORE) {
        memset(buffer, 0, count);
    } else {
        memcpy(buffer, &self->priv->inflate_buffer[part_offset], count);
    }

    return count;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE(MirageFilterStreamDmg, mirage_filter_stream_dmg, MIRAGE_TYPE_FILTER_STREAM);

void mirage_filter_stream_dmg_type_register (GTypeModule *type_module)
{
    return mirage_filter_stream_dmg_register_type(type_module);
}


static void mirage_filter_stream_dmg_init (MirageFilterStreamDmg *self)
{
    self->priv = MIRAGE_FILTER_STREAM_DMG_GET_PRIVATE(self);

    mirage_filter_stream_generate_info(MIRAGE_FILTER_STREAM(self),
        "FILTER-DMG",
        Q_("DMG File Filter"),
        FALSE,
        1,
        Q_("Apple Disk Image (*.dmg)"), "application/x-apple-diskimage"
    );

    self->priv->koly_block = NULL;

    self->priv->streams = NULL;

    self->priv->rsrc_fork = NULL;

    self->priv->num_koly_blocks = 0;
    self->priv->num_streams = 0;

    self->priv->num_parts = 0;
    self->priv->parts = NULL;

    self->priv->cached_part = -1;
    self->priv->inflate_buffer = NULL;
    self->priv->io_buffer = NULL;
}

static void mirage_filter_stream_dmg_finalize (GObject *gobject)
{
    MirageFilterStreamDmg *self = MIRAGE_FILTER_STREAM_DMG(gobject);

    for (gint s = 0; s < self->priv->num_streams; s++) {
        g_object_unref(self->priv->streams[s]);
    }
    g_free(self->priv->streams);

    g_free(self->priv->parts);
    g_free(self->priv->inflate_buffer);
    g_free(self->priv->io_buffer);

    inflateEnd(&self->priv->zlib_stream);
    BZ2_bzDecompressEnd(&self->priv->bzip2_stream);

    g_free(self->priv->koly_block);

    rsrc_fork_free(self->priv->rsrc_fork);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_filter_stream_dmg_parent_class)->finalize(gobject);
}

static void mirage_filter_stream_dmg_class_init (MirageFilterStreamDmgClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageFilterStreamClass *filter_stream_class = MIRAGE_FILTER_STREAM_CLASS(klass);

    gobject_class->finalize = mirage_filter_stream_dmg_finalize;

    filter_stream_class->open = mirage_filter_stream_dmg_open;

    filter_stream_class->simplified_partial_read = mirage_filter_stream_dmg_partial_read;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageFilterStreamDmgPrivate));
}

static void mirage_filter_stream_dmg_class_finalize (MirageFilterStreamDmgClass *klass G_GNUC_UNUSED)
{
}
