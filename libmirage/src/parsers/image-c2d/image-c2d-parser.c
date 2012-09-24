/*
 *  libMirage: C2D image parser: Disc object
 *  Copyright (C) 2008-2012 Henrik Stokseth
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

#include "image-c2d.h"

#define __debug__ "C2D-Parser"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_PARSER_C2D_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_C2D, MirageParser_C2DPrivate))

struct _MirageParser_C2DPrivate
{
    GObject *disc;

    gchar *c2d_filename;
    GObject *c2d_stream;

    C2D_HeaderBlock *header_block;
    C2D_CDTextBlock *cdtext_block;
    C2D_TrackBlock *track_block;

    guint8 *c2d_data;
    gint c2d_data_length;
};


/**********************************************************************\
 *                    Endian-conversion functions                     *
\**********************************************************************/
static inline void c2d_header_block_fix_endian (C2D_HeaderBlock *block)
{
    block->header_size = GUINT16_FROM_LE(block->header_size);
    block->has_upc_ean = GUINT16_FROM_LE(block->has_upc_ean);
    block->num_track_blocks = GUINT16_FROM_LE(block->num_track_blocks);
    block->size_cdtext = GUINT32_FROM_LE(block->size_cdtext);
    block->offset_tracks = GUINT32_FROM_LE(block->offset_tracks);
    block->dummy2 = GUINT32_FROM_LE(block->dummy2);
    block->offset_c2ck = GUINT32_FROM_LE(block->offset_c2ck);
}

static inline void c2d_c2ck_block_fix_endian (C2D_C2CKBlock *block)
{
    block->block_size = GUINT32_FROM_LE(block->block_size);
    block->dummy1[0] = GUINT32_FROM_LE(block->dummy1[0]);
    block->dummy1[1] = GUINT32_FROM_LE(block->dummy1[1]);
    block->next_offset = GUINT64_FROM_LE(block->next_offset);
    block->dummy2[0] = GUINT32_FROM_LE(block->dummy2[0]);
    block->dummy2[1] = GUINT32_FROM_LE(block->dummy2[1]);
}

static inline void c2d_track_block_fix_endian (C2D_TrackBlock *block)
{
    block->block_size = GUINT32_FROM_LE(block->block_size);
    block->first_sector = GUINT32_FROM_LE(block->first_sector);
    block->last_sector = GUINT32_FROM_LE(block->last_sector);
    block->image_offset = GUINT64_FROM_LE(block->image_offset);
    block->sector_size = GUINT32_FROM_LE(block->sector_size);
    block->dummy = GUINT16_FROM_LE(block->dummy);
}

static inline void c2d_z_info_header_fix_endian (C2D_Z_Info_Header *header)
{
    header->dummy = GUINT32_FROM_LE(header->dummy);
}

static inline void c2d_z_info_fix_endian (C2D_Z_Info *block)
{
    block->compressed_size = GUINT32_FROM_LE(block->compressed_size);
    block->image_offset = GUINT64_FROM_LE(block->image_offset);
}

static inline void c2d_c2aw_block_fix_endian (C2D_C2AWBlock *block)
{
    block->block_size = GUINT32_FROM_LE(block->block_size);
    block->info_size = GUINT64_FROM_LE(block->info_size);
    block->next_offset = GUINT64_FROM_LE(block->next_offset);
    block->dummy[0] = GUINT32_FROM_LE(block->dummy[0]);
    block->dummy[1] = GUINT32_FROM_LE(block->dummy[1]);
}

static inline void c2d_wocd_block_fix_endian (C2D_WOCDBlock *block)
{
    block->block_size = GUINT32_FROM_LE(block->block_size);

    for (gint i = 0; i < G_N_ELEMENTS(block->dummy); i++) {
        block->dummy[i] = GUINT32_FROM_LE(block->dummy[i]);
    }
}


/**********************************************************************\
 *                          Parsing functions                         *
\**********************************************************************/
static gint mirage_parser_c2d_convert_track_mode (MirageParser_C2D *self, C2D_Mode mode, guint16 sector_size)
{
    if ((mode == AUDIO) || (mode == AUDIO2)) {
        switch (sector_size) {
            case 2352: {
                return MIRAGE_MODE_AUDIO;
            }
            case 2448: {
                return MIRAGE_MODE_AUDIO;
            }
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown sector size %i!\n", __debug__, sector_size);
                return -1;
            }
        }
    } else if (mode == MODE1) {
        switch (sector_size) {
            case 2048: {
                return MIRAGE_MODE_MODE1;
            }
            case 2448: {
                return MIRAGE_MODE_MODE2_MIXED; /* HACK to support sector size */
            }
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown sector size %i!\n", __debug__, sector_size);
                return -1;
            }
        }
    } else if (mode == MODE2) {
        switch (sector_size) {
            case 2048: {
                return MIRAGE_MODE_MODE2_FORM1;
            }
            case 2324: {
                return MIRAGE_MODE_MODE2_FORM2;
            }
            case 2336: {
                return MIRAGE_MODE_MODE2;
            }
            case 2352: {
                return MIRAGE_MODE_MODE2_MIXED;
            }
            case 2448: {
                return MIRAGE_MODE_MODE2_MIXED;
            }
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown sector size %i!\n", __debug__, sector_size);
                return -1;
            }
        }
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown track mode 0x%X!\n", __debug__, mode);
        return -1;
    }
}

static gboolean mirage_parser_c2d_parse_compressed_track (MirageParser_C2D *self, guint64 offset, GError **error)
{
    gint num = 0;

    C2D_Z_Info_Header header;
    C2D_Z_Info zinfo;

    g_seekable_seek(G_SEEKABLE(self->priv->c2d_stream), offset, G_SEEK_SET, NULL, NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: compression info blocks!\n", __debug__);

    if (g_input_stream_read(G_INPUT_STREAM(self->priv->c2d_stream), &header, sizeof(C2D_Z_Info_Header), NULL, NULL) != sizeof(C2D_Z_Info_Header)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read Z info header!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read Z info header!");
        return FALSE;
    }
    c2d_z_info_header_fix_endian(&header);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: dummy: 0x%X\n", __debug__, header.dummy);

    do {
        if (g_input_stream_read(G_INPUT_STREAM(self->priv->c2d_stream), &zinfo, sizeof(C2D_Z_Info), NULL, NULL) != sizeof(C2D_Z_Info)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read Z info!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read Z info!");
            return FALSE;
        }
        c2d_z_info_fix_endian(&zinfo);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: [%03X] size: 0x%X offset: 0x%X\n", __debug__, num, zinfo.compressed_size, zinfo.image_offset);
        num++;
    } while (zinfo.image_offset);

    /* Not supported yet */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: compressed images not supported yet!\n", __debug__);
    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Compressed images are not supported yet!");

    return FALSE;
}

static gboolean mirage_parser_c2d_parse_track_entries (MirageParser_C2D *self, GError **error)
{
    gint last_session = 0;
    gint last_point = 0;
    gint last_index = 1;

    gint num_tracks = self->priv->header_block->num_track_blocks;
    gint track_start = 0;
    gint track_first_sector = 0;
    gint track_last_sector = 0;
    gboolean new_track = FALSE;

    /* Read track entries */
    for (gint t = 0; t < num_tracks; t++) {
        C2D_TrackBlock *cur_tb = &self->priv->track_block[t];
        c2d_track_block_fix_endian(cur_tb);

        GObject *cur_session = NULL;
        GObject *cur_point = NULL;

        /* Read main blocks related to track */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: TRACK BLOCK %2i:\n", __debug__, t);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   block size: %i\n", __debug__, cur_tb->block_size);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   first sector: %i\n", __debug__, cur_tb->first_sector);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   last sector: %i\n", __debug__, cur_tb->last_sector);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   image offset: 0x%X\n", __debug__, cur_tb->image_offset);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   sector size: %i\n", __debug__, cur_tb->sector_size);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   ISRC: %.12s\n", __debug__, cur_tb->isrc);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   flags: 0x%X\n", __debug__, cur_tb->flags);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   mode: %i\n", __debug__, cur_tb->mode);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   index: %i\n", __debug__, cur_tb->index);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   session: %i\n", __debug__, cur_tb->session);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   point: %i\n", __debug__, cur_tb->point);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   compressed: %i\n\n", __debug__, cur_tb->compressed);

        /* Abort on compressed track data */
        if (cur_tb->compressed) {
            if(!mirage_parser_c2d_parse_compressed_track(self, cur_tb->image_offset, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse compressed track!\n", __debug__);
                return FALSE;
            }
        }

        /* Create a new session? */
        if (cur_tb->session > last_session) {
            GObject *session = g_object_new(MIRAGE_TYPE_SESSION, NULL);
            if (!mirage_disc_add_session_by_number(MIRAGE_DISC(self->priv->disc), cur_tb->session, session, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __debug__);
                g_object_unref(session);
                return FALSE;
            }
            g_object_unref(session);

            last_session = cur_tb->session;
            last_point = 0;
        }

        /* Get current session */
        cur_session = mirage_disc_get_session_by_index(MIRAGE_DISC(self->priv->disc), -1, error);
        if (!cur_session) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current session!\n", __debug__);
            return FALSE;
        }

        /* Add a new track? */
        new_track = FALSE; /* reset */
        if (cur_tb->point > last_point) {
            GObject *track = g_object_new(MIRAGE_TYPE_TRACK, NULL);
            if (!mirage_session_add_track_by_number(MIRAGE_SESSION(cur_session), cur_tb->point, track, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __debug__);
                g_object_unref(track);
                g_object_unref(cur_session);
                return FALSE;
            }
            g_object_unref(track);

            last_point = cur_tb->point;
            last_index = 1;
            new_track = TRUE;
        }

        /* Get current track */
        cur_point = mirage_session_get_track_by_index(MIRAGE_SESSION(cur_session), -1, error);
        if (!cur_point) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current track!\n", __debug__);
            g_object_unref(cur_session);
            return FALSE;
        }

        /* Set track start and calculate track's first and last sector */
        if (new_track) {
            gint n;

            if (cur_tb->point == 1) {
                mirage_track_set_track_start(MIRAGE_TRACK(cur_point), 150);
            }
            track_start = mirage_track_get_track_start(MIRAGE_TRACK(cur_point));

            for (n = 0; n < num_tracks-t; n++) {
                if (cur_tb->point != cur_tb[n].point) break;
            }
            track_first_sector = cur_tb->first_sector;
            track_last_sector = cur_tb[n-1].last_sector;
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   track's first sector: %i, last sector: %i.\n", __debug__, track_first_sector, track_last_sector);
        }

        /* Add new index? */
        if (cur_tb->index > last_index) {
            if (!mirage_track_add_index(MIRAGE_TRACK(cur_point), cur_tb->first_sector - track_first_sector + track_start, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add index!\n", __debug__);
                g_object_unref(cur_point);
                g_object_unref(cur_session);
                return FALSE;
            }
            last_index = cur_tb->index;

            /* If index > 1 then skip making fragments */
            goto skip_making_fragments;
        }

        /* Decode mode */
        gint converted_mode = 0;
        converted_mode = mirage_parser_c2d_convert_track_mode(self, cur_tb->mode, cur_tb->sector_size);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   converted mode: 0x%X\n", __debug__, converted_mode);
        mirage_track_set_mode(MIRAGE_TRACK(cur_point), converted_mode);

        /* Set ISRC */
        if (cur_tb->isrc[0]) {
            mirage_track_set_isrc(MIRAGE_TRACK(cur_point), cur_tb->isrc);
        }

        /* Pregap fragment at the beginning of track */
        if ((cur_tb->point == 1) && (cur_tb->index == 1)) {
            /* Creating NULL fragment should never fail */
            GObject *pregap_fragment = mirage_create_fragment(MIRAGE_TYPE_FRAGMENT_IFACE_NULL, NULL, G_OBJECT(self), error);

            mirage_fragment_set_length(MIRAGE_FRAGMENT(pregap_fragment), 150);

            mirage_track_add_fragment(MIRAGE_TRACK(cur_point), -1, pregap_fragment);
            g_object_unref(pregap_fragment);
        }

        /* Data fragment */
        GObject *data_fragment = mirage_create_fragment(MIRAGE_TYPE_FRAGMENT_IFACE_BINARY, self->priv->c2d_stream, G_OBJECT(self), error);
        if (!data_fragment) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to create fragment!\n", __debug__);
            g_object_unref(cur_point);
            g_object_unref(cur_session);
            return FALSE;
        }

        /* Prepare data fragment */
        guint64 tfile_offset = cur_tb->image_offset;
        gint tfile_sectsize = cur_tb->sector_size;
        gint tfile_format = 0;

        if (converted_mode == MIRAGE_MODE_AUDIO) {
            tfile_format = MIRAGE_TFILE_AUDIO;
        } else {
            tfile_format = MIRAGE_TFILE_DATA;
        }

        gint fragment_len = track_last_sector - track_first_sector + 1;
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   fragment length: %i\n", __debug__, fragment_len);

        mirage_fragment_set_length(MIRAGE_FRAGMENT(data_fragment), fragment_len);

        /* Set file */
        if(!mirage_fragment_iface_binary_track_file_set_file(MIRAGE_FRAGMENT_IFACE_BINARY(data_fragment), self->priv->c2d_filename, self->priv->c2d_stream, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set track data file!\n", __debug__);
            g_object_unref(data_fragment);
            g_object_unref(cur_point);
            g_object_unref(cur_session);
            return FALSE;
        }
        mirage_fragment_iface_binary_track_file_set_offset(MIRAGE_FRAGMENT_IFACE_BINARY(data_fragment), tfile_offset);
        mirage_fragment_iface_binary_track_file_set_sectsize(MIRAGE_FRAGMENT_IFACE_BINARY(data_fragment), tfile_sectsize);
        mirage_fragment_iface_binary_track_file_set_format(MIRAGE_FRAGMENT_IFACE_BINARY(data_fragment), tfile_format);

        /* Subchannel */
        switch (cur_tb->sector_size) {
            case 2448: {
                gint sfile_sectsize = 96;
                gint sfile_format = MIRAGE_SFILE_PW96_INT | MIRAGE_SFILE_INT;

                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   subchannel found; interleaved PW96\n", __debug__);

                mirage_fragment_iface_binary_subchannel_file_set_sectsize(MIRAGE_FRAGMENT_IFACE_BINARY(data_fragment), sfile_sectsize);
                mirage_fragment_iface_binary_subchannel_file_set_format(MIRAGE_FRAGMENT_IFACE_BINARY(data_fragment), sfile_format);

                /* We need to correct the data for track sector size...
                   C2D format has already added 96 bytes to sector size,
                   so we need to subtract it */
                tfile_sectsize = cur_tb->sector_size - sfile_sectsize;
                mirage_fragment_iface_binary_track_file_set_sectsize(MIRAGE_FRAGMENT_IFACE_BINARY(data_fragment), tfile_sectsize);

                break;
            }
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   no subchannel\n", __debug__);
                break;
            }
        }

        mirage_track_add_fragment(MIRAGE_TRACK(cur_point), -1, data_fragment);
        g_object_unref(data_fragment);

skip_making_fragments:
        g_object_unref(cur_point);
        g_object_unref(cur_session);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    }

    return TRUE;
}

static gboolean mirage_parser_c2d_load_cdtext (MirageParser_C2D *self, GError **error)
{
    GObject *session;
    guint8 *cdtext_data = (guint8 *)self->priv->cdtext_block;
    gint cdtext_length = self->priv->header_block->size_cdtext;

    /* Get session */
    session = mirage_disc_get_session_by_index(MIRAGE_DISC(self->priv->disc), 0, error);
    if (!session) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get session!\n", __debug__);
        return FALSE;
    }

    /* Set CD-TEXT data */
    if (!mirage_session_set_cdtext_data(MIRAGE_SESSION(session), cdtext_data, cdtext_length, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set CD-TEXT data!\n", __debug__);
        g_object_unref(session);
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: loaded a total of %i CD-TEXT blocks.\n", __debug__, cdtext_length / sizeof(C2D_CDTextBlock));
    g_assert(cdtext_length % sizeof(C2D_CDTextBlock) == 0);

    g_object_unref(session);

    return TRUE;
}

static gboolean mirage_parser_c2d_load_disc (MirageParser_C2D *self, GError **error)
{
    C2D_HeaderBlock *hb;

    /* Init some block pointers */
    self->priv->header_block = (C2D_HeaderBlock *)self->priv->c2d_data;
    c2d_header_block_fix_endian(self->priv->header_block);
    hb = self->priv->header_block;

    self->priv->track_block = (C2D_TrackBlock *)(self->priv->c2d_data + hb->offset_tracks);
    /* Note: this is pointer to first of the blocks; we fix endianess later, for each block, when needed */

    self->priv->cdtext_block = (C2D_CDTextBlock *)(self->priv->c2d_data + hb->header_size);
    /* Note: no multi-byte data in block, so endianess doesn't need to be fixed */

    /* Print some info from the header */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: HEADER:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Signature: %.32s\n", __debug__, hb->signature);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Length of header: %i\n", __debug__, hb->header_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Has UPC / EAN: %i\n", __debug__, hb->has_upc_ean);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Track blocks: %i\n", __debug__, hb->num_track_blocks);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Size of CD-Text block: %i\n", __debug__, hb->size_cdtext);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Offset to track blocks: 0x%X\n", __debug__, hb->offset_tracks);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Offset to C2CK block: 0x%X\n", __debug__, hb->offset_c2ck);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Description: %.80s\n", __debug__, hb->description);
#if 1
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Dummy1: 0x%X\n", __debug__, hb->dummy1);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Dummy2: 0x%X\n", __debug__, hb->dummy2);
#endif

    /* Set disc's MCN */
    if (hb->has_upc_ean) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   UPC / EAN: %.13s\n", __debug__, &hb->upc_ean);
        mirage_disc_set_mcn(MIRAGE_DISC(self->priv->disc), hb->upc_ean);
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, ":\n");

    /* For now we only support (and assume) CD media */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD-ROM image\n\n", __debug__);
    mirage_disc_set_medium_type(MIRAGE_DISC(self->priv->disc), MIRAGE_MEDIUM_CD);

    /* CD-ROMs start at -150 as per Red Book... */
    mirage_disc_layout_set_start_sector(MIRAGE_DISC(self->priv->disc), -150);

    /* Load tracks */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing track entries...\n", __debug__);
    if (!mirage_parser_c2d_parse_track_entries(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse track entries!\n", __debug__);
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished parsing track entries\n\n", __debug__);

    /* Load CD-Text */
    if (hb->size_cdtext) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing CD-TEXT...\n", __debug__);
        if(!mirage_parser_c2d_load_cdtext(self, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse CD-EXT!\n", __debug__);
            return FALSE;
        }
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished parsing CD-TEXT\n\n", __debug__);
    }

    return TRUE;
}

/**********************************************************************\
 *                 MirageParser methods implementation               *
\**********************************************************************/
static GObject *mirage_parser_c2d_load_image (MirageParser *_self, gchar **filenames, GError **error)
{
    MirageParser_C2D *self = MIRAGE_PARSER_C2D(_self);

    gboolean succeeded = TRUE;
    gchar sig[32] = "";

    /* Open file */
    self->priv->c2d_stream = mirage_create_file_stream(filenames[0], G_OBJECT(self), error);
    if (!self->priv->c2d_stream) {
        return FALSE;
    }

    /* Read signature */
    if (g_input_stream_read(G_INPUT_STREAM(self->priv->c2d_stream), sig, sizeof(sig), NULL, NULL) != sizeof(sig)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read signature!");
        return FALSE;
    }

    if (memcmp(sig, C2D_SIGNATURE_1, sizeof(C2D_SIGNATURE_1) - 1)
        && memcmp(sig, C2D_SIGNATURE_2, sizeof(C2D_SIGNATURE_2) - 1)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Parser cannot handle given image!");
        return FALSE;
    }


    /* Reset position */
    g_seekable_seek(G_SEEKABLE(self->priv->c2d_stream), 0, G_SEEK_SET, NULL, NULL);

    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_attach_child(MIRAGE_OBJECT(self), self->priv->disc);

    mirage_disc_set_filename(MIRAGE_DISC(self->priv->disc), filenames[0]);
    self->priv->c2d_filename = g_strdup(filenames[0]);

    /* Load image header */
    self->priv->c2d_data = g_try_malloc(sizeof(C2D_HeaderBlock));
    if (!self->priv->c2d_data) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to allocate memory for header (%d)!\n", __debug__, sizeof(C2D_HeaderBlock));
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Failed to allocate memory for header (%ld bytes)!", sizeof(C2D_HeaderBlock));
        succeeded = FALSE;
        goto end;
    }

    if (g_input_stream_read(G_INPUT_STREAM(self->priv->c2d_stream), self->priv->c2d_data, sizeof(C2D_HeaderBlock), NULL, NULL) != sizeof(C2D_HeaderBlock)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read header!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read header!");
        succeeded = FALSE;
        goto end;
    }
    self->priv->header_block = (C2D_HeaderBlock *)self->priv->c2d_data;
    c2d_header_block_fix_endian(self->priv->header_block);

    /* Calculate length of image descriptor data */
    self->priv->c2d_data_length = self->priv->header_block->offset_tracks + self->priv->header_block->num_track_blocks * sizeof(C2D_TrackBlock);

    /* Load image descriptor data */
    self->priv->c2d_data = g_try_realloc(self->priv->c2d_data, self->priv->c2d_data_length);
    if (!self->priv->c2d_data) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to allocate memory for descriptor (%d)!\n", __debug__, self->priv->c2d_data_length);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Failed to allocate memory for descriptor (%d)!", self->priv->c2d_data_length);
        succeeded = FALSE;
        goto end;
    }

    g_seekable_seek(G_SEEKABLE(self->priv->c2d_stream), 0, G_SEEK_SET, NULL, NULL);
    if (g_input_stream_read(G_INPUT_STREAM(self->priv->c2d_stream), self->priv->c2d_data, self->priv->c2d_data_length, NULL, NULL) != self->priv->c2d_data_length) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read descriptor!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read descriptor!");
        succeeded = FALSE;
        goto end;
    }

    /* Load disc */
    succeeded = mirage_parser_c2d_load_disc(self, error);

end:
    /* Return disc */
    mirage_object_detach_child(MIRAGE_OBJECT(self), self->priv->disc);
    if (succeeded) {
        return self->priv->disc;
    } else {
        g_object_unref(self->priv->disc);
        return NULL;
    }
}


/******************************************************************************\
 *                                Object init                                 *
\******************************************************************************/
G_DEFINE_DYNAMIC_TYPE(MirageParser_C2D, mirage_parser_c2d, MIRAGE_TYPE_PARSER);

void mirage_parser_c2d_type_register (GTypeModule *type_module)
{
    return mirage_parser_c2d_register_type(type_module);
}

static void mirage_parser_c2d_init (MirageParser_C2D *self)
{
    self->priv = MIRAGE_PARSER_C2D_GET_PRIVATE(self);

    mirage_parser_generate_parser_info(MIRAGE_PARSER(self),
        "PARSER-C2D",
        "C2D Image Parser",
        "C2D (CeQuadrat WinOnCD) images",
        "application/x-c2d"
    );

    self->priv->c2d_filename = NULL;
    self->priv->c2d_stream = NULL;
    self->priv->c2d_data = NULL;
}

static void mirage_parser_c2d_dispose (GObject *gobject)
{
    MirageParser_C2D *self = MIRAGE_PARSER_C2D(gobject);

    if (self->priv->c2d_stream) {
        g_object_unref(self->priv->c2d_stream);
        self->priv->c2d_stream = NULL;
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_parser_c2d_parent_class)->dispose(gobject);
}

static void mirage_parser_c2d_finalize (GObject *gobject)
{
    MirageParser_C2D *self = MIRAGE_PARSER_C2D(gobject);

    g_free(self->priv->c2d_filename);
    g_free(self->priv->c2d_data);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_parser_c2d_parent_class)->finalize(gobject);
}

static void mirage_parser_c2d_class_init (MirageParser_C2DClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageParserClass *parser_class = MIRAGE_PARSER_CLASS(klass);

    gobject_class->dispose = mirage_parser_c2d_dispose;
    gobject_class->finalize = mirage_parser_c2d_finalize;

    parser_class->load_image = mirage_parser_c2d_load_image;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageParser_C2DPrivate));
}

static void mirage_parser_c2d_class_finalize (MirageParser_C2DClass *klass G_GNUC_UNUSED)
{
}
