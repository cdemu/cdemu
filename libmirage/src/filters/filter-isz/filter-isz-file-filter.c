/*
 *  libMirage: ISZ file filter: File filter object
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

#include "filter-isz.h"

#define __debug__ "ISZ-FileFilter"


static const guint8 isz_signature[4] = { 'I', 's', 'Z', '!' };


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_FILE_FILTER_ISZ_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FILE_FILTER_ISZ, MirageFileFilterIszPrivate))

struct _MirageFileFilterIszPrivate
{
    ISZ_Header header;

    /* Part list */
    ISZ_Chunk *parts;
    gint num_parts;

    /* Inflate buffer */
    guint8 *inflate_buffer;
    gint inflate_buffer_size;
    gint cached_part;

    /* I/O buffer */
    guint8 *io_buffer;
    gint io_buffer_size;

    /* Compression streams */
    z_stream  zlib_stream;
    bz_stream bzip2_stream;
};


/**********************************************************************\
 *                      Data conversion routines                      *
\**********************************************************************/
static void mirage_file_filter_isz_fixup_header(ISZ_Header *header)
{
    header->vol_sn        = GUINT32_FROM_LE(header->vol_sn);
    header->total_sectors = GUINT32_FROM_LE(header->total_sectors);
    header->num_blocks    = GUINT32_FROM_LE(header->num_blocks);
    header->block_size    = GUINT32_FROM_LE(header->block_size);
    header->chunk_offs    = GUINT32_FROM_LE(header->chunk_offs);
    header->seg_offs      = GUINT32_FROM_LE(header->seg_offs);
    header->data_offs     = GUINT32_FROM_LE(header->data_offs);

    header->sect_size     = GUINT16_FROM_LE(header->sect_size);
    header->segment_size  = GUINT64_FROM_LE(header->segment_size);

    /* additional header data */
    header->checksum1     = GUINT32_FROM_LE(header->checksum1);
    header->data_size     = GUINT32_FROM_LE(header->data_size);
    header->unknown       = GUINT32_FROM_LE(header->unknown);
    header->checksum2     = GUINT32_FROM_LE(header->checksum2);
}

static inline void mirage_file_filter_isz_deobfuscate(guint8 *data, gint length)
{
    guint8 code[4] = {0xb6, 0x8c, 0xa5, 0xde};

    for (gint i = 0; i < length; i++) {
        data[i] = data[i] ^ code[i % 4];
    }
}


/**********************************************************************\
 *                           Part indexing                            *
\**********************************************************************/
static gboolean mirage_file_filter_isz_read_index (MirageFileFilterIsz *self, GError **error)
{
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));
    z_stream  *zlib_stream  = &self->priv->zlib_stream;
    bz_stream *bzip2_stream = &self->priv->bzip2_stream;

    ISZ_Header *header = &self->priv->header;
    guint8     *chunk_buffer = NULL;

    gint ret;
    gint chunk_buf_size, original_size;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading part index\n", __debug__);

    self->priv->num_parts = header->num_blocks;
    original_size = header->total_sectors * header->sect_size;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of parts: %d\n", __debug__, self->priv->num_parts);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: original stream size: %ld\n",
                 __debug__, original_size);

    /* At least one part must be present */
    if (!self->priv->num_parts) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: no parts in ISZ file!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "No parts in ISZ file!");
        return FALSE;
    }

    /* Chunk pointer length > 4 not implemented */
    if (header->ptr_len > 4) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Pointer length %u not supported yet!\n", __debug__, header->ptr_len);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Unsupported pointer length!");
        return FALSE;
    }

    /* Allocate chunk buffer */
    chunk_buf_size = header->num_blocks * header->ptr_len;
    chunk_buffer = g_try_malloc(chunk_buf_size);
    if (!chunk_buffer) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to allocate memory for chunk buffer!");
        return FALSE;
    }

    /* Position at the beginning of the chunk table */
    if (!g_seekable_seek(G_SEEKABLE(stream), header->chunk_offs, G_SEEK_SET, NULL, NULL)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to seek to the beginning of index!");
        return FALSE;
    }

    /* Read chunk table */
    ret = g_input_stream_read(stream, chunk_buffer, chunk_buf_size, NULL, NULL);
    if (ret != chunk_buf_size) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to read index!");
        return FALSE;
    }

    /* De-obfuscate chunk table */
    mirage_file_filter_isz_deobfuscate(chunk_buffer, chunk_buf_size);

    /* Allocate part index */
    self->priv->parts = g_try_new(ISZ_Chunk, self->priv->num_parts);
    if (!self->priv->parts) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to allocate memory for index!");
        return FALSE;
    }

    /* Compute index from chunk table */
    for (gint i = 0; i < self->priv->num_parts; i++) {
        guint32 *chunk_ptr = (guint32 *) ((guint8 *) &chunk_buffer[i * header->ptr_len]);
        gint    chunk_len_bits = header->ptr_len * 8 - 2;
        gint    chunk_type_bits = 2;
        gint    chunk_len_mask = (1 << chunk_len_bits) - 1;
        gint    chunk_type_mask = (1 << chunk_type_bits) - 1;

        ISZ_Chunk *cur_part = &self->priv->parts[i];

        /* Calculate index entry */
        cur_part->length = *chunk_ptr & chunk_len_mask;
        cur_part->type   = (*chunk_ptr >> chunk_len_bits) & chunk_type_mask;

        /* Fixup endianness */
        cur_part->length = GUINT32_FROM_LE(cur_part->length);

        /* Calculate input offset */
        if (i == 0) {
            cur_part->offset = header->data_offs;
        } else {
            ISZ_Chunk *prev_part = &self->priv->parts[i - 1];

            cur_part->offset = prev_part->offset + prev_part->length;
        }

        /*MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Part %4u: type: %u offs: %u len: %u\n",
                     __debug__, i, cur_part->type, cur_part->offset, cur_part->length);*/
    }

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

    /* Initialize bzip2 stream */
    bzip2_stream->bzalloc = NULL;
    bzip2_stream->bzfree = NULL;
    bzip2_stream->opaque = NULL;
    bzip2_stream->avail_in = 0;
    bzip2_stream->next_in = NULL;

    ret = BZ2_bzDecompressInit(bzip2_stream, 0, 0);

    if (ret != BZ_OK) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to initialize libbz2's decompress (error: %d)!", ret);
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
    mirage_file_filter_set_file_size(MIRAGE_FILE_FILTER(self), original_size);

    g_free(chunk_buffer);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: successfully read index\n\n", __debug__);

    return TRUE;
}


/**********************************************************************\
 *              MirageFileFilter methods implementations              *
\**********************************************************************/
static gboolean mirage_file_filter_isz_can_handle_data_format (MirageFileFilter *_self, GError **error)
{
    MirageFileFilterIsz *self = MIRAGE_FILE_FILTER_ISZ(_self);
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));

    ISZ_Header *header = &self->priv->header;

    /* Read ISZ header */
    g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);
    if (g_input_stream_read(stream, header, sizeof(ISZ_Header), NULL, NULL) != sizeof(ISZ_Header)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to read ISZ header!");
        return FALSE;
    }

    /* Fixup header endianness */
    mirage_file_filter_isz_fixup_header(header);

    /* Validate ISZ header */
    if (memcmp(&header->signature, isz_signature, sizeof(isz_signature)) || header->version > 1) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Filter cannot handle given data!");
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: ISZ header:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  signature: %.4s\n", __debug__, header->signature);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  header_size: %u\n", __debug__, header->header_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  version: %u\n", __debug__, header->version);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  vol_sn: %u\n", __debug__, header->vol_sn);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  sect_size: %u\n", __debug__, header->sect_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  total_sectors: %u\n", __debug__, header->total_sectors);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  encryption_type: %u\n", __debug__, header->encryption_type);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  segment_size: %u\n", __debug__, header->segment_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  num_blocks: %u\n", __debug__, header->num_blocks);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  block_size: %u\n", __debug__, header->block_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  ptr_len: %u\n", __debug__, header->ptr_len);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  seg_num: %u\n", __debug__, header->seg_num);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  chunk_offs: 0x%x\n", __debug__, header->chunk_offs);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  seg_offs: 0x%x\n", __debug__, header->seg_offs);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  data_offs: 0x%x\n", __debug__, header->data_offs);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  checksum1: 0x%x\n", __debug__, header->checksum1);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  data_size: %u\n", __debug__, header->data_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  unknown: %u\n", __debug__, header->unknown);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  checksum2: 0x%x\n", __debug__, header->checksum2);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");

    /* FIXME: Handle encrypted images */
    if (header->encryption_type != NONE) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Filter cannot handle encryption yet!");
        return FALSE;
    }

    /* FIXME: Handle segmented images */
    if (header->seg_offs != 0) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Filter cannot handle segmentation yet!");
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing the underlying stream data...\n", __debug__);

    /* Read chunk table if one exists */
    if (header->chunk_offs) {
        if (!mirage_file_filter_isz_read_index(self, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing failed!\n\n", __debug__);
            return FALSE;
        }
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing completed successfully\n\n", __debug__);

    return TRUE;
}


static gssize mirage_file_filter_isz_partial_read (MirageFileFilter *_self, void *buffer, gsize count)
{
    MirageFileFilterIsz *self = MIRAGE_FILE_FILTER_ISZ(_self);
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));
    goffset position = mirage_file_filter_get_position(MIRAGE_FILE_FILTER(self));
    gint part_idx;

    /* Find part that corresponds to current position */
    part_idx = position / self->priv->header.block_size;

    if (part_idx >= self->priv->num_parts) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: stream position %ld (0x%lX) beyond end of stream, doing nothing!\n", __debug__, position, position);
        return 0;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: stream position: %ld (0x%lX) -> part #%d (cached: #%d)\n", __debug__, position, position, part_idx, self->priv->cached_part);

    /* If we do not have part in cache, uncompress it */
    if (part_idx != self->priv->cached_part) {
        const ISZ_Chunk *part = &self->priv->parts[part_idx];
        z_stream *zlib_stream = &self->priv->zlib_stream;
        bz_stream *bzip2_stream = &self->priv->bzip2_stream;
        gint ret;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: part not cached, reading...\n", __debug__);

        /* Seek to the position */
        if (!g_seekable_seek(G_SEEKABLE(stream), part->offset, G_SEEK_SET, NULL, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to %ld in underlying stream!\n", __debug__, part->offset);
            return -1;
        }

        /* Read a part, either zero, raw or compressed */
        if (part->type == ZERO) {
            /* Return a zero-filled buffer */
            memset (self->priv->inflate_buffer, 0, self->priv->inflate_buffer_size);
        } else if (part->type == DATA) {
            /* Read uncompressed part */
            ret = g_input_stream_read(stream, self->priv->inflate_buffer, self->priv->inflate_buffer_size, NULL, NULL);
            if (ret == -1) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read %d bytes from underlying stream!\n", __debug__, self->priv->inflate_buffer_size);
                return -1;
            } else if (ret == 0) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unexpectedly reached EOF!\n", __debug__);
                return -1;
            }
        } else if (part->type == ZLIB) {
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
                    ret = g_input_stream_read(stream, self->priv->io_buffer, part->length, NULL, NULL);
                    if (ret == -1) {
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read %d bytes from underlying stream!\n", __debug__, part->length);
                        return -1;
                    } else if (ret == 0) {
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unexpectedly reached EOF!\n", __debug__);
                        return -1;
                    }
                    zlib_stream->avail_in = ret;
                    zlib_stream->next_in = self->priv->io_buffer;
                }

                /* Inflate */
                ret = inflate(zlib_stream, Z_NO_FLUSH);
                if (ret == Z_NEED_DICT || ret == Z_MEM_ERROR || ret == Z_DATA_ERROR) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to inflate part: %s!\n", __debug__, zlib_stream->msg);
                    return -1;
                }
            } while (zlib_stream->avail_out);
        } else if (part->type == BZ2) {
            /* Reset decompress engine */
            ret = BZ2_bzDecompressInit(bzip2_stream, 0, 0);
            if (ret != BZ_OK) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to reset decompress engine!\n", __debug__);
                return -1;
            }

            /* Uncompress whole part */
            bzip2_stream->avail_in = 0;
            bzip2_stream->avail_out = self->priv->inflate_buffer_size;
            bzip2_stream->next_out = self->priv->inflate_buffer;

            do {
                /* Read */
                if (!bzip2_stream->avail_in) {
                    /* Read some compressed data */
                    ret = g_input_stream_read(stream, self->priv->io_buffer, part->length, NULL, NULL);
                    if (ret == -1) {
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read %d bytes from underlying stream!\n", __debug__, part->length);
                        return -1;
                    } else if (ret == 0) {
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unexpectedly reached EOF!\n", __debug__);
                        return -1;
                    }
                    bzip2_stream->avail_in = ret;
                    bzip2_stream->next_in = self->priv->io_buffer;

                    /* Restore a correct header */
                    memcpy (self->priv->io_buffer, "BZh", 3);
                }

                /* Inflate */
                ret = BZ2_bzDecompress(bzip2_stream);

                if (ret < 0) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to inflate part: %d!\n", __debug__, ret);
                    return -1;
                }
            } while (bzip2_stream->avail_out);
        } else {
            /* We should never get here... */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Encountered unknown chunk type %u!\n", __debug__, part->type);
            return -1;
        }

        /* Set currently cached part */
        self->priv->cached_part = part_idx;
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: part already cached\n", __debug__);
    }

    /* Copy data */
    goffset part_offset = position % self->priv->header.block_size;
    count = MIN(count, self->priv->header.block_size - part_offset);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: offset within part: %ld, copying %d bytes\n", __debug__, part_offset, count);

    memcpy(buffer, &self->priv->inflate_buffer[part_offset], count);

    return count;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE(MirageFileFilterIsz, mirage_file_filter_isz, MIRAGE_TYPE_FILE_FILTER);

void mirage_file_filter_isz_type_register (GTypeModule *type_module)
{
    return mirage_file_filter_isz_register_type(type_module);
}


static void mirage_file_filter_isz_init (MirageFileFilterIsz *self)
{
    self->priv = MIRAGE_FILE_FILTER_ISZ_GET_PRIVATE(self);

    mirage_file_filter_generate_info(MIRAGE_FILE_FILTER(self),
        "FILTER-ISZ",
        "ISZ File Filter",
        1,
        "Compressed ISO images (*.isz)", "application/x-isz"
    );

    self->priv->num_parts = 0;
    self->priv->parts = NULL;

    self->priv->cached_part = -1;
    self->priv->inflate_buffer = NULL;
    self->priv->io_buffer = NULL;
}

static void mirage_file_filter_isz_finalize (GObject *gobject)
{
    MirageFileFilterIsz *self = MIRAGE_FILE_FILTER_ISZ(gobject);

    g_free(self->priv->parts);
    g_free(self->priv->inflate_buffer);
    g_free(self->priv->io_buffer);

    inflateEnd(&self->priv->zlib_stream);
    BZ2_bzDecompressEnd(&self->priv->bzip2_stream);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_file_filter_isz_parent_class)->finalize(gobject);
}

static void mirage_file_filter_isz_class_init (MirageFileFilterIszClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageFileFilterClass *file_filter_class = MIRAGE_FILE_FILTER_CLASS(klass);

    gobject_class->finalize = mirage_file_filter_isz_finalize;

    file_filter_class->can_handle_data_format = mirage_file_filter_isz_can_handle_data_format;

    file_filter_class->partial_read = mirage_file_filter_isz_partial_read;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageFileFilterIszPrivate));
}

static void mirage_file_filter_isz_class_finalize (MirageFileFilterIszClass *klass G_GNUC_UNUSED)
{
}
