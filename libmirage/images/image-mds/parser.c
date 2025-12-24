/*
 *  libMirage: MDS image: parser
 *  Copyright (C) 2006-2014 Rok Mandeljc
 *
 *  Reverse-engineering work in March, 2005 by Henrik Stokseth.
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

#include "image-mds.h"

#define __debug__ "MDS-Parser"


static const guint8 mds_signature[17] = { 'M', 'E', 'D', 'I', 'A', ' ', 'D', 'E', 'S', 'C', 'R', 'I', 'P', 'T', 'O', 'R', 0x01 };


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
struct _MirageParserMdsPrivate
{
    MirageDisc *disc;

    MDS_Header *header;

    gint32 prev_session_end;

    const gchar *mds_filename;
    guint64 mds_length;
    guint8 *mds_data;
};


/**********************************************************************\
 *                    Endian-conversion functions                     *
\**********************************************************************/
static inline void mds_header_fix_endian (MDS_Header *header)
{
    header->medium_type = GUINT16_FROM_LE(header->medium_type);
    header->num_sessions = GUINT16_FROM_LE(header->num_sessions);

    for (guint i = 0; i < G_N_ELEMENTS(header->__dummy1__); i++) {
        header->__dummy1__[i] = GUINT16_FROM_LE(header->__dummy1__[i]);
    }

    header->bca_len = GUINT16_FROM_LE(header->bca_len);

    for (guint i = 0; i < G_N_ELEMENTS(header->__dummy2__); i++) {
        header->__dummy2__[i] = GUINT32_FROM_LE(header->__dummy2__[i]);
    }

    header->bca_data_offset = GUINT32_FROM_LE(header->bca_data_offset);

    for (guint i = 0; i < G_N_ELEMENTS(header->__dummy3__); i++) {
        header->__dummy3__[i] = GUINT32_FROM_LE(header->__dummy3__[i]);
    }

    header->disc_structures_offset = GUINT32_FROM_LE(header->disc_structures_offset);

    for (guint i = 0; i < G_N_ELEMENTS(header->__dummy4__); i++) {
        header->__dummy4__[i] = GUINT32_FROM_LE(header->__dummy4__[i]);
    }

    header->sessions_blocks_offset = GUINT32_FROM_LE(header->sessions_blocks_offset);
    header->dpm_blocks_offset = GUINT32_FROM_LE(header->dpm_blocks_offset);
}

static inline void mds_session_block_fix_endian (MDS_SessionBlock *block)
{
    block->session_start = GINT32_FROM_LE(block->session_start);
    block->session_end = GINT32_FROM_LE(block->session_end);
    block->session_number = GUINT16_FROM_LE(block->session_number);
    block->first_track = GUINT16_FROM_LE(block->first_track);
    block->last_track = GUINT16_FROM_LE(block->last_track);
    block->__dummy1__ = GUINT32_FROM_LE(block->__dummy1__);
    block->tracks_blocks_offset = GUINT32_FROM_LE(block->tracks_blocks_offset);
}

static inline void mds_track_block_fix_endian (MDS_TrackBlock *block)
{
    block->extra_offset = GUINT32_FROM_LE(block->extra_offset);
    block->sector_size = GUINT16_FROM_LE(block->sector_size);

    block->start_sector = GUINT32_FROM_LE(block->start_sector);
    block->start_offset = GUINT64_FROM_LE(block->start_offset);
    block->number_of_files = GUINT32_FROM_LE(block->number_of_files);
    block->footer_offset = GUINT32_FROM_LE(block->footer_offset);
}

static inline void mds_track_extra_block_fix_endian (MDS_TrackExtraBlock *block)
{
    block->pregap = GUINT32_FROM_LE(block->pregap);
    block->length = GUINT32_FROM_LE(block->length);
}

static inline void mds_footer_fix_endian (MDS_Footer *block)
{
    block->filename_offset = GUINT32_FROM_LE(block->filename_offset);
    block->widechar_filename = GUINT32_FROM_LE(block->widechar_filename);
    block->__dummy1__ = GUINT32_FROM_LE(block->__dummy1__);
    block->__dummy2__ = GUINT32_FROM_LE(block->__dummy2__);
}

static inline void dpm_data_fix_endian (guint32 *dpm_data, guint32 num_entries)
{
    for (guint i = 0; i < num_entries; i++) {
        dpm_data[i] = GUINT32_FROM_LE(dpm_data[i]);
    }
}

static inline void widechar_filename_fix_endian (gunichar2 *filename)
{
    for (guint i = 0; ; i++) {
        filename[i] = GUINT16_FROM_LE(filename[i]);
        if (!filename[i]) {
            break;
        }
    }
}


/**********************************************************************\
 *                         Parsing functions                          *
\**********************************************************************/
/*
    I hexedited the track mode field with various values and fed it to Alchohol;
    it seemed that high part of byte had no effect at all; only the lower one
    affected the mode, in the following manner:
    00: Mode 2, 01: Audio, 02: Mode 1, 03: Mode 2, 04: Mode 2 Form 1, 05: Mode 2 Form 2, 06: UKNONOWN, 07: Mode 2
    08: Mode 2, 09: Audio, 0A: Mode 1, 0B: Mode 2, 0C: Mode 2 Form 1, 0D: Mode 2 Form 2, 0E: UKNONOWN, 0F: Mode 2
*/
static gint mirage_parser_mds_convert_track_mode (MirageParserMds *self, gint mode)
{
    /* convert between two values */
    static const struct {
        gint mds_mode;
        gint mirage_mode;
    } modes[] = {
        {0x00, MIRAGE_SECTOR_MODE2},
        {0x01, MIRAGE_SECTOR_AUDIO},
        {0x02, MIRAGE_SECTOR_MODE1},
        {0x03, MIRAGE_SECTOR_MODE2},
        {0x04, MIRAGE_SECTOR_MODE2_FORM1},
        {0x05, MIRAGE_SECTOR_MODE2_FORM2},
        /*{0x06, MIRAGE_SECTOR_UNKNOWN},*/
        {0x07, MIRAGE_SECTOR_MODE2},
    };

    /* Basically, do the test twice; once for value, and once for value + 8 */
    for (guint i = 0; i < G_N_ELEMENTS(modes); i++) {
        if (((mode & 0x0F) == modes[i].mds_mode)
            || ((mode & 0x0F) == modes[i].mds_mode + 8)) {
            return modes[i].mirage_mode;
        }
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown track mode 0x%X!\n", __debug__, mode);
    return -1;
}


static gchar *__helper_find_binary_file (const gchar *declared_filename, const gchar *mds_filename)
{
    gchar *bin_filename;
    gchar *bin_fullpath;

    /* Is the filename in form of '*.mdf'? */
    GRegex *ext_regex = g_regex_new("\\*\\.(?<ext>\\w+)", 0, 0, NULL);
    GMatchInfo *match_info = NULL;
    if (g_regex_match(ext_regex, declared_filename, 0, &match_info)) {
        /* Replace the extension in mds_filename */
        gchar *ext = g_match_info_fetch_named(match_info, "ext");
        GRegex *mds_regex = g_regex_new("(?<ext>\\w+)$", 0, 0, NULL);
        bin_filename = g_regex_replace(mds_regex, mds_filename, -1, 0, ext, 0, NULL);

        g_regex_unref(mds_regex);
        g_free(ext);
    } else {
        bin_filename = g_strdup(declared_filename);
    }
    g_match_info_free(match_info);
    g_regex_unref(ext_regex);

    bin_fullpath = mirage_helper_find_data_file(bin_filename, mds_filename);
    g_free(bin_filename);

    return bin_fullpath;
}

static void mirage_parser_mds_parse_dpm_block (MirageParserMds *self, guint32 dpm_block_offset)
{
    guint8 *cur_ptr;

    guint32 dpm_block_number;
    guint32 dpm_start_sector;
    guint32 dpm_resolution;
    guint32 dpm_num_entries;

    guint32 *dpm_data;

    cur_ptr = self->priv->mds_data + dpm_block_offset;

    /* DPM information */
    dpm_block_number = GUINT32_FROM_LE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
    cur_ptr += sizeof(guint32);

    dpm_start_sector = GUINT32_FROM_LE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
    cur_ptr += sizeof(guint32);

    dpm_resolution = GUINT32_FROM_LE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
    cur_ptr += sizeof(guint32);

    dpm_num_entries = GUINT32_FROM_LE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
    cur_ptr += sizeof(guint32);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: block number: %d\n", __debug__, dpm_block_number);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: start sector: 0x%X\n", __debug__, dpm_start_sector);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: resolution: %d\n", __debug__, dpm_resolution);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of entries: %d\n", __debug__, dpm_num_entries);

    /* Read all entries */
    dpm_data = MIRAGE_CAST_PTR(cur_ptr, 0, guint32 *);

    dpm_data_fix_endian(dpm_data, dpm_num_entries);

    /* Set DPM data */
    mirage_disc_set_dpm_data(self->priv->disc, dpm_start_sector, dpm_resolution, dpm_num_entries, dpm_data);
}

static void mirage_parser_mds_parse_dpm_data (MirageParserMds *self)
{
    guint8 *cur_ptr;

    guint32 num_dpm_blocks;
    guint32 *dpm_block_offset;

    if (!self->priv->header->dpm_blocks_offset) {
        /* No DPM data, nothing to do */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: no DPM data\n", __debug__);
        return;
    }

    cur_ptr = self->priv->mds_data + self->priv->header->dpm_blocks_offset;

    /* It would seem the first field is number of DPM data sets, followed by
       appropriate number of offsets for those data sets */
    num_dpm_blocks = GUINT32_FROM_LE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
    cur_ptr += sizeof(guint32);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of DPM data blocks: %d\n", __debug__, num_dpm_blocks);

    dpm_block_offset = MIRAGE_CAST_PTR(cur_ptr, 0, guint32 *);

    if (num_dpm_blocks > 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: cannot correctly handle more than 1 DPM block yet!\n", __debug__);
    }

    /* Read each block */
    for (guint i = 0; i < num_dpm_blocks; i++) {
        dpm_block_offset[i] = GUINT32_FROM_LE(dpm_block_offset[i]);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: block[%i]: offset: 0x%X\n", __debug__, i, dpm_block_offset[i]);
        mirage_parser_mds_parse_dpm_block(self, dpm_block_offset[i]);
        /* FIXME: currently, only first DPM block is loaded */
        break;
    }
}

static void mirage_parser_mds_parse_disc_structures (MirageParserMds *self)
{
    guint8 *cur_ptr;

    /* *** Disc structures *** */
    /* Disc structures: in lead-in areas of DVD and BD discs there are several
       control structures that store various information about the media. There
       are various formats defined in MMC-3 for these structures, and they are
       retrieved from disc using READ DISC STRUCTURE command. Of all the structures,
       MDS format seems to store only three types:
        - 0x0001: DVD copyright information (4 bytes)
        - 0x0004: DVD manufacturing information (2048 bytes)
        - 0x0000: Physical format information (2048 bytes)
       They are stored in that order, taking up 4100 bytes. If disc is dual-layer,
       data consists of 8200 bytes, containing afore-mentioned sequence for each
       layer. */
    if (self->priv->header->disc_structures_offset) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading disc structures\n", __debug__);

        cur_ptr = self->priv->mds_data + self->priv->header->disc_structures_offset;

        /* 0x0001: DVD copyright information */
        mirage_disc_set_disc_structure(self->priv->disc, 0, 0x0001, cur_ptr, 4);
        cur_ptr += 4;

        /* 0x0004: DVD manufacturing information */
        mirage_disc_set_disc_structure(self->priv->disc, 0, 0x0004, cur_ptr, 2048);
        cur_ptr += 2048;

        /* 0x0000: Physical information */
        int num_layers = (cur_ptr[2] & 0x60) >> 5; /* Bits 5 and 6 of byte 2 comprise num_layers field */
        if (num_layers == 0x01) {
            num_layers = 2; /* field value 01b specifies 2 layers */
        } else {
            num_layers = 1; /* field value 00b specifies 1 layer */
        }

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of layers: %d\n", __debug__, num_layers);

        mirage_disc_set_disc_structure(self->priv->disc, 0, 0x0000, cur_ptr, 2048);
        cur_ptr += 2048;

        /* Second round if it's dual-layer... */
        if (num_layers == 2) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: dual-layer disc; reading disc structures for second layer\n", __debug__);

            /* 0x0001: DVD copyright information */
            mirage_disc_set_disc_structure(self->priv->disc, 1, 0x0001, cur_ptr, 4);
            cur_ptr += 4;

            /* 0x0004: DVD manufacturing information */
            mirage_disc_set_disc_structure(self->priv->disc, 1, 0x0004, cur_ptr, 2048);
            cur_ptr += 2048;

            /* 0x0000: Physical information */
            mirage_disc_set_disc_structure(self->priv->disc, 1, 0x0000, cur_ptr, 2048);
            cur_ptr += 2048;
        }
    }
}

static void mirage_parser_mds_parse_bca (MirageParserMds *self)
{
    guint8 *cur_ptr;

    /* It seems BCA (Burst Cutting Area) structure is stored as well, but in separate
       place (kinda makes sense, because it doesn't have fixed length) */
    if (self->priv->header->bca_len) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading BCA data (0x%X bytes)\n", __debug__, self->priv->header->bca_len);

        cur_ptr = self->priv->mds_data + self->priv->header->bca_data_offset;
        mirage_disc_set_disc_structure(self->priv->disc, 0, 0x0003, MIRAGE_CAST_PTR(cur_ptr, 0, guint8 *), self->priv->header->bca_len);
    }
}

static gchar *mirage_parser_mds_get_track_filename (MirageParserMds *self, MDS_Footer *footer_block, GError **error)
{
    gchar *tmp_mdf_filename;
    gchar *mdf_filename;

    /* Track file: it seems all tracks have the same extra block, and that
       filename is located at the end of it... meaning filename's length is
       from filename_offset to end of the file */
    if (!footer_block) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: track block does not have a footer, but we're supposed to get filename from it!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Track block does not have a footer!"));
        return NULL;
    }

    /* If footer_block->widechar_filename is set, filename is stored using 16-bit
       (wide) characters, otherwise 8-bit characters are used. */
    if (footer_block->widechar_filename) {
        gunichar2 *tmp_ptr = MIRAGE_CAST_PTR(self->priv->mds_data, footer_block->filename_offset, gunichar2 *);
        widechar_filename_fix_endian(tmp_ptr);
        tmp_mdf_filename = g_utf16_to_utf8(tmp_ptr, -1, NULL, NULL, NULL);
    } else {
        gchar *tmp_ptr = MIRAGE_CAST_PTR(self->priv->mds_data, footer_block->filename_offset, gchar *);
        tmp_mdf_filename = g_strdup(tmp_ptr);
    }

    /* Find binary file */
    mdf_filename = __helper_find_binary_file(tmp_mdf_filename, self->priv->mds_filename);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: MDF filename: <%s> -> <%s>\n", __debug__, tmp_mdf_filename, mdf_filename);
    g_free(tmp_mdf_filename);

    if (!mdf_filename) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to find data file!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DATA_FILE_ERROR, Q_("Failed to find data file!"));
        return NULL;
    }

    return mdf_filename;
}

static gboolean mirage_parser_mds_parse_track_entries (MirageParserMds *self, MDS_SessionBlock *session_block, GError **error)
{
    MirageSession *session;
    guint8 *cur_ptr;
    gint medium_type;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading track blocks\n", __debug__);

    /* Fetch medium type which we'll need later */
    medium_type = mirage_disc_get_medium_type(self->priv->disc);

    /* Get current session */
    session = mirage_disc_get_session_by_index(self->priv->disc, -1, error);
    if (!session) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current session!\n", __debug__);
        return FALSE;
    }

    cur_ptr = self->priv->mds_data + session_block->tracks_blocks_offset;

    /* Read track entries */
    for (guint i = 0; i < session_block->num_all_blocks; i++) {
        MDS_TrackBlock *block;
        MDS_TrackExtraBlock *extra_block = NULL;
        MDS_Footer *footer_block = NULL;

        /* Read main track block */
        block = MIRAGE_CAST_PTR(cur_ptr, 0, MDS_TrackBlock *);
        mds_track_block_fix_endian(block);
        cur_ptr += sizeof(MDS_TrackBlock);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track block #%i:\n", __debug__, i);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  mode: 0x%X\n", __debug__, block->mode);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  subchannel: 0x%X\n\n", __debug__, block->subchannel);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  adr/ctl: 0x%X\n", __debug__, block->adr_ctl);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  tno: 0x%X\n", __debug__, block->tno);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  point: 0x%X\n", __debug__, block->point);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  min: 0x%X\n", __debug__, block->min);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  sec: 0x%X\n", __debug__, block->sec);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  frame: 0x%X\n", __debug__, block->frame);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  zero: 0x%X\n", __debug__, block->zero);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  pmin: 0x%X\n", __debug__, block->pmin);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  psec: 0x%X\n", __debug__, block->psec);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  pframe: 0x%X\n\n", __debug__, block->pframe);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  extra offset: 0x%X\n", __debug__, block->extra_offset);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  sector size: 0x%X\n", __debug__, block->sector_size);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  dummy4: 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X\n", __debug__, block->__dummy4__[0], block->__dummy4__[1], block->__dummy4__[2], block->__dummy4__[3], block->__dummy4__[4], block->__dummy4__[5], block->__dummy4__[6], block->__dummy4__[7], block->__dummy4__[8], block->__dummy4__[9], block->__dummy4__[10], block->__dummy4__[11], block->__dummy4__[12], block->__dummy4__[13], block->__dummy4__[14], block->__dummy4__[15], block->__dummy4__[16], block->__dummy4__[17]);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  start sector: 0x%X\n", __debug__, block->start_sector);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  start offset: 0x%" G_GINT64_MODIFIER "X\n", __debug__, block->start_offset);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  number of files: 0x%X\n", __debug__, block->number_of_files);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  footer offset: 0x%X\n\n", __debug__, block->footer_offset);

        /* Read extra track block, if applicable; it seems that only CD images
           have extra blocks, though. For DVD images, extra_offset seems to
           contain track length */
        if (medium_type == MIRAGE_MEDIUM_CD && block->extra_offset) {
            extra_block = (MDS_TrackExtraBlock *)(self->priv->mds_data + block->extra_offset);
            mds_track_extra_block_fix_endian(extra_block);

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: extra block #%i:\n", __debug__, i);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  pregap: 0x%X\n", __debug__, extra_block->pregap);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  length: 0x%X\n\n", __debug__, extra_block->length);
        }

        /* Read footer, if applicable */
        if (block->footer_offset) {
            for (guint j = 0; j < block->number_of_files; j++) {
                footer_block = (MDS_Footer *)(self->priv->mds_data + block->footer_offset + j*sizeof(MDS_Footer));
                mds_footer_fix_endian(footer_block);

                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: footer block #%i - %i:\n", __debug__, i, j);
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  filename offset: 0x%X\n", __debug__, footer_block->filename_offset);
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  widechar filename: 0x%X\n", __debug__, footer_block->widechar_filename);
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  dummy1: 0x%X\n", __debug__, footer_block->__dummy1__);
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  dummy2: 0x%X\n\n", __debug__, footer_block->__dummy2__);
            }
        }

        if (block->point > 0 && block->point < 99) {
            /* Track entry */
            MirageTrack *track = g_object_new(MIRAGE_TYPE_TRACK, NULL);

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: entry is for track %i\n", __debug__, block->point);

            if (!mirage_session_add_track_by_number(session, block->point, track, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __debug__);
                g_object_unref(track);
                g_object_unref(session);
                return FALSE;
            }

            gint converted_mode = mirage_parser_mds_convert_track_mode(self, block->mode);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track mode: 0x%X\n", __debug__, converted_mode);
            mirage_track_set_sector_type(track, converted_mode);

            /* Flags: decoded from Ctl */
            mirage_track_set_ctl(track, block->adr_ctl & 0x0F);

            /* MDS format doesn't seem to store pregap data in its data file;
               therefore, we need to provide NULL fragment for pregap */
            if (extra_block && extra_block->pregap) {
                /* Create NULL fragment */
                MirageFragment *fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL);

                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track has pregap (0x%X); creating NULL fragment\n", __debug__, extra_block->pregap);

                mirage_fragment_set_length(fragment, extra_block->pregap);

                mirage_track_add_fragment(track, -1, fragment);
                g_object_unref(fragment);

                mirage_track_set_track_start(track, extra_block->pregap);
            }

            /* Data fragment(s): it seems that MDS allows splitting of MDF files into multiple files; it also seems
               files are split on (2048) sector boundary, which means we can simply represent them with multiple data
               fragments */
            for (guint j = 0; j < block->number_of_files; j++) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating data fragment for file #%i\n", __debug__, j);

                footer_block = (MDS_Footer *)(self->priv->mds_data + block->footer_offset + j*sizeof(MDS_Footer));
                /* NOTE: endian already fixed at this point! */

                /* Fragment properties */
                guint64 main_offset = 0; /* Corrected below, if needed */
                gint main_size = block->sector_size;
                gint main_format = 0;

                gint subchannel_size = 0;
                gint subchannel_format = 0;

                if (j == 0) {
                    /* Apply offset only if it's the first file... */
                    main_offset = block->start_offset;
                }

                if (converted_mode == MIRAGE_SECTOR_AUDIO) {
                    main_format = MIRAGE_MAIN_DATA_FORMAT_AUDIO;
                } else {
                    main_format = MIRAGE_MAIN_DATA_FORMAT_DATA;
                }

                /* Subchannel */
                switch (block->subchannel) {
                    case PW_INTERLEAVED: {
                        subchannel_size = 96;
                        subchannel_format = MIRAGE_SUBCHANNEL_DATA_FORMAT_PW96_INTERLEAVED | MIRAGE_SUBCHANNEL_DATA_FORMAT_INTERNAL;

                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: subchannel found; interleaved PW96\n", __debug__);

                        /* We need to correct the data for track sector size...
                           MDS format has already added 96 bytes to sector size,
                           so we need to subtract it */
                        main_size = block->sector_size - subchannel_size;

                        break;
                    }
                    case NONE: {
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: no subchannel\n", __debug__);
                        break;
                    }
                    default: {
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown subchannel type 0x%X!\n", __debug__, block->subchannel);
                        break;
                    }
                }


                /* Track file */
                gchar *mdf_filename = mirage_parser_mds_get_track_filename(self, footer_block, error);
                if (!mdf_filename) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get track filename!\n", __debug__);
                    g_object_unref(track);
                    g_object_unref(session);
                    return FALSE;
                }

                /* Data stream */
                MirageStream *data_stream = mirage_contextual_create_input_stream(MIRAGE_CONTEXTUAL(self), mdf_filename, error);
                if (!data_stream) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open stream on data file: %s!\n", __debug__, mdf_filename);
                    g_free(mdf_filename);
                    g_object_unref(track);
                    g_object_unref(session);
                    return FALSE;
                }
                g_free(mdf_filename);

                /* Determine fragment's length */
                gint64 fragment_len = 0;
                if (medium_type == MIRAGE_MEDIUM_CD) {
                    /* For CDs, track lengths are stored in extra block... and we assume
                       this is the same as fragment's length */
                    fragment_len = extra_block->length;
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD-ROM; track's fragment length: 0x%" G_GINT64_MODIFIER "X\n", __debug__, fragment_len);
                } else {
                    /* For DVDs, -track- length seems to be stored in extra_offset;
                       however, since DVD images can have split MDF files, we need
                       to calculate the individual framgents' lengths ourselves... */
                    mirage_stream_seek(data_stream, 0, G_SEEK_END, NULL);
                    fragment_len = mirage_stream_tell(data_stream);

                    fragment_len = (fragment_len - main_offset)/(main_size + subchannel_size); /* We could've just divided by 2048, too :) */
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: DVD-ROM; track's fragment length: 0x%" G_GINT64_MODIFIER "X\n", __debug__, fragment_len);
                }

                /* Create data fragment */
                MirageFragment *fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL);

                mirage_fragment_set_length(fragment, fragment_len);

                /* Set stream */
                mirage_fragment_main_data_set_stream(fragment, data_stream);
                g_object_unref(data_stream);

                mirage_fragment_main_data_set_offset(fragment, main_offset);
                mirage_fragment_main_data_set_size(fragment, main_size);
                mirage_fragment_main_data_set_format(fragment, main_format);

                mirage_fragment_subchannel_data_set_size(fragment, subchannel_size);
                mirage_fragment_subchannel_data_set_format(fragment, subchannel_format);

                mirage_track_add_fragment(track, -1, fragment);
                g_object_unref(fragment);
            }

            g_object_unref(track);
        } else {
            /* Non-track block; skip */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: skipping non-track entry 0x%X\n", __debug__, block->point);
        }

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    }

    g_object_unref(session);

    return TRUE;
}

static gboolean mirage_parser_mds_parse_sessions (MirageParserMds *self, GError **error)
{
    guint8 *cur_ptr;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading session blocks (%i)\n", __debug__, self->priv->header->num_sessions);

    cur_ptr = self->priv->mds_data + self->priv->header->sessions_blocks_offset;

    /* Read sessions */
    for (gint i = 0; i < self->priv->header->num_sessions; i++) {
        MDS_SessionBlock *session_block = MIRAGE_CAST_PTR(cur_ptr, 0, MDS_SessionBlock *);
        mds_session_block_fix_endian(session_block);
        cur_ptr += sizeof(MDS_SessionBlock);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: session block #%i:\n", __debug__, i);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  start address: 0x%X\n", __debug__, session_block->session_start);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  end address: 0x%X\n", __debug__, session_block->session_end);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  number: %i\n", __debug__, session_block->session_number);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  number of all blocks: %i\n", __debug__, session_block->num_all_blocks);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  number of non-track block: %i\n", __debug__, session_block->num_nontrack_blocks);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  first track: %i\n", __debug__, session_block->first_track);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  last track: %i\n", __debug__, session_block->last_track);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  dummy1: 0x%X\n", __debug__, session_block->__dummy1__);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  track blocks offset: 0x%X\n\n", __debug__, session_block->tracks_blocks_offset);

        /* If this is first session, we'll use its start address as disc start address;
           if not, we need to calculate previous session's leadout length, based on
           this session's start address and previous session's end... */
        if (i == 0) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: first session; setting disc's start to 0x%X (%i)\n", __debug__, session_block->session_start, session_block->session_start);
            mirage_disc_layout_set_start_sector(self->priv->disc, session_block->session_start);
        } else {
            guint32 leadout_length = session_block->session_start - self->priv->prev_session_end;
            MirageSession *prev_session;
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: previous session's leadout length: 0x%X (%i)\n", __debug__, leadout_length, leadout_length);

            /* Use -1 as an index, since we still haven't added current session */
            prev_session = mirage_disc_get_session_by_index(self->priv->disc, -1, error);
            if (!prev_session) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get previous session!\n", __debug__);
                return FALSE;
            }

            mirage_session_set_leadout_length(prev_session, leadout_length);

            g_object_unref(prev_session);
        }
        /* Actually, we could've gotten that one from A2 track entry as well...
           but I'm lazy, and this will hopefully work as well */
        self->priv->prev_session_end = session_block->session_end;

        /* Add session */
        MirageSession *session = g_object_new(MIRAGE_TYPE_SESSION, NULL);
        if (!mirage_disc_add_session_by_number(self->priv->disc, session_block->session_number, session, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __debug__);
            g_object_unref(session);
            return FALSE;
        }
        g_object_unref(session);

        /* Load tracks */
        if (!mirage_parser_mds_parse_track_entries(self, session_block, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse track entries!\n", __debug__);
            return FALSE;
        }

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    }

    return TRUE;
}

static gboolean mirage_parser_mds_load_disc (MirageParserMds *self, GError **error)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");

    /* Read parser structures */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing disc structures...\n", __debug__);
    mirage_parser_mds_parse_disc_structures(self);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished parsing disc structures\n\n", __debug__);

    /* Read BCA */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing BCA data...\n", __debug__);
    mirage_parser_mds_parse_bca(self);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished parsing BCA data\n\n", __debug__);

    /* Sessions */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing sessions...\n", __debug__);
    if (!mirage_parser_mds_parse_sessions(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse sessions!\n", __debug__);
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished parsing sessions\n\n", __debug__);

    /* DPM data */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing DPM data...\n", __debug__);
    mirage_parser_mds_parse_dpm_data(self);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished parsing DPM data\n\n", __debug__);

    return TRUE;
}

/**********************************************************************\
 *                MirageParser methods implementation                *
\**********************************************************************/
static MirageDisc *mirage_parser_mds_load_image (MirageParser *_self, MirageStream **streams, GError **error)
{
    MirageParserMds *self = MIRAGE_PARSER_MDS(_self);

    gboolean succeeded = TRUE;
    guint8 *cur_ptr;
    MirageStream *stream;
    guint64 read_length;
    gchar signature[17];

    /* Check if we can load the image */
    stream = g_object_ref(streams[0]);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: checking if parser can handle given image...\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: veryfing signature at the beginning of the file...\n", __debug__);

    /* Read signature and first byte of version */
    mirage_stream_seek(stream, 0, G_SEEK_SET, NULL);
    if (mirage_stream_read(stream, signature, sizeof(signature), NULL) != sizeof(signature)) {
        g_object_unref(stream);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: parser cannot handle given image: failed to read signature and version!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Parser cannot handle given image: failed to read signature and version!"));
        return FALSE;
    }

    /* We can handle only v.1.X images (Alcohol 120% format) */
    if (memcmp(signature, mds_signature, sizeof(mds_signature))) {
        g_object_unref(stream);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: parser cannot handle given image: invalid signature and/or version!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Parser cannot handle given image: invalid signature and/or version!"));
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: parser can handle given image!\n", __debug__);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing the image...\n", __debug__);

    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_set_parent(MIRAGE_OBJECT(self->priv->disc), self);

    self->priv->mds_filename = mirage_stream_get_filename(stream);
    mirage_disc_set_filename(self->priv->disc, self->priv->mds_filename);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: MDs filename: %s\n", __debug__, self->priv->mds_filename);

    /* Get file size */
    mirage_stream_seek(stream, 0, G_SEEK_END, NULL);
    self->priv->mds_length = mirage_stream_tell(stream);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: MDS length: %" G_GINT64_MODIFIER "d bytes\n", __debug__, self->priv->mds_length);

    /* Allocate buffer */
    self->priv->mds_data = g_malloc(self->priv->mds_length);

    /* Read whole file */
    mirage_stream_seek(stream, 0, G_SEEK_SET, NULL);
    read_length = mirage_stream_read(stream, self->priv->mds_data, self->priv->mds_length, NULL);

    g_object_unref(stream);

    if (read_length != self->priv->mds_length) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read whole MDS file (%" G_GINT64_MODIFIER "d out of %" G_GINT64_MODIFIER "d bytes read)!\n", __debug__, read_length, self->priv->mds_length);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Failed to read whole MDS file!"));
        succeeded = FALSE;
        goto end;
    }

    /* Parse MDS file */
    cur_ptr = self->priv->mds_data;

    self->priv->header = MIRAGE_CAST_PTR(cur_ptr, 0, MDS_Header *);
    mds_header_fix_endian(self->priv->header);
    cur_ptr += sizeof(MDS_Header);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: MDS header:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  signature: %.16s\n", __debug__, self->priv->header->signature);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  version (?): %u.%u\n", __debug__, self->priv->header->version[0], self->priv->header->version[1]);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  medium type: 0x%X\n", __debug__, self->priv->header->medium_type);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  number of sessions: 0x%X\n", __debug__, self->priv->header->num_sessions);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  dummy1: 0x%X, 0x%X\n", __debug__, self->priv->header->__dummy1__[0], self->priv->header->__dummy1__[1]);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  BCA length: 0x%X\n", __debug__, self->priv->header->bca_len);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  dummy2: 0x%X, 0x%X\n", __debug__, self->priv->header->__dummy2__[0], self->priv->header->__dummy2__[1]);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  BCA data offset: 0x%X\n", __debug__, self->priv->header->bca_data_offset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  dummy3: 0x%X, 0x%X, 0x%X, 0x%X, 0x%X, 0x%X\n", __debug__, self->priv->header->__dummy3__[0], self->priv->header->__dummy3__[1], self->priv->header->__dummy3__[2], self->priv->header->__dummy3__[3], self->priv->header->__dummy3__[4], self->priv->header->__dummy3__[5]);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  disc structures offset: 0x%X\n", __debug__, self->priv->header->disc_structures_offset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  dummy4: 0x%X, 0x%X, 0x%X\n", __debug__, self->priv->header->__dummy4__[0], self->priv->header->__dummy4__[1], self->priv->header->__dummy4__[2]);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  session blocks offset: 0x%X\n", __debug__, self->priv->header->sessions_blocks_offset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  DPM blocks offset: 0x%X\n\n", __debug__, self->priv->header->dpm_blocks_offset);

    switch (self->priv->header->medium_type) {
        case CD:
        case CD_R:
        case CD_RW: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD-ROM image\n", __debug__);
            mirage_disc_set_medium_type(self->priv->disc, MIRAGE_MEDIUM_CD);
            succeeded = mirage_parser_mds_load_disc(self, error);
            break;
        }
        case DVD:
        case DVD_MINUS_R: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: DVD-ROM image\n", __debug__);
            mirage_disc_set_medium_type(self->priv->disc, MIRAGE_MEDIUM_DVD);
            succeeded = mirage_parser_mds_load_disc(self, error);
            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: medium of type 0x%X not supported yet!\n", __debug__, self->priv->header->medium_type);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Medium of type 0x%X not supported yet!"), self->priv->header->medium_type);
            succeeded = FALSE;
            break;
        }
    }

end:
    g_free(self->priv->mds_data);
    self->priv->mds_data = NULL;

    /* Return disc */
    if (succeeded) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing completed successfully\n\n", __debug__);
        return self->priv->disc;
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing failed!\n\n", __debug__);
        g_object_unref(self->priv->disc);
        return NULL;
    }
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE_EXTENDED(
    MirageParserMds,
    mirage_parser_mds,
    MIRAGE_TYPE_PARSER,
    0,
    G_ADD_PRIVATE_DYNAMIC(MirageParserMds)
)

void mirage_parser_mds_type_register (GTypeModule *type_module)
{
    mirage_parser_mds_register_type(type_module);
}


static void mirage_parser_mds_init (MirageParserMds *self)
{
    self->priv = mirage_parser_mds_get_instance_private(self);

    mirage_parser_generate_info(MIRAGE_PARSER(self),
        "PARSER-MDS",
        Q_("MDS Image Parser"),
        2,
        /* xgettext:no-c-format */
        Q_("Alchohol 120% images (*.mds)"), "application/x-mds",
        Q_("GameJack images (*.xmd)"), "application/x-xmd"
    );

    self->priv->mds_data = NULL;
}

static void mirage_parser_mds_finalize (GObject *gobject)
{
    MirageParserMds *self = MIRAGE_PARSER_MDS(gobject);

    g_free(self->priv->mds_data);

    /* Chain up to the parent class */
    G_OBJECT_CLASS(mirage_parser_mds_parent_class)->finalize(gobject);
}

static void mirage_parser_mds_class_init (MirageParserMdsClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageParserClass *parser_class = MIRAGE_PARSER_CLASS(klass);

    gobject_class->finalize = mirage_parser_mds_finalize;

    parser_class->load_image = mirage_parser_mds_load_image;
}

static void mirage_parser_mds_class_finalize (MirageParserMdsClass *klass G_GNUC_UNUSED)
{
}
