/*
 *  libMirage: B6T image parser: Parser object
 *  Copyright (C) 2007-2012 Rok Mandeljc
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

#include "image-b6t.h"

#define __debug__ "B6T-Parser"

/* Self-explanatory */
#define WHINE_ON_UNEXPECTED(field, expected) \
    if (field != expected) MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unexpected value in field %s: expected 0x%X, got 0x%X\n", __debug__, #field, expected, field);


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_PARSER_B6T_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_B6T, MIRAGE_Parser_B6TPrivate))

struct _MIRAGE_Parser_B6TPrivate
{
    GObject *disc;

    gchar *b6t_filename;
    guint64 b6t_length;
    guint8 *b6t_data;

    guint8 *cur_ptr;

    B6T_DiscBlock_1 *disc_block_1;
    B6T_DiscBlock_2 *disc_block_2;
    guint8 *cdtext_data;

    gint32 prev_session_end;

    GList *data_blocks_list;
} ;


/**********************************************************************\
 *                    Endian-conversion functions                     *
\**********************************************************************/
static inline void b6t_disc_block_1_fix_endian (B6T_DiscBlock_1 *block)
{
    block->__dummy1__ = GUINT32_FROM_LE(block->__dummy1__);
    block->__dummy2__ = GUINT32_FROM_LE(block->__dummy2__);
    block->__dummy3__ = GUINT32_FROM_LE(block->__dummy3__);
    block->__dummy4__ = GUINT32_FROM_LE(block->__dummy4__);

    block->__dummy5__ = GUINT32_FROM_LE(block->__dummy5__);
    block->__dummy6__ = GUINT32_FROM_LE(block->__dummy6__);
    block->__dummy7__ = GUINT32_FROM_LE(block->__dummy7__);
    block->__dummy8__ = GUINT32_FROM_LE(block->__dummy8__);

    block->disc_type = GUINT16_FROM_LE(block->disc_type);

    block->num_sessions = GUINT16_FROM_LE(block->num_sessions);
    block->__dummy9__ = GUINT32_FROM_LE(block->__dummy9__);
    block->__dummy10__ = GUINT32_FROM_LE(block->__dummy10__);
    block->__dummy11__ = GUINT32_FROM_LE(block->__dummy11__);

    block->__dummy14__ = GUINT32_FROM_LE(block->__dummy14__);
    block->__dummy15__ = GUINT32_FROM_LE(block->__dummy15__);
    block->__dummy16__ = GUINT32_FROM_LE(block->__dummy16__);
    block->__dummy17__ = GUINT32_FROM_LE(block->__dummy17__);

    block->pma_data_length = GUINT16_FROM_LE(block->pma_data_length);
    block->atip_data_length = GUINT16_FROM_LE(block->atip_data_length);
    block->cdtext_data_length = GUINT16_FROM_LE(block->cdtext_data_length);
    block->cdrom_info_length = GUINT16_FROM_LE(block->cdrom_info_length);
    block->dvdrom_bca_length = GUINT32_FROM_LE(block->dvdrom_bca_length);
    block->__dummy19__ = GUINT32_FROM_LE(block->__dummy19__);

    block->__dummy20__ = GUINT32_FROM_LE(block->__dummy20__);
    block->__dummy21__ = GUINT32_FROM_LE(block->__dummy21__);
    block->dvdrom_structures_length = GUINT32_FROM_LE(block->dvdrom_structures_length);
    block->dvdrom_info_length = GUINT32_FROM_LE(block->dvdrom_info_length);
}

static inline void b6t_disc_block_2_fix_endian (B6T_DiscBlock_2 *block)
{
    block->mode_page_2a_length = GUINT32_FROM_LE(block->mode_page_2a_length);
    block->unknown1_length = GUINT32_FROM_LE(block->unknown1_length);
    block->datablocks_length = GUINT32_FROM_LE(block->datablocks_length);
    block->sessions_length = GUINT32_FROM_LE(block->sessions_length);
    block->dpm_data_length = GUINT32_FROM_LE(block->dpm_data_length);
}

static inline void b6t_data_block_fix_endian (B6T_DataBlock *block)
{
    block->type = GUINT32_FROM_LE(block->type);
    block->length_bytes = GUINT32_FROM_LE(block->length_bytes);
    block->__dummy1__ = GUINT32_FROM_LE(block->__dummy1__);
    block->__dummy2__ = GUINT32_FROM_LE(block->__dummy2__);
    block->__dummy3__ = GUINT32_FROM_LE(block->__dummy3__);
    block->__dummy4__ = GUINT32_FROM_LE(block->__dummy4__);
    block->offset = GUINT32_FROM_LE(block->offset);
    block->__dummy5__ = GUINT32_FROM_LE(block->__dummy5__);
    block->__dummy6__ = GUINT32_FROM_LE(block->__dummy6__);
    block->__dummy7__ = GUINT32_FROM_LE(block->__dummy7__);
    block->start_sector = GINT32_FROM_LE(block->start_sector);
    block->length_sectors = GINT32_FROM_LE(block->length_sectors);
    block->filename_length = GUINT32_FROM_LE(block->filename_length);
    block->__dummy8__ = GUINT32_FROM_LE(block->__dummy8__);
}

static inline void b6t_session_fix_endian (B6T_Session *session)
{
    session->number = GUINT16_FROM_LE(session->number);

    session->session_start = GUINT32_FROM_LE(session->session_start);

    session->session_end = GUINT32_FROM_LE(session->session_end);

    session->first_track = GUINT16_FROM_LE(session->first_track);
    session->last_track = GUINT16_FROM_LE(session->last_track);
}

static inline void b6t_track_fix_endian (B6T_Track *track)
{
    track->__dummy4__ = GUINT32_FROM_LE(track->__dummy4__);

    track->pregap = GUINT32_FROM_LE(track->pregap);

    track->__dummy8__ = GUINT32_FROM_LE(track->__dummy8__);
    track->__dummy9__ = GUINT32_FROM_LE(track->__dummy9__);
    track->__dummy10__ = GUINT32_FROM_LE(track->__dummy10__);
    track->__dummy11__ = GUINT32_FROM_LE(track->__dummy11__);

    track->start_sector = GINT32_FROM_LE(track->start_sector);
    track->length = GINT32_FROM_LE(track->length);

    track->__dummy12__ = GUINT32_FROM_LE(track->__dummy12__);
    track->__dummy13__ = GUINT32_FROM_LE(track->__dummy13__);
    track->session_number = GUINT32_FROM_LE(track->session_number);

    track->__dummy14__ = GUINT32_FROM_LE(track->__dummy14__);
}

static inline void widechar_filename_fix_endian (gunichar2 *filename, gint len)
{
    gint i;
    for (i = 0; i < len; i++) {
        filename[i] = GUINT16_FROM_LE(filename[i]);
    }
}

static inline void dpm_data_fix_endian (guint32 *dpm_data, guint32 num_entries)
{
    guint i;
    for (i = 0; i < num_entries; i++) {
        dpm_data[i] = GUINT32_FROM_LE(dpm_data[i]);
    }
}


/**********************************************************************\
 *                          Parsing functions                         *
\**********************************************************************/
static gboolean mirage_parser_b6t_load_bwa_file (MIRAGE_Parser_B6T *self, GError **error)
{
    gchar *bwa_filename;
    gchar *bwa_fullpath;

    /* Use B6T filename and replace its extension with BWA */
    bwa_filename = g_strdup(self->priv->b6t_filename);
    gint len = strlen(bwa_filename);
    sprintf(bwa_filename+len-3, "bwa");

    bwa_fullpath = mirage_helper_find_data_file(bwa_filename, self->priv->b6t_filename);
    g_free(bwa_filename);

    if (bwa_fullpath) {
        GObject *stream;
        guint64 bwa_length, read_length;
        guint8 *bwa_data;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: found BWA file: '%s'\n", __debug__, bwa_fullpath);

        /* Open BWA file */
        stream = libmirage_create_file_stream(bwa_fullpath, G_OBJECT(self), error);
        g_free(bwa_fullpath);

        if (!stream) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open stream on BWA file!\n", __debug__);
            return FALSE;
        }

        /* Read whole BWA file */
        g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_END, NULL, NULL);
        bwa_length = g_seekable_tell(G_SEEKABLE(stream));

        bwa_data = g_malloc(bwa_length);

        g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);
        read_length = g_input_stream_read(G_INPUT_STREAM(stream), bwa_data, bwa_length, NULL, NULL);

        g_object_unref(stream);

        if (read_length != bwa_length) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read whole BWA file!\n", __debug__);
            g_free(bwa_data);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DATA_FILE_ERROR, "Failed to read whole BWA file!");
            return FALSE;
        } else {
            guint8 *cur_ptr = bwa_data;

            guint32 __dummy1__;
            guint32 __dummy2__;
            guint32 __dummy3__;

            guint32 dpm_start_sector;
            guint32 dpm_resolution;
            guint32 dpm_num_entries;

            guint32 *dpm_data;

            /* First three fields seem to have fixed values; probably some sort of
               header? */
            __dummy1__ = GUINT32_FROM_LE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
            cur_ptr += sizeof(guint32);

            __dummy2__ = GUINT32_FROM_LE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
            cur_ptr += sizeof(guint32);

            __dummy3__ = GUINT32_FROM_LE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
            cur_ptr += sizeof(guint32);

            WHINE_ON_UNEXPECTED(__dummy1__, 0x00000001);
            WHINE_ON_UNEXPECTED(__dummy2__, 0x00000008);
            WHINE_ON_UNEXPECTED(__dummy3__, 0x00000001);

            /* The next three fields are start sector, resolution and number
               of entries */
            dpm_start_sector = GUINT32_FROM_LE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
            cur_ptr += sizeof(guint32);

            dpm_resolution = GUINT32_FROM_LE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
            cur_ptr += sizeof(guint32);

            dpm_num_entries = GUINT32_FROM_LE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
            cur_ptr += sizeof(guint32);

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: start sector: 0x%X\n", __debug__, dpm_start_sector);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: resolution: %d\n", __debug__, dpm_resolution);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of entries: %d\n", __debug__, dpm_num_entries);

            /* The rest is DPM data */
            dpm_data = MIRAGE_CAST_PTR(cur_ptr, 0, guint32 *);
            dpm_data_fix_endian(dpm_data, dpm_num_entries);

            /* Set DPM data */
            mirage_disc_set_dpm_data(MIRAGE_DISC(self->priv->disc), dpm_start_sector, dpm_resolution, dpm_num_entries, dpm_data);
        }

        g_free(bwa_data);
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: no accompanying BWA file found\n", __debug__);
    }

    return TRUE;
}

static void mirage_parser_b6t_parse_internal_dpm_data (MIRAGE_Parser_B6T *self)
{
    if (self->priv->disc_block_2->dpm_data_length) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading internal DPM data; 0x%X bytes\n", __debug__, self->priv->disc_block_2->dpm_data_length);

        guint8 *cur_ptr = self->priv->cur_ptr;

        /* FIXME: by the look of it, this should actually support multiple blocks? */
        guint32 __dummy1__;
        guint32 __dummy2__;
        guint32 __dummy3__;
        guint32 __dummy4__;
        guint32 block_len1;
        guint32 block_len2;
        guint32 __dummy5__;
        guint32 __dummy6__;

        guint32 dpm_start_sector;
        guint32 dpm_resolution;
        guint32 dpm_num_entries;

        guint32 *dpm_data = NULL;

        /* Four fields that seem to have fixed values */
        __dummy1__ = GUINT32_FROM_LE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
        cur_ptr += sizeof(guint32);
        __dummy2__ = GUINT32_FROM_LE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
        cur_ptr += sizeof(guint32);
        __dummy3__ = GUINT32_FROM_LE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
        cur_ptr += sizeof(guint32);
        __dummy4__ = GUINT32_FROM_LE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
        cur_ptr += sizeof(guint32);

        WHINE_ON_UNEXPECTED(__dummy1__, 0x00000001);
        WHINE_ON_UNEXPECTED(__dummy2__, 0x00000001);
        WHINE_ON_UNEXPECTED(__dummy3__, 0x00000000);
        WHINE_ON_UNEXPECTED(__dummy4__, 0x00000000);

        /* Next two fields seem to have same value, which appears to be length
           of DPM data */
        block_len1 = GUINT32_FROM_LE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
        cur_ptr += sizeof(guint32);
        block_len2 = GUINT32_FROM_LE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
        cur_ptr += sizeof(guint32);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: DPM block length: 0x%X, 0x%X (numbers should be same)\n", __debug__, block_len1, block_len2);

        /* Two more undeciphered fields */
        __dummy5__ = GUINT32_FROM_LE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
        cur_ptr += sizeof(guint32);
        __dummy6__ = GUINT32_FROM_LE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
        cur_ptr += sizeof(guint32);

        WHINE_ON_UNEXPECTED(__dummy5__, 0x00000000);
        WHINE_ON_UNEXPECTED(__dummy6__, 0x00000001);

        /* The next three fields are start sector, resolution and number
            of entries */
        dpm_start_sector = GUINT32_FROM_LE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
        cur_ptr += sizeof(guint32);

        dpm_resolution = GUINT32_FROM_LE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
        cur_ptr += sizeof(guint32);

        dpm_num_entries = GUINT32_FROM_LE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
        cur_ptr += sizeof(guint32);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: start sector: 0x%X\n", __debug__, dpm_start_sector);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: resolution: %d\n", __debug__, dpm_resolution);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of entries: %d\n", __debug__, dpm_num_entries);

        /* The rest is DPM data */
        dpm_data = MIRAGE_CAST_PTR(cur_ptr, 0, guint32 *);
        dpm_data_fix_endian(dpm_data, dpm_num_entries);
        cur_ptr += dpm_num_entries * sizeof(guint32);

        /* Set DPM data */
        mirage_disc_set_dpm_data(MIRAGE_DISC(self->priv->disc), dpm_start_sector, dpm_resolution, dpm_num_entries, dpm_data);

        /* Calculate length of data we've processed */
        gsize length = (gsize)cur_ptr - (gsize)self->priv->cur_ptr;
        if (length != self->priv->disc_block_2->dpm_data_length) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: we read 0x%zX bytes, declared size is 0x%X bytes, expect trouble!\n", __debug__, length, self->priv->disc_block_2->dpm_data_length);
        }

        /* Skip the whole block, so that parsing errors here aren't fatal */
        self->priv->cur_ptr += self->priv->disc_block_2->dpm_data_length;
    }
}

static gboolean mirage_parser_b6t_setup_track_fragments (MIRAGE_Parser_B6T *self, GObject *cur_track, gint start_sector, gint length, GError **error)
{
    GList *entry;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting up data blocks for track starting at sector 0x%X (%i), length 0x%X\n", __debug__, start_sector, start_sector, length);

    /* Data blocks are nice concept that's similar to libMirage's fragments; they
       are chunks of continuous data, stored in the same file and read in the same
       mode. To illustrate how it goes, consider a disc with single Mode 1 data track
       and three Audio tracks - data track will be stored in one data block, and the
       three audio tracks in another, but both data blocks will be stored in the same
       file, just at different offsets. Note that since they are separated, one data
       block could include subchannel and the other not. On the other hand, consider
       now a couple-of-GB DVD image. It will get split into 2 GB files, each being
       its own block. But all the blocks will make up a single track. So contrary to
       libMirage's fragments, where fragments are subunits of tracks, it is possible
       for single data block to contain data for multiple tracks. */
    G_LIST_FOR_EACH(entry, self->priv->data_blocks_list) {
        B6T_DataBlock *data_block = entry->data;

        if (start_sector >= data_block->start_sector && start_sector < data_block->start_sector + data_block->length_sectors) {
            gint tmp_length;
            gchar *filename;
            GObject *data_fragment;

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: found a block %i\n", __debug__, g_list_position(self->priv->data_blocks_list, entry));

            tmp_length = MIN(length, data_block->length_sectors);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: using length: 0x%X\n", __debug__, tmp_length);

            /* Find filename */
            filename = mirage_helper_find_data_file(data_block->filename, self->priv->b6t_filename);
            if (!filename) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to find data file '%s'\n", __debug__, data_block->filename);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Failed to find data file '%s'!", data_block->filename);
                return FALSE;
            }
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: using data file: %s\n", __debug__, filename);

            /* We'd like a BINARY fragment */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating BINARY fragment\n", __debug__);
            data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FRAG_IFACE_BINARY, filename, error);
            if (!data_fragment) {
                g_free(filename);
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create BINARY fragment!\n", __debug__);
                return FALSE;
            }

            gint tfile_sectsize = 0;
            gint tfile_format = 0;
            guint64 tfile_offset = 0;

            gint sfile_format = 0;
            gint sfile_sectsize = 0;

            /* We calculate sector size... */
            tfile_sectsize = data_block->length_bytes/data_block->length_sectors;
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track file sector size: %i (0x%X)\n", __debug__, tfile_sectsize, tfile_sectsize);
            /* Use sector size to calculate offset */
            tfile_offset = data_block->offset + (start_sector - data_block->start_sector)*tfile_sectsize;
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track file offset: 0x%llX\n", __debug__, tfile_offset);
            /* Adjust sector size to account for subchannel */
            if (tfile_sectsize > 2352) {
                /* If it's more than full sector, we have subchannel */
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track file sector size implies subchannel data...\n", __debug__);
                sfile_sectsize = tfile_sectsize - 2352;
                tfile_sectsize = 2352;
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: subchannel sector size: %i (0x%X)\n", __debug__, sfile_sectsize, sfile_sectsize);
                switch (sfile_sectsize) {
                    case 16: {
                        /* Internal subchannel, PQ */
                        sfile_format = FR_BIN_SFILE_PQ16 | FR_BIN_SFILE_INT;
                        break;
                    }
                    case 96: {
                        /* Internal subchannel, linear PW96 */
                        sfile_format = FR_BIN_SFILE_PW96_LIN | FR_BIN_SFILE_INT;
                        break;
                    }
                    default: {
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unhandled subchannel sector size: %i (0x%X)\n", __debug__, sfile_sectsize, sfile_sectsize);
                        break;
                    }
                }
            }

            /* Data format */
            if ((data_block->type & 0x00008000) == 0x00008000) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: data block is for audio data\n", __debug__);
                tfile_format = FR_BIN_TFILE_AUDIO;
            } else {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: data block is for data track\n", __debug__);
                tfile_format = FR_BIN_TFILE_DATA;
            }

            /* Set file */
            if (!mirage_frag_iface_binary_track_file_set_file(MIRAGE_FRAG_IFACE_BINARY(data_fragment), filename, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set track data file!\n", __debug__);
                g_free(filename);
                return FALSE;
            }
            g_free(filename);

            mirage_frag_iface_binary_track_file_set_sectsize(MIRAGE_FRAG_IFACE_BINARY(data_fragment), tfile_sectsize);
            mirage_frag_iface_binary_track_file_set_offset(MIRAGE_FRAG_IFACE_BINARY(data_fragment), tfile_offset);
            mirage_frag_iface_binary_track_file_set_format(MIRAGE_FRAG_IFACE_BINARY(data_fragment), tfile_format);

            mirage_frag_iface_binary_subchannel_file_set_sectsize(MIRAGE_FRAG_IFACE_BINARY(data_fragment), sfile_sectsize);
            mirage_frag_iface_binary_subchannel_file_set_format(MIRAGE_FRAG_IFACE_BINARY(data_fragment), sfile_format);

            mirage_fragment_set_length(MIRAGE_FRAGMENT(data_fragment), tmp_length);

            /* Add fragment */
            mirage_track_add_fragment(MIRAGE_TRACK(cur_track), -1, data_fragment);
            g_object_unref(data_fragment);

            /* Calculate remaining track length */
            length -= tmp_length;

            if (length == 0) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: used up all length, breaking\n", __debug__);
                break;
            } else {
                /* Calculate start sector for the remainder */
                start_sector += tmp_length;
            }
        }
    }

    return TRUE;
}

static gboolean mirage_parser_b6t_parse_header (MIRAGE_Parser_B6T *self, GError **error)
{
    gchar *header;

    /* Read header (16 bytes) */
    header = MIRAGE_CAST_PTR(self->priv->cur_ptr, 0, gchar *);
    self->priv->cur_ptr += 16;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: file header: %.16s\n", __debug__, header);

    /* Make sure it's correct one */
    if (memcmp(header, "BWT5 STREAM SIGN", 16)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: invalid header!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Invalid header!");
        return FALSE;
    }

    return TRUE;
}


static gboolean mirage_parser_b6t_parse_pma_data (MIRAGE_Parser_B6T *self, GError **error G_GNUC_UNUSED)
{
    if (self->priv->disc_block_1->pma_data_length) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: PMA data not used yet; skipping 0x%X bytes\n", __debug__, self->priv->disc_block_1->pma_data_length);
        self->priv->cur_ptr += self->priv->disc_block_1->pma_data_length;
    }

    return TRUE;
}

static gboolean mirage_parser_b6t_parse_atip_data (MIRAGE_Parser_B6T *self, GError **error G_GNUC_UNUSED)
{
    if (self->priv->disc_block_1->atip_data_length) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: ATIP data not used yet; skipping 0x%X bytes\n", __debug__, self->priv->disc_block_1->atip_data_length);
        self->priv->cur_ptr += self->priv->disc_block_1->atip_data_length;
    }

    return TRUE;
}

static gboolean mirage_parser_b6t_parse_cdtext_data (MIRAGE_Parser_B6T *self, GError **error G_GNUC_UNUSED)
{
    if (self->priv->disc_block_1->cdtext_data_length) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading CD-TEXT data; 0x%X bytes\n", __debug__, self->priv->disc_block_1->cdtext_data_length);
        /* Read; we don't set data here, because at this point we don't have
           parser layout set up yet */
        self->priv->cdtext_data = MIRAGE_CAST_PTR(self->priv->cur_ptr, 0, guint8 *);
        self->priv->cur_ptr += self->priv->disc_block_1->cdtext_data_length;
    }

    return TRUE;
}

static void mirage_parser_b6t_parse_bca (MIRAGE_Parser_B6T *self)
{
    if (self->priv->disc_block_1->dvdrom_bca_length) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading BCA data; 0x%X bytes\n", __debug__, self->priv->disc_block_1->dvdrom_bca_length);

        /* Read */
        guint8 *bca_data = MIRAGE_CAST_PTR(self->priv->cur_ptr, 0, guint8 *);
        self->priv->cur_ptr += self->priv->disc_block_1->dvdrom_bca_length;

        mirage_disc_set_disc_structure(MIRAGE_DISC(self->priv->disc), 0, 0x0003, bca_data, self->priv->disc_block_1->dvdrom_bca_length);
    }
}

static void mirage_parser_b6t_parse_dvd_structures (MIRAGE_Parser_B6T *self)
{
    gint length = 0;

    /* Return if there's nothing to do */
    if (!self->priv->disc_block_1->dvdrom_structures_length) {
        return;
    }

    /* Hmm... it seems there are two bytes set to 0 preceeding the structures */
    guint16 dummy = GUINT16_FROM_LE(MIRAGE_CAST_DATA(self->priv->cur_ptr, 0, guint16));
    self->priv->cur_ptr += sizeof(guint16);
    WHINE_ON_UNEXPECTED(dummy, 0);
    length = sizeof(dummy);

    while (length < self->priv->disc_block_1->dvdrom_structures_length) {
        /* It seems DVD structures are stored in following format:
             - 2 bytes, holding structure number
             - 4 bytes, holding data header as returned by READ PARSER STRUCTURE
               command (i.e. 2 bytes of data length and 2 reserved bytes)
             - actual data
        */
        guint16 struct_number;
        guint16 struct_length;
        guint16 struct_reserved;
        guint8 *struct_data;

        /* Read structure number and data length */
        struct_number = GUINT16_FROM_LE(MIRAGE_CAST_DATA(self->priv->cur_ptr, 0, guint16));
        self->priv->cur_ptr += sizeof(guint16);

        /* Length in header is big-endian; and it also includes two reserved
           bytes following it, so make sure this is accounted for */
        struct_length = GUINT16_FROM_BE(MIRAGE_CAST_DATA(self->priv->cur_ptr, 0, guint16));
        self->priv->cur_ptr += sizeof(guint16);
        struct_length -= 2;

        struct_reserved = GUINT16_FROM_LE(MIRAGE_CAST_DATA(self->priv->cur_ptr, 0, guint16));
        self->priv->cur_ptr += sizeof(guint16);
        WHINE_ON_UNEXPECTED(struct_reserved, 0x0000);

        /* Allocate buffer and read data */
        struct_data = MIRAGE_CAST_PTR(self->priv->cur_ptr, 0, guint8 *);
        self->priv->cur_ptr += struct_length;

        /* Set structure */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: structure 0x%X, length: 0x%X\n", __debug__, struct_number, struct_length);
        mirage_disc_set_disc_structure(MIRAGE_DISC(self->priv->disc), 0, struct_number, struct_data, struct_length);

        length += sizeof(struct_number) + sizeof(struct_length) + sizeof(struct_reserved) + struct_length;
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: read %d out of %d bytes...\n", __debug__, length, self->priv->disc_block_1->dvdrom_structures_length);
    }
}


static gboolean mirage_parser_b6t_decode_disc_type (MIRAGE_Parser_B6T *self, GError **error)
{
    switch (self->priv->disc_block_1->disc_type) {
        case 0x08: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD-ROM disc\n", __debug__);
            mirage_disc_set_medium_type(MIRAGE_DISC(self->priv->disc), MIRAGE_MEDIUM_CD);
            break;
        }
        case 0x09: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD-R disc\n", __debug__);
            mirage_disc_set_medium_type(MIRAGE_DISC(self->priv->disc), MIRAGE_MEDIUM_CD);
            break;
        }
        case 0x0A: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD-RW disc\n", __debug__);
            mirage_disc_set_medium_type(MIRAGE_DISC(self->priv->disc), MIRAGE_MEDIUM_CD);
            break;
        }
        case 0x10: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: DVD-ROM disc\n", __debug__);
            mirage_disc_set_medium_type(MIRAGE_DISC(self->priv->disc), MIRAGE_MEDIUM_DVD);
            break;
        }
        case 0x11: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: DVD-R disc\n", __debug__);
            mirage_disc_set_medium_type(MIRAGE_DISC(self->priv->disc), MIRAGE_MEDIUM_DVD);
            break;
        }
        case 0x12: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: DVD-RAM disc\n", __debug__);
            mirage_disc_set_medium_type(MIRAGE_DISC(self->priv->disc), MIRAGE_MEDIUM_DVD);
            break;
        }
        case 0x13: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: DVD-RW Restricted Overwrite disc\n", __debug__);
            mirage_disc_set_medium_type(MIRAGE_DISC(self->priv->disc), MIRAGE_MEDIUM_DVD);
            break;
        }
        case 0x14: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: DVD-RW Sequential Recording disc\n", __debug__);
            mirage_disc_set_medium_type(MIRAGE_DISC(self->priv->disc), MIRAGE_MEDIUM_DVD);
            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown disc type: 0x%X!\n", __debug__, self->priv->disc_block_1->disc_type);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Unknown disc type 0x%X!", self->priv->disc_block_1->disc_type);
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean mirage_parser_b6t_parse_disc_blocks (MIRAGE_Parser_B6T *self, GError **error)
{
    /* 112 bytes a.k.a. first parser block */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading 'disc block 1'\n", __debug__);
    self->priv->disc_block_1 = MIRAGE_CAST_PTR(self->priv->cur_ptr, 0, B6T_DiscBlock_1 *);
    b6t_disc_block_1_fix_endian(self->priv->disc_block_1);
    self->priv->cur_ptr += sizeof(B6T_DiscBlock_1);

    /* Since most of these fields are not deciphered yet, watch out for
       deviations from 'usual' values */
    WHINE_ON_UNEXPECTED(self->priv->disc_block_1->__dummy1__, 0x00000002);
    WHINE_ON_UNEXPECTED(self->priv->disc_block_1->__dummy2__, 0x00000002);
    WHINE_ON_UNEXPECTED(self->priv->disc_block_1->__dummy3__, 0x00000006);
    WHINE_ON_UNEXPECTED(self->priv->disc_block_1->__dummy4__, 0x00000000);
    WHINE_ON_UNEXPECTED(self->priv->disc_block_1->__dummy5__, 0x00000000);
    WHINE_ON_UNEXPECTED(self->priv->disc_block_1->__dummy6__, 0x00000000);
    WHINE_ON_UNEXPECTED(self->priv->disc_block_1->__dummy7__, 0x00000000);
    WHINE_ON_UNEXPECTED(self->priv->disc_block_1->__dummy8__, 0x00000000);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  disc type: 0x%X\n", __debug__, self->priv->disc_block_1->disc_type);
    mirage_parser_b6t_decode_disc_type(self, error);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  number of sessions: %i\n", __debug__, self->priv->disc_block_1->num_sessions);
    WHINE_ON_UNEXPECTED(self->priv->disc_block_1->__dummy9__, 0x00000002);
    WHINE_ON_UNEXPECTED(self->priv->disc_block_1->__dummy10__, 0x00000000);
    WHINE_ON_UNEXPECTED(self->priv->disc_block_1->__dummy11__, 0x00000000);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  MCN valid: 0x%X\n", __debug__, self->priv->disc_block_1->mcn_valid);
    if (self->priv->disc_block_1->mcn_valid) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  MCN: %.13s\n", __debug__, self->priv->disc_block_1->mcn);
    }
    WHINE_ON_UNEXPECTED(self->priv->disc_block_1->__dummy12__, 0x00000000);
    WHINE_ON_UNEXPECTED(self->priv->disc_block_1->__dummy13__, 0x00000000);
    WHINE_ON_UNEXPECTED(self->priv->disc_block_1->__dummy14__, 0x00000000);
    WHINE_ON_UNEXPECTED(self->priv->disc_block_1->__dummy15__, 0x00000000);
    WHINE_ON_UNEXPECTED(self->priv->disc_block_1->__dummy16__, 0x00000000);
    WHINE_ON_UNEXPECTED(self->priv->disc_block_1->__dummy17__, 0x00000000);
    if (self->priv->disc_block_1->disc_type == 0x08) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  PMA data length: 0x%X\n", __debug__, self->priv->disc_block_1->pma_data_length);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  ATIP data length: 0x%X\n", __debug__, self->priv->disc_block_1->atip_data_length);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  CD-TEXT data length: 0x%X\n", __debug__, self->priv->disc_block_1->cdtext_data_length);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  CD-ROM info block length: 0x%X\n", __debug__, self->priv->disc_block_1->cdrom_info_length);
    } else {
        WHINE_ON_UNEXPECTED(self->priv->disc_block_1->pma_data_length, 0x0000);
        WHINE_ON_UNEXPECTED(self->priv->disc_block_1->atip_data_length, 0x0000);
        WHINE_ON_UNEXPECTED(self->priv->disc_block_1->cdtext_data_length, 0x0000);
        WHINE_ON_UNEXPECTED(self->priv->disc_block_1->cdrom_info_length, 0x0000);
    }
    WHINE_ON_UNEXPECTED(self->priv->disc_block_1->__dummy19__, 0x00000000);
    WHINE_ON_UNEXPECTED(self->priv->disc_block_1->__dummy20__, 0x00000000);
    WHINE_ON_UNEXPECTED(self->priv->disc_block_1->__dummy21__, 0x00000000);
    if (self->priv->disc_block_1->disc_type == 0x10) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  BCA length: 0x%X\n", __debug__, self->priv->disc_block_1->dvdrom_bca_length);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  parser structures length: 0x%X\n", __debug__, self->priv->disc_block_1->dvdrom_structures_length);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  DVD-ROM info block length: 0x%X\n", __debug__, self->priv->disc_block_1->dvdrom_info_length);
    } else {
        WHINE_ON_UNEXPECTED(self->priv->disc_block_1->dvdrom_bca_length, 0x00000000);
        WHINE_ON_UNEXPECTED(self->priv->disc_block_1->dvdrom_structures_length, 0x00000000);
        WHINE_ON_UNEXPECTED(self->priv->disc_block_1->dvdrom_info_length, 0x00000000);
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");


    /* 32 junk bytes; these seem to have some meaning for non-audio CDs... */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: skipping 32 undeciphered bytes\n\n", __debug__);
    self->priv->cur_ptr += 32;


    /* Next 28 bytes are drive identifiers; these are part of data returned by
       INQUIRY command */
    B6T_DriveIdentifiers *inquiry_id = MIRAGE_CAST_PTR(self->priv->cur_ptr, 0, B6T_DriveIdentifiers *);
    /* Note: only strings, nothing to fix endian-wise */
    self->priv->cur_ptr += sizeof(B6T_DriveIdentifiers);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: image was created with following drive:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  vendor: %.8s\n", __debug__, inquiry_id->vendor);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  product: %.16s\n", __debug__, inquiry_id->product);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  revision: %.4s\n", __debug__, inquiry_id->revision);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  vendor specific: %.20s\n\n", __debug__, inquiry_id->vendor_specific);


    /* Then there's 32 bytes of ISO volume descriptor; they represent volume ID,
       if it is a data CD, or they're set to AUDIO CD in case of audio CD */
    gchar *volume_id = MIRAGE_CAST_PTR(self->priv->cur_ptr, 0, gchar *);
    self->priv->cur_ptr += 32;
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: volume ID: %.32s\n\n", __debug__, volume_id);


    /* What comes next is 20 bytes that are seemingly organised into 32-bit
       integers... experimenting with different layouts show that these are
       indeed lengths of blocks that follow */
    self->priv->disc_block_2 = MIRAGE_CAST_PTR(self->priv->cur_ptr, 0, B6T_DiscBlock_2 *);
    b6t_disc_block_2_fix_endian(self->priv->disc_block_2);
    self->priv->cur_ptr += sizeof(B6T_DiscBlock_2);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading 'disc block 2'\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  mode page 0x2A data length: 0x%X\n", __debug__, self->priv->disc_block_2->mode_page_2a_length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  unknown block 1 data length: 0x%X\n", __debug__, self->priv->disc_block_2->unknown1_length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  data-blocks data length: 0x%X\n", __debug__, self->priv->disc_block_2->datablocks_length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  sessions data length: 0x%X\n", __debug__, self->priv->disc_block_2->sessions_length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  DPM data length: 0x%X\n\n", __debug__, self->priv->disc_block_2->dpm_data_length);


    /* Right, you thought so far everything was pretty much straightforward? Well,
       fear not... It would seem that data to follow are blocks whose lengths were
       defined in both disc_block_1 and disc_block_2 - and to make things worse,
       they're mixed... */


    /* Mode Page 0x2A: CD/DVD Capabilities and Mechanical Status Mode Page...
       this one is returned in response to MODE SENSE (10) that Blindwrite issues;
       note that the page per-se doesn't have any influence on image data, and it's
       probably included just for diagnostics or somesuch. Therefore we won't be
       reading it (maybe later, just to dump the data...) */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: skipping Mode Page 0x2A (0x%X bytes)\n\n", __debug__, self->priv->disc_block_2->mode_page_2a_length);
    self->priv->cur_ptr += self->priv->disc_block_2->mode_page_2a_length;

    /* Unknown data block #1... this one seems to be 4 bytes long in all images
       I've tested with, and set to 0. For now, skip it, but print a warning if
       it's not 4 bytes long */
    WHINE_ON_UNEXPECTED(self->priv->disc_block_2->unknown1_length, 4);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: skipping Unknown data block #1 (0x%X bytes)\n\n", __debug__, self->priv->disc_block_2->unknown1_length);
    self->priv->cur_ptr += self->priv->disc_block_2->unknown1_length;

    /* This is where PMA/ATIP/CD-TEXT data gets stored, in that order */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing PMA data...\n", __debug__);
    if (!mirage_parser_b6t_parse_pma_data(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse PMA data!\n", __debug__);
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished parsing PMA data\n\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing ATIP data...\n", __debug__);
    if (!mirage_parser_b6t_parse_atip_data(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse ATIP data!\n", __debug__);
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished parsing ATIP data\n\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing CD-TEXT data...\n", __debug__);
    if (!mirage_parser_b6t_parse_cdtext_data(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse CD-TEXT data!\n", __debug__);
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished parsing CD-TEXT data\n\n", __debug__);

    /* If we're dealing with DVD-ROM image, this is where BCA is stored, followed
       by parser structures... Since I don't think PMA/ATIP/CD-TEXT can be obtained
       from DVD-ROMs, it doesn't really matter which should come first (though,
       judging by order of length integers, I'd say this is correct order...) */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing BCA data...\n", __debug__);
    mirage_parser_b6t_parse_bca(self);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished parsing BCA data\n\n", __debug__);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing DVD structures...\n", __debug__);
    mirage_parser_b6t_parse_dvd_structures(self);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished parsing DVD structures\n\n", __debug__);

    /* Next 56 bytes (0x38) seem to be the block whose length seems to be declared
       in disc_block_1... The block is present in both CD-ROM and DVD-ROM images, but
       its length seems to be declared in different positions. I can't make much out
       of the data itself, expect that the last 8 bytes are verbatim copy of data
       returned by READ CAPACITY command. Again, this data is not really relevant,
       so we're skipping it... */
    if (self->priv->disc_block_1->disc_type == 0x08 || self->priv->disc_block_1->disc_type == 0x09 || self->priv->disc_block_1->disc_type == 0x0A) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: skipping CD-ROM disc info block (0x%X bytes)\n\n", __debug__, self->priv->disc_block_1->cdrom_info_length);
        self->priv->cur_ptr += self->priv->disc_block_1->cdrom_info_length;
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: skipping DVD-ROM disc info block (0x%X bytes)\n\n", __debug__, self->priv->disc_block_1->dvdrom_info_length);
        self->priv->cur_ptr += self->priv->disc_block_1->dvdrom_info_length;
    }

    return TRUE;
}

static gint sort_data_blocks (B6T_DataBlock *block1, B6T_DataBlock *block2)
{
    /* Data blocks should be ordered according to their start sectors */
    if (block1->start_sector < block2->start_sector) {
        return -1;
    } else if (block1->start_sector > block2->start_sector) {
        return 1;
    } else {
        return 0;
    }
}

static gboolean mirage_parser_b6t_parse_data_blocks (MIRAGE_Parser_B6T *self, GError **error G_GNUC_UNUSED)
{
    gsize length = 0;
    gint i;

    /* Store the current pointer (for length calculation) */
    length = (gsize)self->priv->cur_ptr;

    /* First four bytes are number of data blocks */
    guint32 num_data_blocks = GUINT32_FROM_LE(MIRAGE_CAST_DATA(self->priv->cur_ptr, 0, guint32));
    self->priv->cur_ptr += sizeof(guint32);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of data blocks: %i\n", __debug__, num_data_blocks);

    /* Then there's drive path; it seems it's awfully important to B6T image which
       drive it's been created on... it's irrelevant to us, so skip it */
    guint32 drive_path_length = GUINT32_FROM_LE(MIRAGE_CAST_DATA(self->priv->cur_ptr, 0, guint32));
    self->priv->cur_ptr += sizeof(guint32);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: skipping 0x%X bytes of drive path\n\n", __debug__, drive_path_length);
    self->priv->cur_ptr += drive_path_length;

    /* Now, the actual blocks; we need to copy these, because filename field
       needs to be changed */
    for (i = 0; i < num_data_blocks; i++) {
        B6T_DataBlock *data_block = g_new0(B6T_DataBlock, 1);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: data block #%i\n", __debug__, i);

        /* Read data block up to the filename */
        memcpy(data_block, MIRAGE_CAST_PTR(self->priv->cur_ptr, 0, B6T_DataBlock *), offsetof(B6T_DataBlock, filename));
        b6t_data_block_fix_endian(data_block);
        self->priv->cur_ptr += offsetof(B6T_DataBlock, filename);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  type: 0x%X\n", __debug__, data_block->type);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  length (bytes): 0x%X\n", __debug__, data_block->length_bytes);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  offset: 0x%X\n", __debug__, data_block->offset);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  start_sector: 0x%X\n", __debug__, data_block->start_sector);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  length (sectors): 0x%X\n", __debug__, data_block->length_sectors);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  filename_length: %i\n", __debug__, data_block->filename_length);

        /* Temporary UTF-16 filename... note that filename_length is actual
           length in bytes, not characters! */
        gunichar2 *tmp_filename = MIRAGE_CAST_PTR(self->priv->cur_ptr, 0, gunichar2 *);
        widechar_filename_fix_endian(tmp_filename, data_block->filename_length/2);
        self->priv->cur_ptr += data_block->filename_length;

        /* Convert filename */
        data_block->filename = g_utf16_to_utf8(tmp_filename, data_block->filename_length/2, NULL, NULL, NULL);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  filename: %s\n", __debug__, data_block->filename);

        /* Read the trailing four bytes */
        data_block->__dummy8__ = GUINT32_FROM_LE(MIRAGE_CAST_DATA(self->priv->cur_ptr, 0, guint32));
        self->priv->cur_ptr += sizeof(guint32);

        /* Add block to the list */
        self->priv->data_blocks_list = g_list_insert_sorted(self->priv->data_blocks_list, data_block, (GCompareFunc)sort_data_blocks);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    }

    /* Calculate length of data we've processed */
    length = (gsize)self->priv->cur_ptr - length;

    if (length != self->priv->disc_block_2->datablocks_length) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: I'm afraid Dave... we read 0x%zX bytes, declared size is 0x%X bytes (%d data blocks)\n", __debug__, length, self->priv->disc_block_2->datablocks_length, num_data_blocks);
    }

    return TRUE;
}

static gboolean mirage_parser_b6t_parse_track_entry (MIRAGE_Parser_B6T *self, GError **error)
{
    GObject *cur_track = NULL;
    B6T_Track *track = NULL;

    track = MIRAGE_CAST_PTR(self->priv->cur_ptr, 0, B6T_Track *);
    b6t_track_fix_endian(track);
    self->priv->cur_ptr += sizeof(B6T_Track);

    /* We have no use for non-track descriptors at the moment */
    if (track->type == 0) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: skipping non-track descriptor, point 0x%X\n", __debug__, track->point);
        return TRUE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: loading track descriptor:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   type: 0x%X\n", __debug__, track->type);
    if (track->type == 1 || track->type == 6) {
        /* 0 for Audio and DVD tracks */
        WHINE_ON_UNEXPECTED(track->__dummy1__, 0x00);
        WHINE_ON_UNEXPECTED(track->__dummy2__, 0x00);
        WHINE_ON_UNEXPECTED(track->__dummy3__, 0x00);
        WHINE_ON_UNEXPECTED(track->__dummy4__, 0x00000000);
    } else {
        WHINE_ON_UNEXPECTED(track->__dummy1__, 0x01);
        WHINE_ON_UNEXPECTED(track->__dummy2__, 0x01);
        WHINE_ON_UNEXPECTED(track->__dummy3__, 0x01);
        WHINE_ON_UNEXPECTED(track->__dummy4__, 0x00000001);
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   subchannel: 0x%X\n", __debug__, track->subchannel);
    WHINE_ON_UNEXPECTED(track->__dummy5__, 0x00);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   CTL: 0x%X\n", __debug__, track->ctl);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   ADR: 0x%X\n", __debug__, track->adr);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   point: 0x%X\n", __debug__, track->point);
    WHINE_ON_UNEXPECTED(track->__dummy6__, 0x00);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   MSF: %02i:%02i:%02i\n", __debug__, track->min, track->sec, track->frame);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   zero: %i\n", __debug__, track->zero);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   PMSF: %02i:%02i:%02i\n", __debug__, track->pmin, track->psec, track->pframe);
    if (track->type == 6) {
        /* 0 for DVD tracks */
        WHINE_ON_UNEXPECTED(track->__dummy7__, 0x00);
    } else {
        WHINE_ON_UNEXPECTED(track->__dummy7__, 0x01);
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   pregap length: 0x%X\n", __debug__, track->pregap);
    WHINE_ON_UNEXPECTED(track->__dummy8__, 0x00000000);
    WHINE_ON_UNEXPECTED(track->__dummy9__, 0x00000000);
    WHINE_ON_UNEXPECTED(track->__dummy10__, 0x00000000);
    WHINE_ON_UNEXPECTED(track->__dummy11__, 0x00000000);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   start sector: 0x%X\n", __debug__, track->start_sector);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   track length: 0x%X\n", __debug__, track->length);
    WHINE_ON_UNEXPECTED(track->__dummy12__, 0x00000000);
    WHINE_ON_UNEXPECTED(track->__dummy13__, 0x00000000);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   session number: 0x%X\n", __debug__, track->session_number);
    WHINE_ON_UNEXPECTED(track->__dummy14__, 0x0000);

    /* It seems only non-DVD track entries have additional 8 bytes */
    if (track->type != 6 && track->type != 0) {
        self->priv->cur_ptr += 8;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");

    /* Create track now; we'll add it directly to disc, with disc and track number we have */
    if (!mirage_disc_add_track_by_number(MIRAGE_DISC(self->priv->disc), track->point, &cur_track, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __debug__);
        return FALSE;
    }

    /* Track mode... Seems type field for track descriptor determines that:
        - 0: non-track descriptor
        - 1: Audio track
        - 2: Mode 1 track
        - 3: Mode 2 Formless track
        - 4: Mode 2 Form 1 track
        - 5: Mode 2 Form 2 track
        - 6: DVD track
    */
    switch (track->type) {
        case 1: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Audio track\n", __debug__);
            mirage_track_set_mode(MIRAGE_TRACK(cur_track), MIRAGE_MODE_AUDIO);
            break;
        }
        case 2: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Mode 1 track\n", __debug__);
            mirage_track_set_mode(MIRAGE_TRACK(cur_track), MIRAGE_MODE_MODE1);
            break;
        }
        case 3: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Mode 2 track\n", __debug__);
            mirage_track_set_mode(MIRAGE_TRACK(cur_track), MIRAGE_MODE_MODE2_MIXED);
            break;
        }
        case 4: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Mode 2 Form 1 track\n", __debug__);
            mirage_track_set_mode(MIRAGE_TRACK(cur_track), MIRAGE_MODE_MODE2_FORM1);
            break;
        }
        case 5: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Mode 2 Form 2 track\n", __debug__);
            mirage_track_set_mode(MIRAGE_TRACK(cur_track), MIRAGE_MODE_MODE2_FORM2);
            break;
        }
        case 6: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: DVD track\n", __debug__);
            mirage_track_set_mode(MIRAGE_TRACK(cur_track), MIRAGE_MODE_MODE1);
            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown track type: 0x%X!\n", __debug__, track->type);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Unknown track type 0x%X!", track->type);
            g_object_unref(cur_track);
            return FALSE;
        }
    }

    /* Set up track fragments */
    if (!mirage_parser_b6t_setup_track_fragments(self, cur_track, track->start_sector, track->length, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set up track's fragments!\n", __debug__);
        g_object_unref(cur_track);
        return FALSE;
    }

    /* Set track start */
    mirage_track_set_track_start(MIRAGE_TRACK(cur_track), track->pregap);

    g_object_unref(cur_track);
    return TRUE;
}

static gboolean mirage_parser_b6t_parse_session (MIRAGE_Parser_B6T *self, GError **error)
{
    gint i;
    B6T_Session *session;

    session = MIRAGE_CAST_PTR(self->priv->cur_ptr, 0, B6T_Session *);
    b6t_session_fix_endian(session);
    self->priv->cur_ptr += sizeof(B6T_Session);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: loading session:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   number: %i\n", __debug__, session->number);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   number of entries: %i\n", __debug__, session->num_entries);
    WHINE_ON_UNEXPECTED(session->__dummy1__, 3);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   start address: 0x%X\n", __debug__, session->session_start);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   end address: 0x%X\n", __debug__, session->session_end);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   first track: 0x%X\n", __debug__, session->first_track);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   end track: 0x%X\n\n", __debug__, session->last_track);

    /* If this is the first session, its starting address is also the starting address
       of the parser... if not, we need to set the length of lead-out of previous session
       (which would equal difference between previous end and current start address) */
    if (session->number == 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: first session; setting parser's start to 0x%X (%i)\n", __debug__, session->session_start, session->session_start);
        mirage_disc_layout_set_start_sector(MIRAGE_DISC(self->priv->disc), session->session_start);
    } else {
        guint32 leadout_length = session->session_start - self->priv->prev_session_end;
        GObject *prev_session = NULL;
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: previous session's leadout length: 0x%X (%i)\n", __debug__, leadout_length, leadout_length);

        if (!mirage_disc_get_session_by_number(MIRAGE_DISC(self->priv->disc), session->number - 1, &prev_session, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get previous session!\n", __debug__);
            return FALSE;
        }

        mirage_session_set_leadout_length(MIRAGE_SESSION(prev_session), leadout_length);

        g_object_unref(prev_session);
    }
    self->priv->prev_session_end = session->session_end;

    /* Add session */
    if (!mirage_disc_add_session_by_number(MIRAGE_DISC(self->priv->disc), session->number, NULL, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __debug__);
        return FALSE;
    }

    /* Load track entries */
    for (i = 0; i < session->num_entries; i++) {
        if (!mirage_parser_b6t_parse_track_entry(self, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse track entry #%i!\n", __debug__, i);
            return FALSE;
        }
    }


    return TRUE;
}

static gboolean mirage_parser_b6t_parse_sessions (MIRAGE_Parser_B6T *self, GError **error)
{
    gsize length = 0;
    gint i;

    /* Store the offset */
    length = (gsize)self->priv->cur_ptr;

    /* Load each session */
    for (i = 0; i < self->priv->disc_block_1->num_sessions; i++) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing session #%i...\n", __debug__, i);
        if (!mirage_parser_b6t_parse_session(self, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse session #%i!\n", __debug__, i);
            return FALSE;
        }
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished parsing session #%i\n\n", __debug__, i);
    }

    /* Calculate length of data we've processed */
    length = (gsize)self->priv->cur_ptr - length;

    if (length != self->priv->disc_block_2->sessions_length) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: I'm afraid Dave... we read 0x%zX bytes, declared size is 0x%X bytes\n", __debug__, length, self->priv->disc_block_2->sessions_length);
    }

    return TRUE;
}

static gboolean mirage_parser_b6t_parse_footer (MIRAGE_Parser_B6T *self, GError **error)
{
    /* Read footer */
    gchar *footer = MIRAGE_CAST_PTR(self->priv->cur_ptr, 0, gchar *);
    self->priv->cur_ptr += 16;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: file footer: %.16s\n", __debug__, footer);

    /* Make sure it's correct one */
    if (memcmp(footer, "BWT5 STREAM FOOT", 16)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: invalid footer!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Invalid footer!");
        return FALSE;
    }

    return TRUE;
}

static gboolean mirage_parser_b6t_load_disc (MIRAGE_Parser_B6T *self, GError **error)
{
    /* Start at the beginning */
    self->priv->cur_ptr = self->priv->b6t_data;

    /* Read header */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing header...\n", __debug__);
    if (!mirage_parser_b6t_parse_header(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse header!\n", __debug__);
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished parsing header\n\n", __debug__);

    /* Read disc blocks */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing disc blocks...\n", __debug__);
    if (!mirage_parser_b6t_parse_disc_blocks(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse disc blocks!\n", __debug__);
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished parsing disc blocks\n\n", __debug__);

    /* Read data blocks */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing data blocks...\n", __debug__);
    if (!mirage_parser_b6t_parse_data_blocks(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse data blocks!\n", __debug__);
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished parsing data blocks\n\n", __debug__);

    /* Read sessions */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing sessions...\n", __debug__);
    if (!mirage_parser_b6t_parse_sessions(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse sessions!\n", __debug__);
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished parsing sessions\n\n", __debug__);

    /* Read internal DPM data */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing internal DPM data...\n", __debug__);
    mirage_parser_b6t_parse_internal_dpm_data(self);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished parsing internal DPM data\n\n", __debug__);

    /* Read B6T file length */
    guint32 b6t_length = GUINT32_FROM_LE(MIRAGE_CAST_DATA(self->priv->cur_ptr, 0, guint32));
    self->priv->cur_ptr += sizeof(guint32);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: declared B6T file length: %i (0x%X) bytes\n\n", __debug__, b6t_length, b6t_length);


    /* Read footer */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing footer...\n", __debug__);
    if (!mirage_parser_b6t_parse_footer(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse footer!\n");
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished parsing footer\n\n", __debug__);

    /* Try to load external BWA file; the internal DPM data does not necessarily
       contain entries for whole disc, but rather just for beginning of the disc
       (which is usually checked by copy protection). BWA file, on the other hand,
       usually contains DPM data for the whole disc. So, if we have both internal
       DPM data and a BWA available, we'll use the latter... */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing external DPM data...\n", __debug__);
    if (!mirage_parser_b6t_load_bwa_file(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to load BWA file!\n");
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished parsing external DPM data\n\n", __debug__);

    return TRUE;
}


/**********************************************************************\
 *                 MIRAGE_Parser methods implementation               *
\**********************************************************************/
static GObject *mirage_parser_b6t_load_image (MIRAGE_Parser *_self, gchar **filenames, GError **error)
{
    MIRAGE_Parser_B6T *self = MIRAGE_PARSER_B6T(_self);

    gboolean succeeded = TRUE;
    GObject *stream;
    guint64 read_length;
    guint8 header[16];

    /* Check if we can load the image */
    stream = libmirage_create_file_stream(filenames[0], mirage_debuggable_get_debug_context(MIRAGE_DEBUGGABLE(self)), error);
    if (!stream) {
        return FALSE;
    }

    /* Read and verify header; we could also check the footer, but I think
       header check only is sufficient */
    g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);
    if (g_input_stream_read(G_INPUT_STREAM(stream), header, 16, NULL, NULL) != 16) {
        g_object_unref(stream);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read 16 bytes from image file stream!");
        return FALSE;
    }

    if (memcmp(header, "BWT5 STREAM SIGN", 16)) {
        g_object_unref(stream);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Parser cannot handle given image!");
        return FALSE;
    }


    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_attach_child(MIRAGE_OBJECT(self), self->priv->disc);

    mirage_disc_set_filename(MIRAGE_DISC(self->priv->disc), filenames[0]);
    self->priv->b6t_filename = g_strdup(filenames[0]);


    /* Get file size */
    g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_END, NULL, NULL);
    self->priv->b6t_length = g_seekable_tell(G_SEEKABLE(stream));
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: B6T length: %lld bytes\n", __debug__, self->priv->b6t_length);

    /* Allocate buffer */
    self->priv->b6t_data = g_malloc(self->priv->b6t_length);

    /* Read whole file */
    g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);
    read_length = g_input_stream_read(G_INPUT_STREAM(stream), self->priv->b6t_data, self->priv->b6t_length, NULL, NULL);

    g_object_unref(stream);

    if (read_length != self->priv->b6t_length) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read whole B6T file '%s' (%lld out of %lld bytes read)!\n", __debug__, filenames[0], read_length, self->priv->b6t_length);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read whoe B6T file!");
        succeeded = FALSE;
        goto end;
    }

    /* Load disc */
    succeeded = mirage_parser_b6t_load_disc(self, error);
    if (!succeeded) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to load disc!\n", __debug__);
    }

end:
    g_free(self->priv->b6t_data);
    self->priv->b6t_data = NULL;

    /* Return disc */
    mirage_object_detach_child(MIRAGE_OBJECT(self), self->priv->disc);
    if (succeeded) {
        return self->priv->disc;
    } else {
        g_object_unref(self->priv->disc);
        return NULL;
    }
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE(MIRAGE_Parser_B6T, mirage_parser_b6t, MIRAGE_TYPE_PARSER);

void mirage_parser_b6t_type_register (GTypeModule *type_module)
{
    return mirage_parser_b6t_register_type(type_module);
}


static void mirage_parser_b6t_init (MIRAGE_Parser_B6T *self)
{
    self->priv = MIRAGE_PARSER_B6T_GET_PRIVATE(self);

    mirage_parser_generate_parser_info(MIRAGE_PARSER(self),
        "PARSER-B6T",
        "B6T Image Parser",
        "BlindWrite 5/6 images",
        "application/x-b6t"
    );

    self->priv->b6t_filename = NULL;
    self->priv->b6t_data = NULL;
    self->priv->data_blocks_list = NULL;
}

static void mirage_parser_b6t_finalize (GObject *gobject)
{
    MIRAGE_Parser_B6T *self = MIRAGE_PARSER_B6T(gobject);
    GList *entry;

    /* Free list of data blocks */
    G_LIST_FOR_EACH(entry, self->priv->data_blocks_list) {
        if (entry->data) {
            B6T_DataBlock *data_block = entry->data;
            /* Free filename */
            g_free(data_block->filename);
            /* Free the data block */
            g_free(data_block);
        }
    }
    g_list_free(self->priv->data_blocks_list);

    g_free(self->priv->b6t_filename);
    g_free(self->priv->b6t_data);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_parser_b6t_parent_class)->finalize(gobject);
}

static void mirage_parser_b6t_class_init (MIRAGE_Parser_B6TClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MIRAGE_ParserClass *parser_class = MIRAGE_PARSER_CLASS(klass);

    gobject_class->finalize = mirage_parser_b6t_finalize;

    parser_class->load_image = mirage_parser_b6t_load_image;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Parser_B6TPrivate));
}

static void mirage_parser_b6t_class_finalize (MIRAGE_Parser_B6TClass *klass G_GNUC_UNUSED)
{
}
