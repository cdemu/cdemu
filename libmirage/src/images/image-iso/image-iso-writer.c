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

#define PARAM_AUDIO_FILE_SUFFIX "writer.audio_file_suffix"
#define PARAM_WRITE_RAW "writer.write_raw"
#define PARAM_WRITE_SUBCHANNEL "writer.write_subchannel"
#define PARAM_SWAP_RAW_AUDIO_DATA "writer.swap_raw_audio"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_WRITER_ISO_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_WRITER_ISO, MirageWriterIsoPrivate))

struct _MirageWriterIsoPrivate
{
    gchar *image_file_basename;

    GList *image_file_streams;
};

static const gchar *audio_filter_chain[] = {
    "MirageFilterStreamSndfile",
    NULL
};

static const gchar image_file_format[] = "%b-%02s-%02t.%e";


/**********************************************************************\
 *                          Helper functions                          *
\**********************************************************************/
static void mirage_writer_iso_rename_track_image_files (MirageWriterIso *self, MirageDisc *disc)
{
    gint num_sessions = mirage_disc_get_number_of_sessions(disc);
    gint num_tracks = mirage_disc_get_number_of_tracks(disc);

    if (num_tracks > 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: renaming track files...\n", __debug__);

        const gchar *original_filename, *extension;
        gchar *new_filename;

        gint track = 1;
        GList *iter = g_list_first(self->priv->image_file_streams);

        while (iter) {
            /* Construct new filename */
            original_filename = mirage_stream_get_filename(iter->data);
            extension = mirage_helper_get_suffix(original_filename) + 1; /* +1 to skip the '.' */

            if (num_sessions == 1) {
                new_filename = mirage_helper_format_string(image_file_format,
                    "b", g_variant_new_string(self->priv->image_file_basename),
                    "t", g_variant_new_int16(track),
                    "e", g_variant_new_string(extension), NULL);
            } else {
                new_filename = mirage_helper_format_string(image_file_format,
                    "b", g_variant_new_string(self->priv->image_file_basename),
                    "s", g_variant_new_int16(1),
                    "t", g_variant_new_int16(track),
                    "e", g_variant_new_string(extension), NULL);
            }

            /* Move */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: '%s' -> '%s'\n", __debug__, original_filename, new_filename);
            if (!mirage_stream_move_file(iter->data, new_filename, NULL)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to rename file for track #%d to '%s'!\n", __debug__, track, new_filename);
            }
            g_free(new_filename);

            /* If we have only one session, nreak after first iteration */
            if (num_sessions == 1) {
                break;
            }

            track++;
            iter = g_list_next(iter);
        }
    }
}

static void mirage_writer_iso_update_disc_filenames (MirageWriterIso *self G_GNUC_UNUSED, MirageDisc *disc)
{
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
}


/**********************************************************************\
 *                MirageWriter methods implementation                 *
\**********************************************************************/
static gboolean mirage_writer_iso_open_image (MirageWriter *_self, MirageDisc *disc, GError **error G_GNUC_UNUSED)
{
    MirageWriterIso *self = MIRAGE_WRITER_ISO(_self);

    /* Determine image file basename */
    const gchar *filename = mirage_disc_get_filenames(disc)[0];
    const gchar *suffix = mirage_helper_get_suffix(filename);

    if (!suffix) {
        self->priv->image_file_basename = g_strdup(filename);
    } else {
        self->priv->image_file_basename = g_strndup(filename, suffix - filename);
    }

    /* Print parameters */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: image file basename: '%s'\n", __debug__, self->priv->image_file_basename);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: audio file suffix: '%s'\n", __debug__, mirage_writer_get_parameter_string(_self, PARAM_AUDIO_FILE_SUFFIX));
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: write raw: %d\n", __debug__, mirage_writer_get_parameter_boolean(_self, PARAM_WRITE_RAW));
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: write subchannel: %d\n", __debug__, mirage_writer_get_parameter_boolean(_self, PARAM_WRITE_SUBCHANNEL));
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: swap raw audio data: %d\n", __debug__, mirage_writer_get_parameter_boolean(_self, PARAM_SWAP_RAW_AUDIO_DATA));

    return TRUE;
}

static MirageFragment *mirage_writer_iso_create_fragment (MirageWriter *_self, MirageTrack *track, MirageFragmentRole role, GError **error)
{
    MirageWriterIso *self = MIRAGE_WRITER_ISO(_self);

    MirageFragment *fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL);
    gchar *filename;
    MirageStream *stream;

    if (role == MIRAGE_FRAGMENT_PREGAP) {
        return fragment;
    }

    gboolean write_raw = mirage_writer_get_parameter_boolean(_self, PARAM_WRITE_RAW);
    gboolean write_subchannel = mirage_writer_get_parameter_boolean(_self, PARAM_WRITE_SUBCHANNEL);
    gboolean swap_raw_audio_data = mirage_writer_get_parameter_boolean(_self, PARAM_SWAP_RAW_AUDIO_DATA);

    const gchar *extension;
    const gchar **filter_chain = NULL;

    if (write_subchannel || write_raw) {
        /* Raw mode (also implied by subchannel) */
        extension = "bin";
        mirage_fragment_main_data_set_size(fragment, 2352);

        if (mirage_track_get_sector_type(track) == MIRAGE_SECTOR_AUDIO) {
            if (swap_raw_audio_data) {
                mirage_fragment_main_data_set_format(fragment, MIRAGE_MAIN_DATA_FORMAT_AUDIO_SWAP);
            } else {
                mirage_fragment_main_data_set_format(fragment, MIRAGE_MAIN_DATA_FORMAT_AUDIO);
            }
        } else {
            mirage_fragment_main_data_set_format(fragment, MIRAGE_MAIN_DATA_FORMAT_DATA);
        }
    } else {
        /* Cooked mode */
        switch (mirage_track_get_sector_type(track)) {
            case MIRAGE_SECTOR_AUDIO: {
                extension = mirage_writer_get_parameter_string(_self, PARAM_AUDIO_FILE_SUFFIX);
                mirage_fragment_main_data_set_size(fragment, 2352);
                mirage_fragment_main_data_set_format(fragment, MIRAGE_MAIN_DATA_FORMAT_AUDIO);
                filter_chain = audio_filter_chain;
                break;
            }
            case MIRAGE_SECTOR_MODE1:
            case MIRAGE_SECTOR_MODE2_FORM1: {
                extension = "iso";
                mirage_fragment_main_data_set_size(fragment, 2048);
                mirage_fragment_main_data_set_format(fragment, MIRAGE_MAIN_DATA_FORMAT_DATA);
                break;
            }
            case MIRAGE_SECTOR_MODE2:
            case MIRAGE_SECTOR_MODE2_FORM2:
            case MIRAGE_SECTOR_MODE2_MIXED: {
                extension = "bin";
                mirage_fragment_main_data_set_size(fragment, 2336);
                mirage_fragment_main_data_set_format(fragment, MIRAGE_MAIN_DATA_FORMAT_DATA);
                break;
            }
            default: {
                /* Should not happen, but just in case */
                extension = "bin";
                mirage_fragment_main_data_set_size(fragment, 2352);
                mirage_fragment_main_data_set_format(fragment, MIRAGE_MAIN_DATA_FORMAT_DATA);
                break;
            }
        }
    }

    /* Subchannel; only internal PW96 interleaved is supported */
    if (write_subchannel) {
        mirage_fragment_subchannel_data_set_format(fragment, MIRAGE_SUBCHANNEL_DATA_FORMAT_PW96_INTERLEAVED | MIRAGE_SUBCHANNEL_DATA_FORMAT_INTERNAL);
        mirage_fragment_subchannel_data_set_size(fragment, 96);
    }

    /* Format filename */
    gint session_number = mirage_track_layout_get_session_number(track);
    gint track_number = mirage_track_layout_get_track_number(track);

    if (session_number > 1) {
        /* If session number is greater than one, we need to specify
           both session and track number */
        filename = mirage_helper_format_string(image_file_format,
            "b", g_variant_new_string(self->priv->image_file_basename),
            "s", g_variant_new_int16(session_number),
            "t", g_variant_new_int16(track_number),
            "e", g_variant_new_string(extension), NULL);
    } else if (track_number > 1) {
        /* If track number is greater than one, we need to specify it */
        filename = mirage_helper_format_string(image_file_format,
            "b", g_variant_new_string(self->priv->image_file_basename),
            "t", g_variant_new_int16(track_number),
            "e", g_variant_new_string(extension), NULL);
    } else {
        /* First track of first session; specify only basename and extension */
        filename = mirage_helper_format_string(image_file_format,
            "b", g_variant_new_string(self->priv->image_file_basename),
            "e", g_variant_new_string(extension), NULL);
    }

    /* I/O stream */
    stream = mirage_contextual_create_output_stream(MIRAGE_CONTEXTUAL(self), filename, filter_chain, error);
    if (!stream) {
        g_object_unref(fragment);
        return NULL;
    }

    mirage_fragment_main_data_set_stream(fragment, stream);

    /* We keep a list of streams for images of tracks in first session,
       because we might need to rename them */
    if (session_number == 1) {
        self->priv->image_file_streams = g_list_append(self->priv->image_file_streams, g_object_ref(stream));
    }

    g_object_unref(stream);

    return fragment;
}

static gboolean mirage_writer_iso_finalize_image (MirageWriter *_self, MirageDisc *disc, GError **error G_GNUC_UNUSED)
{
    MirageWriterIso *self = MIRAGE_WRITER_ISO(_self);

    /* Rename some of image files, if necessary */
    mirage_writer_iso_rename_track_image_files(self, disc);

    /* Go over disc, and gather the names of track files */
    mirage_writer_iso_update_disc_filenames(self, disc);

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
        "ISO Image Writer"
    );

    self->priv->image_file_basename = NULL;
    self->priv->image_file_streams = NULL;

    /* Create parameter sheet */
    mirage_writer_add_parameter_enum(MIRAGE_WRITER(self),
        PARAM_AUDIO_FILE_SUFFIX,
        "Audio file suffix",
        "Suffix to use for image files of audio tracks. Applicable only when in raw write is disabled.",
        "wav",
        "wav", "aiff", "ogg", "flac", "cdr", NULL);

    mirage_writer_add_parameter_boolean(MIRAGE_WRITER(self),
        PARAM_WRITE_RAW,
        "Write raw",
        "A flag indicating whether to write full 2352-byte sector data or only user data part of it (e.g., 2048 bytes for Mode 1)",
        FALSE);

    mirage_writer_add_parameter_boolean(MIRAGE_WRITER(self),
        PARAM_WRITE_SUBCHANNEL,
        "Write subchannel",
        "A flag indicating whether to write subchannel data or not. If set, it implies raw writing.",
        FALSE);

    mirage_writer_add_parameter_boolean(MIRAGE_WRITER(self),
        PARAM_SWAP_RAW_AUDIO_DATA,
        "Swap raw audio data",
        "A flag indicating whether to swap audio data. Applicable only to raw writing.",
        FALSE);
}

static void mirage_writer_iso_dispose (GObject *gobject)
{
    MirageWriterIso *self = MIRAGE_WRITER_ISO(gobject);

    /* Clear the list of image file streams */
    g_list_free_full(self->priv->image_file_streams, (GDestroyNotify)g_object_unref);
    self->priv->image_file_streams = NULL;

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_writer_iso_parent_class)->dispose(gobject);
}

static void mirage_writer_iso_class_init (MirageWriterIsoClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageWriterClass *writer_class = MIRAGE_WRITER_CLASS(klass);

    gobject_class->dispose = mirage_writer_iso_dispose;

    writer_class->open_image = mirage_writer_iso_open_image;
    writer_class->create_fragment = mirage_writer_iso_create_fragment;
    writer_class->finalize_image = mirage_writer_iso_finalize_image;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageWriterIsoPrivate));
}

static void mirage_writer_iso_class_finalize (MirageWriterIsoClass *klass G_GNUC_UNUSED)
{
}
