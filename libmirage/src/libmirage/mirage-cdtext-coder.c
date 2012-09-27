/*
 *  libMirage: CD-TEXT Encoder/Decoder object
 *  Copyright (C) 2006-2012 Rok Mandeljc
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"

#define __debug__ "CDTEXT-Coder"


/**********************************************************************\
 *                         Common definitions                         *
\**********************************************************************/
typedef struct
{
    /* Header */
    guint8 pack_type;
    guint8 track_number;
    guint8 seq_number;
    guint8 block_number;
    /* Data */
    guint8 data[12];
    /* CRC */
    guint8 crc[2];
} CDTextPack;

typedef struct
{
    guint8 charset;
    guint8 first_track;
    guint8 last_track;
    guint8 copyright;
    guint8 pack_count[16];
    guint8 last_seqnum[8];
    guint8 language_codes[8];
} CDTextSizeInfo;

static const guint16 cdtext_crc_lut[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7, 0x8108,
    0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF, 0x1231, 0x0210,
    0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6, 0x9339, 0x8318, 0xB37B,
    0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE, 0x2462, 0x3443, 0x0420, 0x1401,
    0x64E6, 0x74C7, 0x44A4, 0x5485, 0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE,
    0xF5CF, 0xC5AC, 0xD58D, 0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6,
    0x5695, 0x46B4, 0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D,
    0xC7BC, 0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B, 0x5AF5,
    0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12, 0xDBFD, 0xCBDC,
    0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A, 0x6CA6, 0x7C87, 0x4CE4,
    0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41, 0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD,
    0xAD2A, 0xBD0B, 0x8D68, 0x9D49, 0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13,
    0x2E32, 0x1E51, 0x0E70, 0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A,
    0x9F59, 0x8F78, 0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E,
    0xE16F, 0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E, 0x02B1,
    0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256, 0xB5EA, 0xA5CB,
    0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D, 0x34E2, 0x24C3, 0x14A0,
    0x0481, 0x7466, 0x6447, 0x5424, 0x4405, 0xA7DB, 0xB7FA, 0x8799, 0x97B8,
    0xE75F, 0xF77E, 0xC71D, 0xD73C, 0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657,
    0x7676, 0x4615, 0x5634, 0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9,
    0xB98A, 0xA9AB, 0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882,
    0x28A3, 0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92, 0xFD2E,
    0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9, 0x7C26, 0x6C07,
    0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1, 0xEF1F, 0xFF3E, 0xCF5D,
    0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8, 0x6E17, 0x7E36, 0x4E55, 0x5E74,
    0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};


/**********************************************************************\
 *                          Private structure                         *
\***********************************************************************/
#define MIRAGE_CDTEXT_CODER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_CDTEXT_CODER, MirageCdTextCoderPrivate))

typedef struct
{
    gint langcode; /* Language codes */
    gint charset; /* Character set */
    gint first_track; /* First track */
    gint last_track; /* Last track */
    gint copyright; /* Copyright flags */

    GList *packs_list; /* List of packs data */

    CDTextPack *size_info; /* Pointer to size info packs */
    gint seq_count; /* Sequence count */
    gint pack_count[16]; /* Pack types count */
} MirageCdTextCoderBlock;


struct _MirageCdTextCoderPrivate
{
    /* Buffer */
    guint8 *buffer;
    gint buflen;

    /* EncDec */
    CDTextPack *cur_pack; /* Pointer to current pack */
    gint cur_pack_fill; /* How much of current pack is used */

    /* Blocks */
    gint num_blocks; /* Number of blocks */
    MirageCdTextCoderBlock *blocks;

    gint length; /* Overall length, in packs */
};


/***********************************************************************\
 *                          Private functions                          *
\***********************************************************************/
static GArray *create_value_array (gint block, gint type, gint track, const guint8 *data, gint data_len)
{
    GArray *pack_data = g_array_sized_new(FALSE, TRUE, sizeof(gpointer), 5);
    g_assert(sizeof(gpointer) >= sizeof(gint));
    guint8 *data_copy = g_memdup(data, data_len);

    /* Internal representation of pack: block, type, track, data, length */
    g_array_append_val(pack_data, block);
    g_array_append_val(pack_data, type);
    g_array_append_val(pack_data, track);
    g_array_append_val(pack_data, data_copy);
    g_array_append_val(pack_data, data_len);

    return pack_data;
}

static gint sort_pack_data (GArray *pack1, GArray *pack2)
{
    gint block1 = g_array_index(pack1, gint, 0);
    gint block2 = g_array_index(pack2, gint, 0);

    if (block1 < block2) {
        return -1;
    } else if (block1 > block2) {
        return 1;
    } else {
        gint type1 = g_array_index(pack1, gint, 1);
        gint type2 = g_array_index(pack2, gint, 1);

        if (type1 < type2) {
            return -1;
        } else if (type1 > type2) {
            return 1;
        } else {
            gint track1 = g_array_index(pack1, gint, 2);
            gint track2 = g_array_index(pack2, gint, 2);

            if (track1 < track2) {
                return -1;
            } else if (track1 > track2) {
                return 1;
            } else {
                return 0;
            }
        }
    }
}


static void mirage_cdtext_coder_cleanup (MirageCdTextCoder *self)
{
    /* Cleanup the lists */
    for (gint i = 0; i < self->priv->num_blocks; i++) {
        GList *list = self->priv->blocks[i].packs_list;
        if (list) {
            GList *entry = NULL;
            G_LIST_FOR_EACH(entry, list) {
                /* Free pack data */
                gpointer pack_data = g_array_index((GArray *) entry->data, gpointer, 3);
                g_free(pack_data);
                /* Free pack */
                g_array_free((GArray *) entry->data, TRUE);
            }
            g_list_free(list);
        }
    }

    self->priv->buffer = NULL;
    self->priv->buflen = 0;

    memset(self->priv->blocks, 0, self->priv->num_blocks*sizeof(MirageCdTextCoderBlock));
}


static gint mirage_cdtext_coder_lang2block (MirageCdTextCoder *self, gint langcode)
{
    gint ret = 0;

    for (gint i = 0; i < 8; i++) {
        if (self->priv->blocks[i].langcode == langcode) {
            ret = i;
            break;
        }
    }

    return ret;
}

static gint mirage_cdtext_coder_block2lang (MirageCdTextCoder *self, gint block)
{
    return self->priv->blocks[block].langcode;
}


static void add_crc_to_pack (CDTextPack *pack)
{
    /* Calculate CRC for given pack */
    guint8 *data = (guint8 *)pack;
    guint16 crc = 0;

    for (gint i = 0; i < 16; i++) {
        crc = cdtext_crc_lut[(crc >> 8) ^ data[i]] ^ (crc << 8);
    }

    crc = ~crc;

    pack->crc[0] = (crc & 0xFF00) >> 8;
    pack->crc[1] = crc & 0x00FF;
}


static void mirage_cdtext_encoder_initialize_pack (MirageCdTextCoder *self, gint block, gint type, gint track, gint carry_len)
{
    if (!self->priv->cur_pack->pack_type) {
        /*g_debug("%s: Empty pack, initializing\n", __debug__);*/
        self->priv->cur_pack->pack_type = type;
        if (type != MIRAGE_LANGUAGE_PACK_SIZE) {
            self->priv->cur_pack->track_number = track;
            self->priv->cur_pack->seq_number = self->priv->blocks[block].seq_count;
            self->priv->cur_pack->block_number |= (block) << 4;
            self->priv->cur_pack->block_number |= (carry_len < 15) ? carry_len : 15;
        } else {
            /* Special handling for 0x8F packs */
            self->priv->cur_pack->track_number = self->priv->blocks[block].pack_count[type-0x80];
            self->priv->cur_pack->seq_number = self->priv->blocks[block].seq_count;
            self->priv->cur_pack->block_number |= (block) << 4;
            /* Set up pointer to first pack of size info */
            if (!self->priv->blocks[block].size_info) {
                self->priv->blocks[block].size_info = self->priv->cur_pack;
            }
        }

        self->priv->blocks[block].seq_count++;
        self->priv->blocks[block].pack_count[type-0x80]++;
        self->priv->length++;
    } else {
        /*g_debug("%s: Pack already initialized (0x%X)\n", __debug__, encoder->cur_pack->pack_type);*/
    }
}

static void mirage_cdtext_encoder_pack_data (MirageCdTextCoder *self, GArray *pack) {
    /* Unpack data */
    gint block_number = g_array_index(pack, gint, 0);
    gint pack_type = g_array_index(pack, gint, 1);
    gint track_number = g_array_index(pack, gint, 2);
    guint8 *data = g_array_index(pack, guint8 *, 3);
    gint len = g_array_index(pack, gint, 4);

    /* If current pack is already initialized and the data we're trying to pack
       is if different type, open new pack; this way, we don't have to check if
       the language has changed (if it has, so has the pack type... from 0x8F
       to 0x8X */
    if (self->priv->cur_pack->pack_type && (pack_type != self->priv->cur_pack->pack_type)) {
        /*g_debug("%s: Different pack type, open new pack!\n", __debug__);*/
        self->priv->cur_pack++;
        self->priv->cur_pack_fill = 0;
    }

    gint cur_len = len;
    guint8 *ptr = data;
    gint carry_len = 0; /* How many of characters are contained in previous pack(s) */
    while (cur_len > 0) {
        /* If the current pack is full, open a new one */
        if (self->priv->cur_pack_fill == 12) {
            /*g_debug("%s: Current pack full, moving to next!\n", __debug__);*/
            /* Open new pack */
            self->priv->cur_pack++;
            self->priv->cur_pack_fill = 0;
        }

        mirage_cdtext_encoder_initialize_pack(self, block_number, pack_type, track_number, carry_len);

        gint copy_len = MIN(12 - self->priv->cur_pack_fill, cur_len);
        /*g_debug("%s: Copying %i bytes (cur_len = %i, cur_fill = %i)\n", __debug__, copy_len, cur_len, encoder->cur_pack_fill);*/

        memcpy(self->priv->cur_pack->data+self->priv->cur_pack_fill, ptr, copy_len);

        /* Set the current fill */
        self->priv->cur_pack_fill += copy_len;

        cur_len -= copy_len;
        ptr += copy_len;
        carry_len += copy_len;
    }
}

static void mirage_cdtext_encoder_generate_size_info (MirageCdTextCoder *self, gint block, guint8 **data, gint *len)
{
    CDTextSizeInfo *size_info = g_new0(CDTextSizeInfo, 1);

    size_info->charset = self->priv->blocks[block].charset; /* Character set */
    size_info->first_track = self->priv->blocks[block].first_track; /* First track */
    size_info->last_track = self->priv->blocks[block].last_track; /* Last track */
    size_info->copyright = self->priv->blocks[block].copyright; /* Copyright */

    /* Pack count */
    for (gint i = 0; i < 16; i++) {
        size_info->pack_count[i] = self->priv->blocks[block].pack_count[i];
    }

    /* Last sequence numbers and language codes */
    for (gint i = 0; i < 8; i++) {
        /* Set only if we have at least one pack for that language */
        if (self->priv->blocks[i].seq_count > 0) {
            size_info->last_seqnum[i] = self->priv->blocks[i].seq_count-1; /* It overshoots by one */
            size_info->language_codes[i] = self->priv->blocks[i].langcode;
        }
    }

    *data = (guint8 *)size_info;
    *len = sizeof(CDTextSizeInfo);
}

static void mirage_cdtext_decoder_read_size_info (MirageCdTextCoder *self G_GNUC_UNUSED, CDTextPack *size_info_pack, CDTextSizeInfo **data)
{
    guint8 *size_info = g_malloc0(sizeof(CDTextSizeInfo));
    CDTextPack *cur_pack = size_info_pack;

    for (gint i = 0; i < sizeof(CDTextSizeInfo)/12; i++) {
        memcpy(size_info+12*i, cur_pack->data, 12);
        cur_pack++;
    }

    *data = (CDTextSizeInfo *)size_info;
}


/**********************************************************************\
 *                         Public API: Encoder                        *
\**********************************************************************/
/**
 * mirage_cdtext_encoder_init:
 * @self: a #MirageCdTextCoder
 * @buffer: (in) (array length=buflen): buffer into which data will be encoded
 * @buflen: (in): buffer length
 *
 * <para>
 * Initializes CD-TEXT encoder.
 * </para>
 **/
void mirage_cdtext_encoder_init (MirageCdTextCoder *self, guint8 *buffer, gint buflen)
{
    /* Cleanup old data */
    mirage_cdtext_coder_cleanup(self);

    /* Set new buffer */
    self->priv->buffer = buffer;
    self->priv->buflen = buflen;

    self->priv->cur_pack = (CDTextPack *)self->priv->buffer;
}

/**
 * mirage_cdtext_encoder_set_block_info:
 * @self: a #MirageCdTextCoder
 * @block: (in): block number
 * @langcode: (in): language code
 * @charset: (in): character set
 * @copyright: (in): copyright flag
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Sets block information for CD-TEXT block specified by @block. @block must be
 * a valid block number (0-7). @langcode is the language code that is to be assigned
 * to the block (e.g. 9 for English), @charset denotes character set that is used within
 * the block, and @copyright is the copyright flag for the block.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_cdtext_encoder_set_block_info (MirageCdTextCoder *self, gint block, gint langcode, gint charset, gint copyright, GError **error)
{
    /* Verify that block is valid */
    if (block > self->priv->num_blocks) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_CDTEXT, "%s: invalid block (%i)!\n", __debug__, block);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LANGUAGE_ERROR, "Invalid block number #%i!", block);
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_CDTEXT, "%s: initialized block %i; langcode: %i; charset: %i; copyright: %i\n", __debug__, block, langcode, charset, copyright);
    self->priv->blocks[block].langcode = langcode;
    self->priv->blocks[block].charset = charset;
    self->priv->blocks[block].copyright = copyright;

    return TRUE;
}

/**
 * mirage_cdtext_encoder_add_data:
 * @self: a #MirageCdTextCoder
 * @langcode: (in): language code
 * @type: (in): data type
 * @track: (in): track number
 * @data: (in) (array length=data_len): data
 * @data_len: (in): data length
 *
 * <para>
 * Adds data to the encoder. @langcode is language code of the block the data
 * should be added to. @type denotes pack type and should be one of #MirageLanguagePackTypes.
 * @track is track number the data belongs to, or 0 if data is global (belongs to disc/session).
 * @data is buffer containing data to be added, and @data_len is length of data in the buffer.
 * </para>
 *
 * <para>
 * This function does not perform any encoding yet; it merely adds the data into
 * encoder's internal representation of CD-TEXT block.
 * </para>
 *
 * <note>
 * Block needs to have its information set with mirage_cdtext_encoder_set_block_info()
 * before data can be added to it.
 * </note>
 **/
void mirage_cdtext_encoder_add_data (MirageCdTextCoder *self, gint langcode, gint type, gint track, const guint8 *data, gint data_len)
{
    /* Langcode -> block conversion */
    gint block = mirage_cdtext_coder_lang2block(self, langcode);
    GArray *pack_data = create_value_array(block, type, track, data, data_len);

    /* Add internal representation to ordered list... */
    self->priv->blocks[block].packs_list = g_list_insert_sorted(self->priv->blocks[block].packs_list, pack_data, (GCompareFunc)sort_pack_data);

    /* Set the number of first track that has language... this is not
       very reliable, but I do believe all tracks from now on are
       required by specs to have language block? */
    if (!self->priv->blocks[block].first_track) {
        self->priv->blocks[block].first_track = track;
    }
    /* Keep setting the last track... */
    self->priv->blocks[block].last_track = track;
}

/**
 * mirage_cdtext_encoder_encode:
 * @self: a #MirageCdTextCoder
 * @buffer: (out) (array length=buflen): location to store buffer
 * @buflen: (out): location to store buffer length
 *
 * <para>
 * Encodes the CD-TEXT data. Pointer to buffer containing the encoded data is
 * stored in @buffer, and length of data in buffer is stored in @buflen.
 * </para>
 *
 * <para>
 * Note that @buffer is the same as the argument passed to mirage_cdtext_encoder_init().
 * </para>
 **/
void mirage_cdtext_encoder_encode (MirageCdTextCoder *self, guint8 **buffer, gint *buflen)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_CDTEXT, "%s: encoding CD-TEXT...\n", __debug__);

    /* Encode all blocks */
    for (gint i = 0; i < self->priv->num_blocks; i++) {
        /* Block is valid only if it has langcode set */
        if (self->priv->blocks[i].langcode) {
            GList *entry = NULL;

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_CDTEXT, "%s: encoding block %i; langcode %i\n", __debug__, i, self->priv->blocks[i].langcode);

            /* Encode all on list */
            G_LIST_FOR_EACH(entry, self->priv->blocks[i].packs_list) {
                GArray *pack_data = entry->data;
                mirage_cdtext_encoder_pack_data(self, pack_data);
            }

            /* We need to 'reserve' some space for size info */
            CDTextSizeInfo size_info;
            memset(&size_info, 0, sizeof(size_info));

            GArray *dummy_data = create_value_array(i, 0x8F, 0, (guint8 *)&size_info, sizeof(size_info));
            mirage_cdtext_encoder_pack_data(self, dummy_data);
            g_array_free(dummy_data, TRUE);
        } else {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_CDTEXT, "%s: block %i not valid\n", __debug__, i);
        }
    }

    /* Now that all the packs have been encoded, we can generate size info */
    for (gint i = 0; i < 8; i++) {
        /* Having pointer set means we're using this language... */
        if (self->priv->blocks[i].size_info) {
            guint8 *size_info = NULL;
            gint size_info_len = 0;

            mirage_cdtext_encoder_generate_size_info(self, i, &size_info, &size_info_len);

            /* Write into previously reserved size info packs */
            /*g_debug("%s: Writing info into packs at %p\n", __debug__, encoder.size_info[i]);*/
            self->priv->cur_pack = self->priv->blocks[i].size_info;
            self->priv->cur_pack_fill = 0;
            gint old_len = self->priv->length;

            GArray *pack_data = create_value_array(i, 0x8F, 0, (guint8 *)size_info, size_info_len);
            mirage_cdtext_encoder_pack_data(self, pack_data);
            self->priv->length = old_len;

            g_array_free(pack_data, TRUE);
            g_free(size_info);
        }
    }

    /* Generate CRC for all packs */
    self->priv->cur_pack = (CDTextPack *)self->priv->buffer;
    for (gint i = 0; i < self->priv->length; i++) {
        add_crc_to_pack(self->priv->cur_pack);
        self->priv->cur_pack++;
    }

    *buflen = self->priv->length*sizeof(CDTextPack);
    *buffer = self->priv->buffer;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_CDTEXT, "%s: done encoding CD-TEXT; length: 0x%X\n", __debug__, *buflen);
}


/**********************************************************************\
 *                         Public API: Decoder                        *
\**********************************************************************/
/**
 * mirage_cdtext_decoder_init:
 * @self: a #MirageCdTextCoder
 * @buffer: (in) (array length=buflen): buffer containing encoded data
 * @buflen: (in): length of data in buffer
 *
 * <para>
 * Initializes CD-TEXT decoder. @buffer is the buffer containing encoded CD-TEXT
 * data and @buflen is length of data in the buffer.
 * </para>
 *
 * <para>
 * This function decodes CD-TEXT data and stores it in decoder's internal representation.
 * Information about decoded CD-TEXT blocks and their data can be obtained via
 * subsequent calls to mirage_cdtext_decoder_get_block_info() and
 * mirage_cdtext_decoder_get_data().
 * </para>
 *
 **/
void mirage_cdtext_decoder_init (MirageCdTextCoder *self, guint8 *buffer, gint buflen)
{
    /* Cleanup old data */
    mirage_cdtext_coder_cleanup(self);

    /* Set new buffer */
    self->priv->buffer = buffer;
    self->priv->buflen = buflen;
    self->priv->cur_pack = (CDTextPack *)self->priv->buffer;
    self->priv->length = buflen/sizeof(CDTextPack);

    /* Read size info packs */
    gint i = 0;
    while (i < self->priv->length) {
        CDTextPack *cur_pack = self->priv->cur_pack+i;

        if (cur_pack->pack_type == 0x8F) {
            gint block = (cur_pack->block_number & 0xF0) >> 4;
            self->priv->blocks[block].size_info = cur_pack;

            CDTextSizeInfo *size_info = NULL;
            mirage_cdtext_decoder_read_size_info(self, cur_pack, &size_info);
            self->priv->blocks[block].langcode = size_info->language_codes[block];
            self->priv->blocks[block].charset = size_info->charset;
            self->priv->blocks[block].copyright = size_info->copyright;
            self->priv->blocks[block].first_track = size_info->first_track;
            self->priv->blocks[block].last_track = size_info->last_track;

            self->priv->blocks[block].seq_count = size_info->last_seqnum[block] + 1;
            for (gint j = 0; j < 16; j++) {
                self->priv->blocks[block].pack_count[j] = size_info->pack_count[j];
            }
            g_free(size_info);

            i += 3;
        } else {
            i++;
        }
    }

    /* Now decode... go over all blocks and decode valid ones */
    for (gint block = 0; block < 8; block++) {
        gchar tmp_buffer[255];
        memset(tmp_buffer, 0xFF, sizeof(tmp_buffer));
        gchar *ptr = tmp_buffer;
        gint tmp_len = 0;
        gint cur_track = 0;

        /* Skip empty blocks */
        if (!self->priv->blocks[block].seq_count) {
            continue;
        }

        self->priv->cur_pack = self->priv->blocks[block].size_info - (self->priv->blocks[block].seq_count - 3);

        while (self->priv->cur_pack < self->priv->blocks[block].size_info) {
            if (self->priv->cur_pack->pack_type != (self->priv->cur_pack - 1)->pack_type) {
                /*g_debug("%s: new pack type, resetting strings...\n", __debug__);*/
                memset(tmp_buffer, 0xFF, sizeof(tmp_buffer));
                tmp_len = 0;
                ptr = tmp_buffer;
            }

            /* Current data offset */
            gchar *cur_data = (gchar *)self->priv->cur_pack->data + self->priv->cur_pack_fill;
            /* Length of transfer */
            gint copy_len = MIN(strlen(cur_data) + 1, 12 - self->priv->cur_pack_fill);

            /* Copy */
            memcpy(ptr, self->priv->cur_pack->data + self->priv->cur_pack_fill, copy_len);
            /* Do some necesssary calcs */
            self->priv->cur_pack_fill += copy_len;
            ptr += copy_len;
            tmp_len += copy_len;

            /* The way of the lazy programmer (TM)... we set whole string to 0xFF
               and then wait 'till we get terminating 0 at the end :D (we also need
               to check whether string doesn't contain just "\0", which keeps getting
               returned once we hit the padding at the end of a pack types...) */
            if (!tmp_buffer[tmp_len-1] && strlen(tmp_buffer)) {
                /*g_debug("%s: block: %i; pack type: 0x%X; track: %i; len: %i; data: %s\n", __debug__, block, self->priv->cur_pack->pack_type, cur_track, tmp_len, tmp_buffer);*/

                /* Pack the data and add it to the list; as simple as that... */
                GArray *pack_data = create_value_array(block, self->priv->cur_pack->pack_type, cur_track, (guint8 *)tmp_buffer, tmp_len);
                self->priv->blocks[block].packs_list = g_list_insert_sorted(self->priv->blocks[block].packs_list, pack_data, (GCompareFunc)sort_pack_data);

                /* Clear the temporary buffer */
                memset(tmp_buffer, 0xFF, sizeof(tmp_buffer));
                tmp_len = 0;
                ptr = tmp_buffer;

                /* Increase track number; this is to account for strings too short
                   to cause switch to next pack, whose track number is consequently
                   stored nowhere... */
                cur_track++;
            }

            if (self->priv->cur_pack_fill == 12) {
                /*g_debug("%s: reached the end of packet, going to the next one\n");*/
                self->priv->cur_pack_fill = 0;
                self->priv->cur_pack++;
                /* Set current track number */
                cur_track = self->priv->cur_pack->track_number;
            }
        }
    }
}

/**
 * mirage_cdtext_decoder_get_block_info:
 * @self: a #MirageCdTextCoder
 * @block: (in): block number
 * @langcode: (out) (allow-none): location to store language code, or %NULL
 * @charset: (out) (allow-none): location to store character set, or %NULL
 * @copyright: (out) (allow-none): location to store copyright flag, or %NULL
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves block information for CD-TEXT block specified by @block. @block
 * must be a valid block number (0-7). Language code assigned to the block is
 * stored in @langcode, code of character set used within block is stored in
 * @charset and block's copyright flag is stored in @copyright.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_cdtext_decoder_get_block_info (MirageCdTextCoder *self, gint block, gint *langcode, gint *charset, gint *copyright, GError **error)
{
    /* Verify that block is valid */
    if (block > self->priv->num_blocks) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LANGUAGE_ERROR, "Block number %d exceeds number of blocks %d!", block, self->priv->num_blocks);
        return FALSE;
    }
    if (!self->priv->blocks[block].langcode) {
        /* FIXME: error */
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LANGUAGE_ERROR, "Requested block %d has no language code set!", block);
        return FALSE;
    }

    if (langcode) {
        *langcode = self->priv->blocks[block].langcode;
    }
    if (charset) {
        *charset = self->priv->blocks[block].charset;
    }
    if (copyright) {
        *copyright = self->priv->blocks[block].copyright;
    }

    return TRUE;
}

/**
 * mirage_cdtext_decoder_get_data:
 * @self: a #MirageCdTextCoder
 * @block: (in): block number
 * @callback_func: (in) (scope call): callback function
 * @user_data: (in) (closure): data to be passed to callback function
 *
 * <para>
 * Retrieves data for CD-TEXT block specified by @block. @block must be a valid
 * block number (0-7). It calls @callback_func for every data pack that has been
 * encoded in the block.
 * </para>
 *
 * <para>
 * If @callback_func returns %FALSE, the function immediately returns %FALSE.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_cdtext_decoder_get_data (MirageCdTextCoder *self, gint block, MirageCdTextDataCallback callback_func, gpointer user_data)
{
    GList *entry;

    /* Go over the list and call the callback for each entry */
    G_LIST_FOR_EACH(entry, self->priv->blocks[block].packs_list) {
        GArray *pack_data = entry->data;

        gint block_number = g_array_index(pack_data, gint, 0);
        gint pack_type = g_array_index(pack_data, gint, 1);
        gint track_number = g_array_index(pack_data, gint, 2);
        guint8 *data = g_array_index(pack_data, guint8 *, 3);
        gint len = g_array_index(pack_data, gint, 4);

        gint langcode = mirage_cdtext_coder_block2lang(self, block_number);

        if (!callback_func(langcode, pack_type, track_number, data, len, user_data)) {
            return FALSE;
        }
    }

    return TRUE;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE(MirageCdTextCoder, mirage_cdtext_coder, MIRAGE_TYPE_OBJECT);


static void mirage_cdtext_coder_init (MirageCdTextCoder *self)
{
    self->priv = MIRAGE_CDTEXT_CODER_GET_PRIVATE(self);

    /* Specs say there can be 8 blocks max... */
    self->priv->num_blocks = 8;
    self->priv->blocks = g_new0(MirageCdTextCoderBlock, self->priv->num_blocks);
}

static void mirage_cdtext_coder_finalize (GObject *gobject)
{
    MirageCdTextCoder *self = MIRAGE_CDTEXT_CODER(gobject);

    /* Cleanup the data */
    mirage_cdtext_coder_cleanup(self);

    /* Free blocks data */
    g_free(self->priv->blocks);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_cdtext_coder_parent_class)->finalize(gobject);
}

static void mirage_cdtext_coder_class_init (MirageCdTextCoderClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = mirage_cdtext_coder_finalize;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageCdTextCoderPrivate));
}
