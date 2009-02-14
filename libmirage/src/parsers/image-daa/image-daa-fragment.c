/*
 *  libMirage: DAA image parser: Fragment object
 *  Copyright (C) 2008 Rok Mandeljc
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


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_FRAGMENT_DAA_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FRAGMENT_DAA, MIRAGE_Fragment_DAAPrivate))

enum {
    DAA_COMPRESSION_ZLIB = 0x10,
    DAA_COMPRESSION_LZMA = 0x20,
};

typedef struct {
    guint64 offset;
    guint32 length;
    gint compression;
} DAA_Chunk;

typedef struct {
    FILE *file;
    guint64 offset;
    guint64 start;
    guint64 end;
} DAA_Part;

typedef gchar * (*func_create_filename) (gchar *main_filename, gint index);


typedef struct {
    /* Filename components */
    gchar *main_filename;
    func_create_filename create_filename_func;
        
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
} MIRAGE_Fragment_DAAPrivate;


/* Alloc and free functions for LZMA stream */
static void *sz_alloc (void *p, size_t size) { return g_malloc0(size); }
static void sz_free (void *p, void *address) { g_free(address); }
static ISzAlloc lzma_alloc = { sz_alloc, sz_free };


/******************************************************************************\
 *                         Part filename generation                           *
\******************************************************************************/
/* Format: volname.part01.daa, volname.part02.daa, ... */
static gchar *__create_filename_func_1 (gchar *main_filename, gint index) {
    gchar *ret_filename = g_strdup(main_filename);
    
    if (index) {
        /* Find last occurence of 01. and print index into it */
        gchar *position = g_strrstr(ret_filename, "01.");
        position += g_sprintf(position, "%02i", index+1);
        *position = '.'; /* Since it got overwritten with terminating 0 */
    }    
    
    return ret_filename;
}

/* Format: volname.part001.daa, volname.part002.daa, ... */
static gchar *__create_filename_func_2 (gchar *main_filename, gint index) {
    gchar *ret_filename = g_strdup(main_filename);
    
    if (index) {
        /* Find last occurence of 01. and print index+1 into it */
        gchar *position = g_strrstr(ret_filename, "001.");
        position += g_sprintf(position, "%03i", index+1);
        *position = '.'; /* Since it got overwritten with terminating 0 */
    }    
    
    return ret_filename;
}

/* Format: volname.daa, volname.d00, ... */
static gchar *__create_filename_func_3 (gchar *main_filename, gint index) {
    gchar *ret_filename = g_strdup(main_filename);
    
    if (index) {
        /* Replace last two characters with index-1 */
        gchar *position = ret_filename + strlen(ret_filename) - 2;
        g_sprintf(position, "%02i", index-1);
    }
    
    return ret_filename;
}


/******************************************************************************\
 *                      DAA decryption (Luigi Auriemma)                       *
\******************************************************************************/
static guint8 daa_crypt_table[128][256];

static void __daa_crypt_key (gchar *pass, gint num) {
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
            for(c = 0; c != i;) {
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

static void __daa_crypt_block (guint8 *ret, guint8 *data, gint size) {
    gint i;
    guint8 c, t, *tab;

    if (!size) return;
    tab = daa_crypt_table[size - 1];

    memset(ret, 0, size);
    for (i = 0; i < size; i++) {
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

static void __daa_crypt (guint8 *key, guint8 *data, gint size) {
    gint blocks, rem;
    guint8 tmp[128];
    guint8 *p;

    blocks = size >> 7;
    for (p = data; blocks--; p += 128) {
        __daa_crypt_block(tmp, p, 128);
        memcpy(p, tmp, 128);
    }

    rem = size & 127;
    if (rem) {
        __daa_crypt_block(tmp, p, rem);
        memcpy(p, tmp, rem);
    }
}

static void __daa_crypt_init (guint8 *pwdkey, gchar *pass, guint8 *daakey) {
    int i;

    for(i = 1; i <= 128; i++) {
        __daa_crypt_key(pass, i);
    }
    
    __daa_crypt_block(pwdkey, daakey, 128);
}


/******************************************************************************\
 *                              Data access                                   *
\******************************************************************************/
static gboolean __mirage_fragment_daa_read_from_stream (MIRAGE_Fragment *self, guint64 offset, guint32 length, guint8 *buffer, GError **error) {
    MIRAGE_Fragment_DAAPrivate *_priv = MIRAGE_FRAGMENT_DAA_GET_PRIVATE(self);
    guint8 *buf_ptr = buffer;
    gint i;
    
    /* A rather complex loop, thanks to the possibility that a chunk spans across
       multiple part files... */
    while (length > 0) {
        DAA_Part *part;
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: reading 0x%X bytes from stream at offset 0x%llX\n", __debug__, length, offset);
        
        /* Find the part to which the given offset belongs */
        for (i = 0; i < _priv->num_parts; i++) {
            if (offset >= _priv->part_table[i].start && offset < _priv->part_table[i].end) {
                part = &_priv->part_table[i];
                break;
            }
        }
        if (!part) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to find part for offset 0x%llX!\n", __debug__, offset);
            mirage_error(MIRAGE_E_GENERIC, error);
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
        
        if (fseeko(part->file, file_offset, SEEK_SET) < 0) {
            gchar str_error[256] = "";
            strerror_r(errno, str_error, 256);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to 0x%X: %s\n", __debug__, file_offset, str_error);
            mirage_error(MIRAGE_E_GENERIC, error);
            return FALSE;
        }
        
        if (fread(buf_ptr, read_length, 1, part->file) < 1) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read 0x%X bytes!\n", __debug__, read_length);
            mirage_error(MIRAGE_E_GENERIC, error);
            return FALSE;
        }
        
        /* Update length and offset */
        length -= read_length;
        offset += read_length;
        buf_ptr += read_length;
    }
    
    return TRUE;
}

/******************************************************************************\
 *                     Interface implementation: <private>                    *
\******************************************************************************/
gboolean mirage_fragment_daa_set_file (MIRAGE_Fragment *self, gchar *filename, gchar *password, GError **error) {
    MIRAGE_Fragment_DAAPrivate *_priv = MIRAGE_FRAGMENT_DAA_GET_PRIVATE(self);
    gchar signature[16];
    guint64 tmp_offset = 0;
    FILE *file;
    gint i;
    
    gint bsize_type = 0;
    gint bsize_len = 0;
    gint bpos = 0;
    
    /* Open main file file */
    file = g_fopen(filename, "r");
    if (!file) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open file '%s'!\n", __debug__, filename);
        mirage_error(MIRAGE_E_DATAFILE, error);
        return FALSE;
    }
    
    /* Store filename for later processing */
    _priv->main_filename = g_strdup(filename);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: main filename: %s\n", __debug__, _priv->main_filename);

    /* Read signature */
    if (fread(signature, sizeof(signature), 1, file) < 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read signature!\n", __debug__);
        fclose(file);
        mirage_error(MIRAGE_E_READFAILED, error);
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  signature: %.16s\n", __debug__, signature);
    if (memcmp(signature, daa_main_signature, sizeof(daa_main_signature))) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: invalid signature!\n", __debug__);
        fclose(file);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }
    
    /* Parse header */
    DAA_Main_Header header;

    if (fread(&header, sizeof(DAA_Main_Header), 1, file) < 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read header!\n", __debug__);
        fclose(file);
        mirage_error(MIRAGE_E_READFAILED, error);
        return FALSE;
    }
    
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
        fclose(file);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }
    
    /* Initialize Z stream */
    _priv->z.zalloc = NULL;
    _priv->z.zfree = NULL;
    _priv->z.opaque = NULL;
    if (inflateInit2(&_priv->z, -15)) {
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
        
        LzmaDec_Construct(&_priv->lzma);
        LzmaDec_Allocate(&_priv->lzma, header.hdata + 7, LZMA_PROPS_SIZE, &lzma_alloc);
    }
    
    
    /* Allocate inflate buffer */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: allocating inflate buffer: 0x%X\n", __debug__, header.chunksize);
    _priv->buffer = g_malloc0(header.chunksize);
    _priv->buflen = header.chunksize;
    _priv->cur_chunk_index = -1; /* Because 0 won't do... */
    
    /* Calculate amount of sectors within one chunk - must be an integer... */
    if (header.chunksize % 2048) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: chunk size is not a multiple of 2048!\n", __debug__);
        fclose(file);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    } else {
        _priv->sectors_per_chunk = header.chunksize / 2048;
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: sectors per chunk: %d\n", __debug__, _priv->sectors_per_chunk);
    }
    
    /* Set fragment length */
    gint fragment_length = header.isosize / 2048;
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: fragment length: 0x%X (%d)\n", __debug__, fragment_length, fragment_length);
    mirage_fragment_set_length(self, fragment_length, NULL);

    /* Set number of parts to 1 (true for non-split images); if image consists
       of multiple parts, this will be set accordingly by the code below */
    gint num_parts = 1;
    
    /* Parse blocks */
    while (ftell(file) < header.size_offset) {
        guint32 type = 0;
        guint32 len = 0;
        
        if (fread(&type, sizeof(guint32), 1, file) < 1) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read block type!\n", __debug__);
            fclose(file);
            mirage_error(MIRAGE_E_READFAILED, error);
            return FALSE;
        }
        
        if (fread(&len, sizeof(guint32), 1, file) < 1) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read block length!\n", __debug__);
            fclose(file);
            mirage_error(MIRAGE_E_READFAILED, error);
            return FALSE;
        }
        
        len -= 2*sizeof(guint32); /* Block length includes type and length fields; I like to operate with pure data length */
        
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: block %i (len: %d):\n", __debug__, type, len);
        
        switch (type) {
            case 0x01: {
                /* Block 0x01: Part information */
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  part information; skipping\n", __debug__);
                /* We skip Part information here; it'll be parsed by another function */
                fseek(file, len, SEEK_CUR);
                break;
            }
            case 0x02: {
                /* Block 0x02: Split archive information */
                guint32 b1 = 0;
                                
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  split archive information\n", __debug__);
                
                /* First field is number of parts (files) */
                if (fread(&num_parts, sizeof(guint32), 1, file) < 1) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read number of parts!\n", __debug__);
                    fclose(file);
                    mirage_error(MIRAGE_E_READFAILED, error);
                    return FALSE;
                }
                len -= sizeof(guint32);
                
                /* Next field is always 0x01? */
                if (fread(&b1, sizeof(guint32), 1, file) < 1) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read field 0x01!\n", __debug__);
                    fclose(file);
                    mirage_error(MIRAGE_E_READFAILED, error);
                    return FALSE;
                }
                len -= sizeof(guint32);
                                
                /* Determine filename format and set appropriate function */
                switch (len / 5) {
                    case 99: {
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   filename format: volname.part01.daa, volname.part02.daa, ...\n", __debug__);
                        _priv->create_filename_func = __create_filename_func_1;
                        break;
                    }
                    case 512: {
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   filename format: volname.part001.daa, volname.part002.daa, ...\n", __debug__);
                        _priv->create_filename_func = __create_filename_func_2;
                        break;
                    }
                    case 101: {
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   filename format: volname.daa, volname.d00, ...\n", __debug__);
                        _priv->create_filename_func = __create_filename_func_3;
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
                fseek(file, len, SEEK_CUR);
                break;
            }
            case 0x03: {
                /* Block 0x03: Password block */                
                guint32 pwd_type;
                guint32 pwd_crc;
                
                guint8 daa_key[128];
                                                
                if (fread(&pwd_type, sizeof(guint32), 1, file) < 1) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s:  failed to read password type!\n", __debug__);
                    fclose(file);
                    mirage_error(MIRAGE_E_READFAILED, error);
                    return FALSE;
                }

                if (fread(&pwd_crc, sizeof(guint32), 1, file) < 1) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s:  failed to read password CRC!\n", __debug__);
                    fclose(file);
                    mirage_error(MIRAGE_E_READFAILED, error);
                    return FALSE;
                }
                
                if (fread(daa_key, 128, 1, file) < 1) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s:  failed to read DAA ley!\n", __debug__);
                    fclose(file);
                    mirage_error(MIRAGE_E_READFAILED, error);
                    return FALSE;
                }
                
                if (pwd_type != 0) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s:  type of encryption '%d' might not be supported!\n", __debug__, pwd_type);
                }
                
                /* First, check if password has already been provided via parser parameters
                   (separate code paths because if acquired via password function, the string
                   must be freed) */
                if (password) {
                    __daa_crypt_init(_priv->pwd_key, password, daa_key);
                } else {
                    /* Get password from user via password function */
                    password = libmirage_obtain_password(NULL);
                    if (!password) {
                        /* Password not provided (or password function is not set) */
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  failed to obtain password for encrypted image!\n", __debug__);
                        fclose(file);
                        mirage_error(MIRAGE_E_NEEDPASSWORD, error);
                        return FALSE;
                    }
                    
                    __daa_crypt_init(_priv->pwd_key, password, daa_key);
                    g_free(password);
                }
                
                /* Check if password is correct */
                if (pwd_crc != crc32(0, _priv->pwd_key, 128)) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  incorrect password!\n", __debug__);
                    mirage_error(MIRAGE_E_WRONGPASSWORD, error);
                    return FALSE;
                }
                
                /* Set encrypted flag - used later, when reading data */
                _priv->encrypted = TRUE;
                break;
            }
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s:  unhandled block type 0x%X; skipping\n", __debug__, type);
                fseek(file, len, SEEK_CUR);
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
    
    _priv->num_chunks = num_chunks;
    _priv->chunk_table = g_new0(DAA_Chunk, _priv->num_chunks);
    
    fseek(file, header.size_offset, SEEK_SET);
    if (fread(tmp_chunks_data, tmp_chunks_len, 1, file) < 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read chunks data!\n", __debug__);
        fclose(file);
        mirage_error(MIRAGE_E_READFAILED, error);
        return FALSE;
    }
    
    tmp_offset = 0;
    for (i = 0; i < _priv->num_chunks; i++) {
        DAA_Chunk *chunk = &_priv->chunk_table[i];
        
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
                break;
            }
        }

        /* Chunk compression type; format 0x100 uses zlib, while format 0x110 
           can use either only LZMA or combination of zlib and LZMA */
        switch (tmp_comp) {
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
    fclose(file);
    
    /* Build parts table */
    _priv->num_parts = num_parts;
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: building parts table (%d entries)...\n", __debug__, _priv->num_parts);
    
    _priv->part_table = g_new0(DAA_Part, _priv->num_parts);
    
    tmp_offset = 0;
    for (i = 0; i < _priv->num_parts; i++) {
        DAA_Part *part = &_priv->part_table[i];
        gchar *filename = NULL;
        gchar signature[16] = "";        
        guint64 part_length = 0;

        /* If we have create_filename_func set, use it... otherwise we're a 
           non-split image and should be using _priv->main_filename anyway */
        if (_priv->create_filename_func) {
            filename = _priv->create_filename_func(_priv->main_filename, i);
        } else {
            filename = g_strdup(_priv->main_filename);
        }
        
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  part #%i: %s\n", __debug__, i, filename);
        
        part->file = g_fopen(filename, "r");

        if (!part->file) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open file '%s'!\n", __debug__, filename);
            mirage_error(MIRAGE_E_DATAFILE, error);
            g_free(filename);
            return FALSE;
        }
        g_free(filename);
        
        /* Read signature */
        if (fread(signature, sizeof(signature), 1, part->file) < 1) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read part's signature!\n", __debug__);
            mirage_error(MIRAGE_E_READFAILED, error);
            return FALSE;
        }
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   signature: %.16s\n", __debug__, signature);
        
        if (!memcmp(signature, daa_main_signature, sizeof(daa_main_signature))) {
            DAA_Main_Header header;
            if (fread(&header, sizeof(DAA_Main_Header), 1, part->file) < 1) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read part's header!\n", __debug__);
                mirage_error(MIRAGE_E_READFAILED, error);
                return FALSE;
            }
            part->offset = header.data_offset & 0xFFFFFF;
        } else if (!memcmp(signature, daa_part_signature, sizeof(daa_part_signature))) {
            DAA_Part_Header header;
            if (fread(&header, sizeof(DAA_Part_Header), 1, part->file) < 1)  {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read part's header!\n", __debug__);
                mirage_error(MIRAGE_E_READFAILED, error);
                return FALSE;
            }
            part->offset = header.data_offset & 0xFFFFFF;
        } else {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: invalid part's signature!\n", __debug__);
            mirage_error(MIRAGE_E_PARSER, error);
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
        fseek(part->file, 0, SEEK_END);
        part_length = ftello(part->file);
        
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


/******************************************************************************\
 *                   MIRAGE_Fragment methods implementations                  *
\******************************************************************************/
static gboolean __mirage_fragment_daa_can_handle_data_format (MIRAGE_Fragment *self, gchar *filename, GError **error) {
    /* Not implemented */
    mirage_error(MIRAGE_E_NOTIMPL, error);
    return FALSE;
}

static gboolean __mirage_fragment_daa_use_the_rest_of_file (MIRAGE_Fragment *self, GError **error) {
    /* Not implemented */
    mirage_error(MIRAGE_E_NOTIMPL, error);
    return FALSE;
}


static gint __inflate_zlib (MIRAGE_Fragment *self, guint8 *in_buf, gint in_len) {
    MIRAGE_Fragment_DAAPrivate *_priv = MIRAGE_FRAGMENT_DAA_GET_PRIVATE(self);
    
    inflateReset(&_priv->z);
    
    _priv->z.next_in = in_buf;
    _priv->z.avail_in = in_len;
    _priv->z.next_out = _priv->buffer;
    _priv->z.avail_out = _priv->buflen;
    
    if (inflate(&_priv->z, Z_SYNC_FLUSH) != Z_STREAM_END) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to inflate!\n", __debug__);
        return 0;
    }
    
    return _priv->z.total_out;
}

static gint __inflate_lzma (MIRAGE_Fragment *self, guint8 *in_buf, gint in_len) {
    MIRAGE_Fragment_DAAPrivate *_priv = MIRAGE_FRAGMENT_DAA_GET_PRIVATE(self);
    ELzmaStatus status;
    SizeT inlen, outlen;

    LzmaDec_Init(&_priv->lzma);

    inlen = in_len;
    outlen = _priv->buflen;
    if (LzmaDec_DecodeToBuf(&_priv->lzma, _priv->buffer, &outlen, in_buf, &inlen, LZMA_FINISH_END, &status) != SZ_OK) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to inflate!\n", __debug__);
        return 0;
    }
    
    guint32 state;
    x86_Convert_Init(state);
    x86_Convert(_priv->buffer, _priv->buflen, 0, &state, 0);
    
    return outlen;
}

static gboolean __mirage_fragment_daa_read_main_data (MIRAGE_Fragment *self, gint address, guint8 *buf, gint *length, GError **error) {
    MIRAGE_Fragment_DAAPrivate *_priv = MIRAGE_FRAGMENT_DAA_GET_PRIVATE(self);

    gint chunk_index = address / _priv->sectors_per_chunk;
    gint chunk_offset = address % _priv->sectors_per_chunk;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: address 0x%X; chunk index: %d; offset within chunk: %d\n", __debug__, address, chunk_index, chunk_offset);
    
    /* Inflate, if necessary */
    if (_priv->cur_chunk_index != chunk_index) {
        DAA_Chunk *chunk = &_priv->chunk_table[chunk_index];

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: inflating chunk #%i...\n", __debug__, chunk_index);
        
        /* Read chunk */
        gint tmp_buflen = chunk->length;
        guint8 *tmp_buffer = g_malloc0(tmp_buflen);
        
        if (!__mirage_fragment_daa_read_from_stream(self, chunk->offset, chunk->length, tmp_buffer, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read data for chunk #%i\n", __debug__, chunk_index);
            g_free(tmp_buffer);
            return FALSE;
        }
        
        /* Decrypt if encrypted */
        if (_priv->encrypted) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: decrypting...\n", __debug__);
            __daa_crypt(_priv->pwd_key, tmp_buffer, tmp_buflen);
        }
                
        /* Inflate */
        gint inflated = 0;
        switch (chunk->compression) {
            case DAA_COMPRESSION_ZLIB: {
                inflated = __inflate_zlib(self, tmp_buffer, tmp_buflen);
                break;
            }
            case DAA_COMPRESSION_LZMA: {
                inflated = __inflate_lzma(self, tmp_buffer, tmp_buflen);
                break;
            }
        }
        
        g_free(tmp_buffer);
        
        /* It's OK for the last chunk not to be fully inflated, because it doesn't
           necessarily hold full number of sectors */
        if (inflated != _priv->buflen && chunk_index != _priv->num_chunks-1) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to inflate whole chunk #%i (0x%X bytes instead of 0x%X)\n", __debug__, chunk_index, _priv->z.total_out, _priv->buflen);
        } else {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: successfully inflated chunk #%i (0x%X bytes)\n", __debug__, chunk_index, _priv->z.total_out);
            /* Set the index of currently inflated chunk */
            _priv->cur_chunk_index = chunk_index;
        }
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: chunk #%i already inflated\n", __debug__, chunk_index);
    }
    
    
    /* Copy data */
    if (buf) {
        memcpy(buf, _priv->buffer + (chunk_offset*2048), 2048);
    }
    
    if (length) {
        *length = 2048;
    }
    
    return TRUE;
}

static gboolean __mirage_fragment_daa_read_subchannel_data (MIRAGE_Fragment *self, gint address, guint8 *buf, gint *length, GError **error) {
    /* Nothing to read */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: no subchannel data in DAA fragment\n", __debug__);
    if (length) {
        *length = 0;
    }
    return TRUE;
}


/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static MIRAGE_FragmentClass *parent_class = NULL;

static void __mirage_fragment_daa_instance_init (GTypeInstance *instance, gpointer g_class) {
    /* Create fragment info */
    mirage_fragment_generate_fragment_info(MIRAGE_FRAGMENT(instance),
        "FRAGMENT-DAA",
        "DAA Fragment"
    );
    
    return;
}


static void __mirage_fragment_daa_finalize (GObject *obj) {
    MIRAGE_Fragment_DAA *self = MIRAGE_FRAGMENT_DAA(obj);
    MIRAGE_Fragment_DAAPrivate *_priv = MIRAGE_FRAGMENT_DAA_GET_PRIVATE(self);
    gint i;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: finalizing object\n", __debug__);
    
    /* Free stream */
    inflateEnd(&_priv->z);
    LzmaDec_Free(&_priv->lzma, &lzma_alloc);
    
    /* Free main filename */
    g_free(_priv->main_filename);
    
    /* Free chunk table */
    g_free(_priv->chunk_table);
    
    /* Free part table */
    for (i = 0; i < _priv->num_parts; i++) {
        DAA_Part *part = &_priv->part_table[i];
        if (part->file) fclose(part->file);
    }
    g_free(_priv->part_table);
    
    /* Free buffer */
    g_free(_priv->buffer);
        
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __debug__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __mirage_fragment_daa_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_FragmentClass *class_fragment = MIRAGE_FRAGMENT_CLASS(g_class);
    MIRAGE_Fragment_DAAClass *klass = MIRAGE_FRAGMENT_DAA_CLASS(g_class);

    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Fragment_DAAPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_fragment_daa_finalize;
    
    /* Initialize MIRAGE_Fragment methods */
    class_fragment->can_handle_data_format = __mirage_fragment_daa_can_handle_data_format;
    class_fragment->use_the_rest_of_file = __mirage_fragment_daa_use_the_rest_of_file;
    class_fragment->read_main_data = __mirage_fragment_daa_read_main_data;
    class_fragment->read_subchannel_data = __mirage_fragment_daa_read_subchannel_data;
    
    return;
}

GType mirage_fragment_daa_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Fragment_DAAClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_fragment_daa_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Fragment_DAA),
            0,      /* n_preallocs */
            __mirage_fragment_daa_instance_init    /* instance_init */
        };
        
        type = g_type_module_register_type(module, MIRAGE_TYPE_FRAGMENT, "MIRAGE_Fragment_DAA", &info, 0);
    }
    
    return type;
}
