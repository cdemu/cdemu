/*
 *  libMirage: ISZ filter: filter stream
 *  Copyright (C) 2012-2014 Henrik Stokseth
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

#include "filter-isz.h"

#define __debug__ "ISZ-FilterStream"


static const guint8 isz_signature[4] = { 'I', 's', 'Z', '!' };


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_FILTER_STREAM_ISZ_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FILTER_STREAM_ISZ, MirageFilterStreamIszPrivate))

struct _MirageFilterStreamIszPrivate
{
    ISZ_Header header;

    /* Segment list */
    ISZ_Segment *segments;
    gint num_segments;

    MirageStream **streams;

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
static void mirage_filter_stream_isz_fixup_header(ISZ_Header *header)
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
    if (header->header_size > 48) {
        header->checksum1     = GUINT32_FROM_LE(header->checksum1);
        header->data_size     = GUINT32_FROM_LE(header->data_size);
        header->unknown       = GUINT32_FROM_LE(header->unknown);
        header->checksum2     = GUINT32_FROM_LE(header->checksum2);
    }
}

static void mirage_filter_stream_isz_fixup_segment(ISZ_Segment *segment)
{
    segment->size            = GUINT64_FROM_LE(segment->size);

    segment->num_chunks      = GUINT32_FROM_LE(segment->num_chunks);
    segment->first_chunk_num = GUINT32_FROM_LE(segment->first_chunk_num);
    segment->chunk_offs      = GUINT32_FROM_LE(segment->chunk_offs);
    segment->left_size       = GUINT32_FROM_LE(segment->left_size);
}

static inline void mirage_filter_stream_isz_deobfuscate(guint8 *data, gint length)
{
    /* XOR with the NOT'ed version of the ISZ signature */
    for (gint i = 0; i < length; i++) {
        data[i] ^= ~isz_signature[i % 4];
    }
}


/**********************************************************************\
 *                     Part filename generation                       *
\**********************************************************************/
static gchar *create_filename_func (const gchar *main_filename, gint index)
{
    gchar *ret_filename = g_strdup(main_filename);

    if (index) {
        /* Replace last two characters with index */
        gchar *position = ret_filename + strlen(ret_filename) - 2;
        g_snprintf(position, 3, "%02i", index);
    }

    return ret_filename;
}


/**********************************************************************\
 *                           Part indexing                            *
\**********************************************************************/
static gboolean mirage_filter_stream_isz_read_segments (MirageFilterStreamIsz *self, GError **error)
{
    MirageStream *stream = mirage_filter_stream_get_underlying_stream(MIRAGE_FILTER_STREAM(self));

    ISZ_Header *header = &self->priv->header;

    gint ret;
    gboolean count_done = FALSE;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading segments\n", __debug__);

    /* Position at the beginning of the segment table */
    if (!mirage_stream_seek(stream, header->seg_offs, G_SEEK_SET, NULL)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to seek to the beginning of segment table!");
        return FALSE;
    }

    /* Read segments */

    for (gint s = 0;; s++) {
        ISZ_Segment cur_segment;

        /* Read segment */
        ret = mirage_stream_read(stream, &cur_segment, sizeof(ISZ_Segment), NULL);
        if (ret != sizeof(ISZ_Segment)) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to read segment!");
            return FALSE;
        }

        /* De-obfuscate segment */
        mirage_filter_stream_isz_deobfuscate((guint8 *)&cur_segment, sizeof(ISZ_Segment));

        mirage_filter_stream_isz_fixup_segment(&cur_segment);

        /* Are we done? */
        if (cur_segment.size == 0) {
            if (count_done) {
                break; /* Read the last segment */
            } else {
                s = -1; /* Stop counting, start reading dammit! */
                count_done = TRUE;

                /* Allocate segments */
                self->priv->segments = g_try_new(ISZ_Segment, self->priv->num_segments);
                if (!self->priv->segments) {
                    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to allocate memory for segment table!");
                    return FALSE;
                }

                /* Position at the beginning of the segment table */
                if (!mirage_stream_seek(stream, header->seg_offs, G_SEEK_SET, NULL)) {
                    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to seek to the beginning of segment table!");
                    return FALSE;
                }

                continue;
            }
        }

        /* Are we counting or reading? */
        if (!count_done) {
            self->priv->num_segments++;
        } else {
            self->priv->segments[s] = cur_segment;

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: %2d: %lu %u %u %u %u\n", __debug__, s,
                         cur_segment.size, cur_segment.num_chunks, cur_segment.first_chunk_num,
                         cur_segment.chunk_offs, cur_segment.left_size);
        }
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: successfully read %d segments\n\n", __debug__, self->priv->num_segments);

    return TRUE;
}

static gboolean mirage_filter_stream_isz_create_new_segment_table (MirageFilterStreamIsz *self, GError **error)
{
    ISZ_Header *header = &self->priv->header;

    gint sector_count = 0;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating a new segment table\n", __debug__);

    if (header->segment_size) {
        /* Allocate segments */
        self->priv->num_segments = header->total_sectors * header->sect_size / (header->segment_size - header->header_size);
        if (header->total_sectors * header->sect_size % (header->segment_size - header->header_size)) {
            self->priv->num_segments++;
        }
        self->priv->segments = g_try_new(ISZ_Segment, self->priv->num_segments);
        if (!self->priv->segments) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to allocate memory for segment table!");
            return FALSE;
        }

        /* Fill in data for segments */
        for (gint s = 0; s < self->priv->num_segments; s++) {
            ISZ_Segment *cur_segment  = &self->priv->segments[s];
            ISZ_Segment *prev_segment = &self->priv->segments[s - 1];

            cur_segment->first_chunk_num = sector_count;
            cur_segment->size = header->segment_size;

            if (s == self->priv->num_segments - 1) {
                cur_segment->num_chunks = header->num_blocks - sector_count;
                cur_segment->chunk_offs = header->data_offs + prev_segment->left_size;
                cur_segment->left_size = 0;
            } else if (s > 0) {
                cur_segment->num_chunks = (cur_segment->size - header->header_size - prev_segment->left_size) / header->block_size;
                if ((cur_segment->size - header->header_size - prev_segment->left_size) % header->block_size) {
                    cur_segment->num_chunks++;
                }
                cur_segment->chunk_offs = header->data_offs + prev_segment->left_size;
                cur_segment->left_size = (cur_segment->size - header->header_size - prev_segment->left_size) % header->block_size;
            } else {
                cur_segment->num_chunks = (cur_segment->size - header->header_size) / header->block_size;
                if ((cur_segment->size - header->header_size) % header->block_size) {
                    cur_segment->num_chunks++;
                }
                cur_segment->chunk_offs = header->data_offs;
                cur_segment->left_size = (cur_segment->size - header->header_size) % header->block_size;
            }

            sector_count += cur_segment->num_chunks;

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  %2d: %lu %u %u %u %u\n", __debug__, s,
                         cur_segment->size, cur_segment->num_chunks, cur_segment->first_chunk_num,
                         cur_segment->chunk_offs, cur_segment->left_size);
        }
    } else {
        ISZ_Segment *cur_segment;

        /* Allocate segment */
        self->priv->num_segments = 1;
        self->priv->segments = cur_segment = g_try_new(ISZ_Segment, self->priv->num_segments);
        if (!self->priv->segments) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to allocate memory for segment table!");
            return FALSE;
        }

        /* Fill in data for segment */
        cur_segment->size = header->total_sectors * header->sect_size;
        cur_segment->num_chunks = header->num_blocks;
        cur_segment->first_chunk_num = 0;
        cur_segment->chunk_offs = header->data_offs;
        cur_segment->left_size = 0;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   0: %lu %u %u %u %u\n", __debug__,
                     cur_segment->size, cur_segment->num_chunks, cur_segment->first_chunk_num,
                     cur_segment->chunk_offs, cur_segment->left_size);
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: successfully created a new segment table\n", __debug__);

    return TRUE;
}

static gboolean mirage_filter_stream_isz_open_streams (MirageFilterStreamIsz *self, GError **error)
{
    MirageStream **streams;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: opening streams\n", __debug__);

    /* Allocate space for streams */
    self->priv->streams = streams = g_try_new(MirageStream *, self->priv->num_segments);
    if (!streams) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to allocate memory for streams!");
        return FALSE;
    }

    /* Fill in existing stream */
    streams[0] = g_object_ref(g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self)));

    const gchar *original_filename = mirage_stream_get_filename(streams[0]);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  %s\n", __debug__, original_filename);

    /* Create the rest of the streams */
    for (gint s = 1; s < self->priv->num_segments; s++) {
        gchar *filename = create_filename_func(original_filename, s);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  %s\n", __debug__, filename);
        streams[s] = mirage_contextual_create_input_stream (MIRAGE_CONTEXTUAL(self), filename, error);
        if (!streams[s]) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to create stream!");
            return FALSE;
        }
        g_free(filename);
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: sucessfully opened streams\n\n", __debug__);

    return TRUE;
}

static inline void mirage_filter_stream_isz_decode_chunk_ptr(guint8 *chunk_ptr, guint8 ptr_len, guint8 *type, guint32 *length)
{
    guint8  temp_buf[sizeof(guint32)] = {0};
    guint32 *temp_ptr = (guint32 *) temp_buf;

    guint32 chunk_len_bits  = ptr_len * 8 - 2;
    guint32 chunk_type_bits = 2;
    guint32 chunk_len_mask  = (1 << chunk_len_bits) - 1;
    guint32 chunk_type_mask = (1 << chunk_type_bits) - 1;

    g_assert (ptr_len <= sizeof(guint32));

    for (gint b = 0; b < ptr_len; b++) {
        temp_buf[b] = chunk_ptr[b];
    }
    *temp_ptr = GUINT32_FROM_LE(*temp_ptr);

    *length = *temp_ptr & chunk_len_mask;
    *type   = (*temp_ptr >> chunk_len_bits) & chunk_type_mask;
}

static gboolean mirage_filter_stream_isz_read_index (MirageFilterStreamIsz *self, GError **error)
{
    MirageStream *stream = mirage_filter_stream_get_underlying_stream(MIRAGE_FILTER_STREAM(self));
    z_stream  *zlib_stream  = &self->priv->zlib_stream;
    bz_stream *bzip2_stream = &self->priv->bzip2_stream;

    ISZ_Header *header = &self->priv->header;

    gint ret, original_size;
    gint last_segment = 0;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading part index\n", __debug__);

    self->priv->num_parts = header->num_blocks;
    original_size = header->total_sectors * header->sect_size;

    mirage_filter_stream_simplified_set_stream_length(MIRAGE_FILTER_STREAM(self), original_size);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of parts: %d\n", __debug__, self->priv->num_parts);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: original stream size: %ld\n", __debug__, original_size);

    /* At least one part must be present */
    if (!self->priv->num_parts) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: no parts in ISZ file!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "No parts in ISZ file!");
        return FALSE;
    }

    /* Allocate part index */
    self->priv->parts = g_try_new(ISZ_Chunk, self->priv->num_parts);
    if (!self->priv->parts) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to allocate memory for chunk table!");
        return FALSE;
    }

    /* Do we have a chunk table? */
    if (header->chunk_offs) {
        guint8 *chunk_buffer = NULL;
        gint   chunk_buf_size;

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
        if (!mirage_stream_seek(stream, header->chunk_offs, G_SEEK_SET, NULL)) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to seek to the beginning of chunk table!");
            g_free (chunk_buffer);
            return FALSE;
        }

        /* Read chunk table */
        ret = mirage_stream_read(stream, chunk_buffer, chunk_buf_size, NULL);
        if (ret != chunk_buf_size) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "Failed to read index!");
            g_free (chunk_buffer);
            return FALSE;
        }

        /* De-obfuscate chunk table */
        mirage_filter_stream_isz_deobfuscate(chunk_buffer, chunk_buf_size);

        /* Compute index from chunk table */
        for (gint i = 0; i < self->priv->num_parts; i++) {
            guint8 *chunk_ptr = &chunk_buffer[i * header->ptr_len];
            ISZ_Chunk *cur_part  = &self->priv->parts[i];

            /* Calculate index entry */
            mirage_filter_stream_isz_decode_chunk_ptr(chunk_ptr, header->ptr_len, &cur_part->type, &cur_part->length);
        }

        g_free(chunk_buffer);
    }

    /* We don't have a chunk table so initialize a part index */
    else {
        for (gint i = 0; i < self->priv->num_parts; i++) {
            ISZ_Chunk *cur_part  = &self->priv->parts[i];

            cur_part->type = DATA;

            if (i == self->priv->num_parts - 1) {
                cur_part->length = header->total_sectors * header->sect_size % header->block_size;
            } else {
                cur_part->length = header->block_size;
            }
        }
    }

    /* Compute offsets for index */
    for (gint i = 0; i < self->priv->num_parts; i++) {
        ISZ_Chunk *cur_part  = &self->priv->parts[i];
        ISZ_Chunk *prev_part = &self->priv->parts[i - 1];

        /* Calculate input offset */
        if (i == 0) {
            cur_part->offset = 0;
            cur_part->adj_offset = 0;
        } else {
            cur_part->offset = prev_part->offset + prev_part->length;
            cur_part->adj_offset = prev_part->adj_offset + prev_part->length;
        }

        /* Which segment holds this chunk? */
        for (gint s = 0; s < self->priv->num_segments; s++) {
            ISZ_Segment *segment = &self->priv->segments[s];

            if ((i >= segment->first_chunk_num) && (i < segment->first_chunk_num + segment->num_chunks)) {
                cur_part->segment = s;
            }
        }

        if (cur_part->segment > last_segment) {
            last_segment = cur_part->segment;
            cur_part->adj_offset = 0;
        }

        /*MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Part %4u: type: %u offs: %8u adj: %8u len: %6u seg: %2u\n",
                     __debug__, i, cur_part->type, cur_part->offset, cur_part->adj_offset, cur_part->length, cur_part->segment);*/
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

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: successfully read index\n\n", __debug__);

    return TRUE;
}


/**********************************************************************\
 *             MirageFilterStream methods implementations             *
\**********************************************************************/
static gboolean mirage_filter_stream_isz_open (MirageFilterStream *_self, MirageStream *stream, gboolean writable G_GNUC_UNUSED, GError **error)
{
    MirageFilterStreamIsz *self = MIRAGE_FILTER_STREAM_ISZ(_self);

    ISZ_Header *header = &self->priv->header;

    /* Read ISZ header */
    mirage_stream_seek(stream, 0, G_SEEK_SET, NULL);
    if (mirage_stream_read(stream, header, sizeof(ISZ_Header), NULL) != sizeof(ISZ_Header)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Filter cannot handle given data: failed to read ISZ header!");
        return FALSE;
    }

    /* Fixup header endianness */
    mirage_filter_stream_isz_fixup_header(header);

    /* Validate ISZ header */
    if (memcmp(&header->signature, isz_signature, sizeof(isz_signature)) || header->version > 1) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Filter cannot handle given data: invalid ISZ header!");
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing the underlying stream data...\n", __debug__);

    /* Only perform parsing on the first file in a set */
    const gchar *original_filename = mirage_stream_get_filename(stream);
    if (!mirage_helper_has_suffix(original_filename, ".isz")) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, "File is not the first file of a set!");
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

    if (header->header_size > 48) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  checksum1: 0x%x (actually: 0x%x)\n", __debug__, header->checksum1, ~header->checksum1);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  data_size: %u\n", __debug__, header->data_size);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  unknown: %u\n", __debug__, header->unknown);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  checksum2: 0x%x (actually: 0x%x)\n", __debug__, header->checksum2, ~header->checksum2);
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  (note: image does not contain checksums)\n", __debug__);
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");

    /* Read segment table if one exists */
    if (header->seg_offs) {
        if (!mirage_filter_stream_isz_read_segments(self, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing segments failed!\n\n", __debug__);
            return FALSE;
        }
    } else {
        if (!mirage_filter_stream_isz_create_new_segment_table(self, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating a new segment failed!\n\n", __debug__);
            return FALSE;
        }
    }

    /* Stream like you've never streamed before! */
    if (!mirage_filter_stream_isz_open_streams(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: opening streams failed!\n\n", __debug__);
        return FALSE;
    }

    /* Read chunk table */
    if (!mirage_filter_stream_isz_read_index(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing chunks failed!\n\n", __debug__);
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing completed successfully\n\n", __debug__);

    return TRUE;
}

static gssize mirage_filter_stream_isz_read_raw_chunk (MirageFilterStreamIsz *self, guint8 *buffer, gint chunk_num)
{
    const ISZ_Chunk *part = &self->priv->parts[chunk_num];
    ISZ_Segment *segment = &self->priv->segments[part->segment];
    MirageStream *stream = self->priv->streams[part->segment];

    gsize   to_read = part->length;
    gsize   have_read = 0;
    goffset part_offs = segment->chunk_offs + part->adj_offset;
    gsize   part_avail = chunk_num < segment->first_chunk_num + segment->num_chunks - 1 ?
                         part->length : part->length - segment->left_size;
    gint    ret;

    /* Seek to the position */
    if (!mirage_stream_seek(stream, part_offs, G_SEEK_SET, NULL)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to %ld in underlying stream!\n", __debug__, part_offs);
        return -1;
    }

    /* Read raw chunk data */
    ret = mirage_stream_read(stream, &buffer[have_read], MIN(to_read, part_avail), NULL);
    if (ret < 0) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read %d bytes from underlying stream!\n", __debug__, to_read);
        return -1;
    } else if (ret == 0) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unexpectedly reached EOF!\n", __debug__);
        return -1;
    } else if (ret == to_read) {
        have_read += ret;
        to_read -= ret;
    } else if (ret < to_read) {
        /*MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading remaining data!\n", __debug__);*/
        have_read += ret;
        to_read -= ret;
        g_assert(to_read == segment->left_size);

        segment = &self->priv->segments[part->segment + 1];
        stream = self->priv->streams[part->segment + 1];
        part_offs = segment->chunk_offs - to_read;

        /* Seek to the position */
        if (!mirage_stream_seek(stream, part_offs, G_SEEK_SET, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to %ld in underlying stream!\n", __debug__, part_offs);
            return -1;
        }

        /* Read raw chunk data */
        ret = mirage_stream_read(stream, &buffer[have_read], to_read, NULL);
        if (ret < 0) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read %d bytes from underlying stream!\n", __debug__, to_read);
            return -1;
        } else if (ret == 0) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unexpectedly reached EOF!\n", __debug__);
            return -1;
        } else if (ret == to_read) {
            have_read += ret;
            to_read -= ret;
        }
    }

    g_assert(to_read == 0 && have_read == part->length);

    return have_read;
}

static gssize mirage_filter_stream_isz_partial_read (MirageFilterStream *_self, void *buffer, gsize count)
{
    MirageFilterStreamIsz *self = MIRAGE_FILTER_STREAM_ISZ(_self);
    goffset position = mirage_filter_stream_simplified_get_position(MIRAGE_FILTER_STREAM(self));
    gint part_idx;

    /* Find part that corresponds to current position */
    part_idx = position / self->priv->header.block_size;

    if (part_idx >= self->priv->num_parts) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: stream position %ld (0x%lX) beyond end of stream, doing nothing!\n", __debug__, position, position);
        return 0;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: stream position: %ld (0x%lX) -> part #%d (cached: #%d)\n", __debug__, position, position, part_idx, self->priv->cached_part);

    /* If we do not have part in cache, uncompress it */
    if (part_idx != self->priv->cached_part) {
        const ISZ_Chunk *part = &self->priv->parts[part_idx];
        z_stream  *zlib_stream = &self->priv->zlib_stream;
        bz_stream *bzip2_stream = &self->priv->bzip2_stream;

        gint ret;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: part not cached, reading...\n", __debug__);

        /* Read a part, either zero, raw or compressed */
        if (part->type == ZERO) {
            /* Return a zero-filled buffer */
            memset (self->priv->inflate_buffer, 0, self->priv->inflate_buffer_size);
        } else if (part->type == DATA) {
            /* Read uncompressed part */
            ret = mirage_filter_stream_isz_read_raw_chunk (self, self->priv->inflate_buffer, part_idx);
            if (ret != part->length) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read raw chunk!\n", __debug__);
                return -1;
            }
        } else if (part->type == ZLIB) {
            /* Reset inflate engine */
            ret = inflateReset2(zlib_stream, 15);
            if (ret != Z_OK) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to reset inflate engine!\n", __debug__);
                return -1;
            }

            /* Uncompress whole part */
            zlib_stream->avail_in  = part->length;
            zlib_stream->next_in   = self->priv->io_buffer;
            zlib_stream->avail_out = self->priv->inflate_buffer_size;
            zlib_stream->next_out  = self->priv->inflate_buffer;

            /* Read some compressed data */
            ret = mirage_filter_stream_isz_read_raw_chunk (self, self->priv->io_buffer, part_idx);
            if (ret != part->length) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read raw chunk!\n", __debug__);
                return -1;
            }

            /* Inflate */
            do {
                ret = inflate(zlib_stream, Z_NO_FLUSH);
                if (ret == Z_NEED_DICT || ret == Z_MEM_ERROR || ret == Z_DATA_ERROR) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to inflate part: %s!\n", __debug__, zlib_stream->msg);
                    return -1;
                }
            } while (zlib_stream->avail_in);
        } else if (part->type == BZ2) {
            /* Reset decompress engine */
            ret = BZ2_bzDecompressInit(bzip2_stream, 0, 0);
            if (ret != BZ_OK) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to initialize decompress engine!\n", __debug__);
                return -1;
            }

            /* Uncompress whole part */
            bzip2_stream->avail_in  = part->length;
            bzip2_stream->next_in   = (gchar *) self->priv->io_buffer;
            bzip2_stream->avail_out = self->priv->inflate_buffer_size;
            bzip2_stream->next_out  = (gchar *) self->priv->inflate_buffer;

            /* Read some compressed data */
            ret = mirage_filter_stream_isz_read_raw_chunk (self, self->priv->io_buffer, part_idx);
            if (ret != part->length) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read raw chunk!\n", __debug__);
                return -1;
            }

            /* Restore a correct header */
            memcpy (self->priv->io_buffer, "BZh", 3);

            /* Inflate */
            do {
                ret = BZ2_bzDecompress(bzip2_stream);
                if (ret < 0) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to inflate part: %d!\n", __debug__, ret);
                    return -1;
                }
            } while (bzip2_stream->avail_in);

            /* Uninitialize decompress engine */
            ret = BZ2_bzDecompressEnd(bzip2_stream);
            if (ret != BZ_OK) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to uninitialize decompress engine!\n", __debug__);
                return -1;
            }
        } else {
            /* We should never get here... */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Encountered unknown chunk type %u!\n", __debug__, part->type);
            return -1;
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
G_DEFINE_DYNAMIC_TYPE(MirageFilterStreamIsz, mirage_filter_stream_isz, MIRAGE_TYPE_FILTER_STREAM);

void mirage_filter_stream_isz_type_register (GTypeModule *type_module)
{
    return mirage_filter_stream_isz_register_type(type_module);
}


static void mirage_filter_stream_isz_init (MirageFilterStreamIsz *self)
{
    self->priv = MIRAGE_FILTER_STREAM_ISZ_GET_PRIVATE(self);

    mirage_filter_stream_generate_info(MIRAGE_FILTER_STREAM(self),
        "FILTER-ISZ",
        "ISZ File Filter",
        FALSE,
        1,
        "Compressed ISO images (*.isz)", "application/x-isz"
    );

    self->priv->num_segments = 0;
    self->priv->segments = NULL;

    self->priv->streams = NULL;

    self->priv->num_parts = 0;
    self->priv->parts = NULL;

    self->priv->cached_part = -1;
    self->priv->inflate_buffer = NULL;
    self->priv->io_buffer = NULL;
}

static void mirage_filter_stream_isz_finalize (GObject *gobject)
{
    MirageFilterStreamIsz *self = MIRAGE_FILTER_STREAM_ISZ(gobject);

    for (gint s = 0; s < self->priv->num_segments; s++) {
        g_object_unref(self->priv->streams[s]);
    }
    g_free(self->priv->streams);

    g_free(self->priv->segments);
    g_free(self->priv->parts);
    g_free(self->priv->inflate_buffer);
    g_free(self->priv->io_buffer);

    inflateEnd(&self->priv->zlib_stream);
    BZ2_bzDecompressEnd(&self->priv->bzip2_stream);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_filter_stream_isz_parent_class)->finalize(gobject);
}

static void mirage_filter_stream_isz_class_init (MirageFilterStreamIszClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageFilterStreamClass *filter_stream_class = MIRAGE_FILTER_STREAM_CLASS(klass);

    gobject_class->finalize = mirage_filter_stream_isz_finalize;

    filter_stream_class->open = mirage_filter_stream_isz_open;

    filter_stream_class->simplified_partial_read = mirage_filter_stream_isz_partial_read;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageFilterStreamIszPrivate));
}

static void mirage_filter_stream_isz_class_finalize (MirageFilterStreamIszClass *klass G_GNUC_UNUSED)
{
}
