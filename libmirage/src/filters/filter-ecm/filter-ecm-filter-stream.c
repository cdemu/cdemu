/*
 *  libMirage: ECM filter: Filter stream object
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "filter-ecm.h"

#define __debug__ "ECM-FilterStream"


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
#define MIRAGE_FILTER_STREAM_ECM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FILTER_STREAM_ECM, MirageFilterStreamEcmPrivate))

struct _MirageFilterStreamEcmPrivate
{
    /* Part list */
    ECM_Part *parts;
    gint num_parts;
    gint allocated_parts;

    /* Cache */
    gint cached_part;
    gint cached_block;
    guint8 buffer[2352];
};


/**********************************************************************\
 *                           Part indexing                            *
\**********************************************************************/
static gboolean mirage_filter_stream_ecm_append_part (MirageFilterStreamEcm *self, gint num, guint8 type, goffset raw_offset, gsize raw_size, goffset offset, gsize size, GError **error)
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

static gboolean mirage_filter_stream_ecm_build_index (MirageFilterStreamEcm *self, GError **error)
{
    MirageStream *stream = mirage_filter_stream_get_underlying_stream(MIRAGE_FILTER_STREAM(self));

    gint8 type;
    guint32 num;

    guint64 file_size = 0;

    goffset raw_offset;
    gsize raw_size;
    gsize size;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: building part index\n", __debug__);

    /* Position behind the signature */
    if (!mirage_stream_seek(stream, 4, G_SEEK_SET, NULL)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to seek behind signature!");
        return FALSE;
    }

    while (1) {
        guint8 c;
        gint bits = 5;

        /* Read type and number of sectors */
        if (mirage_stream_read(stream, &c, sizeof(c), NULL) != sizeof(c)) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to read a byte!");
            return FALSE;
        }

        type = c & 3;
        num = (c >> 2) & 0x1F;

        while (c & 0x80) {
            if (mirage_stream_read(stream, &c, sizeof(c), NULL) != sizeof(c)) {
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
        raw_offset = mirage_stream_tell(stream);
        if (!mirage_stream_seek(stream, raw_size, G_SEEK_CUR, NULL)) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to seek over ECM part data!");
            return FALSE;
        }

        /* Append to list of parts */
        if (!mirage_filter_stream_ecm_append_part(self, num, type, raw_offset, raw_size, file_size, size, error)) {
            return FALSE;
        }

        /* Original file size */
        file_size += size;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of parts: %d!\n", __debug__, self->priv->num_parts);

    /* At least one part must be present */
    if (!self->priv->num_parts) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: no parts in ECM file!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "No parts in ECM file!");
        return FALSE;
    }

    /* Release unused allocated parts */
    self->priv->parts = g_renew(ECM_Part, self->priv->parts, self->priv->num_parts);

    /* Store file size */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: file size: %lld (0x%llX)\n", __debug__, file_size, file_size);
    mirage_filter_stream_set_stream_length(MIRAGE_FILTER_STREAM(self), file_size);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: index building completed\n", __debug__);

    return TRUE;
}


/**********************************************************************\
 *              MirageFilterStream methods implementations            *
\**********************************************************************/
static gboolean mirage_filter_stream_ecm_open (MirageFilterStream *_self, MirageStream *stream, GError **error)
{
    MirageFilterStreamEcm *self = MIRAGE_FILTER_STREAM_ECM(_self);

    guint8 sig[4];

    /* Look for "ECM " signature at the beginning */
    mirage_stream_seek(stream, 0, G_SEEK_SET, NULL);
    if (mirage_stream_read(stream, sig, sizeof(sig), NULL) != sizeof(sig)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Filter cannot handle given data: failed to read 4 signature bytes!");
        return FALSE;
    }

    /* Check signature */
    if (memcmp(sig, ecm_signature, sizeof(ecm_signature))) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Filter cannot handle given data: invalid signature!");
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing the underlying stream data...\n", __debug__);

    /* Build index */
    if (!mirage_filter_stream_ecm_build_index(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing failed!\n\n", __debug__);
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing completed successfully\n\n", __debug__);

    return TRUE;
}


static gint mirage_filter_stream_ecm_find_part (MirageFilterStreamEcm *self, goffset position)
{
    const ECM_Part *part;
    gint part_index;

    /* Check if position is within currently cached part */
    part_index = self->priv->cached_part;
    if (part_index != -1 ) {
        part = &self->priv->parts[part_index];
        if (position >= part->offset && position < (part->offset + part->size)) {
            return part_index;
        }
    }

    /* Check if it's within the first part */
    part_index = 0;
    part = &self->priv->parts[part_index];
    if (position >= part->offset && position < (part->offset + part->size)) {
        return part_index;
    }

    /* Check if it's within the last part */
    part_index = self->priv->num_parts - 1;
    part = &self->priv->parts[part_index];
    if (position >= part->offset && position < (part->offset + part->size)) {
        return part_index;
    }

    /* Seek from currently cached part */
    part_index = (self->priv->cached_part != -1) ? self->priv->cached_part : 0;
    part = &self->priv->parts[part_index];

    if (position < part->offset) {
        /* Seek backward (first part has already been checked) */
        for (gint i = part_index; i > 0; i--) {
            part = &self->priv->parts[i];
            if (position >= part->offset) {
                return i;
            }
        }
    } else {
        /* Seek forward (last part has already been checked) */
        for (gint i = part_index; i < self->priv->num_parts - 1; i++) {
            part = &self->priv->parts[i];
            if (position < part->offset + part->size) {
                return i;
            }
        }
    }

    /* Part not found */
    return -1;
}

static gssize mirage_filter_stream_ecm_partial_read (MirageFilterStream *_self, void *buffer, gsize count)
{
    MirageFilterStreamEcm *self = MIRAGE_FILTER_STREAM_ECM(_self);
    MirageStream *stream = mirage_filter_stream_get_underlying_stream(_self);
    goffset position = mirage_filter_stream_get_position(_self);
    const ECM_Part *part;
    gint part_idx;

    goffset part_offset, stream_offset;

    gint block_idx;
    gint block_size, raw_block_size, skip_bytes;

    /* Find part that corresponds to current position */
    part_idx = mirage_filter_stream_ecm_find_part(self, position);
    if (part_idx == -1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: stream position %ld (0x%lX) beyond end of stream, doing nothing!\n", __debug__, position, position);
        return 0;
    }
    part = &self->priv->parts[part_idx];

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: stream position: %ld (0x%lX) -> part #%d\n", __debug__, position, position, part_idx);

    /* Compute offset within part */
    part_offset = position - part->offset;
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: offset within part: %ld\n", __debug__, part_offset);

    /* Decode part type */
    switch (part->type) {
        case ECM_RAW: {
            /* This one is different from others, because we read data directly */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: part type: raw => reading raw bytes\n", __debug__);

            /* Seek to appropriate position */
            stream_offset = part->raw_offset + part_offset;
            if (!mirage_stream_seek(stream, stream_offset, G_SEEK_SET, NULL)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to %ld in underlying stream!\n", __debug__, stream_offset);
                return -1;
            }

            /* Read all available data */
            count = MIN(count, part->size - part_offset);

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: reading %d bytes\n", __debug__, count);
            if (mirage_stream_read(stream, buffer, count, NULL) != count) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read %ld bytes from underlying stream!\n", __debug__, count);
                return -1;
            }

            return count;
        }
        case ECM_MODE1_2352: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: part type: Mode 1 (2051 -> 2352)\n", __debug__);
            block_size = 2352;
            raw_block_size = 3+2048;
            skip_bytes = 0;
            break;
        }
        case ECM_MODE2_FORM1_2336: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: part type: Mode 2 Form 1 (2052 -> 2336)\n", __debug__);
            block_size = 2336;
            raw_block_size = 4+2048;
            skip_bytes = 16;
            break;
        }
        case ECM_MODE2_FORM2_2336: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: part type: Mode 2 Form 1 (2328 -> 2336)\n", __debug__);
            block_size = 2336;
            raw_block_size = 4+2324;
            skip_bytes = 16;
            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unhandled type %d!\n", __debug__, part->type);
            return -1;
        }
    }

    /* Compute the block number within part */
    block_idx = part_offset / block_size;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: reading from block %d in part %d\n", __debug__, block_idx, part_idx);

    /* If this particular block in this particular part is not cached,
       read the data and reconstruct ECC/EDC */
    if (part_idx != self->priv->cached_part || block_idx != self->priv->cached_block) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: block not cached, reading...\n", __debug__);

        /* Compute offset within underlying stream */
        stream_offset = part->raw_offset + block_idx*raw_block_size;

        if (!mirage_stream_seek(stream, stream_offset, G_SEEK_SET, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to %ld in underlying stream!\n", __debug__, part_offset);
            return -1;
        }

        /* Read and reconstruct sector data */
        switch (part->type) {
            case ECM_MODE1_2352: {
                /* Read data */
                if (mirage_stream_read(stream, self->priv->buffer+0x00C, 0x003, NULL) != 0x003) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read %ld bytes from underlying stream!\n", __debug__, 0x003);
                    return -1;
                }
                if (mirage_stream_read(stream, self->priv->buffer+0x010, 0x800, NULL) != 0x800) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read %ld bytes from underlying stream!\n", __debug__, 0x800);
                    return -1;
                }

                /* Set sync pattern */
                memcpy(self->priv->buffer, mirage_pattern_sync, sizeof(mirage_pattern_sync));

                /* Set mode byte in header */
                self->priv->buffer[0x00F] = 1;

                /* Generate EDC */
                mirage_helper_sector_edc_ecc_compute_edc_block(self->priv->buffer+0x000, 0x810, self->priv->buffer+0x810);

                /* Generate ECC P/Q codes */
                mirage_helper_sector_edc_ecc_compute_ecc_block(self->priv->buffer+0x00C, 86, 24, 2, 86, self->priv->buffer+0x81C); /* P */
                mirage_helper_sector_edc_ecc_compute_ecc_block(self->priv->buffer+0x00C, 52, 43, 86, 88, self->priv->buffer+0x8C8); /* Q */

                break;
            }
            case ECM_MODE2_FORM1_2336: {
                /* Read data */
                if (mirage_stream_read(stream, self->priv->buffer+0x014, 0x804, NULL) != 0x804) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read %ld bytes from underlying stream!\n", __debug__, 0x804);
                    return -1;
                }

                /* Make sure that header fields are zeroed out */
                memset(self->priv->buffer+0x00C, 0, 4);

                /* Duplicate subheader */
                memcpy(self->priv->buffer+0x010, self->priv->buffer+0x014, 4);

                /* Generate EDC */
                mirage_helper_sector_edc_ecc_compute_edc_block(self->priv->buffer+0x010, 0x808, self->priv->buffer+0x818);

                /* Generate ECC P/Q codes */
                mirage_helper_sector_edc_ecc_compute_ecc_block(self->priv->buffer+0x00C, 86, 24, 2, 86, self->priv->buffer+0x81C); /* P */
                mirage_helper_sector_edc_ecc_compute_ecc_block(self->priv->buffer+0x00C, 52, 43, 86, 88, self->priv->buffer+0x8C8); /* Q */

                break;
            }
            case ECM_MODE2_FORM2_2336: {
                /* Read data */
                if (mirage_stream_read(stream, self->priv->buffer+0x014, 0x918, NULL) != 0x918) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read %ld bytes from underlying stream!\n", __debug__, 0x918);
                    return -1;
                }

                /* Duplicate subheader */
                memcpy(self->priv->buffer+0x010, self->priv->buffer+0x014, 4);

                /* Generate EDC */
                mirage_helper_sector_edc_ecc_compute_edc_block(self->priv->buffer+0x010, 0x91C, self->priv->buffer+0x92C);

                break;
            }
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unhandled type %d!\n", __debug__, part->type);
                return -1;
            }
        }

        /* Set currently cached block and part */
        self->priv->cached_part = part_idx;
        self->priv->cached_block = block_idx;
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: block already cached\n", __debug__);
    }

    /* Copy data */
    gint block_offset = part_offset % block_size;
    count = MIN(count, block_size - block_offset);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: offset within block: %ld, copying %d bytes\n", __debug__, block_offset, count);

    memcpy(buffer, self->priv->buffer + skip_bytes + block_offset, count);

    return count;
}



/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE(MirageFilterStreamEcm, mirage_filter_stream_ecm, MIRAGE_TYPE_FILTER_STREAM);

void mirage_filter_stream_ecm_type_register (GTypeModule *type_module)
{
    return mirage_filter_stream_ecm_register_type(type_module);
}


static void mirage_filter_stream_ecm_init (MirageFilterStreamEcm *self)
{
    self->priv = MIRAGE_FILTER_STREAM_ECM_GET_PRIVATE(self);

    mirage_filter_stream_generate_info(MIRAGE_FILTER_STREAM(self),
        "FILTER-ECM",
        "ECM File Filter",
        FALSE,
        1,
        "ECM'ified images (*.ecm)", "application/x-ecm"
    );

    self->priv->allocated_parts = 0;
    self->priv->num_parts = 0;
    self->priv->parts = NULL;

    self->priv->cached_part = -1;
    self->priv->cached_block = -1;
}

static void mirage_filter_stream_ecm_finalize (GObject *gobject)
{
    MirageFilterStreamEcm *self = MIRAGE_FILTER_STREAM_ECM(gobject);

    g_free(self->priv->parts);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_filter_stream_ecm_parent_class)->finalize(gobject);
}

static void mirage_filter_stream_ecm_class_init (MirageFilterStreamEcmClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageFilterStreamClass *filter_stream_class = MIRAGE_FILTER_STREAM_CLASS(klass);

    gobject_class->finalize = mirage_filter_stream_ecm_finalize;

    filter_stream_class->open = mirage_filter_stream_ecm_open;

    filter_stream_class->partial_read = mirage_filter_stream_ecm_partial_read;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageFilterStreamEcmPrivate));
}

static void mirage_filter_stream_ecm_class_finalize (MirageFilterStreamEcmClass *klass G_GNUC_UNUSED)
{
}
