/*
 *  libMirage: readcd image parser: Parser object
 *  Copyright (C) 2011-2012 Rok Mandeljc
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

#include "image-readcd.h"

#define __debug__ "READCD-Parser"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_PARSER_READCD_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_READCD, MIRAGE_Parser_READCDPrivate))

struct _MIRAGE_Parser_READCDPrivate {
    GObject *disc;

    gint cur_lba;
    gint leadout_lba;

    gchar *data_filename;

    GObject *cur_session;
    GObject *cur_track;

    gint prev_mode;
};


static gboolean mirage_parser_readcd_is_file_valid (MIRAGE_Parser_READCD *self, gchar *filename, GError **error)
{
    gboolean succeeded;
    FILE *file;
    guint64 file_size;

    guint16 toc_len;

    /* File must have .toc suffix */
    if (!mirage_helper_has_suffix(filename, ".toc")) {
        mirage_error(MIRAGE_E_CANTHANDLE, error);
        succeeded = FALSE;
    }

    file = g_fopen(filename, "r");
    if (!file) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open file!\n", __debug__);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }

    /* First 4 bytes of TOC are its header; and first 2 bytes of that indicate
       the length */
    if (fread(&toc_len, 1, 2, file) != 2) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read 2-byte TOC length!\n", __debug__);
        mirage_error(MIRAGE_E_READFAILED, error);
        succeeded = FALSE;
        goto end;
    }
    toc_len = GUINT16_FROM_BE(toc_len);

    /* Does TOC length match? (the TOC file actually contains TOC plus two bytes
       that indicate sector types) */
    fseek(file, 0, SEEK_END);
    file_size = ftell(file);

    if (file_size - 2 == toc_len + 2) {
        succeeded = TRUE;
    } else {
        /* Nope, can't load the file */
        mirage_error(MIRAGE_E_CANTHANDLE, error);
        succeeded = FALSE;
    }

end:
    fclose(file);
    return succeeded;
}

static gboolean mirage_parser_readcd_determine_track_mode (MIRAGE_Parser_READCD *self, GObject *track, GError **error)
{
    GObject *data_fragment = NULL;
    guint64 offset = 0;
    const gchar *filename;
    FILE *file;
    size_t blocks_read;
    guint8 sync[12] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
    guint8 buf[24] = {0};

    /* Get last fragment */
    if (!mirage_track_get_fragment_by_index(MIRAGE_TRACK(track), -1, &data_fragment, NULL)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get fragment\n", __debug__);
        return FALSE;
    }

    /* 2352-byte sectors are assumed */
    mirage_frag_iface_binary_track_file_get_file(MIRAGE_FRAG_IFACE_BINARY(data_fragment), &filename, NULL);
    mirage_frag_iface_binary_track_file_get_position(MIRAGE_FRAG_IFACE_BINARY(data_fragment), 0, &offset, NULL);

    file = g_fopen(filename, "r");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: checking for track type in data file at location 0x%llX\n", __debug__, offset);
    fseeko(file, offset, SEEK_SET);
    blocks_read = fread(buf, 24, 1, file);
    fclose(file);
    if (blocks_read < 1) {
        mirage_error(MIRAGE_E_DATAFILE, error);
        return FALSE;
    }
    if (!memcmp(buf, sync, 12)) {
        switch (buf[15]) {
            case 0x01: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Mode 1 track\n", __debug__);
                mirage_track_set_mode(MIRAGE_TRACK(track), MIRAGE_MODE_MODE1, NULL);
                break;
            }
            case 0x02: {
                /* Mode 2; let's say we're Mode 2 Mixed and let the sector
                   code do the rest for us */
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Mode 2 track; setting to mixed...\n", __debug__);
                mirage_track_set_mode(MIRAGE_TRACK(track), MIRAGE_MODE_MODE2_MIXED, NULL);
                break;
            }
        }
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Audio track\n", __debug__);
        mirage_track_set_mode(MIRAGE_TRACK(track), MIRAGE_MODE_AUDIO, NULL);
    }

    g_object_unref(data_fragment);

    return TRUE;
}

static gboolean mirage_parser_readcd_finish_previous_track (MIRAGE_Parser_READCD *self, gint next_address)
{
    if (self->priv->cur_lba != -1) {
        gint length = next_address - self->priv->cur_lba;
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: previous track's length: %d (0x%X)\n", __debug__, length, length);

        GObject *fragment;

        /* Get last fragment */
        if (!mirage_track_get_fragment_by_index(MIRAGE_TRACK(self->priv->cur_track), -1, &fragment, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get fragment\n", __debug__);
            return FALSE;
        }

        /* Set length */
        mirage_fragment_set_length(MIRAGE_FRAGMENT(fragment), length, NULL);

        g_object_unref(fragment);
    }

    return TRUE;
}


static gboolean mirage_parser_readcd_parse_toc_entry (MIRAGE_Parser_READCD *self, guint8 *entry, GError **error)
{
    gboolean succeeded = TRUE;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", __debug__, entry[0], entry[1], entry[2], entry[3], entry[4], entry[5], entry[6], entry[7], entry[8], entry[9], entry[10]);

    if (entry[3] == 0xA0) {
        /* First track number */
        guint8 session = entry[0];

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating session #%d\n", __debug__, session);

        self->priv->cur_session = NULL;
        succeeded = mirage_disc_add_session_by_number(MIRAGE_DISC(self->priv->disc), session, &self->priv->cur_session, error);
        if (!succeeded) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __debug__);
            goto end;
        }

        g_object_unref(self->priv->cur_session); /* Keep only pointer, without reference */

        /* Finish previous track (i.e. last track in previous session) */
        if (self->priv->cur_track && self->priv->leadout_lba != -1) {
            mirage_parser_readcd_finish_previous_track(self, self->priv->leadout_lba);
        }

        /* Reset */
        self->priv->cur_lba = -1;
        self->priv->cur_track = NULL;
    } else if (entry[3] == 0xA1) {
        /* Last track number */
    } else if (entry[3] == 0xA2) {
        /* Lead-put position */
        gint leadout_lba = mirage_helper_msf2lba(entry[8], entry[9], entry[10], TRUE);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: leadout LBA: %d (0x%X)\n", __debug__, leadout_lba, leadout_lba);

        self->priv->leadout_lba = leadout_lba;
    } else if (entry[3] > 0 && entry[3] < 99) {
        /* Track */
        guint8 track = entry[3];

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating track #%d\n", __debug__, track);

        gint track_lba = mirage_helper_msf2lba(entry[8], entry[9], entry[10], TRUE);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track LBA: %d (0x%X)\n", __debug__, track_lba, track_lba);

        /* Calculate previous track's length */
        mirage_parser_readcd_finish_previous_track(self, track_lba);
        self->priv->cur_lba = track_lba;

        /* Add track */
        self->priv->cur_track = NULL;
        succeeded = mirage_session_add_track_by_number(MIRAGE_SESSION(self->priv->cur_session), track, &self->priv->cur_track, error);
        if (!succeeded) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __debug__);
            goto end;
        }

        mirage_track_set_ctl(MIRAGE_TRACK(self->priv->cur_track), entry[1], NULL);

        g_object_unref(self->priv->cur_track); /* Keep only pointer, without reference */

        /* Data fragment */
        GObject *data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FRAG_IFACE_BINARY, self->priv->data_filename, error);
        if (!data_fragment) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create data fragment!\n", __debug__);
            succeeded = FALSE;
            goto end;
        }

        gint tfile_sectsize = 2352; /* Always */
        guint64 tfile_offset = track_lba * 2448; /* Guess this one's always true, too */
        gint tfile_format = FR_BIN_TFILE_DATA; /* Assume data, but change later if it's audio */

        gint sfile_sectsize = 96; /* Always */
        gint sfile_format = FR_BIN_SFILE_PW96_INT | FR_BIN_SFILE_INT;

        if (!mirage_frag_iface_binary_track_file_set_file(MIRAGE_FRAG_IFACE_BINARY(data_fragment), self->priv->data_filename, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set track data file!\n", __debug__);
            g_object_unref(data_fragment);
            succeeded = FALSE;
            goto end;
        }
        mirage_frag_iface_binary_track_file_set_sectsize(MIRAGE_FRAG_IFACE_BINARY(data_fragment), tfile_sectsize, NULL);
        mirage_frag_iface_binary_track_file_set_offset(MIRAGE_FRAG_IFACE_BINARY(data_fragment), tfile_offset, NULL);
        mirage_frag_iface_binary_track_file_set_format(MIRAGE_FRAG_IFACE_BINARY(data_fragment), tfile_format, NULL);

        mirage_frag_iface_binary_subchannel_file_set_sectsize(MIRAGE_FRAG_IFACE_BINARY(data_fragment), sfile_sectsize, NULL);
        mirage_frag_iface_binary_subchannel_file_set_format(MIRAGE_FRAG_IFACE_BINARY(data_fragment), sfile_format, NULL);

        mirage_track_add_fragment(MIRAGE_TRACK(self->priv->cur_track), -1, &data_fragment, NULL);
        g_object_unref(data_fragment);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: determining track mode\n", __debug__);
        mirage_parser_readcd_determine_track_mode(self, self->priv->cur_track, NULL);

        /* Store track mode for comparison */
        gint track_mode;
        mirage_track_get_mode(MIRAGE_TRACK(self->priv->cur_track), &track_mode, NULL);

        if (self->priv->prev_mode != -1) {
            /* Check if track mode has changed from/to audio track */
            if (track_mode != self->priv->prev_mode && (track_mode == MIRAGE_MODE_AUDIO || self->priv->prev_mode == MIRAGE_MODE_AUDIO)) {
                GObject *prev_track, *prev_fragment;
                gint prev_fragment_len;

                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track mode changed from/to audio track; assume 2 second pregap!\n", __debug__);

                /* Previous track: shorten its fragment by 150 frames */
                mirage_track_get_prev(MIRAGE_TRACK(self->priv->cur_track), &prev_track, NULL);
                mirage_track_get_fragment_by_index(MIRAGE_TRACK(prev_track), -1, &prev_fragment, NULL);

                mirage_fragment_get_length(MIRAGE_FRAGMENT(prev_fragment), &prev_fragment_len, NULL);
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: shortening previous track's fragment: %d -> %d\n", __debug__, prev_fragment_len, prev_fragment_len - 150);
                prev_fragment_len -= 150;
                mirage_fragment_set_length(MIRAGE_FRAGMENT(prev_fragment), prev_fragment_len, NULL);

                g_object_unref(prev_fragment);
                g_object_unref(prev_track);

                /* Current track: add 150-frame pregap with data from data file */
                GObject *pregap_fragment = libmirage_create_fragment(MIRAGE_TYPE_FRAG_IFACE_BINARY, self->priv->data_filename, error);
                if (!pregap_fragment) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create data fragment!\n", __debug__);
                    succeeded = FALSE;
                    goto end;
                }

                gint tfile_sectsize = 2352; /* Always */
                guint64 tfile_offset = (track_lba - 150) * 2448; /* Guess this one's always true, too */
                gint tfile_format = FR_BIN_TFILE_DATA; /* Assume data, but change later if it's audio */

                gint sfile_sectsize = 96; /* Always */
                gint sfile_format = FR_BIN_SFILE_PW96_INT | FR_BIN_SFILE_INT;

                if (!mirage_frag_iface_binary_track_file_set_file(MIRAGE_FRAG_IFACE_BINARY(pregap_fragment), self->priv->data_filename, error)) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set track data file!\n", __debug__);
                    g_object_unref(pregap_fragment);
                    succeeded = FALSE;
                    goto end;
                }
                mirage_frag_iface_binary_track_file_set_sectsize(MIRAGE_FRAG_IFACE_BINARY(pregap_fragment), tfile_sectsize, NULL);
                mirage_frag_iface_binary_track_file_set_offset(MIRAGE_FRAG_IFACE_BINARY(pregap_fragment), tfile_offset, NULL);
                mirage_frag_iface_binary_track_file_set_format(MIRAGE_FRAG_IFACE_BINARY(pregap_fragment), tfile_format, NULL);

                mirage_frag_iface_binary_subchannel_file_set_sectsize(MIRAGE_FRAG_IFACE_BINARY(pregap_fragment), sfile_sectsize, NULL);
                mirage_frag_iface_binary_subchannel_file_set_format(MIRAGE_FRAG_IFACE_BINARY(pregap_fragment), sfile_format, NULL);

                mirage_fragment_set_length(MIRAGE_FRAGMENT(pregap_fragment), 150, 0);

                mirage_track_add_fragment(MIRAGE_TRACK(self->priv->cur_track), 0, &pregap_fragment, NULL);
                g_object_unref(pregap_fragment);
            }
        }

        self->priv->prev_mode = track_mode;
    }

end:
    return succeeded;
}

static gboolean mirage_parser_readcd_parse_toc (MIRAGE_Parser_READCD *self, gchar *filename, GError **error)
{
    gboolean succeeded = TRUE;
    FILE *file;
    guint64 file_size, read_size;
    guint8 *data = NULL;

    /* NOTE: the mirage_parser_readcd_is_file_valid() check guarantees that the
       image filename has a valid suffix... */
    self->priv->data_filename = g_strdup(filename);
    *mirage_helper_get_suffix(self->priv->data_filename) = 0; /* Skip the suffix */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: TOC filename: %s\n", __debug__, filename);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: data filename: %s\n", __debug__, self->priv->data_filename);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:\n", __debug__);
    
    /* Make sure data file exists */
     if (!g_file_test(self->priv->data_filename, G_FILE_TEST_IS_REGULAR)) {
         mirage_error(MIRAGE_E_DATAFILE, error);
         return FALSE;
     }

    /* Read whole TOC file */
    file = g_fopen(filename, "r");   
    if (!file) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open TOC file '%s'!\n", __debug__, filename);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }

    fseek(file, 0, SEEK_END);
    file_size = ftell(file);

    data = g_malloc(file_size);
    
    fseek(file, 0, SEEK_SET);
    read_size = fread(data, 1, file_size, file);

    fclose(file);

    if (read_size != file_size) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read whole TOC file!\n", __debug__);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        succeeded = FALSE;
        goto end;
    }


    /* Parse TOC file */
    guint8 cur_off = 0;

    guint16 toc_len;
    guint8 first_session, last_session;

    toc_len = GUINT16_FROM_BE(MIRAGE_CAST_DATA(data, cur_off, guint16));
    cur_off += sizeof(guint16);

    first_session = MIRAGE_CAST_DATA(data, cur_off, guint8);
    cur_off += sizeof(guint8);

    last_session = MIRAGE_CAST_DATA(data, cur_off, guint8);
    cur_off += sizeof(guint8);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: TOC:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  TOC length: %d (0x%X)\n", __debug__, toc_len, toc_len);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  first session: %d\n", __debug__, first_session);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  last session: %d\n\n", __debug__, last_session);

    toc_len -= 2; /* Session numbers */

    /* Reset */
    self->priv->leadout_lba = -1;
    self->priv->cur_lba = -1;
    self->priv->cur_session = NULL;
    self->priv->cur_track = NULL;
    self->priv->prev_mode = -1;

    /* Go over all TOC entries */
    for (int i = 0; i < toc_len/11; i++) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: TOC entry #%d\n", __debug__, i);

        /* Parse TOC entry */
        succeeded = mirage_parser_readcd_parse_toc_entry(self, data+cur_off, error);
        if (!succeeded) {
            goto end;
        }

        cur_off += 11;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    }

    /* Finish previous track (i.e. last track in previous session) */
    if (self->priv->cur_track && self->priv->leadout_lba != -1) {
        mirage_parser_readcd_finish_previous_track(self, self->priv->leadout_lba);
    }

    guint8 sector0 = MIRAGE_CAST_DATA(data, cur_off, guint8);
    cur_off += sizeof(guint8);

    guint8 sectorE = MIRAGE_CAST_DATA(data, cur_off, guint8);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: start sector type: %Xh\n", __debug__, sector0);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: end sector type: %Xh\n", __debug__, sectorE);

end:
    g_free(data);

    return succeeded;
}


/**********************************************************************\
 *                MIRAGE_Parser methods implementation               *
\**********************************************************************/
static gboolean mirage_parser_readcd_load_image (MIRAGE_Parser *_self, gchar **filenames, GObject **disc, GError **error)
{
    MIRAGE_Parser_READCD *self = MIRAGE_PARSER_READCD(_self);
    gboolean succeeded = TRUE;

    /* Check if file can be loaded */
    if (!mirage_parser_readcd_is_file_valid(self, filenames[0], error)) {
        return FALSE;
    }

    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_attach_child(MIRAGE_OBJECT(self), self->priv->disc, NULL);

    /* Set filenames */
    mirage_disc_set_filename(MIRAGE_DISC(self->priv->disc), filenames[0], NULL);

    /* Read and parse TOC */
    if (!mirage_parser_readcd_parse_toc(self, filenames[0], error)) {
        succeeded = FALSE;
        goto end;
    }

    /* If it's a multisession disc, fix up the lead-in/lead-out lengths
       (NOTE: last session is left out for readibility; but it's irrelevant) */
    gint i, num_sessions;
    mirage_disc_get_number_of_sessions(MIRAGE_DISC(self->priv->disc), &num_sessions, NULL);
    for (i = 0; i < num_sessions - 1; i++) {
        GObject *session;
        mirage_disc_get_session_by_index(MIRAGE_DISC(self->priv->disc), i, &session, NULL);

        if (i == 0) {
            /* Actually, it should be 6750 previous leadout, 4500 current leadin */
            mirage_session_set_leadout_length(MIRAGE_SESSION(session), 11250, NULL);
        } else {
            /* Actually, it should be 2250 previous leadout, 4500 current leadin */
            mirage_session_set_leadout_length(MIRAGE_SESSION(session), 6750, NULL);
        }

        g_object_unref(session);
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");

    /* Now guess medium type and if it's a CD-ROM, add Red Book pregap */
    gint medium_type = mirage_parser_guess_medium_type(MIRAGE_PARSER(self), self->priv->disc);
    mirage_disc_set_medium_type(MIRAGE_DISC(self->priv->disc), medium_type, NULL);
    if (medium_type == MIRAGE_MEDIUM_CD) {
        mirage_parser_add_redbook_pregap(MIRAGE_PARSER(self), self->priv->disc, NULL);
    }

end:
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
G_DEFINE_DYNAMIC_TYPE(MIRAGE_Parser_READCD, mirage_parser_readcd, MIRAGE_TYPE_PARSER);

void mirage_parser_readcd_type_register (GTypeModule *type_module)
{
    return mirage_parser_readcd_register_type(type_module);
}


static void mirage_parser_readcd_init (MIRAGE_Parser_READCD *self)
{
    self->priv = MIRAGE_PARSER_READCD_GET_PRIVATE(self);

    mirage_parser_generate_parser_info(MIRAGE_PARSER(self),
        "PARSER-READCD",
        "READCD Image Parser",
        "READCD images",
        "application/x-cd-image"
    );

    self->priv->data_filename = NULL;
}

static void mirage_parser_readcd_finalize (GObject *gobject)
{
    MIRAGE_Parser_READCD *self = MIRAGE_PARSER_READCD(gobject);

    g_free(self->priv->data_filename);
    
    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_parser_readcd_parent_class)->finalize(gobject);
}

static void mirage_parser_readcd_class_init (MIRAGE_Parser_READCDClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MIRAGE_ParserClass *parser_class = MIRAGE_PARSER_CLASS(klass);

    gobject_class->finalize = mirage_parser_readcd_finalize;

    parser_class->load_image = mirage_parser_readcd_load_image;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Parser_READCDPrivate));
}

static void mirage_parser_readcd_class_finalize (MIRAGE_Parser_READCDClass *klass G_GNUC_UNUSED)
{
}
