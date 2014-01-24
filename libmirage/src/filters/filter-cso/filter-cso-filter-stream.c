/*
 *  libMirage: CSO filter: Filter stream object
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

#include "filter-cso.h"

#define __debug__ "CSO-FilterStream"

typedef struct
{
    goffset  offset;
    guint64  comp_size;
    gboolean raw;
} CSO_Part;

static const guint8 ciso_signature[4] = { 'C', 'I', 'S', 'O' };


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_FILTER_STREAM_CSO_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FILTER_STREAM_CSO, MirageFilterStreamCsoPrivate))

struct _MirageFilterStreamCsoPrivate
{
    ciso_header_t header;

    /* Part list */
    CSO_Part *parts;
    gint num_parts;
    gint num_indices;

    /* Inflate buffer */
    guint8 *inflate_buffer;
    gint inflate_buffer_size;
    gint cached_part;

    /* I/O buffer */
    guint8 *io_buffer;
    gint io_buffer_size;

    /* Zlib stream */
    z_stream zlib_stream;
};


/**********************************************************************\
 *                           Part indexing                            *
\**********************************************************************/
static gboolean mirage_filter_stream_cso_read_index (MirageFilterStreamCso *self, GError **error)
{
    MirageStream *stream = mirage_filter_stream_get_underlying_stream(MIRAGE_FILTER_STREAM(self));
    z_stream *zlib_stream = &self->priv->zlib_stream;

    ciso_header_t *header = &self->priv->header;
    gint ret;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading part index\n", __debug__);

    self->priv->num_parts = header->total_bytes / header->block_size;
    self->priv->num_indices = self->priv->num_parts + 1; /* Contains EOF offset */
    g_assert(header->total_bytes % header->block_size == 0);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of parts: %d\n", __debug__, self->priv->num_parts);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: original stream size: %ld\n", __debug__, header->total_bytes);

    /* At least one part must be present */
    if (!self->priv->num_parts) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: no parts in CSO file!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "No parts in CSO file!");
        return FALSE;
    }

    /* Allocate part index */
    self->priv->parts = g_try_new(CSO_Part, self->priv->num_indices);
    if (!self->priv->parts) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to allocate memory for index!");
        return FALSE;
    }

    /* Position at the beginning of the index */
    if (!mirage_stream_seek(stream, sizeof(ciso_header_t), G_SEEK_SET, NULL)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to seek to the beginning of index!");
        return FALSE;
    }

    /* Read and decode index */
    for (gint i = 0; i < self->priv->num_indices; i++) {
        guint32 buf;

        CSO_Part *cur_part = &self->priv->parts[i];

        /* Read index entry */
        ret = mirage_stream_read(stream, &buf, sizeof(buf), NULL);
        if (ret != sizeof(guint32)) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to read from index!");
            return FALSE;
        }

        /* Fixup endianness */
        buf = GUINT32_FROM_LE(buf);

        /* Calculate part info */
        cur_part->offset = (buf & 0x7FFFFFFF) << header->idx_align;
        cur_part->raw = buf >> 31;
        if (i > 0) {
            CSO_Part *prev_part = &self->priv->parts[i-1];

            prev_part->comp_size = cur_part->offset - prev_part->offset;
        }
    }

    /* EOF index has no size */
    self->priv->parts[self->priv->num_indices - 1].comp_size = 0;

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

    /* Allocate inflate buffer */
    self->priv->inflate_buffer_size = header->block_size;
    self->priv->inflate_buffer = g_try_malloc(self->priv->inflate_buffer_size);
    if (!self->priv->inflate_buffer) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to allocate memory for inflate buffer!");
        return FALSE;
    }

    /* Allocate I/O buffer */
    self->priv->io_buffer_size = header->block_size;
    self->priv->io_buffer = g_try_malloc(self->priv->io_buffer_size);
    if (!self->priv->io_buffer) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to allocate memory for I/O buffer!");
        return FALSE;
    }

    /* Set file size */
    mirage_filter_stream_simplified_set_stream_length(MIRAGE_FILTER_STREAM(self), header->total_bytes);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: successfully read index\n\n", __debug__);

    return TRUE;
}


/**********************************************************************\
 *              MirageFilterStream methods implementations            *
\**********************************************************************/
static void mirage_filter_stream_fixup_header(MirageFilterStreamCso *self)
{
    ciso_header_t *header = &self->priv->header;

    header->header_size = GUINT32_FROM_LE(header->header_size);
    header->total_bytes = GUINT64_FROM_LE(header->total_bytes);
    header->block_size  = GUINT32_FROM_LE(header->block_size);
}

static gboolean mirage_filter_stream_cso_open (MirageFilterStream *_self, MirageStream *stream, gboolean writable G_GNUC_UNUSED, GError **error)
{
    MirageFilterStreamCso *self = MIRAGE_FILTER_STREAM_CSO(_self);

    ciso_header_t *header = &self->priv->header;

    /* Read CISO header */
    mirage_stream_seek(stream, 0, G_SEEK_SET, NULL);
    if (mirage_stream_read(stream, header, sizeof(ciso_header_t), NULL) != sizeof(ciso_header_t)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Filter cannot handle given data: failed to read CISO header!");
        return FALSE;
    }

    /* Fixup header endianness */
    mirage_filter_stream_fixup_header(self);

    /* Validate CISO header */
    if (memcmp(&header->magic, ciso_signature, sizeof(ciso_signature)) || header->version > 1 ||
        header->total_bytes == 0 || header->block_size == 0) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Filter cannot handle given data: invalid header!");
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing the underlying stream data...\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CISO file alignment: %d.\n", __debug__, 1 << header->idx_align);

    /* Read index */
    if (!mirage_filter_stream_cso_read_index(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing failed!\n\n", __debug__);
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing completed successfully\n\n", __debug__);

    return TRUE;
}


static gssize mirage_filter_stream_cso_partial_read (MirageFilterStream *_self, void *buffer, gsize count)
{
    MirageFilterStreamCso *self = MIRAGE_FILTER_STREAM_CSO(_self);
    MirageStream *stream = mirage_filter_stream_get_underlying_stream(_self);
    goffset position = mirage_filter_stream_simplified_get_position(_self);
    gint part_idx;

    /* Find part that corresponds tho current position */
    part_idx = position / self->priv->header.block_size;

    if (part_idx >= self->priv->num_parts) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: stream position %ld (0x%lX) beyond end of stream, doing nothing!\n", __debug__, position, position);
        return 0;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: stream position: %ld (0x%lX) -> part #%d (cached: #%d)\n", __debug__, position, part_idx, self->priv->cached_part);

    /* If we do not have part in cache, uncompress it */
    if (part_idx != self->priv->cached_part) {
        const CSO_Part *part = &self->priv->parts[part_idx];
        z_stream *zlib_stream = &self->priv->zlib_stream;
        gint ret;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: part not cached, reading...\n", __debug__);

        /* Seek to the position */
        if (!mirage_stream_seek(stream, part->offset, G_SEEK_SET, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to %ld in underlying stream!\n", __debug__, part->offset);
            return -1;
        }

        /* Read a part, either raw or compressed */
        if (part->raw) {
            /* Read uncompressed part */
            ret = mirage_stream_read(stream, self->priv->inflate_buffer, self->priv->inflate_buffer_size, NULL);
            if (ret == -1) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read %d bytes from underlying stream!\n", __debug__, self->priv->inflate_buffer_size);
                return -1;
            } else if (ret == 0) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unexpectedly reached EOF!\n", __debug__);
                return -1;
            }
        } else {
            /* Reset inflate engine */
            ret = inflateReset2(zlib_stream, -15);
            if (ret != Z_OK) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to reset inflate engine!\n", __debug__);
                return -1;
            }

            /* Uncompress whole part */
            zlib_stream->avail_in = 0;
            zlib_stream->avail_out = self->priv->inflate_buffer_size;
            zlib_stream->next_out = self->priv->inflate_buffer;

            do {
                /* Read */
                if (!zlib_stream->avail_in) {
                    /* Read some compressed data */
                    ret = mirage_stream_read(stream, self->priv->io_buffer, part->comp_size, NULL);
                    if (ret == -1) {
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read %d bytes from underlying stream!\n", __debug__, self->priv->io_buffer_size);
                        return -1;
                    } else if (ret == 0) {
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unexpectedly reached EOF\n!", __debug__);
                        return -1;
                    }
                    zlib_stream->avail_in = ret;
                    zlib_stream->next_in = self->priv->io_buffer;
                }

                /* Inflate */
                ret = inflate(zlib_stream, Z_NO_FLUSH);
                if (ret == Z_NEED_DICT || ret == Z_MEM_ERROR || ret == Z_DATA_ERROR) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to inflate part: %s\n!", __debug__, zlib_stream->msg);
                    return -1;
                }
            } while (zlib_stream->avail_out);
        }

        /* Set currently cached part */
        self->priv->cached_part = part_idx;
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: part already cached\n", __debug__);
    }


    /* Copy data */
    goffset part_offset = position % self->priv->header.block_size;
    count = MIN(count, self->priv->header.block_size - part_offset);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: offset within part: %ld, copying %d bytes\n", __debug__, part_offset, count);

    memcpy(buffer, &self->priv->inflate_buffer[part_offset], count);

    return count;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE(MirageFilterStreamCso, mirage_filter_stream_cso, MIRAGE_TYPE_FILTER_STREAM);

void mirage_filter_stream_cso_type_register (GTypeModule *type_module)
{
    return mirage_filter_stream_cso_register_type(type_module);
}


static void mirage_filter_stream_cso_init (MirageFilterStreamCso *self)
{
    self->priv = MIRAGE_FILTER_STREAM_CSO_GET_PRIVATE(self);

    mirage_filter_stream_generate_info(MIRAGE_FILTER_STREAM(self),
        "FILTER-CSO",
        "CSO File Filter",
        FALSE,
        1,
        "Compressed ISO images (*.ciso, *.cso)", "application/x-cso"
    );

    self->priv->num_parts = 0;
    self->priv->parts = NULL;

    self->priv->cached_part = -1;
    self->priv->inflate_buffer = NULL;
    self->priv->io_buffer = NULL;
}

static void mirage_filter_stream_cso_finalize (GObject *gobject)
{
    MirageFilterStreamCso *self = MIRAGE_FILTER_STREAM_CSO(gobject);

    g_free(self->priv->parts);
    g_free(self->priv->inflate_buffer);
    g_free(self->priv->io_buffer);

    inflateEnd(&self->priv->zlib_stream);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_filter_stream_cso_parent_class)->finalize(gobject);
}

static void mirage_filter_stream_cso_class_init (MirageFilterStreamCsoClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageFilterStreamClass *filter_stream_class = MIRAGE_FILTER_STREAM_CLASS(klass);

    gobject_class->finalize = mirage_filter_stream_cso_finalize;

    filter_stream_class->open = mirage_filter_stream_cso_open;

    filter_stream_class->simplified_partial_read = mirage_filter_stream_cso_partial_read;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageFilterStreamCsoPrivate));
}

static void mirage_filter_stream_cso_class_finalize (MirageFilterStreamCsoClass *klass G_GNUC_UNUSED)
{
}
