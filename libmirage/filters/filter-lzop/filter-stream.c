/*
 *  libMirage: LZOP filter: filter stream
 *  Copyright (C) 2026 Rok Mandeljc
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

#include "filter-lzop.h"

#define __debug__ "LZOP-FilterStream"


static const unsigned char lzop_signature[9] = {0x89, 0x4c, 0x5a, 0x4f, 0x00, 0x0d, 0x0a, 0x1a, 0x0a};

#define ADLER32_INIT_VALUE 1
#define CRC32_INIT_VALUE 0

/* Subset of header flags relevant to this stream */
#define FLAG_ADLER32_D     0x00000001L
#define FLAG_ADLER32_C     0x00000002L
#define FLAG_STDIN         0x00000004L
#define FLAG_H_EXTRA_FIELD 0x00000040L
#define FLAG_CRC32_D       0x00000100L
#define FLAG_CRC32_C       0x00000200L
#define FLAG_MULTIPART     0x00000400L
#define FLAG_H_FILTER      0x00000800L
#define FLAG_H_CRC32       0x00001000L

/* Subset of compression methods supported by this stream */
#define METHOD_LZO1X_1 1
#define METHOD_LZO1X_1_15 2
#define METHOD_LZO1X_999 3

#define MAX_BLOCK_SIZE (64*1024l*1024l)

typedef struct
{
    /* NOTE: original lzop code uses 32-bit "unsigned" types here, but
     * reads them as 16-bit types... */
    guint16 version;
    guint16 lib_version;
    guint16 version_needed_to_extract;

    guint8 method;
    guint8 level;
    guint32 flags;
    guint32 filter;
    guint32 mode;

    guint32 mtime_low;
    guint32 mtime_high;

    guint32 header_checksum;

    guint32 extra_field_len;
    guint32 extra_field_checksum;

    gchar method_name[32];

    gchar name[255 + 1];
} lzop_header_t;

typedef struct
{
    guint32 compressed_size;
    guint32 uncompressed_size;

    guint64 compressed_offset;
    guint64 uncompressed_offset;

    guint32 compressed_crc32;
    guint32 compressed_adler32;

    guint32 uncompressed_crc32;
    guint32 uncompressed_adler32;
} lzop_block_t;

/**********************************************************************\
 *                  Object and its private structure                  *
\**********************************************************************/
struct _MirageFilterStreamLzopPrivate
{
    /* lzop header */
    lzop_header_t header;

    /* Block index */
    lzop_block_t *blocks;
    gint num_blocks;

    /* I/O buffer */
    guint8 *io_buffer;
    gint io_buffer_size;

    /* Cache */
    gint cached_block;
    guint8 *block_buffer;
    gint block_buffer_size;
};


G_DEFINE_DYNAMIC_TYPE_EXTENDED(
    MirageFilterStreamLzop,
    mirage_filter_stream_lzop,
    MIRAGE_TYPE_FILTER_STREAM,
    0,
    G_ADD_PRIVATE_DYNAMIC(MirageFilterStreamLzop)
)

void mirage_filter_stream_lzop_type_register (GTypeModule *type_module)
{
    mirage_filter_stream_lzop_register_type(type_module);
}


/**********************************************************************\
 *                   Helpers for reading stream data                  *
\**********************************************************************/
static gboolean mirage_filter_stream_lzop_read_data (MirageStream *stream, guint8 *buffer, gsize length, guint32 *adler32, guint32 *crc32)
{
    /* Read data */
    if (mirage_stream_read(stream, buffer, length, NULL) != (gssize)length) {
        return FALSE;
    }

    /* Update checksums */
    if (adler32) {
        *adler32 = lzo_adler32(*adler32, buffer, length);
    }
    if (crc32) {
        *crc32 = lzo_crc32(*crc32, buffer, length);
    }

    return TRUE;
}

static gboolean mirage_filter_stream_lzop_read_guint8 (MirageStream *stream, guint8 *out, guint32 *adler32, guint32 *crc32)
{
    guint8 buffer[1];
    if (!mirage_filter_stream_lzop_read_data(stream, buffer, sizeof(buffer), adler32, crc32)) {
        return FALSE;
    }

    if (out) {
        *out = *buffer;
    }

    return TRUE;
}

static gboolean mirage_filter_stream_lzop_read_guint16 (MirageStream *stream, guint16 *out, guint32 *adler32, guint32 *crc32)
{
    guint8 buffer[2];
    if (!mirage_filter_stream_lzop_read_data(stream, buffer, sizeof(buffer), adler32, crc32)) {
        return FALSE;
    }

    if (out) {
        *out = GUINT16_FROM_BE(*(guint16 *)(buffer));
    }

    return TRUE;
}

static gboolean mirage_filter_stream_lzop_read_guint32 (MirageStream *stream, guint32 *out, guint32 *adler32, guint32 *crc32)
{
    guint8 buffer[4];
    if (!mirage_filter_stream_lzop_read_data(stream, buffer, sizeof(buffer), adler32, crc32)) {
        return FALSE;
    }

    if (out) {
        *out = GUINT32_FROM_BE(*(guint32 *)(buffer));
    }

    return TRUE;
}


static gboolean mirage_filter_stream_lzop_read_header (MirageFilterStreamLzop *self, MirageStream *stream)
{
    lzop_header_t *header = &self->priv->header;

    guint32 adler32;
    guint32 crc32;

    memset(header, 0, sizeof(lzop_header_t));
    header->version_needed_to_extract = 0x0900;  /* first public lzop version */

    adler32 = ADLER32_INIT_VALUE;
    crc32 = CRC32_INIT_VALUE;

    /* Version */
    if (!mirage_filter_stream_lzop_read_guint16(stream, &header->version, &adler32, &crc32)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read header->version!", __debug__);
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: version: 0x%X", __debug__, header->version);
    if (header->version < 0x0900) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: invalid value in header->version (0x%X)!", __debug__, header->version);
        return FALSE;
    }

    /* Library version */
    if (!mirage_filter_stream_lzop_read_guint16(stream, &header->lib_version, &adler32, &crc32)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read header->lib_version!", __debug__);
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: library version: 0x%X", __debug__, header->lib_version);

    /* Version needed to extract */
    if (header->version >= 0x0940) {
        if (!mirage_filter_stream_lzop_read_guint16(stream, &header->version_needed_to_extract, &adler32, &crc32)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read header->version_needed_to_extract!", __debug__);
            return FALSE;
        }
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: version needed to extract: 0x%X", __debug__, header->version_needed_to_extract);

        /* This corresponds to lzop 1.04, the latest at the time of writing */
        if (header->version_needed_to_extract > 0x1040) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: incompatible header->version_needed_to_extract value (0x%X)!", __debug__, header->version_needed_to_extract);
            return FALSE;
        }
        if (header->version_needed_to_extract < 0x0900) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: invalid header->version_needed_to_extract value (0x%X)!", __debug__, header->version_needed_to_extract);
            return FALSE;
        }
    }

    /* Method */
    if (!mirage_filter_stream_lzop_read_guint8(stream, &header->method, &adler32, &crc32)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read header->method!", __debug__);
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: method: 0x%X", __debug__, header->method);

    /* Level */
    if (header->version >= 0x0940) {
        if (!mirage_filter_stream_lzop_read_guint8(stream, &header->level, &adler32, &crc32)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read header->level!", __debug__);
            return FALSE;
        }
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: level: %d", __debug__, header->level);

    /* Flags */
    if (!mirage_filter_stream_lzop_read_guint32(stream, &header->flags, &adler32, &crc32)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read header->flags!", __debug__);
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: flags: 0x%08X", __debug__, header->flags);

    /* Filter */
    if (header->flags & FLAG_H_FILTER) {
        if (!mirage_filter_stream_lzop_read_guint32(stream, &header->filter, &adler32, &crc32)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read header->filter!", __debug__);
            return FALSE;
        }
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: filter: 0x%08X", __debug__, header->flags);

    /* Mode */
    if (!mirage_filter_stream_lzop_read_guint32(stream, &header->mode, &adler32, &crc32)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read header->mode!", __debug__);
        return FALSE;
    }
    if (header->flags & FLAG_STDIN) {
        header->mode = 0; /* do not use mode from stdin compression */
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: mode: 0x%08X", __debug__, header->mode);

    /* Modification time */
    if (!mirage_filter_stream_lzop_read_guint32(stream, &header->mtime_low, &adler32, &crc32)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read header->mtime_low!", __debug__);
        return FALSE;
    }
    if (header->version >= 0x0940) {
        if (!mirage_filter_stream_lzop_read_guint32(stream, &header->mtime_high, &adler32, &crc32)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read header->mtime_high!", __debug__);
            return FALSE;
        }
    }
    if (header->version < 0x0120) {
        if (header->mtime_low == 0xffffffffUL) {
            header->mtime_low = 0;
        }
        header->mtime_high = 0;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: mtime (low): 0x%08X", __debug__, header->mtime_low);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: mtime (high): 0x%08X", __debug__, header->mtime_high);

    /* Name */
    guint8 name_length;
    if (!mirage_filter_stream_lzop_read_guint8(stream, &name_length, &adler32, &crc32)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read header->name_length!", __debug__);
        return FALSE;
    }
    if (name_length > 0) {
        if (!mirage_filter_stream_lzop_read_data(stream, (unsigned char *)header->name, name_length, &adler32, &crc32)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read header->name (%d bytes)!", __debug__, name_length);
            return FALSE;
        }
        header->name[name_length] = 0;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: name (%d): %.*s", __debug__, name_length, name_length, header->name);

    /* Checksum */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: computed checksum (CRC32): 0x%X", __debug__, crc32);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: computed checksum (ADLER32): 0x%X", __debug__, adler32);
    const guint32 computed_checksum = (header->flags & FLAG_H_CRC32) ? crc32 : adler32;

    if (!mirage_filter_stream_lzop_read_guint32(stream, &header->header_checksum, &adler32, &crc32)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read header->header_checksum!", __debug__);
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: header checksum (%s): 0x%X", __debug__, (header->flags & FLAG_H_CRC32) ? "CRC32" : "ADLER32", header->header_checksum);

    if (header->header_checksum != computed_checksum) {
        /* Invalid checksum */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: header checksum (%s) mismatch: 0x%X vs computed 0x%X!", __debug__, (header->flags & FLAG_H_CRC32) ? "CRC32" : "ADLER32", header->header_checksum, computed_checksum);
        return FALSE;
    }

    /* Validate method */
    if (header->method <= 0) {
        /* Invalid method */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: invalid header->method value (%d)!", __debug__, header->method);
        return FALSE;
    }

    /* Equivalent to x_get_method() and lzo_get_method(); we support only
     * LZO here... */
    switch (header->method) {
        case METHOD_LZO1X_1: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: method name: LZO1X-1", __debug__);
            if (header->level == 0) {
                header->level = 3;
            }
            break;
        }
        case METHOD_LZO1X_1_15: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: method name: LZO1X-1(15)", __debug__);
            if (header->level == 0) {
                header->level = 1;
            }
            break;
        }
        case METHOD_LZO1X_999: {
            if (header->level == 0) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: method name: LZO1X-999", __debug__);
                header->level = 9;
            } else if (header->version >= 0x0950 && header->lib_version >= 0x1020) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: method name: LZO1X-999/%d", __debug__, header->level);
            }
            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unsupported header->method value (%d)!", __debug__, header->method);
            return FALSE;
        }
    }

    /* Check compression level */
    if (header->level < 1 || header->level > 9) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: invalid header->method value (%d)!", __debug__, header->level);
        return FALSE;
    }

    /* NOTE: we do not care about reserved flags being set... */

    /* Skip extra field */
    if (header->flags & FLAG_H_EXTRA_FIELD) {
        /* Note: checksum also covers the length */
        adler32 = ADLER32_INIT_VALUE;
        crc32 = CRC32_INIT_VALUE;

        if (!mirage_filter_stream_lzop_read_guint32(stream, &header->extra_field_len, &adler32, &crc32)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read header->extra_field_len!", __debug__);
            return FALSE;
        }
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: extra field length: %d", __debug__, header->extra_field_len);

        for (guint i = 0; i < header->extra_field_len; i++) {
            if (!mirage_filter_stream_lzop_read_guint8(stream, NULL, &adler32, &crc32)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read byte %d of header->extra_field_len!", __debug__, i);
                return FALSE;
            }
        }

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: extra field computed checksum (CRC32): 0x%X", __debug__, crc32);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: extra field computed checksum (ADLER32): 0x%X", __debug__, adler32);
        const guint32 computed_extra_field_checksum = (header->flags & FLAG_H_CRC32) ? crc32 : adler32;

        if (!mirage_filter_stream_lzop_read_guint32(stream, &header->extra_field_checksum, &adler32, &crc32)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read header->extra_field_checksum!", __debug__);
            return FALSE;
        }
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: extra field checksum (%s): 0x%X", __debug__, (header->flags & FLAG_H_CRC32) ? "CRC32" : "ADLER32", header->extra_field_checksum);

        if (header->extra_field_checksum != computed_extra_field_checksum) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: extra field checksum (%s) mismatch: 0x%X vs computed 0x%X!", __debug__, (header->flags & FLAG_H_CRC32) ? "CRC32" : "ADLER32", header->extra_field_checksum, computed_extra_field_checksum);
            return FALSE;
        }
    }

    return TRUE;
}


static gboolean mirage_filter_stream_lzop_build_index (MirageFilterStreamLzop *self, MirageStream *stream)
{
    const lzop_header_t *header = &self->priv->header;

    guint64 total_size = 0;

    guint32 max_compressed_size = 0;
    guint32 max_uncompressed_size = 0;

    GArray *blocks = g_array_new(FALSE, FALSE, sizeof(lzop_block_t));

    while (1) {
        lzop_block_t block;
        memset(&block, 0, sizeof(block));

        /* Read size of uncompressed block */
        if (!mirage_filter_stream_lzop_read_guint32(stream, &block.uncompressed_size, NULL, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read uncompressed block size!", __debug__);
            return FALSE;
        }

        if (block.uncompressed_size == 0xffffffffUL) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: split files are not supported!", __debug__);
            return FALSE;
        }

        if (block.uncompressed_size > MAX_BLOCK_SIZE) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: uncompressed block size (%d) exceeds maximum block size (%ld)!", __debug__, block.uncompressed_size, MAX_BLOCK_SIZE);
            return FALSE;
        }

        /* Exit if last block */
        if (block.uncompressed_size == 0) {
            break;
        }

        /* Read size of compressed block */
        if (!mirage_filter_stream_lzop_read_guint32(stream, &block.compressed_size, NULL, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read compressed block size!", __debug__);
            return FALSE;
        }

        if (block.compressed_size <= 0 || block.compressed_size > block.uncompressed_size) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: invalid compressed block size (%d)!", __debug__, block.compressed_size);
            return FALSE;
        }

        /* Read checksum of uncompressed block */
        if (header->flags & FLAG_ADLER32_D) {
            if (!mirage_filter_stream_lzop_read_guint32(stream, &block.uncompressed_adler32, NULL, NULL)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to ADLER32 checksum of uncompressed block!", __debug__);
                return FALSE;
            }
        }

        if (header->flags & FLAG_CRC32_D) {
            if (!mirage_filter_stream_lzop_read_guint32(stream, &block.uncompressed_crc32, NULL, NULL)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to CRC32 checksum of uncompressed block!", __debug__);
                return FALSE;
            }
        }

        /* Read checksum of compressed block */
        if (header->flags & FLAG_ADLER32_C) {
            if (block.compressed_size < block.uncompressed_size) {
                if (!mirage_filter_stream_lzop_read_guint32(stream, &block.compressed_adler32, NULL, NULL)) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to ADLER32 checksum of compressed block!", __debug__);
                    return FALSE;
                }
            } else {
                block.compressed_adler32 = block.uncompressed_adler32;
            }
        }
        if (header->flags & FLAG_CRC32_C) {
            if (block.compressed_size < block.uncompressed_size) {
                if (!mirage_filter_stream_lzop_read_guint32(stream, &block.compressed_crc32, NULL, NULL)) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to CRC32 checksum of compressed block!", __debug__);
                    return FALSE;
                }
            } else {
                block.compressed_crc32 = block.uncompressed_crc32;
            }
        }

        /* Record the source and destination data offsets */
        block.compressed_offset = mirage_stream_tell(stream);
        block.uncompressed_offset = total_size;

        if (!mirage_stream_seek(stream, block.compressed_size, G_SEEK_CUR, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to start of next block!", __debug__);
            return FALSE;
        }

        /* Prepare block entry */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: block entry:", __debug__);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - compressed size: %d", __debug__, block.compressed_size);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - uncompressed size: %d", __debug__, block.uncompressed_size);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - compressed offset: %" G_GINT64_MODIFIER "u (0x%" G_GINT64_MODIFIER "X)", __debug__, block.compressed_offset, block.compressed_offset);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - uncompressed offset: %" G_GINT64_MODIFIER "u (0x%" G_GINT64_MODIFIER "X)", __debug__, block.uncompressed_offset, block.uncompressed_offset);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - compressed data CRC32: 0x%X", __debug__, block.compressed_crc32);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - compressed data ADLER32: 0x%X", __debug__, block.compressed_adler32);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - uncompressed data CRC32: 0x%X", __debug__, block.uncompressed_crc32);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - uncompressed data ADLER32: 0x%X", __debug__, block.uncompressed_adler32);

        total_size += block.uncompressed_size;

        max_compressed_size = MAX(max_compressed_size, block.compressed_size);
        max_uncompressed_size = MAX(max_uncompressed_size, block.uncompressed_size);

        g_array_append_val(blocks, block);
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: total data length = %" G_GINT64_MODIFIER "u (0x%" G_GINT64_MODIFIER "X)", __debug__, total_size, total_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: max compressed block size = %d (0x%X)", __debug__, max_compressed_size, max_compressed_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: max uncompressed block size = %d (0x%X)", __debug__, max_uncompressed_size, max_uncompressed_size);

    /* Free the blocks array and take its data */
    self->priv->num_blocks = blocks->len;
    self->priv->blocks = (lzop_block_t *)g_array_free(blocks, FALSE);

    /* Store the required buffer sizes */
    self->priv->io_buffer_size = max_compressed_size;
    self->priv->block_buffer_size = max_uncompressed_size;

    /* Set total size */
    mirage_filter_stream_simplified_set_stream_length(MIRAGE_FILTER_STREAM(self), total_size);

    return TRUE;
}


/**********************************************************************\
 *              MirageFilterStream methods implementations            *
\**********************************************************************/
static gboolean mirage_filter_stream_lzop_open (MirageFilterStream *_self, MirageStream *stream, gboolean writable G_GNUC_UNUSED, GError **error)
{
    MirageFilterStreamLzop *self = MIRAGE_FILTER_STREAM_LZOP(_self);
    guint8 sig[9];

    /* Look for lzop signature at the beginning */
    mirage_stream_seek(stream, 0, G_SEEK_SET, NULL);
    if (mirage_stream_read(stream, sig, sizeof(sig), NULL) != sizeof(sig)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Filter cannot handle given data: failed to read 9 signature bytes!"));
        return FALSE;
    }

    /* Check signature */
    if (memcmp(sig, lzop_signature, sizeof(lzop_signature))) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Filter cannot handle given data: invalid signature!"));
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing the underlying stream data...", __debug__);

    /* Read the header */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading lzop header...", __debug__);
    if (!mirage_filter_stream_lzop_read_header(self, stream)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read/process lzop header!", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, Q_("Failed to read lzop header!"));
        return FALSE;
    }

    /* Build index */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: building block index...", __debug__);
    if (!mirage_filter_stream_lzop_build_index(self, stream)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to build block index!", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, Q_("Failed to build block index!"));
        return FALSE;
    }

    /* Allocate buffers */
    self->priv->io_buffer = g_try_malloc(self->priv->io_buffer_size);
    self->priv->block_buffer = g_try_malloc(self->priv->block_buffer_size);

    if (!self->priv->io_buffer || !self->priv->block_buffer) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, Q_("Failed to allocate buffers!"));
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing completed successfully", __debug__);

    return TRUE;
}

static gint mirage_filter_stream_lzop_find_block (MirageFilterStreamLzop *self, guint64 position)
{
    const lzop_block_t *block;
    gint block_index;

    /* Check if position is within currently cached block */
    block_index = self->priv->cached_block;
    if (block_index != -1 ) {
        block = &self->priv->blocks[block_index];
        if (position >= block->uncompressed_offset && position < (block->uncompressed_offset + block->uncompressed_size)) {
            return block_index;
        }
    }

    /* Check if it's within the first block */
    block_index = 0;
    block = &self->priv->blocks[block_index];
    if (position >= block->uncompressed_offset && position < (block->uncompressed_offset + block->uncompressed_size)) {
        return block_index;
    }

    /* Check if it's within the last block */
    block_index = self->priv->num_blocks - 1;
    block = &self->priv->blocks[block_index];
    if (position >= block->uncompressed_offset && position < (block->uncompressed_offset + block->uncompressed_size)) {
        return block_index;
    }

    /* Seek from currently cached block */
    block_index = (self->priv->cached_block != -1) ? self->priv->cached_block : 0;
    block = &self->priv->blocks[block_index];

    if (position < block->uncompressed_offset) {
        /* Seek backward (first block has already been checked) */
        for (gint i = block_index; i > 0; i--) {
            block = &self->priv->blocks[i];
            if (position >= block->uncompressed_offset) {
                return i;
            }
        }
    } else {
        /* Seek forward (last block has already been checked) */
        for (gint i = block_index; i < self->priv->num_blocks - 1; i++) {
            block = &self->priv->blocks[i];
            if (position < block->uncompressed_offset + block->uncompressed_size) {
                return i;
            }
        }
    }

    /* Block not found */
    return -1;
}

static gssize mirage_filter_stream_lzop_partial_read (MirageFilterStream *_self, void *buffer, gsize count)
{
    MirageFilterStreamLzop *self = MIRAGE_FILTER_STREAM_LZOP(_self);
    MirageStream *stream = mirage_filter_stream_get_underlying_stream(_self);
    goffset position = mirage_filter_stream_simplified_get_position(_self);

    const lzop_header_t *header = &self->priv->header;

    const lzop_block_t *block;
    gint block_idx;

    /* Find block that corresponds to current position */
    block_idx = mirage_filter_stream_lzop_find_block(self, position);
    if (block_idx == -1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: stream position %" G_GOFFSET_MODIFIER "d (0x%" G_GOFFSET_MODIFIER "X) beyond end of stream, doing nothing!", __debug__, position, position);
        return 0;
    }
    block = &self->priv->blocks[block_idx];

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: stream position: %" G_GOFFSET_MODIFIER "d (0x%" G_GOFFSET_MODIFIER "X) -> block #%d (cached: #%d)", __debug__, position, position, block_idx, self->priv->cached_block);

    /* If we do not have block in cache, uncompress it */
    if (block_idx != self->priv->cached_block) {
        gint ret;
        gboolean is_compressed = block->compressed_size < block->uncompressed_size;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: block not cached, reading...", __debug__);

        /* Seek to the position of compressed block data */
        if (!mirage_stream_seek(stream, block->compressed_offset, G_SEEK_SET, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to %" G_GOFFSET_MODIFIER "d in underlying stream!", __debug__, block->compressed_offset);
            return -1;
        }

        /* Read the whole compressed block data */
        ret = mirage_stream_read(stream, is_compressed ? self->priv->io_buffer : self->priv->block_buffer, block->compressed_size, NULL);
        if (ret == -1) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read %d bytes from underlying stream!", __debug__, block->compressed_size);
            return -1;
        } else if (ret == 0) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unexpectedly reached EOF!", __debug__);
            return -1;
        } else if ((guint)ret != block->compressed_size) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read complete compressed block (%d out of %d bytes)!", __debug__, ret, block->compressed_size);
            return -1;
        }

        /* Verify checksum on compressed data */
        if (is_compressed) {
            if (header->flags & FLAG_ADLER32_C) {
                guint32 adler32 = lzo_adler32(ADLER32_INIT_VALUE, self->priv->io_buffer, block->compressed_size);
                if (adler32 != block->compressed_adler32) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: ADLER32 checksum mismatch for compressed block #%d (0x%X vs computed 0x%X)!", __debug__, block_idx, block->compressed_adler32, adler32);
                    return -1;
                }
            }
            if (header->flags & FLAG_CRC32_C) {
                guint32 crc32 = lzo_crc32(CRC32_INIT_VALUE, self->priv->io_buffer, block->compressed_size);
                if (crc32 != block->compressed_crc32) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: CRC32 checksum mismatch for compressed block #%d (0x%X vs computed 0x%X)!", __debug__, block_idx, block->compressed_crc32, crc32);
                    return -1;
                }
            }
        }

        /* Decompress, if necessary */
        if (is_compressed) {
            lzo_uint decompressed_size = block->uncompressed_size;
            ret = lzo1x_decompress(self->priv->io_buffer, block->compressed_size, self->priv->block_buffer, &decompressed_size, NULL);
            if (ret != LZO_E_OK || decompressed_size != block->uncompressed_size) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to decompress block #%d; status = %d, size = %ld vs expected %d!", __debug__, block_idx, ret, decompressed_size, block->uncompressed_size);
                return -1;
            }
        }

        /* Verify checksum on decompressed data */
        if (header->flags & FLAG_ADLER32_D) {
            guint32 adler32 = lzo_adler32(ADLER32_INIT_VALUE, self->priv->block_buffer, block->uncompressed_size);
            if (adler32 != block->uncompressed_adler32) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: ADLER32 checksum mismatch for uncompressed block #%d (0x%X vs computed 0x%X)!", __debug__, block_idx, block->uncompressed_adler32, adler32);
                return -1;
            }
        }
        if (header->flags & FLAG_CRC32_D) {
            guint32 crc32 = lzo_crc32(CRC32_INIT_VALUE, self->priv->block_buffer, block->uncompressed_size);
            if (crc32 != block->uncompressed_crc32) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: CRC32 checksum mismatch for uncompressed block #%d (0x%X vs computed 0x%X)!", __debug__, block_idx, block->uncompressed_crc32, crc32);
                return -1;
            }
        }

        /* Set currently cached block */
        self->priv->cached_block = block_idx;
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: block already cached", __debug__);
    }

    /* Copy data */
    gsize block_offset = position - block->uncompressed_offset;
    count = MIN(count, block->uncompressed_size - block_offset);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: offset within block: %" G_GOFFSET_MODIFIER "d, copying %" G_GSIZE_MODIFIER "d bytes", __debug__, block_offset, count);

    memcpy(buffer, self->priv->block_buffer + block_offset, count);

    return count;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
static void mirage_filter_stream_lzop_init (MirageFilterStreamLzop *self)
{
    self->priv = mirage_filter_stream_lzop_get_instance_private(self);

    mirage_filter_stream_generate_info(MIRAGE_FILTER_STREAM(self),
        "FILTER-LZOP",
        Q_("LZOP File Filter"),
        FALSE,
        1,
        Q_("lzop-compressed images (*.lzo)"), "application/x-lzop"
    );

    self->priv->cached_block = -1;

    self->priv->num_blocks = 0;
    self->priv->blocks = NULL;

    self->priv->io_buffer = NULL;
    self->priv->block_buffer = NULL;
}

static void mirage_filter_stream_lzop_finalize (GObject *gobject)
{
    MirageFilterStreamLzop *self = MIRAGE_FILTER_STREAM_LZOP(gobject);

    g_free(self->priv->blocks);

    g_free(self->priv->io_buffer);
    g_free(self->priv->block_buffer);

    /* Chain up to the parent class */
    G_OBJECT_CLASS(mirage_filter_stream_lzop_parent_class)->finalize(gobject);
}

static void mirage_filter_stream_lzop_class_init (MirageFilterStreamLzopClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageFilterStreamClass *filter_stream_class = MIRAGE_FILTER_STREAM_CLASS(klass);

    gobject_class->finalize = mirage_filter_stream_lzop_finalize;

    filter_stream_class->open = mirage_filter_stream_lzop_open;

    filter_stream_class->simplified_partial_read = mirage_filter_stream_lzop_partial_read;
}

static void mirage_filter_stream_lzop_class_finalize (MirageFilterStreamLzopClass *klass G_GNUC_UNUSED)
{
}
