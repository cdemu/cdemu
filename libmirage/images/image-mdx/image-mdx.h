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

#include <gcrypt.h>

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


typedef enum
{
    MDX_MEDIUM_CD_ROM = 0, /* CD-ROM */
    MDX_MEDIUM_DVD_ROM = 3, /* DVD-ROM */
} MDX_MediumType;

/* MDSv2/MDX descriptor header (96 bytes) */
typedef struct
{
    gchar media_descriptor[16]; /* "MEDIA DESCRIPTOR" string */
    guint8 version_major; /* Format major version: 2 */
    guint8 version_minor; /* Format minor version: 0, 1 */

    guint16 medium_type; /* Medium type */
    guint16 num_sessions; /* Number of sessions */

    guint8 __unknown1__[8]; /* TODO */
    guint16 cdtext_size; /* Size of CD-TEXT data block (CD-ROM) */
    guint8 __unknown2__[8]; /* TODO */
    guint32 cdtext_offset; /* Offset to CD-TEXT data block (CD-ROM) */
    guint8 __unknown3__[36]; /* TODO */

    guint32 sessions_blocks_offset; /* Offset to session blocks */
    guint32 dpm_blocks_offset; /* Offset to DPM data blocks */
    guint32 encryption_header_offset; /* Offset to encryption header for encrypted track data. */
    guint32 __unknown4__;
} MDX_DescriptorHeader;

typedef enum
{
    /* NOTE: these values are the same as the ones used with 3-bit
     * "Expected Sector Type" field of READ CD packet command
     * (see MMC-3/INF-8090) */
    MDX_SECTOR_AUDIO = 1, /* Audio sector */
	MDX_SECTOR_MODE1 = 2, /* Mode 1 */
	MDX_SECTOR_MODE2 = 3, /* Mode 2 (formless) */
	MDX_SECTOR_MODE2_FORM1 = 4, /* Mode 2 Form 1 */
	MDX_SECTOR_MODE2_FORM2 = 5 /* Mode 2 Form 2*/
} MDX_SectorType;

typedef enum
{
    /* NOTE: these values are the same as the ones used with 3-bit
     * "Sub-Channel Data Selection Bits" field of READ CD packet command
     * (see MMC-3/INF-8090) */
    MDX_SUBCHANNEL_NONE = 0, /* No subchannel */
    MDX_SUBCHANNEL_PW = 1, /* Raw 96-byte PW */
    MDX_SUBCHANNEL_Q = 2, /* 16-byte PQ data */
    MDX_SUBCHANNEL_RW = 4, /* 96-byte RW */
} MDX_SubchannelType;

/* Session block (32 bytes) */
typedef struct
{
    guint64 session_start; /* Session's start address */
    guint16 session_number; /* Session number */
    guint8 num_all_blocks; /* Number of all data blocks */
    guint8 num_nontrack_blocks; /* Number of lead-in data blocks */
    guint16 first_track; /* First track in session */
    guint16 last_track; /* Last track in session */
    guint32 __unknown1__;
    guint32 tracks_blocks_offset; /* Offset of lead-in+regular track data blocks. */
    guint64 session_end; /* Session's end address */
} MDX_SessionBlock;

/* Track block (80 bytes) */
typedef struct
{
    /* Track mode for sector data (lowest three bits), plus extra data
     * availability flags.
     *
     * The structure of this byte matches the byte 9 of READ CD packet
     * command (see MMC-3/INF-8090), with the lowest three bits (2-bit
     * "Error Flags" and 1-bit "Reserved") replaced by 3-bit value of
     * the "Expected Sector Type" from Byte 1 of READ CD packet command. */
#if G_BYTE_ORDER == G_BIG_ENDIAN
    guint8 has_sync_pattern : 1;
    guint8 has_subheader : 1;
    guint8 has_header : 1;
    guint8 has_unknown : 1;
    guint8 has_edc_ecc : 1;
    guint6 sector_mode : 3;
#else
    guint8 sector_mode : 3;
    guint8 has_edc_ecc : 1;
    guint8 has_unknown : 1;
    guint8 has_header : 1;
    guint8 has_subheader : 1;
    guint8 has_sync_pattern : 1;
#endif

    /* This field contains three bits (as indicated by bit-masking in
     * https://github.com/Marisa-Chan/mdsx) that seem to directly
     * correspond to 3-bit "Sub-Channel Data Selection Bits" from READ
     * CD packet command; although here, they are shifted three bits to
     * the left, for some reason.
     *
     * The subchannel value seems to be set only in MDX v2.1; in v2.0,
     * the value seems to be 0 even if subchannel data is present. */
#if G_BYTE_ORDER == G_BIG_ENDIAN
    guint8 __unknown1__ : 2;
    guint8 subchannel : 3;  /* Subchannel mode */
    guint8 __unknown2__ : 3;
#else
    guint8 __unknown1__ : 3;
    guint8 subchannel : 3;  /* Subchannel mode */
    guint8 __unknown2__ : 2;
#endif

    /* These are the fields from Sub-channel Q information, which are
     * also returned in full TOC by READ TOC/PMA/ATIP command */
    guint8 adr_ctl; /* Adr/Ctl */
    guint8 tno; /* Track number field */
    guint8 point; /* Point field (= track number for track entries) */
    guint8 min; /* Min */
    guint8 sec; /* Sec */
    guint8 frame; /* Frame */
    guint8 zero; /* Zero */
    guint8 pmin; /* PMin */
    guint8 psec; /* PSec */
    guint8 pframe; /* PFrame */

    guint32 extra_offset; /* Start offset of this track's extra block. */
    guint16 sector_size; /* Sector size. */

    guint8 __unknown3__[18];

    guint32 start_sector; /* Track start sector (PLBA). */
    guint64 start_offset; /* Track start offset. */
    guint32 footer_count; /* Number of footer entries (= number of files when split) */
    guint32 footer_offset; /* Start offset of footer. */

    guint64 start_sector64; /* TODO - MDSv2 specific */
    guint64 track_length64; /* TODO - MDSv2 specific */

    guint8 __unknown4__[8];
} MDX_TrackBlock;

/* Extra track block (8 bytes) */
typedef struct
{
    guint32 pregap; /* Number of sectors in pregap. */
    guint32 length; /* Number of sectors in track. */
} MDX_TrackExtraBlock;

/* Footer block (32 bytes) */
typedef struct
{
    guint32 filename_offset; /* Start offset of image filename. */
    guint8 flags;
    guint8 __unknown1__;
    guint16 __unknown2__;
    guint32 __unknown3__;
    guint32 blocks_in_compression_group; /* Number of blocks in compression group */
    guint64 track_data_length; /* Number of sectors covered by this footer block. */
    guint64 compression_table_offset; /* Offset to compression table */
} MDX_Footer;


gboolean mdx_crypto_decipher_encryption_header (
    MDX_EncryptionHeader *header,
    const gchar *password,
    const gsize password_length,
    const gboolean main_header,
    GError **error
);

guint8 *mdx_crypto_decipher_and_decompress_descriptor(
    guint8 *data,
    const gsize length,
    const MDX_EncryptionHeader *header,
    GError **error
);

gboolean mdx_crypto_decipher_buffer_lrw (
    gcry_cipher_hd_t crypt_handle,
    guint8 *data,
    gsize len,
    const guint8 *tweak_key,
    guint64 sector_number,
    GError **error
);


#pragma pack()


G_END_DECLS
