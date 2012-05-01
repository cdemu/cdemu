/*
 *  libMirage: MDS image parser: Parser object
 *  Copyright (C) 2006-2012 Rok Mandeljc
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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "image-mds.h"

#define __debug__ "MDS-Parser"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_PARSER_MDS_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_MDS, MIRAGE_Parser_MDSPrivate))

struct _MIRAGE_Parser_MDSPrivate
{
    GObject *disc;

    MDS_Header *header;

    gint32 prev_session_end;

    gchar *mds_filename;
    guint64 mds_length;
    guint8 *mds_data;
};


/*
    I hexedited the track mode field with various values and fed it to Alchohol;
    it seemed that high part of byte had no effect at all; only the lower one
    affected the mode, in the following manner:
    00: Mode 2, 01: Audio, 02: Mode 1, 03: Mode 2, 04: Mode 2 Form 1, 05: Mode 2 Form 2, 06: UKNONOWN, 07: Mode 2
    08: Mode 2, 09: Audio, 0A: Mode 1, 0B: Mode 2, 0C: Mode 2 Form 1, 0D: Mode 2 Form 2, 0E: UKNONOWN, 0F: Mode 2
*/
static gint mirage_parser_mds_convert_track_mode (MIRAGE_Parser_MDS *self, gint mode)
{
    /* convert between two values */
    static const struct {
        gint mds_mode;
        gint mirage_mode;
    } modes[] = {
        {0x00, MIRAGE_MODE_MODE2},
        {0x01, MIRAGE_MODE_AUDIO},
        {0x02, MIRAGE_MODE_MODE1},
        {0x03, MIRAGE_MODE_MODE2},
        {0x04, MIRAGE_MODE_MODE2_FORM1},
        {0x05, MIRAGE_MODE_MODE2_FORM2},
        /*{0x06, MIRAGE_MODE_UNKNOWN},*/
        {0x07, MIRAGE_MODE_MODE2},
    };
    gint i;

    /* Basically, do the test twice; once for value, and once for value + 8 */
    for (i = 0; i < G_N_ELEMENTS(modes); i++) {
        if (((mode & 0x0F) == modes[i].mds_mode)
            || ((mode & 0x0F) == modes[i].mds_mode + 8)) {
            return modes[i].mirage_mode;
        }
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown track mode 0x%X!\n", __debug__, mode);
    return -1;
}


static gchar *__helper_find_binary_file (gchar *declared_filename, gchar *mds_filename)
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
        g_match_info_free(match_info);
    } else {
        bin_filename = g_strdup(declared_filename);
    }
    g_regex_unref(ext_regex);

    bin_fullpath = mirage_helper_find_data_file(bin_filename, mds_filename);
    g_free(bin_filename);

    return bin_fullpath;
}

static gboolean mirage_parser_mds_parse_dpm_block (MIRAGE_Parser_MDS *self, guint32 dpm_block_offset, GError **error)
{
    guint8 *cur_ptr;

    guint32 dpm_block_number;
    guint32 dpm_start_sector;
    guint32 dpm_resolution;
    guint32 dpm_num_entries;

    guint32 *dpm_data;

    cur_ptr = self->priv->mds_data + dpm_block_offset;

    /* DPM information */
    dpm_block_number = MIRAGE_CAST_DATA(cur_ptr, 0, guint32);
    cur_ptr += sizeof(guint32);

    dpm_start_sector = MIRAGE_CAST_DATA(cur_ptr, 0, guint32);
    cur_ptr += sizeof(guint32);

    dpm_resolution = MIRAGE_CAST_DATA(cur_ptr, 0, guint32);
    cur_ptr += sizeof(guint32);

    dpm_num_entries = MIRAGE_CAST_DATA(cur_ptr, 0, guint32);
    cur_ptr += sizeof(guint32);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: block number: %d\n", __debug__, dpm_block_number);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: start sector: 0x%X\n", __debug__, dpm_start_sector);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: resolution: %d\n", __debug__, dpm_resolution);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of entries: %d\n", __debug__, dpm_num_entries);

    /* Read all entries */
    dpm_data = MIRAGE_CAST_PTR(cur_ptr, 0, guint32 *);

    /* Set DPM data */
    if (!mirage_disc_set_dpm_data(MIRAGE_DISC(self->priv->disc), dpm_start_sector, dpm_resolution, dpm_num_entries, dpm_data, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set DPM data!\n", __debug__);
        return FALSE;
    }

    return TRUE;
}

static gboolean mirage_parser_mds_parse_dpm_data (MIRAGE_Parser_MDS *self, GError **error G_GNUC_UNUSED)
{
    guint8 *cur_ptr;

    gint i;

    guint32 num_dpm_blocks;
    guint32 *dpm_block_offset;

    if (!self->priv->header->dpm_blocks_offset) {
        /* No DPM data, nothing to do */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: no DPM data\n", __debug__);
        return TRUE;
    }

    cur_ptr = self->priv->mds_data + self->priv->header->dpm_blocks_offset;

    /* It would seem the first field is number of DPM data sets, followed by
       appropriate number of offsets for those data sets */
    num_dpm_blocks = MIRAGE_CAST_DATA(cur_ptr, 0, guint32);
    cur_ptr += sizeof(guint32);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of DPM data blocks: %d\n", __debug__, num_dpm_blocks);

    dpm_block_offset = MIRAGE_CAST_PTR(cur_ptr, 0, guint32 *);

    if (num_dpm_blocks > 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: cannot correctly handle more than 1 DPM block yet!\n", __debug__);
    }

    /* Read each block */
    for (i = 0; i < num_dpm_blocks; i++) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: block[%i]: offset: 0x%X\n", __debug__, i, dpm_block_offset[i]);
        mirage_parser_mds_parse_dpm_block(self, dpm_block_offset[i], NULL);
        /* FIXME: currently, only first DPM block is loaded */
        break;
    }

    return TRUE;
}

static gboolean mirage_parser_mds_parse_disc_structures (MIRAGE_Parser_MDS *self, GError **error G_GNUC_UNUSED)
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
        MIRAGE_DiscStruct_Copyright *copy_info;
        MIRAGE_DiscStruct_Manufacture *manu_info;
        MIRAGE_DiscStruct_PhysInfo *phys_info;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading disc structures\n", __debug__);

        cur_ptr = self->priv->mds_data + self->priv->header->disc_structures_offset;

        /* DVD copyright information */
        copy_info = MIRAGE_CAST_PTR(cur_ptr, 0, MIRAGE_DiscStruct_Copyright *);
        cur_ptr += sizeof(MIRAGE_DiscStruct_Copyright);

        /* DVD manufacture information */
        manu_info = MIRAGE_CAST_PTR(cur_ptr, 0, MIRAGE_DiscStruct_Manufacture *);
        cur_ptr += sizeof(MIRAGE_DiscStruct_Manufacture);

        /* Physical information */
        phys_info = MIRAGE_CAST_PTR(cur_ptr, 0, MIRAGE_DiscStruct_PhysInfo *);
        cur_ptr += sizeof(MIRAGE_DiscStruct_PhysInfo);

        mirage_disc_set_disc_structure(MIRAGE_DISC(self->priv->disc), 0, 0x0000, (guint8 *)phys_info, sizeof(MIRAGE_DiscStruct_Copyright), NULL);
        mirage_disc_set_disc_structure(MIRAGE_DISC(self->priv->disc), 0, 0x0001, (guint8 *)copy_info, sizeof(MIRAGE_DiscStruct_Manufacture), NULL);
        mirage_disc_set_disc_structure(MIRAGE_DISC(self->priv->disc), 0, 0x0004, (guint8 *)manu_info, sizeof(MIRAGE_DiscStruct_PhysInfo), NULL);

        /* Second round if it's dual-layer... */
        if (phys_info->num_layers == 0x01) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: dual-layer disc; reading disc structures for second layer\n", __debug__);

            /* DVD copyright information */
            copy_info = MIRAGE_CAST_PTR(cur_ptr, 0, MIRAGE_DiscStruct_Copyright *);
            cur_ptr += sizeof(MIRAGE_DiscStruct_Copyright);

            /* DVD manufacture information */
            manu_info = MIRAGE_CAST_PTR(cur_ptr, 0, MIRAGE_DiscStruct_Manufacture *);
            cur_ptr += sizeof(MIRAGE_DiscStruct_Manufacture);

            /* Physical information */
            phys_info = MIRAGE_CAST_PTR(cur_ptr, 0, MIRAGE_DiscStruct_PhysInfo *);
            cur_ptr += sizeof(MIRAGE_DiscStruct_PhysInfo);

            mirage_disc_set_disc_structure(MIRAGE_DISC(self->priv->disc), 0, 0x0000, (guint8 *)phys_info, sizeof(MIRAGE_DiscStruct_Copyright), NULL);
            mirage_disc_set_disc_structure(MIRAGE_DISC(self->priv->disc), 0, 0x0001, (guint8 *)copy_info, sizeof(MIRAGE_DiscStruct_Manufacture), NULL);
            mirage_disc_set_disc_structure(MIRAGE_DISC(self->priv->disc), 0, 0x0004, (guint8 *)manu_info, sizeof(MIRAGE_DiscStruct_PhysInfo), NULL);
        }
    }

    return TRUE;
}

static gboolean mirage_parser_mds_parse_bca (MIRAGE_Parser_MDS *self, GError **error G_GNUC_UNUSED)
{
    guint8 *cur_ptr;

    /* It seems BCA (Burst Cutting Area) structure is stored as well, but in separate
       place (kinda makes sense, because it doesn't have fixed length) */
    if (self->priv->header->bca_len) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading BCA data (0x%X bytes)\n", __debug__, self->priv->header->bca_len);

        cur_ptr = self->priv->mds_data + self->priv->header->bca_data_offset;
        mirage_disc_set_disc_structure(MIRAGE_DISC(self->priv->disc), 0, 0x0003, MIRAGE_CAST_PTR(cur_ptr, 0, guint8 *), self->priv->header->bca_len, NULL);
    }

    return TRUE;
}

static gchar *mirage_parser_mds_get_track_filename (MIRAGE_Parser_MDS *self, MDS_Footer *footer_block, GError **error)
{
    gchar *tmp_mdf_filename;
    gchar *mdf_filename;

    /* Track file: it seems all tracks have the same extra block, and that
       filename is located at the end of it... meaning filename's length is
       from filename_offset to end of the file */
    if (!footer_block) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: track block does not have a footer, but we're supposed to get filename from it!\n", __debug__);
        mirage_error(MIRAGE_E_PARSER, error);
        return NULL;
    }

    /* If footer_block->widechar_filename is set, filename is stored using 16-bit
       (wide) characters, otherwise 8-bit characters are used. */
    if (footer_block->widechar_filename) {
        gunichar2 *tmp_ptr = MIRAGE_CAST_PTR(self->priv->mds_data, footer_block->filename_offset, gunichar2 *);
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
        mirage_error(MIRAGE_E_DATAFILE, error);
        return NULL;
    }

    return mdf_filename;
}

static gboolean mirage_parser_mds_parse_track_entries (MIRAGE_Parser_MDS *self, MDS_SessionBlock *session_block, GError **error)
{
    GObject *cur_session = NULL;
    guint8 *cur_ptr;
    gint medium_type;
    gint i;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading track blocks\n", __debug__);

    /* Fetch medium type which we'll need later */
    mirage_disc_get_medium_type(MIRAGE_DISC(self->priv->disc), &medium_type, NULL);

    /* Get current session */
    if (!mirage_disc_get_session_by_index(MIRAGE_DISC(self->priv->disc), -1, &cur_session, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current session!\n", __debug__);
        return FALSE;
    }

    cur_ptr = self->priv->mds_data + session_block->tracks_blocks_offset;

    /* Read track entries */
    for (i = 0; i < session_block->num_all_blocks; i++) {
        MDS_TrackBlock *block;
        MDS_TrackExtraBlock *extra_block = NULL;
        MDS_Footer *footer_block = NULL;

        /* Read main track block */
        block = MIRAGE_CAST_PTR(cur_ptr, 0, MDS_TrackBlock *);
        cur_ptr += sizeof(MDS_TrackBlock);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track block #%i:\n", __debug__, i);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  mode: 0x%X\n", __debug__, block->mode);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  subchannel: 0x%X\n", __debug__, block->subchannel);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  adr/ctl: 0x%X\n", __debug__, block->adr_ctl);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  dummy2: 0x%X\n", __debug__, block->__dummy2__);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  point: 0x%X\n", __debug__, block->point);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  dummy3: 0x%X\n", __debug__, block->__dummy3__);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  min: %i\n", __debug__, block->min);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  sec: %i\n", __debug__, block->sec);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  frame: %i\n", __debug__, block->frame);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  extra offset: 0x%X\n", __debug__, block->extra_offset);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  sector size: 0x%X\n", __debug__, block->sector_size);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  start sector: 0x%X\n", __debug__, block->start_sector);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  start offset: 0x%llX\n", __debug__, block->start_offset);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  number of files: 0x%X\n", __debug__, block->number_of_files);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  footer offset: 0x%X\n", __debug__, block->footer_offset);

        /* Read extra track block, if applicable; it seems that only CD images
           have extra blocks, though. For DVD images, extra_offset seems to
           contain track length */
        if (medium_type == MIRAGE_MEDIUM_CD && block->extra_offset) {
            extra_block = (MDS_TrackExtraBlock *)(self->priv->mds_data + block->extra_offset);

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: extra block #%i:\n", __debug__, i);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  pregap: 0x%X\n", __debug__, extra_block->pregap);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  length: 0x%X\n", __debug__, extra_block->length);
        }

        /* Read footer, if applicable */
        if (block->footer_offset) {
            gint j;

            for (j = 0; j < block->number_of_files; j++) {
                footer_block = (MDS_Footer *)(self->priv->mds_data + block->footer_offset + j*sizeof(MDS_Footer));

                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: footer block #%i - %i:\n", __debug__, i, j);
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  filename offset: 0x%X\n", __debug__, footer_block->filename_offset);
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  widechar filename: 0x%X\n", __debug__, footer_block->widechar_filename);
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  dummy1: 0x%X\n", __debug__, footer_block->__dummy1__);
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  dummy2: 0x%X\n", __debug__, footer_block->__dummy2__);
            }
        }

        if (block->point > 0 && block->point < 99) {
            /* Track entry */
            GObject *cur_track = NULL;

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: entry is for track %i\n", __debug__, block->point);

            if (!mirage_session_add_track_by_number(MIRAGE_SESSION(cur_session), block->point, &cur_track, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __debug__);
                g_object_unref(cur_session);
                return FALSE;
            }

            gint converted_mode = mirage_parser_mds_convert_track_mode(self, block->mode);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track mode: 0x%X\n", __debug__, converted_mode);
            mirage_track_set_mode(MIRAGE_TRACK(cur_track), converted_mode, NULL);

            /* Flags: decoded from Ctl */
            mirage_track_set_ctl(MIRAGE_TRACK(cur_track), block->adr_ctl & 0x0F, NULL);

            /* MDS format doesn't seem to store pregap data in its data file;
               therefore, we need to provide NULL fragment for pregap */
            if (extra_block && extra_block->pregap) {
                GObject *pregap_fragment;

                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track has pregap (0x%X); creating NULL fragment\n", __debug__, extra_block->pregap);

                pregap_fragment = libmirage_create_fragment(MIRAGE_TYPE_FRAG_IFACE_NULL, "NULL", error);
                if (!pregap_fragment) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to create NULL fragment!\n", __debug__);
                    g_object_unref(cur_track);
                    g_object_unref(cur_session);
                    return FALSE;
                }

                mirage_fragment_set_length(MIRAGE_FRAGMENT(pregap_fragment), extra_block->pregap, NULL);

                mirage_track_add_fragment(MIRAGE_TRACK(cur_track), -1, &pregap_fragment, error);
                g_object_unref(pregap_fragment);

                mirage_track_set_track_start(MIRAGE_TRACK(cur_track), extra_block->pregap, NULL);
            }

            /* Data fragment(s): it seems that MDS allows splitting of MDF files into multiple files; it also seems
               files are split on (2048) sector boundary, which means we can simply represent them with multiple data
               fragments */
            gint j;
            for (j = 0; j < block->number_of_files; j++) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating data fragment for file #%i\n", __debug__, j);

                footer_block = (MDS_Footer *)(self->priv->mds_data + block->footer_offset + j*sizeof(MDS_Footer));

                /* Fragment properties */
                guint64 tfile_offset = 0; /* Corrected below, if needed */
                gint tfile_sectsize = block->sector_size;
                gint tfile_format = 0;

                gint sfile_sectsize = 0;
                gint sfile_format = 0;

                if (j == 0) {
                    /* Apply offset only if it's the first file... */
                    tfile_offset = block->start_offset;
                }

                if (converted_mode == MIRAGE_MODE_AUDIO) {
                    tfile_format = FR_BIN_TFILE_AUDIO;
                } else {
                    tfile_format = FR_BIN_TFILE_DATA;
                }

                /* Subchannel */
                switch (block->subchannel) {
                    case MDS_SUBCHAN_PW_INTERLEAVED: {
                        sfile_sectsize = 96;
                        sfile_format = FR_BIN_SFILE_PW96_INT | FR_BIN_SFILE_INT;

                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: subchannel found; interleaved PW96\n", __debug__);

                        /* We need to correct the data for track sector size...
                           MDS format has already added 96 bytes to sector size,
                           so we need to subtract it */
                        tfile_sectsize = block->sector_size - sfile_sectsize;

                        break;
                    }
                    case MDS_SUBCHAN_NONE: {
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
                    g_object_unref(cur_track);
                    g_object_unref(cur_session);
                    return FALSE;
                }

                gint fragment_len = 0;

                /* Determine fragment's length */
                if (medium_type == MIRAGE_MEDIUM_CD) {
                    /* For CDs, track lengths are stored in extra block... and we assume
                       this is the same as fragment's length */
                    fragment_len = extra_block->length;
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD-ROM; track's fragment length: 0x%X\n", __debug__, fragment_len);
                } else {
                    /* For DVDs, -track- length seems to be stored in extra_offset;
                       however, since DVD images can have split MDF files, we need
                       to calculate the individual framgents' lengths ourselves... */
                    struct stat st;
                    
                    if (g_stat(mdf_filename, &st) < 0) {
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to stat data file!\n", __debug__);
                        mirage_error(MIRAGE_E_IMAGEFILE, error);
                        g_free(mdf_filename);
                        g_object_unref(cur_track);
                        g_object_unref(cur_session);
                        return FALSE;
                    }
                    
                    fragment_len = (st.st_size - tfile_offset)/(tfile_sectsize + sfile_sectsize); /* We could've just divided by 2048, too :) */
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: DVD-ROM; track's fragment length: 0x%X\n", __debug__, fragment_len);
                }

                /* Create data fragment */
                GObject *data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FRAG_IFACE_BINARY, mdf_filename, error);
                if (!data_fragment) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to create fragment!\n", __debug__);
                    g_free(mdf_filename);
                    g_object_unref(cur_track);
                    g_object_unref(cur_session);
                    return FALSE;
                }

                mirage_fragment_set_length(MIRAGE_FRAGMENT(data_fragment), fragment_len, NULL);

                /* Set file */
                if (!mirage_frag_iface_binary_track_file_set_file(MIRAGE_FRAG_IFACE_BINARY(data_fragment), mdf_filename, error)) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set track data file!\n", __debug__);
                    g_free(mdf_filename);
                    g_object_unref(data_fragment);
                    g_object_unref(cur_track);
                    g_object_unref(cur_session);
                    return FALSE;
                }
                g_free(mdf_filename);
                
                mirage_frag_iface_binary_track_file_set_offset(MIRAGE_FRAG_IFACE_BINARY(data_fragment), tfile_offset, NULL);
                mirage_frag_iface_binary_track_file_set_sectsize(MIRAGE_FRAG_IFACE_BINARY(data_fragment), tfile_sectsize, NULL);
                mirage_frag_iface_binary_track_file_set_format(MIRAGE_FRAG_IFACE_BINARY(data_fragment), tfile_format, NULL);

                mirage_frag_iface_binary_subchannel_file_set_sectsize(MIRAGE_FRAG_IFACE_BINARY(data_fragment), sfile_sectsize, NULL);
                mirage_frag_iface_binary_subchannel_file_set_format(MIRAGE_FRAG_IFACE_BINARY(data_fragment), sfile_format, NULL);

                mirage_track_add_fragment(MIRAGE_TRACK(cur_track), -1, &data_fragment, error);
                g_object_unref(data_fragment);
            }

            g_object_unref(cur_track);
        } else {
            /* Non-track block; skip */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: skipping non-track entry 0x%X\n", __debug__, block->point);
        }
    }

    g_object_unref(cur_session);

    return TRUE;
}

static gboolean mirage_parser_mds_parse_sessions (MIRAGE_Parser_MDS *self, GError **error)
{
    guint8 *cur_ptr;
    gint i;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading session blocks (%i)\n", __debug__, self->priv->header->num_sessions);

    cur_ptr = self->priv->mds_data + self->priv->header->sessions_blocks_offset;

    /* Read sessions */
    for (i = 0; i < self->priv->header->num_sessions; i++) {
        MDS_SessionBlock *session = MIRAGE_CAST_PTR(cur_ptr, 0, MDS_SessionBlock *);
        cur_ptr += sizeof(MDS_SessionBlock);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: session block #%i:\n", __debug__, i);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  start address: 0x%X\n", __debug__, session->session_start);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  length: 0x%X\n", __debug__, session->session_end);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  number: %i\n", __debug__, session->session_number);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  number of all blocks: %i\n", __debug__, session->num_all_blocks);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  number of non-track block: %i\n", __debug__, session->num_nontrack_blocks);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  first track: %i\n", __debug__, session->first_track);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  last track: %i\n", __debug__, session->last_track);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  dummy2: 0x%X\n", __debug__, session->__dummy2__);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  track blocks offset: 0x%X\n", __debug__, session->tracks_blocks_offset);

        /* If this is first session, we'll use its start address as disc start address;
           if not, we need to calculate previous session's leadout length, based on
           this session's start address and previous session's end... */
        if (i == 0) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: first session; setting disc's start to 0x%X (%i)\n", __debug__, session->session_start, session->session_start);
            mirage_disc_layout_set_start_sector(MIRAGE_DISC(self->priv->disc), session->session_start, NULL);
        } else {
            guint32 leadout_length = session->session_start - self->priv->prev_session_end;
            GObject *prev_session = NULL;
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: previous session's leadout length: 0x%X (%i)\n", __debug__, leadout_length, leadout_length);

            /* Use -1 as an index, since we still haven't added current session */
            if (!mirage_disc_get_session_by_index(MIRAGE_DISC(self->priv->disc), -1, &prev_session, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get previous session!\n", __debug__);
                return FALSE;
            }

            if (!mirage_session_set_leadout_length(MIRAGE_SESSION(prev_session), leadout_length, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set leadout length!\n", __debug__);
                g_object_unref(prev_session);
                return FALSE;
            }

            g_object_unref(prev_session);
        }
        /* Actually, we could've gotten that one from A2 track entry as well...
           but I'm lazy, and this will hopefully work as well */
        self->priv->prev_session_end = session->session_end;

        /* Add session */
        if (!mirage_disc_add_session_by_number(MIRAGE_DISC(self->priv->disc), session->session_number, NULL, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __debug__);
            return FALSE;
        }

        /* Load tracks */
        if (!mirage_parser_mds_parse_track_entries(self, session, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse track entries!\n", __debug__);
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean mirage_parser_mds_load_disc (MIRAGE_Parser_MDS *self, GError **error)
{
    /* Read parser structures */
    if (!mirage_parser_mds_parse_disc_structures(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse disc structures!\n", __debug__);
        return FALSE;
    }

    /* Read BCA */
    if (!mirage_parser_mds_parse_bca(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse BCA!\n", __debug__);
        return FALSE;
    }

    /* Sessions */
    if (!mirage_parser_mds_parse_sessions(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse sessions!\n", __debug__);
        return FALSE;
    }

    /* DPM data */
    if (!mirage_parser_mds_parse_dpm_data(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse DPM data!\n", __debug__);
        return FALSE;
    }

    return TRUE;
}

/**********************************************************************\
 *                MIRAGE_Parser methods implementation                *
\**********************************************************************/
static gboolean mirage_parser_mds_load_image (MIRAGE_Parser *_self, gchar **filenames, GObject **disc, GError **error)
{
    MIRAGE_Parser_MDS *self = MIRAGE_PARSER_MDS(_self);
    
    gboolean succeeded = TRUE;
    guint8 *cur_ptr;
    FILE *file;
    guint64 read_length;
    gchar sig[16] = "";
    guint8 ver[2];

    /* Check if we can load the image */
    file = g_fopen(filenames[0], "r");
    if (!file) {
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }

    /* Read signature and version */
    if ((fread(sig, 16, 1, file) < 1) || (fread(ver, 2, 1, file) < 1)) {
        fclose(file);
        mirage_error(MIRAGE_E_READFAILED, error);
        return FALSE;
    }

    if (memcmp(sig, "MEDIA DESCRIPTOR", 16)) {
        fclose(file);
        mirage_error(MIRAGE_E_CANTHANDLE, error);
        return FALSE;
    }

    /* We can handle only v.1.X images (Alcohol 120% format) */
    if (ver[0] != 1 ) {
        fclose(file);
        mirage_error(MIRAGE_E_CANTHANDLE, error);
        return FALSE;
    }


    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_attach_child(MIRAGE_OBJECT(self), self->priv->disc, NULL);

    mirage_disc_set_filename(MIRAGE_DISC(self->priv->disc), filenames[0], NULL);
    self->priv->mds_filename = g_strdup(filenames[0]);


    /* Get file size */
    fseek(file, 0, SEEK_END);
    self->priv->mds_length = ftell(file);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: MDS length: %lld bytes\n", __debug__, self->priv->mds_length);

    /* Allocate buffer */
    self->priv->mds_data = g_malloc(self->priv->mds_length);

    /* Read whole file */
    fseek(file, 0, SEEK_SET);
    read_length = fread(self->priv->mds_data, 1, self->priv->mds_length, file);

    fclose(file);

    if (read_length != self->priv->mds_length) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read whole MDS file '%s' (%lld out of %lld bytes read)!\n", __debug__, filenames[0], read_length, self->priv->mds_length);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        succeeded = FALSE;
        goto end;
    }

    /* Parse MDS file */
    cur_ptr = self->priv->mds_data;

    self->priv->header = MIRAGE_CAST_PTR(cur_ptr, 0, MDS_Header *);
    cur_ptr += sizeof(MDS_Header);

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
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  DPM blocks offset: 0x%X\n", __debug__, self->priv->header->dpm_blocks_offset);

    switch (self->priv->header->medium_type) {
        case MDS_MEDIUM_CD:
        case MDS_MEDIUM_CD_R:
        case MDS_MEDIUM_CD_RW: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD-ROM image\n", __debug__);
            mirage_disc_set_medium_type(MIRAGE_DISC(self->priv->disc), MIRAGE_MEDIUM_CD, NULL);
            succeeded = mirage_parser_mds_load_disc(self, error);
            break;
        }
        case MDS_MEDIUM_DVD:
        case MDS_MEDIUM_DVD_MINUS_R: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: DVD-ROM image\n", __debug__);
            mirage_disc_set_medium_type(MIRAGE_DISC(self->priv->disc), MIRAGE_MEDIUM_DVD, NULL);
            succeeded = mirage_parser_mds_load_disc(self, error);
            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: medium of type 0x%X not supported yet!\n", __debug__, self->priv->header->medium_type);
            mirage_error(MIRAGE_E_NOTIMPL, error);
            succeeded = FALSE;
            break;
        }
    }

end:
    g_free(self->priv->mds_data);
    self->priv->mds_data = NULL;
    
    /* Return disc */
    mirage_object_detach_child(MIRAGE_OBJECT(self), self->priv->disc, NULL);
    if (succeeded) {
        *disc = self->priv->disc;
    } else {
        g_object_unref(self->priv->disc);
        *disc = NULL;
    }

    return succeeded;
}


/**********************************************************************\
 *                             Object init                            * 
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE(MIRAGE_Parser_MDS, mirage_parser_mds, MIRAGE_TYPE_PARSER);

void mirage_parser_mds_type_register (GTypeModule *type_module)
{
    return mirage_parser_mds_register_type(type_module);
}


static void mirage_parser_mds_init (MIRAGE_Parser_MDS *self)
{
    self->priv = MIRAGE_PARSER_MDS_GET_PRIVATE(self);

    mirage_parser_generate_parser_info(MIRAGE_PARSER(self),
        "PARSER-MDS",
        "MDS Image Parser",
        "MDS (Media descriptor) images",
        "application/x-mds"
    );

    self->priv->mds_filename = NULL;
    self->priv->mds_data = NULL;
}

static void mirage_parser_mds_finalize (GObject *gobject)
{
    MIRAGE_Parser_MDS *self = MIRAGE_PARSER_MDS(gobject);

    g_free(self->priv->mds_filename);
    g_free(self->priv->mds_data);
    
    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_parser_mds_parent_class)->finalize(gobject);
}

static void mirage_parser_mds_class_init (MIRAGE_Parser_MDSClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MIRAGE_ParserClass *parser_class = MIRAGE_PARSER_CLASS(klass);

    gobject_class->finalize = mirage_parser_mds_finalize;

    parser_class->load_image = mirage_parser_mds_load_image;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Parser_MDSPrivate));
}

static void mirage_parser_mds_class_finalize (MIRAGE_Parser_MDSClass *klass G_GNUC_UNUSED)
{
}
