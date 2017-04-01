/*
 *  libMirage: ISO image: parser
 *  Copyright (C) 2006-2014 Rok Mandeljc
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

#define __debug__ "ISO-Parser"

struct IsoFileInfo
{
    gint main_data_size;
    gint main_data_format;
    gint track_mode;

    gint subchannel_data_size;
    gint subchannel_format;

    const gchar *filename;
    MirageStream *stream;
};


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_PARSER_ISO_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_ISO, MirageParserIsoPrivate))

struct _MirageParserIsoPrivate
{
    MirageDisc *disc;
};


/**********************************************************************\
 *                         File identification                        *
\**********************************************************************/
static gboolean mirage_parser_iso_determine_sector_size (MirageParserIso *self, struct IsoFileInfo *file_info, GError **error)
{
    const gint valid_sector_sizes[] = { 2048, 2332, 2336, 2352 };
    const gint data_offset[] = { 0, 0, 0, 16 };
    const gint valid_subchannel_sizes[] = { 0, 16, 96 };

    MirageStream *stream = file_info->stream;

    gsize file_length;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: verifying file size...\n", __debug__);

    /* Get stream length */
    if (!mirage_stream_seek(stream, 0, G_SEEK_END, error)) {
        return FALSE;
    }
    file_length = mirage_stream_tell(stream);

    /* Make sure the file is large enough; INF8090 requires a track to
       be at least four seconds long */
    if (file_length < 4*75*2048) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: parser cannot handle given image: file is too small!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Parser cannot handle given image: file is too small!"));
        return FALSE;
    }

    /* Check all possible combinations of sector data and subchannel sizes */
    for (gint i = 0; i < G_N_ELEMENTS(valid_subchannel_sizes); i++) {
        for (gint j = 0; j < G_N_ELEMENTS(valid_sector_sizes); j++) {
            gint full_sector_size = valid_sector_sizes[j] + valid_subchannel_sizes[i];
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: checking %d-byte sector size with %d-byte subchannel...\n", __debug__, valid_sector_sizes[j], valid_subchannel_sizes[i]);

            /* Check if file size is a multiple of full sector size */
            if (file_length % full_sector_size != 0) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: file size is not a multiple of %d!\n", __debug__, full_sector_size);
                continue;
            }

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: file size check passed; looking for CD001/BEA01 pattern at sector 16...\n", __debug__);

            /* Check for CD001 or BEA01 at sector 16 */
            guint8 buf[8];
            goffset offset = 16*full_sector_size + data_offset[j];

            if (!mirage_stream_seek(stream, offset, G_SEEK_SET, NULL)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to %" G_GOFFSET_MODIFIER "Xh to read 8-byte pattern!\n", __debug__, offset);

                gchar tmp[100] = ""; /* Work-around for lack of direct G_GOFFSET_MODIFIER support in xgettext() */
                g_snprintf(tmp, sizeof(tmp)/sizeof(tmp[0]), "%" G_GINT64_MODIFIER "Xh", offset);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Failed to seek to %s to read 8-byte pattern!"), tmp);

                return FALSE;
            }

            if (mirage_stream_read(stream, buf, 8, NULL) != 8) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read 8-byte pattern!\n", __debug__);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Failed to read 8-byte pattern!"));
                return FALSE;
            }

            if (!memcmp(buf, mirage_pattern_cd001, sizeof(mirage_pattern_cd001))
                || !memcmp(buf, mirage_pattern_bea01, sizeof(mirage_pattern_bea01))) {
                file_info->main_data_size = valid_sector_sizes[j];
                file_info->subchannel_data_size = valid_subchannel_sizes[i];
                file_info->main_data_format = MIRAGE_MAIN_DATA_FORMAT_DATA;

                MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: image is an ISO9660/UDF image, with %d-byte sector data and %d-byte subchannel data\n", __debug__, file_info->main_data_size, file_info->subchannel_data_size);
                return TRUE;
            } else {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: CD001/BEA01 pattern not found!\n", __debug__);
            }
        }
    }

    /* Now that we have ruled out any possible combination for a data
       track, if the stream length is multiple of 2352, we assume that
       we are dealing with audio track data */
    if (file_length % 2352 == 0) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: file size is multiple of 2352; assuming file contains audio track data...\n", __debug__);
        file_info->main_data_size = 2352;
        file_info->subchannel_data_size = 0;
        file_info->main_data_format = MIRAGE_MAIN_DATA_FORMAT_AUDIO;
        return TRUE;
    }

    /* No supported combination */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: parser cannot handle given image: file size does not correspond to any supported combination of sector and subchannel size!\n", __debug__);
    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Parser cannot handle given image: file size does not correspond to any supported combination of sector and subchannel size!"));
    return FALSE;
}

static gboolean mirage_parser_iso_determine_track_type (MirageParserIso *self, struct IsoFileInfo *file_info, GError **error)
{
    MirageStream *stream = file_info->stream;

    /* We try to guess track type from main channel data size */
    switch (file_info->main_data_size) {
        case 2048: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 2048-byte main sector data; assuming Mode 1 track\n", __debug__);
            file_info->track_mode = MIRAGE_SECTOR_MODE1;
            break;
        }
        case 2332:
        case 2336: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 2332/2336-byte main sector data; assuming Mode 2 track\n", __debug__);
            file_info->track_mode = MIRAGE_SECTOR_MODE2_MIXED;
            break;
        }
        case 2352: {
            guint8 buf[16];
            goffset offset = 16 * (file_info->main_data_size + file_info->subchannel_data_size);

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 2352-byte main sector data; determining track type at address 16 (offset %" G_GOFFSET_MODIFIER "Xh)...\n", __debug__, offset);

            if (!mirage_stream_seek(stream, offset, G_SEEK_SET, NULL)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to offset %" G_GOFFSET_MODIFIER "Xh to read 16-byte pattern!\n", __debug__, offset);

                gchar tmp[100] = ""; /* Work-around for lack of direct G_GOFFSET_MODIFIER support in xgettext() */
                g_snprintf(tmp, sizeof(tmp)/sizeof(tmp[0]), "%" G_GINT64_MODIFIER "Xh", offset);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Failed to seek to offset %s to read 16-byte pattern!"), tmp);

                return FALSE;
            }

            if (mirage_stream_read(stream, buf, 16, NULL) != 16) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read 16-byte pattern!\n", __debug__);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Failed to read 16-byte pattern!"));
                return FALSE;
            }

            file_info->track_mode = mirage_helper_determine_sector_type(buf);
            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unhalded main sector data size %d!\n", __debug__, file_info->main_data_size);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Unhalded main sector data size %d!"), file_info->main_data_size);
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean mirage_parser_iso_determine_subchannel_type (MirageParserIso *self, struct IsoFileInfo *file_info, GError **error)
{
    MirageStream *stream = file_info->stream;

    switch (file_info->subchannel_data_size) {
        case 0: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: no subchannel data found!\n", __debug__);
            file_info->subchannel_format = 0;
            break;
        }
        case 16: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 16-byte internal Q subchannel data found!\n", __debug__);
            file_info->subchannel_format = MIRAGE_SUBCHANNEL_DATA_FORMAT_INTERNAL | MIRAGE_SUBCHANNEL_DATA_FORMAT_Q16;
            break;
        }
        case 96: {
            guint8 buf[96];
            goffset offset = 16 * (file_info->main_data_size + file_info->subchannel_data_size) + file_info->main_data_size;

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 96-byte internal PW subchannel data found!\n", __debug__);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: determining whether it is linear or interleaved from subchannel data of sector 16 (offset %" G_GOFFSET_MODIFIER "Xh)...\n", __debug__, offset);

            if (!mirage_stream_seek(stream, offset, G_SEEK_SET, NULL)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to offset %" G_GOFFSET_MODIFIER "Xh to read subchannel data!\n", __debug__, offset);

                gchar tmp[100] = ""; /* Work-around for lack of direct G_GOFFSET_MODIFIER support in xgettext() */
                g_snprintf(tmp, sizeof(tmp)/sizeof(tmp[0]), "%" G_GINT64_MODIFIER "Xh", offset);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Failed to seek to offset %s to read subchannel data!"), tmp);

                return FALSE;
            }

            if (mirage_stream_read(stream, buf, sizeof(buf), NULL) != sizeof(buf)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read subchannel data!\n", __debug__);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Failed to read subchannel data!"));
                return FALSE;
            }

            /* Determine whether subchannel data is linear or interleaved;
               we assume it is linear, and validate CRC over Q-channel
               data. An alternative would be to validate address stored in
               subchannel data... */
            guint16 crc = mirage_helper_subchannel_q_calculate_crc(buf+12);
            if ((buf[22] << 8 | buf[23]) == crc) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: subchannel data appears to be linear!\n", __debug__);
                file_info->subchannel_format = MIRAGE_SUBCHANNEL_DATA_FORMAT_INTERNAL | MIRAGE_SUBCHANNEL_DATA_FORMAT_PW96_LINEAR;
            } else {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: subchannel data appears to be interleaved!\n", __debug__);
                file_info->subchannel_format = MIRAGE_SUBCHANNEL_DATA_FORMAT_INTERNAL | MIRAGE_SUBCHANNEL_DATA_FORMAT_PW96_INTERLEAVED;
            }

            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unhalded subchannel data size %d!\n", __debug__, file_info->subchannel_data_size);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Unhalded subchannel data size %d!"), file_info->subchannel_data_size);
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean mirage_parser_iso_load_track (MirageParserIso *self, const struct IsoFileInfo *file_info, gboolean add_pregap, GError **error)
{
    MirageSession *session;
    MirageTrack *track;
    MirageFragment *fragment;

    /* Add track */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding track\n", __debug__);

    session = mirage_disc_get_session_by_index(self->priv->disc, -1, NULL);

    track = g_object_new(MIRAGE_TYPE_TRACK, NULL);
    mirage_session_add_track_by_index(session, -1, track);
    g_object_unref(session);

    /* Set track mode */
    mirage_track_set_sector_type(track, file_info->track_mode);

    /* Create pregap fragment */
    if (add_pregap) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating pregap fragment\n", __debug__);
        fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL);

        mirage_fragment_set_length(fragment, 150);
        mirage_track_add_fragment(track, -1, fragment);

        g_object_unref(fragment);
    }

    /* Create data fragment */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating data fragment\n", __debug__);
    fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL);

    mirage_fragment_main_data_set_stream(fragment, file_info->stream);
    mirage_fragment_main_data_set_size(fragment, file_info->main_data_size);
    mirage_fragment_main_data_set_format(fragment, file_info->main_data_format);
    mirage_fragment_subchannel_data_set_size(fragment, file_info->subchannel_data_size);
    mirage_fragment_subchannel_data_set_format(fragment, file_info->subchannel_format);

    /* Use whole file */
    if (!mirage_fragment_use_the_rest_of_file(fragment, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to use the rest of file!\n", __debug__);
        g_object_unref(fragment);
        g_object_unref(track);
        return FALSE;
    }

    mirage_track_add_fragment(track, -1, fragment);

    g_object_unref(fragment);
    g_object_unref(track);

    return TRUE;
}


/**********************************************************************\
 *                MirageParser methods implementation                *
\**********************************************************************/
static MirageDisc *mirage_parser_iso_load_image (MirageParser *_self, MirageStream **streams, GError **error)
{
    MirageParserIso *self = MIRAGE_PARSER_ISO(_self);
    struct IsoFileInfo *file_info;
    gchar **filenames;
    gint num_files;

    gboolean succeeded = TRUE;

    /* Determine number of streams */
    for (num_files = 0; streams[num_files]; num_files++);
    file_info = g_new0(struct IsoFileInfo, num_files);
    filenames = g_new0(gchar *, num_files + 1);

    /* Check if all files can be loaded */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: checking if parser can handle given image...\n", __debug__);
    for (gint i = 0; i < num_files; i++) {
        file_info[i].stream = streams[i];
        filenames[i] = g_strdup(mirage_stream_get_filename(streams[i]));

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: checking file #%d: '%s'\n", __debug__, i, filenames[i]);
        if (!mirage_parser_iso_determine_sector_size(self, &file_info[i], error)) {
            g_free(file_info);
            g_strfreev(filenames);
            return FALSE;
        }
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: parser can handle given image!\n", __debug__);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing the image...\n", __debug__);

    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_set_parent(MIRAGE_OBJECT(self->priv->disc), self);

    /* Session: one session (with possibly multiple tracks) */
    MirageSession *session = g_object_new(MIRAGE_TYPE_SESSION, NULL);
    mirage_disc_add_session_by_index(self->priv->disc, 0, session);

    /* ISO image parser assumes single-session image, so we're dealing with regular CD-ROM session */
    mirage_session_set_session_type(session, MIRAGE_SESSION_CDROM);
    g_object_unref(session);

    /* Go over all files and create tracks */
    for (gint i = 0; i < num_files; i++) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: file #%d: '%s'\n", __debug__, i, filenames[i]);

        /* Determine track and subchannel mode */
        if (!mirage_parser_iso_determine_track_type(self, &file_info[i], error)) {
            succeeded = FALSE;
            break;
        }
        if (!mirage_parser_iso_determine_subchannel_type(self, &file_info[i], error)) {
            succeeded = FALSE;
            break;
        }

        /* Load track; in the complete absence of any relevant information,
           we construct the image in TAO mode (pregaps between tracks) */
        if (!mirage_parser_iso_load_track(self, &file_info[i], i > 0, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to load track!\n", __debug__);
            succeeded = FALSE;
            break;
        }
    }

    /* Finishing touch */
    if (succeeded) {
        /* Set filenames */
        mirage_disc_set_filenames(self->priv->disc, filenames);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finishing the layout\n", __debug__);

        /* Now guess medium type and if it's a CD-ROM, add Red Book pregap */
        gint medium_type = mirage_parser_guess_medium_type(MIRAGE_PARSER(self), self->priv->disc);
        mirage_disc_set_medium_type(self->priv->disc, medium_type);
        if (medium_type == MIRAGE_MEDIUM_CD) {
            mirage_parser_add_redbook_pregap(MIRAGE_PARSER(self), self->priv->disc);
        }
    }

    /* Cleanup */
    g_free(file_info);
    g_strfreev(filenames);

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
        Q_("ISO Image Parser"),
        2,
        Q_("ISO images (*.iso, *.bin, *.img)"), "application/x-cd-image",
        Q_("WAV audio files (*.wav)"), "audio/x-wav"
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
