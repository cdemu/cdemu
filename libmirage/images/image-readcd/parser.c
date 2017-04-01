/*
 *  libMirage: readcd image: parser
 *  Copyright (C) 2011-2014 Rok Mandeljc
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

#include "image-readcd.h"

#define __debug__ "READCD-Parser"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_PARSER_READCD_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_READCD, MirageParserReadcdPrivate))

struct _MirageParserReadcdPrivate {
    MirageDisc *disc;

    gint cur_lba;
    gint leadout_lba;

    const gchar *toc_filename;
    gchar *data_filename;
    MirageStream *data_stream;

    MirageSession *cur_session;
    MirageTrack *cur_track;

    gint prev_mode;
};


static gboolean mirage_parser_readcd_is_file_valid (MirageParserReadcd *self, MirageStream *stream, GError **error)
{
    guint64 file_size;
    guint16 toc_len;

    /* File must have .toc suffix */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: verifying image file's suffix...\n", __debug__);
    if (!mirage_helper_has_suffix(mirage_stream_get_filename(stream), ".toc")) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: invalid suffix (not a *.toc file!)!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Parser cannot handle given image: invalid suffix!"));
        return FALSE;
    }

    /* First 4 bytes of TOC are its header; and first 2 bytes of that indicate
       the length */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: reading 4-byte header...\n", __debug__);
    mirage_stream_seek(stream, 0, G_SEEK_SET, NULL);
    if (mirage_stream_read(stream, &toc_len, sizeof(toc_len), NULL) != sizeof(toc_len)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Parser cannot handle given image: failed to read 2-byte TOC length!"));
        return FALSE;
    }
    toc_len = GUINT16_FROM_BE(toc_len);

    /* Does TOC length match? (the TOC file actually contains TOC plus two bytes
       that indicate sector types) */
    mirage_stream_seek(stream, 0, G_SEEK_END, NULL);
    file_size = mirage_stream_tell(stream);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: verifying file length:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s:  expected size (based on header): %d or %d\n", __debug__, 2 + toc_len + 2, 2 + toc_len + 3);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s:  actual data file size: %" G_GINT64_MODIFIER "d\n", __debug__, file_size);

    /* readcd from cdrdtools appears to pad odd TOC lengths to make them
       even, whereas readcd from cdrkit does not. So we account for both
       cases. */
    if ((file_size == 2 + toc_len + 2) || (file_size == 2 + toc_len + 3)) {
        return TRUE;
    }

    /* Nope, can't load the file */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: parser cannot handle given image: invalid data file size!\n", __debug__);
    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Parser cannot handle given image!"));
    return FALSE;
}

static gboolean mirage_parser_readcd_determine_track_mode (MirageParserReadcd *self, MirageTrack *track, GError **error)
{
    MirageFragment *fragment;
    gint track_mode;
    guint8 *buffer;
    gint length;

    /* Get last fragment */
    fragment = mirage_track_get_fragment_by_index(track, -1, error);
    if (!fragment) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get fragment\n", __debug__);
        return FALSE;
    }

    //* Read main sector data from fragment; 2352-byte sectors are assumed */
    if (!mirage_fragment_read_main_data(fragment, 0, &buffer, &length, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to read data from fragment to determine track mode!\n", __debug__);
        g_object_unref(fragment);
        return FALSE;
    }
    g_object_unref(fragment);

    /* Determine track mode*/
    track_mode = mirage_helper_determine_sector_type(buffer);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track mode determined to be: %d\n", __debug__, track_mode);
    mirage_track_set_sector_type(track, track_mode);

    g_free(buffer);

    return TRUE;
}

static gboolean mirage_parser_readcd_finish_previous_track (MirageParserReadcd *self, gint next_address)
{
    if (self->priv->cur_lba != -1) {
        gint length = next_address - self->priv->cur_lba;
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: previous track's length: %d (0x%X)\n", __debug__, length, length);

        /* Get last fragment */
        MirageFragment *fragment = mirage_track_get_fragment_by_index(self->priv->cur_track, -1, NULL);
        if (!fragment) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get fragment\n", __debug__);
            return FALSE;
        }

        /* Set length */
        mirage_fragment_set_length(fragment, length);

        g_object_unref(fragment);
    }

    return TRUE;
}


static gboolean mirage_parser_readcd_parse_toc_entry (MirageParserReadcd *self, guint8 *entry, GError **error)
{
    gboolean succeeded = TRUE;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", __debug__, entry[0], entry[1], entry[2], entry[3], entry[4], entry[5], entry[6], entry[7], entry[8], entry[9], entry[10]);

    if (entry[3] == 0xA0) {
        /* First track numer; add session */
        guint8 session_number = entry[0];

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating session #%d\n", __debug__, session_number);

        self->priv->cur_session = g_object_new(MIRAGE_TYPE_SESSION, NULL);
        succeeded = mirage_disc_add_session_by_number(self->priv->disc, session_number, self->priv->cur_session, error);
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
        succeeded = mirage_session_add_track_by_number(self->priv->cur_session, track_number, self->priv->cur_track, error);
        g_object_unref(self->priv->cur_track); /* Keep only pointer, without reference */

        if (!succeeded) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __debug__);
            g_object_unref(self->priv->cur_track);
            goto end;
        }

        mirage_track_set_ctl(self->priv->cur_track, entry[1]);

        /* Data fragment */
        MirageFragment *fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL);

        mirage_fragment_main_data_set_stream(fragment, self->priv->data_stream);
        mirage_fragment_main_data_set_size(fragment, 2352);
        mirage_fragment_main_data_set_offset(fragment, track_lba*2448);
        mirage_fragment_main_data_set_format(fragment, MIRAGE_MAIN_DATA_FORMAT_DATA);

        mirage_fragment_subchannel_data_set_size(fragment, 96);
        mirage_fragment_subchannel_data_set_format(fragment, MIRAGE_SUBCHANNEL_DATA_FORMAT_PW96_INTERLEAVED | MIRAGE_SUBCHANNEL_DATA_FORMAT_INTERNAL);

        mirage_track_add_fragment(MIRAGE_TRACK(self->priv->cur_track), -1, fragment);
        g_object_unref(fragment);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: determining track mode\n", __debug__);
        mirage_parser_readcd_determine_track_mode(self, self->priv->cur_track, NULL);

        /* Store track mode for comparison */
        gint track_mode = mirage_track_get_sector_type(self->priv->cur_track);

        if (self->priv->prev_mode != -1) {
            /* Check if track mode has changed from/to audio track */
            if (track_mode != self->priv->prev_mode && (track_mode == MIRAGE_SECTOR_AUDIO || self->priv->prev_mode == MIRAGE_SECTOR_AUDIO)) {
                MirageTrack *prev_track;
                MirageFragment *prev_fragment;
                gint prev_fragment_len;

                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track mode changed from/to audio track; assume 2 second pregap!\n", __debug__);

                /* Previous track: shorten its fragment by 150 frames */
                prev_track = mirage_track_get_prev(self->priv->cur_track, NULL);
                prev_fragment = mirage_track_get_fragment_by_index(prev_track, -1, NULL);

                prev_fragment_len = mirage_fragment_get_length(prev_fragment);
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: shortening previous track's fragment: %d -> %d\n", __debug__, prev_fragment_len, prev_fragment_len - 150);
                prev_fragment_len -= 150;
                mirage_fragment_set_length(prev_fragment, prev_fragment_len);

                g_object_unref(prev_fragment);
                g_object_unref(prev_track);

                /* Current track: add 150-frame pregap with data from data file */
                fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL);

                mirage_fragment_main_data_set_stream(fragment, self->priv->data_stream);
                mirage_fragment_main_data_set_size(fragment, 2352);
                mirage_fragment_main_data_set_offset(fragment, (track_lba - 150)*2448);
                mirage_fragment_main_data_set_format(fragment, MIRAGE_MAIN_DATA_FORMAT_DATA);

                mirage_fragment_subchannel_data_set_size(fragment, 96);
                mirage_fragment_subchannel_data_set_format(fragment, MIRAGE_SUBCHANNEL_DATA_FORMAT_PW96_INTERLEAVED | MIRAGE_SUBCHANNEL_DATA_FORMAT_INTERNAL);

                mirage_fragment_set_length(fragment, 150);

                mirage_track_add_fragment(self->priv->cur_track, 0, fragment);
                g_object_unref(fragment);
            }
        }

        self->priv->prev_mode = track_mode;
    }

end:
    return succeeded;
}

static gboolean mirage_parser_readcd_parse_toc (MirageParserReadcd *self, MirageStream *stream, GError **error)
{
    gboolean succeeded;
    guint64 file_size, read_size;
    guint8 *data;

    gchar *tmp_data_filename;
    const gchar *suffix;

    /* NOTE: the mirage_parser_readcd_is_file_valid() check guarantees that the
       image filename has a valid suffix... */
    tmp_data_filename = g_strdup(self->priv->toc_filename);
    suffix = mirage_helper_get_suffix(tmp_data_filename);
    tmp_data_filename[suffix-tmp_data_filename] = '\0'; /* Skip the suffix */

    self->priv->data_filename = mirage_helper_find_data_file(tmp_data_filename, self->priv->toc_filename);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: data filename: %s\n", __debug__, tmp_data_filename);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: actual data filename: %s\n", __debug__, self->priv->data_filename);

    g_free(tmp_data_filename);

    /* Open data stream */
    if (!self->priv->data_filename) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: data file not found!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DATA_FILE_ERROR, Q_("Data file not found!"));
        return FALSE;
    }

    self->priv->data_stream = mirage_contextual_create_input_stream(MIRAGE_CONTEXTUAL(self), self->priv->data_filename, error);
    if (!self->priv->data_stream) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open data file '%s'!\n", __debug__, self->priv->data_filename);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DATA_FILE_ERROR, Q_("Failed to create stream on data file!"));
        return FALSE;
    }


    /* Read whole TOC file */
    mirage_stream_seek(stream, 0, G_SEEK_END, NULL);
    file_size = mirage_stream_tell(stream);

    data = g_malloc(file_size);

    mirage_stream_seek(stream, 0, G_SEEK_SET, NULL);
    read_size = mirage_stream_read(stream, data, file_size, NULL);

    if (read_size != file_size) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read whole TOC file!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Failed to read whole TOC file!"));
        g_free(data);
        return FALSE;
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
    for (gint i = 0; i < toc_len/11; i++) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: TOC entry #%d\n", __debug__, i);

        /* Parse TOC entry */
        succeeded = mirage_parser_readcd_parse_toc_entry(self, data+cur_off, error);
        if (!succeeded) {
            g_free(data);
            return FALSE;
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

    return TRUE;
}


/**********************************************************************\
 *                MirageParser methods implementation               *
\**********************************************************************/
static MirageDisc *mirage_parser_readcd_load_image (MirageParser *_self, MirageStream **streams, GError **error)
{
    MirageParserReadcd *self = MIRAGE_PARSER_READCD(_self);
    gboolean succeeded = TRUE;
    MirageStream *stream;

    /* Check if file can be loaded */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: checking if parser can handle given image...\n", __debug__);

    stream = g_object_ref(streams[0]);

    if (!mirage_parser_readcd_is_file_valid(self, stream, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: parser cannot handle given image: invalid readcd TOC file!\n", __debug__);
        g_object_unref(stream);
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: parser can handle given image!\n", __debug__);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing the image...\n", __debug__);

    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_set_parent(MIRAGE_OBJECT(self->priv->disc), self);

    /* Set filenames */
    self->priv->toc_filename = mirage_stream_get_filename(stream);
    mirage_disc_set_filename(self->priv->disc, self->priv->toc_filename);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: TOC filename: %s\n", __debug__, self->priv->toc_filename);

    /* Read and parse TOC */
    succeeded = mirage_parser_readcd_parse_toc(self, stream, error);
    g_object_unref(stream);

    if (succeeded) {
        /* If it's a multisession disc, fix up the lead-in/lead-out lengths
           (NOTE: last session is left out for readibility; but it's irrelevant) */
        gint num_sessions = mirage_disc_get_number_of_sessions(self->priv->disc);
        for (gint i = 0; i < num_sessions - 1; i++) {
            MirageSession *session = mirage_disc_get_session_by_index(self->priv->disc, i, NULL);

            if (i == 0) {
                /* Actually, it should be 6750 previous leadout, 4500 current leadin */
                mirage_session_set_leadout_length(session, 11250);
            } else {
                /* Actually, it should be 2250 previous leadout, 4500 current leadin */
                mirage_session_set_leadout_length(session, 6750);
            }

            g_object_unref(session);
        }

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");

        /* Now guess medium type and if it's a CD-ROM, add Red Book pregap */
        gint medium_type = mirage_parser_guess_medium_type(MIRAGE_PARSER(self), self->priv->disc);
        mirage_disc_set_medium_type(self->priv->disc, medium_type);
        if (medium_type == MIRAGE_MEDIUM_CD) {
            mirage_parser_add_redbook_pregap(MIRAGE_PARSER(self), self->priv->disc);
        }
    }

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
G_DEFINE_DYNAMIC_TYPE(MirageParserReadcd, mirage_parser_readcd, MIRAGE_TYPE_PARSER);

void mirage_parser_readcd_type_register (GTypeModule *type_module)
{
    return mirage_parser_readcd_register_type(type_module);
}


static void mirage_parser_readcd_init (MirageParserReadcd *self)
{
    self->priv = MIRAGE_PARSER_READCD_GET_PRIVATE(self);

    mirage_parser_generate_info(MIRAGE_PARSER(self),
        "PARSER-READCD",
        Q_("READCD Image Parser"),
        1,
        Q_("readcd images (*.toc)"), "application/x-cd-image"
    );

    self->priv->data_filename = NULL;
    self->priv->data_stream = NULL;
}

static void mirage_parser_readcd_dispose (GObject *gobject)
{
    MirageParserReadcd *self = MIRAGE_PARSER_READCD(gobject);

    if (self->priv->data_stream) {
        g_object_unref(self->priv->data_stream);
        self->priv->data_stream = NULL;
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_parser_readcd_parent_class)->dispose(gobject);
}

static void mirage_parser_readcd_finalize (GObject *gobject)
{
    MirageParserReadcd *self = MIRAGE_PARSER_READCD(gobject);

    g_free(self->priv->data_filename);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_parser_readcd_parent_class)->finalize(gobject);
}

static void mirage_parser_readcd_class_init (MirageParserReadcdClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageParserClass *parser_class = MIRAGE_PARSER_CLASS(klass);

    gobject_class->dispose = mirage_parser_readcd_dispose;
    gobject_class->finalize = mirage_parser_readcd_finalize;

    parser_class->load_image = mirage_parser_readcd_load_image;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageParserReadcdPrivate));
}

static void mirage_parser_readcd_class_finalize (MirageParserReadcdClass *klass G_GNUC_UNUSED)
{
}
