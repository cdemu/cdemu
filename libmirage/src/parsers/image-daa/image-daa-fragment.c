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

#define __debug__ "DAA-Fragment"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_FRAGMENT_DAA_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FRAGMENT_DAA, MIRAGE_Fragment_DAAPrivate))

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


struct _MIRAGE_Fragment_DAAPrivate
{
    /* Filename components */
    gchar *main_filename;
    DAA_create_filename_func create_filename_func;

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

    /* Streams */
    z_stream z;
    CLzmaDec lzma;

    /* Encryption */
    gboolean encrypted;
    guint8 pwd_key[128];
};


/* Alloc and free functions for LZMA stream */
static void *sz_alloc (void *p G_GNUC_UNUSED, size_t size) { return g_malloc0(size); }
static void sz_free (void *p G_GNUC_UNUSED, void *address) { g_free(address); }
static ISzAlloc lzma_alloc = { sz_alloc, sz_free };


/**********************************************************************\
 *                    Endian-conversion functions                     *
\**********************************************************************/
static inline void daa_main_header_fix_endian (DAA_Main_Header *header)
{
    header->size_offset = GUINT32_FROM_LE(header->size_offset);
    header->format = GUINT32_FROM_LE(header->format);
    header->data_offset = GUINT32_FROM_LE(header->data_offset);
    header->b1 = GUINT32_FROM_LE(header->b1);
    header->b0 = GUINT32_FROM_LE(header->b0);
    header->chunksize = GUINT32_FROM_LE(header->chunksize);
    header->isosize = GUINT64_FROM_LE(header->isosize);
    header->filesize = GUINT64_FROM_LE(header->filesize);
    header->crc = GUINT32_FROM_LE(header->crc);
}

static inline void daa_part_header_fix_endian (DAA_Part_Header *header)
{
    header->data_offset = GUINT32_FROM_LE(header->data_offset);
    header->crc = GUINT32_FROM_LE(header->crc);
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
        g_snprintf(position, 4, "%02i.", index+1);
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
        g_snprintf(position, 5, "%03i.", index+1);
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

static void daa_crypt (guint8 *key G_GNUC_UNUSED, guint8 *data, gint size)
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
 *                          Data access                               *
\**********************************************************************/
static gboolean mirage_fragment_daa_read_from_stream (MIRAGE_Fragment_DAA *self, guint64 offset, guint32 length, guint8 *buffer, GError **error)
{
    guint8 *buf_ptr = buffer;

    /* A rather complex loop, thanks to the possibility that a chunk spans across
       multiple part files... */
    while (length > 0) {
        gint i;
        DAA_Part *part = NULL;
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: reading 0x%X bytes from stream at offset 0x%llX\n", __debug__, length, offset);

        /* Find the part to which the given offset belongs */
        for (i = 0; i < self->priv->num_parts; i++) {
            if (offset >= self->priv->part_table[i].start && offset < self->priv->part_table[i].end) {
                part = &self->priv->part_table[i];
                break;
            }
        }
        if (!part) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to find part for offset 0x%llX!\n", __debug__, offset);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to find part for offset 0x%lX!", offset);
            return FALSE;
        }

        guint32 read_length = length;
        if (offset + length > part->end) {
            read_length = part->end - offset;
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: requested data range spanning across the range of this part; clipping read length to 0x%X bytes\n", __debug__, read_length);
        }

        guint64 local_offset = offset - part->start; /* Offset within part */
        guint64 file_offset = part->offset + local_offset; /* Actual offset within part file */

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: local offset: 0x%llX\n", __debug__, local_offset);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: file offset: 0x%llX (part #%i)\n", __debug__, file_offset, i);

        if (!g_seekable_seek(G_SEEKABLE(part->stream), file_offset, G_SEEK_SET, NULL, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to 0x%X\n", __debug__, file_offset);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to seek to 0x%lX!", file_offset);
            return FALSE;
        }

        if (g_input_stream_read(G_INPUT_STREAM(part->stream), buf_ptr, read_length, NULL, NULL) != read_length) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read 0x%X bytes!\n", __debug__, read_length);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to read 0x%X bytes!", read_length);
            return FALSE;
        }

        /* Update length and offset */
        length -= read_length;
        offset += read_length;
        buf_ptr += read_length;
    }

    return TRUE;
}

/**********************************************************************\
 *                  Interface implementation: <private>               *
\**********************************************************************/
gboolean mirage_fragment_daa_set_file (MIRAGE_Fragment_DAA *self, const gchar *filename, const gchar *password, GError **error)
{
    gchar signature[16];
    guint64 tmp_offset = 0;
    GObject *stream;

    gint bsize_type = 0;
    gint bsize_len = 0;
    gint bpos = 0;

    /* Open main file */
    stream = libmirage_create_file_stream(filename, G_OBJECT(self), error);
    if (!stream) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open file '%s'!\n", __debug__, filename);
        return FALSE;
    }

    /* Store filename for later processing */
    self->priv->main_filename = g_strdup(filename);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: main filename: %s\n", __debug__, self->priv->main_filename);

    /* Read signature */
    if (g_input_stream_read(G_INPUT_STREAM(stream), signature, sizeof(signature), NULL, NULL) != sizeof(signature)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read signature!\n", __debug__);
        g_object_unref(stream);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read signature!");
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  signature: %.16s\n", __debug__, signature);
    if (memcmp(signature, daa_main_signature, sizeof(daa_main_signature))) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: invalid signature!\n", __debug__);
        g_object_unref(stream);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Invalid signature!");
        return FALSE;
    }

    /* Parse header */
    DAA_Main_Header header;

    if (g_input_stream_read(G_INPUT_STREAM(stream), &header, sizeof(DAA_Main_Header), NULL, NULL) != sizeof(DAA_Main_Header)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read header!\n", __debug__);
        g_object_unref(stream);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read header!");
        return FALSE;
    }

    daa_main_header_fix_endian(&header);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: DAA main file header:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  size_offset: 0x%X (%d)\n", __debug__, header.size_offset, header.size_offset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  format: 0x%X\n", __debug__, header.format);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  data_offset: 0x%X (%d)\n", __debug__, header.data_offset, header.data_offset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  b1: 0x%X\n", __debug__, header.b1);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  b0: 0x%X\n", __debug__, header.b0);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  chunksize: 0x%X (%d)\n", __debug__, header.chunksize, header.chunksize);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  isosize: 0x%llX (%lld)\n", __debug__, header.isosize, header.isosize);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  filesize: 0x%llX (%lld)\n", __debug__, header.filesize, header.filesize);

    /* Check format */
    if (header.format != DAA_FORMAT_100 && header.format != DAA_FORMAT_110) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unsupported format: 0x%X!\n", __debug__, header.format);
        g_object_unref(stream);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Unsupported format: 0x%X!", header.format);
        return FALSE;
    }

    /* Initialize Z stream */
    self->priv->z.zalloc = NULL;
    self->priv->z.zfree = NULL;
    self->priv->z.opaque = NULL;
    if (inflateInit2(&self->priv->z, -15)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to initialize zlib decoder!\n", __debug__);
    }

    /* 0x110 format provides us with some additional obfuscation */
    if (header.format == DAA_FORMAT_110) {
        guint32 len;

        header.data_offset &= 0xFFFFFF;
        header.chunksize = (header.chunksize & 0xFFF) << 14;

        bsize_type = header.hdata[5] & 7;
        bsize_len = header.hdata[5] >> 3;
        if (bsize_len) bsize_len += 10;
        if (!bsize_len) {
            for (bsize_len = 0, len = header.chunksize; len > bsize_type; bsize_len++, len >>= 1);
        }
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: LZMA compression; length field bitsize=%d, compression type field bitsize=%d\n", __debug__, bsize_len, bsize_type);

        LzmaDec_Construct(&self->priv->lzma);
        LzmaDec_Allocate(&self->priv->lzma, header.hdata + 7, LZMA_PROPS_SIZE, &lzma_alloc);
    }


    /* Allocate inflate buffer */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: allocating inflate buffer: 0x%X\n", __debug__, header.chunksize);
    self->priv->buffer = g_malloc0(header.chunksize);
    self->priv->buflen = header.chunksize;
    self->priv->cur_chunk_index = -1; /* Because 0 won't do... */

    /* Calculate amount of sectors within one chunk - must be an integer... */
    if (header.chunksize % 2048) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: chunk size is not a multiple of 2048!\n", __debug__);
        g_object_unref(stream);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Chunk size is not a multiple of 2048!");
        return FALSE;
    } else {
        self->priv->sectors_per_chunk = header.chunksize / 2048;
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: sectors per chunk: %d\n", __debug__, self->priv->sectors_per_chunk);
    }

    /* Set fragment length */
    gint fragment_length = header.isosize / 2048;
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: fragment length: 0x%X (%d)\n", __debug__, fragment_length, fragment_length);
    mirage_fragment_set_length(MIRAGE_FRAGMENT(self), fragment_length);

    /* Set number of parts to 1 (true for non-split images); if image consists
       of multiple parts, this will be set accordingly by the code below */
    gint num_parts = 1;

    /* Parse blocks */
    while (g_seekable_tell(G_SEEKABLE(stream)) < header.size_offset) {
        guint32 type = 0;
        guint32 len = 0;

        if (g_input_stream_read(G_INPUT_STREAM(stream), &type, sizeof(type), NULL, NULL) != sizeof(type)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read block type!\n", __debug__);
            g_object_unref(stream);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read block type!");
            return FALSE;
        }
        type = GUINT32_FROM_LE(type);

        if (g_input_stream_read(G_INPUT_STREAM(stream), &len, sizeof(len), NULL, NULL) != sizeof(len)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read block length!\n", __debug__);
            g_object_unref(stream);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read block length!");
            return FALSE;
        }
        len = GUINT32_FROM_LE(len);

        len -= 2*sizeof(guint32); /* Block length includes type and length fields; I like to operate with pure data length */

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: block %i (len: %d):\n", __debug__, type, len);

        switch (type) {
            case 0x01: {
                /* Block 0x01: Part information */
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  part information; skipping\n", __debug__);
                /* We skip Part information here; it'll be parsed by another function */
                g_seekable_seek(G_SEEKABLE(stream), len, G_SEEK_CUR, NULL, NULL);
                break;
            }
            case 0x02: {
                /* Block 0x02: Split archive information */
                guint32 b1 = 0;

                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  split archive information\n", __debug__);

                /* First field is number of parts (files) */
                if (g_input_stream_read(G_INPUT_STREAM(stream), &num_parts, sizeof(num_parts), NULL, NULL) != sizeof(num_parts)) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read number of parts!\n", __debug__);
                    g_object_unref(stream);
                    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read number of parts!");
                    return FALSE;
                }
                num_parts = GUINT32_FROM_LE(num_parts);
                len -= sizeof(guint32);

                /* Next field is always 0x01? */
                if (g_input_stream_read(G_INPUT_STREAM(stream), &b1, sizeof(b1), NULL, NULL) != sizeof(b1)) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read field 0x01!\n", __debug__);
                    g_object_unref(stream);
                    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read field 0x01!");
                    return FALSE;
                }
                b1 = GUINT32_FROM_LE(b1);
                len -= sizeof(guint32);

                /* Determine filename format and set appropriate function */
                switch (len / 5) {
                    case 99: {
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   filename format: volname.part01.daa, volname.part02.daa, ...\n", __debug__);
                        self->priv->create_filename_func = create_filename_func_1;
                        break;
                    }
                    case 512: {
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   filename format: volname.part001.daa, volname.part002.daa, ...\n", __debug__);
                        self->priv->create_filename_func = create_filename_func_2;
                        break;
                    }
                    case 101: {
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   filename format: volname.daa, volname.d00, ...\n", __debug__);
                        self->priv->create_filename_func = create_filename_func_3;
                        break;
                    }
                    default: {
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s:   invalid filename format type!\n", __debug__);
                        break;
                    }
                }

                /* Then there's 99/512/101 5-byte fields in which part sizes are
                   stored (Why 5-byte? Probably to save some space, and you can
                   store part lengths up to TB in those... )

                   We don't really need these, since we could get the same info
                   from Block 1 of the the part files themselves... */
                g_seekable_seek(G_SEEKABLE(stream), len, G_SEEK_CUR, NULL, NULL);
                break;
            }
            case 0x03: {
                /* Block 0x03: Password block */
                guint32 pwd_type;
                guint32 pwd_crc;

                guint8 daa_key[128];

                if (g_input_stream_read(G_INPUT_STREAM(stream), &pwd_type, sizeof(pwd_type), NULL, NULL) != sizeof(pwd_type)) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s:  failed to read password type!\n", __debug__);
                    g_object_unref(stream);
                    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read password type!");
                    return FALSE;
                }
                pwd_type = GUINT32_FROM_LE(pwd_type);

                if (g_input_stream_read(G_INPUT_STREAM(stream), &pwd_crc, sizeof(pwd_crc), NULL, NULL) != sizeof(pwd_crc)) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s:  failed to read password CRC!\n", __debug__);
                    g_object_unref(stream);
                    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read password CRC!");
                    return FALSE;
                }
                pwd_crc = GUINT32_FROM_LE(pwd_crc);

                if (g_input_stream_read(G_INPUT_STREAM(stream), daa_key, sizeof(daa_key), NULL, NULL) != sizeof(daa_key)) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s:  failed to read DAA key!\n", __debug__);
                    g_object_unref(stream);
                    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read DAA key!");
                    return FALSE;
                }

                if (pwd_type != 0) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s:  type of encryption '%d' might not be supported!\n", __debug__, pwd_type);
                }

                /* First, check if password has already been provided via parser parameters
                   (separate code paths because if acquired via password function, the string
                   must be freed) */
                if (password) {
                    daa_crypt_init(self->priv->pwd_key, password, daa_key);
                } else {
                    /* Get password from user via password function */
                    gchar *prompt_password = libmirage_obtain_password(NULL);
                    if (!prompt_password) {
                        /* Password not provided (or password function is not set) */
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  failed to obtain password for encrypted image!\n", __debug__);
                        g_object_unref(stream);
                        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_ENCRYPTED_IMAGE, "Image is encrypted!");
                        return FALSE;
                    }

                    daa_crypt_init(self->priv->pwd_key, prompt_password, daa_key);
                    g_free(prompt_password);
                }

                /* Check if password is correct */
                if (pwd_crc != crc32(0, self->priv->pwd_key, 128)) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  incorrect password!\n", __debug__);
                    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Incorrect password!");
                    return FALSE;
                }

                /* Set encrypted flag - used later, when reading data */
                self->priv->encrypted = TRUE;
                break;
            }
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s:  unhandled block type 0x%X; skipping\n", __debug__, type);
                g_seekable_seek(G_SEEKABLE(stream), len, G_SEEK_CUR, NULL, NULL);
                break;
            }
        }

    }

    /* Build the chunk table */
    guint8 *tmp_chunks_data;
    gint tmp_chunks_len;
    gint num_chunks = 0;

    tmp_chunks_len = header.data_offset - header.size_offset;
    tmp_chunks_data = g_new0(guint8, tmp_chunks_len);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: building chunk table (%d entries), %X vs %X...\n", __debug__, tmp_chunks_len, header.data_offset, header.size_offset);

    switch (header.format) {
        case DAA_FORMAT_100: {
            /* 3-byte fields */
            num_chunks = tmp_chunks_len / 3;
            break;
        }
        case DAA_FORMAT_110: {
            /* tmp_chunks_len bytes, each having 8 bits, over bitsize of type and len fields */
            num_chunks = (tmp_chunks_len << 3) / (bsize_type + bsize_len);
            break;
        }
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: building chunk table (%d entries)...\n", __debug__, num_chunks);

    self->priv->num_chunks = num_chunks;
    self->priv->chunk_table = g_new0(DAA_Chunk, self->priv->num_chunks);

    g_seekable_seek(G_SEEKABLE(stream), header.size_offset, G_SEEK_SET, NULL, NULL);
    if (g_input_stream_read(G_INPUT_STREAM(stream), tmp_chunks_data, tmp_chunks_len, NULL, NULL) != tmp_chunks_len) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read chunks data!\n", __debug__);
        g_object_unref(stream);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read chunks data!");
        return FALSE;
    }

    tmp_offset = 0;
    for (gint i = 0; i < self->priv->num_chunks; i++) {
        DAA_Chunk *chunk = &self->priv->chunk_table[i];

        guint32 tmp_length = 0;
        gint tmp_comp = -1;
        gchar *comp_string = "unknown";

        switch (header.format) {
            case DAA_FORMAT_100: {
                gint off = i*3;
                tmp_length = (tmp_chunks_data[off+0] << 16) | (tmp_chunks_data[off+2] << 8) | tmp_chunks_data[off+1];
                tmp_comp = 1;
                break;
            }
            case DAA_FORMAT_110: {
                tmp_length = read_bits(bsize_len, tmp_chunks_data, bpos); bpos += bsize_len;
                tmp_length += 5;
                tmp_comp = read_bits(bsize_type, tmp_chunks_data, bpos); bpos += bsize_type;
                if (tmp_length >= header.chunksize) {
                    tmp_comp = -1;
                }
                break;
            }
        }

        /* Chunk compression type; format 0x100 uses zlib, while format 0x110
           can use either only LZMA or combination of zlib and LZMA */
        switch (tmp_comp) {
            case -1: {
                tmp_comp = DAA_COMPRESSION_NONE;
                comp_string = "NONE";
                break;
            }
            case 0: {
                tmp_comp = DAA_COMPRESSION_LZMA;
                comp_string = "LZMA";
                break;
            }
            case 1: {
                tmp_comp = DAA_COMPRESSION_ZLIB;
                comp_string = "ZLIB";
                break;
            }
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown compression type %d!\n", __debug__, tmp_comp);
            }
        }

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  entry #%i: offset 0x%llX, length: 0x%X, compression: %s\n", __debug__, i, tmp_offset, tmp_length, comp_string);

        chunk->offset = tmp_offset;
        chunk->length = tmp_length;
        chunk->compression = tmp_comp;

        tmp_offset += tmp_length;
    }

    g_free(tmp_chunks_data);

    /* Close the main filename as it's not needed from this point on */
    g_object_unref(stream);

    /* Build parts table */
    self->priv->num_parts = num_parts;
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: building parts table (%d entries)...\n", __debug__, self->priv->num_parts);

    self->priv->part_table = g_new0(DAA_Part, self->priv->num_parts);

    tmp_offset = 0;
    for (gint i = 0; i < self->priv->num_parts; i++) {
        DAA_Part *part = &self->priv->part_table[i];
        gchar *part_filename = NULL;
        gchar part_signature[16] = "";
        guint64 part_length = 0;

        /* If we have create_filename_func set, use it... otherwise we're a
           non-split image and should be using self->priv->main_filename anyway */
        if (self->priv->create_filename_func) {
            part_filename = self->priv->create_filename_func(self->priv->main_filename, i);
        } else {
            part_filename = g_strdup(self->priv->main_filename);
        }

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  part #%i: %s\n", __debug__, i, part_filename);

        part->stream = libmirage_create_file_stream(part_filename, G_OBJECT(self), error);
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
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   signature: %.16s\n", __debug__, part_signature);

        if (!memcmp(part_signature, daa_main_signature, sizeof(daa_main_signature))) {
            DAA_Main_Header part_header;
            if (g_input_stream_read(G_INPUT_STREAM(part->stream), &part_header, sizeof(DAA_Main_Header), NULL, NULL) != sizeof(DAA_Main_Header)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read part's header!\n", __debug__);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DATA_FILE_ERROR, "Failed to read part's header!");
                return FALSE;
            }
            daa_main_header_fix_endian(&part_header);
            part->offset = part_header.data_offset & 0xFFFFFF;
        } else if (!memcmp(part_signature, daa_part_signature, sizeof(daa_part_signature))) {
            DAA_Part_Header part_header;
            if (g_input_stream_read(G_INPUT_STREAM(part->stream), &part_header, sizeof(DAA_Part_Header), NULL, NULL) != sizeof(DAA_Part_Header))  {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read part's header!\n", __debug__);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DATA_FILE_ERROR, "Failed to read part's header!");
                return FALSE;
            }
            daa_part_header_fix_endian(&part_header);
            part->offset = part_header.data_offset & 0xFFFFFF;
        } else {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: invalid part's signature!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Part's signature is invalid!");
            return FALSE;
        }

        /* We could parse Block 0x01 here; it's present in both main and part
           files, and it has same format. The block appears to contain previous
           and current part's index, and the length of current part (with some
           other fields in between). However, those part lengths are literal part
           files' lengths, and we actually need the lengths of zipped streams
           they contain. So we'll calculate that ourselves and leave Block 0x01
           alone... Part indices aren't of any use to us either, because I
           haven't seen any DAA image having them mixed up... */
        g_seekable_seek(G_SEEKABLE(part->stream), 0, G_SEEK_END, NULL, NULL);
        part_length = g_seekable_tell(G_SEEKABLE(part->stream));

        part_length -= part->offset;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  part length: 0x%llX\n", __debug__, part_length);

        part->start = tmp_offset;
        tmp_offset += part_length;
        part->end = tmp_offset;
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  part start: 0x%llX\n", __debug__, part->start);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  part end: 0x%llX\n", __debug__, part->end);
    }

    return TRUE;
}


/**********************************************************************\
 *                MIRAGE_Fragment methods implementations             *
\**********************************************************************/
static gboolean mirage_fragment_daa_can_handle_data_format (MIRAGE_Fragment *_self G_GNUC_UNUSED, GObject *stream G_GNUC_UNUSED, GError **error G_GNUC_UNUSED)
{
    /* Not implemented */
    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Function not implemented!");
    return FALSE;
}

static gboolean mirage_fragment_daa_use_the_rest_of_file (MIRAGE_Fragment *_self G_GNUC_UNUSED, GError **error G_GNUC_UNUSED)
{
    /* Not implemented */
    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Function not implemented!");
    return FALSE;
}



static gint mirage_fragment_daa_inflate_zlib (MIRAGE_Fragment_DAA *self, guint8 *in_buf, gint in_len)
{
    inflateReset(&self->priv->z);

    self->priv->z.next_in = in_buf;
    self->priv->z.avail_in = in_len;
    self->priv->z.next_out = self->priv->buffer;
    self->priv->z.avail_out = self->priv->buflen;

    if (inflate(&self->priv->z, Z_SYNC_FLUSH) != Z_STREAM_END) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to inflate!\n", __debug__);
        return 0;
    }

    return self->priv->z.total_out;
}

static gint mirage_fragment_daa_inflate_lzma (MIRAGE_Fragment_DAA *self, guint8 *in_buf, gint in_len)
{
    ELzmaStatus status;
    SizeT inlen, outlen;

    LzmaDec_Init(&self->priv->lzma);

    inlen = in_len;
    outlen = self->priv->buflen;
    if (LzmaDec_DecodeToBuf(&self->priv->lzma, self->priv->buffer, &outlen, in_buf, &inlen, LZMA_FINISH_END, &status) != SZ_OK) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to inflate!\n", __debug__);
        return 0;
    }

    guint32 state;
    /* FIXME: The following two lines of code is not portable */
    x86_Convert_Init(state);
    x86_Convert(self->priv->buffer, self->priv->buflen, 0, &state, 0);

    return outlen;
}

static gboolean mirage_fragment_daa_read_main_data (MIRAGE_Fragment *_self, gint address, guint8 *buf, gint *length, GError **error)
{
    MIRAGE_Fragment_DAA *self = MIRAGE_FRAGMENT_DAA(_self);

    gint chunk_index = address / self->priv->sectors_per_chunk;
    gint chunk_offset = address % self->priv->sectors_per_chunk;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: address 0x%X; chunk index: %d; offset within chunk: %d\n", __debug__, address, chunk_index, chunk_offset);

    /* Inflate, if necessary */
    if (self->priv->cur_chunk_index != chunk_index) {
        DAA_Chunk *chunk = &self->priv->chunk_table[chunk_index];

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: inflating chunk #%i...\n", __debug__, chunk_index);

        /* Read chunk */
        gint tmp_buflen = chunk->length;
        guint8 *tmp_buffer = g_malloc0(tmp_buflen);

        if (!mirage_fragment_daa_read_from_stream(self, chunk->offset, chunk->length, tmp_buffer, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read data for chunk #%i\n", __debug__, chunk_index);
            g_free(tmp_buffer);
            return FALSE;
        }

        /* Decrypt if encrypted */
        if (self->priv->encrypted) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: decrypting...\n", __debug__);
            daa_crypt(self->priv->pwd_key, tmp_buffer, tmp_buflen);
        }

        /* Inflate */
        gint inflated = 0;
        switch (chunk->compression) {
            case DAA_COMPRESSION_NONE: {
                memcpy(self->priv->buffer, tmp_buffer, self->priv->buflen); /* We use self->priv->buflen, because tmp_buflen tends to be too large for 4 bytes... */
                inflated = self->priv->buflen;
                break;
            }
            case DAA_COMPRESSION_ZLIB: {
                inflated = mirage_fragment_daa_inflate_zlib(self, tmp_buffer, tmp_buflen);
                break;
            }
            case DAA_COMPRESSION_LZMA: {
                inflated = mirage_fragment_daa_inflate_lzma(self, tmp_buffer, tmp_buflen);
                break;
            }
        }

        g_free(tmp_buffer);

        /* It's OK for the last chunk not to be fully inflated, because it doesn't
           necessarily hold full number of sectors */
        if (inflated != self->priv->buflen && chunk_index != self->priv->num_chunks-1) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to inflate whole chunk #%i (0x%X bytes instead of 0x%X)\n", __debug__, chunk_index, self->priv->z.total_out, self->priv->buflen);
        } else {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: successfully inflated chunk #%i (0x%X bytes)\n", __debug__, chunk_index, self->priv->z.total_out);
            /* Set the index of currently inflated chunk */
            self->priv->cur_chunk_index = chunk_index;
        }
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

static gboolean mirage_fragment_daa_read_subchannel_data (MIRAGE_Fragment *_self, gint address G_GNUC_UNUSED, guint8 *buf G_GNUC_UNUSED, gint *length, GError **error G_GNUC_UNUSED)
{
    MIRAGE_Fragment_DAA *self = MIRAGE_FRAGMENT_DAA(_self);

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
G_DEFINE_DYNAMIC_TYPE(MIRAGE_Fragment_DAA, mirage_fragment_daa, MIRAGE_TYPE_FRAGMENT);

void mirage_fragment_daa_type_register (GTypeModule *type_module)
{
    return mirage_fragment_daa_register_type(type_module);
}


static void mirage_fragment_daa_init (MIRAGE_Fragment_DAA *self)
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
}

static void mirage_fragment_daa_finalize (GObject *gobject)
{
    MIRAGE_Fragment_DAA *self = MIRAGE_FRAGMENT_DAA(gobject);

    /* Free stream */
    inflateEnd(&self->priv->z);
    LzmaDec_Free(&self->priv->lzma, &lzma_alloc);

    /* Free main filename */
    g_free(self->priv->main_filename);

    /* Free chunk table */
    g_free(self->priv->chunk_table);

    /* Free part table */
    for (gint i = 0; i < self->priv->num_parts; i++) {
        DAA_Part *part = &self->priv->part_table[i];
        if (part->stream) {
            g_object_unref(part->stream);
        }
    }
    g_free(self->priv->part_table);

    /* Free buffer */
    g_free(self->priv->buffer);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_fragment_daa_parent_class)->finalize(gobject);
}

static void mirage_fragment_daa_class_init (MIRAGE_Fragment_DAAClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MIRAGE_FragmentClass *fragment_class = MIRAGE_FRAGMENT_CLASS(klass);

    gobject_class->finalize = mirage_fragment_daa_finalize;

    fragment_class->can_handle_data_format = mirage_fragment_daa_can_handle_data_format;
    fragment_class->use_the_rest_of_file = mirage_fragment_daa_use_the_rest_of_file;
    fragment_class->read_main_data = mirage_fragment_daa_read_main_data;
    fragment_class->read_subchannel_data = mirage_fragment_daa_read_subchannel_data;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Fragment_DAAPrivate));
}

static void mirage_fragment_daa_class_finalize (MIRAGE_Fragment_DAAClass *klass G_GNUC_UNUSED)
{
}
