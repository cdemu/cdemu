/*
 *  libMirage: XZ file filter: File filter object
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

#include "filter-xz.h"

#define __debug__ "XZ-FileFilter"


#define MAX_BLOCK_SIZE 10485760 /* For performance reasons, we support only 10 MB blocks and smaller */


static const guint8 xz_signature[6] = { 0xFD, '7', 'z', 'X', 'Z', 0x00 };


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_FILE_FILTER_XZ_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FILE_FILTER_XZ, MirageFileFilterXzPrivate))

struct _MirageFileFilterXzPrivate
{
    guint64 file_size;
    goffset cur_position;

    /* */
    guint8 *io_buffer;
    gint io_buffer_size;

    /* */
    gint cached_block_number;
    guint8 *block_buffer;
    gint block_buffer_size;

    /* XZ stream */
    lzma_stream_flags header;
    lzma_stream_flags footer;

    lzma_index *index;
};

static gboolean mirage_file_filter_xz_reallocate_read_buffer (MirageFileFilterXz *self, gint size, GError **error)
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
static gboolean mirage_file_filter_xz_read_header_and_footer (MirageFileFilterXz *self, GError **error)
{
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));
    lzma_ret ret;

    /* Allocate read buffer: header and footer (12 bytes) */
    if (!mirage_file_filter_xz_reallocate_read_buffer(self, LZMA_STREAM_HEADER_SIZE, error)) {
        return FALSE;
    }

    /* Read and decode header */
    if (!g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to the beginning of header!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to seek to the beginning of header!");
        return FALSE;
    }

    if (g_input_stream_read(G_INPUT_STREAM(stream), self->priv->io_buffer, LZMA_STREAM_HEADER_SIZE, NULL, NULL) != LZMA_STREAM_HEADER_SIZE) {
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
    if (!g_seekable_seek(G_SEEKABLE(stream), -LZMA_STREAM_HEADER_SIZE, G_SEEK_END, NULL, NULL)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to the beginning of footer!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to seek to the beginning of footer!");
        return FALSE;
    }

    if (g_input_stream_read(G_INPUT_STREAM(stream), self->priv->io_buffer, LZMA_STREAM_HEADER_SIZE, NULL, NULL) != LZMA_STREAM_HEADER_SIZE) {
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


static gboolean mirage_file_filter_xz_read_index (MirageFileFilterXz *self, GError **error)
{
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));
    guint64 memory_limit = G_MAXUINT64;
    gsize in_pos = 0;
    lzma_ret ret;

    /* Allocate read buffer: compressed index size is declared in footer */
    if (!mirage_file_filter_xz_reallocate_read_buffer(self, self->priv->footer.backward_size, error)) {
        return FALSE;
    }

    /* Read and decode index */
    if (!g_seekable_seek(G_SEEKABLE(stream), -(LZMA_STREAM_HEADER_SIZE + self->priv->footer.backward_size), G_SEEK_END, NULL, NULL)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to seek to the beginning of index!");
        return FALSE;
    }

    if (g_input_stream_read(G_INPUT_STREAM(stream), self->priv->io_buffer, self->priv->footer.backward_size, NULL, NULL) != self->priv->footer.backward_size) {
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
    self->priv->file_size = lzma_index_uncompressed_size(self->priv->index);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: file size: %lld (0x%llX)\n", __debug__, self->priv->file_size, self->priv->file_size);


    return TRUE;
}


static gboolean mirage_file_filter_xz_parse_stream (MirageFileFilterXz *self, GError **error)
{
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));
    guint64 max_block_size = 0;
    gint ret;

    /* Read and decode header and footer */
    if (!mirage_file_filter_xz_read_header_and_footer(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read/decode header and footer!\n", __debug__);
        return FALSE;
    }

    /* Read and decode index */
    if (!mirage_file_filter_xz_read_index(self, error)) {
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

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: listing blocks...\n", __debug__);
    lzma_index_iter_init(&index_iter, self->priv->index);
    while (lzma_index_iter_next(&index_iter, LZMA_INDEX_ITER_BLOCK) == 0) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: block #%d:\n", __debug__, index_iter.block.number_in_file);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: uncompressed size #%ld:\n", __debug__, index_iter.block.uncompressed_size, max_block_size);
        max_block_size = MAX(max_block_size, index_iter.block.uncompressed_size);
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "\n");


    /* For performance reasons, we limit the allowed size of blocks */
    if (max_block_size > MAX_BLOCK_SIZE) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: the largest block (%ld bytes) exceeds the limit of %d bytes')!\n", __debug__, max_block_size, MAX_BLOCK_SIZE);
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
    if (!mirage_file_filter_xz_reallocate_read_buffer(self, 32768, error)) {
        return FALSE;
    }

    return TRUE;
}



/**********************************************************************\
 *                              Seeking                               *
\**********************************************************************/
static gboolean mirage_file_filter_xz_set_current_position (MirageFileFilterXz *self, goffset new_position, GError **error)
{
    /* Validate new position */
    if (new_position < 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Seek before beginning of file not allowed!");
        return FALSE;
    }

    /* Set new position; corresponding block is determined upon read */
    self->priv->cur_position = new_position;

    return TRUE;
}


/**********************************************************************\
 *                         Reading from parts                         *
\**********************************************************************/
static gssize mirage_filter_xz_read_from_part (MirageFileFilterXz *self, guint8 *buffer, gsize count)
{
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));
    goffset block_offset;
    lzma_index_iter index_iter;

    /* Find block */
    lzma_index_iter_init(&index_iter, self->priv->index);
    if (lzma_index_iter_locate(&index_iter, self->priv->cur_position)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: current position %ld (0x%lX) beyond end of stream, doing nothing!\n", __debug__, self->priv->cur_position, self->priv->cur_position);
        return 0;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: current position: %ld (block #%d) -> read %ld bytes\n", __debug__, self->priv->cur_position, index_iter.block.number_in_file, count);

    /* If we do not have block in cache, uncompress it */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: currently cached block: #%d\n", __debug__, self->priv->cached_block_number);
    if (index_iter.block.number_in_file != self->priv->cached_block_number) {
        lzma_stream lzma = LZMA_STREAM_INIT;
        lzma_filter filters[LZMA_FILTERS_MAX+1];
        lzma_block block;

        guint8 value;
        gint ret;

        /* Seek to the position */
        if (!g_seekable_seek(G_SEEKABLE(stream), index_iter.block.compressed_file_offset, G_SEEK_SET, NULL, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to %ld in underlying stream!\n", __debug__, index_iter.block.compressed_file_offset);
            return -1;
        }

        /* Read first byte of block header */
        ret = g_input_stream_read(G_INPUT_STREAM(stream), &value, sizeof(value), NULL, NULL);
        if (ret != sizeof(value)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read first byte of block header!\n", __debug__);
            return -1;
        }
        if (!g_seekable_seek(G_SEEKABLE(stream), -sizeof(value), G_SEEK_CUR, NULL, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek at beginning of block header!\n", __debug__);
            return -1;
        }


        /* We need to set some block header fields ourselves */
        block.version = 0;
        block.header_size = lzma_block_header_size_decode(value);
        block.check = self->priv->footer.check;
        block.compressed_size = LZMA_VLI_UNKNOWN;
        block.filters = filters;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: block header size: %d!\n", __debug__, block.header_size);


        /* Read and decode header */
        ret = g_input_stream_read(G_INPUT_STREAM(stream), self->priv->io_buffer, block.header_size, NULL, NULL);
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
            lzma.avail_in = g_input_stream_read(G_INPUT_STREAM(stream), self->priv->io_buffer, self->priv->io_buffer_size, NULL, NULL);

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
    }

    block_offset = self->priv->cur_position - index_iter.block.uncompressed_stream_offset;
    count = MIN(count, index_iter.block.uncompressed_size - block_offset);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: offset within block: %ld, copying %d bytes\n", __debug__, block_offset, count);

    memcpy(buffer, self->priv->block_buffer + block_offset, count);

    return count;
}


/**********************************************************************\
 *              MirageFileFilter methods implementations             *
\**********************************************************************/
static gboolean mirage_file_filter_xz_can_handle_data_format (MirageFileFilter *_self, GError **error)
{
    MirageFileFilterXz *self = MIRAGE_FILE_FILTER_XZ(_self);
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));

    guint8 sig[6];

    /* Look for signature at the beginning */
    g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);
    if (g_input_stream_read(G_INPUT_STREAM(stream), sig, sizeof(sig), NULL, NULL) != sizeof(sig)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to read 6 signature bytes!");
        return FALSE;
    }

    /* Check signature */
    if (memcmp(sig, xz_signature, sizeof(sig))) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Filter cannot handle given data!");
        return FALSE;
    }

    /* Parse XZ stream */
    if (!mirage_file_filter_xz_parse_stream(self, error)) {
        return FALSE;
    }

    return TRUE;
}


static gssize mirage_file_filter_xz_read (MirageFileFilter *_self, void *buffer, gsize count, GError **error)
{
    MirageFileFilterXz *self = MIRAGE_FILE_FILTER_XZ(_self);

    gssize total_read, read_len;
    guint8 *ptr = buffer;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE, "%s: read %ld (0x%lX) bytes from current position %ld (0x%lX)!\n", __debug__, count, count, self->priv->cur_position, self->priv->cur_position);

    /* Read until all is read */
    total_read = 0;

    while (count > 0) {
        /* Read a single block from current part */
        read_len = mirage_filter_xz_read_from_part(self, ptr, count);

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
        if (!mirage_file_filter_xz_set_current_position(self, self->priv->cur_position+read_len, error)) {
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


static goffset mirage_file_filter_xz_tell (MirageFileFilter *_self)
{
    MirageFileFilterXz *self = MIRAGE_FILE_FILTER_XZ(_self);
    return self->priv->cur_position;
}

static gboolean mirage_file_filter_xz_seek (MirageFileFilter *_self, goffset offset, GSeekType type, GError **error)
{
    MirageFileFilterXz *self = MIRAGE_FILE_FILTER_XZ(_self);
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
    if (!mirage_file_filter_xz_set_current_position(self, new_position, error)) {
        return FALSE;
    }

    return TRUE;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE(MirageFileFilterXz, mirage_file_filter_xz, MIRAGE_TYPE_FILE_FILTER);

void mirage_file_filter_xz_type_register (GTypeModule *type_module)
{
    return mirage_file_filter_xz_register_type(type_module);
}


static void mirage_file_filter_xz_init (MirageFileFilterXz *self)
{
    self->priv = MIRAGE_FILE_FILTER_XZ_GET_PRIVATE(self);

    mirage_file_filter_generate_file_filter_info(MIRAGE_FILE_FILTER(self),
        "FILTER-XZ",
        "XZ File Filter"
    );

    self->priv->file_size = 0;
    self->priv->cur_position = 0;

    self->priv->cached_block_number = -1;

    self->priv->index = NULL;

    self->priv->io_buffer = NULL;
    self->priv->block_buffer = NULL;
}

static void mirage_file_filter_xz_finalize (GObject *gobject)
{
    MirageFileFilterXz *self = MIRAGE_FILE_FILTER_XZ(gobject);

    lzma_index_end(self->priv->index, NULL);

    g_free(self->priv->io_buffer);
    g_free(self->priv->block_buffer);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_file_filter_xz_parent_class)->finalize(gobject);
}

static void mirage_file_filter_xz_class_init (MirageFileFilterXzClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageFileFilterClass *file_filter_class = MIRAGE_FILE_FILTER_CLASS(klass);

    gobject_class->finalize = mirage_file_filter_xz_finalize;

    file_filter_class->can_handle_data_format = mirage_file_filter_xz_can_handle_data_format;

    file_filter_class->read = mirage_file_filter_xz_read;

    file_filter_class->tell = mirage_file_filter_xz_tell;
    file_filter_class->seek = mirage_file_filter_xz_seek;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageFileFilterXzPrivate));
}

static void mirage_file_filter_xz_class_finalize (MirageFileFilterXzClass *klass G_GNUC_UNUSED)
{
}
