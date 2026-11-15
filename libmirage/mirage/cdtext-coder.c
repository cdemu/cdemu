/*
 *  libMirage: CD-TEXT encoder/decoder
 *  Copyright (C) 2006-2026 Rok Mandeljc
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

/**
 * SECTION: mirage-cdtext-coder
 * @title: MirageCdTextCoder
 * @short_description: General-purpose CD-TEXT encoder/decoder object.
 * @see_also: #MirageLanguage, #MirageSession
 * @include: mirage-cdtext-coder.h
 *
 * #MirageCdTextCoder object is a general-purpose CD-TEXT encoder/decoder.
 * It was designed to be used by #MirageSession objects to encode and decode
 * CD-TEXT data, but it could be used in other applications as well.
 *
 * It is loosely based on the CD-TEXT encoding/decoding code found in
 * cdrdao and supports 8 CD-TEXT blocks with pack types from 0x80 to
 * 0x8F. When encoding data, pack size data (pack type 0x8F) is always
 * generated.
 *
 * To be used as encoder, a #MirageCdTextCoder encoder must be first
 * initialized with mirage_cdtext_encoder_init(). Then, information for
 * at least one CD-TEXT block (up to 8 are supported) should be set with
 * mirage_cdtext_encoder_set_block_info(). After all the CD-TEXT data is
 * added to encoder with mirage_cdtext_encoder_add_data(), buffer containing
 * the encoded data can be obtained with mirage_cdtext_encoder_encode().
 *
 * To use a #MirageCdTextCoder as CD-TEXT decoder, one should first
 * initialize it with mirage_cdtext_decoder_init(). This function already
 * performs all the decoding; block information can be obtained with
 * mirage_cdtext_decoder_get_block_info() and data for each block can
 * be obtained with mirage_cdtext_decoder_get_data() and the appropriate
 * callback function.
 */

#include "mirage/config.h"
#include "mirage/mirage.h"

#include <glib/gi18n-lib.h>

#define __debug__ "CDTEXT-Coder"


/**********************************************************************\
 *                         Common definitions                         *
\**********************************************************************/
typedef struct
{
    gint block_number;
    gint pack_type;
    gint track_number;

    guint8 *data;
    gint data_len;
} CDTextDecodedPack;

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
} CDTextEncodedPack;

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


/**********************************************************************\
 *                  Object and its private structure                  *
\**********************************************************************/
typedef struct
{
    gint code; /* Language code */
    gint charset; /* Character set */
    gint first_track; /* First track */
    gint last_track; /* Last track */
    gint copyright; /* Copyright flags */

    GList *packs_list; /* List of packs data */

    CDTextEncodedPack *size_info; /* Pointer to size info packs */
    gint seq_count; /* Sequence count */
    gint pack_count[16]; /* Pack types count */
} MirageCdTextCoderBlock;


struct _MirageCdTextCoderPrivate
{
    /* Buffer */
    guint8 *buffer;
    gint buflen;

    /* EncDec */
    CDTextEncodedPack *cur_pack; /* Pointer to current pack */
    gint cur_pack_fill; /* How much of current pack is used */

    /* Blocks */
    MirageCdTextCoderBlock blocks[8];

    gint length; /* Overall length, in packs */
};


G_DEFINE_TYPE_WITH_PRIVATE(MirageCdTextCoder, mirage_cdtext_coder, MIRAGE_TYPE_OBJECT)


/***********************************************************************\
 *                          Private functions                          *
\***********************************************************************/
static gint sort_pack_data (const CDTextDecodedPack *pack1, const CDTextDecodedPack *pack2)
{
    if (pack1->block_number == pack2->block_number) {
        if (pack1->pack_type == pack2->pack_type) {
            return pack1->track_number - pack2->track_number;
        }
        return pack1->pack_type - pack2->pack_type;
    }
    return pack1->block_number - pack2->block_number;
}


static void mirage_cdtext_coder_cleanup (MirageCdTextCoder *self)
{
    /* Cleanup the lists */
    for (guint i = 0; i < G_N_ELEMENTS(self->priv->blocks); i++) {
        MirageCdTextCoderBlock *block = &self->priv->blocks[i];

        GList *list = block->packs_list;
        if (!list) {
            continue;
        }

        for (GList *entry = list; entry; entry = entry->next) {
            CDTextDecodedPack *pack_data = entry->data;

            g_free(pack_data->data);
            g_free(pack_data);
        }
        g_list_free(list);
    }

    self->priv->buffer = NULL;
    self->priv->buflen = 0;

    memset(self->priv->blocks, 0, sizeof(self->priv->blocks));
}


static gint mirage_cdtext_coder_lang2block (MirageCdTextCoder *self, gint code)
{
    gint ret = 0;

    for (guint i = 0; i < G_N_ELEMENTS(self->priv->blocks); i++) {
        const MirageCdTextCoderBlock *block = &self->priv->blocks[i];
        if (block->code == code) {
            ret = i;
            break;
        }
    }

    return ret;
}

static gint mirage_cdtext_coder_block2lang (MirageCdTextCoder *self, gint block_number)
{
    const MirageCdTextCoderBlock *block = &self->priv->blocks[block_number];
    return block->code;
}


static void add_crc_to_pack (CDTextEncodedPack *pack)
{
    guint16 crc;
    guint16 *dest = (guint16 *)(void *)&pack->crc;

    crc = mirage_helper_calculate_crc16((const guint8 *)pack, 16, crc16_1021_lut, FALSE, TRUE);

    *dest = GUINT16_TO_BE(crc);
}


static void mirage_cdtext_encoder_initialize_pack (MirageCdTextCoder *self, gint block_number, gint type, gint track, gint carry_len)
{
    CDTextEncodedPack *cur_pack = self->priv->cur_pack;
    if (cur_pack->pack_type != 0) {
        return; /* Already initialized */
    }

    MirageCdTextCoderBlock *block = &self->priv->blocks[block_number];

    cur_pack->pack_type = type;
    if (type == MIRAGE_LANGUAGE_PACK_SIZE) {
        /* Special handling for 0x8F packs */
        cur_pack->track_number = block->pack_count[type - 0x80];
        cur_pack->seq_number = block->seq_count;
        cur_pack->block_number |= (block_number) << 4;
        /* Set up pointer to first pack of size info */
        if (!block->size_info) {
            block->size_info = cur_pack;
        }
    } else {
        cur_pack->track_number = track;
        cur_pack->seq_number = block->seq_count;
        cur_pack->block_number |= (block_number) << 4;
        cur_pack->block_number |= (carry_len < 15) ? carry_len : 15;
    }

    block->seq_count++;
    block->pack_count[type - 0x80]++;
    self->priv->length++;
}

static void mirage_cdtext_encoder_encode_pack (MirageCdTextCoder *self, CDTextDecodedPack *pack)
{
    /* If current pack is already initialized and the pack we are trying to encode
     * is of a different type, open a new pack; this way, we don't have to check if
     * the language has changed (if it has, so has the pack type... from 0x8F
     * to 0x8X) */
    if (self->priv->cur_pack->pack_type && (pack->pack_type != self->priv->cur_pack->pack_type)) {
        self->priv->cur_pack++;
        self->priv->cur_pack_fill = 0;
    }

    gint cur_len = pack->data_len;
    const guint8 *ptr = pack->data;
    gint carry_len = 0; /* How many of characters are contained in previous pack(s) */

    /* Text in packs 0x80 to 0x85 needs to be converted to the block's
     * character set. The rest of the packs either contain binary data,
     * or have a fixed encoding (for example, 0x8E is supposed to contain
     * ASCII text). */
    gchar *converted_text = NULL;
    if (pack->pack_type >= 0x80 && pack->pack_type <= 0x85) {
        GError *error = NULL;
        gsize bytes_read;
        gsize bytes_written;

        /* Internally, we store data as UTF-8 */
        const gchar *source_encoding = "UTF-8";

        /* Determine target encoding, by looking it up in the pack's
         * language block */
        const MirageCdTextCoderBlock *block = &self->priv->blocks[pack->block_number];
        const gchar *target_encoding = "ISO-8859-1";
        switch (block->charset) {
            /* Language codes as per EN 60908:1999 (see also
             * https://libcdio.github.io/cd-text-format.html). The same
             * values are also used by MirageLanguageEncoding enum. */
            case MIRAGE_LANGUAGE_ENCODING_ISO88591: {
                target_encoding = "ISO-8859-1";
                break;
            }
            case MIRAGE_LANGUAGE_ENCODING_ASCII: {
                target_encoding = "ASCII";
                break;
            }
            case MIRAGE_LANGUAGE_ENCODING_MS_JIS: {
                target_encoding = "CP932"; /* cdrdao uses CP932 for MS-JIS */
                break;
            }
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unsupported charset/encoding code 0x%X; falling back to ISO-8859-1", __debug__, block->charset);
                break;
            }
        }

        converted_text = g_convert(
            (const gchar *)pack->data,
            pack->data_len, /* includes terminating NUL */
            target_encoding,
            source_encoding,
            &bytes_read,
            &bytes_written,
            &error
        );

        if (converted_text) {
            ptr = (const guint8 *)converted_text;
            cur_len = bytes_written; /* includes NUL from the input data */
        } else {
            /* Conversion failed; display a warning, and use original data... */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to convert CD-TEXT data (block number: %d, pack type: 0x%02X, track number: %d) from UTF-8 to target encoding (%s): %s", __debug__, pack->block_number, pack->pack_type, pack->track_number, target_encoding, error->message);
            g_error_free(error);
        }
    }

    while (cur_len > 0) {
        gint copy_len;

        /* If the current pack is full, open a new one */
        if (self->priv->cur_pack_fill == 12) {
            /* Open new pack */
            self->priv->cur_pack++;
            self->priv->cur_pack_fill = 0;
        }

        mirage_cdtext_encoder_initialize_pack(self, pack->block_number, pack->pack_type, pack->track_number, carry_len);

        copy_len = MIN(12 - self->priv->cur_pack_fill, cur_len);

        memcpy(self->priv->cur_pack->data + self->priv->cur_pack_fill, ptr, copy_len);

        /* Set the current fill */
        self->priv->cur_pack_fill += copy_len;

        cur_len -= copy_len;
        ptr += copy_len;
        carry_len += copy_len;
    }

    g_free(converted_text);
}

static void mirage_cdtext_encoder_generate_size_info (MirageCdTextCoder *self, gint block_number, guint8 **data, gint *len)
{
    const MirageCdTextCoderBlock *block = &self->priv->blocks[block_number];

    CDTextSizeInfo *size_info = g_new0(CDTextSizeInfo, 1);

    size_info->charset = block->charset; /* Character set */
    size_info->first_track = block->first_track; /* First track */
    size_info->last_track = block->last_track; /* Last track */
    size_info->copyright = block->copyright; /* Copyright */

    /* Pack count */
    for (guint i = 0; i < G_N_ELEMENTS(size_info->pack_count); i++) {
        size_info->pack_count[i] = block->pack_count[i];
    }

    /* Last sequence number and language code for all blocks */
    for (guint i = 0; i < G_N_ELEMENTS(self->priv->blocks); i++) {
        block = &self->priv->blocks[i];

        /* Set only if we have at least one pack for that language */
        if (block->seq_count > 0) {
            size_info->last_seqnum[i] = block->seq_count - 1; /* count -> last number/index */
            size_info->language_codes[i] = block->code;
        }
    }

    *data = (guint8 *)size_info;
    *len = sizeof(CDTextSizeInfo);
}

static void mirage_cdtext_decoder_read_size_info (MirageCdTextCoder *self G_GNUC_UNUSED, const CDTextEncodedPack *size_info_pack, CDTextSizeInfo **data)
{
    guint8 *size_info = g_malloc0(sizeof(CDTextSizeInfo));
    const CDTextEncodedPack *cur_pack = size_info_pack;

    for (guint i = 0; i < sizeof(CDTextSizeInfo)/12; i++) {
        memcpy(size_info + 12*i, cur_pack->data, 12);
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
 * Initializes CD-TEXT encoder.
 */
void mirage_cdtext_encoder_init (MirageCdTextCoder *self, guint8 *buffer, gint buflen)
{
    /* Cleanup old data */
    mirage_cdtext_coder_cleanup(self);

    /* Set new buffer */
    self->priv->buffer = buffer;
    self->priv->buflen = buflen;

    self->priv->cur_pack = (CDTextEncodedPack *)self->priv->buffer;
}

/**
 * mirage_cdtext_encoder_set_block_info:
 * @self: a #MirageCdTextCoder
 * @block_number: (in): block number
 * @code: (in): language code
 * @charset: (in): character set
 * @copyright: (in): copyright flag
 * @error: (out) (optional): location to store error, or %NULL
 *
 * Sets block information for CD-TEXT block specified by @block_number.
 * @block_number must be a valid block number (0-7). @code is the language
 * code that is to be assigned to the block (e.g. 9 for English), @charset
 * denotes character set that is used within the block, and @copyright
 * is the copyright flag for the block.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_cdtext_encoder_set_block_info (MirageCdTextCoder *self, gint block_number, gint code, gint charset, gint copyright, GError **error)
{
    /* Verify that block number is valid */
    if ((guint)block_number >= G_N_ELEMENTS(self->priv->blocks)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_CDTEXT, "%s: invalid block (%i)!", __debug__, block_number);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LANGUAGE_ERROR, Q_("Invalid block number #%i!"), block_number);
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_CDTEXT, "%s: initialized block %i; code: %i; charset: %i; copyright: %i", __debug__, block_number, code, charset, copyright);

    MirageCdTextCoderBlock *block = &self->priv->blocks[block_number];

    block->code = code;
    block->charset = charset;
    block->copyright = copyright;

    return TRUE;
}

/**
 * mirage_cdtext_encoder_add_data:
 * @self: a #MirageCdTextCoder
 * @code: (in): language code
 * @type: (in): data type
 * @track: (in): track number
 * @data: (in) (array length=data_len): data
 * @data_len: (in): data length
 *
 * Adds data to the encoder. @code is language code of the block the data
 * should be added to. @type denotes pack type and should be one of #MirageLanguagePackType.
 * @track is track number the data belongs to, or 0 if data is global (belongs to disc/session).
 * @data is buffer containing data to be added, and @data_len is length of data in the buffer.
 *
 * This function does not perform any encoding yet; it merely adds the data into
 * encoder's internal representation of CD-TEXT block.
 *
 * <note>
 * Block needs to have its information set with mirage_cdtext_encoder_set_block_info()
 * before data can be added to it.
 * </note>
 */
void mirage_cdtext_encoder_add_data (MirageCdTextCoder *self, gint code, gint type, gint track, const guint8 *data, gint data_len)
{
    CDTextDecodedPack *pack_data;
    gint block_number = mirage_cdtext_coder_lang2block(self, code); /* language code -> block number */

    MirageCdTextCoderBlock *block = &self->priv->blocks[block_number];

    /* Create pack structure */
    pack_data = g_new0(CDTextDecodedPack, 1);
    pack_data->block_number = block_number;
    pack_data->pack_type = type;
    pack_data->track_number = track;
#if GLIB_CHECK_VERSION(2, 68, 0)
    pack_data->data = g_memdup2(data, data_len);
#else
    pack_data->data = g_memdup(data, data_len);
#endif
    pack_data->data_len = data_len;

    /* Add internal representation to ordered list... */
    block->packs_list = g_list_insert_sorted(block->packs_list, pack_data, (GCompareFunc)sort_pack_data);

    /* Set the number of first track that has language... this is not
     * very reliable, but I do believe all tracks from now on are
     * required by specs to have language block? */
    if (!block->first_track) {
        block->first_track = track;
    }
    /* Keep setting the last track... */
    block->last_track = track;
}

/**
 * mirage_cdtext_encoder_encode:
 * @self: a #MirageCdTextCoder
 * @buffer: (out) (array length=buflen): location to store buffer
 * @buflen: (out): location to store buffer length
 *
 * Encodes the CD-TEXT data. Pointer to buffer containing the encoded data is
 * stored in @buffer, and length of data in buffer is stored in @buflen.
 *
 * Note that @buffer is the same as the argument passed to mirage_cdtext_encoder_init().
 */
void mirage_cdtext_encoder_encode (MirageCdTextCoder *self, guint8 **buffer, gint *buflen)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_CDTEXT, "%s: encoding CD-TEXT...", __debug__);

    /* Encode all blocks */
    for (guint i = 0; i < G_N_ELEMENTS(self->priv->blocks); i++) {
        const MirageCdTextCoderBlock *block = &self->priv->blocks[i];

        /* Block is valid only if it has language code set */
        if (block->code) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_CDTEXT, "%s: encoding block %i; code %i", __debug__, i, block->code);

            /* Encode all on list */
            for (GList *entry = block->packs_list; entry; entry = entry->next) {
                mirage_cdtext_encoder_encode_pack(self, entry->data);
            }

            /* We need to 'reserve' some space for size info */
            CDTextDecodedPack pack_data;
            CDTextSizeInfo size_info;
            memset(&size_info, 0, sizeof(size_info));

            pack_data.block_number = i;
            pack_data.pack_type = 0x8F;
            pack_data.track_number = 0;
            pack_data.data = (guint8 *)&size_info;
            pack_data.data_len = sizeof(size_info);

            mirage_cdtext_encoder_encode_pack(self, &pack_data);
        } else {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_CDTEXT, "%s: block %i not valid", __debug__, i);
        }
    }

    /* Now that all the packs have been encoded, we can generate size info */
    for (guint i = 0; i < G_N_ELEMENTS(self->priv->blocks); i++) {
        MirageCdTextCoderBlock *block = &self->priv->blocks[i];

        /* Valid language block (i.e., one that is in use) should have
         * the size_info pointer set by mirage_cdtext_encoder_initialize_pack(),
         * which is called by mirage_cdtext_encoder_encode_pack() call
         * with dummy size info pack in the above loop... */
        if (!block->size_info) {
            continue;
        }

        guint8 *size_info;
        gint size_info_len;

        gint old_len;

        /* Generate size info */
        mirage_cdtext_encoder_generate_size_info(self, i, &size_info, &size_info_len);

        /* Write into previously reserved size info packs */
        self->priv->cur_pack = block->size_info;
        self->priv->cur_pack_fill = 0;

        /* Store current length */
        old_len = self->priv->length;

        /* Encode pack */
        CDTextDecodedPack pack_data;
        pack_data.block_number = i;
        pack_data.pack_type = 0x8F;
        pack_data.track_number = 0;
        pack_data.data = size_info;
        pack_data.data_len = size_info_len;

        mirage_cdtext_encoder_encode_pack(self, &pack_data);

        /* Restore length */
        self->priv->length = old_len;

        /* Free size info */
        g_free(size_info);
    }

    /* Generate CRC for all packs */
    self->priv->cur_pack = (CDTextEncodedPack *)self->priv->buffer;
    for (gint i = 0; i < self->priv->length; i++) {
        add_crc_to_pack(self->priv->cur_pack);
        self->priv->cur_pack++;
    }

    *buflen = self->priv->length*sizeof(CDTextEncodedPack);
    *buffer = self->priv->buffer;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_CDTEXT, "%s: done encoding CD-TEXT; length: 0x%X", __debug__, *buflen);
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
 * Initializes CD-TEXT decoder. @buffer is the buffer containing encoded CD-TEXT
 * data and @buflen is length of data in the buffer.
 *
 * This function decodes CD-TEXT data and stores it in decoder's internal representation.
 * Information about decoded CD-TEXT blocks and their data can be obtained via
 * subsequent calls to mirage_cdtext_decoder_get_block_info() and
 * mirage_cdtext_decoder_get_data().
 *
 */
void mirage_cdtext_decoder_init (MirageCdTextCoder *self, guint8 *buffer, gint buflen)
{
    /* Cleanup old data */
    mirage_cdtext_coder_cleanup(self);

    /* Set new buffer */
    self->priv->buffer = buffer;
    self->priv->buflen = buflen;
    self->priv->cur_pack = (CDTextEncodedPack *)self->priv->buffer;
    self->priv->length = buflen/sizeof(CDTextEncodedPack);

    /* Read size info packs */
    gint i = 0;
    while (i < self->priv->length) {
        CDTextEncodedPack *cur_pack = self->priv->cur_pack + i;

        if (cur_pack->pack_type == 0x8F) {
            gint block_number = (cur_pack->block_number & 0x70) >> 4;

            MirageCdTextCoderBlock *block = &self->priv->blocks[block_number];

            block->size_info = cur_pack;

            CDTextSizeInfo *size_info = NULL;
            mirage_cdtext_decoder_read_size_info(self, cur_pack, &size_info);
            block->code = size_info->language_codes[block_number];
            block->charset = size_info->charset;
            block->copyright = size_info->copyright;
            block->first_track = size_info->first_track;
            block->last_track = size_info->last_track;

            block->seq_count = size_info->last_seqnum[block_number] + 1;
            for (guint j = 0; j < G_N_ELEMENTS(block->pack_count); j++) {
                block->pack_count[j] = size_info->pack_count[j];
            }
            g_free(size_info);

            i += 3;
        } else {
            i++;
        }
    }

    /* Now decode... go over all blocks and decode valid ones */
    for (guint block_number = 0; block_number < G_N_ELEMENTS(self->priv->blocks); block_number++) {
        gchar tmp_buffer[256 * 12];
        memset(tmp_buffer, 0xFF, sizeof(tmp_buffer));
        gchar *ptr = tmp_buffer;
        gint tmp_len = 0;
        gint cur_track = 0;

        /* Skip empty blocks */
        MirageCdTextCoderBlock *block = &self->priv->blocks[block_number];
        if (!block->seq_count) {
            continue;
        }

        self->priv->cur_pack = block->size_info - (block->seq_count - 3);

        while (self->priv->cur_pack < block->size_info) {
            if (self->priv->cur_pack->pack_type != (self->priv->cur_pack - 1)->pack_type) {
                memset(tmp_buffer, 0xFF, sizeof(tmp_buffer));
                tmp_len = 0;
                ptr = tmp_buffer;
            }

            /* Current data offset */
            gchar *cur_data = (gchar *)self->priv->cur_pack->data + self->priv->cur_pack_fill;
            /* Length of transfer */
            gint copy_len = MIN(strlen(cur_data) + 1, (size_t)(12 - self->priv->cur_pack_fill));

            /* Copy */
            memcpy(ptr, self->priv->cur_pack->data + self->priv->cur_pack_fill, copy_len);
            /* Do some necesssary calcs */
            self->priv->cur_pack_fill += copy_len;
            ptr += copy_len;
            tmp_len += copy_len;

            /* The way of the lazy programmer (TM)... we set whole string to 0xFF
             * and then wait 'till we get terminating 0 at the end :D (we also need
             * to check whether string doesn't contain just "\0", which keeps getting
             * returned once we hit the padding at the end of a pack types...) */
            if (!tmp_buffer[tmp_len-1] && strlen(tmp_buffer)) {
                /* Pack the data and add it to the list; as simple as that... */
                CDTextDecodedPack *pack_data = g_new0(CDTextDecodedPack, 1);
                pack_data->block_number = block_number;
                pack_data->pack_type = self->priv->cur_pack->pack_type;
                pack_data->track_number = cur_track;

                /* Text in packs 0x80 to 0x85 is encoded using the block's character
                 * set, and we need to convert it to UTF-8 for our internal
                 * representation. The rest of the packs either contain binary data,
                 * or have a fixed encoding (for example, 0x8E is supposed to contain
                 * ASCII text).*/
                if (pack_data->pack_type >= 0x80 && pack_data->pack_type <= 0x85) {
                    GError *error = NULL;
                    gsize bytes_read;
                    gsize bytes_written;

                    /* Source encoding */
                    const gchar *source_encoding = "ISO-8859-1";
                    switch (block->charset) {
                        /* Language codes as per EN 60908:1999 (see also
                         * https://libcdio.github.io/cd-text-format.html). The same
                         * values are also used by MirageLanguageEncoding enum. */
                        case MIRAGE_LANGUAGE_ENCODING_ISO88591: {
                            source_encoding = "ISO-8859-1";
                            break;
                        }
                        case MIRAGE_LANGUAGE_ENCODING_ASCII: {
                            source_encoding = "ASCII";
                            break;
                        }
                        case MIRAGE_LANGUAGE_ENCODING_MS_JIS: {
                            source_encoding = "CP932"; /* cdrdao uses CP932 for MS-JIS */
                            break;
                        }
                        default: {
                            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unsupported charset/encoding code 0x%X; falling back to ISO-8859-1", __debug__, block->charset);
                            break;
                        }
                    }

                    /* Internally, we store data as UTF-8 */
                    const gchar *target_encoding = "UTF-8";

                    gchar *converted_text = g_convert(
                        (const gchar *)tmp_buffer,
                        tmp_len, /* includes terminating NUL */
                        target_encoding,
                        source_encoding,
                        &bytes_read,
                        &bytes_written,
                        &error
                    );

                    if (converted_text) {
                        pack_data->data = (guint8 *)converted_text;
                        pack_data->data_len = bytes_written; /* includes NUL from the input data */
                    } else {
                        /* Conversion failed; display a warning, and use original data... */
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to convert CD-TEXT data (block number: %d, pack type: 0x%02X, track number: %d) from %s to UTF-8: %s", __debug__, pack_data->block_number, pack_data->pack_type, pack_data->track_number, source_encoding, error->message);
                        g_error_free(error);

#if GLIB_CHECK_VERSION(2, 68, 0)
                        pack_data->data = g_memdup2(tmp_buffer, tmp_len);
#else
                        pack_data->data = g_memdup(tmp_buffer, tmp_len);
#endif
                        pack_data->data_len = tmp_len;
                    }
                } else {
                    /* Copy as-is */
#if GLIB_CHECK_VERSION(2, 68, 0)
                    pack_data->data = g_memdup2(tmp_buffer, tmp_len);
#else
                    pack_data->data = g_memdup(tmp_buffer, tmp_len);
#endif
                    pack_data->data_len = tmp_len;
                }

                block->packs_list = g_list_insert_sorted(block->packs_list, pack_data, (GCompareFunc)sort_pack_data);

                /* Clear the temporary buffer */
                memset(tmp_buffer, 0xFF, sizeof(tmp_buffer));
                tmp_len = 0;
                ptr = tmp_buffer;

                /* Increase track number; this is to account for strings too short
                 * to cause switch to next pack, whose track number is consequently
                 * stored nowhere... */
                cur_track++;
            }

            if (self->priv->cur_pack_fill == 12) {
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
 * @block_number: (in): block number
 * @code: (out) (optional): location to store language code, or %NULL
 * @charset: (out) (optional): location to store character set, or %NULL
 * @copyright: (out) (optional): location to store copyright flag, or %NULL
 * @error: (out) (optional): location to store error, or %NULL
 *
 * Retrieves block information for CD-TEXT block specified by @block_number.
 * @block_number must be a valid block number (0-7). Language code assigned
 * to the block is stored in @code, code of character set used within block
 * is stored in @charset and block's copyright flag is stored in @copyright.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_cdtext_decoder_get_block_info (MirageCdTextCoder *self, gint block_number, gint *code, gint *charset, gint *copyright, GError **error)
{
    /* Verify that block number is valid */
    if ((guint)block_number >= G_N_ELEMENTS(self->priv->blocks)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LANGUAGE_ERROR, Q_("Block number %d exceeds number of blocks %d!"), block_number, (gint)G_N_ELEMENTS(self->priv->blocks));
        return FALSE;
    }
    const MirageCdTextCoderBlock *block = &self->priv->blocks[block_number];

    if (!block->code) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LANGUAGE_ERROR, Q_("Requested block %d has no language code set!"), block_number);
        return FALSE;
    }

    if (code) {
        *code = block->code;
    }
    if (charset) {
        *charset = block->charset;
    }
    if (copyright) {
        *copyright = block->copyright;
    }

    return TRUE;
}

/**
 * mirage_cdtext_decoder_get_data:
 * @self: a #MirageCdTextCoder
 * @block_number: (in): block number
 * @callback_func: (in) (scope call) (closure user_data): callback function
 * @user_data: (in) (nullable): data to be passed to callback function
 *
 * Retrieves data for CD-TEXT block specified by @block_number. @block_number
 * must be a valid block number (0-7). It calls @callback_func for every data
 * pack that has been encoded in the block.
 *
 * If @callback_func returns %FALSE, the function immediately returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_cdtext_decoder_get_data (MirageCdTextCoder *self, gint block_number, MirageCdTextDataCallback callback_func, gpointer user_data)
{
    const MirageCdTextCoderBlock *block = &self->priv->blocks[block_number];

    /* Go over the list and call the callback for each entry */
    for(GList *entry = block->packs_list; entry; entry = entry->next) {
        CDTextDecodedPack *pack_data = entry->data;
        gint code = mirage_cdtext_coder_block2lang(self, pack_data->block_number);

        if (!callback_func(code, pack_data->pack_type, pack_data->track_number, pack_data->data, pack_data->data_len, user_data)) {
            return FALSE;
        }
    }

    return TRUE;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
static void mirage_cdtext_coder_init (MirageCdTextCoder *self)
{
    self->priv = mirage_cdtext_coder_get_instance_private(self);

    /* Clear the block data */
    memset(self->priv->blocks, 0, sizeof(self->priv->blocks));
}

static void mirage_cdtext_coder_finalize (GObject *gobject)
{
    MirageCdTextCoder *self = MIRAGE_CDTEXT_CODER(gobject);

    /* Cleanup the data */
    mirage_cdtext_coder_cleanup(self);

    /* Chain up to the parent class */
    G_OBJECT_CLASS(mirage_cdtext_coder_parent_class)->finalize(gobject);
}

static void mirage_cdtext_coder_class_init (MirageCdTextCoderClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = mirage_cdtext_coder_finalize;
}
