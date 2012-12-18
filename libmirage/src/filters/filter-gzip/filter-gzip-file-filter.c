/*
 *  libMirage: GZIP file filter: File filter object
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

#include "filter-gzip.h"

#define __debug__ "GZIP-FileFilter"

/* The index building code is based on zran.c, which comes with zlib */

#define SPAN 1048576 /* desired distance between access points - 1024 kB */
#define WINSIZE 32768 /* sliding window size - 32 kB */
#define CHUNKSIZE 16384 /* file input buffer size - 16 kB */

typedef struct
{
    goffset offset;
    gint size;

    goffset raw_offset;
    gint bits;
    guint8 window[WINSIZE];
} GZIP_Part;


static const guint8 gzip_signature[2] = { 0x1F, 0x8B };


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_FILE_FILTER_GZIP_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FILE_FILTER_GZIP, MirageFileFilterGzipPrivate))

struct _MirageFileFilterGzipPrivate
{
    /* I/O buffer */
    guint8 *io_buffer;
    gint io_buffer_size;

    guint8 *window_buffer;
    gint window_buffer_size;

    /* Part list */
    GZIP_Part *parts;
    gint num_parts;
    gint allocated_parts;

    /* Cache */
    gint cached_part;
    guint8 *part_buffer;
    gint part_buffer_size;

    /* Zlib stream */
    z_stream zlib_stream;
};


/**********************************************************************\
 *                           Part indexing                            *
\**********************************************************************/
static gboolean mirage_file_filter_gzip_compute_part_sizes (MirageFileFilterGzip *self, guint64 file_size, GError **error)
{
    GZIP_Part *part;
    gint max_size = 0;

    /* Compute sizes for all parts but last one, based on their offsets */
    for (gint i = 0; i < self->priv->num_parts - 1; i++) {
        part = &self->priv->parts[i];

        part->size = (part+1)->offset - part->offset;
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: part #%d: offset: %lld, size: %lld\n", __debug__, i, part->offset, part->size);

        max_size = MAX(max_size, part->size);
    }

    /* Last part size */
    part = &self->priv->parts[self->priv->num_parts-1];
    part->size = file_size - part->offset;
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: part #%d: offset: %lld, size: %lld\n", __debug__, self->priv->num_parts-1, part->offset, part->size);

    max_size = MAX(max_size, part->size);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: largest part size: %lld\n", __debug__, max_size);

    /* Allocate part buffer */
    self->priv->part_buffer_size = max_size;
    self->priv->part_buffer = g_try_malloc(self->priv->part_buffer_size);
    if (!self->priv->part_buffer) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to allocate part buffer (%d bytes)!", self->priv->part_buffer_size);
        return FALSE;
    }

    return TRUE;
}

static gboolean mirage_file_filter_gzip_append_part (MirageFileFilterGzip *self, gint bits, goffset raw_offset, goffset offset, gint left, guint8 *window, GError **error)
{
    /* If no parts have been allocated yet, do so now; start with eight */
    if (!self->priv->allocated_parts) {
        self->priv->allocated_parts = 8;
        self->priv->parts = g_try_renew(GZIP_Part, self->priv->parts, self->priv->allocated_parts);

        if (!self->priv->parts) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to allocate %d GZIP parts!", self->priv->allocated_parts);
            return FALSE;
        }
    }

    /* Increase parts counter */
    self->priv->num_parts++;

    /* Check if we need to allocate more parts; if we do, double the
       number of allocated parts to avoid reallocating often */
    if (self->priv->num_parts > self->priv->allocated_parts) {
        self->priv->allocated_parts *= 2;
        self->priv->parts = g_try_renew(GZIP_Part, self->priv->parts, self->priv->allocated_parts);

        if (!self->priv->parts) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to allocate %d GZIP parts!", self->priv->allocated_parts);
            return FALSE;
        }
    }

    /* Fill in the new part */
    GZIP_Part *new_part = &self->priv->parts[self->priv->num_parts-1];
    new_part->bits = bits;
    new_part->offset = offset;
    new_part->raw_offset = raw_offset;
    if (left) {
        memcpy(new_part->window, window + WINSIZE - left, left);
    }
    if (left < WINSIZE) {
        memcpy(new_part->window + left, window, WINSIZE - left);
    }

    return TRUE;
}

static gboolean mirage_file_filter_gzip_build_index (MirageFileFilterGzip *self, GError **error)
{
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));
    z_stream *zlib_stream = &self->priv->zlib_stream;

    goffset totalIn, totalOut, last;
    gint ret;


    /* Allocate I/O and window buffer */
    self->priv->io_buffer_size = CHUNKSIZE;
    self->priv->io_buffer = g_try_malloc(self->priv->io_buffer_size);

    self->priv->window_buffer_size = WINSIZE;
    self->priv->window_buffer = g_try_malloc(self->priv->window_buffer_size);

    if (!self->priv->io_buffer || !self->priv->window_buffer) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to allocate buffers!");
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: building part index\n", __debug__);

    /* Initialize zlib stream */
    zlib_stream->zalloc = Z_NULL;
    zlib_stream->zfree = Z_NULL;
    zlib_stream->opaque = Z_NULL;
    zlib_stream->avail_in = 0;
    zlib_stream->next_in = Z_NULL;

    ret = inflateInit2(zlib_stream, 47); /* 47 = automatic zlib/gzip decoding */

    if (ret != Z_OK) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to initialize zlib's inflate (error: %d)!", ret);
        return FALSE;
    }

    /* Position at the beginning of the stream */
    if (!g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to seek to the beginning of stream!");
        return FALSE;
    }

    /* Inflate the input and build an index */
    totalIn = totalOut = last = 0;
    zlib_stream->avail_out = 0;
    do {
        /* Read some compressed data */
        ret = g_input_stream_read(stream, self->priv->io_buffer, self->priv->io_buffer_size, NULL, NULL);
        if (ret == -1) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to read %d bytes from underlying stream!", CHUNKSIZE);
            return FALSE;
        } else if (ret == 0) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Unexpectedly reached EOF!");
            return FALSE;
        }

        zlib_stream->avail_in = ret;
        zlib_stream->next_in = self->priv->io_buffer;

        /* Process read data */
        do {
            /* Reset sliding window if neccessary */
            if (!zlib_stream->avail_out) {
                zlib_stream->avail_out = self->priv->window_buffer_size;
                zlib_stream->next_out = self->priv->window_buffer;
            }

            /* Inflate until end of input or output, or until end of block */
            totalIn += zlib_stream->avail_in;
            totalOut += zlib_stream->avail_out;

            ret = inflate(zlib_stream, Z_BLOCK);

            totalIn -= zlib_stream->avail_in;
            totalOut -= zlib_stream->avail_out;

            if (ret == Z_NEED_DICT || ret == Z_MEM_ERROR || ret == Z_DATA_ERROR) {
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to inflate!");
                return FALSE;
            }
            if (ret == Z_STREAM_END) {
                break;
            }

            /* If at end of block, add part entry */
            if ((zlib_stream->data_type & 128) && !(zlib_stream->data_type & 64) &&
                (totalOut == 0 || totalOut - last > SPAN)) {

                if (!mirage_file_filter_gzip_append_part(self, zlib_stream->data_type & 7, totalIn, totalOut, zlib_stream->avail_out, self->priv->window_buffer, error)) {
                    return FALSE;
                }

                last = totalOut;
            }
        } while (zlib_stream->avail_in);
    } while (ret != Z_STREAM_END);

    /* At least one part must be present */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of parts: %d\n", __debug__, self->priv->num_parts);
    if (!self->priv->num_parts) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: no parts in GZIP file!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "No parts in GZIP file!");
        return FALSE;
    }

    /* Release unused allocated parts */
    self->priv->parts = g_renew(GZIP_Part, self->priv->parts, self->priv->num_parts);

    /* Compute sizes of parts and allocate part buffer */
    if (!mirage_file_filter_gzip_compute_part_sizes(self, totalOut, error)) {
        return FALSE;
    }

    /* Store file size (= totalOut) */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: file size: %lld (0x%llX)\n", __debug__, totalOut, totalOut);
    mirage_file_filter_set_file_size(MIRAGE_FILE_FILTER(self), totalOut);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: index building completed\n", __debug__);

    return TRUE;
}


/**********************************************************************\
 *              MirageFileFilter methods implementations             *
\**********************************************************************/
static gboolean mirage_file_filter_gzip_can_handle_data_format (MirageFileFilter *_self, GError **error)
{
    MirageFileFilterGzip *self = MIRAGE_FILE_FILTER_GZIP(_self);
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));

    guint8 sig[2];

    /* Look for gzip signature at the beginning */
    g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);
    if (g_input_stream_read(stream, sig, sizeof(sig), NULL, NULL) != sizeof(sig)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Filter cannot handle given data: failed to read 4 signature bytes!");
        return FALSE;
    }

    /* Check signature */
    if (memcmp(sig, gzip_signature, sizeof(gzip_signature))) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Filter cannot handle given data: invalid signature!");
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing the underlying stream data...\n", __debug__);

    /* Build index */
    if (!mirage_file_filter_gzip_build_index(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing failed!\n\n", __debug__);
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing completed successfully\n\n", __debug__);

    return TRUE;
}


static gint mirage_file_filter_gzip_find_part (MirageFileFilterGzip *self, goffset position)
{
    const GZIP_Part *part;
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

static gssize mirage_file_filter_gzip_partial_read (MirageFileFilter *_self, void *buffer, gsize count)
{
    MirageFileFilterGzip *self = MIRAGE_FILE_FILTER_GZIP(_self);
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));
    goffset position = mirage_file_filter_get_position(MIRAGE_FILE_FILTER(self));
    const GZIP_Part *part;
    gint part_idx;

    /* Find part that corresponds to current position */
    part_idx = mirage_file_filter_gzip_find_part(self, position);
    if (part_idx == -1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: stream position %ld (0x%lX) beyond end of stream, doing nothing!\n", __debug__, position, position);
        return 0;
    }
    part = &self->priv->parts[part_idx];

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: stream position: %ld (0x%lX) -> part #%d (cached: #%d)\n", __debug__, position, position, part_idx, self->priv->cached_part);

    /* If we do not have part in cache, uncompress it */
    if (part_idx != self->priv->cached_part) {
        z_stream *zlib_stream = &self->priv->zlib_stream;
        goffset underlying_stream_offset;
        gint ret;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: part not cached, reading...\n", __debug__);

        /* Offset in underlying stream */
        underlying_stream_offset = part->raw_offset;
        if (part->bits) {
            underlying_stream_offset--;
        }

        /* Seek to the position */
        if (!g_seekable_seek(G_SEEKABLE(stream), underlying_stream_offset, G_SEEK_SET, NULL, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to %ld in underlying stream!\n", __debug__, underlying_stream_offset);
            return -1;
        }

        /* Reset inflate engine */
        ret = inflateReset2(zlib_stream, -15); /* -15 = raw inflate */
        if (ret != Z_OK) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to reset inflate engine!\n", __debug__);
            return -1;
        }

        /* Initialize inflate on the part */
        if (part->bits) {
            guint8 value;
            ret = g_input_stream_read(stream, &value, sizeof(value), NULL, NULL);
            if (ret != sizeof(value)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read bits!\n", __debug__);
                return -1;
            }
            inflatePrime(zlib_stream, part->bits, value >> (8 - part->bits));
        }
        inflateSetDictionary(zlib_stream, part->window, WINSIZE);

        /* Uncompress whole part */
        zlib_stream->avail_in = 0;
        zlib_stream->avail_out = part->size;
        zlib_stream->next_out = self->priv->part_buffer;

        do {
            /* Read */
            if (!zlib_stream->avail_in) {
                /* Read some compressed data */
                ret = g_input_stream_read(stream, self->priv->io_buffer, self->priv->io_buffer_size, NULL, NULL);
                if (ret == -1) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read %d bytes from underlying stream!", __debug__, CHUNKSIZE);
                    return -1;
                } else if (ret == 0) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: inexpectedly reached EOF!", __debug__);
                    return -1;
                }
                zlib_stream->avail_in = ret;
                zlib_stream->next_in = self->priv->io_buffer;
            }

            /* Inflate */
            ret = inflate(zlib_stream, Z_NO_FLUSH);
            if (ret == Z_NEED_DICT || ret == Z_MEM_ERROR || ret == Z_DATA_ERROR) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to inflate part: %s!", __debug__, zlib_stream->msg);
                return -1;
            }
        } while (zlib_stream->avail_out);


        /* Set currently cached part */
        self->priv->cached_part = part_idx;
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: part already cached\n", __debug__);
    }

    /* Copy data */
    goffset part_offset = position - part->offset;
    count = MIN(count, part->size - part_offset);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: offset within part: %ld, copying %d bytes\n", __debug__, part_offset, count);

    memcpy(buffer, self->priv->part_buffer + part_offset, count);

    return count;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE(MirageFileFilterGzip, mirage_file_filter_gzip, MIRAGE_TYPE_FILE_FILTER);

void mirage_file_filter_gzip_type_register (GTypeModule *type_module)
{
    return mirage_file_filter_gzip_register_type(type_module);
}


static void mirage_file_filter_gzip_init (MirageFileFilterGzip *self)
{
    self->priv = MIRAGE_FILE_FILTER_GZIP_GET_PRIVATE(self);

    mirage_file_filter_generate_info(MIRAGE_FILE_FILTER(self),
        "FILTER-GZIP",
        "GZIP File Filter",
        1,
        "gzip-compressed images (*.gz)", "application/x-gzip"
    );

    self->priv->cached_part = -1;

    self->priv->allocated_parts = 0;
    self->priv->num_parts = 0;
    self->priv->parts = NULL;

    self->priv->io_buffer = NULL;
    self->priv->window_buffer = NULL;
    self->priv->part_buffer = NULL;
}

static void mirage_file_filter_gzip_finalize (GObject *gobject)
{
    MirageFileFilterGzip *self = MIRAGE_FILE_FILTER_GZIP(gobject);

    g_free(self->priv->parts);
    g_free(self->priv->part_buffer);

    g_free(self->priv->io_buffer);
    g_free(self->priv->window_buffer);

    inflateEnd(&self->priv->zlib_stream);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_file_filter_gzip_parent_class)->finalize(gobject);
}

static void mirage_file_filter_gzip_class_init (MirageFileFilterGzipClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageFileFilterClass *file_filter_class = MIRAGE_FILE_FILTER_CLASS(klass);

    gobject_class->finalize = mirage_file_filter_gzip_finalize;

    file_filter_class->can_handle_data_format = mirage_file_filter_gzip_can_handle_data_format;

    file_filter_class->partial_read = mirage_file_filter_gzip_partial_read;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageFileFilterGzipPrivate));
}

static void mirage_file_filter_gzip_class_finalize (MirageFileFilterGzipClass *klass G_GNUC_UNUSED)
{
}
