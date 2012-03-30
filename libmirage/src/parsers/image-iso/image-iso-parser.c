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


static const guint8 cd001_pattern[] = {0x01, 0x43, 0x44, 0x30, 0x30, 0x31, 0x01, 0x00};
static const guint8 bea01_pattern[] = {0x00, 0x42, 0x45, 0x41, 0x30, 0x31, 0x01, 0x00};
static const guint8 sync_pattern[] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};



static gboolean mirage_parser_iso_is_file_valid (MIRAGE_Parser_ISO *self, gchar *filename, GError **error)
{
    gboolean succeeded;
    struct stat st;
    FILE *file;

    if (g_stat(filename, &st) < 0) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to stat file!\n", __debug__);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }

    file = g_fopen(filename, "r");
    if (!file) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open file!\n", __debug__);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }

    /* 2048-byte standard ISO9660/UDF image check */
    if (st.st_size % 2048 == 0) {
        guint8 buf[8] = {};

        fseeko(file, 16*2048, SEEK_SET);

        if (fread(buf, 8, 1, file) < 1) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read 8-byte pattern!\n", __debug__);
            mirage_error(MIRAGE_E_READFAILED, error);
            succeeded = FALSE;
            goto end;
        }

        if (!memcmp(buf, cd001_pattern, sizeof(cd001_pattern))
            || !memcmp(buf, bea01_pattern, sizeof(bea01_pattern))) {
            self->priv->track_sectsize = 2048;
            self->priv->track_mode = MIRAGE_MODE_MODE1;

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: standard 2048-byte ISO9660/UDF track, Mode 1 assumed\n", __debug__);

            succeeded = TRUE;
            goto end;
        }
    }

    /* 2352-byte image check */
    if (st.st_size % 2352 == 0) {
        guint8 buf[12] = {};

        fseeko(file, 16*2352, SEEK_SET);

        if (fread(buf, 12, 1, file) < 1) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read sync pattern!\n", __debug__);
            mirage_error(MIRAGE_E_READFAILED, error);
            succeeded = FALSE;
            goto end;
        }

        if (!memcmp(buf, sync_pattern, sizeof(sync_pattern))) {
            guint8 mode_byte = 0;


            /* Read mode byte from header */
            fseeko(file, 3, SEEK_CUR); /* We're at the end of sync, we just need to skip MSF */

            if (fread(&mode_byte, 1, 1, file) < 1) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read mode byte!\n", __debug__);
                mirage_error(MIRAGE_E_READFAILED, error);
                succeeded = FALSE;
                goto end;
            }

            switch (mode_byte) {
                case 0: {
                    self->priv->track_sectsize = 2352;
                    self->priv->track_mode = MIRAGE_MODE_MODE0;

                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 2352-byte track, Mode 0\n", __debug__);

                    succeeded = TRUE;
                    goto end;
                }
                case 1: {
                    self->priv->track_sectsize = 2352;
                    self->priv->track_mode = MIRAGE_MODE_MODE1;

                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 2352-byte track, Mode 1\n", __debug__);

                    succeeded = TRUE;
                    goto end;
                }
                case 2: {
                    self->priv->track_sectsize = 2352;
                    self->priv->track_mode = MIRAGE_MODE_MODE2_MIXED;

                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 2352-byte track, Mode 2 Mixed\n", __debug__);

                    succeeded = TRUE;
                    goto end;
                }
            }
        } else {
            self->priv->track_sectsize = 2352;
            self->priv->track_mode = MIRAGE_MODE_AUDIO;

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 2352-byte track w/o sync pattern, Audio assumed\n", __debug__);

            succeeded = TRUE;
            goto end;
        }
    }

    /* 2332/2336-byte image check */
    if (st.st_size % 2332 == 0) {
        self->priv->track_sectsize = 2332;
        self->priv->track_mode = MIRAGE_MODE_MODE2_MIXED;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 2332-byte track, Mode 2 Mixed assumed (unreliable!)\n", __debug__);

        succeeded = TRUE;
        goto end;
    }
    if (st.st_size % 2336 == 0) {
        self->priv->track_sectsize = 2336;
        self->priv->track_mode = MIRAGE_MODE_MODE2_MIXED;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 2336-byte track, Mode 2 Mixed assumed (unreliable!)\n", __debug__);

        succeeded = TRUE;
        goto end;
    }

    /* Nope, can't load the file */
    mirage_error(MIRAGE_E_CANTHANDLE, error);
    succeeded = FALSE;

end:
    fclose(file);
    return succeeded;
}

static gboolean mirage_parser_iso_load_track (MIRAGE_Parser_ISO *self, gchar *filename, GError **error)
{
    gboolean succeeded = TRUE;
    GObject *session = NULL;
    GObject *track = NULL;
    GObject *data_fragment = NULL;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: loading track from ISO file: %s\n", __debug__, filename);

    /* Create data fragment */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating data fragment\n", __debug__);
    data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FRAG_IFACE_BINARY, filename, error);
    if (!data_fragment) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create BINARY fragment!\n", __debug__);
        return FALSE;
    }

    /* Set file */
    if (!mirage_frag_iface_binary_track_file_set_file(MIRAGE_FRAG_IFACE_BINARY(data_fragment), filename, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set track data file!\n", __debug__);
        g_object_unref(data_fragment);
        return FALSE;
    }
    mirage_frag_iface_binary_track_file_set_sectsize(MIRAGE_FRAG_IFACE_BINARY(data_fragment), self->priv->track_sectsize, NULL);
    mirage_frag_iface_binary_track_file_set_format(MIRAGE_FRAG_IFACE_BINARY(data_fragment), FR_BIN_TFILE_DATA, NULL);

    /* Use whole file */
    if (!mirage_fragment_use_the_rest_of_file(MIRAGE_FRAGMENT(data_fragment), error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to use the rest of file!\n", __debug__);
        g_object_unref(data_fragment);
        return FALSE;
    }

    /* Add track */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding track\n", __debug__);

    mirage_disc_get_session_by_index(MIRAGE_DISC(self->priv->disc), -1, &session, NULL);
    succeeded = mirage_session_add_track_by_index(MIRAGE_SESSION(session), -1, &track, error);
    g_object_unref(session);
    if (!succeeded) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __debug__);
        g_object_unref(data_fragment);
        return succeeded;
    }

    /* Set track mode */
    mirage_track_set_mode(MIRAGE_TRACK(track), self->priv->track_mode, NULL);

    /* Add fragment to track */
    if (!mirage_track_add_fragment(MIRAGE_TRACK(track), -1, &data_fragment, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add fragment!\n", __debug__);
        g_object_unref(data_fragment);
        g_object_unref(track);
        return FALSE;
    }

    g_object_unref(data_fragment);
    g_object_unref(track);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished loading track\n", __debug__);

    return TRUE;
}

/**********************************************************************\
 *                MIRAGE_Parser methods implementation                *
\**********************************************************************/
static gboolean mirage_parser_iso_load_image (MIRAGE_Parser *_self, gchar **filenames, GObject **disc, GError **error)
{
    MIRAGE_Parser_ISO *self = MIRAGE_PARSER_ISO(_self);
    
    gboolean succeeded = TRUE;

    /* Check if file can be loaded */
    if (!mirage_parser_iso_is_file_valid(self, filenames[0], error)) {
        return FALSE;
    }

    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_attach_child(MIRAGE_OBJECT(self), self->priv->disc, NULL);

    /* Set filenames */
    mirage_disc_set_filename(MIRAGE_DISC(self->priv->disc), filenames[0], NULL);

    /* Session: one session (with possibly multiple tracks) */
    GObject *session = NULL;
    if (!mirage_disc_add_session_by_number(MIRAGE_DISC(self->priv->disc), 1, &session, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __debug__);
        succeeded = FALSE;
        goto end;
    }
    /* ISO image parser assumes single-track image, so we're dealing with regular CD-ROM session */
    mirage_session_set_session_type(MIRAGE_SESSION(session), MIRAGE_SESSION_CD_ROM, NULL);
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
