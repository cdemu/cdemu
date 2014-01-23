/*
 *  libMirage: XZ filter: Filter stream object
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

#include "filter-xz.h"

#define __debug__ "XZ-FilterStream"


#define MAX_BLOCK_SIZE 10485760 /* For performance reasons, we support only 10 MB blocks and smaller */


static const guint8 xz_signature[6] = { 0xFD, '7', 'z', 'X', 'Z', 0x00 };


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_FILTER_STREAM_XZ_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FILTER_STREAM_XZ, MirageFilterStreamXzPrivate))

struct _MirageFilterStreamXzPrivate
{
    /* I/O buffer */
    guint8 *io_buffer;
    gint io_buffer_size;

    /* Block (inflate) buffer */
    gint cached_block_number;
    guint8 *block_buffer;
    gint block_buffer_size;

    /* XZ stream */
    lzma_stream_flags header;
    lzma_stream_flags footer;

    lzma_index *index;
};

static gboolean mirage_filter_stream_xz_reallocate_read_buffer (MirageFilterStreamXz *self, gint size, GError **error)
{
    self->priv->io_buffer = g_try_realloc(self->priv->io_buffer, size);
    if (!self->priv->io_buffer) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to (re)allocate read buffer (%d bytes)!\n", __debug__, size);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to (re)allocate read buffer (%d bytes)!", size);
        return FALSE;
    }
    self->priv->io_buffer_size = size;

    return TRUE;
}


/**********************************************************************\
 *                           Stream parsing                           *
\**********************************************************************/
static gboolean mirage_filter_stream_xz_read_header_and_footer (MirageFilterStreamXz *self, GError **error)
{
    MirageStream *stream = mirage_filter_stream_get_underlying_stream(MIRAGE_FILTER_STREAM(self));
    lzma_ret ret;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing header and footer...\n", __debug__);

    /* Allocate read buffer: header and footer (12 bytes) */
    if (!mirage_filter_stream_xz_reallocate_read_buffer(self, LZMA_STREAM_HEADER_SIZE, error)) {
        return FALSE;
    }

    /* Read and decode header */
    if (!mirage_stream_seek(stream, 0, G_SEEK_SET, NULL)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to the beginning of header!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to seek to the beginning of header!");
        return FALSE;
    }

    if (mirage_stream_read(stream, self->priv->io_buffer, LZMA_STREAM_HEADER_SIZE, NULL) != LZMA_STREAM_HEADER_SIZE) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read stream's header!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to read stream's header!");
        return FALSE;
    }

    ret = lzma_stream_header_decode(&self->priv->header, self->priv->io_buffer);
    if (ret != LZMA_OK) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to decode stream's header!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to decode stream's header!");
        return FALSE;
    }


    /* Read and decode footer */
    if (!mirage_stream_seek(stream, -LZMA_STREAM_HEADER_SIZE, G_SEEK_END, NULL)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to the beginning of footer!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to seek to the beginning of footer!");
        return FALSE;
    }

    if (mirage_stream_read(stream, self->priv->io_buffer, LZMA_STREAM_HEADER_SIZE, NULL) != LZMA_STREAM_HEADER_SIZE) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read stream's footer!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to read stream's footer!");
        return FALSE;
    }

    ret = lzma_stream_footer_decode(&self->priv->footer, self->priv->io_buffer);
    if (ret != LZMA_OK) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to decode stream's footer!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to decode stream's footer!");
        return FALSE;
    }


    /* Validate */
    ret = lzma_stream_flags_compare(&self->priv->header, &self->priv->footer);
    if (ret != LZMA_OK) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: stream's header and footer do not match!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Stream's header and footer do not match!");
        return FALSE;
    }

    return TRUE;
}


static gboolean mirage_filter_stream_xz_read_index (MirageFilterStreamXz *self, GError **error)
{
    MirageStream *stream = mirage_filter_stream_get_underlying_stream(MIRAGE_FILTER_STREAM(self));
    guint64 memory_limit = G_MAXUINT64;
    gsize in_pos = 0;
    lzma_ret ret;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing index...\n", __debug__);

    /* Allocate read buffer: compressed index size is declared in footer */
    if (!mirage_filter_stream_xz_reallocate_read_buffer(self, self->priv->footer.backward_size, error)) {
        return FALSE;
    }

    /* Read and decode index */
    if (!mirage_stream_seek(stream, -(LZMA_STREAM_HEADER_SIZE + self->priv->footer.backward_size), G_SEEK_END, NULL)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to seek to the beginning of index!");
        return FALSE;
    }

    if (mirage_stream_read(stream, self->priv->io_buffer, self->priv->footer.backward_size, NULL) != self->priv->footer.backward_size) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to read stream's index!");
        return FALSE;
    }

    ret = lzma_index_buffer_decode(&self->priv->index, &memory_limit, NULL, self->priv->io_buffer, &in_pos, self->priv->footer.backward_size);
    if (ret != LZMA_OK) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to decode stream's index!");
        return FALSE;
    }


    /* Validate */
    if (lzma_index_size(self->priv->index) != self->priv->footer.backward_size) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Declared and actual index size mismatch!");
        return FALSE;
    }

    /* Store file size */
    guint64 file_size = lzma_index_uncompressed_size(self->priv->index);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: file size: %lld (0x%llX)\n", __debug__, file_size, file_size);
    mirage_filter_stream_set_stream_length(MIRAGE_FILTER_STREAM(self), file_size);

    return TRUE;
}


static gboolean mirage_filter_stream_xz_parse_stream (MirageFilterStreamXz *self, GError **error)
{
    guint64 max_block_size = 0;

    /* Read and decode header and footer */
    if (!mirage_filter_stream_xz_read_header_and_footer(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read/decode header and footer!\n", __debug__);
        return FALSE;
    }

    /* Read and decode index */
    if (!mirage_filter_stream_xz_read_index(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read/decode index!\n", __debug__);
        return FALSE;
    }

    /* Warn about multiple streams */
    if (lzma_index_stream_count(self->priv->index) > 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: XZ file contains multiple (%d) streams! Their content will be treated as a single compressed file!\n", __debug__, lzma_index_stream_count(self->priv->index));
    }


    /* Warn about single-block streams */
    if (lzma_index_block_count(self->priv->index) == 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: XZ stream contains a single large block! To allow efficient seeking, consider re-compressing the file using smaller blocks (e.g. 'xz --block-size=1M ...')!\n", __debug__);
    }

    /* Find maximum block size */
    lzma_index_iter index_iter;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: listing blocks...\n", __debug__);
    lzma_index_iter_init(&index_iter, self->priv->index);
    while (lzma_index_iter_next(&index_iter, LZMA_INDEX_ITER_BLOCK) == 0) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: block #%d\n", __debug__, index_iter.block.number_in_file);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: uncompressed size #%ld\n", __debug__, index_iter.block.uncompressed_size, max_block_size);
        max_block_size = MAX(max_block_size, index_iter.block.uncompressed_size);
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");


    /* For performance reasons, we limit the allowed size of blocks */
    if (max_block_size > MAX_BLOCK_SIZE) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: the largest block (%ld bytes) exceeds the limit of %d bytes')!\n", __debug__, max_block_size, MAX_BLOCK_SIZE);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_DEBUG_PARSER, "The largest block (%ld bytes) exceeds the limit of %d bytes')!", max_block_size, MAX_BLOCK_SIZE);
        return FALSE;
    }


    /* Allocate block buffer */
    self->priv->block_buffer_size = max_block_size;
    self->priv->block_buffer = g_try_malloc(self->priv->block_buffer_size);
    if (!self->priv->block_buffer) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to allocate block buffer (%d bytes)!", self->priv->block_buffer_size);
        return FALSE;
    }

    /* Allocate read buffer - 32 kB */
    if (!mirage_filter_stream_xz_reallocate_read_buffer(self, 32768, error)) {
        return FALSE;
    }

    return TRUE;
}


/**********************************************************************\
 *              MirageFilterStream methods implementations            *
\**********************************************************************/
static gboolean mirage_filter_stream_xz_open (MirageFilterStream *_self, MirageStream *stream, GError **error)
{
    MirageFilterStreamXz *self = MIRAGE_FILTER_STREAM_XZ(_self);

    guint8 sig[6];

    /* Look for signature at the beginning */
    mirage_stream_seek(stream, 0, G_SEEK_SET, NULL);
    if (mirage_stream_read(stream, sig, sizeof(sig), NULL) != sizeof(sig)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Filter cannot handle given data: failed to read 6 signature bytes!");
        return FALSE;
    }

    /* Check signature */
    if (memcmp(sig, xz_signature, sizeof(xz_signature))) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Filter cannot handle given data: invalid signature!");
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing the underlying stream data...\n", __debug__);

    /* Parse XZ stream */
    if (!mirage_filter_stream_xz_parse_stream(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing failed!\n\n", __debug__);
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing completed successfully\n\n", __debug__);

    return TRUE;
}

static gssize mirage_filter_stream_xz_partial_read (MirageFilterStream *_self, void *buffer, gsize count)
{
    MirageFilterStreamXz *self = MIRAGE_FILTER_STREAM_XZ(_self);
    MirageStream *stream = mirage_filter_stream_get_underlying_stream(_self);
    goffset position = mirage_filter_stream_get_position(_self);
    lzma_index_iter index_iter;

    /* Find block that corresponds to current position */
    lzma_index_iter_init(&index_iter, self->priv->index);
    if (lzma_index_iter_locate(&index_iter, position)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: stream position %ld (0x%lX) beyond end of stream, doing nothing!\n", __debug__, position, position);
        return 0;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: stream position: %ld (0x%lX) -> block #%d (cached: #%d)\n", __debug__, position, position, index_iter.block.number_in_file, self->priv->cached_block_number);

    /* If we do not have block in cache, uncompress it */
    if (index_iter.block.number_in_file != self->priv->cached_block_number) {
        lzma_stream lzma = LZMA_STREAM_INIT;
        lzma_filter filters[LZMA_FILTERS_MAX+1];
        lzma_block block;

        guint8 value;
        gint ret;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: block not cached, reading...\n", __debug__);

        /* Seek to the position */
        if (!mirage_stream_seek(stream, index_iter.block.compressed_file_offset, G_SEEK_SET, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to %ld in underlying stream!\n", __debug__, index_iter.block.compressed_file_offset);
            return -1;
        }

        /* Read first byte of block header */
        ret = mirage_stream_read(stream, &value, sizeof(value), NULL);
        if (ret != sizeof(value)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read first byte of block header!\n", __debug__);
            return -1;
        }
        if (!mirage_stream_seek(stream, -sizeof(value), G_SEEK_CUR, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek at beginning of block header!\n", __debug__);
            return -1;
        }


        /* We need to set some block header fields ourselves */
        block.version = 0;
        block.header_size = lzma_block_header_size_decode(value);
        block.check = self->priv->footer.check;
        block.compressed_size = LZMA_VLI_UNKNOWN;
        block.filters = filters;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: block header size: %d!\n", __debug__, block.header_size);


        /* Read and decode header */
        ret = mirage_stream_read(stream, self->priv->io_buffer, block.header_size, NULL);
        if (ret != block.header_size) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read block header!\n", __debug__);
            return -1;
        }

        ret = lzma_block_header_decode(&block, NULL, self->priv->io_buffer);
        if (ret != LZMA_OK) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to decode block header (error: %d)!\n", __debug__, ret);
            return -1;
        }


        /* Initialize LZMA stream */
        lzma.next_out = self->priv->block_buffer;
        lzma.avail_out = self->priv->block_buffer_size;

        ret = lzma_block_decoder(&lzma, &block);
        if (ret != LZMA_OK) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to initialize block decoder!\n", __debug__);
            return -1;
        }

        /* Read and uncompress */
        while (1) {
            lzma.next_in = self->priv->io_buffer;
            lzma.avail_in = mirage_stream_read(stream, self->priv->io_buffer, self->priv->io_buffer_size, NULL);

            ret = lzma_code(&lzma, LZMA_RUN);
            if (ret == LZMA_STREAM_END) {
                break;
            } else if (ret != LZMA_OK) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: error while decoding block: %d (consumed %d bytes, uncompressed %d bytes)!\n", __debug__, ret, lzma.total_in, lzma.total_out);
                return -1;
            }
        }

        lzma_end(&lzma);

        /* Store the number of currently stored block */
        self->priv->cached_block_number = index_iter.block.number_in_file;
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: block already cached\n", __debug__);
    }

    /* Copy data */
    goffset block_offset = position - index_iter.block.uncompressed_stream_offset;
    count = MIN(count, index_iter.block.uncompressed_size - block_offset);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: offset within block: %ld, copying %d bytes\n", __debug__, block_offset, count);

    memcpy(buffer, self->priv->block_buffer + block_offset, count);

    return count;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE(MirageFilterStreamXz, mirage_filter_stream_xz, MIRAGE_TYPE_FILTER_STREAM);

void mirage_filter_stream_xz_type_register (GTypeModule *type_module)
{
    return mirage_filter_stream_xz_register_type(type_module);
}


static void mirage_filter_stream_xz_init (MirageFilterStreamXz *self)
{
    self->priv = MIRAGE_FILTER_STREAM_XZ_GET_PRIVATE(self);

    mirage_filter_stream_generate_info(MIRAGE_FILTER_STREAM(self),
        "FILTER-XZ",
        "XZ File Filter",
        FALSE,
        1,
        "xz-compressed images (*.xz)", "application/x-xz"
    );

    self->priv->cached_block_number = -1;

    self->priv->index = NULL;

    self->priv->io_buffer = NULL;
    self->priv->block_buffer = NULL;
}

static void mirage_filter_stream_xz_finalize (GObject *gobject)
{
    MirageFilterStreamXz *self = MIRAGE_FILTER_STREAM_XZ(gobject);

    lzma_index_end(self->priv->index, NULL);

    g_free(self->priv->io_buffer);
    g_free(self->priv->block_buffer);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_filter_stream_xz_parent_class)->finalize(gobject);
}

static void mirage_filter_stream_xz_class_init (MirageFilterStreamXzClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageFilterStreamClass *filter_stream_class = MIRAGE_FILTER_STREAM_CLASS(klass);

    gobject_class->finalize = mirage_filter_stream_xz_finalize;

    filter_stream_class->open = mirage_filter_stream_xz_open;

    filter_stream_class->partial_read = mirage_filter_stream_xz_partial_read;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageFilterStreamXzPrivate));
}

static void mirage_filter_stream_xz_class_finalize (MirageFilterStreamXzClass *klass G_GNUC_UNUSED)
{
}
