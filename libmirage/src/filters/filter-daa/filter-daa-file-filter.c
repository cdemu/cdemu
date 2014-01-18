/*
 *  libMirage: DAA file filter: File filter object
 *  Copyright (C) 2008-2012 Rok Mandeljc
 *
 *  Derived from code of GPLed utility daa2iso, written by Luigi Auriemma:
 *  http://aluigi.altervista.org/mytoolz.htm
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

#include "filter-daa.h"

#include <zlib.h>

#include "LzmaDec.h"
#include "Bra.h"

#define __debug__ "DAA-FileFilter"


/* Signatures */
const gchar daa_main_signature[16] = "DAA";
const gchar daa_part_signature[16] = "DAA VOL";

const gchar gbi_main_signature[16] = "GBI";
const gchar gbi_part_signature[16] = "GBI VOL";


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_FILE_FILTER_DAA_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FILE_FILTER_DAA, MirageFileFilterDaaPrivate))

typedef enum {
    COMPRESSION_NONE = 0x00,
    COMPRESSION_ZLIB = 0x10,
    COMPRESSION_LZMA = 0x20,
} CompressionType;

typedef enum {
    IMAGE_DAA = 0x00,
    IMAGE_GBI = 0x01,
} ImageType;

typedef struct
{
    guint64 offset;
    guint32 length;
    CompressionType compression;
} DAA_Chunk;

typedef struct
{
    GInputStream *stream;
    guint64 offset;
    guint64 start;
    guint64 end;
} DAA_Part;

typedef gchar * (*DAA_create_filename_func) (const gchar *main_filename, gint index);


struct _MirageFileFilterDaaPrivate
{
    /* Filename components */
    const gchar *main_filename;
    DAA_create_filename_func create_filename_func;

    /* Header */
    DAA_MainHeader header;

    ImageType image_type;

    gboolean compressed_chunk_table;
    gboolean obfuscated_chunk_table;
    gboolean obfuscated_bits;
    gint bit_swap_type;

    gint chunk_table_offset;
    gint chunk_data_offset;
    gint chunk_size;

    /* Bitsizes of bit-packed chunk table (format version 2) */
    gint bitsize_type;
    gint bitsize_length;

    /* Chunks table */
    gint num_chunks;
    DAA_Chunk *chunk_table;

    /* Parts table */
    gint num_parts;
    DAA_Part *part_table;

    /* I/O buffer */
    guint8 *io_buffer;
    gint io_buffer_size;

    /* Inflate buffer */
    guint8 *inflate_buffer;
    gint inflate_buffer_size;
    gint cached_chunk; /* Index of currently cached chunk */
    gsize cached_chunk_size;

    /* Compression */
    z_stream zlib_stream;

    CLzmaDec lzma_decoder;

    /* Encryption */
    gboolean encrypted;
    guint8 decryption_table[128][256];
};


/* Allocator for LZMA decoder */
static void *lzma_alloc (void *p G_GNUC_UNUSED, size_t size) { return g_malloc0(size); }
static void lzma_free (void *p G_GNUC_UNUSED, void *address) { g_free(address); }
static ISzAlloc lzma_allocator = { lzma_alloc, lzma_free };


/**********************************************************************\
 *                    Endian-conversion functions                     *
\**********************************************************************/
static inline void daa_format2_header_fix_endian (DAA_Format2Header *header)
{
    header->chunk_table_compressed = GUINT32_FROM_LE(header->chunk_table_compressed);
}

static inline void daa_main_header_fix_endian (DAA_MainHeader *header)
{
    header->chunk_table_offset = GUINT32_FROM_LE(header->chunk_table_offset);
    header->format_version = GUINT32_FROM_LE(header->format_version);
    header->chunk_data_offset = GUINT32_FROM_LE(header->chunk_data_offset);
    header->__dummy__1 = GUINT32_FROM_LE(header->__dummy__1);
    header->__dummy__2 = GUINT32_FROM_LE(header->__dummy__2);
    header->chunk_size = GUINT32_FROM_LE(header->chunk_size);
    header->iso_size = GUINT64_FROM_LE(header->iso_size);
    header->daa_size = GUINT64_FROM_LE(header->daa_size);
    daa_format2_header_fix_endian(&header->format2);
    header->crc = GUINT32_FROM_LE(header->crc);
}

static inline void daa_part_header_fix_endian (DAA_PartHeader *header)
{
    header->chunk_data_offset = GUINT32_FROM_LE(header->chunk_data_offset);
    daa_format2_header_fix_endian(&header->format2);
    header->crc = GUINT32_FROM_LE(header->crc);
}

static inline void daa_descriptor_header_fix_endian (DAA_DescriptorHeader *header)
{
    header->type = GUINT32_FROM_LE(header->type);
    header->length = GUINT32_FROM_LE(header->length);
}

static inline void daa_descriptor_split_fix_endian (DAA_DescriptorSplit *data)
{
    data->num_parts = GUINT32_FROM_LE(data->num_parts);
    data->__dummy__ = GUINT32_FROM_LE(data->__dummy__);
}

static inline void daa_descriptor_encryption_fix_endian (DAA_DescriptorEncryption *data)
{
    data->encryption_type = GUINT32_FROM_LE(data->encryption_type);
    data->password_crc = GUINT32_FROM_LE(data->password_crc);
}


/**********************************************************************\
 *                     Part filename generation                       *
\**********************************************************************/
/* Format: volname.part01.daa, volname.part02.daa, ... */
static gchar *create_filename_func_1 (const gchar *main_filename, gint index)
{
    gchar *ret_filename = g_strdup(main_filename);

    if (index) {
        /* Find last occurence of 01. and print index into it */
        gchar *position = g_strrstr(ret_filename, "01.");
        position += g_snprintf(position, 3, "%02i", index+1);
        *position = '.'; /* Since it got overwritten with terminating 0 */
    }

    return ret_filename;
}

/* Format: volname.part001.daa, volname.part002.daa, ... */
static gchar *create_filename_func_2 (const gchar *main_filename, gint index)
{
    gchar *ret_filename = g_strdup(main_filename);

    if (index) {
        /* Find last occurence of 01. and print index+1 into it */
        gchar *position = g_strrstr(ret_filename, "001.");
        position += g_snprintf(position, 4, "%03i", index+1);
        *position = '.'; /* Since it got overwritten with terminating 0 */
    }

    return ret_filename;
}

/* Format: volname.daa, volname.d00, ... */
static gchar *create_filename_func_3 (const gchar *main_filename, gint index)
{
    gchar *ret_filename = g_strdup(main_filename);

    if (index) {
        /* Replace last two characters with index-1 */
        gchar *position = ret_filename + strlen(ret_filename) - 2;
        g_snprintf(position, 3, "%02i", index-1);
    }

    return ret_filename;
}


/**********************************************************************\
 *                  DAA decryption (Luigi Auriemma)                   *
\**********************************************************************/
static void mirage_filter_daa_create_decryption_table (MirageFileFilterDaa *self, const gchar *pass, gint num)
{
    gint a, b, c, d, s, i, p;
    gint passlen;
    gshort tmp[256];
    guint8 *tab;

    passlen = strlen(pass);
    tab = self->priv->decryption_table[num - 1];
    d = num << 1;

    for (i = 0; i < 256; i++) {
        tmp[i] = i;
    }
    memset(tab, 0, 256);

    if (d <= 64) {
        a = pass[0] >> 5;
        if (a >= d) a = d - 1;
        for (c = 0; c < d; c++) {
            for (s = 0; s != 11;) {
                a++;
                if (a == d) a = 0;
                if (tmp[a] != -1) s++;
            }
            tab[c] = a;
            tmp[a] = -1;
        }
        return;
    }

    a = pass[0];
    b = d - 32;
    a >>= 5;
    tmp[a + 32] = -1;
    tab[0] = a + 32;
    p = 1;

    for (s = 1; s < b; s++) {
        c = 11;
        if (p < passlen) {
            c = pass[p];
            p++;
            if (!c) c = 11;
        }
        for (i = 0; i != c;) {
            a++;
            if (a == d) a = 32;
            if (tmp[a] != -1) i++;
        }
        tmp[a] = -1;
        tab[s] = a;
    }

    i = pass[0] & 7;
    if(!i) i = 7;

    for (; s < d; s++) {
        for (c = 0; c != i;) {
            a++;
            if (a == d) a = 0;
            if (tmp[a] != -1) c++;
        }
        tmp[a] = -1;
        tab[s] = a;
    }

    for (i = 0; i < d; i++) {
        tmp[i] = tab[i];
    }

    i = pass[0] & 24;
    if (i) {
        a = 0;
        for (s = 0; s < d; s++) {
            for (c = 0; c != i;) {
                a++;
                if(a == d) a = 0;
                if(tmp[a] != -1) c++;
            }
            c = tmp[a];
            tmp[a] = -1;
            tab[s] = c;
        }
    }
}

/* Decrypt block of specified size */
static void mirage_filter_daa_decrypt_block (MirageFileFilterDaa *self, guint8 *ret, guint8 *data, gint size)
{
    guint8 c, t, *tab;

    if (!size) {
        return;
    }
    tab = self->priv->decryption_table[size - 1];

    memset(ret, 0, size);
    for (gint i = 0; i < size; i++) {
        c = data[i] & 15;
        t = tab[i << 1];
        if (t & 1) c <<= 4;
        ret[t >> 1] |= c;

        c = data[i] >> 4;
        t = tab[(i << 1) + 1];
        if (t & 1) c <<= 4;
        ret[t >> 1] |= c;
    }
}

static void mirage_file_filter_daa_decrypt_buffer (MirageFileFilterDaa *self, guint8 *data, gint size)
{
    gint blocks, rem;
    guint8 tmp[128];
    guint8 *p;

    blocks = size >> 7;
    for (p = data; blocks--; p += 128) {
        mirage_filter_daa_decrypt_block(self, tmp, p, 128);
        memcpy(p, tmp, 128);
    }

    rem = size & 127;
    if (rem) {
        mirage_filter_daa_decrypt_block(self, tmp, p, rem);
        memcpy(p, tmp, rem);
    }
}

static void mirage_file_filter_daa_initialize_decryption (MirageFileFilterDaa *self, guint8 *pwdkey, const gchar *password, guint8 *daakey)
{
    /* Create decryption table */
    for (gint i = 1; i <= 128; i++) {
        mirage_filter_daa_create_decryption_table(self, password, i);
    }

    /* Get password hash */
    mirage_filter_daa_decrypt_block(self, pwdkey, daakey, 128);
}


/**********************************************************************\
 *                            Compression                             *
\**********************************************************************/
static gboolean mirage_file_filter_daa_initialize_zlib (MirageFileFilterDaa *self, GError **error)
{
    z_stream *zlib_stream = &self->priv->zlib_stream;
    gint ret;

    zlib_stream->zalloc = Z_NULL;
    zlib_stream->zfree = Z_NULL;
    zlib_stream->opaque = Z_NULL;
    zlib_stream->avail_in = 0;
    zlib_stream->next_in = Z_NULL;

    ret = inflateInit2(zlib_stream, -15);
    if (ret != Z_OK) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to initialize zlib decoder!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Failed to initialize zlib's inflate (error: %d)!", ret);
        return FALSE;
    }

    return TRUE;
}

static gint mirage_file_filter_daa_inflate_zlib (MirageFileFilterDaa *self, guint8 *in_buf, gsize in_len)
{
    z_stream *zlib_stream = &self->priv->zlib_stream;
    gint ret;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: decompressing using zlib; in_len: %ld bytes\n", __debug__, in_len);
    inflateReset(zlib_stream);

    zlib_stream->next_in = in_buf;
    zlib_stream->avail_in = in_len;
    zlib_stream->next_out = self->priv->inflate_buffer;
    zlib_stream->avail_out = self->priv->inflate_buffer_size;

    ret = inflate(zlib_stream, Z_SYNC_FLUSH);
    if (ret != Z_STREAM_END) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to inflate (error code = %d)!\n", __debug__, ret);
        return 0;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: inflated: %ld bytes, consumed: %ld bytes\n", __debug__, zlib_stream->total_out, zlib_stream->total_in);
    return zlib_stream->total_out;
}


static gboolean mirage_file_filter_daa_initialize_lzma (MirageFileFilterDaa *self, GError **error G_GNUC_UNUSED)
{
    LzmaDec_Construct(&self->priv->lzma_decoder);
    LzmaDec_Allocate(&self->priv->lzma_decoder, self->priv->header.format2.lzma_props, LZMA_PROPS_SIZE, &lzma_allocator);
    return TRUE;
}

static gint mirage_file_filter_daa_inflate_lzma (MirageFileFilterDaa *self, guint8 *in_buf, gsize in_len)
{
    ELzmaStatus status;
    SizeT inlen, outlen;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: decompressing using LZMA; in_len: %ld bytes\n", __debug__, in_len);

    /* Initialize decoder */
    LzmaDec_Init(&self->priv->lzma_decoder);

    /* LZMA */
    inlen = in_len;
    outlen = self->priv->inflate_buffer_size;
    if (LzmaDec_DecodeToBuf(&self->priv->lzma_decoder, self->priv->inflate_buffer, &outlen, in_buf, &inlen, LZMA_FINISH_END, &status) != SZ_OK) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to inflate (status: %d)!\n", __debug__, status);
        return 0;
    }

    /* Filter */
    switch (self->priv->header.format2.lzma_filter) {
        case 0: {
            /* No filter */
            break;
        }
        case 1: {
            /* x86 BCJ filter */
            guint32 state;
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: applying x86 BCJ filter to decompressed data\n", __debug__);
            x86_Convert_Init(state);
            x86_Convert(self->priv->inflate_buffer, outlen, 0, &state, 0);
            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unhandled LZMA filter type %d!\n", __debug__, self->priv->header.format2.lzma_filter);
            break;
        }
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: inflated: %ld bytes, consumed: %ld bytes\n", __debug__, outlen, inlen);
    return outlen;
}



/**********************************************************************\
 *                            Data accesss                            *
\**********************************************************************/
static gboolean mirage_file_filter_daa_read_main_header (MirageFileFilterDaa *self, GInputStream *stream, DAA_MainHeader *header, GError **error)
{
    guint32 crc;

    /* Seek to the beginning */
    g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);

    /* Read main header */
    if (g_input_stream_read(stream, header, sizeof(DAA_MainHeader), NULL, NULL) != sizeof(DAA_MainHeader)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read main file's header!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read main file's header!");
        return FALSE;
    }

    /* Compute CRC */
    crc = crc32(0, (guint8 *)header, sizeof(DAA_MainHeader) - 4);

    /* Fix endianess */
    daa_main_header_fix_endian(header);

    /* Debug */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Main file header:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   - signature: %.16s\n", __debug__, header->signature);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   - chunk_table_offset: 0x%X (%d)\n", __debug__, header->chunk_table_offset, header->chunk_table_offset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   - format_version: 0x%X\n", __debug__, header->format_version);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   - chunk_data_offset: 0x%X (%d)\n", __debug__, header->chunk_data_offset, header->chunk_data_offset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   - __dummy__1: 0x%X\n", __debug__, header->__dummy__1);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   - __dummy__2: 0x%X\n", __debug__, header->__dummy__2);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   - chunk_size: 0x%X (%d)\n", __debug__, header->chunk_size, header->chunk_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   - iso_size: 0x%llX (%lld)\n", __debug__, header->iso_size, header->iso_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   - daa_size: 0x%llX (%lld)\n", __debug__, header->daa_size, header->daa_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   - format2 header:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:       - profile: %d\n", __debug__, header->format2.profile);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:       - chunk_table_compressed: 0x%lX (%ld)\n", __debug__, header->format2.chunk_table_compressed, header->format2.chunk_table_compressed);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:       - chunk_table_bit_settings: 0x%X (%d)\n", __debug__, header->format2.chunk_table_bit_settings, header->format2.chunk_table_bit_settings);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:       - lzma_filter: %d\n", __debug__, header->format2.lzma_filter);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:       - lzma_props: %02X %02X %02X %02X %02X\n", __debug__, header->format2.lzma_props[0], header->format2.lzma_props[1], header->format2.lzma_props[2], header->format2.lzma_props[3], header->format2.lzma_props[4]);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:       - reserved: %02X %02X %02X %02X\n", __debug__, header->format2.reserved[0], header->format2.reserved[1], header->format2.reserved[2], header->format2.reserved[3]);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   - crc: 0x%X (computed: 0x%X)\n", __debug__, header->crc, crc);

    if (crc != header->crc) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CRC32 checksum mismatch!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "CRC32 checksum mismatch!");
        return FALSE;
    }

    return TRUE;
}

static gboolean mirage_file_filter_daa_read_part_header (MirageFileFilterDaa *self, GInputStream *stream, DAA_PartHeader *header, GError **error)
{
    guint32 crc;

    /* Seek to the beginning */
    g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);

    /* Read main header */
    if (g_input_stream_read(stream, header, sizeof(DAA_PartHeader), NULL, NULL) != sizeof(DAA_PartHeader)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read part file's header!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read part file's header!");
        return FALSE;
    }

    /* Compute CRC */
    crc = crc32(0, (guint8 *)header, sizeof(DAA_PartHeader) - 4);

    /* Fix endianess */
    daa_part_header_fix_endian(header);

    /* Debug */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Part file header:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   - signature: %.16s\n", __debug__, header->signature);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   - chunk_data_offset: 0x%X (%d)\n", __debug__, header->chunk_data_offset, header->chunk_data_offset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   - format2 header:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:       - profile: %d\n", __debug__, header->format2.profile);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:       - chunk_table_compressed: 0x%lX (%ld)\n", __debug__, header->format2.chunk_table_compressed, header->format2.chunk_table_compressed);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:       - chunk_table_bit_settings: 0x%X (%d)\n", __debug__, header->format2.chunk_table_bit_settings, header->format2.chunk_table_bit_settings);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:       - lzma_filter: %d\n", __debug__, header->format2.lzma_filter);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:       - lzma_props: %02X %02X %02X %02X %02X\n", __debug__, header->format2.lzma_props[0], header->format2.lzma_props[1], header->format2.lzma_props[2], header->format2.lzma_props[3], header->format2.lzma_props[4]);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:       - reserved: %02X %02X %02X %02X\n", __debug__, header->format2.reserved[0], header->format2.reserved[1], header->format2.reserved[2], header->format2.reserved[3]);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   - crc: 0x%X (computed: 0x%X)\n", __debug__, header->crc, crc);

    if (crc != header->crc) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CRC32 checksum mismatch!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "CRC32 checksum mismatch!");
        return FALSE;
    }

    return TRUE;
}

static gboolean mirage_file_filter_daa_read_from_stream (MirageFileFilterDaa *self, guint64 offset, guint32 length, guint8 *buffer, GError **error)
{
    /* A rather complex loop, thanks to the possibility that a chunk spans across
       multiple part files... */
    while (length > 0) {
        guint64 local_offset, file_offset;
        guint32 read_length;
        DAA_Part *part = NULL;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: reading 0x%X bytes from stream at offset 0x%llX\n", __debug__, length, offset);

        /* Find the part to which the given offset belongs */
        for (gint i = 0; i < self->priv->num_parts; i++) {
            if (offset >= self->priv->part_table[i].start && offset < self->priv->part_table[i].end) {
                part = &self->priv->part_table[i];
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: using part #%i\n", __debug__, i);
                break;
            }
        }
        if (!part) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to find part for offset 0x%llX!\n", __debug__, offset);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to find part for offset 0x%lX!", offset);
            return FALSE;
        }

        read_length = length;
        if (offset + length > part->end) {
            read_length = part->end - offset;
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: requested data range spanning across the range of this part; clipping read length to 0x%X bytes\n", __debug__, read_length);
        }

        local_offset = offset - part->start; /* Offset within part */
        file_offset = part->offset + local_offset; /* Actual offset within part file */

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: local offset: 0x%llX\n", __debug__, local_offset);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: file offset: 0x%llX\n", __debug__, file_offset);

        if (!g_seekable_seek(G_SEEKABLE(part->stream), file_offset, G_SEEK_SET, NULL, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to 0x%X\n", __debug__, file_offset);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to seek to 0x%lX!", file_offset);
            return FALSE;
        }

        if (g_input_stream_read(part->stream, buffer, read_length, NULL, NULL) != read_length) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read 0x%X bytes!\n", __debug__, read_length);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to read 0x%X bytes!", read_length);
            return FALSE;
        }

        /* Update length and offset */
        length -= read_length;
        offset += read_length;
        buffer += read_length;
    }

    return TRUE;
}

/**********************************************************************\
 *                         Descriptor parsing                         *
\**********************************************************************/
static gboolean mirage_file_filter_daa_parse_descriptor_split (MirageFileFilterDaa *self, gint descriptor_size, GError **error)
{
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));
    DAA_DescriptorSplit descriptor;

    /* First field is number of parts (files) */
    if (g_input_stream_read(stream, &descriptor, sizeof(descriptor), NULL, NULL) != sizeof(descriptor)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read descriptor data!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read descriptor data!");
        return FALSE;
    }
    daa_descriptor_split_fix_endian(&descriptor);
    descriptor_size -= sizeof(descriptor);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of parts: %d\n", __debug__, descriptor.num_parts);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: unknown field: %d (should always be 1)\n", __debug__, descriptor.__dummy__);

    self->priv->num_parts = descriptor.num_parts; /* Set number of parts */

    /* Depending on the filename format, we have a fixed number of 5-byte
       fields, in which part sizes are stored. We don't really need these,
       as we can get same info from part descriptor of each part file.
       However, it can help us determine the filename format. */
    switch (descriptor_size / 5) {
        case 99: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: filename format: volname.part01.daa, volname.part02.daa, ...\n", __debug__);
            self->priv->create_filename_func = create_filename_func_1;
            break;
        }
        case 512: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: filename format: volname.part001.daa, volname.part002.daa, ...\n", __debug__);
            self->priv->create_filename_func = create_filename_func_2;
            break;
        }
        case 101: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: filename format: volname.daa, volname.d00, ...\n", __debug__);
            self->priv->create_filename_func = create_filename_func_3;
            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: invalid filename format type!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Invalid filename format type!");
            return FALSE;
        }
    }
    g_seekable_seek(G_SEEKABLE(stream), descriptor_size, G_SEEK_CUR, NULL, NULL);

    return TRUE;
}

static gboolean mirage_file_filter_daa_parse_descriptor_encryption (MirageFileFilterDaa *self, gint descriptor_size, GError **error)
{
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));
    DAA_DescriptorEncryption descriptor;
    guint8 computed_key[128];

    /* Validate descriptor size */
    if (descriptor_size != sizeof(descriptor)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: invalid size for encryption descriptor (%d vs %d)!\n", __debug__, descriptor_size, sizeof(descriptor));
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Invalid size for encryption descriptor!");
        return FALSE;
    }

    /* Read descriptor data */
    if (g_input_stream_read(stream, &descriptor, sizeof(descriptor), NULL, NULL) != sizeof(descriptor)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read descriptor data!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read descriptor data!");
        return FALSE;
    }
    daa_descriptor_encryption_fix_endian(&descriptor);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: encryption type: 0x%X\n", __debug__, descriptor.encryption_type);
    if (descriptor.encryption_type != 0) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: type of encryption 0x%d might not be supported!\n", __debug__, descriptor.encryption_type);
    }


    /* First, check if password has already been provided via context options
       (separate code paths because if acquired via password function, the string
       must be freed) */
    GVariant *password_value = mirage_contextual_get_option(MIRAGE_CONTEXTUAL(self), "password");
    if (password_value) {
        mirage_file_filter_daa_initialize_decryption(self, computed_key, g_variant_get_string(password_value, NULL), descriptor.daa_key);
        g_variant_unref(password_value);
    } else {
        /* Get password from user via password function */
        gchar *prompt_password = mirage_contextual_obtain_password(MIRAGE_CONTEXTUAL(self), NULL);
        if (!prompt_password) {
            /* Password not provided (or password function is not set) */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  failed to obtain password for encrypted image!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_ENCRYPTED_IMAGE, "Image is encrypted!");
            return FALSE;
        }

        mirage_file_filter_daa_initialize_decryption(self, computed_key, prompt_password, descriptor.daa_key);
        g_free(prompt_password);
    }


    /* Check if password is correct */
    if (descriptor.password_crc != crc32(0, computed_key, 128)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  incorrect password!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Incorrect password!");
        return FALSE;
    }


    /* Set encrypted flag - used later, when reading data */
    self->priv->encrypted = TRUE;

    return TRUE;
}

static gboolean mirage_file_filter_daa_parse_descriptors (MirageFileFilterDaa *self, GError **error)
{
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing descriptors (stream position: 0x%lX)\n", __debug__, g_seekable_tell(G_SEEKABLE(stream)));

    /* Set number of parts to 1 (true for non-split images); if image consists
       of multiple parts, this will be set accordingly by the code below */
    self->priv->num_parts = 1;

    /* Parse descriptors... they are located between header and chunk table */
    while (g_seekable_tell(G_SEEKABLE(stream)) < self->priv->chunk_table_offset) {
        DAA_DescriptorHeader descriptor_header;

        /* Read descriptor header */
        if (g_input_stream_read(stream, &descriptor_header, sizeof(descriptor_header), NULL, NULL) != sizeof(descriptor_header)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read descriptor header!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read descriptor type!");
            return FALSE;
        }

        /* Fix endianess */
        daa_descriptor_header_fix_endian(&descriptor_header);

        /* Length includes type and length fields... */
        descriptor_header.length -= 2*sizeof(guint32);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: descriptor #%i (data length: %d):\n", __debug__, descriptor_header.type, descriptor_header.length);

        switch (descriptor_header.type) {
            case DESCRIPTOR_PART: {
                /* Part information; we skip it here, as it will be parsed by another function */
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: -> part information; skipping\n", __debug__);
                g_seekable_seek(G_SEEKABLE(stream), descriptor_header.length, G_SEEK_CUR, NULL, NULL);
                break;
            }
            case DESCRIPTOR_SPLIT: {
                /* Split archive information */
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: -> split archive information\n", __debug__);
                if (!mirage_file_filter_daa_parse_descriptor_split(self, descriptor_header.length, error)) {
                    return FALSE;
                }
                break;
            }
            case DESCRIPTOR_ENCRYPTION: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: -> encryption information\n", __debug__);
                if (!mirage_file_filter_daa_parse_descriptor_encryption(self, descriptor_header.length, error)) {
                    return FALSE;
                }
                break;
            }
            case DESCRIPTOR_COMMENT: /* FIXME */
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: -> unhandled block type 0x%X; skipping\n", __debug__, descriptor_header.type);
                g_seekable_seek(G_SEEKABLE(stream), descriptor_header.length, G_SEEK_CUR, NULL, NULL);
                break;
            }
        }

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    }

    return TRUE;
}


/**********************************************************************\
 *                         Chunk table parsing                        *
\**********************************************************************/
static void deobfuscate_chunk_table_gbi (guint8 *data, gint size, guint8 crc8)
{
    guint8 d = size / 4;
    for (gint i = 0; i < size; i++) {
        data[i] -= crc8;
        data[i] ^= d;
    }
}

static void deobfuscate_chunk_table_daa (guint8 *data, gint size, guint64 iso_size)
{
    guint8 a, c;

    iso_size /= 2048;
    a = (iso_size >> 8) & 0xff;
    c = iso_size & 0xff;
    for (gint i = 0; i < size; i++) {
        data[i] -= c;
        c += a;
    }
}

static inline guint read_bits (guint bits, guint8 *in, guint in_bits, gboolean bits_obfuscated, gint *bits_obfuscation_counter)
{
    static const guint8 obfuscation_mask[] = { 0x0A, 0x35, 0x2D, 0x3F, 0x08, 0x33, 0x09, 0x15 };
    guint seek_bits;
    guint rem;
    guint seek = 0;
    guint ret = 0;
    guint mask = 0xFFFFFFFF;

    if (bits > 32) {
        return 0;
    }

    if (bits < 32) {
        mask = (1 << bits) - 1;
    }

    for (;;) {
        seek_bits = in_bits & 7;
        ret |= ((in[in_bits >> 3] >> seek_bits)) << seek;
        rem = 8 - seek_bits;
        if (rem >= bits) {
            break;
        }
        bits -= rem;
        in_bits += rem;
        seek += rem;
    }

    if (bits_obfuscated) {
        gint counter = *bits_obfuscation_counter;
        ret ^= ((counter ^ obfuscation_mask[counter & 7]) & 0xff) * 0x01010101;
        *bits_obfuscation_counter = counter++;
    }

    return ret & mask;
}

static gboolean mirage_file_filter_daa_parse_chunk_table (MirageFileFilterDaa *self, GError **error)
{
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));
    guint8 *tmp_chunks_data;
    gint tmp_chunks_len;
    gint num_chunks = 0;
    gint tmp_offset = 0;

    gint bit_pos = 0; /* Bit position */
    gint bit_obfuscation_counter = 0; /* Bit obfuscation counter */

    gint max_chunk_size = 0;

    /* Compute chunk table size */
    tmp_chunks_len = self->priv->chunk_data_offset - self->priv->chunk_table_offset;
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing chunk table (length: %d bytes, from 0x%lX to 0x%lX)...\n", __debug__, tmp_chunks_len, self->priv->chunk_table_offset, self->priv->chunk_data_offset);

    /* Allocate temporary buffer */
    tmp_chunks_data = g_try_new(guint8, tmp_chunks_len);
    if (!tmp_chunks_data) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to allocate chunk table buffer (%d bytes)!\n", __debug__, tmp_chunks_len);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Failed to allocate chunk table buffer (%d bytes)!", tmp_chunks_len);
        return FALSE;
    }

    /* Read chunk data */
    g_seekable_seek(G_SEEKABLE(stream), self->priv->chunk_table_offset, G_SEEK_SET, NULL, NULL);
    if (g_input_stream_read(stream, tmp_chunks_data, tmp_chunks_len, NULL, NULL) != tmp_chunks_len) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read chunk table data!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read chunk table data!");
        return FALSE;
    }

    /* De-obfuscate chunk table data */
    if (self->priv->image_type == IMAGE_GBI) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: applying GBI-specific chunk table deobfuscation...\n", __debug__);
        deobfuscate_chunk_table_gbi(tmp_chunks_data, tmp_chunks_len, self->priv->header.crc & 0xff);
    } else if (self->priv->obfuscated_chunk_table) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: applying DAA-specific chunk table deobfuscation...\n", __debug__);
        deobfuscate_chunk_table_daa(tmp_chunks_data, tmp_chunks_len, self->priv->header.iso_size);
    }

    /* Compute number of chunks */
    switch (self->priv->header.format_version) {
        case FORMAT_VERSION1: {
            /* 3-byte fields */
            num_chunks = tmp_chunks_len / 3;
            break;
        }
        case FORMAT_VERSION2: {
            /* tmp_chunks_len bytes * 8 bits, over bit size of type and length fields */
            num_chunks = (tmp_chunks_len * 8) / (self->priv->bitsize_type + self->priv->bitsize_length);
            break;
        }
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: chunk table: %d entries\n", __debug__, num_chunks);

    /* Allocate chunk table */
    self->priv->num_chunks = num_chunks;
    self->priv->chunk_table = g_try_new(DAA_Chunk, self->priv->num_chunks);
    if (!self->priv->chunk_table) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to allocate chunk table (%ld bytes)!\n", __debug__, self->priv->num_chunks*sizeof(DAA_Chunk));
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Failed to allocate chunk table (%ld bytes)!", self->priv->num_chunks*sizeof(DAA_Chunk));
        return FALSE;
    }

    for (gint i = 0; i < self->priv->num_chunks; i++) {
        DAA_Chunk *chunk = &self->priv->chunk_table[i];

        guint32 tmp_chunk_length = 0;
        gint tmp_compression_type = -1;
        gchar *compression_type_string = "unknown";

        switch (self->priv->header.format_version) {
            case FORMAT_VERSION1: {
                gint off = i*3;
                tmp_chunk_length = (tmp_chunks_data[off+0] << 16) | (tmp_chunks_data[off+2] << 8) | tmp_chunks_data[off+1];
                tmp_compression_type = 1;
                break;
            }
            case FORMAT_VERSION2: {
                /* In version 2, chunk table is bit-packed */
                tmp_chunk_length = read_bits(self->priv->bitsize_length, tmp_chunks_data, bit_pos, self->priv->obfuscated_bits, NULL);
                bit_pos += self->priv->bitsize_length;

                tmp_chunk_length += LZMA_PROPS_SIZE; /* LZMA props size */

                tmp_compression_type = read_bits(self->priv->bitsize_type, tmp_chunks_data, bit_pos, self->priv->obfuscated_bits, &bit_obfuscation_counter);
                bit_pos += self->priv->bitsize_type;

                /* Detect uncompressed chunk */
                if (tmp_chunk_length >= self->priv->chunk_size) {
                    tmp_compression_type = -1;
                }
                break;
            }
        }

        /* Chunk compression type; format 0x100 uses zlib, while format 0x110
           can use either only LZMA or combination of zlib and LZMA */
        switch (tmp_compression_type) {
            case -1: {
                tmp_compression_type = COMPRESSION_NONE;
                compression_type_string = "NONE";
                break;
            }
            case 0: {
                tmp_compression_type = COMPRESSION_LZMA;
                compression_type_string = "LZMA";
                break;
            }
            case 1: {
                tmp_compression_type = COMPRESSION_ZLIB;
                compression_type_string = "ZLIB";
                break;
            }
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown compression type %d!\n", __debug__, tmp_compression_type);
            }
        }

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  entry #%i: offset 0x%llX, length: 0x%X, compression: %s\n", __debug__, i, tmp_offset, tmp_chunk_length, compression_type_string);

        chunk->offset = tmp_offset;
        chunk->length = tmp_chunk_length;
        chunk->compression = tmp_compression_type;

        tmp_offset += tmp_chunk_length;

        max_chunk_size = MAX(max_chunk_size, chunk->length);
    }

    g_free(tmp_chunks_data);

    /* Allocate I/O buffer */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: max chunk size: %d (0x%X)\n", __debug__, max_chunk_size, max_chunk_size);
    self->priv->io_buffer_size = max_chunk_size;
    self->priv->io_buffer = g_try_malloc(self->priv->io_buffer_size);
    if (!self->priv->io_buffer) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: faled to allocate I/O buffer (%d bytes)!\n", __debug__, self->priv->io_buffer_size);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Failed to allocate I/O buffer size (%d bytes)!", self->priv->io_buffer_size);
        return FALSE;
    }

    return TRUE;
}


/**********************************************************************\
 *                       Part table construction                       *
\**********************************************************************/
static gboolean mirage_file_filter_daa_build_part_table (MirageFileFilterDaa *self, GError **error)
{
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));

    gint tmp_offset = 0;
    guint64 part_length, tmp_position;
    DAA_Part *part;
    gchar *part_filename;
    gchar part_signature[16];

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: building parts table (%d entries)...\n\n", __debug__, self->priv->num_parts);

    /* Allocate part table */
    self->priv->part_table = g_try_new0(DAA_Part, self->priv->num_parts);
    if (!self->priv->part_table) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: faled to allocate part table (%d bytes)!\n", __debug__, self->priv->num_parts*sizeof(DAA_Part));
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Failed to allocate part table!");
        return FALSE;
    }

    /* First part is the DAA file we are parsing */
    part = &self->priv->part_table[0];

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating part for main file\n\n", __debug__);

    part->stream = stream;
    g_object_ref(part->stream);

    part->offset = self->priv->chunk_data_offset;
    part->start = tmp_offset;

    tmp_position = g_seekable_tell(G_SEEKABLE(part->stream)); /* Store current position */
    g_seekable_seek(G_SEEKABLE(part->stream), 0, G_SEEK_END, NULL, NULL);
    part_length = g_seekable_tell(G_SEEKABLE(part->stream));
    g_seekable_seek(G_SEEKABLE(part->stream), tmp_position, G_SEEK_SET, NULL, NULL); /* Restore position */

    part_length -= part->offset;

    part->start = tmp_offset;
    tmp_offset += part_length;
    part->end = tmp_offset;


    /* Add the rest of the parts */
    for (gint i = 1; i < self->priv->num_parts; i++) {
        part = &self->priv->part_table[i];

        /* If we have create_filename_func set, use it... otherwise we're a
           non-split image and should be using self->priv->main_filename anyway */
        if (self->priv->create_filename_func) {
            part_filename = self->priv->create_filename_func(self->priv->main_filename, i);
        } else {
            part_filename = g_strdup(self->priv->main_filename);
        }

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: part #%i: %s\n", __debug__, i, part_filename);

        /* Create stream */
        part->stream = mirage_contextual_create_input_stream(MIRAGE_CONTEXTUAL(self), part_filename, error);
        if (!part->stream) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open stream on file '%s'!\n", __debug__, part_filename);
            g_free(part_filename);
            return FALSE;
        }
        g_free(part_filename);

        /* Read signature */
        if (g_input_stream_read(part->stream, part_signature, sizeof(part_signature), NULL, NULL) != sizeof(part_signature)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read part's signature!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DATA_FILE_ERROR, "Failed to read part's signature!");
            return FALSE;
        }

        /* Read header */
        if (!memcmp(part_signature, daa_part_signature, sizeof(daa_part_signature))
            || !memcmp(part_signature, gbi_part_signature, sizeof(gbi_part_signature))) {
            DAA_PartHeader part_header;
            if (!mirage_file_filter_daa_read_part_header(self, part->stream, &part_header, error)) {
                return FALSE;
            }
            part->offset = part_header.chunk_data_offset & 0x00FFFFFF;
        } else {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: invalid part's signature!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Part's signature is invalid!");
            return FALSE;
        }

        /* We could parse part descriptor here; it's present in both main and part
           files, and it has same format. It appears to contain previous
           and current part's index, and the length of current part (with some
           other fields in between). However, those part lengths are literal part
           files' lengths, and we actually need the lengths of zipped streams
           they contain. So we'll calculate that ourselves and leave part descriptor
           alone... Part indices aren't of any use to us either, because I
           haven't seen any DAA image having them mixed up... */
        g_seekable_seek(G_SEEKABLE(part->stream), 0, G_SEEK_END, NULL, NULL);
        part_length = g_seekable_tell(G_SEEKABLE(part->stream));

        part_length -= part->offset;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: part length: 0x%llX\n", __debug__, part_length);

        part->start = tmp_offset;
        tmp_offset += part_length;
        part->end = tmp_offset;
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: part start: 0x%llX\n", __debug__, part->start);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: part end: 0x%llX\n\n", __debug__, part->end);
    }

    return TRUE;
}


/**********************************************************************\
 *                        DAA file parsing                            *
\**********************************************************************/
static gboolean mirage_file_filter_daa_parse_daa_file (MirageFileFilterDaa *self, GError **error)
{
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));

    /* Read main header */
    if (!mirage_file_filter_daa_read_main_header(self, stream, &self->priv->header, error)) {
        return FALSE;
    }

    /* Set stream size */
    mirage_file_filter_set_file_size(MIRAGE_FILE_FILTER(self), self->priv->header.iso_size);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");

    /* Format-dependent header parsing */
    switch (self->priv->header.format_version) {
        case FORMAT_VERSION1: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: format version 1\n\n", __debug__);

            self->priv->chunk_table_offset = self->priv->header.chunk_table_offset;
            self->priv->chunk_data_offset = self->priv->header.chunk_data_offset;
            self->priv->chunk_size = self->priv->header.chunk_size;

            break;
        }
        case FORMAT_VERSION2: {
            guint32 len;
            gint bsize_type = 0;
            gint bsize_len = 0;

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: format version 2\n\n", __debug__);

            self->priv->chunk_table_offset = self->priv->header.chunk_table_offset;
            self->priv->chunk_data_offset = self->priv->header.chunk_data_offset & 0x00FFFFFF;
            self->priv->chunk_size = (self->priv->header.chunk_size & 0x00000FFF) << 14;

            /* Chunk table compression/obfuscation flags */
            self->priv->compressed_chunk_table = self->priv->header.chunk_size & 0x4000;

            self->priv->obfuscated_bits = self->priv->header.chunk_size & 0x20000;
            self->priv->obfuscated_chunk_table = self->priv->header.chunk_size & 0x8000000;

            self->priv->bit_swap_type = (self->priv->header.chunk_size >> 0x17) & 3;;
            if (self->priv->image_type == IMAGE_GBI) {
                self->priv->bit_swap_type ^= 1;
            }

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: actual chunk_data_offset: 0x%X (%d)\n", __debug__, self->priv->chunk_data_offset, self->priv->chunk_data_offset);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: actual chunk_size: 0x%X (%d)\n", __debug__, self->priv->chunk_size, self->priv->chunk_size);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: compressed chunk table: %d\n", __debug__, self->priv->compressed_chunk_table);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: obfuscated chunk table: %d\n", __debug__, self->priv->obfuscated_chunk_table);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: obfuscated bits in chunk table: %d\n", __debug__, self->priv->obfuscated_bits);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: bit swap type: %d\n", __debug__, self->priv->bit_swap_type);

            /* We do not handle compressed chunk table yet, as I'd like
               to have a test image first... */
            if (self->priv->compressed_chunk_table) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: compressed chunk table not supported yet!\n", __debug__);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Compressed chunk table not supported yet!");
                return FALSE;
            }

            /* We do not handle swapped bytes, either */
            if (self->priv->bit_swap_type != 0) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: bit swap type %d not supported yet!\n", __debug__, self->priv->bit_swap_type);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Bit swap type %d not supported yet!", self->priv->bit_swap_type);
                return FALSE;
            }

            /* Decipher bit size for type and length fields of chunk table */
            bsize_type = self->priv->header.format2.chunk_table_bit_settings & 7;
            bsize_len  = self->priv->header.format2.chunk_table_bit_settings >> 3;
            if (bsize_len) {
                bsize_len += 10;
            }
            if (!bsize_len) {
                for (bsize_len = 0, len = self->priv->chunk_size; len > bsize_type; bsize_len++, len >>= 1);
            }
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: chunk table bit-size: length field: %d, compression type field: %d\n", __debug__, bsize_len, bsize_type);
            self->priv->bitsize_length = bsize_len;
            self->priv->bitsize_type = bsize_type;

            /* Initialize LZMA */
            if (!mirage_file_filter_daa_initialize_lzma(self, error)) {
                return FALSE;
            }

            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unsupported format: 0x%X!\n", __debug__, self->priv->header.format_version);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Unsupported format: 0x%X!", self->priv->header.format_version);
            return FALSE;
        }
    }

    /* Initialize zlib stream */
    if (!mirage_file_filter_daa_initialize_zlib(self, error)) {
        return FALSE;
    }

    /* Allocate inflate buffer */
    self->priv->inflate_buffer_size = self->priv->chunk_size;
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: allocating inflate buffer: 0x%X\n", __debug__, self->priv->inflate_buffer_size);
    self->priv->inflate_buffer = g_try_malloc(self->priv->inflate_buffer_size);
    if (!self->priv->inflate_buffer) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to allocate inflate buffer (%d bytes)!\n", __debug__, self->priv->inflate_buffer_size);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Failed to allocate inflate buffer (%d bytes)!", self->priv->inflate_buffer_size);
        return FALSE;
    }

    /* Parse descriptors */
    if (!mirage_file_filter_daa_parse_descriptors(self, error)) {
        return FALSE;
    }


    /* Parse chunk table */
    if (!mirage_file_filter_daa_parse_chunk_table(self, error)) {
        return FALSE;
    }

    /* Build parts table */
    if (!mirage_file_filter_daa_build_part_table(self, error)) {
        return FALSE;
    }

    return TRUE;
}


/**********************************************************************\
 *              MirageFileFilter methods implementations             *
\**********************************************************************/
static gboolean mirage_file_filter_daa_can_handle_data_format (MirageFileFilter *_self, GError **error)
{
    MirageFileFilterDaa *self = MIRAGE_FILE_FILTER_DAA(_self);
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));

    gchar signature[16];

    /* Look for signature at the beginning */
    g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);
    if (g_input_stream_read(stream, signature, sizeof(signature), NULL, NULL) != sizeof(signature)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Filter cannot handle given data: failed to read 16-byte signature!");
        return FALSE;
    }

    /* Check signature */
    if (!memcmp(signature, daa_main_signature, sizeof(daa_main_signature))) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: DAA (PowerISO) format\n", __debug__);
        self->priv->image_type = IMAGE_DAA;
    } else if (!memcmp(signature, gbi_main_signature, sizeof(gbi_main_signature))) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: GBI (gBurner) format\n", __debug__);
        self->priv->image_type = IMAGE_GBI;
    } else {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Filter cannot handle given data: invalid signature!");
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing the underlying stream data...\n", __debug__);

    /* Store filename for later processing */
    self->priv->main_filename = mirage_contextual_get_file_stream_filename(MIRAGE_CONTEXTUAL(self), stream);

    /* Parse DAA file */
    if (!mirage_file_filter_daa_parse_daa_file(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing failed!\n\n", __debug__);
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing completed successfully\n\n", __debug__);

    return TRUE;
}


static gssize mirage_file_filter_daa_partial_read (MirageFileFilter *_self, void *buffer, gsize count)
{
    MirageFileFilterDaa *self = MIRAGE_FILE_FILTER_DAA(_self);
    goffset position = mirage_file_filter_get_position(MIRAGE_FILE_FILTER(self));
    gint chunk_index;

    /* Find chunk that corresponds to current position */
    chunk_index = position / self->priv->chunk_size;
    if (chunk_index >= self->priv->num_chunks) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: stream position %ld (0x%lX) beyond end of stream, doing nothing!\n", __debug__, position, position);
        return 0;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: stream position: %ld (0x%lX) -> chunk #%d (cached: #%d)\n", __debug__, position, chunk_index, self->priv->cached_chunk);

    /* Inflate, if necessary */
    if (chunk_index != self->priv->cached_chunk) {
        DAA_Chunk *chunk = &self->priv->chunk_table[chunk_index];
        gsize expected_inflated_size, inflated_size;

        /* Determine expected inflated size */
        if (chunk_index == self->priv->num_chunks-1) {
            /* Last chunk: remainder */
            expected_inflated_size = self->priv->header.iso_size % self->priv->chunk_size;
        } else {
            expected_inflated_size = self->priv->chunk_size;
        }

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: chunk not cached, reading...\n", __debug__);

        /* Read chunk */
        if (!mirage_file_filter_daa_read_from_stream(self, chunk->offset, chunk->length, self->priv->io_buffer, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read data for chunk #%i\n", __debug__, chunk_index);
            return -1;
        }

        /* Decrypt if encrypted */
        if (self->priv->encrypted) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: decrypting...\n", __debug__);
            mirage_file_filter_daa_decrypt_buffer(self, self->priv->io_buffer, chunk->length);
        }

        /* Inflate */
        memset(self->priv->inflate_buffer, 0, self->priv->inflate_buffer_size); /* Clear the buffer in case we get a failure */
        switch (chunk->compression) {
            case COMPRESSION_NONE: {
                inflated_size = chunk->length - 4;
                memcpy(self->priv->inflate_buffer, self->priv->io_buffer, inflated_size);
                break;
            }
            case COMPRESSION_ZLIB: {
                inflated_size = mirage_file_filter_daa_inflate_zlib(self, self->priv->io_buffer, chunk->length);
                break;
            }
            case COMPRESSION_LZMA: {
                inflated_size = mirage_file_filter_daa_inflate_lzma(self, self->priv->io_buffer, chunk->length);
                break;
            }
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: invalid chunk compression type %d!\n", __debug__, chunk->compression);
                return -1;
            }
        }

        /* Inflated size should match the expected one */
        if (inflated_size != expected_inflated_size && chunk_index != self->priv->num_chunks - 1) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to inflate whole chunk #%i (0x%lX bytes instead of 0x%lX)\n", __debug__, chunk_index, inflated_size, expected_inflated_size);
            return -1;
        } else {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: successfully inflated chunk #%i (0x%X bytes)\n", __debug__, chunk_index, inflated_size);
        }

        /* Set the index of currently inflated chunk */
        self->priv->cached_chunk = chunk_index;
        self->priv->cached_chunk_size = inflated_size;
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: chunk already cached\n", __debug__);
    }


    /* Copy data */
    gint chunk_offset = position % self->priv->chunk_size;
    count = MIN(count, self->priv->cached_chunk_size - chunk_offset);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: offset within chunk: %ld, copying %d bytes\n", __debug__, chunk_offset, count);

    memcpy(buffer, self->priv->inflate_buffer + chunk_offset, count);

    return count;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE(MirageFileFilterDaa, mirage_file_filter_daa, MIRAGE_TYPE_FILE_FILTER);

void mirage_file_filter_daa_type_register (GTypeModule *type_module)
{
    return mirage_file_filter_daa_register_type(type_module);
}


static void mirage_file_filter_daa_init (MirageFileFilterDaa *self)
{
    self->priv = MIRAGE_FILE_FILTER_DAA_GET_PRIVATE(self);

    mirage_file_filter_generate_info(MIRAGE_FILE_FILTER(self),
        "FILTER-DAA",
        "DAA File Filter",
        2,
        "PowerISO images (*.daa)", "application/x-daa",
        "gBurner images (*.gbi)", "application/x-gbi"
    );

    self->priv->chunk_table = NULL;
    self->priv->part_table = NULL;
    self->priv->io_buffer = NULL;
    self->priv->inflate_buffer = NULL;

    self->priv->cached_chunk = -1;
}

static void mirage_file_filter_daa_finalize (GObject *gobject)
{
    MirageFileFilterDaa *self = MIRAGE_FILE_FILTER_DAA(gobject);

    /* Free stream */
    inflateEnd(&self->priv->zlib_stream);
    LzmaDec_Free(&self->priv->lzma_decoder, &lzma_allocator);

    /* Free chunk table */
    g_free(self->priv->chunk_table);

    /* Free part table */
    if (self->priv->part_table) {
        for (gint i = 0; i < self->priv->num_parts; i++) {
            DAA_Part *part = &self->priv->part_table[i];
            if (part->stream) {
                g_object_unref(part->stream);
            }
        }
    }
    g_free(self->priv->part_table);

    /* Free buffer */
    g_free(self->priv->io_buffer);
    g_free(self->priv->inflate_buffer);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_file_filter_daa_parent_class)->finalize(gobject);
}

static void mirage_file_filter_daa_class_init (MirageFileFilterDaaClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageFileFilterClass *file_filter_class = MIRAGE_FILE_FILTER_CLASS(klass);

    gobject_class->finalize = mirage_file_filter_daa_finalize;

    file_filter_class->can_handle_data_format = mirage_file_filter_daa_can_handle_data_format;

    file_filter_class->partial_read = mirage_file_filter_daa_partial_read;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageFileFilterDaaPrivate));
}

static void mirage_file_filter_daa_class_finalize (MirageFileFilterDaaClass *klass G_GNUC_UNUSED)
{
}
