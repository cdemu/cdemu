/*
 *  libMirage: DAA image parser: Parser object
 *  Copyright (C) 2008-2012 Rok Mandeljc
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

#include "image-daa.h"

#define __debug__ "DAA-Parser"


/**********************************************************************\
 *                          Private structure                          *
\**********************************************************************/
#define MIRAGE_PARSER_DAA_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_DAA, MIRAGE_Parser_DAAPrivate))

struct _MIRAGE_Parser_DAAPrivate
{
    GObject *disc;
};


/**********************************************************************\
 *                 MIRAGE_Parser methods implementation                *
\**********************************************************************/
static GObject *mirage_parser_daa_load_image (MIRAGE_Parser *_self, gchar **filenames, GError **error)
{
    MIRAGE_Parser_DAA *self = MIRAGE_PARSER_DAA(_self);

    gboolean succeeded = TRUE;
    GObject *stream;
    gchar signature[16] = "";

    /* Open file */
    stream = libmirage_create_file_stream(filenames[0], G_OBJECT(self), NULL);
    if (!stream) {
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }

    /* Read signature */
    g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);
    if (g_input_stream_read(G_INPUT_STREAM(stream), signature, sizeof(signature), NULL, NULL) != sizeof(signature)) {
        g_object_unref(stream);
        mirage_error(MIRAGE_E_READFAILED, error);
        return FALSE;
    }
    g_object_unref(stream);

    /* Check signature (we're comparing -all- 16 bytes!) */
    if (memcmp(signature, daa_main_signature, sizeof(daa_main_signature))) {
        mirage_error(MIRAGE_E_CANTHANDLE, error);
        return FALSE;
    }

    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_attach_child(MIRAGE_OBJECT(self), self->priv->disc);

    mirage_disc_set_filename(MIRAGE_DISC(self->priv->disc), filenames[0]);

    /* Add session */
    GObject *session = NULL;

    if (!mirage_disc_add_session_by_number(MIRAGE_DISC(self->priv->disc), 1, &session, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __debug__);
        succeeded = FALSE;
        goto end;
    }

    mirage_session_set_session_type(MIRAGE_SESSION(session), MIRAGE_SESSION_CD_ROM);

    /* Add track */
    GObject *track = NULL;
    succeeded = mirage_session_add_track_by_index(MIRAGE_SESSION(session), -1, &track, error);
    g_object_unref(session);
    if (!succeeded) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __debug__);
        succeeded = FALSE;
        goto end;
    }

    mirage_track_set_mode(MIRAGE_TRACK(track), MIRAGE_MODE_MODE1);

    /* Try to get password from parser parameters */
    const gchar *password = mirage_parser_get_param_string(MIRAGE_PARSER(self), "password");

    /* Fragment(s); we use private, DAA fragments for this */
    GObject *data_fragment = g_object_new(MIRAGE_TYPE_FRAGMENT_DAA, NULL);
    GError *local_error = NULL;

    mirage_track_add_fragment(MIRAGE_TRACK(track), -1, data_fragment);

    if (!mirage_fragment_daa_set_file(MIRAGE_FRAGMENT_DAA(data_fragment), filenames[0], password, &local_error)) {
        /* Don't make buzz for password failures */
        if (local_error->code != MIRAGE_E_NEEDPASSWORD
            && local_error->code != MIRAGE_E_WRONGPASSWORD) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set file to fragment!\n", __debug__);
        }
        g_propagate_error(error, local_error);
        g_object_unref(data_fragment);
        g_object_unref(track);
        succeeded = FALSE;
        goto end;
    }

    g_object_unref(data_fragment);
    g_object_unref(track);

    /* Now guess medium type and if it's a CD-ROM, add Red Book pregap */
    gint medium_type = mirage_parser_guess_medium_type(MIRAGE_PARSER(self), self->priv->disc);
    mirage_disc_set_medium_type(MIRAGE_DISC(self->priv->disc), medium_type);
    if (medium_type == MIRAGE_MEDIUM_CD) {
        mirage_parser_add_redbook_pregap(MIRAGE_PARSER(self), self->priv->disc, NULL);
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
G_DEFINE_DYNAMIC_TYPE(MIRAGE_Parser_DAA, mirage_parser_daa, MIRAGE_TYPE_PARSER);

void mirage_parser_daa_type_register (GTypeModule *type_module)
{
    return mirage_parser_daa_register_type(type_module);
}


static void mirage_parser_daa_init (MIRAGE_Parser_DAA *self)
{
    self->priv = MIRAGE_PARSER_DAA_GET_PRIVATE(self);

    mirage_parser_generate_parser_info(MIRAGE_PARSER(self),
        "PARSER-DAA",
        "DAA Image Parser",
        "PowerISO direct access archives",
        "application/x-daa"
    );
}

static void mirage_parser_daa_class_init (MIRAGE_Parser_DAAClass *klass)
{
    MIRAGE_ParserClass *parser_class = MIRAGE_PARSER_CLASS(klass);

    parser_class->load_image = mirage_parser_daa_load_image;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Parser_DAAPrivate));
}

static void mirage_parser_daa_class_finalize (MIRAGE_Parser_DAAClass *klass G_GNUC_UNUSED)
{
}
