/*
 *  libMirage: DAA image parser: Fragment object
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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "image-daa.h"

#include <zlib.h>

#include "LzmaDec.h"
#include "Bra.h"

#define __debug__ "DAA-Fragment"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_FRAGMENT_DAA_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FRAGMENT_DAA, MirageFragmentDaaPrivate))

enum
{
    DAA_COMPRESSION_NONE = 0x00,
    DAA_COMPRESSION_ZLIB = 0x10,
    DAA_COMPRESSION_LZMA = 0x20,
};

typedef struct
{
    guint64 offset;
    guint32 length;
    gint compression;
} DAA_Chunk;

typedef struct
{
    GObject *stream;
    guint64 offset;
    guint64 start;
    guint64 end;
} DAA_Part;

typedef gchar * (*DAA_create_filename_func) (gchar *main_filename, gint index);


struct _MirageFragmentDaaPrivate
{
    /* Filename components */
    gchar *main_filename;
    DAA_create_filename_func create_filename_func;

    /* Header */
    DAA_MainHeader header;

    gint chunk_table_offset;
    gint chunk_data_offset;
    gint chunk_size;

    /* Bitsizes of bit-packed chunk table (format version 2) */
    gint bitsize_type;
    gint bitsize_length;

    /* Sectors per chunk */
    gint sectors_per_chunk;

    /* Chunks table */
    gint num_chunks;
    DAA_Chunk *chunk_table;

    /* Parts table */
    gint num_parts;
    DAA_Part *part_table;

    /* Inflation buffer */
    guint8 *buffer; /* Inflation buffer */
    gint buflen; /* Inflation buffer size */
    gint cur_chunk_index; /* Index of currently inflated chunk */

    /* Compression */
    z_stream zlib_stream;

    CLzmaDec lzma_decoder;

    /* Encryption */
    gboolean encrypted;
    const gchar *password;
};


/* Signatures */
const gchar daa_main_signature[16] = "DAA";
const gchar daa_part_signature[16] = "DAA VOL";


/* Alloc and free functions for LZMA decoder */
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
static gchar *create_filename_func_1 (gchar *main_filename, gint index)
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
static gchar *create_filename_func_2 (gchar *main_filename, gint index)
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
static gchar *create_filename_func_3 (gchar *main_filename, gint index)
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
static guint8 daa_crypt_table[128][256];

static void daa_crypt_key (const gchar *pass, gint num)
{
    gint a, b, c, d, s, i, p;
    gint passlen;
    gshort tmp[256];
    guint8 *tab;

    passlen = strlen(pass);
    tab = daa_crypt_table[num - 1];
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

    return;
}

static void daa_crypt_block (guint8 *ret, guint8 *data, gint size)
{
    guint8 c, t, *tab;

    if (!size) return;
    tab = daa_crypt_table[size - 1];

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

    return;
}

static void daa_crypt (guint8 *data, gint size)
{
    gint blocks, rem;
    guint8 tmp[128];
    guint8 *p;

    blocks = size >> 7;
    for (p = data; blocks--; p += 128) {
        daa_crypt_block(tmp, p, 128);
        memcpy(p, tmp, 128);
    }

    rem = size & 127;
    if (rem) {
        daa_crypt_block(tmp, p, rem);
        memcpy(p, tmp, rem);
    }
}

static void daa_crypt_init (guint8 *pwdkey, const gchar *pass, guint8 *daakey)
{
    for (gint i = 1; i <= 128; i++) {
        daa_crypt_key(pass, i);
    }

    daa_crypt_block(pwdkey, daakey, 128);
}


/**********************************************************************\
 *                            Compression                             *
\**********************************************************************/
static gboolean mirage_fragment_daa_initialize_zlib (MirageFragmentDaa *self, GError **error)
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

static gint mirage_fragment_daa_inflate_zlib (MirageFragmentDaa *self, guint8 *in_buf, gsize in_len, gsize uncompressed_size)
{
    z_stream *zlib_stream = &self->priv->zlib_stream;
    gint ret;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: decompressing using zlib; in_len: %ld bytes, uncompressed_size: %ld bytes\n", __debug__, in_len, uncompressed_size);
    inflateReset(zlib_stream);

    zlib_stream->next_in = in_buf;
    zlib_stream->avail_in = in_len;
    zlib_stream->next_out = self->priv->buffer;
    zlib_stream->avail_out = uncompressed_size;

    ret = inflate(zlib_stream, Z_SYNC_FLUSH);
    if (ret != Z_STREAM_END) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to inflate (error code = %d)!\n", __debug__, ret);
        return 0;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: inflated: %ld bytes, consumed: %ld bytes\n", __debug__, zlib_stream->total_out, zlib_stream->total_in);
    return zlib_stream->total_out;
}


static gboolean mirage_fragment_daa_initialize_lzma (MirageFragmentDaa *self, GError **error G_GNUC_UNUSED)
{
    LzmaDec_Construct(&self->priv->lzma_decoder);
    LzmaDec_Allocate(&self->priv->lzma_decoder, self->priv->header.format2.lzma_props, LZMA_PROPS_SIZE, &lzma_allocator);
    return TRUE;
}

static gint mirage_fragment_daa_inflate_lzma (MirageFragmentDaa *self, guint8 *in_buf, gsize in_len, gsize uncompressed_size)
{
    ELzmaStatus status;
    SizeT inlen, outlen;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: decompressing using LZMA; in_len: %ld bytes, uncompressed_size: %ld bytes\n", __debug__, in_len, uncompressed_size);

    /* Initialize decoder */
    LzmaDec_Init(&self->priv->lzma_decoder);

    /* LZMA */
    inlen = in_len;
    outlen = uncompressed_size;
    if (LzmaDec_DecodeToBuf(&self->priv->lzma_decoder, self->priv->buffer, &outlen, in_buf, &inlen, LZMA_FINISH_END, &status) != SZ_OK) {
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
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: applying x86 BCJ filter to decompressed data\n", __debug__);
            x86_Convert_Init(state);
            x86_Convert(self->priv->buffer, outlen, 0, &state, 0);
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unhandled LZMA filter type %d!\n", __debug__, self->priv->header.format2.lzma_filter);

        }
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: inflated: %ld bytes, consumed: %ld bytes\n", __debug__, outlen, inlen);
    return outlen;
}



/**********************************************************************\
 *                            Data accesss                            *
\**********************************************************************/
static gboolean mirage_fragment_daa_read_main_header (MirageFragmentDaa *self, GObject *stream, DAA_MainHeader *header, GError **error)
{
    guint32 crc;

    /* Seek to the beginning */
    g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);

    /* Read main header */
    if (g_input_stream_read(G_INPUT_STREAM(stream), header, sizeof(DAA_MainHeader), NULL, NULL) != sizeof(DAA_MainHeader)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read main file's header!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read main file's header!");
        return FALSE;
    }

    /* Compute CRC */
    crc = crc32(0, (guint8 *)header, sizeof(DAA_MainHeader) - 4);

    /* Fix endianess */
    daa_main_header_fix_endian(header);

    /* Debug */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: DAA main file header:\n", __debug__);
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

static gboolean mirage_fragment_daa_read_part_header (MirageFragmentDaa *self, GObject *stream, DAA_PartHeader *header, GError **error)
{
    guint32 crc;

    /* Seek to the beginning */
    g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);

    /* Read main header */
    if (g_input_stream_read(G_INPUT_STREAM(stream), header, sizeof(DAA_PartHeader), NULL, NULL) != sizeof(DAA_PartHeader)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read part file's header!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read part file's header!");
        return FALSE;
    }

    /* Compute CRC */
    crc = crc32(0, (guint8 *)header, sizeof(DAA_PartHeader) - 4);

    /* Fix endianess */
    daa_part_header_fix_endian(header);

    /* Debug */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: DAA part file header:\n", __debug__);
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

static gboolean mirage_fragment_daa_read_from_stream (MirageFragmentDaa *self, guint64 offset, guint32 length, guint8 *buffer, GError **error)
{
    /* A rather complex loop, thanks to the possibility that a chunk spans across
       multiple part files... */
    while (length > 0) {
        guint64 local_offset, file_offset;
        guint32 read_length;
        DAA_Part *part = NULL;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: reading 0x%X bytes from stream at offset 0x%llX\n", __debug__, length, offset);

        /* Find the part to which the given offset belongs */
        for (gint i = 0; i < self->priv->num_parts; i++) {
            if (offset >= self->priv->part_table[i].start && offset < self->priv->part_table[i].end) {
                part = &self->priv->part_table[i];
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: using part #%i\n", __debug__, i);
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
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: requested data range spanning across the range of this part; clipping read length to 0x%X bytes\n", __debug__, read_length);
        }

        local_offset = offset - part->start; /* Offset within part */
        file_offset = part->offset + local_offset; /* Actual offset within part file */

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: local offset: 0x%llX\n", __debug__, local_offset);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: file offset: 0x%llX\n", __debug__, file_offset);

        if (!g_seekable_seek(G_SEEKABLE(part->stream), file_offset, G_SEEK_SET, NULL, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to 0x%X\n", __debug__, file_offset);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to seek to 0x%lX!", file_offset);
            return FALSE;
        }

        if (g_input_stream_read(G_INPUT_STREAM(part->stream), buffer, read_length, NULL, NULL) != read_length) {
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
static gboolean mirage_fragment_daa_parse_descriptor_split (MirageFragmentDaa *self, GObject *stream, gint descriptor_size, GError **error)
{
    DAA_DescriptorSplit descriptor;

    /* First field is number of parts (files) */
    if (g_input_stream_read(G_INPUT_STREAM(stream), &descriptor, sizeof(descriptor), NULL, NULL) != sizeof(descriptor)) {
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

static gboolean mirage_fragment_daa_parse_descriptor_encryption (MirageFragmentDaa *self, GObject *stream, gint descriptor_size, GError **error)
{
    DAA_DescriptorEncryption descriptor;
    guint8 computed_key[128];

    /* Validate descriptor size */
    if (descriptor_size != sizeof(descriptor)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: invalid size for encryption descriptor (%d vs %d)!\n", __debug__, descriptor_size, sizeof(descriptor));
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Invalid size for encryption descriptor!");
        return FALSE;
    }

    /* Read descriptor data */
    if (g_input_stream_read(G_INPUT_STREAM(stream), &descriptor, sizeof(descriptor), NULL, NULL) != sizeof(descriptor)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read descriptor data!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read descriptor data!");
        return FALSE;
    }
    daa_descriptor_encryption_fix_endian(&descriptor);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: encryption type: 0x%X\n", __debug__, descriptor.encryption_type);
    if (descriptor.encryption_type != 0) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: type of encryption 0x%d might not be supported!\n", __debug__, descriptor.encryption_type);
    }


    /* First, check if password has already been provided via parser parameters
       (separate code paths because if acquired via password function, the string
       must be freed) */
    if (self->priv->password) {
        daa_crypt_init(computed_key, self->priv->password, descriptor.daa_key);
    } else {
        /* Get password from user via password function */
        gchar *prompt_password = mirage_obtain_password(NULL);
        if (!prompt_password) {
            /* Password not provided (or password function is not set) */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  failed to obtain password for encrypted image!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_ENCRYPTED_IMAGE, "Image is encrypted!");
            return FALSE;
        }

        daa_crypt_init(computed_key, prompt_password, descriptor.daa_key);
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

static gboolean mirage_fragment_daa_parse_descriptors (MirageFragmentDaa *self, GObject *stream, GError **error)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing descriptors (stream position: 0x%lX)\n", __debug__, g_seekable_tell(G_SEEKABLE(stream)));

    /* Set number of parts to 1 (true for non-split images); if image consists
       of multiple parts, this will be set accordingly by the code below */
    self->priv->num_parts = 1;

    /* Parse descriptors... they are located between header and chunk table */
    while (g_seekable_tell(G_SEEKABLE(stream)) < self->priv->chunk_table_offset) {
        DAA_DescriptorHeader descriptor_header;

        /* Read descriptor header */
        if (g_input_stream_read(G_INPUT_STREAM(stream), &descriptor_header, sizeof(descriptor_header), NULL, NULL) != sizeof(descriptor_header)) {
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
                if (!mirage_fragment_daa_parse_descriptor_split(self, stream, descriptor_header.length, error)) {
                    return FALSE;
                }
                break;
            }
            case DESCRIPTOR_ENCRYPTION: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: -> encryption information\n", __debug__);
                if (!mirage_fragment_daa_parse_descriptor_encryption(self, stream, descriptor_header.length, error)) {
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
static gboolean mirage_fragment_daa_parse_chunk_table (MirageFragmentDaa *self, GObject *stream, GError **error)
{
    guint8 *tmp_chunks_data;
    gint tmp_chunks_len;
    gint num_chunks = 0;
    gint tmp_offset = 0;

    gint bit_pos = 0; /* Bit position */

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
    if (g_input_stream_read(G_INPUT_STREAM(stream), tmp_chunks_data, tmp_chunks_len, NULL, NULL) != tmp_chunks_len) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read chunk table data!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read chunk table data!");
        return FALSE;
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
                tmp_chunk_length = read_bits(self->priv->bitsize_length, tmp_chunks_data, bit_pos);
                bit_pos += self->priv->bitsize_length;

                tmp_chunk_length += 5; /* LZMA props size */

                tmp_compression_type = read_bits(self->priv->bitsize_type, tmp_chunks_data, bit_pos);
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
                tmp_compression_type = DAA_COMPRESSION_NONE;
                compression_type_string = "NONE";
                break;
            }
            case 0: {
                tmp_compression_type = DAA_COMPRESSION_LZMA;
                compression_type_string = "LZMA";
                break;
            }
            case 1: {
                tmp_compression_type = DAA_COMPRESSION_ZLIB;
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
    }

    g_free(tmp_chunks_data);

    return TRUE;
}


/**********************************************************************\
 *                       Part table construction                       *
\**********************************************************************/
static gboolean mirage_fragment_daa_build_part_table (MirageFragmentDaa *self, GObject *stream, GError **error)
{
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

    g_object_ref(stream);
    part->stream = stream;

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
        part->stream = mirage_create_file_stream(part_filename, G_OBJECT(self), error);
        if (!part->stream) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open stream on file '%s'!\n", __debug__, part_filename);
            g_free(part_filename);
            return FALSE;
        }
        g_free(part_filename);

        /* Read signature */
        if (g_input_stream_read(G_INPUT_STREAM(part->stream), part_signature, sizeof(part_signature), NULL, NULL) != sizeof(part_signature)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read part's signature!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DATA_FILE_ERROR, "Failed to read part's signature!");
            return FALSE;
        }

        /* Read header */
        if (!memcmp(part_signature, daa_part_signature, sizeof(daa_part_signature))) {
            DAA_PartHeader part_header;
            if (!mirage_fragment_daa_read_part_header(self, part->stream, &part_header, error)) {
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
 *                         Buffer allocation                          *
\**********************************************************************/
static gboolean mirage_fragment_daa_setup_buffer (MirageFragmentDaa *self, GError **error)
{
    gint fragment_length;

    /* Calculate amount of sectors within one chunk - must be an integer... */
    self->priv->buflen = self->priv->chunk_size;
    if (self->priv->buflen % 2048) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: chunk size is not a multiple of 2048!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Chunk size is not a multiple of 2048!");
        return FALSE;
    }
    self->priv->sectors_per_chunk = self->priv->buflen / 2048;
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: sectors per chunk: %d\n", __debug__, self->priv->sectors_per_chunk);

    /* Allocate inflate buffer */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: allocating inflate buffer: 0x%X\n", __debug__, self->priv->buflen);
    self->priv->buffer = g_try_malloc(self->priv->buflen);
    if (!self->priv->buffer) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to allocate inflate buffer (%d bytes)!\n", __debug__, self->priv->buflen);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Failed to allocate inflate buffer (%d bytes)!", self->priv->buflen);
        return FALSE;
    }


    self->priv->cur_chunk_index = -1; /* Because 0 won't do... */

    /* Set fragment length */
    fragment_length = self->priv->header.iso_size / 2048;
    mirage_fragment_set_length(MIRAGE_FRAGMENT(self), fragment_length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: fragment length: 0x%X (%d)\n", __debug__, fragment_length, fragment_length);

    return TRUE;
}


/**********************************************************************\
 *                        DAA file parsing                            *
\**********************************************************************/
static gboolean mirage_fragment_daa_parse_daa_file (MirageFragmentDaa *self, GObject *stream, GError **error)
{
    /* Read main header */
    if (!mirage_fragment_daa_read_main_header(self, stream, &self->priv->header, error)) {
        return FALSE;
    }

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

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: actual chunk_data_offset: 0x%X (%d)\n", __debug__, self->priv->chunk_data_offset, self->priv->chunk_data_offset);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: actual chunk_size: 0x%X (%d)\n", __debug__, self->priv->chunk_size, self->priv->chunk_size);

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
            if (!mirage_fragment_daa_initialize_lzma(self, error)) {
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
    if (!mirage_fragment_daa_initialize_zlib(self, error)) {
        return FALSE;
    }

    /* Setup inflate buffer, set fragment length */
    if (!mirage_fragment_daa_setup_buffer(self, error)) {
        return FALSE;
    }


    /* Parse descriptors */
    if (!mirage_fragment_daa_parse_descriptors(self, stream, error)) {
        return FALSE;
    }


    /* Parse chunk table */
    if (!mirage_fragment_daa_parse_chunk_table(self, stream, error)) {
        return FALSE;
    }

    /* Build parts table */
    if (!mirage_fragment_daa_build_part_table(self, stream, error)) {
        return FALSE;
    }

    return TRUE;
}


/**********************************************************************\
 *                  Interface implementation: <private>               *
\**********************************************************************/
gboolean mirage_fragment_daa_set_file (MirageFragmentDaa *self, const gchar *filename, const gchar *password, GError **error)
{
    gchar signature[16];
    GObject *stream;
    gboolean succeeded;

    /* Open main file */
    stream = mirage_create_file_stream(filename, G_OBJECT(self), error);
    if (!stream) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open file '%s'!\n", __debug__, filename);
        return FALSE;
    }

    /* Store filename for later processing */
    self->priv->main_filename = g_strdup(filename);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: main filename: %s\n", __debug__, self->priv->main_filename);

    /* Store password (NOTE: we don't have to copy it */
    self->priv->password = password;

    /* Read signature */
    if (g_input_stream_read(G_INPUT_STREAM(stream), signature, sizeof(signature), NULL, NULL) != sizeof(signature)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read signature!\n", __debug__);
        g_object_unref(stream);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read signature!");
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: signature: %.16s\n", __debug__, signature);
    if (memcmp(signature, daa_main_signature, sizeof(daa_main_signature))) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: invalid signature!\n", __debug__);
        g_object_unref(stream);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Invalid signature!");
        return FALSE;
    }

    /* Parse DAA file */
    succeeded = mirage_fragment_daa_parse_daa_file(self, stream, error);

    g_object_unref(stream);

    return succeeded;
}


/**********************************************************************\
 *                MirageFragment methods implementations             *
\**********************************************************************/
static gboolean mirage_fragment_daa_can_handle_data_format (MirageFragment *_self G_GNUC_UNUSED, GObject *stream G_GNUC_UNUSED, GError **error G_GNUC_UNUSED)
{
    /* Not implemented */
    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Function not implemented!");
    return FALSE;
}

static gboolean mirage_fragment_daa_use_the_rest_of_file (MirageFragment *_self G_GNUC_UNUSED, GError **error G_GNUC_UNUSED)
{
    /* Not implemented */
    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Function not implemented!");
    return FALSE;
}

static gboolean mirage_fragment_daa_read_main_data (MirageFragment *_self, gint address, guint8 *buf, gint *length, GError **error)
{
    MirageFragmentDaa *self = MIRAGE_FRAGMENT_DAA(_self);

    gint chunk_index = address / self->priv->sectors_per_chunk;
    gint chunk_offset = address % self->priv->sectors_per_chunk;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: address 0x%X; chunk index: %d; offset within chunk: %d\n", __debug__, address, chunk_index, chunk_offset);

    /* Inflate, if necessary */
    if (self->priv->cur_chunk_index != chunk_index) {
        DAA_Chunk *chunk = &self->priv->chunk_table[chunk_index];
        gint tmp_buflen;
        guint8 *tmp_buffer;
        gsize expected_inflated_size, inflated_size;

        /* Determine expected inflated size */
        if (chunk_index == self->priv->num_chunks-1) {
            /* Last chunk: remainder */
            expected_inflated_size = self->priv->header.iso_size % self->priv->chunk_size;
        } else {
            expected_inflated_size = self->priv->chunk_size;
        }

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: inflating chunk #%i (expected size: %d bytes)...\n", __debug__, chunk_index, expected_inflated_size);

        /* Allocate read buffer for chunk */
        tmp_buflen = chunk->length;
        tmp_buffer = g_try_malloc0(tmp_buflen);
        if (!tmp_buffer) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to allocate read buffer (%d bytes)\n", __debug__, tmp_buflen);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to allocate read buffer (0x%X bytes)!", tmp_buflen);
            return FALSE;
        }

        /* Read chunk */
        if (!mirage_fragment_daa_read_from_stream(self, chunk->offset, chunk->length, tmp_buffer, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read data for chunk #%i\n", __debug__, chunk_index);
            g_free(tmp_buffer);
            return FALSE;
        }

        /* Decrypt if encrypted */
        if (self->priv->encrypted) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: decrypting...\n", __debug__);
            daa_crypt(tmp_buffer, tmp_buflen);
        }

        /* Inflate */
        memset(self->priv->buffer, 0, self->priv->buflen); /* Clear the buffer in case we get a failure */
        switch (chunk->compression) {
            case DAA_COMPRESSION_NONE: {
                memcpy(self->priv->buffer, tmp_buffer, self->priv->buflen); /* We use self->priv->buflen, because tmp_buflen tends to be too large for 4 bytes... */
                inflated_size = self->priv->buflen;
                break;
            }
            case DAA_COMPRESSION_ZLIB: {
                inflated_size = mirage_fragment_daa_inflate_zlib(self, tmp_buffer, tmp_buflen, expected_inflated_size);
                break;
            }
            case DAA_COMPRESSION_LZMA: {
                inflated_size = mirage_fragment_daa_inflate_lzma(self, tmp_buffer, tmp_buflen, expected_inflated_size);
                break;
            }
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: invalid chunk compression type %d!\n", __debug__, chunk->compression);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Invalid chunk compression type %d!", chunk->compression);
                g_free(tmp_buffer);
                return FALSE;
            }
        }

        g_free(tmp_buffer);

        /* Inflated size should match the expected one */
        if (inflated_size != expected_inflated_size) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to inflate whole chunk #%i (0x%lX bytes instead of 0x%lX)\n", __debug__, chunk_index, inflated_size, expected_inflated_size);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to inflate whole chunk #%i (0x%lX bytes instead of 0x%lX)\n", chunk_index, inflated_size, expected_inflated_size);
            return FALSE;
        } else {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: successfully inflated chunk #%i (0x%X bytes)\n", __debug__, chunk_index, inflated_size);
        }

        /* Set the index of currently inflated chunk */
        self->priv->cur_chunk_index = chunk_index;
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: chunk #%i already inflated\n", __debug__, chunk_index);
    }


    /* Copy data */
    if (buf) {
        memcpy(buf, self->priv->buffer + (chunk_offset*2048), 2048);
    }

    if (length) {
        *length = 2048;
    }

    return TRUE;
}

static gboolean mirage_fragment_daa_read_subchannel_data (MirageFragment *_self, gint address G_GNUC_UNUSED, guint8 *buf G_GNUC_UNUSED, gint *length, GError **error G_GNUC_UNUSED)
{
    MirageFragmentDaa *self = MIRAGE_FRAGMENT_DAA(_self);

    /* Nothing to read */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: no subchannel data in DAA fragment\n", __debug__);
    if (length) {
        *length = 0;
    }

    return TRUE;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE(MirageFragmentDaa, mirage_fragment_daa, MIRAGE_TYPE_FRAGMENT);

void mirage_fragment_daa_type_register (GTypeModule *type_module)
{
    return mirage_fragment_daa_register_type(type_module);
}


static void mirage_fragment_daa_init (MirageFragmentDaa *self)
{
    self->priv = MIRAGE_FRAGMENT_DAA_GET_PRIVATE(self);

    mirage_fragment_generate_fragment_info(MIRAGE_FRAGMENT(self),
        "FRAGMENT-DAA",
        "DAA Fragment"
    );

    self->priv->main_filename = NULL;
    self->priv->chunk_table = NULL;
    self->priv->part_table = NULL;
    self->priv->buffer = NULL;

    self->priv->password = NULL;
}

static void mirage_fragment_daa_finalize (GObject *gobject)
{
    MirageFragmentDaa *self = MIRAGE_FRAGMENT_DAA(gobject);

    /* Free stream */
    inflateEnd(&self->priv->zlib_stream);
    LzmaDec_Free(&self->priv->lzma_decoder, &lzma_allocator);

    /* Free main filename */
    g_free(self->priv->main_filename);

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
    g_free(self->priv->buffer);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_fragment_daa_parent_class)->finalize(gobject);
}

static void mirage_fragment_daa_class_init (MirageFragmentDaaClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageFragmentClass *fragment_class = MIRAGE_FRAGMENT_CLASS(klass);

    gobject_class->finalize = mirage_fragment_daa_finalize;

    fragment_class->can_handle_data_format = mirage_fragment_daa_can_handle_data_format;
    fragment_class->use_the_rest_of_file = mirage_fragment_daa_use_the_rest_of_file;
    fragment_class->read_main_data = mirage_fragment_daa_read_main_data;
    fragment_class->read_subchannel_data = mirage_fragment_daa_read_subchannel_data;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageFragmentDaaPrivate));
}

static void mirage_fragment_daa_class_finalize (MirageFragmentDaaClass *klass G_GNUC_UNUSED)
{
}
