/*
 *  libMirage: ISO image writer: Writer object
 *  Copyright (C) 2014 Rok Mandeljc
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

#include "image-iso.h"

#define __debug__ "ISO-Writer"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_WRITER_ISO_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_WRITER_ISO, MirageWriterIsoPrivate))

struct _MirageWriterIsoPrivate
{
    gchar *image_file_basename;
};


/**********************************************************************\
 *                MirageWriter methods implementation                 *
\**********************************************************************/
static gboolean mirage_writer_iso_open_image (MirageWriter *self_, MirageDisc *disc, GError **error G_GNUC_UNUSED)
{
    MirageWriterIso *self = MIRAGE_WRITER_ISO(self_);

    /* For now, assume that we are given only prefix */
    const gchar **filenames = mirage_disc_get_filenames(disc);
    self->priv->image_file_basename = g_strdup(filenames[0]);

    return TRUE;
}

static MirageFragment *mirage_writer_iso_create_fragment (MirageWriter *self_, MirageTrack *track, MirageFragmentRole role, GError **error)
{
    MirageWriterIso *self = MIRAGE_WRITER_ISO(self_);

    MirageFragment *fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL);
    gchar *filename;
    GOutputStream *output_stream;
    GInputStream *input_stream;

    if (role == MIRAGE_FRAGMENT_PREGAP) {
        return fragment;
    }

    const gchar *extension;
    if (mirage_track_get_sector_type(track) == MIRAGE_SECTOR_AUDIO) {
        extension = "cdr";

        mirage_fragment_main_data_set_size(fragment, 2352);
    } else {
        extension = "iso";

        mirage_fragment_main_data_set_size(fragment, 2048);
    }

    filename = g_strdup_printf("%s-%d-%d.%s", self->priv->image_file_basename, mirage_track_layout_get_session_number(track), mirage_track_layout_get_track_number(track), extension);

    /* Output stream */
    output_stream = mirage_contextual_create_output_stream(MIRAGE_CONTEXTUAL(self), filename, error);
    if (!output_stream) {
        g_object_unref(fragment);
        return NULL;
    }
    mirage_fragment_main_data_set_output_stream(fragment, output_stream);
    g_object_unref(output_stream);

    /* Input stream */
    input_stream = mirage_contextual_create_input_stream(MIRAGE_CONTEXTUAL(self), filename, error);
    if (!input_stream) {
        g_object_unref(fragment);
        return NULL;
    }
    mirage_fragment_main_data_set_input_stream(fragment, input_stream);
    g_object_unref(input_stream);

    return fragment;
}

static gboolean mirage_writer_iso_finalize_image (MirageWriter *self_ G_GNUC_UNUSED)
{
    return TRUE;
}



/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE(MirageWriterIso, mirage_writer_iso, MIRAGE_TYPE_WRITER);

void mirage_writer_iso_type_register (GTypeModule *type_module)
{
    return mirage_writer_iso_register_type(type_module);
}


static void mirage_writer_iso_init (MirageWriterIso *self)
{
    self->priv = MIRAGE_WRITER_ISO_GET_PRIVATE(self);

    self->priv->image_file_basename = NULL;
}

static void mirage_writer_iso_finalize (GObject *gobject)
{
    MirageWriterIso *self = MIRAGE_WRITER_ISO(gobject);

    g_free(self->priv->image_file_basename);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_writer_iso_parent_class)->finalize(gobject);
}

static void mirage_writer_iso_class_init (MirageWriterIsoClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageWriterClass *writer_class = MIRAGE_WRITER_CLASS(klass);

    gobject_class->finalize = mirage_writer_iso_finalize;

    writer_class->open_image = mirage_writer_iso_open_image;
    writer_class->create_fragment = mirage_writer_iso_create_fragment;
    writer_class->finalize_image = mirage_writer_iso_finalize_image;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageWriterIsoPrivate));
}

static void mirage_writer_iso_class_finalize (MirageWriterIsoClass *klass G_GNUC_UNUSED)
{
}
