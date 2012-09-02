/*
 *  libMirage: ISO image parser: Parser object
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

#include "image-iso.h"

#define __debug__ "ISO-Parser"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_PARSER_ISO_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_ISO, MIRAGE_Parser_ISOPrivate))

struct _MIRAGE_Parser_ISOPrivate
{
    GObject *disc;

    gint track_mode;
    gint track_sectsize;
};


static gboolean mirage_parser_iso_is_file_valid (MIRAGE_Parser_ISO *self, gchar *filename, GError **error)
{
    gboolean succeeded;
    gsize file_length;
    GObject *stream;

    /* Create stream */
    stream = mirage_parser_get_cached_data_stream(MIRAGE_PARSER(self), filename, error);
    if (!stream) {
        return FALSE;
    }

    /* Get stream length */
    if (!g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_END, NULL, error)) {
        succeeded = FALSE;
        goto end;
    }
    file_length = g_seekable_tell(G_SEEKABLE(stream));


    /* 2048-byte standard ISO9660/UDF image check */
    if (file_length % 2048 == 0) {
        guint8 buf[8];

        if (!g_seekable_seek(G_SEEKABLE(stream), 16*2048, G_SEEK_SET, NULL, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to 8-byte pattern!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to seek to 8-byte pattern!");
            succeeded = FALSE;
            goto end;
        }

        if (g_input_stream_read(G_INPUT_STREAM(stream), buf, 8, NULL, NULL) != 8) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read 8-byte pattern!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read 8-byte pattern!");
            succeeded = FALSE;
            goto end;
        }

        if (!memcmp(buf, mirage_pattern_cd001, sizeof(mirage_pattern_cd001))
            || !memcmp(buf, mirage_pattern_bea01, sizeof(mirage_pattern_bea01))) {
            self->priv->track_sectsize = 2048;
            self->priv->track_mode = MIRAGE_MODE_MODE1;

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: standard 2048-byte ISO9660/UDF track, Mode 1 assumed\n", __debug__);

            succeeded = TRUE;
            goto end;
        }
    }

    /* 2352-byte image check */
    if (file_length % 2352 == 0) {
        guint8 buf[16];

        if (!g_seekable_seek(G_SEEKABLE(stream), 16*2352, G_SEEK_SET, NULL, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to 16-byte pattern!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to seek to 16-byte pattern!");
            succeeded = FALSE;
            goto end;
        }

        if (g_input_stream_read(G_INPUT_STREAM(stream), buf, 16, NULL, NULL) != 16) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read 16-byte pattern!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read 16-byte pattern!");
            succeeded = FALSE;
            goto end;
        }

        /* Determine mode */
        self->priv->track_sectsize = 2352;
        self->priv->track_mode = mirage_helper_determine_sector_type(buf);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 2352-byte track, track mode: %d\n", __debug__, self->priv->track_mode);

        succeeded = TRUE;
        goto end;
    }

    /* 2332/2336-byte image check */
    if (file_length % 2332 == 0) {
        self->priv->track_sectsize = 2332;
        self->priv->track_mode = MIRAGE_MODE_MODE2_MIXED;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 2332-byte track, Mode 2 Mixed assumed (unreliable!)\n", __debug__);

        succeeded = TRUE;
        goto end;
    }
    if (file_length % 2336 == 0) {
        self->priv->track_sectsize = 2336;
        self->priv->track_mode = MIRAGE_MODE_MODE2_MIXED;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 2336-byte track, Mode 2 Mixed assumed (unreliable!)\n", __debug__);

        succeeded = TRUE;
        goto end;
    }

    /* Nope, can't load the file */
    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Parser cannot handle given image!");
    succeeded = FALSE;

end:
    g_object_unref(stream);
    return succeeded;
}

static gboolean mirage_parser_iso_load_track (MIRAGE_Parser_ISO *self, gchar *filename, GError **error)
{
    GObject *session;
    GObject *track;
    GObject *fragment;
    GObject *data_stream;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: loading track from ISO file: %s\n", __debug__, filename);

    /* Get data stream */
    data_stream = mirage_parser_get_cached_data_stream(MIRAGE_PARSER(self), filename, error);
    if (!data_stream) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open data stream on file: %s!\n", __debug__, filename);
        return FALSE;
    }

    /* Create data fragment */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating data fragment\n", __debug__);
    fragment = libmirage_create_fragment(MIRAGE_TYPE_FRAG_IFACE_BINARY, data_stream, G_OBJECT(self), error);
    if (!fragment) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create BINARY fragment!\n", __debug__);
        g_object_unref(data_stream);
        return FALSE;
    }

    /* Set file */
    if (!mirage_frag_iface_binary_track_file_set_file(MIRAGE_FRAG_IFACE_BINARY(fragment), filename, data_stream, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set track data file!\n", __debug__);
        g_object_unref(data_stream);
        g_object_unref(fragment);
        return FALSE;
    }
    mirage_frag_iface_binary_track_file_set_sectsize(MIRAGE_FRAG_IFACE_BINARY(fragment), self->priv->track_sectsize);
    mirage_frag_iface_binary_track_file_set_format(MIRAGE_FRAG_IFACE_BINARY(fragment), FR_BIN_TFILE_DATA);

    /* Use whole file */
    if (!mirage_fragment_use_the_rest_of_file(MIRAGE_FRAGMENT(fragment), error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to use the rest of file!\n", __debug__);
        g_object_unref(data_stream);
        g_object_unref(fragment);
        return FALSE;
    }

    /* Add track */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding track\n", __debug__);

    session = mirage_disc_get_session_by_index(MIRAGE_DISC(self->priv->disc), -1, NULL);

    track = g_object_new(MIRAGE_TYPE_TRACK, NULL);
    mirage_session_add_track_by_index(MIRAGE_SESSION(session), -1, track);
    g_object_unref(session);

    /* Set track mode */
    mirage_track_set_mode(MIRAGE_TRACK(track), self->priv->track_mode);

    /* Add fragment to track */
    mirage_track_add_fragment(MIRAGE_TRACK(track), -1, fragment);

    g_object_unref(data_stream);
    g_object_unref(fragment);
    g_object_unref(track);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished loading track\n", __debug__);

    return TRUE;
}


/**********************************************************************\
 *                MIRAGE_Parser methods implementation                *
\**********************************************************************/
static GObject *mirage_parser_iso_load_image (MIRAGE_Parser *_self, gchar **filenames, GError **error)
{
    MIRAGE_Parser_ISO *self = MIRAGE_PARSER_ISO(_self);

    gboolean succeeded = TRUE;

    /* Check if file can be loaded */
    if (!mirage_parser_iso_is_file_valid(self, filenames[0], error)) {
        return FALSE;
    }

    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_attach_child(MIRAGE_OBJECT(self), self->priv->disc);

    /* Set filenames */
    mirage_disc_set_filename(MIRAGE_DISC(self->priv->disc), filenames[0]);

    /* Session: one session (with possibly multiple tracks) */
    GObject *session = g_object_new(MIRAGE_TYPE_SESSION, NULL);
    mirage_disc_add_session_by_index(MIRAGE_DISC(self->priv->disc), 0, session);

    /* ISO image parser assumes single-track image, so we're dealing with regular CD-ROM session */
    mirage_session_set_session_type(MIRAGE_SESSION(session), MIRAGE_SESSION_CD_ROM);
    g_object_unref(session);

    /* Load track */
    if (!mirage_parser_iso_load_track(self, filenames[0], error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to load track!\n", __debug__);
        succeeded = FALSE;
        goto end;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finishing the layout\n", __debug__);

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
G_DEFINE_DYNAMIC_TYPE(MIRAGE_Parser_ISO, mirage_parser_iso, MIRAGE_TYPE_PARSER);

void mirage_parser_iso_type_register (GTypeModule *type_module)
{
    return mirage_parser_iso_register_type(type_module);
}


static void mirage_parser_iso_init (MIRAGE_Parser_ISO *self)
{
    self->priv = MIRAGE_PARSER_ISO_GET_PRIVATE(self);

    mirage_parser_generate_parser_info(MIRAGE_PARSER(self),
        "PARSER-ISO",
        "ISO Image Parser",
        "ISO images",
        "application/x-cd-image"
    );
}

static void mirage_parser_iso_class_init (MIRAGE_Parser_ISOClass *klass)
{
    MIRAGE_ParserClass *parser_class = MIRAGE_PARSER_CLASS(klass);

    parser_class->load_image = mirage_parser_iso_load_image;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Parser_ISOPrivate));
}

static void mirage_parser_iso_class_finalize (MIRAGE_Parser_ISOClass *klass G_GNUC_UNUSED)
{
}
