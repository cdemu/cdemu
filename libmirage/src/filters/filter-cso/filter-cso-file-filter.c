/*
 *  libMirage: CSO file filter: File filter object
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

#include "filter-cso.h"

#define __debug__ "CSO-FileFilter"

typedef struct
{
    goffset  offset;
    guint64  comp_size;
    gboolean raw;
} CSO_Part;

/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_FILE_FILTER_CSO_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FILE_FILTER_CSO, MirageFileFilterCsoPrivate))

struct _MirageFileFilterCsoPrivate
{
    ciso_header_t header;

    goffset cur_position;

    gint cur_part_idx;
    const CSO_Part *cur_part;

    /* Part list */
    CSO_Part *parts;
    gint num_parts;
    gint num_indices;

    /* Cache */
    gint cache_part_idx;
    guint8 *part_buffer;

    /* Zlib stream */
    z_stream zlib_stream;
};


/**********************************************************************\
 *                           Part indexing                            *
\**********************************************************************/
static gboolean mirage_file_filter_cso_read_index (MirageFileFilterCso *self, GError **error)
{
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));
    z_stream *zlib_stream = &self->priv->zlib_stream;

    ciso_header_t *header = &self->priv->header;
    gint ret;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: reading part index\n", __debug__);

    self->priv->num_parts = header->total_bytes / header->block_size;
    self->priv->num_indices = self->priv->num_parts + 1; /* Contains EOF offset */
    g_assert(header->total_bytes % header->block_size == 0);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: number of parts: %d!\n", __debug__, self->priv->num_parts);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: original stream size: %ld!\n", __debug__, header->total_bytes);

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
    if (!g_seekable_seek(G_SEEKABLE(stream), sizeof(ciso_header_t), G_SEEK_SET, NULL, NULL)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to seek to the beginning of index!");
        g_free(self->priv->parts);
        return FALSE;
    }

    /* Read and decode index */
    for (gint i = 0; i < self->priv->num_indices; i++) {
        guint32 buf;

        CSO_Part *cur_part = &self->priv->parts[i];

        ret = g_input_stream_read(G_INPUT_STREAM(stream), &buf, sizeof(buf), NULL, NULL);
        if (ret != sizeof(guint32)) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to read from index!");
            g_free(self->priv->parts);
            return FALSE;
        }

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
        g_free(self->priv->parts);
        return FALSE;
    }

    /* Allocate part cache */
    self->priv->part_buffer = g_try_malloc(header->block_size);
    if (!self->priv->part_buffer) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to allocate memory for part cache!");
        g_free(self->priv->parts);
        return FALSE;
    }

    /* Set stream position to beginning */
    self->priv->cur_position = 0;
    self->priv->cur_part_idx = 0;
    self->priv->cur_part = &self->priv->parts[self->priv->cur_part_idx];

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: successfully read index\n\n", __debug__);

    return TRUE;
}


/**********************************************************************\
 *                              Seeking                               *
\**********************************************************************/
static gboolean mirage_file_filter_cso_set_current_position (MirageFileFilterCso *self, goffset new_position, GError **error)
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
    /*if (new_position > self->priv->header.total_bytes) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Seek beyond end of file not allowed!");
        return FALSE;
    }*/

    /* Find the corresponding part */
    self->priv->cur_part_idx = new_position / self->priv->header.block_size;
    self->priv->cur_part = &self->priv->parts[self->priv->cur_part_idx];

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: position %ld (0x%lX) found in part #%d!\n", __debug__, new_position, new_position, self->priv->cur_part_idx);

    /* Set new position */
    self->priv->cur_position = new_position;

    return TRUE;
}


/**********************************************************************\
 *                         Reading from parts                         *
\**********************************************************************/
static gssize mirage_filter_cso_read_from_part (MirageFileFilterCso *self, guint8 *buffer, gsize count)
{
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: current position: %ld (part #%d, offset: %ld, size: %d) -> read %d bytes\n",
                 __debug__, self->priv->cur_position, self->priv->cur_part_idx, self->priv->cur_part->offset, self->priv->cur_part->comp_size, count);

    /* Make sure we're not at end of stream */
    if (self->priv->cur_position >= self->priv->header.total_bytes) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: read beyond end of stream, doing nothing!\n", __debug__);
        return 0;
    }

    /* If we do not have part in cache, uncompress it */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: currently cached part: #%d\n", __debug__, self->priv->cache_part_idx);
    if (self->priv->cur_part_idx != self->priv->cache_part_idx) {
        z_stream *zlib_stream = &self->priv->zlib_stream;
        const CSO_Part *part = self->priv->cur_part;

        goffset stream_offset;

        guint8 *chunk_buffer = g_try_malloc(self->priv->header.block_size);

        if (!chunk_buffer) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Failed to allocate memory.\n", __debug__);
            return -1;
        }

        gint ret;

        /* Offset in underlying stream */
        stream_offset = part->offset;

        /* Seek to the position */
        if (!g_seekable_seek(G_SEEKABLE(stream), stream_offset, G_SEEK_SET, NULL, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to %ld in underlying stream!\n", __debug__, stream_offset);
            g_free(chunk_buffer);
            return -1;
        }

        /* Read a part, either raw or compressed */
        if (part->raw) {
            /* Read uncompressed part */
            ret = g_input_stream_read(G_INPUT_STREAM(stream), self->priv->part_buffer, self->priv->header.block_size, NULL, NULL);
            if (ret == -1) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read %d bytes from underlying stream!", __debug__, self->priv->header.block_size);
                g_free(chunk_buffer);
                return -1;
            } else if (ret == 0) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: inexpectedly reached EOF!", __debug__);
                g_free(chunk_buffer);
                return -1;
            }
        } else {
            /* Reset inflate engine */
            ret = inflateReset2(zlib_stream, -15);
            if (ret != Z_OK) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to reset inflate engine!\n", __debug__);
                g_free(chunk_buffer);
                return -1;
            }

            /* Uncompress whole part */
            zlib_stream->avail_in = 0;
            zlib_stream->avail_out = self->priv->header.block_size;
            zlib_stream->next_out = self->priv->part_buffer;

            do {
                /* Read */
                if (!zlib_stream->avail_in) {
                    /* Read some compressed data */
                    ret = g_input_stream_read(G_INPUT_STREAM(stream), chunk_buffer, self->priv->header.block_size, NULL, NULL);
                    if (ret == -1) {
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read %d bytes from underlying stream!", __debug__, self->priv->header.block_size);
                        g_free(chunk_buffer);
                        return -1;
                    } else if (ret == 0) {
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: inexpectedly reached EOF!", __debug__);
                        g_free(chunk_buffer);
                        return -1;
                    }
                    zlib_stream->avail_in = ret;
                    zlib_stream->next_in = chunk_buffer;
                }

                /* Inflate */
                ret = inflate(zlib_stream, Z_NO_FLUSH);
                if (ret == Z_NEED_DICT || ret == Z_MEM_ERROR || ret == Z_DATA_ERROR) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to inflate part: %s!", __debug__, zlib_stream->msg);
                    g_free(chunk_buffer);
                    return -1;
                }
            } while (zlib_stream->avail_out);

            g_free(chunk_buffer);
        }
    }

    /* Set currently cached part */
    self->priv->cache_part_idx = self->priv->cur_part_idx;

    count = MIN(count, self->priv->header.block_size);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: copying %d bytes\n", __debug__, count);

    memcpy(buffer, self->priv->part_buffer, count);

    return count;
}


/**********************************************************************\
 *              MirageFileFilter methods implementations             *
\**********************************************************************/
static gboolean mirage_file_filter_cso_can_handle_data_format (MirageFileFilter *_self, GError **error)
{
    MirageFileFilterCso *self = MIRAGE_FILE_FILTER_CSO(_self);
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));

    ciso_header_t *header = &self->priv->header;

    /* Read CISO header */
    g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);
    if (g_input_stream_read(G_INPUT_STREAM(stream), header, sizeof(ciso_header_t), NULL, NULL) != sizeof(ciso_header_t)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to read CISO header!");
        return FALSE;
    }

    /* Validate CISO header */
    if (memcmp(&header->magic, "CISO", sizeof(gchar[4])) || header->version > 1 ||
        header->total_bytes == 0 || header->block_size == 0) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Filter cannot handle given data!");
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: Found valid CISO file. Using alignment: %d.\n", __debug__, 1 << header->idx_align);

    /* Read index */
    if (!mirage_file_filter_cso_read_index(self, error)) {
        return FALSE;
    }

    return TRUE;
}


static gssize mirage_file_filter_cso_read (MirageFileFilter *_self, void *buffer, gsize count, GError **error)
{
    MirageFileFilterCso *self = MIRAGE_FILE_FILTER_CSO(_self);

    gssize total_read, read_len;
    guint8 *ptr = buffer;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: read %ld (0x%lX) bytes from current position %ld (0x%lX)!\n", __debug__, count, count, self->priv->cur_position, self->priv->cur_position);

    /* Read until all is read */
    total_read = 0;

    while (count > 0) {
        /* Read a single block from current part */
        read_len = mirage_filter_cso_read_from_part(self, ptr, count);

        if (read_len == -1) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read data block!\n", __debug__);
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to read a data block from CSO stream.");
            return -1;
        }

        ptr += read_len;
        total_read += read_len;
        count -= read_len;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: read %ld (0x%lX) bytes... %ld (0x%lX) remaining\n\n", __debug__, read_len, read_len, count, count);

        /* Update position */
        if (!mirage_file_filter_cso_set_current_position(self, self->priv->cur_position+read_len, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to update position!\n", __debug__);
            return -1;
        }

        /* Check if we're at end of stream */
        if (self->priv->cur_position >= self->priv->header.total_bytes) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: end of stream reached!\n", __debug__);
            break;
        }
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: read complete\n", __debug__);

    return total_read;
}


static goffset mirage_file_filter_cso_tell (MirageFileFilter *_self)
{
    MirageFileFilterCso *self = MIRAGE_FILE_FILTER_CSO(_self);
    return self->priv->cur_position;
}

static gboolean mirage_file_filter_cso_seek (MirageFileFilter *_self, goffset offset, GSeekType type, GError **error)
{
    MirageFileFilterCso *self = MIRAGE_FILE_FILTER_CSO(_self);
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
            new_position = self->priv->header.total_bytes + offset;
            break;
        }
        default: {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid seek type.");
            return FALSE;
        }
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: request for seek to %ld (0x%lX)\n", __debug__, new_position, new_position);

    /* Seek */
    if (!mirage_file_filter_cso_set_current_position(self, new_position, error)) {
        return FALSE;
    }

    return TRUE;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE(MirageFileFilterCso, mirage_file_filter_cso, MIRAGE_TYPE_FILE_FILTER);

void mirage_file_filter_cso_type_register (GTypeModule *type_module)
{
    return mirage_file_filter_cso_register_type(type_module);
}


static void mirage_file_filter_cso_init (MirageFileFilterCso *self)
{
    self->priv = MIRAGE_FILE_FILTER_CSO_GET_PRIVATE(self);

    mirage_file_filter_generate_info(MIRAGE_FILE_FILTER(self),
        "FILTER-CSO",
        "CSO File Filter",
        "Compressed ISO images",
        "application/x-cso"
    );

    self->priv->cur_position = 0;
    self->priv->cur_part_idx = 0;
    self->priv->cur_part = NULL;

    self->priv->num_parts = 0;
    self->priv->parts = NULL;

    self->priv->cache_part_idx = -1;
    self->priv->part_buffer = NULL;
}

static void mirage_file_filter_cso_finalize (GObject *gobject)
{
    MirageFileFilterCso *self = MIRAGE_FILE_FILTER_CSO(gobject);

    g_free(self->priv->parts);
    g_free(self->priv->part_buffer);

    inflateEnd(&self->priv->zlib_stream);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_file_filter_cso_parent_class)->finalize(gobject);
}

static void mirage_file_filter_cso_class_init (MirageFileFilterCsoClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageFileFilterClass *file_filter_class = MIRAGE_FILE_FILTER_CLASS(klass);

    gobject_class->finalize = mirage_file_filter_cso_finalize;

    file_filter_class->can_handle_data_format = mirage_file_filter_cso_can_handle_data_format;

    file_filter_class->read = mirage_file_filter_cso_read;

    file_filter_class->tell = mirage_file_filter_cso_tell;
    file_filter_class->seek = mirage_file_filter_cso_seek;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageFileFilterCsoPrivate));
}

static void mirage_file_filter_cso_class_finalize (MirageFileFilterCsoClass *klass G_GNUC_UNUSED)
{
}
