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


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_FILE_FILTER_GZIP_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FILE_FILTER_GZIP, MIRAGE_FileFilter_GZIPPrivate))

struct _MIRAGE_FileFilter_GZIPPrivate
{
    goffset cur_position;

    gint cur_part_idx;
    const GZIP_Part *cur_part;

    guint64 file_size;

    /* Part list */
    GZIP_Part *parts;
    gint num_parts;
    gint allocated_parts;

    /* Cache */
    gint cache_part_idx;
    guint8 *part_buffer;

    /* Zlib stream */
    z_stream zlib_stream;
};


/**********************************************************************\
 *                           Part indexing                            *
\**********************************************************************/
static gboolean mirage_file_filter_gzip_compute_part_sizes (MIRAGE_FileFilter_GZIP *self, GError **error)
{
    GZIP_Part *part;
    gint max_size = 0;
    gint i;

    /* Compute sizes for all parts but last one, based on their offsets */
    for (i = 0; i < self->priv->num_parts - 1; i++) {
        part = &self->priv->parts[i];

        part->size = (part+1)->offset - part->offset;
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: part #%d: offset: %lld, size: %lld\n", __debug__, i, part->offset, part->size);

        max_size = MAX(max_size, part->size);
    }

    /* Last part size */
    part = &self->priv->parts[self->priv->num_parts-1];
    part->size = self->priv->file_size - part->offset;
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: part #%d: offset: %lld, size: %lld\n", __debug__, self->priv->num_parts-1, part->offset, part->size);

    max_size = MAX(max_size, part->size);

    /* Allocate part buffer */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: largest part: %lld; allocating part buffer...\n", __debug__, max_size);

    self->priv->part_buffer = g_try_malloc(max_size);
    if (!self->priv->part_buffer) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to allocate part buffer (%d bytes)", max_size);
        return FALSE;
    }

    return TRUE;
}

static gboolean mirage_file_filter_gzip_append_part (MIRAGE_FileFilter_GZIP *self, gint bits, goffset raw_offset, goffset offset, gint left, guint8 *window, GError **error)
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

static gboolean mirage_file_filter_gzip_build_index (MIRAGE_FileFilter_GZIP *self, GError **error)
{
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));
    z_stream *zlib_stream = &self->priv->zlib_stream;

    guint8 chunk_buffer[CHUNKSIZE];
    guint8 window_buffer[WINSIZE];

    goffset totalIn, totalOut, last;
    gint ret;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: building part index\n", __debug__);

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
        ret = g_input_stream_read(G_INPUT_STREAM(stream), chunk_buffer, CHUNKSIZE, NULL, NULL);
        if (ret == -1) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to read %d bytes from underlying stream!", CHUNKSIZE);
            return FALSE;
        } else if (ret == 0) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Unexpectedly reached EOF!");
            return FALSE;
        }

        zlib_stream->avail_in = ret;
        zlib_stream->next_in = chunk_buffer;

        /* Process read data */
        do {
            /* Reset sliding window if neccessary */
            if (!zlib_stream->avail_out) {
                zlib_stream->avail_out = WINSIZE;
                zlib_stream->next_out = window_buffer;
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

                if (!mirage_file_filter_gzip_append_part(self, zlib_stream->data_type & 7, totalIn, totalOut, zlib_stream->avail_out, window_buffer, error)) {
                    return FALSE;
                }

                last = totalOut;
            }
        } while (zlib_stream->avail_in);
    } while (ret != Z_STREAM_END);

    /* totalOut holds the size of the stream */
    self->priv->file_size = totalOut;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: number of parts: %d!\n", __debug__, self->priv->num_parts);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: original stream size: %ld!\n", __debug__, self->priv->file_size);

    /* At least one part must be present */
    if (!self->priv->num_parts) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: no parts in GZIP file!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "No parts in GZIP file!");
        return FALSE;
    }

    /* Release unused allocated parts */
    self->priv->parts = g_renew(GZIP_Part, self->priv->parts, self->priv->num_parts);

    /* Compute sizes of parts and allocate part buffer */
    if (!mirage_file_filter_gzip_compute_part_sizes(self, error)) {
        return FALSE;
    }

    /* Set stream position to beginning */
    self->priv->cur_position = 0;
    self->priv->cur_part_idx = 0;
    self->priv->cur_part = &self->priv->parts[self->priv->cur_part_idx];

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: successfully built index\n\n", __debug__);

    return TRUE;
}



/**********************************************************************\
 *                              Seeking                               *
\**********************************************************************/
static inline gboolean is_within_part (goffset position, const GZIP_Part *part, gboolean last_part)
{
    if (last_part) {
        return position >= part->offset;
    } else {
        return position >= part->offset && position < (part->offset + part->size);
    }
}


static gboolean mirage_file_filter_gzip_set_current_position (MIRAGE_FileFilter_GZIP *self, goffset new_position, GError **error)
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
        gint i;

        /* Seek part-by-part in appropriate direction (do not check first and last part, though) */
        if (new_position < self->priv->cur_position) {
            /* Seek backward */
            for (i = self->priv->cur_part_idx; i > 0; i--) {
                if (is_within_part(new_position, &self->priv->parts[i], FALSE)) {
                    self->priv->cur_part_idx = i;
                    break;
                }
            }
        } else {
            /* Seek foward */
            for (i = self->priv->cur_part_idx; i < self->priv->num_parts-1; i++) {
                if (is_within_part(new_position, &self->priv->parts[i], FALSE)) {
                    self->priv->cur_part_idx = i;
                    break;
                }
            }
        }
    }
    self->priv->cur_part = &self->priv->parts[self->priv->cur_part_idx];

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: position %ld (0x%lX) found in part #%d!\n", __debug__, new_position, new_position, self->priv->cur_part_idx);

    /* Set new position */
    self->priv->cur_position = new_position;

    return TRUE;
}


/**********************************************************************\
 *                         Reading from parts                         *
\**********************************************************************/
static gssize mirage_filter_gzip_read_from_part (MIRAGE_FileFilter_GZIP *self, guint8 *buffer, gsize count)
{
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));
    goffset part_offset;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: current position: %ld (part #%d, offset: %ld, size: %d, raw offset: %ld) -> read %d bytes\n", __debug__, self->priv->cur_position, self->priv->cur_part_idx, self->priv->cur_part->offset, self->priv->cur_part->size, self->priv->cur_part->raw_offset, count);

    /* Make sure we're not at end of stream */
    if (self->priv->cur_position >= self->priv->file_size) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: read beyond end of stream, doing nothing!\n", __debug__);
        return 0;
    }

    /* If we do not have part in cache, uncompress it */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: currently cached part: #%d\n", __debug__, self->priv->cache_part_idx);
    if (self->priv->cur_part_idx != self->priv->cache_part_idx) {
        z_stream *zlib_stream = &self->priv->zlib_stream;
        const GZIP_Part *part = self->priv->cur_part;

        goffset stream_offset;

        guint8 chunk_buffer[CHUNKSIZE];

        gint ret;

        /* Offset in underlying stream */
        stream_offset = part->raw_offset;
        if (part->bits) {
            stream_offset--;
        }

        /* Seek to the position */
        if (!g_seekable_seek(G_SEEKABLE(stream), stream_offset, G_SEEK_SET, NULL, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to %ld in underlying stream!\n", __debug__, stream_offset);
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
            ret = g_input_stream_read(G_INPUT_STREAM(stream), &value, sizeof(value), NULL, NULL);
            if (ret != 1) {
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
                ret = g_input_stream_read(G_INPUT_STREAM(stream), chunk_buffer, CHUNKSIZE, NULL, NULL);
                if (ret == -1) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read %d bytes from underlying stream!", __debug__, CHUNKSIZE);
                    return -1;
                } else if (ret == 0) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: inexpectedly reached EOF!", __debug__);
                    return -1;
                }
                zlib_stream->avail_in = ret;
                zlib_stream->next_in = chunk_buffer;
            }

            /* Inflate */
            ret = inflate(zlib_stream, Z_NO_FLUSH);
            if (ret == Z_NEED_DICT || ret == Z_MEM_ERROR || ret == Z_DATA_ERROR) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to inflate part: %s!", __debug__, zlib_stream->msg);
                return -1;
            }
        } while(zlib_stream->avail_out);


        /* Set currently cached part */
        self->priv->cache_part_idx = self->priv->cur_part_idx;
    }

    part_offset = self->priv->cur_position - self->priv->cur_part->offset;
    count = MIN(count, self->priv->cur_part->size - part_offset);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: offset within part: %ld, copying %d bytes\n", __debug__, part_offset, count);

    memcpy(buffer, self->priv->part_buffer + part_offset, count);

    return count;
}


/**********************************************************************\
 *              MIRAGE_FileFilter methods implementations             *
\**********************************************************************/
static gboolean mirage_file_filter_gzip_can_handle_data_format (MIRAGE_FileFilter *_self, GError **error)
{
    MIRAGE_FileFilter_GZIP *self = MIRAGE_FILE_FILTER_GZIP(_self);
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));

    guint8 sig[2];

    /* Look for gzip signature at the beginning */
    g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);
    if (g_input_stream_read(G_INPUT_STREAM(stream), sig, sizeof(sig), NULL, NULL) != sizeof(sig)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to read 4 signature bytes!");
        return FALSE;
    }

    /* Check signature */
    if (memcmp(sig, "\x1f\x8b", sizeof(sig))) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Filter cannot handle given data!");
        return FALSE;
    }

    /* Build index */
    if (!mirage_file_filter_gzip_build_index(self, error)) {
        return FALSE;
    }

    return TRUE;
}


static gssize mirage_file_filter_gzip_read (MIRAGE_FileFilter *_self, void *buffer, gsize count, GError **error)
{
    MIRAGE_FileFilter_GZIP *self = MIRAGE_FILE_FILTER_GZIP(_self);

    gssize total_read, read_len;
    guint8 *ptr = buffer;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: read %ld (0x%lX) bytes from current position %ld (0x%lX)!\n", __debug__, count, count, self->priv->cur_position, self->priv->cur_position);

    /* Read until all is read */
    total_read = 0;

    while (count > 0) {
        /* Read a single block from current part */
        read_len = mirage_filter_gzip_read_from_part(self, ptr, count);

        if (read_len == -1) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read data block!\n", __debug__);
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to read a data block from GZIP stream.");
            return -1;
        }

        ptr += read_len;
        total_read += read_len;
        count -= read_len;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: read %ld (0x%lX) bytes... %ld (0x%lX) remaining\n\n", __debug__, read_len, read_len, count, count);

        /* Update position */
        if (!mirage_file_filter_gzip_set_current_position(self, self->priv->cur_position+read_len, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to update position!\n", __debug__);
            return -1;
        }

        /* Check if we're at end of stream */
        if (self->priv->cur_position >= self->priv->file_size) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: end of stream reached!\n", __debug__);
            break;
        }
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: read complete\n", __debug__);

    return total_read;
}


static goffset mirage_file_filter_gzip_tell (MIRAGE_FileFilter *_self)
{
    MIRAGE_FileFilter_GZIP *self = MIRAGE_FILE_FILTER_GZIP(_self);
    return self->priv->cur_position;
}

static gboolean mirage_file_filter_gzip_seek (MIRAGE_FileFilter *_self, goffset offset, GSeekType type, GError **error)
{
    MIRAGE_FileFilter_GZIP *self = MIRAGE_FILE_FILTER_GZIP(_self);
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

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: request for seek to %ld (0x%lX)\n", __debug__, new_position, new_position);

    /* Seek */
    if (!mirage_file_filter_gzip_set_current_position(self, new_position, error)) {
        return FALSE;
    }

    return TRUE;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE(MIRAGE_FileFilter_GZIP, mirage_file_filter_gzip, MIRAGE_TYPE_FILE_FILTER);

void mirage_file_filter_gzip_type_register (GTypeModule *type_module)
{
    return mirage_file_filter_gzip_register_type(type_module);
}


static void mirage_file_filter_gzip_init (MIRAGE_FileFilter_GZIP *self)
{
    self->priv = MIRAGE_FILE_FILTER_GZIP_GET_PRIVATE(self);

    mirage_file_filter_generate_file_filter_info(MIRAGE_FILE_FILTER(self),
        "FILTER-GZIP",
        "GZIP File Filter"
    );

    self->priv->file_size = 0;

    self->priv->cur_position = 0;
    self->priv->cur_part_idx = 0;
    self->priv->cur_part = NULL;

    self->priv->allocated_parts = 0;
    self->priv->num_parts = 0;
    self->priv->parts = NULL;

    self->priv->cache_part_idx = -1;
    self->priv->part_buffer = NULL;
}

static void mirage_file_filter_gzip_finalize (GObject *gobject)
{
    MIRAGE_FileFilter_GZIP *self = MIRAGE_FILE_FILTER_GZIP(gobject);

    g_free(self->priv->parts);
    g_free(self->priv->part_buffer);

    inflateEnd(&self->priv->zlib_stream);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_file_filter_gzip_parent_class)->finalize(gobject);
}

static void mirage_file_filter_gzip_class_init (MIRAGE_FileFilter_GZIPClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MIRAGE_FileFilterClass *file_filter_class = MIRAGE_FILE_FILTER_CLASS(klass);

    gobject_class->finalize = mirage_file_filter_gzip_finalize;

    file_filter_class->can_handle_data_format = mirage_file_filter_gzip_can_handle_data_format;

    file_filter_class->read = mirage_file_filter_gzip_read;

    file_filter_class->tell = mirage_file_filter_gzip_tell;
    file_filter_class->seek = mirage_file_filter_gzip_seek;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_FileFilter_GZIPPrivate));
}

static void mirage_file_filter_gzip_class_finalize (MIRAGE_FileFilter_GZIPClass *klass G_GNUC_UNUSED)
{
}
