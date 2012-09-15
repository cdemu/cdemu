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
    GObject *data_stream;

    GObject *cur_session;
    GObject *cur_track;

    gint prev_mode;
};


static gboolean mirage_parser_readcd_is_file_valid (MIRAGE_Parser_READCD *self, const gchar *filename, GError **error)
{
    gboolean succeeded;
    GObject *stream;
    guint64 file_size;

    guint16 toc_len;

    /* File must have .toc suffix */
    if (!mirage_helper_has_suffix(filename, ".toc")) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Parser cannot handle given image!");
        return FALSE;
    }

    stream = libmirage_create_file_stream(filename, G_OBJECT(self), error);
    if (!stream) {
        return FALSE;
    }

    /* First 4 bytes of TOC are its header; and first 2 bytes of that indicate
       the length */
    if (g_input_stream_read(G_INPUT_STREAM(stream), &toc_len, sizeof(toc_len), NULL, NULL) != sizeof(toc_len)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read 2-byte TOC length!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read 2-byte TOC length!");
        succeeded = FALSE;
        goto end;
    }
    toc_len = GUINT16_FROM_BE(toc_len);

    /* Does TOC length match? (the TOC file actually contains TOC plus two bytes
       that indicate sector types) */
    g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_END, NULL, NULL);
    file_size = g_seekable_tell(G_SEEKABLE(stream));

    if (file_size - 2 == toc_len + 2) {
        succeeded = TRUE;
    } else {
        /* Nope, can't load the file */
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Parser cannot handle given image!");
        succeeded = FALSE;
    }

end:
    g_object_unref(stream);
    return succeeded;
}

static gboolean mirage_parser_readcd_determine_track_mode (MIRAGE_Parser_READCD *self, GObject *track, GError **error)
{
    GObject *fragment;
    guint8 *buf = (guint8 *) g_malloc(2532);
    gint track_mode;

	if(!buf) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Failed to allocate memory.");
		return FALSE;
	}

    /* Get last fragment */
    fragment = mirage_track_get_fragment_by_index(MIRAGE_TRACK(track), -1, error);
    if (!fragment) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get fragment\n", __debug__);
        return FALSE;
    }

    //* Read main sector data from fragment; 2352-byte sectors are assumed */
    if (!mirage_fragment_read_main_data(MIRAGE_FRAGMENT(fragment), 0, buf, NULL, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to read data from fragment to determine track mode!\n", __debug__);
        g_object_unref(fragment);
        return FALSE;
    }
    g_object_unref(fragment);

    /* Determine track mode*/
    track_mode = mirage_helper_determine_sector_type(buf);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track mode determined to be: %d\n", __debug__, track_mode);
    mirage_track_set_mode(MIRAGE_TRACK(track), track_mode);

	g_free(buf);

    return TRUE;
}

static gboolean mirage_parser_readcd_finish_previous_track (MIRAGE_Parser_READCD *self, gint next_address)
{
    if (self->priv->cur_lba != -1) {
        gint length = next_address - self->priv->cur_lba;
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: previous track's length: %d (0x%X)\n", __debug__, length, length);

        /* Get last fragment */
        GObject *fragment = mirage_track_get_fragment_by_index(MIRAGE_TRACK(self->priv->cur_track), -1, NULL);
        if (!fragment) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get fragment\n", __debug__);
            return FALSE;
        }

        /* Set length */
        mirage_fragment_set_length(MIRAGE_FRAGMENT(fragment), length);

        g_object_unref(fragment);
    }

    return TRUE;
}


static gboolean mirage_parser_readcd_parse_toc_entry (MIRAGE_Parser_READCD *self, guint8 *entry, GError **error)
{
    gboolean succeeded = TRUE;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", __debug__, entry[0], entry[1], entry[2], entry[3], entry[4], entry[5], entry[6], entry[7], entry[8], entry[9], entry[10]);

    if (entry[3] == 0xA0) {
        /* First track numer; add session */
        guint8 session_number = entry[0];

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating session #%d\n", __debug__, session_number);

        self->priv->cur_session = g_object_new(MIRAGE_TYPE_SESSION, NULL);
        succeeded = mirage_disc_add_session_by_number(MIRAGE_DISC(self->priv->disc), session_number, self->priv->cur_session, error);
        g_object_unref(self->priv->cur_session); /* Keep only pointer, without reference */
        if (!succeeded) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __debug__);
            goto end;
        }

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
        guint8 track_number = entry[3];

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating track #%d\n", __debug__, track_number);

        gint track_lba = mirage_helper_msf2lba(entry[8], entry[9], entry[10], TRUE);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track LBA: %d (0x%X)\n", __debug__, track_lba, track_lba);

        /* Calculate previous track's length */
        mirage_parser_readcd_finish_previous_track(self, track_lba);
        self->priv->cur_lba = track_lba;

        /* Add track */
        self->priv->cur_track = g_object_new(MIRAGE_TYPE_TRACK, NULL);
        succeeded = mirage_session_add_track_by_number(MIRAGE_SESSION(self->priv->cur_session), track_number, self->priv->cur_track, error);
        g_object_unref(self->priv->cur_track); /* Keep only pointer, without reference */

        if (!succeeded) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __debug__);
            g_object_unref(self->priv->cur_track);
            goto end;
        }

        mirage_track_set_ctl(MIRAGE_TRACK(self->priv->cur_track), entry[1]);

        /* Data fragment */
        GObject *fragment = libmirage_create_fragment(MIRAGE_TYPE_FRAG_IFACE_BINARY, self->priv->data_stream, G_OBJECT(self), error);
        if (!fragment) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create data fragment!\n", __debug__);
            succeeded = FALSE;
            goto end;
        }

        if (!mirage_frag_iface_binary_track_file_set_file(MIRAGE_FRAG_IFACE_BINARY(fragment), self->priv->data_filename, self->priv->data_stream, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set track data file!\n", __debug__);
            g_object_unref(fragment);
            succeeded = FALSE;
            goto end;
        }
        mirage_frag_iface_binary_track_file_set_sectsize(MIRAGE_FRAG_IFACE_BINARY(fragment), 2352);
        mirage_frag_iface_binary_track_file_set_offset(MIRAGE_FRAG_IFACE_BINARY(fragment), track_lba*2448);
        mirage_frag_iface_binary_track_file_set_format(MIRAGE_FRAG_IFACE_BINARY(fragment), FR_BIN_TFILE_DATA);

        mirage_frag_iface_binary_subchannel_file_set_sectsize(MIRAGE_FRAG_IFACE_BINARY(fragment), 96);
        mirage_frag_iface_binary_subchannel_file_set_format(MIRAGE_FRAG_IFACE_BINARY(fragment), FR_BIN_SFILE_PW96_INT | FR_BIN_SFILE_INT);

        mirage_track_add_fragment(MIRAGE_TRACK(self->priv->cur_track), -1, fragment);
        g_object_unref(fragment);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: determining track mode\n", __debug__);
        mirage_parser_readcd_determine_track_mode(self, self->priv->cur_track, NULL);

        /* Store track mode for comparison */
        gint track_mode = mirage_track_get_mode(MIRAGE_TRACK(self->priv->cur_track));

        if (self->priv->prev_mode != -1) {
            /* Check if track mode has changed from/to audio track */
            if (track_mode != self->priv->prev_mode && (track_mode == MIRAGE_MODE_AUDIO || self->priv->prev_mode == MIRAGE_MODE_AUDIO)) {
                GObject *prev_track, *prev_fragment;
                gint prev_fragment_len;

                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track mode changed from/to audio track; assume 2 second pregap!\n", __debug__);

                /* Previous track: shorten its fragment by 150 frames */
                prev_track = mirage_track_get_prev(MIRAGE_TRACK(self->priv->cur_track), NULL);
                prev_fragment = mirage_track_get_fragment_by_index(MIRAGE_TRACK(prev_track), -1, NULL);

                prev_fragment_len = mirage_fragment_get_length(MIRAGE_FRAGMENT(prev_fragment));
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: shortening previous track's fragment: %d -> %d\n", __debug__, prev_fragment_len, prev_fragment_len - 150);
                prev_fragment_len -= 150;
                mirage_fragment_set_length(MIRAGE_FRAGMENT(prev_fragment), prev_fragment_len);

                g_object_unref(prev_fragment);
                g_object_unref(prev_track);

                /* Current track: add 150-frame pregap with data from data file */
                fragment = libmirage_create_fragment(MIRAGE_TYPE_FRAG_IFACE_BINARY, self->priv->data_stream, G_OBJECT(self), error);
                if (!fragment) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create data fragment!\n", __debug__);
                    succeeded = FALSE;
                    goto end;
                }

                if (!mirage_frag_iface_binary_track_file_set_file(MIRAGE_FRAG_IFACE_BINARY(fragment), self->priv->data_filename, self->priv->data_stream, error)) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set track data file!\n", __debug__);
                    g_object_unref(fragment);
                    succeeded = FALSE;
                    goto end;
                }
                mirage_frag_iface_binary_track_file_set_sectsize(MIRAGE_FRAG_IFACE_BINARY(fragment), 2352);
                mirage_frag_iface_binary_track_file_set_offset(MIRAGE_FRAG_IFACE_BINARY(fragment), (track_lba - 150)*2448);
                mirage_frag_iface_binary_track_file_set_format(MIRAGE_FRAG_IFACE_BINARY(fragment), FR_BIN_TFILE_DATA);

                mirage_frag_iface_binary_subchannel_file_set_sectsize(MIRAGE_FRAG_IFACE_BINARY(fragment), 96);
                mirage_frag_iface_binary_subchannel_file_set_format(MIRAGE_FRAG_IFACE_BINARY(fragment), FR_BIN_SFILE_PW96_INT | FR_BIN_SFILE_INT);

                mirage_fragment_set_length(MIRAGE_FRAGMENT(fragment), 150);

                mirage_track_add_fragment(MIRAGE_TRACK(self->priv->cur_track), 0, fragment);
                g_object_unref(fragment);
            }
        }

        self->priv->prev_mode = track_mode;
    }

end:
    return succeeded;
}

static gboolean mirage_parser_readcd_parse_toc (MIRAGE_Parser_READCD *self, const gchar *filename, GError **error)
{
    gboolean succeeded = TRUE;
    GObject *stream;
    guint64 file_size, read_size;
    guint8 *data = NULL;

    /* NOTE: the mirage_parser_readcd_is_file_valid() check guarantees that the
       image filename has a valid suffix... */
    gchar *tmp_data_filename = g_strdup(filename);
    *mirage_helper_get_suffix(tmp_data_filename) = 0; /* Skip the suffix */

    self->priv->data_filename = mirage_helper_find_data_file(tmp_data_filename, filename);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: TOC filename: %s\n", __debug__, filename);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: data filename: %s\n", __debug__, tmp_data_filename);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: actual data filename: %s\n", __debug__, self->priv->data_filename);

    g_free(tmp_data_filename);

    /* Open data stream */
    if (!self->priv->data_filename) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: data file not found!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DATA_FILE_ERROR, "Data file not found!");
        return FALSE;
    }

    self->priv->data_stream = libmirage_create_file_stream(self->priv->data_filename, G_OBJECT(self), error);
    if (!self->priv->data_stream) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open data file '%s'!\n", __debug__, self->priv->data_filename);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DATA_FILE_ERROR, "Failed to create stream on data file!");
        return FALSE;
    }


    /* Read whole TOC file */
    stream = libmirage_create_file_stream(filename, G_OBJECT(self), error);
    if (!stream) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open TOC file '%s'!\n", __debug__, filename);
        return FALSE;
    }

    g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_END, NULL, NULL);
    file_size = g_seekable_tell(G_SEEKABLE(stream));

    data = g_malloc(file_size);

    g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);
    read_size = g_input_stream_read(G_INPUT_STREAM(stream), data, file_size, NULL, NULL);

    g_object_unref(stream);

    if (read_size != file_size) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read whole TOC file!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read whole TOC file!");
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
static GObject *mirage_parser_readcd_load_image (MIRAGE_Parser *_self, gchar **filenames, GError **error)
{
    MIRAGE_Parser_READCD *self = MIRAGE_PARSER_READCD(_self);
    gboolean succeeded = TRUE;

    /* Check if file can be loaded */
    if (!mirage_parser_readcd_is_file_valid(self, filenames[0], error)) {
        return FALSE;
    }

    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_attach_child(MIRAGE_OBJECT(self), self->priv->disc);

    /* Set filenames */
    mirage_disc_set_filename(MIRAGE_DISC(self->priv->disc), filenames[0]);

    /* Read and parse TOC */
    if (!mirage_parser_readcd_parse_toc(self, filenames[0], error)) {
        succeeded = FALSE;
        goto end;
    }

    /* If it's a multisession disc, fix up the lead-in/lead-out lengths
       (NOTE: last session is left out for readibility; but it's irrelevant) */
    gint num_sessions = mirage_disc_get_number_of_sessions(MIRAGE_DISC(self->priv->disc));
    gint i;
    for (i = 0; i < num_sessions - 1; i++) {
        GObject *session = mirage_disc_get_session_by_index(MIRAGE_DISC(self->priv->disc), i, NULL);

        if (i == 0) {
            /* Actually, it should be 6750 previous leadout, 4500 current leadin */
            mirage_session_set_leadout_length(MIRAGE_SESSION(session), 11250);
        } else {
            /* Actually, it should be 2250 previous leadout, 4500 current leadin */
            mirage_session_set_leadout_length(MIRAGE_SESSION(session), 6750);
        }

        g_object_unref(session);
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");

    /* Now guess medium type and if it's a CD-ROM, add Red Book pregap */
    gint medium_type = mirage_parser_guess_medium_type(MIRAGE_PARSER(self), self->priv->disc);
    mirage_disc_set_medium_type(MIRAGE_DISC(self->priv->disc), medium_type);
    if (medium_type == MIRAGE_MEDIUM_CD) {
        mirage_parser_add_redbook_pregap(MIRAGE_PARSER(self), self->priv->disc);
    }

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
    self->priv->data_stream = NULL;
}

static void mirage_parser_readcd_dispose (GObject *gobject)
{
    MIRAGE_Parser_READCD *self = MIRAGE_PARSER_READCD(gobject);

    if (self->priv->data_stream) {
        g_object_unref(self->priv->data_stream);
        self->priv->data_stream = NULL;
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_parser_readcd_parent_class)->dispose(gobject);
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

    gobject_class->dispose = mirage_parser_readcd_dispose;
    gobject_class->finalize = mirage_parser_readcd_finalize;

    parser_class->load_image = mirage_parser_readcd_load_image;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Parser_READCDPrivate));
}

static void mirage_parser_readcd_class_finalize (MIRAGE_Parser_READCDClass *klass G_GNUC_UNUSED)
{
}
