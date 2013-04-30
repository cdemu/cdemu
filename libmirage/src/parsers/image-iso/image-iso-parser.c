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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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

    gint main_data_size;
    gint track_mode;

    gint subchannel_data_size;
    gint subchannel_format;
};


static gboolean mirage_parser_iso_determine_sector_size (MirageParserIso *self, GInputStream *stream, GError **error)
{
    const gint valid_sector_sizes[] = { 2048, 2332, 2336, 2352 };
    const gint data_offset[] = { 0, 0, 0, 16 };
    const gint valid_subchannel_sizes[] = { 0, 16, 96 };

    gsize file_length;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: verifying file size...\n", __debug__);

    /* Get stream length */
    if (!g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_END, NULL, error)) {
        return FALSE;
    }
    file_length = g_seekable_tell(G_SEEKABLE(stream));

    /* Make sure the file is large enough to contain ISO descriptor */
    if (file_length < 17*2448) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: parser cannot handle given image: file is too small!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Parser cannot handle given image: file is too small!");
        return FALSE;
    }

    /* Check all possible combinations of sector data and subchannel sizes */
    for (gint i = 0; i < G_N_ELEMENTS(valid_subchannel_sizes); i++) {
        for (gint j = 0; j < G_N_ELEMENTS(valid_sector_sizes); j++) {
            gint full_sector_size = valid_sector_sizes[j] + valid_subchannel_sizes[i];
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: cheking %d-byte sector size with %d-byte subchannel...\n", __debug__, valid_sector_sizes[j], valid_subchannel_sizes[i]);

            /* Check if file size is a multiple of full sector size */
            if (file_length % full_sector_size != 0) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: file size is not a multiple of %d!\n", __debug__, full_sector_size);
                continue;
            }

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: file size check passed; looking for CD001/BEA01 pattern at sector 16...\n", __debug__);

            /* Check for CD001 or BEA01 at sector 16 */
            guint8 buf[8];
            gsize offset = 16*full_sector_size + data_offset[j];

            if (!g_seekable_seek(G_SEEKABLE(stream), offset, G_SEEK_SET, NULL, NULL)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to %lXh to read 8-byte pattern!\n", __debug__, offset);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to seek to %lXh to read 8-byte pattern!", offset);
                return FALSE;
            }

            if (g_input_stream_read(stream, buf, 8, NULL, NULL) != 8) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read 8-byte pattern!\n", __debug__);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read 8-byte pattern!");
                return FALSE;
            }

            if (!memcmp(buf, mirage_pattern_cd001, sizeof(mirage_pattern_cd001))
                || !memcmp(buf, mirage_pattern_bea01, sizeof(mirage_pattern_bea01))) {
                self->priv->main_data_size = valid_sector_sizes[j];
                self->priv->subchannel_data_size = valid_subchannel_sizes[i];

                MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: image is an ISO9660/UDF image, with %d-byte sector data and %d-byte subchannel data\n", __debug__, self->priv->main_data_size, self->priv->subchannel_data_size);
                return TRUE;
            } else {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: CD001/BEA01 pattern not found!\n", __debug__);
            }
        }
    }

    /* No supported combination */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: parser cannot handle given image: file size does not correspond to any supported combination of sector and subchannel size!\n", __debug__);
    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Parser cannot handle given image: file size does not correspond to any supported combination of sector and subchannel size!");
    return FALSE;
}

static gboolean mirage_parser_iso_determine_track_type (MirageParserIso *self, GInputStream *stream, GError **error)
{
    /* We try to guess track type from main channel data size */
    switch (self->priv->main_data_size) {
        case 2048: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 2048-byte main sector data; assuming Mode 1 track\n", __debug__);
            self->priv->track_mode = MIRAGE_MODE_MODE1;
            break;
        }
        case 2332:
        case 2336: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 2332/2336-byte main sector data; assuming Mode 2 track\n", __debug__);
            self->priv->track_mode = MIRAGE_MODE_MODE2_MIXED;
            break;
        }
        case 2352: {
            guint8 buf[16];
            gsize offset = 16 * (self->priv->main_data_size + self->priv->subchannel_data_size);

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 2352-byte main sector data; determining track type at address 16 (offset %lXh)...\n", __debug__, offset);

            if (!g_seekable_seek(G_SEEKABLE(stream), offset, G_SEEK_SET, NULL, NULL)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to offset %lXh to read 16-byte pattern!\n", __debug__, offset);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to seek to offset %lXh to read 16-byte pattern!", offset);
                return FALSE;
            }

            if (g_input_stream_read(stream, buf, 16, NULL, NULL) != 16) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read 16-byte pattern!\n", __debug__);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read 16-byte pattern!");
                return FALSE;
            }

            self->priv->track_mode = mirage_helper_determine_sector_type(buf);
            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unhalded main sector data size %d!\n", __debug__, self->priv->main_data_size);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Unhalded main sector data size %d!", self->priv->main_data_size);
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean mirage_parser_iso_determine_subchannel_type (MirageParserIso *self, GInputStream *stream, GError **error)
{
    switch (self->priv->subchannel_data_size) {
        case 0: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: no subchannel data found!\n", __debug__);
            self->priv->subchannel_format = 0;
            break;
        }
        case 16: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 16-byte internal PQ subchannel data found!\n", __debug__);
            self->priv->subchannel_format = MIRAGE_SUBCHANNEL_INT | MIRAGE_SUBCHANNEL_PQ16;
            break;
        }
        case 96: {
            guint8 buf[96];
            gsize offset = 16 * (self->priv->main_data_size + self->priv->subchannel_data_size) + self->priv->main_data_size;
            
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 96-byte internal PW subchannel data found!\n", __debug__);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: determining whether it is linear or interleaved from subchannel data of sector 16 (offset %lXh)...\n", __debug__, offset);

            if (!g_seekable_seek(G_SEEKABLE(stream), offset, G_SEEK_SET, NULL, NULL)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to offset %lXh to read subchannel data!\n", __debug__, offset);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to seek to offset %lXh to read subchannel data!", offset);
                return FALSE;
            }

            if (g_input_stream_read(stream, buf, sizeof(buf), NULL, NULL) != sizeof(buf)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read subchannel data!\n", __debug__);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read subchannel data!");
                return FALSE;
            }

            /* Determine whether subchannel data is linear or interleaved;
               we assume it is linear, and validate CRC over Q-channel 
               data. An alternative would be to validate address stored in 
               subchannel data... */
            guint16 crc = mirage_helper_subchannel_q_calculate_crc(buf+12);
            if ((buf[22] << 8 | buf[23]) == crc) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: subchannel data appears to be linear!\n", __debug__);
                self->priv->subchannel_format = MIRAGE_SUBCHANNEL_INT | MIRAGE_SUBCHANNEL_PW96_LIN;
            } else {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: subchannel data appears to be interleaved!\n", __debug__);
                self->priv->subchannel_format = MIRAGE_SUBCHANNEL_INT | MIRAGE_SUBCHANNEL_PW96_INT;                
            }
            
            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unhalded subchannel data size %d!\n", __debug__, self->priv->subchannel_data_size);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Unhalded subchannel data size %d!", self->priv->subchannel_data_size);
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean mirage_parser_iso_load_track (MirageParserIso *self, GInputStream *stream, GError **error)
{
    MirageSession *session;
    MirageTrack *track;
    MirageFragment *fragment;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: loading track...\n", __debug__);
    
    /* Create data fragment */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating data fragment\n", __debug__);
    fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL);

    mirage_fragment_main_data_set_stream(fragment, stream);
    mirage_fragment_main_data_set_size(fragment, self->priv->main_data_size);
    mirage_fragment_main_data_set_format(fragment, MIRAGE_MAIN_DATA);
    mirage_fragment_subchannel_data_set_size(fragment, self->priv->subchannel_data_size);
    mirage_fragment_subchannel_data_set_format(fragment, self->priv->subchannel_format);
    
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

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: checking if parser can handle given image...\n", __debug__);
    if (!mirage_parser_iso_determine_sector_size(self, stream, error)) {
        g_object_unref(stream);
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: parser can handle given image!\n", __debug__);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing the image...\n", __debug__);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: ISO filename: %s\n", __debug__, iso_filename);

    /* Determine track and subchannel mode */
    if (!mirage_parser_iso_determine_track_type(self, stream, error)) {
        succeeded = FALSE;
        goto end;
    }
    if (!mirage_parser_iso_determine_subchannel_type(self, stream, error)) {
        succeeded = FALSE;
        goto end;
    }

    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_set_parent(MIRAGE_OBJECT(self->priv->disc), self);

    /* Set filenames */
    mirage_disc_set_filename(self->priv->disc, iso_filename);

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
