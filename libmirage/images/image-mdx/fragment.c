/*
 *  libMirage: MDX image: custom fragment for handling data compression/encryption
 *  Copyright (C) 2026 Rok Mandeljc
 *
 *  Based on reverse-engineering effort from:
 *  https://github.com/Marisa-Chan/mdsx
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

#include "image-mdx.h"
#include "fragment.h"

#include <zlib.h>

#define __debug__ "MDX-Fragment"


/**********************************************************************\
 *                  Object and its private structure                  *
\**********************************************************************/
enum
{
    /* No compression; data block is stored as-is, and only needs to be read. */
    MDX_COMPRESSION_NONE,
    /* Run-length encoding; whole data block can be represented by a single value.
     * No data is stored (aside from the value, which is encoded in the compression
     * table value). */
    MDX_COMPRESSION_RLE,
    /* Data block is compressed using zlib's deflate. It needs to be read and
     * decompressed using inflate. */
    MDX_COMPRESSION_ZLIB,
};

typedef struct
{
    guint8 compression_type; /* Compression type; see above enum */
    guint8 rle_value; /* The value used with run-length encoding */
    guint16 compressed_size; /* Compressed block size (zlib compression) */
    guint64 data_offset; /* Data offset (zlib or none), relative to data start offset */
} MDX_CompressionTableEntry;

struct _MirageFragmentMdxPrivate
{
    /* Data buffer */
    guint8 *buffer;
    gint buffer_size;

    gint sectors_in_group; /* Number of sectors in a group (i.e., stored in the buffer at the same time) */

    guint cached_sector_group; /* Currently cached sector or sector group */

    /* Cipher handle for data deciphering */
    gcry_cipher_hd_t crypt_handle;
    gpointer gfmul_table;

    /* Compression table */
    MDX_CompressionTableEntry *compression_table;
    guint compression_table_size;

    /* zlib stream for decompression */
    z_stream *zlib_stream;
    guint8 *zlib_buffer; /* Buffer for reading compressed data */

    /* The following elements are stored in private structure of
     * MirageFragment, which is (by design) inaccessible from here.
     * So keep our own copies. */
    MirageStream *data_stream; /* Data stream (main channel and subchannel) */

    guint64 data_offset; /* Data offset in the stream */

    gint main_size;
    gint main_format;

    gint subchannel_size;
    gint subchannel_format;

    gint length;
};

G_DEFINE_TYPE_WITH_PRIVATE(MirageFragmentMdx, mirage_fragment_mdx, MIRAGE_TYPE_FRAGMENT)


/**********************************************************************\
 *                      Custom fragment functionality                 *
\**********************************************************************/
static gboolean mirage_fragment_mdx_read_compression_table (
    MirageFragmentMdx *self,
    const MDX_Footer *footer,
    GError **error
)
{
    gboolean succeeded = FALSE;
    guint8 *compressed_data = NULL; /* Compressed compression table data */
    guint16 *table_values = NULL; /* Uncompressed compression table data */
    guint num_entries; /* Number of entries in compression table */

    /* Determine number of entries in compression table (i.e., number of
     * compression groups): number of sectors in the fragment, divided by
     * number of sectors per compression group, rounded up. */
    num_entries = (self->priv->length + footer->blocks_in_compression_group - 1) / footer->blocks_in_compression_group;
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: estimated entries in compression table: %d\n", __debug__, num_entries);

    /* Read the (compressed) data for compression table. Apparently, the
     * size of compressed compression table is not stored anywhere, so
     * we need to read more data than we can expect to require and let
     * zlib exit early during decompression.
     * See: https://github.com/Marisa-Chan/mdsx/blob/55bb25d/src/mds.c#L390-L391
     *
     * Also, the compression table offset given in the footer seems to
     * be relative to the track data offset, rather than an absolute
     * offset. */
    guint to_read = (num_entries + 0x800) * 2;

    compressed_data = g_malloc0(to_read);
    if (!compressed_data) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to allocate read buffer (%d bytes)!", to_read);
        goto end;
    }

    guint64 compression_table_offset = self->priv->data_offset + footer->compression_table_offset;
    if (!mirage_stream_seek(self->priv->data_stream, compression_table_offset, G_SEEK_SET, NULL)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to seek to compression table offset!");
        goto end;
    }

    gsize read_bytes = mirage_stream_read(self->priv->data_stream, compressed_data, to_read, NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: read %" G_GSIZE_MODIFIER "d bytes of compression table data\n", __debug__, read_bytes);

    /* Allocate the buffer for decompressed data */
    table_values = g_new0(guint16, num_entries);
    if (!table_values) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to allocate buffer for compression table entries!");
        goto end;
    }

    /* Decompress */
    gint zlib_ret;
    gsize total_in;
    gsize total_out;

    z_stream *zlib_stream = self->priv->zlib_stream;

    zlib_stream->avail_in = read_bytes;
    zlib_stream->next_in = compressed_data;

    zlib_stream->avail_out = num_entries * sizeof(guint16);
    zlib_stream->next_out = (guint8 *)table_values;

    inflateReset2(zlib_stream, 15);
    zlib_ret = inflate(zlib_stream, Z_FINISH); /* Single-step inflate() */

    total_in = zlib_stream->total_in;
    total_out = zlib_stream->total_out;

    if (zlib_ret != Z_STREAM_END) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to read/inflate compression table (status=%d)!", zlib_ret);
        goto end;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: inflated %" G_GSIZE_MODIFIER "d bytes from input compressed data into %" G_GSIZE_MODIFIER "d bytes of compression table data\n", __debug__, total_in, total_out);

    /* Sanity check */
    if (total_out != num_entries * sizeof(guint16)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: compression table size mismatch - expected %" G_GSIZE_MODIFIER "d, inflate returned %" G_GSIZE_MODIFIER "d!\n", __debug__, num_entries * sizeof(guint16), total_out);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Compression table size mismatch!");
        goto end;
    }

    /* Process compression table entries; determine the type of compression,
     * and keep track of cumulative offset, which we need to implement random
     * access. */
    self->priv->compression_table = g_new0(MDX_CompressionTableEntry, num_entries);
    if (!self->priv->compression_table) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to allocate buffer for compression table!");
        goto end;
    }
    self->priv->compression_table_size = num_entries;

    gint num_none = 0;
    gint num_rle = 0;
    gint num_zlib = 0;

    guint64 entry_offset = 0;
    for (guint i = 0; i < num_entries; i++) {
        /* Fix endianness */
        guint16 value = GUINT16_FROM_LE(table_values[i]);
        MDX_CompressionTableEntry *entry = &self->priv->compression_table[i];

        if (value == 0) {
            /* No compression */
            entry->compression_type = MDX_COMPRESSION_NONE;
            entry->data_offset = entry_offset;

            entry_offset += footer->blocks_in_compression_group * (self->priv->main_size + self->priv->subchannel_size);

            num_none++;
        } else if (value & 0x8000) {
            /* Run-length encoding; value is stored in the lower byte */
            entry->compression_type = MDX_COMPRESSION_RLE;
            entry->rle_value = value & 0xFF;

            num_rle++;
        } else {
            /* Zlib compression */
            entry->compression_type = MDX_COMPRESSION_ZLIB;
            entry->compressed_size = value;
            entry->data_offset = entry_offset;

            entry_offset += value;

            num_zlib++;
        }
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: read compression table with %d entries; none=%d, rle=%d, zlib=%d\n", __debug__, num_entries, num_none, num_rle, num_zlib);

    succeeded = TRUE;

    /* Cleanup */
end:
    g_free(compressed_data);
    g_free(table_values);

    return succeeded;
}

gboolean mirage_fragment_mdx_setup (
    MirageFragmentMdx *self,
    gint length,
    MirageStream *data_stream,
    guint64 data_offset,
    gint main_size,
    gint main_format,
    gint subchannel_size,
    gint subchannel_format,
    const MDX_Footer *footer,
    const MDX_EncryptionHeader *encryption_header,
    gpointer gfmul_table,
    GError **error
)
{
    /* NOTE: we implicitly assume that this method is called only once;
     * since this fragment is used only within MDX parser, we can ensure
     * that this is the case. */

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: setting up MDX fragment:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s:  - data offset: %" G_GINT64_MODIFIER "d (0x%" G_GINT64_MODIFIER "X)\n", __debug__, data_offset, data_offset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s:  - main channel size: %d\n", __debug__, main_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s:  - main channel format: %d\n", __debug__, main_format);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s:  - subchannel size: %d\n", __debug__, subchannel_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s:  - subchannel format: %d\n", __debug__, subchannel_format);

    /* Propagate the information to parent - this is to ensure that
     * common codepaths in parent class work as expected, as well as
     * to ensure that users that try to access the fragment's properties
     * (for example, image-analyzer), get correct values. */
    mirage_fragment_set_length(MIRAGE_FRAGMENT(self), length);
    mirage_fragment_main_data_set_stream(MIRAGE_FRAGMENT(self), data_stream);

    mirage_fragment_main_data_set_offset(MIRAGE_FRAGMENT(self), data_offset);
    mirage_fragment_main_data_set_size(MIRAGE_FRAGMENT(self), main_size);
    mirage_fragment_main_data_set_format(MIRAGE_FRAGMENT(self), main_format);

    mirage_fragment_subchannel_data_set_size(MIRAGE_FRAGMENT(self), subchannel_size);
    mirage_fragment_subchannel_data_set_format(MIRAGE_FRAGMENT(self), subchannel_format);

    /* Keep reference to data stream */
    self->priv->data_stream = g_object_ref(data_stream);
    self->priv->data_offset = data_offset;

    /* Store information about sector size and fragment length */
    self->priv->main_size = main_size;
    self->priv->main_format = main_format;

    self->priv->subchannel_size = subchannel_size;
    self->priv->subchannel_format = subchannel_format;

    self->priv->length = length;

    /* Number of sectors and total sector size for buffer allocation
     * (will be increased if compression is enabled). */
    gint num_sectors = 1;
    gint full_size = main_size + subchannel_size;

    /* If encryption header is available, initialize the cipher handle. */
    if (encryption_header) {
        gpg_error_t rc;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: data encryption is enabled! Setting up cipher...\n", __debug__);

        rc = gcry_cipher_open(&self->priv->crypt_handle, GCRY_CIPHER_AES256, GCRY_CIPHER_MODE_ECB, 0);
        if (rc != 0) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to initialize AES-256 cipher! Error code: %d (%X)!", rc, rc);
            return FALSE;
        }

        rc = gcry_cipher_setkey(self->priv->crypt_handle, encryption_header->key_data + MDX_IV_SIZE, 32);
        if (rc != 0) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to set cipher key! Error code: %d (%X)!", rc, rc);
            return FALSE;
        }

        /* Store reference to shared instance of pre-computed GF(2^128)
         * multiplication table. */
        self->priv->gfmul_table = g_rc_box_acquire(gfmul_table);
    }

    /* If compression is enabled, read compression table */
    if (footer->flags & 0x01) {
        int zlib_ret;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: data compression is enabled! Settings in footer:\n", __debug__);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s:  - compression table offset: %" G_GINT64_MODIFIER "d (0x%" G_GINT64_MODIFIER "X)\n", __debug__, footer->compression_table_offset, footer->compression_table_offset);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s:  - number of sectors in group: %d (0x%X)\n", __debug__, footer->blocks_in_compression_group, footer->blocks_in_compression_group);

        if (!footer->blocks_in_compression_group) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Invalid number of sectors in compression group (%d)!", footer->blocks_in_compression_group);
            return FALSE;
        }

        /* Allocate and initialize zlib stream; used both to decompress
         * the compression table data and actual sector data. */
        self->priv->zlib_stream = g_new0(z_stream, 1);
        if (!self->priv->zlib_stream) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to allocate zlib z_stream structure!");
            return FALSE;
        }

        self->priv->zlib_stream->zalloc = Z_NULL;
        self->priv->zlib_stream->zfree = Z_NULL;
        self->priv->zlib_stream->opaque = Z_NULL;
        self->priv->zlib_stream->avail_in = 0;
        self->priv->zlib_stream->next_in = Z_NULL;

        zlib_ret = inflateInit2(self->priv->zlib_stream, 15);
        if (zlib_ret != Z_OK) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to initialize zlib z_stream!");
            return FALSE;
        }

        /* Read compression table */
        if (!mirage_fragment_mdx_read_compression_table(self, footer, error)) {
            return FALSE;
        }

        /* Allocate buffer for reading zlib-compressed chunks, based on
         * maximum size of such chunk. */
        guint max_zlib_size = 0;
        for (guint i = 0; i < self->priv->compression_table_size; i++) {
            const MDX_CompressionTableEntry *entry = &self->priv->compression_table[i];
            if (entry->compression_type == MDX_COMPRESSION_ZLIB) {
                max_zlib_size = MAX(max_zlib_size, entry->compressed_size);
            }
        }
        if (max_zlib_size) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: allocating zlib input buffer: %d bytes\n", __debug__, max_zlib_size);
            self->priv->zlib_buffer = g_malloc0(max_zlib_size);
            if (!self->priv->zlib_buffer) {
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to allocate zlib input buffer (%d) bytes!", max_zlib_size);
                return FALSE;
            }
        }

        /* Modify number of sectors that we need to hold in our buffer/cache */
        num_sectors = footer->blocks_in_compression_group;
    }

    /* Allocate buffer/cache */
    self->priv->sectors_in_group = num_sectors;
    self->priv->buffer_size = num_sectors * full_size;
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: allocating data buffer: %d sector(s) with size of %d, total %d bytes\n", __debug__, num_sectors, full_size, self->priv->buffer_size);
    self->priv->buffer = g_malloc0(self->priv->buffer_size);
    if (!self->priv->buffer) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to allocate read buffer (%d bytes)!", self->priv->buffer_size);
        return FALSE;
    }

    return TRUE;
}

static gboolean mirage_fragment_mdx_read_sector_data (MirageFragmentMdx *self, gint address, GError **error)
{
    guint sector_group = address / self->priv->sectors_in_group;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: read sector data request for relative address %d (sector group %d)\n", __debug__, address, sector_group);

    /* Check if data is already in the buffer */
    if (sector_group == self->priv->cached_sector_group) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: data for relative address %d (sector group %d) is already loaded in buffer!\n", __debug__, address, sector_group);
        return TRUE;
    }

    /* Clear the buffer */
    memset(self->priv->buffer, 0, self->priv->buffer_size);

    /* Number of sectors in the group; might need to be adjusted if compression
     * is enabled and this is last sector group. */
    guint num_sectors = self->priv->sectors_in_group;
    guint sector_size = self->priv->main_size + self->priv->subchannel_size;

    /* If compression is enabled, look up compression-table entry */
    const MDX_CompressionTableEntry *compression_entry = NULL;
    if (self->priv->compression_table) {
        if (sector_group >= self->priv->compression_table_size) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: sector group index %d exceeds number of entries in compression table (%d)!\n", __debug__, sector_group, self->priv->compression_table_size);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Sector group index (%d) out of range!", sector_group);
            return FALSE;
        }
        compression_entry = &self->priv->compression_table[sector_group];

        /* If this is last sector group, check if we need to adjust number of
         * sectors (i.e., if the fragment length is not divisible by sector
         * group size). */
        if (sector_group + 1 == self->priv->compression_table_size) {
            gint remaining_sectors = self->priv->length % self->priv->sectors_in_group;
            if (remaining_sectors) {
                num_sectors = remaining_sectors;
            }
        }
    }

    if (!compression_entry || compression_entry->compression_type == MDX_COMPRESSION_NONE || compression_entry->compression_type == MDX_COMPRESSION_ZLIB) {
        /* No compression (with or without compression table) or zlib compression */
        gboolean is_zlib = FALSE;
        guint64 data_offset;
        guint64 to_read;

        if (compression_entry) {
            if (compression_entry->compression_type == MDX_COMPRESSION_ZLIB) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: sector group %d: zlib compression\n", __debug__, sector_group);
                data_offset = self->priv->data_offset + compression_entry->data_offset;
                to_read = compression_entry->compressed_size;
                is_zlib = TRUE;
            } else {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: sector group %d: no compression\n", __debug__, sector_group);
                data_offset = self->priv->data_offset + compression_entry->data_offset;
                to_read = num_sectors * sector_size;
            }
        } else {
            data_offset = self->priv->data_offset + (guint64)address * sector_size;
            to_read = num_sectors * sector_size; /* num_sectors = 1 */
        }

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: reading %" G_GINT64_MODIFIER "d bytes from offset %" G_GINT64_MODIFIER "d (0x%" G_GINT64_MODIFIER "X)\n", __debug__, to_read, data_offset, data_offset);

        mirage_stream_seek(self->priv->data_stream, data_offset, G_SEEK_SET, NULL);
        const gsize read_len = mirage_stream_read(self->priv->data_stream, is_zlib ? self->priv->zlib_buffer : self->priv->buffer, to_read, NULL);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: read %" G_GSIZE_MODIFIER "d bytes\n", __debug__, read_len);

        /* Decrypt */
        if (self->priv->crypt_handle) {
            GError *local_error = NULL;

            /* AES uses 16-byte blocks, so the size of data to be encrypted
             * or decrypted is required to be a multiple of 16 bytes. The
             * LRW mode is typically applied in disk encryption, where
             * (hard-)disk sectors have size of 512 bytes, and are thus
             * aligned with AES block size.
             *
             * On the other hand, as seen in https://github.com/Marisa-Chan/mdsx,
             * the AES+LRW encryption in MDX/MDSv2 format is applied to either
             * individual sectors (when compression is not enabled), to
             * sector groups without compression (MDX_COMPRESSION_NONE),
             * and to zlib-compressed sector groups (MDX_COMPRESSION_ZLIB).
             *
             * This means that neither the size of data chunks nor the size
             * of individual sector (which is used to compute the initial
             * value of the tweak counter) is necessarily a multiple of
             * 16 bytes. The full sector size (2352 bytes) is a multiple,
             * and so is the size of user-data part in Mode1 / Mode 2 Form 1
             * sectors (2048 bytes). The added subchannel (16 or 96 bytes)
             * does not change the alignment. However, MDX/MDSv2 allows the
             * image to contain more than just user-data part of sectors,
             * but less than full sector data; for example, reading a disc
             * with TAGES profile seems to store header + user-data for
             * Mode 1 sectors (2048 + 4 bytes), and header + subheader + user-data
             * for Mode 2 Form 1 sectors (2048 + 4 + 8 bytes). Similarly,
             * zlib compression can reduce the size of data for a sector
             * group (that originally was a multiple of 16 bytes, although
             * this is also not a given!) to a size that is not aligned to
             * 16 bytes anymore.
             *
             * The above discrepancies seem to be solved by simply rounding
             * the input data size down to nearest multiple of 16, and
             * leaving the remaining data un-processed. Same strategy is
             * applied to sector size when computing start value of the
             * tweak counter (which seems to be based on fragment-relative
             * sector addresses). */
            const gsize aligned_data_len = read_len & ~15;
            const guint aligned_sector_size = sector_size & ~15;

            const guint64 start_sector_address = sector_group * self->priv->sectors_in_group;
            const guint64 tweak_counter = 1 + start_sector_address * aligned_sector_size / 16;

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: decrypting data with starting sector number %" G_GINT64_MODIFIER "d, aligned data length is %" G_GSIZE_MODIFIER "d, aligned sector size is %d, start value of tweak counter is %" G_GINT64_MODIFIER "d\n", __debug__, start_sector_address, aligned_data_len, aligned_sector_size, tweak_counter);

            gboolean succeeded = mdx_crypto_decipher_buffer_lrw(
                self->priv->crypt_handle,
                self->priv->gfmul_table,
                is_zlib ? self->priv->zlib_buffer : self->priv->buffer,
                aligned_data_len,
                tweak_counter,
                &local_error
            );

            if (!succeeded) {
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to decrypt sector data: %s", local_error->message);
                g_error_free(local_error);
                return FALSE;
            }
        }

        /* Decompress, if necessary */
        if (is_zlib) {
            gint zlib_ret;
            gsize total_in;
            gsize total_out;

            z_stream *zlib_stream = self->priv->zlib_stream;

            zlib_stream->avail_in = read_len;
            zlib_stream->next_in = self->priv->zlib_buffer;

            zlib_stream->avail_out = self->priv->buffer_size;
            zlib_stream->next_out = self->priv->buffer;

            inflateReset2(zlib_stream, -15);
            zlib_ret = inflate(zlib_stream, Z_FINISH); /* Single-step inflate() */

            total_in = zlib_stream->total_in;
            total_out = zlib_stream->total_out;

            if (zlib_ret != Z_STREAM_END) {
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to read/inflate sector data (status=%d)!", zlib_ret);
                return FALSE;
            }

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: inflated %" G_GSIZE_MODIFIER "d bytes from input compressed data into %" G_GSIZE_MODIFIER "d bytes of sector data\n", __debug__, total_in, total_out);
        }
    } else if (compression_entry->compression_type == MDX_COMPRESSION_RLE) {
        /* Run-length encoding */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: sector group %d; run-length encoding\n", __debug__, sector_group);

        /* Fill buffer with specified value */
        const gsize to_fill = num_sectors * sector_size;
        const guint8 fill_value = compression_entry->rle_value;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: filling %" G_GSIZE_MODIFIER "d bytes with value %hhd (0x%02hhX)\n", __debug__, to_fill, fill_value, fill_value);

        memset(self->priv->buffer, compression_entry->rle_value, num_sectors * sector_size);
    } else {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Unsupported compression/read mode!");
        return FALSE;
    }

    /* Update the cached sector / sector group indicator */
    self->priv->cached_sector_group = sector_group;

    return TRUE;
}


/**********************************************************************\
 *                          MirageFragment methods                    *
\**********************************************************************/
static gboolean mirage_fragment_mdx_read_main_data (MirageFragment *_self, gint address, guint8 **buffer, gint *length, GError **error)
{
    MirageFragmentMdx *self = MIRAGE_FRAGMENT_MDX(_self);

#if 0
    if (!self->priv->crypt_handle && !self->priv->compression_table)  {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: neither compression nor encryption is not used; using parent implementation of read_main_data()...\n", __debug__);
        return MIRAGE_FRAGMENT_CLASS(mirage_fragment_mdx_parent_class)->read_main_data(_self, address, buffer, length, error);
    }
#endif

    /* Clear both variables */
    *length = 0;
    if (buffer) {
        *buffer = NULL;
    }

    /* Ensure sector data is available in cache */
    if (!mirage_fragment_mdx_read_sector_data(MIRAGE_FRAGMENT_MDX(_self), address, error)) {
        return FALSE;
    }

    /* Length */
    *length = self->priv->main_size;

    /* Data */
    if (buffer) {
        guint offset = 0;
        if (self->priv->sectors_in_group > 1) {
            guint sector_index = address % self->priv->sectors_in_group;
            offset = sector_index * (self->priv->main_size + self->priv->subchannel_size);
        }

        guint8 *data_buffer = g_malloc0(self->priv->main_size);
        memcpy(data_buffer, self->priv->buffer + offset, self->priv->main_size);
        *buffer = data_buffer;
    }

    return TRUE;
}

static gboolean mirage_fragment_mdx_read_subchannel_data (MirageFragment *_self, gint address, guint8 **buffer, gint *length, GError **error)
{
    MirageFragmentMdx *self = MIRAGE_FRAGMENT_MDX(_self);

#if 0
    if (!self->priv->crypt_handle && !self->priv->compression_table)  {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: neither compression nor encryption is not used; using parent implementation of read_subchannel_data()...\n", __debug__);
        return MIRAGE_FRAGMENT_CLASS(mirage_fragment_mdx_parent_class)->read_subchannel_data(_self, address, buffer, length, error);
    }
#endif

    /* Clear both variables */
    *length = 0;
    if (buffer) {
        *buffer = NULL;
    }

    /* Ensure sector data is available in cache */
    if (!mirage_fragment_mdx_read_sector_data(MIRAGE_FRAGMENT_MDX(_self), address, error)) {
        return FALSE;
    }

    /* If there's no subchannel, return 0 for the length */
    if (!self->priv->subchannel_size) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: no subchannel (size = 0)!\n", __debug__);
        return TRUE;
    }

    /* Length */
    *length = 96; /* Always 96, because we do the processing here */

    /* Data */
    if (buffer) {
        guint offset = 0;
        if (self->priv->sectors_in_group > 1) {
            guint sector_index = address % self->priv->sectors_in_group;
            offset = sector_index * (self->priv->main_size + self->priv->subchannel_size);
        }
        offset += self->priv->main_size;

        guint8 *data_buffer = g_malloc0(96);

        if (self->priv->subchannel_format & MIRAGE_SUBCHANNEL_DATA_FORMAT_Q16) {
            /* 16-byte Q; interleave it and pretend everything else's 0 */
            mirage_helper_subchannel_interleave(SUBCHANNEL_Q, self->priv->buffer + offset, data_buffer);
        } else if (self->priv->subchannel_format & MIRAGE_SUBCHANNEL_DATA_FORMAT_PW96_INTERLEAVED) {
            /* 96-byte interleaved PW; just copy it */
            memcpy(data_buffer, self->priv->buffer + offset, 96);
        }

        *buffer = data_buffer;
    }

    return TRUE;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
static void mirage_fragment_mdx_init (MirageFragmentMdx *self)
{
    self->priv = mirage_fragment_mdx_get_instance_private(self);

    self->priv->buffer = NULL;
    self->priv->buffer_size = 0;

    self->priv->cached_sector_group = -1; /* unsigned; so this becomes max value */

    self->priv->crypt_handle = NULL;
    self->priv->gfmul_table = NULL;

    self->priv->compression_table = NULL;
    self->priv->compression_table_size = 0;

    self->priv->zlib_stream = NULL;
    self->priv->zlib_buffer = NULL;

    self->priv->data_stream = NULL;
}

static void mirage_fragment_mdx_dispose (GObject *gobject)
{
    MirageFragmentMdx *self = MIRAGE_FRAGMENT_MDX(gobject);

    if (self->priv->data_stream) {
        g_object_unref(self->priv->data_stream);
        self->priv->data_stream = NULL;
    }

    if (self->priv->crypt_handle) {
        gcry_cipher_close(self->priv->crypt_handle);
        self->priv->crypt_handle = NULL;
    }

    if (self->priv->gfmul_table) {
        g_rc_box_release(self->priv->gfmul_table);
        self->priv->gfmul_table = NULL;
    }

    if (self->priv->zlib_stream) {
        inflateEnd(self->priv->zlib_stream);
        g_free(self->priv->zlib_stream);
        self->priv->zlib_stream = NULL;
    }

    /* Chain up to the parent class */
    G_OBJECT_CLASS(mirage_fragment_mdx_parent_class)->dispose(gobject);
}

static void mirage_fragment_mdx_finalize (GObject *gobject)
{
    MirageFragmentMdx *self = MIRAGE_FRAGMENT_MDX(gobject);

    g_free(self->priv->buffer);
    g_free(self->priv->compression_table);
    g_free(self->priv->zlib_buffer);

    /* Chain up to the parent class */
    G_OBJECT_CLASS(mirage_fragment_mdx_parent_class)->finalize(gobject);
}

static void mirage_fragment_mdx_class_init (MirageFragmentMdxClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageFragmentClass *fragment_class = MIRAGE_FRAGMENT_CLASS(klass);

    gobject_class->dispose = mirage_fragment_mdx_dispose;
    gobject_class->finalize = mirage_fragment_mdx_finalize;

    fragment_class->read_main_data = mirage_fragment_mdx_read_main_data;
    fragment_class->read_subchannel_data = mirage_fragment_mdx_read_subchannel_data;
}
