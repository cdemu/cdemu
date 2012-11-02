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
#define MIRAGE_PARSER_ISO_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_ISO, MirageParserIsoPrivate))

struct _MirageParserIsoPrivate
{
    MirageDisc *disc;

    gint track_mode;
    gint track_sectsize;
};


static gboolean mirage_parser_iso_is_file_valid (MirageParserIso *self, GInputStream *stream, GError **error)
{
    gsize file_length;

    /* Get stream length */
    if (!g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_END, NULL, error)) {
        return FALSE;
    }
    file_length = g_seekable_tell(G_SEEKABLE(stream));

    /* 2048-byte standard ISO9660/UDF image check */
    if (file_length % 2048 == 0) {
        guint8 buf[8];

        if (!g_seekable_seek(G_SEEKABLE(stream), 16*2048, G_SEEK_SET, NULL, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to 8-byte pattern!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to seek to 8-byte pattern!");
            return FALSE;
        }

        if (g_input_stream_read(stream, buf, 8, NULL, NULL) != 8) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read 8-byte pattern!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read 8-byte pattern!");
            return FALSE;
        }

        if (!memcmp(buf, mirage_pattern_cd001, sizeof(mirage_pattern_cd001))
            || !memcmp(buf, mirage_pattern_bea01, sizeof(mirage_pattern_bea01))) {
            self->priv->track_sectsize = 2048;
            self->priv->track_mode = MIRAGE_MODE_MODE1;

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: standard 2048-byte ISO9660/UDF track, Mode 1 assumed\n", __debug__);

            return TRUE;
        }
    }

    /* 2352-byte image check */
    if (file_length % 2352 == 0) {
        guint8 buf[16];

        if (!g_seekable_seek(G_SEEKABLE(stream), 16*2352, G_SEEK_SET, NULL, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to 16-byte pattern!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to seek to 16-byte pattern!");
            return FALSE;
        }

        if (g_input_stream_read(stream, buf, 16, NULL, NULL) != 16) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read 16-byte pattern!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read 16-byte pattern!");
            return FALSE;
        }

        /* Determine mode */
        self->priv->track_sectsize = 2352;
        self->priv->track_mode = mirage_helper_determine_sector_type(buf);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 2352-byte track, track mode: %d\n", __debug__, self->priv->track_mode);

        return TRUE;
    }

    /* 2332/2336-byte image check */
    if (file_length % 2332 == 0) {
        self->priv->track_sectsize = 2332;
        self->priv->track_mode = MIRAGE_MODE_MODE2_MIXED;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 2332-byte track, Mode 2 Mixed assumed (unreliable!)\n", __debug__);

        return TRUE;
    }
    if (file_length % 2336 == 0) {
        self->priv->track_sectsize = 2336;
        self->priv->track_mode = MIRAGE_MODE_MODE2_MIXED;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 2336-byte track, Mode 2 Mixed assumed (unreliable!)\n", __debug__);

        return TRUE;
    }

    /* Macintosh .CDR image check */
    /* These are raw images with typical Mac partitioning starting with a
       Driver Descriptor Map (DDM) and GUID Partition Map Entries following.
       Each are exactly 512-bytes long and have "ER" and "PM" signatures. */
    guint8 mac_buf[512];

    if (!g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to DDM block!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to seek to DDM block!");
        return FALSE;
    }

    if (g_input_stream_read(stream, mac_buf, sizeof(mac_buf), NULL, NULL) != sizeof(mac_buf)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read DDM block!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read DDM block!");
        return FALSE;
    }

    if (!memcmp(mac_buf, "ER", 2)) {
        self->priv->track_sectsize = GUINT16_FROM_BE(*((guint16 *) mac_buf + 1));
        self->priv->track_mode = MIRAGE_MODE_MODE1;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: %u-byte Macintosh GUID partitioned track, Mode 1 assumed\n",
                     __debug__, self->priv->track_sectsize);

        for (gint part = 0;; part++) {
            gchar *part_name;
            gchar *part_type;

            guint32 num_parts;

            if (g_input_stream_read(stream, mac_buf, sizeof(mac_buf), NULL, NULL) != sizeof(mac_buf)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read partition map entry!\n", __debug__);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read partition map entry!");
                return FALSE;
            }

            if (memcmp(mac_buf, "PM", 2)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: invalid partition map entry!\n", __debug__);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "invalid partition map entry!");
                return FALSE;
            }

            num_parts = GUINT32_FROM_BE(*((guint32 *) mac_buf + 1));
            part_name = (gchar *) mac_buf + 16;
            part_type = (gchar *) mac_buf + 48;

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Part. %2i name: %.32s\n", __debug__, part, part_name);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:          type: %.32s\n", __debug__, part_type);

            /* Last partition? */
            if (part + 1 >= num_parts) break;
        }

        return TRUE;
    }

    /* Nope, can't load the file */
    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Parser cannot handle given image!");
    return FALSE;
}

static gboolean mirage_parser_iso_load_track (MirageParserIso *self, GInputStream *stream, GError **error)
{
    MirageSession *session;
    MirageTrack *track;
    MirageFragment *fragment;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: loading track...\n", __debug__);

    /* Create data fragment */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating data fragment\n", __debug__);
    fragment = mirage_contextual_create_fragment(MIRAGE_CONTEXTUAL(self), MIRAGE_TYPE_DATA_FRAGMENT, stream, error);
    if (!fragment) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create BINARY fragment!\n", __debug__);
        return FALSE;
    }

    /* Set stream */
    mirage_data_fragment_main_data_set_stream(MIRAGE_DATA_FRAGMENT(fragment), stream);
    mirage_data_fragment_main_data_set_size(MIRAGE_DATA_FRAGMENT(fragment), self->priv->track_sectsize);
    mirage_data_fragment_main_data_set_format(MIRAGE_DATA_FRAGMENT(fragment), MIRAGE_MAIN_DATA);

    /* Use whole file */
    if (!mirage_fragment_use_the_rest_of_file(fragment, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to use the rest of file!\n", __debug__);
        g_object_unref(fragment);
        return FALSE;
    }

    /* Add track */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding track\n", __debug__);

    session = mirage_disc_get_session_by_index(self->priv->disc, -1, NULL);

    track = g_object_new(MIRAGE_TYPE_TRACK, NULL);
    mirage_session_add_track_by_index(session, -1, track);
    g_object_unref(session);

    /* Set track mode */
    mirage_track_set_mode(track, self->priv->track_mode);

    /* Add fragment to track */
    mirage_track_add_fragment(track, -1, fragment);

    g_object_unref(fragment);
    g_object_unref(track);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished loading track\n", __debug__);

    return TRUE;
}


/**********************************************************************\
 *                MirageParser methods implementation                *
\**********************************************************************/
static MirageDisc *mirage_parser_iso_load_image (MirageParser *_self, GInputStream **streams, GError **error)
{
    MirageParserIso *self = MIRAGE_PARSER_ISO(_self);
    const gchar *iso_filename;
    GInputStream *stream;
    gboolean succeeded = TRUE;

    /* Check if file can be loaded */
    stream = streams[0];
    g_object_ref(stream);
    iso_filename = mirage_contextual_get_file_stream_filename(MIRAGE_CONTEXTUAL(self), stream);

    if (!mirage_parser_iso_is_file_valid(self, stream, error)) {
        g_object_unref(stream);
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing the image...\n", __debug__);

    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_set_parent(MIRAGE_OBJECT(self->priv->disc), self);

    /* Set filenames */
    mirage_disc_set_filename(self->priv->disc, iso_filename);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: ISO filename: %s\n", __debug__, iso_filename);

    /* Session: one session (with possibly multiple tracks) */
    MirageSession *session = g_object_new(MIRAGE_TYPE_SESSION, NULL);
    mirage_disc_add_session_by_index(self->priv->disc, 0, session);

    /* ISO image parser assumes single-track image, so we're dealing with regular CD-ROM session */
    mirage_session_set_session_type(session, MIRAGE_SESSION_CD_ROM);
    g_object_unref(session);

    /* Load track */
    if (!mirage_parser_iso_load_track(self, stream, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to load track!\n", __debug__);
        succeeded = FALSE;
        goto end;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finishing the layout\n", __debug__);

    /* Now guess medium type and if it's a CD-ROM, add Red Book pregap */
    gint medium_type = mirage_parser_guess_medium_type(MIRAGE_PARSER(self), self->priv->disc);
    mirage_disc_set_medium_type(self->priv->disc, medium_type);
    if (medium_type == MIRAGE_MEDIUM_CD) {
        mirage_parser_add_redbook_pregap(MIRAGE_PARSER(self), self->priv->disc);
    }

end:
    g_object_unref(stream);

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
G_DEFINE_DYNAMIC_TYPE(MirageParserIso, mirage_parser_iso, MIRAGE_TYPE_PARSER);

void mirage_parser_iso_type_register (GTypeModule *type_module)
{
    return mirage_parser_iso_register_type(type_module);
}


static void mirage_parser_iso_init (MirageParserIso *self)
{
    self->priv = MIRAGE_PARSER_ISO_GET_PRIVATE(self);

    mirage_parser_generate_info(MIRAGE_PARSER(self),
        "PARSER-ISO",
        "ISO Image Parser",
        1,
        "ISO images (*.iso, *.bin, *.img)", "application/x-cd-image"
    );
}

static void mirage_parser_iso_class_init (MirageParserIsoClass *klass)
{
    MirageParserClass *parser_class = MIRAGE_PARSER_CLASS(klass);

    parser_class->load_image = mirage_parser_iso_load_image;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageParserIsoPrivate));
}

static void mirage_parser_iso_class_finalize (MirageParserIsoClass *klass G_GNUC_UNUSED)
{
}
