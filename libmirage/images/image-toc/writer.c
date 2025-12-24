/*
 *  libMirage: TOC image: writer
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

#include "image-toc.h"

#define __debug__ "TOC-Writer"

#define PARAM_AUDIO_FILE_SUFFIX "writer.audio_file_suffix"
#define PARAM_WRITE_RAW "writer.write_raw"
#define PARAM_WRITE_SUBCHANNEL "writer.write_subchannel"
#define PARAM_SWAP_RAW_AUDIO_DATA "writer.swap_raw_audio"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
struct _MirageWriterTocPrivate
{
    gchar *image_file_basename;

    GList *image_file_streams;
};

static const gchar *audio_filter_chain[] = {
    "MirageFilterStreamSndfile",
    NULL
};

static const gchar toc_file_format[] = "%b-%02s.toc";
static const gchar data_file_format[] = "%b-%02s-%02t.%e";


/**********************************************************************\
 *                          Helper functions                          *
\**********************************************************************/
static void mirage_writer_toc_rename_track_image_files (MirageWriterToc *self, MirageDisc *disc)
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
                new_filename = mirage_helper_format_string(data_file_format,
                    "b", g_variant_new_string(self->priv->image_file_basename),
                    "t", g_variant_new_int16(track),
                    "e", g_variant_new_string(extension), NULL);
            } else {
                new_filename = mirage_helper_format_string(data_file_format,
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

static void dump_language (GString *toc_contents, gint index, MirageLanguage *language)
{
    static struct {
        gchar *pack_id;
        gint pack_type;
    } packs[] = {
        { "TITLE", MIRAGE_LANGUAGE_PACK_TITLE },
        { "PERFORMER", MIRAGE_LANGUAGE_PACK_PERFORMER },
        { "SONGWRITER", MIRAGE_LANGUAGE_PACK_SONGWRITER },
        { "COMPOSER", MIRAGE_LANGUAGE_PACK_COMPOSER },
        { "ARRANGER", MIRAGE_LANGUAGE_PACK_ARRANGER },
        { "MESSAGE", MIRAGE_LANGUAGE_PACK_MESSAGE },
        { "DISC_ID", MIRAGE_LANGUAGE_PACK_DISC_ID },
        /*{ "GENRE", MIRAGE_LANGUAGE_PACK_GENRE },*/
        /*{ "TOC_INFO1", MIRAGE_LANGUAGE_PACK_TOC },*/
        /*{ "TOC_INFO2", MIRAGE_LANGUAGE_PACK_TOC2 },*/
        { "UPC_EAN", MIRAGE_LANGUAGE_PACK_UPC_ISRC },
        /*{ "SIZE_INFO", MIRAGE_LANGUAGE_PACK_SIZE },*/
    };

    g_string_append_printf(toc_contents, "  LANGUAGE %d {\n", index);

    for (guint i = 0; i < G_N_ELEMENTS(packs); i++) {
        const guint8 *pack_data;
        gint pack_length = 0;
        if (mirage_language_get_pack_data(language, packs[i].pack_type, &pack_data, &pack_length, NULL)) {
            g_string_append_printf(toc_contents, "    %s \"%.*s\"\n", packs[i].pack_id, pack_length, pack_data);
        }
    }

    g_string_append_printf(toc_contents, "  }\n");
}

static GString *mirage_writer_toc_create_toc_file (MirageWriterToc *self G_GNUC_UNUSED, MirageSession *session)
{
    GString *toc_contents = g_string_new(NULL);

    gboolean write_raw = mirage_writer_get_parameter_boolean(MIRAGE_WRITER(self), PARAM_WRITE_RAW);
    gboolean write_subchannel = mirage_writer_get_parameter_boolean(MIRAGE_WRITER(self), PARAM_WRITE_SUBCHANNEL);

    /* Session type */
    switch (mirage_session_get_session_type(session)) {
        case MIRAGE_SESSION_CDDA: {
            g_string_append_printf(toc_contents, "CD_DA\n\n");
            break;
        }
        case MIRAGE_SESSION_CDROM: {
            g_string_append_printf(toc_contents, "CD_ROM\n\n");
            break;
        }
        case MIRAGE_SESSION_CDI: {
            g_string_append_printf(toc_contents, "CD_I\n\n");
            break;
        }
        case MIRAGE_SESSION_CDROM_XA: {
            g_string_append_printf(toc_contents, "CD_ROM_XA\n");
            break;
        }
    }

    /* Catalog */
    const gchar *mcn = mirage_session_get_mcn(session);
    if (mcn) {
        g_string_append_printf(toc_contents, "\nCATALOG \"%s\"\n", mcn);
    }

    /* Languages (CD-TEXT) */
    gint num_languages = mirage_session_get_number_of_languages(session);
    if (num_languages) {
        g_string_append_printf(toc_contents, "\nCD_TEXT {\n");

        /* First pass: language map */
        g_string_append_printf(toc_contents, "  LANGUAGE_MAP {\n");
        for (gint i = 0; i < num_languages; i++) {
            MirageLanguage *language = mirage_session_get_language_by_index(session, i, NULL);
            g_string_append_printf(toc_contents, "    %d: %d\n", i, mirage_language_get_code(language));
            g_object_unref(language);
        }
        g_string_append_printf(toc_contents, "  }\n");

        /* Second pass: dump session's languages */
        for (gint i = 0; i < num_languages; i++) {
            MirageLanguage *language = mirage_session_get_language_by_index(session, i, NULL);
            dump_language(toc_contents, i, language);
            g_object_unref(language);
        }

        g_string_append_printf(toc_contents, "}\n");
    }

    /* Tracks */
    gint num_tracks = mirage_session_get_number_of_tracks(session);
    for (gint i = 0; i < num_tracks; i++) {
        MirageTrack *track = mirage_session_get_track_by_index(session, i, NULL);
        gint track_number = mirage_track_layout_get_track_number(track);
        gint sector_type = mirage_track_get_sector_type(track);
        gint flags = mirage_track_get_flags(track);
        gint track_start = mirage_track_get_track_start(track);
        gint num_fragments = mirage_track_get_number_of_fragments(track);

        g_string_append_printf(toc_contents, "\n// Track %d\n", track_number);

        /* Track type */
        g_string_append_printf(toc_contents, "TRACK ");
        switch (sector_type) {
            case MIRAGE_SECTOR_AUDIO: {
                g_string_append_printf(toc_contents, "AUDIO");
                break;
            }
            case MIRAGE_SECTOR_MODE1: {
                if (write_raw || write_subchannel) {
                    g_string_append_printf(toc_contents, "MODE1_RAW");
                } else {
                    g_string_append_printf(toc_contents, "MODE1");
                }
                break;
            }
            case MIRAGE_SECTOR_MODE2: {
                if (write_raw || write_subchannel) {
                    g_string_append_printf(toc_contents, "MODE2_RAW");
                } else {
                    g_string_append_printf(toc_contents, "MODE2");
                }
                break;
            }
            case MIRAGE_SECTOR_MODE2_FORM1: {
                if (write_raw || write_subchannel) {
                    g_string_append_printf(toc_contents, "MODE2_RAW");
                } else {
                    g_string_append_printf(toc_contents, "MODE2_FORM1");
                }
                break;
            }
            case MIRAGE_SECTOR_MODE2_FORM2: {
                if (write_raw || write_subchannel) {
                    g_string_append_printf(toc_contents, "MODE2_RAW");
                } else {
                    g_string_append_printf(toc_contents, "MODE2_FORM2");
                }
                break;
            }
            case MIRAGE_SECTOR_MODE2_MIXED: {
                if (write_raw | write_subchannel) {
                    g_string_append_printf(toc_contents, "MODE2_RAW");
                } else {
                    g_string_append_printf(toc_contents, "MODE2_MIXED");
                }
                break;
            }
        }

        if (write_subchannel) {
            g_string_append_printf(toc_contents, " RW_RAW\n");
        } else {
            g_string_append_printf(toc_contents, "\n");
        }

        /* Track flags */
        if (sector_type == MIRAGE_SECTOR_AUDIO) {
            /* Copy */
            if (flags & MIRAGE_TRACK_FLAG_COPYPERMITTED) {
                g_string_append_printf(toc_contents, "COPY\n");
            } else {
                g_string_append_printf(toc_contents, "NO COPY\n");
            }
            /* Pre-emphasis */
            if (flags & MIRAGE_TRACK_FLAG_PREEMPHASIS) {
                g_string_append_printf(toc_contents, "PRE_EMPHASIS\n");
            } else {
                g_string_append_printf(toc_contents, "NO PRE_EMPHASIS\n");
            }
            /* Four-channel */
            if (flags & MIRAGE_TRACK_FLAG_FOURCHANNEL) {
                g_string_append_printf(toc_contents, "FOUR_CHANNEL_AUDIO\n");
            } else {
                g_string_append_printf(toc_contents, "TWO_CHANNEL_AUDIO\n");
            }

            /* ISRC */
            const gchar *isrc = mirage_track_get_isrc(track);
            if (isrc) {
                g_string_append_printf(toc_contents, "ISRC \"%s\"\n", isrc);
            }
        } else {
            /* Copy */
            if (flags & MIRAGE_TRACK_FLAG_COPYPERMITTED) {
                g_string_append_printf(toc_contents, "COPY\n");
            } else {
                g_string_append_printf(toc_contents, "NO COPY\n");
            }
        }

        /* Languages (CD-TEXT) */
        num_languages = mirage_track_get_number_of_languages(track);
        if (num_languages) {
            g_string_append_printf(toc_contents, "CD_TEXT {\n");

            /* Dump track's languages */
            for (gint j = 0; j < num_languages; j++) {
                MirageLanguage *language = mirage_track_get_language_by_index(track, j, NULL);
                dump_language(toc_contents, j, language);
                g_object_unref(language);
            }

            g_string_append_printf(toc_contents, "}\n");
        }

        /* List fragments */
        for (gint j = 0; j < num_fragments; j++) {
            MirageFragment *fragment = mirage_track_get_fragment_by_index(track, j, NULL);
            const gchar *filename = mirage_fragment_main_data_get_filename(fragment);
            gint length = mirage_fragment_get_length(fragment);
            gchar *length_msf = mirage_helper_lba2msf_str(length, FALSE);

            if (filename) {
                GFile *tmp_file = g_file_new_for_path(filename);
                gchar *short_filename = g_file_get_basename(tmp_file);

                /* Data fragment */
                if (sector_type == MIRAGE_SECTOR_AUDIO && !write_subchannel) {
                    g_string_append_printf(toc_contents, "FILE \"%s\" %d %s\n", short_filename, 0, length_msf);
                } else {
                    gint length_bytes = length * (mirage_fragment_main_data_get_size(fragment) + mirage_fragment_subchannel_data_get_size(fragment));
                    g_string_append_printf(toc_contents, "DATAFILE \"%s\" %s // length in bytes: %d\n", short_filename, length_msf, length_bytes);
                }

                g_free(short_filename);
                g_object_unref(tmp_file);
            } else {
                /* Pregap fragment */
                if (i == 0 && j == 0) {
                    /* TOC does not list first track's pregap. Unless it
                       is greater than standard 150? But the first fragment
                       (j == 0) always model the standard pregap */
                    g_free(length_msf);
                    g_object_unref(fragment);
                    continue;
                }

                if (sector_type == MIRAGE_SECTOR_AUDIO && !write_subchannel) {
                    g_string_append_printf(toc_contents, "SILENCE %s\n", length_msf);
                } else {
                    g_string_append_printf(toc_contents, "ZERO %s\n", length_msf);
                }
            }

            g_free(length_msf);
            g_object_unref(fragment);
        }

        /* Track start */
        if (track_start) {
            if (i == 0) {
                /* First track is a special case, since TOC does not list
                   its standard 150-sector pregap */
                if (track_start <= 150) {
                    g_object_unref(track);
                    continue;
                } else {
                    track_start -= 150;
                }
            }

            gchar *track_start_msf = mirage_helper_lba2msf_str(track_start, FALSE);
            g_string_append_printf(toc_contents, "START %s\n", track_start_msf);
            g_free(track_start_msf);
        }

        g_object_unref(track);
    }

    return toc_contents;
}

static gboolean mirage_writer_toc_create_toc_files (MirageWriterToc *self, MirageDisc *disc, GError **error)
{
    gint num_sessions = mirage_disc_get_number_of_sessions(disc);
    gchar **filenames = g_new0(gchar *, num_sessions + 1);
    gboolean succeeded = TRUE;

    for (gint i = 0; i < num_sessions; i++) {
        MirageSession *session = mirage_disc_get_session_by_index(disc, i, NULL);
        gchar *filename;

        /* Format TOC filename */
        if (num_sessions > 1) {
            /* If we have more than one session, we put session number
               in TOC file name */
            filename = mirage_helper_format_string(toc_file_format,
                "b", g_variant_new_string(self->priv->image_file_basename),
                "s", g_variant_new_int16(mirage_session_layout_get_session_number(session)), NULL);
        } else {
            /* Single session */
            filename = mirage_helper_format_string(toc_file_format,
                "b", g_variant_new_string(self->priv->image_file_basename), NULL);
        }

        /* Store in our array */
        filenames[i] = filename;

        /* Open stream on TOC file */
        MirageStream *toc_stream = mirage_contextual_create_output_stream(MIRAGE_CONTEXTUAL(self), filename, NULL, error);
        if (toc_stream) {
            /* Generate TOC file contents */
            GString *toc_contents = mirage_writer_toc_create_toc_file(self, session);

            /* Write */
            if ((gsize)mirage_stream_write(toc_stream, toc_contents->str, toc_contents->len, error) != (toc_contents->len)) {
                succeeded = FALSE;
            }

            g_object_unref(toc_stream);
            g_string_free(toc_contents, TRUE);
        } else {
            succeeded = FALSE;
        }

        g_object_unref(session);

        if (!succeeded) {
            break;
        }
    }

    /* Set disc filenames */
    mirage_disc_set_filenames(disc, filenames);

    g_strfreev(filenames);

    return TRUE;
}


/**********************************************************************\
 *                MirageWriter methods implementation                 *
\**********************************************************************/
static gboolean mirage_writer_toc_open_image_impl (MirageWriter *_self, MirageDisc *disc, GError **error G_GNUC_UNUSED)
{
    MirageWriterToc *self = MIRAGE_WRITER_TOC(_self);

    /* This writer supports only CD-ROM medium */
    if (mirage_disc_get_medium_type(disc) != MIRAGE_MEDIUM_CD) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: TOC image writer supports only CD-ROM medium format!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_WRITER_ERROR, Q_("Unsupported medium format!"));
        return FALSE;
    }

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
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: write raw: %d\n", __debug__, mirage_writer_get_parameter_boolean(_self, PARAM_WRITE_RAW));
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: write subchannel: %d\n", __debug__, mirage_writer_get_parameter_boolean(_self, PARAM_WRITE_SUBCHANNEL));
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: swap raw audio data: %d\n", __debug__, mirage_writer_get_parameter_boolean(_self, PARAM_SWAP_RAW_AUDIO_DATA));

    return TRUE;
}

static MirageFragment *mirage_writer_toc_create_fragment (MirageWriter *_self, MirageTrack *track, MirageFragmentRole role, GError **error)
{
    MirageWriterToc *self = MIRAGE_WRITER_TOC(_self);

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
                extension = "wav";
                mirage_fragment_main_data_set_size(fragment, 2352);
                mirage_fragment_main_data_set_format(fragment, MIRAGE_MAIN_DATA_FORMAT_AUDIO);
                filter_chain = audio_filter_chain;
                break;
            }
            case MIRAGE_SECTOR_MODE1:
            case MIRAGE_SECTOR_MODE2_FORM1: {
                extension = "bin";
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
        filename = mirage_helper_format_string(data_file_format,
            "b", g_variant_new_string(self->priv->image_file_basename),
            "s", g_variant_new_int16(session_number),
            "t", g_variant_new_int16(track_number),
            "e", g_variant_new_string(extension), NULL);
    } else if (track_number > 1) {
        /* If track number is greater than one, we need to specify it */
        filename = mirage_helper_format_string(data_file_format,
            "b", g_variant_new_string(self->priv->image_file_basename),
            "t", g_variant_new_int16(track_number),
            "e", g_variant_new_string(extension), NULL);
    } else {
        /* First track of first session; specify only basename and extension */
        filename = mirage_helper_format_string(data_file_format,
            "b", g_variant_new_string(self->priv->image_file_basename),
            "e", g_variant_new_string(extension), NULL);
    }

    /* I/O stream */
    stream = mirage_contextual_create_output_stream(MIRAGE_CONTEXTUAL(self), filename, filter_chain, error);
    g_free(filename);
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

static gboolean mirage_writer_toc_finalize_image (MirageWriter *_self, MirageDisc *disc, GError **error G_GNUC_UNUSED)
{
    MirageWriterToc *self = MIRAGE_WRITER_TOC(_self);

    /* Rename some of image files, if necessary */
    mirage_writer_toc_rename_track_image_files(self, disc);

    /* Create TOC files */
    return mirage_writer_toc_create_toc_files(self, disc, error);
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE_EXTENDED(MirageWriterToc,
                               mirage_writer_toc,
                               MIRAGE_TYPE_WRITER,
                               0,
                               G_ADD_PRIVATE_DYNAMIC(MirageWriterToc))

void mirage_writer_toc_type_register (GTypeModule *type_module)
{
    return mirage_writer_toc_register_type(type_module);
}

static void mirage_writer_toc_init (MirageWriterToc *self)
{
    self->priv = mirage_writer_toc_get_instance_private(self);

    mirage_writer_generate_info(MIRAGE_WRITER(self),
        "WRITER-TOC",
        Q_("TOC Image Writer")
    );

    self->priv->image_file_basename = NULL;
    self->priv->image_file_streams = NULL;

    /* Create parameter sheet */
    mirage_writer_add_parameter_boolean(MIRAGE_WRITER(self),
        PARAM_WRITE_RAW,
        Q_("Write raw"),
        Q_("A flag indicating whether to write full 2352-byte sector data or only user data part of it (e.g., 2048 bytes for Mode 1)"),
        FALSE);

    mirage_writer_add_parameter_boolean(MIRAGE_WRITER(self),
        PARAM_WRITE_SUBCHANNEL,
        Q_("Write subchannel"),
        Q_("A flag indicating whether to write subchannel data or not. If set, it implies raw writing."),
        FALSE);

    mirage_writer_add_parameter_boolean(MIRAGE_WRITER(self),
        PARAM_SWAP_RAW_AUDIO_DATA,
        Q_("Swap raw audio data"),
        Q_("A flag indicating whether to swap audio data. Applicable only to raw writing."),
        TRUE);
}

static void mirage_writer_toc_dispose (GObject *gobject)
{
    MirageWriterToc *self = MIRAGE_WRITER_TOC(gobject);

    /* Clear the list of image file streams */
    g_list_free_full(self->priv->image_file_streams, (GDestroyNotify)g_object_unref);
    self->priv->image_file_streams = NULL;

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_writer_toc_parent_class)->dispose(gobject);
}

static void mirage_writer_toc_finalize (GObject *gobject)
{
    MirageWriterToc *self = MIRAGE_WRITER_TOC(gobject);

    /* Free the image basename */
    g_free(self->priv->image_file_basename);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_writer_toc_parent_class)->finalize(gobject);
}

static void mirage_writer_toc_class_init (MirageWriterTocClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageWriterClass *writer_class = MIRAGE_WRITER_CLASS(klass);

    gobject_class->dispose = mirage_writer_toc_dispose;
    gobject_class->finalize = mirage_writer_toc_finalize;

    writer_class->open_image_impl = mirage_writer_toc_open_image_impl;
    writer_class->create_fragment = mirage_writer_toc_create_fragment;
    writer_class->finalize_image = mirage_writer_toc_finalize_image;
}

static void mirage_writer_toc_class_finalize (MirageWriterTocClass *klass G_GNUC_UNUSED)
{
}
