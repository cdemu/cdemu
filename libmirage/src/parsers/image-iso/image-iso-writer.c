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
    gchar *image_file_format;
    gchar *image_file_basename;
    gchar *audio_file_suffix;
    gboolean write_raw;
    gboolean write_subchannel;
};

static const gchar *audio_filter_chain[] = {
    "MirageFilterStreamSndfile",
    NULL
};

static const gchar parameter_sheet[];


static void mirage_writer_iso_clear_options (MirageWriterIso *self)
{
    g_free(self->priv->image_file_format);
    self->priv->image_file_format = NULL;

    g_free(self->priv->image_file_basename);
    self->priv->image_file_basename = NULL;

    g_free(self->priv->audio_file_suffix);
    self->priv->audio_file_suffix = NULL;
}

/**********************************************************************\
 *                MirageWriter methods implementation                 *
\**********************************************************************/
static gboolean mirage_writer_iso_open_image (MirageWriter *self_, MirageDisc *disc, GError **error G_GNUC_UNUSED)
{
    MirageWriterIso *self = MIRAGE_WRITER_ISO(self_);
    GVariant *option;

    /* Clear options */
    mirage_writer_iso_clear_options(self);

    /* Determine image file basename */
    const gchar *filename = mirage_disc_get_filenames(disc)[0];
    const gchar *suffix = mirage_helper_get_suffix(filename);

    if (!suffix) {
        self->priv->image_file_basename = g_strdup(filename);
    } else {
        self->priv->image_file_basename = g_strndup(filename, suffix - filename);
    }

    /* Option: image file format */
    option = mirage_contextual_get_option(MIRAGE_CONTEXTUAL(self), "writer.image_file_format");
    if (option) {
        self->priv->image_file_format = g_variant_dup_string(option, NULL);
    } else {
        self->priv->image_file_format = g_strdup("%b-%s-%t.%e");
    }

    /* Option: audio file suffix */
    option = mirage_contextual_get_option(MIRAGE_CONTEXTUAL(self), "writer.audio_file_suffix");
    if (option) {
        self->priv->audio_file_suffix = g_variant_dup_string(option, NULL);
    } else {
        self->priv->audio_file_suffix = g_strdup("wav");
    }

    /* Option: write raw */
    option = mirage_contextual_get_option(MIRAGE_CONTEXTUAL(self), "writer.write_raw");
    if (option) {
        self->priv->write_raw = g_variant_get_boolean(option);
    } else {
        self->priv->write_raw = FALSE;
    }

    /* Option: write subchannel */
    option = mirage_contextual_get_option(MIRAGE_CONTEXTUAL(self), "writer.write_subchannel");
    if (option) {
        self->priv->write_subchannel = g_variant_get_boolean(option);
    } else {
        self->priv->write_subchannel = FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: image file format: '%s'\n", __debug__, self->priv->image_file_format);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: image file basename: '%s'\n", __debug__, self->priv->image_file_basename);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: audio file suffix: '%s'\n", __debug__, self->priv->audio_file_suffix);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: write raw: %d\n", __debug__, self->priv->write_raw);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: write subchannel: %d\n", __debug__, self->priv->write_subchannel);

    return TRUE;
}

static MirageFragment *mirage_writer_iso_create_fragment (MirageWriter *self_, MirageTrack *track, MirageFragmentRole role, GError **error)
{
    MirageWriterIso *self = MIRAGE_WRITER_ISO(self_);

    MirageFragment *fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL);
    gchar *filename;
    MirageStream *stream;

    if (role == MIRAGE_FRAGMENT_PREGAP) {
        return fragment;
    }

    const gchar *extension;
    const gchar **filter_chain = NULL;

    if (self->priv->write_subchannel || self->priv->write_raw) {
        /* Raw mode (also implied by subchannel) */
        extension = "bin";
        mirage_fragment_main_data_set_size(fragment, 2352);
    } else {
        /* Cooked mode */
        switch (mirage_track_get_sector_type(track)) {
            case MIRAGE_SECTOR_AUDIO: {
                extension = self->priv->audio_file_suffix;
                mirage_fragment_main_data_set_size(fragment, 2352);
                filter_chain = audio_filter_chain;
                break;
            }
            case MIRAGE_SECTOR_MODE1:
            case MIRAGE_SECTOR_MODE2_FORM1: {
                extension = "iso";
                mirage_fragment_main_data_set_size(fragment, 2048);
                break;
            }
            case MIRAGE_SECTOR_MODE2:
            case MIRAGE_SECTOR_MODE2_FORM2:
            case MIRAGE_SECTOR_MODE2_MIXED: {
                extension = "bin";
                mirage_fragment_main_data_set_size(fragment, 2336);
                break;
            }
            default: {
                /* Should not happen, but just in case */
                extension = "bin";
                mirage_fragment_main_data_set_size(fragment, 2352);
                break;
            }
        }
    }

    /* Subchannel; only internal PW96 interleaved is supported */
    if (self->priv->write_subchannel) {
        mirage_fragment_subchannel_data_set_format(fragment, MIRAGE_SUBCHANNEL_DATA_FORMAT_PW96_INTERLEAVED | MIRAGE_SUBCHANNEL_DATA_FORMAT_INTERNAL);
        mirage_fragment_subchannel_data_set_size(fragment, 0);
    }

    filename = mirage_helper_format_string(self->priv->image_file_format,
        "b", g_variant_new_string(self->priv->image_file_basename),
        "s", g_variant_new_int16(mirage_track_layout_get_session_number(track)),
        "t", g_variant_new_int16(mirage_track_layout_get_track_number(track)),
        "e", g_variant_new_string(extension), NULL);

    /* I/O stream */
    stream = mirage_contextual_create_output_stream(MIRAGE_CONTEXTUAL(self), filename, filter_chain, error);
    if (!stream) {
        g_object_unref(fragment);
        return NULL;
    }
    mirage_fragment_main_data_set_stream(fragment, stream);
    g_object_unref(stream);

    return fragment;
}

static gboolean mirage_writer_iso_finalize_image (MirageWriter *self_ G_GNUC_UNUSED, MirageDisc *disc, GError **error G_GNUC_UNUSED)
{
    /* Go over disc, and gather the names of track files */
    gint num_tracks = mirage_disc_get_number_of_tracks(disc);
    const gchar **filenames = g_new0(const gchar *, num_tracks + 1);

    for (gint i = 0; i < num_tracks; i++) {
        MirageTrack *track = mirage_disc_get_track_by_index(disc, i, NULL);
        if (!track) {
            continue;
        }

        gint num_fragments = mirage_track_get_number_of_fragments(track);
        for (gint f = num_fragments - 1; f >= 0; f--) {
            MirageFragment *fragment = mirage_track_get_fragment_by_index(track, f, NULL);
            if (!fragment) {
                continue;
            }

            filenames[i] = mirage_fragment_main_data_get_filename(fragment);

            g_object_unref(fragment);

            if (filenames[i]) {
                break;
            }
        }

        g_object_unref(track);

        if (!filenames[i]) {
            filenames[i] = "<ERROR>";
        }
    }

    mirage_disc_set_filenames(disc, filenames);

    g_object_unref(disc);

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

    mirage_writer_generate_info(MIRAGE_WRITER(self),
        "WRITER-ISO",
        "ISO Image Writer",
        parameter_sheet
    );

    self->priv->image_file_format = NULL;
    self->priv->image_file_basename = NULL;
    self->priv->audio_file_suffix = NULL;

    self->priv->write_raw = FALSE;
    self->priv->write_subchannel = FALSE;
}

static void mirage_writer_iso_finalize (GObject *gobject)
{
    MirageWriterIso *self = MIRAGE_WRITER_ISO(gobject);

    /* Clear options */
    mirage_writer_iso_clear_options(self);

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

/**********************************************************************\
 *                        Writer parameter sheet                      *
\**********************************************************************/
static const gchar parameter_sheet[] =
"<parameters>"
"  <parameter>"
"    <id>writer.image_file_format</id>"
"    <name>Image file format</name>"
"    <description></description>"
"    <type>string</type>"
"    <default></default>"
"  </parameter>"
"  <parameter>"
"    <id>writer.audio_file_suffix</id>"
"    <name>Image file format</name>"
"    <description></description>"
"    <type>enum</type>"
"    <enum>wav</enum>"
"    <enum>aiff</enum>"
"    <enum>ogg</enum>"
"    <enum>flac</enum>"
"    <enum>cdr</enum>"
"    <default>wav</default>"
"  </parameter>"
"  <parameter>"
"    <id>writer.write_raw</id>"
"    <name>Write raw</name>"
"    <description></description>"
"    <type>boolean</type>"
"    <default></default>"
"  </parameter>"
"  <parameter>"
"    <id>writer.write_subchannel</id>"
"    <name>Write subchannel</name>"
"    <description></description>"
"    <type>string</type>"
"    <default></default>"
"  </parameter>"
"</parameters>";
