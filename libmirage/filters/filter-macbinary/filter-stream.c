/*
 *  libMirage: MacBinary filter: filter stream
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

#include "filter-macbinary.h"

#define __debug__ "MACBINARY-FilterStream"

typedef struct {
    bcem_type_t type;

    guint32  first_sector;
    guint32  num_sectors;
    gint     segment;
    goffset  in_offset;
    gsize    in_length;
} NDIF_Part;


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
struct _MirageFilterStreamMacBinaryPrivate
{
    /* macbinary header */
    macbinary_header_t header;

    /* resource fork */
    rsrc_fork_t *rsrc_fork;

    /* part list */
    NDIF_Part *parts;
    gint num_parts;

    /* Inflate buffer */
    guint8 *inflate_buffer;
    guint inflate_buffer_size;
    gint cached_part;

    /* I/O buffer */
    guint8 *io_buffer;
    guint io_buffer_size;
};


/**********************************************************************\
 *             Endianness conversion and debug functions              *
\**********************************************************************/

/* Classic Mac OS represents time in seconds since 1904, instead of
   Unix time's 1970 epoch. This is the difference between the two. */
#define MAC_TIME_OFFSET 2082844800

static void mirage_filter_stream_macbinary_fixup_header(macbinary_header_t *header)
{
    g_assert(header);

    header->vert_pos      = GUINT16_FROM_BE(header->vert_pos);
    header->horiz_pos     = GUINT16_FROM_BE(header->horiz_pos);
    header->window_id     = GUINT16_FROM_BE(header->window_id);
    header->getinfo_len   = GUINT16_FROM_BE(header->getinfo_len);
    header->secondary_len = GUINT16_FROM_BE(header->secondary_len);
    header->crc16         = GUINT16_FROM_BE(header->crc16);

    header->datafork_len = GUINT32_FROM_BE(header->datafork_len);
    header->resfork_len  = GUINT32_FROM_BE(header->resfork_len);
    header->created      = GUINT32_FROM_BE(header->created) - MAC_TIME_OFFSET;
    header->modified     = GUINT32_FROM_BE(header->modified) - MAC_TIME_OFFSET;
    header->unpacked_len = GUINT32_FROM_BE(header->unpacked_len);
}

static void mirage_filter_stream_macbinary_fixup_bcem_block(bcem_block_t *bcem_block)
{
    g_assert(bcem_block);

    bcem_block->version_major  = GUINT16_FROM_BE(bcem_block->version_major);
    bcem_block->version_minor  = GUINT16_FROM_BE(bcem_block->version_minor);
    bcem_block->num_sectors    = GUINT32_FROM_BE(bcem_block->num_sectors);
    bcem_block->chunk_size     = GUINT32_FROM_BE(bcem_block->chunk_size);
    bcem_block->bs_zero_offset = GUINT32_FROM_BE(bcem_block->bs_zero_offset);
    bcem_block->crc32          = GUINT32_FROM_BE(bcem_block->crc32);
    bcem_block->is_segmented   = GUINT32_FROM_BE(bcem_block->is_segmented);
    bcem_block->num_blocks     = GUINT32_FROM_BE(bcem_block->num_blocks);

    /* ignoring unknown1 and reserved */
}

static void mirage_filter_stream_macbinary_fixup_bcem_data(bcem_data_t *bcem_data)
{
    guint8 temp;

    g_assert(bcem_data);

    temp = bcem_data->sector[0];
    bcem_data->sector[0] = bcem_data->sector[2];
    bcem_data->sector[2] = temp;

    bcem_data->offset = GUINT32_FROM_BE(bcem_data->offset);
    bcem_data->length = GUINT32_FROM_BE(bcem_data->length);
}

static void mirage_filter_stream_macbinary_fixup_bcm_block(bcm_block_t *bcm_block)
{
    g_assert(bcm_block);

    bcm_block->part     = GUINT16_FROM_BE(bcm_block->part);
    bcm_block->parts    = GUINT16_FROM_BE(bcm_block->parts);
    bcm_block->unknown1 = GUINT32_FROM_BE(bcm_block->unknown1);

    for (guint i = 0; i < 4; i++) {
        bcm_block->UUID[i] = GUINT32_FROM_BE(bcm_block->UUID[i]);
    }
}

static void mirage_filter_stream_macbinary_print_header(MirageFilterStreamMacBinary *self, macbinary_header_t *header, guint16 calculated_crc)
{
    GString   *filename = NULL;
    GDateTime *created = NULL;
    GDateTime *modified = NULL;
    gchar     *created_str = NULL;
    gchar     *modified_str = NULL;

    g_assert(self && header);

    filename = g_string_new_len(header->filename, header->fn_length);
    g_assert(filename);

    created = g_date_time_new_from_unix_utc(header->created);
    modified = g_date_time_new_from_unix_utc(header->modified);
    g_assert(created && modified);

    created_str = g_date_time_format(created, "%Y-%m-%d %H:%M.%S");
    modified_str = g_date_time_format(modified, "%Y-%m-%d %H:%M.%S");
    g_assert(created_str && modified_str);

    g_date_time_unref(created);
    g_date_time_unref(modified);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n%s: MacBinary header:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  Original filename: %s\n", __debug__, filename->str);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  File type: %.4s creator: %.4s\n", __debug__, header->filetype, header->creator);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  Data fork length: %d\n", __debug__, header->datafork_len);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  Resource fork length: %d\n", __debug__, header->resfork_len);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  Created: %s\n", __debug__, created_str);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  Modified: %s\n", __debug__, modified_str);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  Get info comment length: %d\n", __debug__, header->getinfo_len);

    if (calculated_crc == header->crc16) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  Length of total files: %d\n", __debug__, header->unpacked_len);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  Length of secondary header: %d\n", __debug__, header->secondary_len);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  CRC16: 0x%04x (calculated: 0x%04x)\n", __debug__, header->crc16, calculated_crc);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  Version used to pack: %d\n", __debug__, header->pack_ver-129);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  Version needed to unpack: %d\n", __debug__, header->unpack_ver-129);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  Finder flags: 0x%04x\n", __debug__, (header->finder_flags << 8) + header->finder_flags_2);
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  Finder flags: 0x%04x\n", __debug__, header->finder_flags << 8);
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");

    g_string_free(filename, TRUE);

    g_free(created_str);
    g_free(modified_str);
}

static void mirage_filter_stream_macbinary_print_bcem_block(MirageFilterStreamMacBinary *self, bcem_block_t *bcem_block)
{
    GString *imagename = NULL;

    g_assert(self && bcem_block);

    imagename = g_string_new_len(bcem_block->imagename, bcem_block->imagename_len);
    g_assert(imagename);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n%s: bcem block:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  Version: %u.%u\n", __debug__, bcem_block->version_major, bcem_block->version_minor);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  Image name: %s\n", __debug__, imagename->str);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  Number of sectors: %u\n", __debug__, bcem_block->num_sectors);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  Chunk size: %u\n", __debug__, bcem_block->chunk_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  BS zero offset: 0x%08x\n", __debug__, bcem_block->bs_zero_offset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  CRC32: 0x%08x\n", __debug__, bcem_block->crc32);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  Is segmented: %u\n", __debug__, bcem_block->is_segmented);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  Number of blocks: %u\n\n", __debug__, bcem_block->num_blocks);

    /*for (guint u = 0; u < 2; u++) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  Unknown1[%u]: 0x%08x\n", __debug__, u, bcem_block->unknown1[u]);
    }*/

    g_string_free(imagename, TRUE);
}


/**********************************************************************\
 *             MirageFilterStream methods implementations             *
\**********************************************************************/
static gboolean mirage_filter_stream_macbinary_open (MirageFilterStream *_self, MirageStream *stream, gboolean writable G_GNUC_UNUSED, GError **error)
{
    MirageFilterStreamMacBinary *self = MIRAGE_FILTER_STREAM_MACBINARY(_self);

    macbinary_header_t *header = &self->priv->header;
    rsrc_fork_t        *rsrc_fork = NULL;

    guint16 calculated_crc = 0;

    const gboolean trust_unreliable_v1_check = FALSE;

    /* Read MacBinary header */
    mirage_stream_seek(stream, 0, G_SEEK_SET, NULL);
    if (mirage_stream_read(stream, header, sizeof(macbinary_header_t), NULL) != sizeof(macbinary_header_t)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: Filter cannot handle given data: failed to read MacBinary header!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Filter cannot handle given data: failed to read MacBinary header!"));
        return FALSE;
    }

    /* We need to calculate CRC16 before we fixup the header */
    calculated_crc = mirage_helper_calculate_crc16((guint8 *) header, sizeof(macbinary_header_t) - 4, crc16_1021_lut, FALSE, FALSE);

    /* Fixup header endianness */
    mirage_filter_stream_macbinary_fixup_header(header);

    /* Validate MacBinary header */
    if (header->version != 0 || header->reserved_1 != 0 || header->reserved_2 != 0 ||
        header->fn_length < 1 || header->fn_length > 63) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: Filter cannot handle given data: invalid header!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Filter cannot handle given data: invalid header!"));
        return FALSE;
    }

    /* Valid CRC indicates v2.0 */
    if (calculated_crc != header->crc16) {
        /* Do we have v1.0 then? Hard to say for sure... */
        if (!trust_unreliable_v1_check) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: File validates as MacBinary v1.0, however the check is unreliable and therefore disabled!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("File validates as MacBinary v1.0, however the check is unreliable and therefore disabled!"));
            return FALSE;
        }
    }

    /* Print some info */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing the underlying stream data...\n", __debug__);

    mirage_filter_stream_macbinary_print_header(self, header, calculated_crc);

    /* Read the resource fork if any exists */
    if (header->resfork_len) {
        goffset     rsrc_fork_pos = 0;
        gchar       *rsrc_fork_data = NULL;

        rsrc_fork_pos = sizeof(macbinary_header_t) + header->datafork_len;
        if (header->datafork_len % 128) {
            rsrc_fork_pos += 128 - (header->datafork_len % 128);
        }

        rsrc_fork_data = g_try_malloc(header->resfork_len);
        if (!rsrc_fork_data) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Failed to allocate memory!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Failed to allocate memory!"));
            return FALSE;
        }

        mirage_stream_seek(stream, rsrc_fork_pos, G_SEEK_SET, NULL);
        if (mirage_stream_read(stream, rsrc_fork_data, header->resfork_len, NULL) != header->resfork_len) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Failed to read resource-fork!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Failed to read resource-fork!"));
            return FALSE;
        }

        rsrc_fork = self->priv->rsrc_fork = rsrc_fork_read_binary(rsrc_fork_data, header->resfork_len);
        if (!rsrc_fork) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Failed to parse resource-fork!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Failed to parse resource-fork!"));
            return FALSE;
        }

        g_free(rsrc_fork_data);
    }

    /* Search resource-fork for NDIF data */
    if (rsrc_fork) {
        rsrc_ref_t *rsrc_ref = NULL;

        /* Look up "bcem" resource */
        rsrc_ref = rsrc_find_ref_by_type_and_id(rsrc_fork, "bcem", 128);

        if (rsrc_ref) {
            bcem_block_t *bcem_block = (bcem_block_t *) rsrc_ref->data;
            bcem_data_t  *bcem_data = (bcem_data_t *) (rsrc_ref->data + sizeof(bcem_block_t));

            mirage_filter_stream_macbinary_fixup_bcem_block(bcem_block);

            mirage_filter_stream_macbinary_print_bcem_block(self, bcem_block);

            /* Set the total image size */
            mirage_filter_stream_simplified_set_stream_length(MIRAGE_FILTER_STREAM(self), bcem_block->num_sectors * 512);

            /* Construct a part index */
            self->priv->num_parts = bcem_block->num_blocks - 1;
            self->priv->parts = g_try_new0(NDIF_Part, self->priv->num_parts);
            if (!self->priv->parts) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Failed to allocate memory!\n", __debug__);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Failed to allocate memory!"));
                return FALSE;
            }

            for (guint b = 0; b < bcem_block->num_blocks; b++) {
                mirage_filter_stream_macbinary_fixup_bcem_data(&bcem_data[b]);
            }

            for (guint b = 0; b < bcem_block->num_blocks; b++) {
                NDIF_Part *cur_part = &self->priv->parts[b];
                guint32   start_sector, end_sector;

                start_sector  = (bcem_data[b].sector[2] << 16) + (bcem_data[b].sector[1] << 8) + bcem_data[b].sector[0];
                end_sector    = (bcem_data[b+1].sector[2] << 16) + (bcem_data[b+1].sector[1] << 8) + bcem_data[b+1].sector[0];

                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: [%3u] Sector: %8u Type: %4d Offset: 0x%08x Length: 0x%08x (%u)\n",
                             __debug__, b, start_sector, bcem_data[b].type, bcem_data[b].offset, bcem_data[b].length, bcem_data[b].length);

                if (bcem_data[b].type == BCEM_ADC || bcem_data[b].type == BCEM_ZERO || bcem_data[b].type == BCEM_RAW) {
                    /* Fill in part table entry */
                    cur_part->type         = bcem_data[b].type;
                    cur_part->first_sector = start_sector;
                    cur_part->num_sectors  = end_sector - start_sector;
                    cur_part->segment      = -1; /* uninitialized default */
                    cur_part->in_offset    = bcem_data[b].offset;
                    cur_part->in_length    = bcem_data[b].length;

                    /* Update buffer sizes */
                    if (cur_part->type == BCEM_ADC) {
                        if (self->priv->io_buffer_size < cur_part->in_length) {
                            self->priv->io_buffer_size = cur_part->in_length;
                        }
                        if (self->priv->inflate_buffer_size < cur_part->num_sectors * 512) {
                            self->priv->inflate_buffer_size = cur_part->num_sectors * 512;
                        }
                    } else if (cur_part->type == BCEM_RAW) {
                        if (self->priv->inflate_buffer_size < cur_part->num_sectors * 512) {
                            self->priv->inflate_buffer_size = cur_part->num_sectors * 512;
                        }
                    } else if (cur_part->type == BCEM_ZERO) {
                        /* Avoid use of buffer for zeros */
                    }
                } else if (bcem_data[b].type == BCEM_TERM) {
                    /* Skip the terminating block */
                    g_assert(start_sector == bcem_block->num_sectors);
                } else if (bcem_data[b].type == BCEM_KENCODE) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: KenCode decompression is not supported!\n", __debug__);
                    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("KenCode decompression is not supported!"));
                    return FALSE;
                } else {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Encountered unknown part type: %d!\n", __debug__, bcem_data[b].type);
                    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Encountered unknown part type: %d!"), bcem_data[b].type);
                    return FALSE;
                }
            }

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: IO buffer size: %u\n", __debug__, self->priv->io_buffer_size);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Inflate buffer size: %u\n\n", __debug__, self->priv->inflate_buffer_size);

            self->priv->io_buffer = g_try_malloc(self->priv->io_buffer_size);
            if (!self->priv->io_buffer && self->priv->io_buffer_size) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Error allocating memory for buffers!\n", __debug__);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Error allocating memory for buffers!"));
                return FALSE;
            }

            self->priv->inflate_buffer = g_try_malloc(self->priv->inflate_buffer_size);
            if (!self->priv->inflate_buffer && self->priv->inflate_buffer_size) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Error allocating memory for buffers!\n", __debug__);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Error allocating memory for buffers!"));
                return FALSE;
            }
        }

        /* Look up "bcm#" resource */
        rsrc_ref = rsrc_find_ref_by_type_and_id(rsrc_fork, "bcm#", 128);

        if (rsrc_ref) {
            bcm_block_t *bcm_block = (bcm_block_t *) rsrc_ref->data;

            mirage_filter_stream_macbinary_fixup_bcm_block(bcm_block);

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: This file is part %u of a set of %u files!\n",
                         __debug__, bcm_block->part, bcm_block->parts);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: UUID&Unknown1: 0x%08x 0x%08x 0x%08x 0x%08x - 0x%08x\n\n", __debug__,
                         bcm_block->UUID[0], bcm_block->UUID[1], bcm_block->UUID[2], bcm_block->UUID[3],
                         bcm_block->unknown1);
        }
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing completed successfully\n\n", __debug__);

    /* NDIF parts list indicates success */
    if (self->priv->parts) {
        return TRUE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: NDIF data structures not found!\n", __debug__);
    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("NDIF data structures not found!"));
    return FALSE;
}

static gssize mirage_filter_stream_macbinary_read_raw_chunk (MirageFilterStreamMacBinary *self, guint8 *buffer, gint chunk_num)
{
    const NDIF_Part    *part = &self->priv->parts[chunk_num];
    MirageStream       *stream = mirage_filter_stream_get_underlying_stream(MIRAGE_FILTER_STREAM(self));
    macbinary_header_t *header = &self->priv->header;

    gsize   to_read = part->in_length;
    gsize   have_read = 0;
    goffset part_offs = sizeof(macbinary_header_t) + part->in_offset;
    gsize   part_avail = MIN(part->in_length, header->datafork_len - part->in_offset);
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
    } else if ((guint)ret == to_read) {
        have_read += ret;
        to_read -= ret;
    } else if ((guint)ret < to_read) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading remaining data!\n", __debug__);
        have_read += ret;
        to_read -= ret;

        /* FIXME: We don't support segmented images yet! */
        g_assert_not_reached();
    }

    g_assert(to_read == 0 && have_read == part->in_length);

    return have_read;
}

static gssize mirage_filter_stream_macbinary_partial_read (MirageFilterStream *_self, void *buffer, gsize count)
{
    MirageFilterStreamMacBinary *self = MIRAGE_FILTER_STREAM_MACBINARY(_self);

    goffset position = mirage_filter_stream_simplified_get_position(MIRAGE_FILTER_STREAM(self));
    gint    part_idx = -1;

    /* Find part that corresponds to current position */
    for (gint p = 0; p < self->priv->num_parts; p++) {
        const NDIF_Part *cur_part = &self->priv->parts[p];
        guint req_sector = position / 512;

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
        const NDIF_Part *part = &self->priv->parts[part_idx];
        gsize ret;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: part not cached, reading...\n", __debug__);

        /* Read a part */
        if (part->type == BCEM_ZERO) {
            /* We don't use internal buffers for zero data */
        } else if (part->type == BCEM_RAW) {
            /* Read uncompressed part */
            ret = mirage_filter_stream_macbinary_read_raw_chunk (self, self->priv->inflate_buffer, part_idx);
            if (ret != part->in_length) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read raw chunk!\n", __debug__);
                return -1;
            }
        } else if (part->type == BCEM_ADC) {
            gsize written_bytes;

            /* Read some compressed data */
            ret = mirage_filter_stream_macbinary_read_raw_chunk (self, self->priv->io_buffer, part_idx);
            if (ret != part->in_length) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read raw chunk!\n", __debug__);
                return -1;
            }

            /* Inflate */
            ret = adc_decompress(part->in_length, self->priv->io_buffer, part->num_sectors * 512, self->priv->inflate_buffer, &written_bytes);

            g_assert (ret == part->in_length);
            g_assert (written_bytes == part->num_sectors * 512);
        } else {
            /* We should never get here... */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Encountered unknown chunk type: %d!\n", __debug__, part->type);
            return -1;
        }

        /* Set currently cached part */
        if (part->type != BCEM_ZERO) {
            self->priv->cached_part = part_idx;
        }
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: part already cached\n", __debug__);
    }

    /* Copy data */
    const NDIF_Part *part = &self->priv->parts[part_idx];

    gsize   part_size = part->num_sectors * 512;
    guint64 part_offset = position - (part->first_sector * 512);
    count = MIN(count, part_size - part_offset);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: offset within part: %" G_GINT64_MODIFIER "d, copying %" G_GSIZE_MODIFIER "d bytes\n", __debug__, part_offset, count);

    if (part->type == BCEM_ZERO) {
        memset(buffer, 0, count);
    } else {
        memcpy(buffer, &self->priv->inflate_buffer[part_offset], count);
    }

    return count;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE_EXTENDED(
    MirageFilterStreamMacBinary,
    mirage_filter_stream_macbinary,
    MIRAGE_TYPE_FILTER_STREAM,
    0,
    G_ADD_PRIVATE_DYNAMIC(MirageFilterStreamMacBinary)
)

void mirage_filter_stream_macbinary_type_register (GTypeModule *type_module)
{
    mirage_filter_stream_macbinary_register_type(type_module);
}


static void mirage_filter_stream_macbinary_init (MirageFilterStreamMacBinary *self)
{
    self->priv = mirage_filter_stream_macbinary_get_instance_private(self);

    mirage_filter_stream_generate_info(MIRAGE_FILTER_STREAM(self),
        "FILTER-MACBINARY",
        Q_("MACBINARY File Filter"),
        FALSE,
        1,
        Q_("MacBinary images (*.bin, *.macbin)"), "application/x-macbinary"
    );

    self->priv->rsrc_fork = NULL;
    self->priv->parts = NULL;
    self->priv->inflate_buffer = NULL;
    self->priv->io_buffer = NULL;

    self->priv->num_parts = 0;
    self->priv->inflate_buffer_size = 0;
    self->priv->io_buffer_size = 0;

    self->priv->cached_part = -1;
}

static void mirage_filter_stream_macbinary_finalize (GObject *gobject)
{
    MirageFilterStreamMacBinary *self = MIRAGE_FILTER_STREAM_MACBINARY(gobject);

    if (self->priv->rsrc_fork) {
        rsrc_fork_free(self->priv->rsrc_fork);
    }

    if (self->priv->parts) {
        g_free(self->priv->parts);
    }

    if (self->priv->inflate_buffer) {
        g_free(self->priv->inflate_buffer);
    }

    if (self->priv->io_buffer) {
        g_free(self->priv->io_buffer);
    }

    /* Chain up to the parent class */
    G_OBJECT_CLASS(mirage_filter_stream_macbinary_parent_class)->finalize(gobject);
}

static void mirage_filter_stream_macbinary_class_init (MirageFilterStreamMacBinaryClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageFilterStreamClass *filter_stream_class = MIRAGE_FILTER_STREAM_CLASS(klass);

    gobject_class->finalize = mirage_filter_stream_macbinary_finalize;

    filter_stream_class->open = mirage_filter_stream_macbinary_open;

    filter_stream_class->simplified_partial_read = mirage_filter_stream_macbinary_partial_read;
}

static void mirage_filter_stream_macbinary_class_finalize (MirageFilterStreamMacBinaryClass *klass G_GNUC_UNUSED)
{
}

