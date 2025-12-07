/*
 *  libMirage: MDX image
 *  Copyright (C) 2006-2014 Henrik Stokseth
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

#pragma once

#include "mirage/config.h"
#include <mirage/mirage.h>

#include <glib/gi18n-lib.h>

#include "parser.h"

G_BEGIN_DECLS


#define MDX_MEDIA_DESCRIPTOR "MEDIA DESCRIPTOR"

#pragma pack(1)

/* MDSv2/MDX file header (48 bytes) at the start of the file */
typedef struct
{
    gchar media_descriptor[16]; /* "MEDIA DESCRIPTOR" string */
    guint8 version_major; /* Format major version: 2 */
    guint8 version_minor; /* Format minor version: 0, 1 */
    gchar copyright[26]; /* "(C) 2000-2010 DT Soft Ltd." (minor 0), "(C) 2000-2015 Disc Soft Ltd." string (minor 1) */
    guint32 encryption_header_offset; /* Offset to encryption header (MDSv2) or 0xFFFFFFFF (MDX) */
} MDX_FileHeader;


/* Size of the PKCS#5 salt (in bytes) */
#define MDX_PKCS5_SALT_SIZE 64

/* Size of the volume header area containing concatenated master key(s) and secondary key(s) (XTS mode) */
#define MDX_KEYDATA_SIZE 256

/* Size of the deprecated volume header item containing either an IV seed (CBC mode) or tweak key (LRW mode) */
#define MDX_IV_SIZE 32

/* Magic pattern in encryption header: 'T', 'R', 'U', 'E' */
#define MDX_MAGIC_PATTERN 0x54525545

/* 512-byte encryption header */
typedef struct
{
    /* Unencrypted part */
    guint8 salt[MDX_PKCS5_SALT_SIZE];

    /* Encrypted part */
    guint32 key_data_checksum; /* CRC-32 checksum of the (decrypted) key_data bytes. */
    guint32 magic; /* ASCII string 'TRUE' (stored as little-endian integer) */
    guint16 __unknown1__; /* Unknown: always 1? */
    guint16 key_size; /* Data size? Seems to be always 0x100, and matches the size of master key data. */
    guint32 __unknown2__; /* Unknown: always 0? */
    guint8 key_data[MDX_KEYDATA_SIZE]; /* Concatenated primary master key(s) and secondary master key(s) (XTS mode) */
    guint32 compressed_size; /* Compressed MDS descriptor size */
    guint32 decompressed_size; /* Decompressed MDS descriptor size */
    guint8 __unknown3__[168]; /* Zeroes */
} MDX_EncryptionHeader;


/* MDSv2/MDX descriptor header (96 bytes) */
typedef struct
{
    gchar media_descriptor[16]; /* "MEDIA DESCRIPTOR" string */
    guint8 version_major; /* Format major version: 2 */
    guint8 version_minor; /* Format minor version: 0, 1 */

    guint8 __unknown1__[70]; /* TODO */
    guint32 encryption_header_offset; /* Offset to encryption header for encrypted track data. */
    guint32 __unknown2__;
} MDX_DescriptorHeader;


gboolean mdx_crypto_decipher_main_encryption_header (MDX_EncryptionHeader *header, GError **error);
gboolean mdx_crypto_decipher_data_encryption_header (MDX_EncryptionHeader *header, const gchar *password, gsize password_length, GError **error);

guint8 *mdx_crypto_decipher_and_decompress_descriptor(guint8 *data, gsize length, const MDX_EncryptionHeader *header, GError **error);


#pragma pack()


G_END_DECLS
