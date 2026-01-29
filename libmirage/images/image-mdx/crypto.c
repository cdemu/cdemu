/*
 *  libMirage: MDX image: data decryption functions
 *  Copyright (C) 2025 Rok Mandeljc
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

#include <gcrypt.h>
#include <zlib.h>

#include "gf128mul.h"


/**********************************************************************\
 *                         AES-256 with LRW                           *
\**********************************************************************/
gboolean
mdx_crypto_decipher_buffer_lrw (
    gcry_cipher_hd_t crypt_handle,
    const gpointer gfmul_table,
    guint8 *data,
    gsize len,
    guint64 sector_number,
    GError **error
)
{
    const gint block_size = 16;
    gpg_error_t rc;

    if (len % block_size) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Data length is not a multiple of 16-byte block size!");
        return FALSE;
    }

    /* Decipher each 16-byte block */
    for (gsize i = 0; i < len / block_size; i++) {
        /* Tweak: product of tweak key (F) and tweak index (I) in GF(2^128).
         * Use the table-based multiplication (where table was initialized
         * using the tweak key). */
        guint128_bbe tweak;
        tweak.a = 0;
        tweak.b = GUINT64_TO_BE(sector_number + i);
        gf128mul_64k_bbe(&tweak, (gf128mul_64k_table *)gfmul_table);

        /* XOR with tweak */
        ((guint128_bbe *)data)->a ^= tweak.a;
        ((guint128_bbe *)data)->b ^= tweak.b;

        /* Decipher */
        rc = gcry_cipher_decrypt(crypt_handle, data, block_size, NULL, 0);
        if (rc != 0) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "gcry_cipher_decrypt() failed with error code: %d", rc);
            return FALSE;
        }

        /* XOR with tweak */
        ((guint128_bbe *)data)->a ^= tweak.a;
        ((guint128_bbe *)data)->b ^= tweak.b;

        data += block_size / sizeof(*data);
    }

    return TRUE;
}


/**********************************************************************\
 *                         AES-256 with CBC                           *
\**********************************************************************/
static gboolean
mdx_crypto_decipher_buffer_cbc (
    gcry_cipher_hd_t crypt_handle,
    guint64 *data,
    gsize len,
    const guint64 *iv, /* 16 bytes (two 64-bit entries) */
    GError **error
)
{
    guint64 buf_iv[2];
    guint64 ct[2];
    const gint block_size = 16;
    gpg_error_t rc;

    if (len % block_size) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Data length is not a multiple of 16-byte block size!");
        return FALSE;
    }

    /* IV */
    buf_iv[0] = iv[0];
    buf_iv[1] = iv[1];

    /* Decipher each 16-byte block */
    for (gsize i = 0; i < len / block_size; i++) {
        /* De-whitening; using upper half of IV vector */
        data[0] ^= iv[1];
        data[1] ^= iv[1];

        /* CBC */
        ct[0] = data[0];
        ct[1] = data[1];

        /* Decipher */
        rc = gcry_cipher_decrypt(crypt_handle, data, block_size, NULL, 0);
        if (rc != 0) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "gcry_cipher_decrypt() failed with error code: %d", rc);
            return FALSE;
        }

        /* CBC */
        data[0] ^= buf_iv[0];
        data[1] ^= buf_iv[1];

        buf_iv[0] = ct[0];
        buf_iv[1] = ct[1];

        data += block_size / sizeof(*data);
    }

    return TRUE;
}


/**********************************************************************\
 *                          Header decryption                         *
\**********************************************************************/
static gboolean _mdx_crypto_decipher_encryption_header (
    MDX_EncryptionHeader *header,
    const guint8 *password_buffer,
    const gsize password_length,
    const gboolean main_header,
    GError **error
)
{
    gcry_cipher_hd_t crypt_handle;
    gpg_error_t rc;

    GError *local_error = NULL;
    gboolean succeeded;

    /* Derive the primary header key using PKCS#5 with RIPE-MD-160 as
     * hashing algorithm. Salt data is available at the start of the
     * encryption header (the only non-encrypted part). */
    guint8 master_key[MDX_KEYDATA_SIZE];
    memset(master_key, 0, MDX_KEYDATA_SIZE);

    rc = gcry_kdf_derive(
        password_buffer,
        password_length,
        GCRY_KDF_PBKDF2, /* PKCS#5 PBKDF2 */
        GCRY_MD_RMD160, /* RIPE-MD-160 */
        header->salt,
        sizeof(header->salt), /* PKCS5_SALT_SIZE */
        2000,
        120 + MDX_IV_SIZE, /* EAGetLargestKey() + LEGACY_VOL_IV_SIZE in TrueCrypt */
        master_key
    );
    if (rc != 0) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Failed to compute PKCS#5 PBKDF2 with RIPE-MD-160! Error code: %d (%X)!", rc, rc);
        return FALSE;
    }

    /* Decrypt using AES-256 cipher. For main encryption header, special
     * CBC scheme with de-whitening is used. For data encryption header,
     * LRW mode is used. Therefore, we initialize cipher in ECB mode and
     * implement additional parts ourselves... */
    rc = gcry_cipher_open(
        &crypt_handle,
        GCRY_CIPHER_AES256,
        GCRY_CIPHER_MODE_ECB,
        0
    );
    if (rc != 0) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Failed to initialize AES-256 cipher! Error code: %d (%X)!", rc, rc);
        return FALSE;
    }

    rc = gcry_cipher_setkey(crypt_handle, master_key + MDX_IV_SIZE, 32);
    if (rc != 0) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Failed to set cipher key! Error code: %d (%X)!", rc, rc);
        return FALSE;
    }

    /* Main encryption header uses CBC, the data encryption header uses LRW */
    if (main_header) {
        succeeded = mdx_crypto_decipher_buffer_cbc(
            crypt_handle,
            (guint64 *)(&header->key_data_checksum), /* first field in encrypted-data area */
            sizeof(MDX_EncryptionHeader) - offsetof(MDX_EncryptionHeader, key_data_checksum),
            (const guint64 *)master_key, /* IV (16 bytes) at the start of master key buffer */
            &local_error
        );
    } else {
        /* Initialize table for GF(2^128) multiplication using the master key */
        gpointer gfmul_table = g_new0(gf128mul_64k_table, 1);
        if (!gfmul_table) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Failed to initialize table for GF(2^128) multiplication!");
            return FALSE;
        }
        gf128mul_init_64k_table_bbe((guint128_bbe *)master_key, gfmul_table);

        succeeded = mdx_crypto_decipher_buffer_lrw(
            crypt_handle,
            gfmul_table,
            (guint8 *)(&header->key_data_checksum), /* first field in encrypted-data area */
            sizeof(MDX_EncryptionHeader) - offsetof(MDX_EncryptionHeader, key_data_checksum),
            1, /* Sector number = 1 */
            &local_error
        );

        g_free(gfmul_table);
    }
    if (!succeeded) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Failed to decipher header data buffer: %s", local_error->message);
        g_error_free(local_error);
        return FALSE;
    }

    gcry_cipher_close(crypt_handle);

    /* Fix-up endianness - the data is stored as little-endian */
    header->key_data_checksum = GUINT32_FROM_LE(header->key_data_checksum);
    header->magic = GUINT32_FROM_LE(header->magic);
    header->__unknown1__ = GUINT16_FROM_LE(header->__unknown1__);
    header->key_size = GUINT16_FROM_LE(header->key_size);
    header->__unknown2__ = GUINT16_FROM_LE(header->__unknown2__);
    header->compressed_size = GUINT32_FROM_LE(header->compressed_size);
    header->decompressed_size = GUINT32_FROM_LE(header->decompressed_size);

    /* Check the magic pattern */
    if (header->magic != MDX_MAGIC_PATTERN) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Magic pattern mismatch - expected 0x%X, found 0x%X!", MDX_MAGIC_PATTERN, header->magic);
        return FALSE;
    }

    /* Check data size field */
    if (header->key_size != MDX_KEYDATA_SIZE) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Unexpected key data size - expected 0x%X, found 0x%X!", MDX_KEYDATA_SIZE, header->key_size);
        return FALSE;
    }

    /* Compute CRC */
    guint32 computed_crc = 0;
    gcry_md_hash_buffer(GCRY_MD_CRC32, &computed_crc, header->key_data, sizeof(header->key_data));
    /* The CRC32 computed by libcrypt is, in fact, encoded as big endian:
     * https://github.com/gpg/libgcrypt/blob/libgcrypt-1.11.1/cipher/crc.c#L495
     * Convert it to native endian for comparison with header field (which
     * has also been converted to native endian from its original little-endian
     * representation). */
    computed_crc = GUINT32_FROM_BE(computed_crc);

    if (header->key_data_checksum != computed_crc) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "CRC mismatch - computed 0x%X, stored value is 0x%X!", computed_crc, header->key_data_checksum);
        return FALSE;
    }

    return TRUE;
}

gboolean mdx_crypto_decipher_encryption_header (
    MDX_EncryptionHeader *header,
    const gchar *password,
    const gsize password_length,
    const gboolean main_header,
    GError **error)
{
    /* If password is supplied (track data encryption header), use it. */
    if (password) {
        return _mdx_crypto_decipher_encryption_header(header, (const guint8 *)password, password_length, main_header, error);
    }

    /* The password for descriptor's encryption header is derived from
     * the salt data; see the unshuffle1() function in implementation
     * from https://github.com/Marisa-Chan/mdsx.
     *
     * The same scheme seems to be used with certain profiles (e.g., TAGES)
     * for "password-less" encryption of track data. */
    guint32 password_buffer[MDX_PKCS5_SALT_SIZE / 4]; /* We will process buffer in 32-bit chunks! */
    memcpy(password_buffer, header->salt, MDX_PKCS5_SALT_SIZE);

    /* Compute CRC32 using the same EDC LUT that is used for sector data. */
    guint32 modifier = mirage_helper_calculate_crc32_fast((guint8 *)password_buffer, MDX_PKCS5_SALT_SIZE, crc32_d8018001_lut, TRUE, FALSE);
    modifier ^= 0x567372ff;

    for (gint i = 0; i < MDX_PKCS5_SALT_SIZE / 4; i++) {
        guint32 buffer_value = password_buffer[i];

        modifier = (modifier * 0x35e85a6d) + 0x1548dce9;
        buffer_value = buffer_value ^ modifier ^ 0xec564717;

        /* Replace all 0x00 octets with 0x5f */
        buffer_value |= (!(buffer_value & 0x000000ff)) * 0x0000005f;
        buffer_value |= (!(buffer_value & 0x0000ff00)) * 0x00005f00;
        buffer_value |= (!(buffer_value & 0x00ff0000)) * 0x005f0000;
        buffer_value |= (!(buffer_value & 0xff000000)) * 0x5f000000;

        password_buffer[i] = buffer_value;
    }

    return _mdx_crypto_decipher_encryption_header(header, (guint8 *)password_buffer, MDX_PKCS5_SALT_SIZE, main_header, error);
}


/**********************************************************************\
 *              Descriptor decryption and decompression               *
\**********************************************************************/
guint8 *mdx_crypto_decipher_and_decompress_descriptor(
    guint8 *data,
    const gsize length,
    const MDX_EncryptionHeader *header,
    GError **error
)
{
    gcry_cipher_hd_t crypt_handle;
    gpg_error_t rc;

    GError *local_error = NULL;

    /* Decrypt using AES-256 with CBC */
    rc = gcry_cipher_open(&crypt_handle, GCRY_CIPHER_AES256, GCRY_CIPHER_MODE_ECB, 0);
    if (rc != 0) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Failed to initialize AES-256 cipher! Error code: %d (%X)!", rc, rc);
        return NULL;
    }

    rc = gcry_cipher_setkey(crypt_handle, header->key_data + MDX_IV_SIZE, 32);
    if (rc != 0) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Failed to set cipher key! Error code: %d (%X)!", rc, rc);
        return NULL;
    }

    /* The (padded) descriptor data length might exceed 512 bytes, which
     * is the block size used by TrueCrypt. If this is the case, we need
     * to split data in 512-byte blocks, because de-whitening needs to
     * be applied to each such 515-byte data block! */
    gssize remaining_length = length;
    guint8 *data_ptr = data;
    while (remaining_length > 0) {
        gssize block_size = remaining_length > 512 ? 512 : remaining_length;
        gboolean succeeded = mdx_crypto_decipher_buffer_cbc(
            crypt_handle,
            (guint64 *)data_ptr,
            block_size,
            (guint64 *)header->key_data, /* IV (16 bytes) at the start of master key buffer */
            &local_error
        );
        if (!succeeded) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Failed to decipher (part of) descriptor: %s", local_error->message);
            g_error_free(local_error);
            return NULL;
        }
        remaining_length -= block_size;
        data_ptr += block_size;
    }

    gcry_cipher_close(crypt_handle);

    /* Allocate output buffer; add 18 bytes so that caller can copy the
     * signature and version fields from the file header (as these 18
     * bytes seem to be assumed by the offset values inside descriptor). */
    guint8 *descriptor_data = g_malloc0(header->decompressed_size + 18);
    if (!descriptor_data) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Failed to allocate output buffer!");
        return NULL;
    }

    /* Decompress using zlib */
    z_stream zlib_stream;

    zlib_stream.zalloc = Z_NULL;
    zlib_stream.zfree = Z_NULL;
    zlib_stream.opaque = Z_NULL;

    /* We could also pass descriptor_size here - zlib ignores the extra padding bytes that may be present. */
    zlib_stream.avail_in = header->compressed_size;
    zlib_stream.next_in = data;

    zlib_stream.avail_out = header->decompressed_size;
    zlib_stream.next_out = descriptor_data + 18;

    inflateInit(&zlib_stream);
    rc = inflate(&zlib_stream, Z_FINISH); /* Single-step inflate() */
    inflateEnd(&zlib_stream);

    /* Check return code: should be Z_STREAM_END */
    if (rc != Z_STREAM_END) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Unexpected decompression status code: %d", rc);
        goto error;
    }

    /* Sanity check - ensure decompression consumed the expected amount
     * of input data, and produced expected amount of output data! */
    if (zlib_stream.total_in != header->compressed_size) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Decompression input data length mismatch (expected %d, actual %" G_GINT64_MODIFIER "d)!", header->compressed_size, zlib_stream.total_in);
        goto error;
    }
    if (zlib_stream.total_out != header->decompressed_size) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Decompression output data length mismatch (expected %d, actual %" G_GINT64_MODIFIER "d)!", header->decompressed_size, zlib_stream.total_out);
        goto error;
    }

    return descriptor_data;

error:
    g_free(descriptor_data);
    return NULL;
}
