/*
 *  libMirage: ECM file filter: File filter object
 *  Copyright (C) 2012 Rok Mandeljc
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

#include "filter-ecm.h"

#define __debug__ "ECM-FileFilter"


typedef struct
{
    guint8 type;
    gint num;

    goffset raw_offset;
    gsize raw_size;

    goffset offset;
    gsize size;
} ECM_Part;

static const guint8 ecm_signature[4] = { 'E', 'C', 'M', 0x00 };


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_FILE_FILTER_ECM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FILE_FILTER_ECM, MirageFileFilterEcmPrivate))

struct _MirageFileFilterEcmPrivate
{
    goffset cur_position;

    gint cur_part_idx;
    const ECM_Part *cur_part;

    guint64 file_size;

    /* Part list */
    ECM_Part *parts;
    gint num_parts;
    gint allocated_parts;

    /* Cache */
    gint cache_part_idx;
    gint cache_block_idx;
    guint8 cache_buffer[2352];
};


/**********************************************************************\
 *                           Part indexing                            *
\**********************************************************************/
static gboolean mirage_file_filter_ecm_append_part (MirageFileFilterEcm *self, gint num, guint8 type, goffset raw_offset, gsize raw_size, goffset offset, gsize size, GError **error)
{
    /* If no parts have been allocated yet, do so now; start with eight */
    if (!self->priv->allocated_parts) {
        self->priv->allocated_parts = 8;
        self->priv->parts = g_try_renew(ECM_Part, self->priv->parts, self->priv->allocated_parts);

        if (!self->priv->parts) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to allocate %d ECM parts!", self->priv->allocated_parts);
            return FALSE;
        }
    }

    /* Increase parts counter */
    self->priv->num_parts++;

    /* Check if we need to allocate more parts; if we do, double the
       number of allocated parts to avoid reallocating often */
    if (self->priv->num_parts > self->priv->allocated_parts) {
        self->priv->allocated_parts *= 2;
        self->priv->parts = g_try_renew(ECM_Part, self->priv->parts, self->priv->allocated_parts);

        if (!self->priv->parts) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to allocate %d ECM parts!", self->priv->allocated_parts);
            return FALSE;
        }
    }

    /* Fill in the new part */
    ECM_Part *new_part = &self->priv->parts[self->priv->num_parts-1];
    new_part->num = num;
    new_part->type = type;
    new_part->raw_offset = raw_offset;
    new_part->raw_size = raw_size;
    new_part->offset = offset;
    new_part->size = size;

    return TRUE;
}

static gboolean mirage_file_filter_ecm_build_index (MirageFileFilterEcm *self, GError **error)
{
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));

    gint8 type;
    guint32 num;

    goffset raw_offset;
    gsize raw_size;
    gsize size;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: building part index\n", __debug__);

    /* Position behind the signature */
    if (!g_seekable_seek(G_SEEKABLE(stream), 4, G_SEEK_SET, NULL, NULL)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to seek behind signature!");
        return FALSE;
    }

    while (1) {
        guint8 c;
        gint bits = 5;

        /* Read type and number of sectors */
        if (g_input_stream_read(G_INPUT_STREAM(stream), &c, sizeof(c), NULL, NULL) != sizeof(c)) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to read a byte!");
            return FALSE;
        }

        type = c & 3;
        num = (c >> 2) & 0x1F;

        while (c & 0x80) {
            if (g_input_stream_read(G_INPUT_STREAM(stream), &c, sizeof(c), NULL, NULL) != sizeof(c)) {
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to read a byte!");
                return FALSE;
            }

            if ( (bits > 31) || ((guint32)(c & 0x7F)) >= (((guint32)0x80000000LU) >> (bits-1)) ) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: corrupted ECM file; invalid sector count!\n", __debug__);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Corrupted ECM file; invalid sector count!");
                return FALSE;
            }
            num |= ((guint32)(c & 0x7F)) << bits;
            bits += 7;
        }

        /* End indicator */
        if (num == 0xFFFFFFFF) {
            break;
        }

        num++;

        /* Decode type */
        switch (type) {
            case 0: {
                raw_size = num;
                size = num;
                break;
            }
            case 1: {
                raw_size = num * (3+2048);
                size = num * 2352;
                break;
            }
            case 2: {
                raw_size = num * (4+2048);
                size = num * 2336;
                break;
            }
            case 3: {
                raw_size = num * (4+2324);
                size = num * 2336;
                break;
            }
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unhandled ECM part type %d!\n", __debug__, type);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Unhandled ECM part type %d!", type);
                return FALSE;
            }
        }

        /* Get raw offset, then skip raw data */
        raw_offset = g_seekable_tell(G_SEEKABLE(stream));
        if (!g_seekable_seek(G_SEEKABLE(stream), raw_size, G_SEEK_CUR, NULL, NULL)) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to seek over ECM part data!");
            return FALSE;
        }

        /* Append to list of parts */
        if (!mirage_file_filter_ecm_append_part(self, num, type, raw_offset, raw_size, self->priv->file_size, size, error)) {
            return FALSE;
        }

        /* Original file size */
        self->priv->file_size += size;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of parts: %d!\n", __debug__, self->priv->num_parts);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: original stream size: %ld!\n", __debug__, self->priv->file_size);

    /* At least one part must be present */
    if (!self->priv->num_parts) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: no parts in ECM file!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "No parts in ECM file!");
        return FALSE;
    }

    /* Release unused allocated parts */
    self->priv->parts = g_renew(ECM_Part, self->priv->parts, self->priv->num_parts);

    /* Set stream position to beginning */
    self->priv->cur_position = 0;
    self->priv->cur_part_idx = 0;
    self->priv->cur_part = &self->priv->parts[self->priv->cur_part_idx];

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: successfully built index\n", __debug__);

    return TRUE;
}



/**********************************************************************\
 *                              Seeking                               *
\**********************************************************************/
static inline gboolean is_within_part (goffset position, const ECM_Part *part, gboolean last_part)
{
    if (last_part) {
        return position >= part->offset;
    } else {
        return position >= part->offset && position < (part->offset + part->size);
    }
}


static gboolean mirage_file_filter_ecm_set_current_position (MirageFileFilterEcm *self, goffset new_position, GError **error)
{
    /* If new position matches current position, do nothing */
    if (new_position == self->priv->cur_position) {
        return TRUE;
    }

    /* Validate new position */
    if (new_position < 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Seek before beginning of file not allowed!");
        return FALSE;
    }

    /* Seeking beyond end of file appears to be supported by GFileInputStream, so we allow it as well */
    /*if (new_position > self->priv->file_size) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Seek beyond end of file not allowed!");
        return FALSE;
    }*/

    /* Find the corresponding part */
    if (is_within_part(new_position, self->priv->cur_part, FALSE)) {
        /* Position still within current part; nothing to do */
    } else if (is_within_part(new_position, &self->priv->parts[0], FALSE)) {
        /* Position within first part */
        self->priv->cur_part_idx = 0;
    } else if (is_within_part(new_position, &self->priv->parts[self->priv->num_parts-1], TRUE)) {
        /* Position within last part */
        self->priv->cur_part_idx = self->priv->num_parts - 1;
    } else {
        /* Seek part-by-part in appropriate direction (do not check first and last part, though) */
        if (new_position < self->priv->cur_position) {
            /* Seek backward */
            for (gint i = self->priv->cur_part_idx; i > 0; i--) {
                if (is_within_part(new_position, &self->priv->parts[i], FALSE)) {
                    self->priv->cur_part_idx = i;
                    break;
                }
            }
        } else {
            /* Seek foward */
            for (gint i = self->priv->cur_part_idx; i < self->priv->num_parts-1; i++) {
                if (is_within_part(new_position, &self->priv->parts[i], FALSE)) {
                    self->priv->cur_part_idx = i;
                    break;
                }
            }
        }
    }
    self->priv->cur_part = &self->priv->parts[self->priv->cur_part_idx];

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: position %ld (0x%lX) found in part #%d!\n", __debug__, new_position, new_position, self->priv->cur_part_idx);

    /* Set new position */
    self->priv->cur_position = new_position;

    return TRUE;
}


/**********************************************************************\
 *                         Reading from parts                         *
\**********************************************************************/
static gssize mirage_filter_ecm_read_single_block_from_part (MirageFileFilterEcm *self, guint8 *buffer, gsize count)
{
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));

    goffset part_offset, buffer_offset, stream_offset;
    gint block_idx;
    gssize read_len;

    gint block_size, raw_block_size, skip_bytes;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: current position: %ld (part #%d, type %d, offset: %ld, size: %ld, raw offset: %ld, raw size: %ld) -> read %d bytes\n", __debug__, self->priv->cur_position, self->priv->cur_part_idx, self->priv->cur_part->type, self->priv->cur_part->offset, self->priv->cur_part->size, self->priv->cur_part->raw_offset, self->priv->cur_part->raw_size, count);

    /* Make sure we're not at end of stream */
    if (self->priv->cur_position >= self->priv->file_size) {
        return 0;
    }

    /* Compute offset within part */
    part_offset = self->priv->cur_position - self->priv->cur_part->offset;
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: offset within part: %ld\n", __debug__, part_offset);

    /* Decode types */
    switch (self->priv->cur_part->type) {
        case ECM_RAW: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: part type: raw => reading raw bytes\n", __debug__);

            /* This one is different from others, because we read data directly */
            stream_offset = self->priv->cur_part->raw_offset + part_offset;

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: seeking to %ld (0x%lX) in underlying stream\n", __debug__, part_offset, stream_offset);
            if (!g_seekable_seek(G_SEEKABLE(stream), stream_offset, G_SEEK_SET, NULL, NULL)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to %ld in underlying stream!\n", __debug__, stream_offset);
                return -1;
            }

            /* Read all available data */
            count = MIN(count, self->priv->cur_part->size - part_offset);

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: reading %ld (0x%lX) bytes from underlying stream\n", __debug__, count, count);
            read_len = g_input_stream_read(G_INPUT_STREAM(stream), buffer, count, NULL, NULL);
            if (read_len != count) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read %ld bytes from underlying stream!\n", __debug__, count);
                return -1;
            }

            return read_len;
        }
        case ECM_MODE1_2352: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: part type: Mode 1 (2051 -> 2352)\n", __debug__);
            block_size = 2352;
            raw_block_size = 3+2048;
            skip_bytes = 0;
            break;
        }
        case ECM_MODE2_FORM1_2336: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: part type: Mode 2 Form 1 (2052 -> 2336)\n", __debug__);
            block_size = 2336;
            raw_block_size = 4+2048;
            skip_bytes = 16;
            break;
        }
        case ECM_MODE2_FORM2_2336: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: part type: Mode 2 Form 1 (2328 -> 2336)\n", __debug__);
            block_size = 2336;
            raw_block_size = 4+2324;
            skip_bytes = 16;
            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unhandled type %d!\n", __debug__, self->priv->cur_part->type);
            return -1;
        }
    }

    /* Compute the block number, and offset within buffer */
    block_idx = part_offset / block_size;
    buffer_offset = part_offset % block_size;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: reading from block %d; buffer offset: %d\n", __debug__, block_idx, buffer_offset);

    /* If this particular block in this particular part is not cached,
       read the data and reconstruct ECC/EDC */
    if (self->priv->cache_part_idx != self->priv->cur_part_idx || self->priv->cache_block_idx != block_idx) {
        /* Compute offset within underlying stream */
        stream_offset = self->priv->cur_part->raw_offset + block_idx*raw_block_size;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: seeking to %ld (0x%lX) in underlying stream\n", __debug__, part_offset, part_offset);
        if (!g_seekable_seek(G_SEEKABLE(stream), stream_offset, G_SEEK_SET, NULL, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to %ld in underlying stream!\n", __debug__, part_offset);
            return -1;
        }

        /* Read and reconstruct sector data */
        switch (self->priv->cur_part->type) {
            case ECM_MODE1_2352: {
                /* Read data */
                if (g_input_stream_read(G_INPUT_STREAM(stream), self->priv->cache_buffer+0x00C, 0x003, NULL, NULL) != 0x003) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read %ld bytes from underlying stream!\n", __debug__, 0x003);
                    return -1;
                }
                if (g_input_stream_read(G_INPUT_STREAM(stream), self->priv->cache_buffer+0x010, 0x800, NULL, NULL) != 0x800) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read %ld bytes from underlying stream!\n", __debug__, 0x800);
                    return -1;
                }

                /* Set sync pattern */
                memcpy(self->priv->cache_buffer, mirage_pattern_sync, sizeof(mirage_pattern_sync));

                /* Set mode byte in header */
                self->priv->cache_buffer[0x00F] = 1;

                /* Generate EDC */
                mirage_helper_sector_edc_ecc_compute_edc_block(self->priv->cache_buffer+0x000, 0x810, self->priv->cache_buffer+0x810);

                /* Generate ECC P/Q codes */
                mirage_helper_sector_edc_ecc_compute_ecc_block(self->priv->cache_buffer+0x00C, 86, 24, 2, 86, self->priv->cache_buffer+0x81C); /* P */
                mirage_helper_sector_edc_ecc_compute_ecc_block(self->priv->cache_buffer+0x00C, 52, 43, 86, 88, self->priv->cache_buffer+0x8C8); /* Q */

                break;
            }
            case ECM_MODE2_FORM1_2336: {
                /* Read data */
                if (g_input_stream_read(G_INPUT_STREAM(stream), self->priv->cache_buffer+0x014, 0x804, NULL, NULL) != 0x804) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read %ld bytes from underlying stream!\n", __debug__, 0x804);
                    return -1;
                }

                /* Make sure that header fields are zeroed out */
                memset(self->priv->cache_buffer+0x00C, 0, 4);

                /* Duplicate subheader */
                memcpy(self->priv->cache_buffer+0x010, self->priv->cache_buffer+0x014, 4);

                /* Generate EDC */
                mirage_helper_sector_edc_ecc_compute_edc_block(self->priv->cache_buffer+0x010, 0x808, self->priv->cache_buffer+0x818);

                /* Generate ECC P/Q codes */
                mirage_helper_sector_edc_ecc_compute_ecc_block(self->priv->cache_buffer+0x00C, 86, 24, 2, 86, self->priv->cache_buffer+0x81C); /* P */
                mirage_helper_sector_edc_ecc_compute_ecc_block(self->priv->cache_buffer+0x00C, 52, 43, 86, 88, self->priv->cache_buffer+0x8C8); /* Q */

                break;
            }
            case ECM_MODE2_FORM2_2336: {
                /* Read data */
                if (g_input_stream_read(G_INPUT_STREAM(stream), self->priv->cache_buffer+0x014, 0x918, NULL, NULL) != 0x918) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read %ld bytes from underlying stream!\n", __debug__, 0x918);
                    return -1;
                }

                /* Duplicate subheader */
                memcpy(self->priv->cache_buffer+0x010, self->priv->cache_buffer+0x014, 4);

                /* Generate EDC */
                mirage_helper_sector_edc_ecc_compute_edc_block(self->priv->cache_buffer+0x010, 0x91C, self->priv->cache_buffer+0x92C);

                break;
            }
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unhandled type %d!\n", __debug__, self->priv->cur_part->type);
                return -1;
            }
        }

        self->priv->cache_part_idx = self->priv->cur_part_idx;
        self->priv->cache_block_idx = block_idx;
    }

    /* Copy from cache buffer */
    read_len = MIN(count, block_size - buffer_offset);
    memcpy(buffer, self->priv->cache_buffer+skip_bytes+buffer_offset, read_len);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: block read complete (%d bytes)!\n", __debug__, read_len);

    return read_len;
}


/**********************************************************************\
 *              MirageFileFilter methods implementations             *
\**********************************************************************/
static gboolean mirage_file_filter_ecm_can_handle_data_format (MirageFileFilter *_self, GError **error)
{
    MirageFileFilterEcm *self = MIRAGE_FILE_FILTER_ECM(_self);
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));

    guint8 sig[4];

    /* Look for "ECM " signature at the beginning */
    g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);
    if (g_input_stream_read(G_INPUT_STREAM(stream), sig, sizeof(sig), NULL, NULL) != sizeof(sig)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to read 4 signature bytes!");
        return FALSE;
    }

    /* Check signature */
    if (memcmp(sig, ecm_signature, sizeof(ecm_signature))) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Filter cannot handle given data!");
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing the underlying stream data...\n", __debug__);

    /* Build index */
    if (!mirage_file_filter_ecm_build_index(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing failed!\n\n", __debug__);
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing completed successfully\n\n", __debug__);

    return TRUE;
}


static gssize mirage_file_filter_ecm_read (MirageFileFilter *_self, void *buffer, gsize count, GError **error)
{
    MirageFileFilterEcm *self = MIRAGE_FILE_FILTER_ECM(_self);

    gssize total_read, read_len;
    guint8 *ptr = buffer;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: read %ld (0x%lX) bytes from current position %ld (0x%lX)!\n", __debug__, count, count, self->priv->cur_position, self->priv->cur_position);

    /* Read until all is read */
    total_read = 0;

    while (count > 0) {
        /* Read a single block from current part */
        read_len = mirage_filter_ecm_read_single_block_from_part(self, ptr, count);

        if (read_len == -1) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read data block!\n", __debug__);
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to read a data block from ECM stream.");
            return -1;
        }

        ptr += read_len;
        total_read += read_len;
        count -= read_len;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: read %ld (0x%lX) bytes... %ld (0x%lX) remaining\n\n", __debug__, read_len, read_len, count, count);

        /* Update position */
        if (!mirage_file_filter_ecm_set_current_position(self, self->priv->cur_position+read_len, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to update position!\n", __debug__);
            return -1;
        }

        /* Check if we're at end of stream */
        if (self->priv->cur_position >= self->priv->file_size) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: end of stream reached!\n", __debug__);
            break;
        }
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: read complete\n", __debug__);

    return total_read;
}


static goffset mirage_file_filter_ecm_tell (MirageFileFilter *_self)
{
    MirageFileFilterEcm *self = MIRAGE_FILE_FILTER_ECM(_self);
    return self->priv->cur_position;
}

static gboolean mirage_file_filter_ecm_seek (MirageFileFilter *_self, goffset offset, GSeekType type, GError **error)
{
    MirageFileFilterEcm *self = MIRAGE_FILE_FILTER_ECM(_self);
    goffset new_position;

    /* Compute new position */
    switch (type) {
        case G_SEEK_SET: {
            new_position = offset;
            break;
        }
        case G_SEEK_CUR: {
            new_position = self->priv->cur_position + offset;
            break;
        }
        case G_SEEK_END: {
            new_position = self->priv->file_size + offset;
            break;
        }
        default: {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid seek type.");
            return FALSE;
        }
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: request for seek to %ld (0x%lX)\n", __debug__, new_position, new_position);

    /* Seek */
    if (!mirage_file_filter_ecm_set_current_position(self, new_position, error)) {
        return FALSE;
    }

    return TRUE;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE(MirageFileFilterEcm, mirage_file_filter_ecm, MIRAGE_TYPE_FILE_FILTER);

void mirage_file_filter_ecm_type_register (GTypeModule *type_module)
{
    return mirage_file_filter_ecm_register_type(type_module);
}


static void mirage_file_filter_ecm_init (MirageFileFilterEcm *self)
{
    self->priv = MIRAGE_FILE_FILTER_ECM_GET_PRIVATE(self);

    mirage_file_filter_generate_info(MIRAGE_FILE_FILTER(self),
        "FILTER-ECM",
        "ECM File Filter",
        "ECM'ified images",
        "application/x-ecm"
    );

    self->priv->file_size = 0;

    self->priv->cur_position = 0;
    self->priv->cur_part_idx = 0;
    self->priv->cur_part = NULL;

    self->priv->allocated_parts = 0;
    self->priv->num_parts = 0;
    self->priv->parts = NULL;

    self->priv->cache_part_idx = -1;
    self->priv->cache_block_idx = -1;
}

static void mirage_file_filter_ecm_finalize (GObject *gobject)
{
    MirageFileFilterEcm *self = MIRAGE_FILE_FILTER_ECM(gobject);

    g_free(self->priv->parts);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_file_filter_ecm_parent_class)->finalize(gobject);
}

static void mirage_file_filter_ecm_class_init (MirageFileFilterEcmClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageFileFilterClass *file_filter_class = MIRAGE_FILE_FILTER_CLASS(klass);

    gobject_class->finalize = mirage_file_filter_ecm_finalize;

    file_filter_class->can_handle_data_format = mirage_file_filter_ecm_can_handle_data_format;

    file_filter_class->read = mirage_file_filter_ecm_read;

    file_filter_class->tell = mirage_file_filter_ecm_tell;
    file_filter_class->seek = mirage_file_filter_ecm_seek;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageFileFilterEcmPrivate));
}

static void mirage_file_filter_ecm_class_finalize (MirageFileFilterEcmClass *klass G_GNUC_UNUSED)
{
}
