/*
 *  libMirage: CHD image: parser
 *  Copyright (C) 2026 Rok Mandeljc
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

#include "image-chd.h"
#include "fragment.h"

#define __debug__ "CHD-Parser"


/**********************************************************************\
 *                  Object and its private structure                  *
\**********************************************************************/
struct _MirageParserChdPrivate
{
    MirageDisc *disc;

    /* Pointer boxed using GLib's RcBox for reference counting */
    shared_chd_file_t *chd_file_ptr;

    /* Temporary buffer for reading metadata */
    gchar metadata_buffer[256];

    /* Metadata tag that is available for disc layout reconstruction */
    guint32 metadata_tag;
};


G_DEFINE_DYNAMIC_TYPE_EXTENDED(
    MirageParserChd,
    mirage_parser_chd,
    MIRAGE_TYPE_PARSER,
    0,
    G_ADD_PRIVATE_DYNAMIC(MirageParserChd)
)

void mirage_parser_chd_type_register (GTypeModule *type_module)
{
    mirage_parser_chd_register_type(type_module);
}


/**********************************************************************\
 *                         Debug functions                            *
\**********************************************************************/
const char *_chd_error_str (chd_error error_code)
{
    #define _CASE(x) \
        case x: { \
            return #x; \
        }

    switch (error_code) {
        _CASE(CHDERR_NONE)
        _CASE(CHDERR_NO_INTERFACE)
        _CASE(CHDERR_OUT_OF_MEMORY)
        _CASE(CHDERR_INVALID_FILE)
        _CASE(CHDERR_INVALID_PARAMETER)
        _CASE(CHDERR_INVALID_DATA)
        _CASE(CHDERR_FILE_NOT_FOUND)
        _CASE(CHDERR_REQUIRES_PARENT)
        _CASE(CHDERR_FILE_NOT_WRITEABLE)
        _CASE(CHDERR_READ_ERROR)
        _CASE(CHDERR_WRITE_ERROR)
        _CASE(CHDERR_CODEC_ERROR)
        _CASE(CHDERR_INVALID_PARENT)
        _CASE(CHDERR_HUNK_OUT_OF_RANGE)
        _CASE(CHDERR_DECOMPRESSION_ERROR)
        _CASE(CHDERR_COMPRESSION_ERROR)
        _CASE(CHDERR_CANT_CREATE_FILE)
        _CASE(CHDERR_CANT_VERIFY)
        _CASE(CHDERR_NOT_SUPPORTED)
        _CASE(CHDERR_METADATA_NOT_FOUND)
        _CASE(CHDERR_INVALID_METADATA_SIZE)
        _CASE(CHDERR_UNSUPPORTED_VERSION)
        _CASE(CHDERR_VERIFY_INCOMPLETE)
        _CASE(CHDERR_INVALID_METADATA)
        _CASE(CHDERR_INVALID_STATE)
        _CASE(CHDERR_OPERATION_PENDING)
        _CASE(CHDERR_NO_ASYNC_OPERATION)
        _CASE(CHDERR_UNSUPPORTED_FORMAT)
        default: {
            return "<unknown>";
        }
    }

    #undef _CASE
}

const char *_chd_tag_str (uint32_t tag)
{
    #define _CASE(x) \
        case x: { \
            return #x; \
        }

    switch (tag) {
        /* Compression type tags */
        _CASE(CHD_CODEC_NONE)
        _CASE(CHD_CODEC_ZLIB)
        _CASE(CHD_CODEC_LZMA)
        _CASE(CHD_CODEC_HUFFMAN)
        _CASE(CHD_CODEC_FLAC)
        _CASE(CHD_CODEC_ZSTD)
        _CASE(CHD_CODEC_CD_ZLIB)
        _CASE(CHD_CODEC_CD_LZMA)
        _CASE(CHD_CODEC_CD_FLAC)
        _CASE(CHD_CODEC_CD_ZSTD)
        /* Metadata tags */
        _CASE(HARD_DISK_METADATA_TAG)
        _CASE(HARD_DISK_IDENT_METADATA_TAG)
        _CASE(HARD_DISK_KEY_METADATA_TAG)
        _CASE(PCMCIA_CIS_METADATA_TAG)
        _CASE(CDROM_OLD_METADATA_TAG)
        _CASE(CDROM_TRACK_METADATA_TAG)
        _CASE(CDROM_TRACK_METADATA2_TAG)
        _CASE(GDROM_OLD_METADATA_TAG)
        _CASE(GDROM_TRACK_METADATA_TAG)
        _CASE(AV_METADATA_TAG)
        _CASE(AV_LD_METADATA_TAG)
        _CASE(DVD_METADATA_TAG)
        default: {
            return "<unknown>";
        }
    }

    #undef _CASE
}


/**********************************************************************\
 *               I/O adapter for chd_core_file_callbacks               *
\**********************************************************************/
static uint64_t _chd_fsize (void *fp)
{
    MirageStream *stream = MIRAGE_STREAM(fp);

    goffset cur_position;
    goffset end_position;
    GError *local_error = NULL;

    cur_position = mirage_stream_tell(stream);

    if (!mirage_stream_seek(stream, 0, G_SEEK_END, &local_error)) {
        MIRAGE_DEBUG(stream, MIRAGE_DEBUG_WARNING, "%s: failed to seek to end of stream: %s\n", __debug__, local_error->message);
        g_error_free(local_error);
        return 0;
    }

    end_position = mirage_stream_tell(stream);

    if (!mirage_stream_seek(stream, cur_position, G_SEEK_SET, &local_error)) {
        MIRAGE_DEBUG(stream, MIRAGE_DEBUG_WARNING, "%s: failed to seek to original position: %s\n", __debug__, local_error->message);
        g_error_free(local_error);
        local_error = NULL;
    }

    return end_position;
}

static size_t _chd_fread (void *ptr, size_t size, size_t n, void *fp)
{
    MirageStream *stream = MIRAGE_STREAM(fp);

    GError *local_error = NULL;
    gssize ret;


    ret = mirage_stream_read(stream, ptr, n * size, &local_error);
    if (ret < 0) {
        MIRAGE_DEBUG(stream, MIRAGE_DEBUG_WARNING, "%s: failed to read data: %s\n", __debug__, local_error->message);
        g_error_free(local_error);
        local_error = NULL;
        ret = 0; /* Reset to 0, since return type is unsigned */
    }

    return ret;
}

static int _chd_fclose (void *fp)
{
    MirageStream *stream = MIRAGE_STREAM(fp);
    g_object_unref(stream); /* Release the stream reference */
    return 0;
}

static int _chd_fseek (void *fp, int64_t where, int whence)
{
    MirageStream *stream = MIRAGE_STREAM(fp);
    GError *local_error = NULL;
    gint seek_type;

    /* Map stdio fseek() whence codes to GLib's GSeekType (because
     * _CUR and _SET have switched values in these APIs). */
    switch (whence) {
        case SEEK_CUR:
            seek_type = G_SEEK_CUR;
            break;
        case SEEK_SET:
            seek_type = G_SEEK_SET;
            break;
        case SEEK_END:
            seek_type = G_SEEK_END;
            break;
        default: {
            return -1;
        }
    }

    if (!mirage_stream_seek(stream, where, seek_type, &local_error)) {
        MIRAGE_DEBUG(stream, MIRAGE_DEBUG_WARNING, "%s: failed to seek: %s\n", __debug__, local_error->message);
        g_error_free(local_error);
        return -1;
    }

    return 0;
}


static const struct chd_core_file_callbacks _chd_file_adapter = {
    .fsize = _chd_fsize,
    .fread = _chd_fread,
    .fclose = _chd_fclose,
    .fseek = _chd_fseek
};


/* Cleanup helper for shared_chd_file_t, to be used with g_rc_box_release_full() */
void shared_chd_file_cleanup (shared_chd_file_t *p)
{
    if (p->chd_file) {
        chd_close(p->chd_file);
        p->chd_file = NULL;
    }
}


/**********************************************************************\
 *                         Parsing functions                          *
\**********************************************************************/
struct track_info_t
{
    gint track_number;
    gint length;
    gint sector_type;

    gint main_data_size;
    gint main_data_format;

    gint subchannel_data_size;
    gint subchannel_data_format;

    /* Pre-gap */
    gint pregap_length;
    gboolean pregap_has_data;

    /* Post-grap */
    gint postgap_length;
};

static gboolean mirage_parser_chd_parse_mode_string (const gchar *mode_str, gint *sector_type, gint *data_size, gint *data_format)
{
    static const struct {
        char *name;
        gint sector_type;
        gint data_size;
    } entries[] = {
        {"MODE1", MIRAGE_SECTOR_MODE1, 2048},
        {"MODE1_RAW", MIRAGE_SECTOR_MODE1, 2352},
        {"MODE2", MIRAGE_SECTOR_MODE2, 2336},
        {"MODE2_FORM1", MIRAGE_SECTOR_MODE2_FORM1, 2048},
        {"MODE2_FORM2", MIRAGE_SECTOR_MODE2_FORM2, 2324},
        {"MODE2_FORM_MIX", MIRAGE_SECTOR_MODE2_MIXED, 2336},
        {"MODE2_RAW", MIRAGE_SECTOR_MODE2_MIXED, 2352},
        {"AUDIO", MIRAGE_SECTOR_AUDIO, 2352}
    };

    for (guint i = 0; i < G_N_ELEMENTS(entries); i++) {
        if (strcmp(mode_str, entries[i].name) == 0) {
            if (sector_type) {
                *sector_type = entries[i].sector_type;
            }
            if (data_size) {
                *data_size = entries[i].data_size;
            }
            if (data_format) {
                /* CHD uses swapped audio format! */
                *data_format = (
                    (entries[i].sector_type == MIRAGE_SECTOR_AUDIO) ?
                    MIRAGE_MAIN_DATA_FORMAT_AUDIO_SWAP :
                    MIRAGE_MAIN_DATA_FORMAT_DATA
                );
            }
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean mirage_parser_chd_parse_subchannel_string (const gchar *subchannel_str, gint *data_size, gint *data_format)
{
    static const struct {
        char *name;
        gint data_size;
        gint data_format;
    } entries[] = {
        {"NONE", 0, 0},
        {"RW", 96, MIRAGE_SUBCHANNEL_DATA_FORMAT_RW96 | MIRAGE_SUBCHANNEL_DATA_FORMAT_INTERNAL},
        {"RW_RAW", 96, MIRAGE_SUBCHANNEL_DATA_FORMAT_PW96_INTERLEAVED | MIRAGE_SUBCHANNEL_DATA_FORMAT_INTERNAL}
    };

    for (guint i = 0; i < G_N_ELEMENTS(entries); i++) {
        if (strcmp(subchannel_str, entries[i].name) == 0) {
            if (data_size) {
                *data_size = entries[i].data_size;
            }
            if (data_format) {
                *data_format = entries[i].data_format;
            }
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean mirage_parser_chd_parse_cd_track_metadata (const guint32 metadata_tag, const gchar *metadata_buffer, struct track_info_t *track_info, GError **error)
{
    gint track_number;
    gchar track_mode_str[256] = "";
    gchar subchannel_str[256] = "";
    gint num_sectors;

    gint pregap_length = 0;
    gchar pregap_mode_str[256] = "";
    gchar pregap_subchannel_str[256] = "";
    gint postgap_length = 0;

    gint ret;
    gint expected_ret;

    /* Parse using supplied pattern */
    if (metadata_tag == CDROM_TRACK_METADATA_TAG) {
        ret = sscanf(
            metadata_buffer,
            CDROM_TRACK_METADATA_FORMAT,
            &track_number,
            track_mode_str,
            subchannel_str,
            &num_sectors
        );
        expected_ret = 4;
    } else /*if (CDROM_TRACK_METADATA2_TAG)*/ {
        ret = sscanf(
            metadata_buffer,
            CDROM_TRACK_METADATA2_FORMAT,
            &track_number,
            track_mode_str,
            subchannel_str,
            &num_sectors,
            &pregap_length,
            pregap_mode_str,
            pregap_subchannel_str,
            &postgap_length
        );
        expected_ret = 8;
    }
    if (ret != expected_ret) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Failed to extract %d values from metadata string!", expected_ret);
        return FALSE;
    }

    track_info->track_number = track_number;
    track_info->length = num_sectors;

    if (!mirage_parser_chd_parse_mode_string(track_mode_str, &track_info->sector_type, &track_info->main_data_size, &track_info->main_data_format)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Unsupported track mode string: %s", track_mode_str);
        return FALSE;
    }
    if (!mirage_parser_chd_parse_subchannel_string(subchannel_str, &track_info->subchannel_data_size, &track_info->subchannel_data_format)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Unsupported subchannel string: %s", subchannel_str);
        return FALSE;
    }

    track_info->pregap_length = pregap_length;
    track_info->pregap_has_data = FALSE;

    if (pregap_length > 0) {
        /* Pregap data can be included in the image data or can be "external",
         * in which case it is assumed to be all zeroes. The former is
         * equivalent to "INDEX 00 M:S:F" + "INDEX 01 M:S:F" in a CUE/BIN
         * image and to "START M:S:F" in a TOC/BIN image. The latter is
         * equivalent to "PREGAP M:S:F" + "INDEX 01 M:S:F" in a CUE/BIN image
         * and to "ZERO M:S:F" + "START M:S:F" in TOC/BIN images.
         *
         * When pregap data is included in the image, the pregap mode string
         * seems to have an added 'V' character prefix; when it is external/zeroes,
         * the mode seems to be set to MODE1 (which seems also to be the case when
         * no pregap is present). */
        if (pregap_mode_str[0] == 'V') {
            track_info->pregap_has_data = TRUE;

            /* Until we come across an image that proves otherwise, assume
             * that mode and subchannel type are consistent between the pregap
             * and track. In theory, this does not have to be the case in a CHD
             * image, but in practice, it is for all other image types (from
             * which the CHD is most likely created). */
            if (strcmp(track_mode_str, pregap_mode_str + 1) != 0) {
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Pregap mode does not match track mode: %s vs %s", pregap_mode_str, track_mode_str);
                return FALSE;
            }
            if (strcmp(subchannel_str, pregap_subchannel_str) != 0) {
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Pregap subchannel does not match track subchannel: %s vs %s", pregap_subchannel_str, subchannel_str);
                return FALSE;
            }
        }
    }

    track_info->postgap_length = postgap_length;

    return TRUE;
}

static void mirage_parser_chd_update_session_type (MirageSession *session)
{
    gboolean has_audio = FALSE;
    gboolean has_mode1 = FALSE;
    gboolean has_mode2 = FALSE;

    gint num_tracks = mirage_session_get_number_of_tracks(session);

    for (gint i = 0; i < num_tracks; i++) {
        MirageTrack *track = mirage_session_get_track_by_index(session, i, NULL);
        gint sector_type = mirage_track_get_sector_type(track);

        switch (sector_type) {
            case MIRAGE_SECTOR_AUDIO: {
                has_audio = TRUE;
                break;
            }
            case MIRAGE_SECTOR_MODE1: {
                has_mode1 = TRUE;
                break;
            }
            case MIRAGE_SECTOR_MODE2:
            case MIRAGE_SECTOR_MODE2_FORM1:
            case MIRAGE_SECTOR_MODE2_FORM2:
            case MIRAGE_SECTOR_MODE2_MIXED: {
                has_mode2 = TRUE;
                break;
            }
        }

        g_object_unref(track);
    }

    /* This is how cdrdao's cue2toc determine's session type. */
    if (has_audio && !has_mode1 && !has_mode2) {
        mirage_session_set_session_type(session, MIRAGE_SESSION_CDDA);
    } else if ((has_audio && has_mode1 && !has_mode2) || (!has_audio && has_mode1 && !has_mode2)) {
        mirage_session_set_session_type(session, MIRAGE_SESSION_CDROM);
    } else if ((has_audio && !has_mode1 && has_mode2) || (!has_audio && !has_mode1 && has_mode2)) {
        mirage_session_set_session_type(session, MIRAGE_SESSION_CDROM_XA);
    }
}

static MirageTrack *mirage_parser_chd_create_track (
    MirageParserChd *self,
    const chd_header *header,
    const struct track_info_t *track_info,
    const guint64 start_sector,
    GError **error
)
{
    MirageTrack *track;
    MirageFragment *fragment = NULL;
    MirageFragment *fragment_pregap = NULL;
    MirageFragment *fragment_postgap = NULL;

    gboolean succeeded;
    GError *local_error = NULL;

    /* Create main fragment */
    fragment = g_object_new(MIRAGE_TYPE_FRAGMENT_CHD, NULL);

    /* Immediately propagate context for debug purposes; so we may
     * see debug messages emitted during fragment setup. */
    mirage_contextual_inherit_context(MIRAGE_CONTEXTUAL(fragment), MIRAGE_CONTEXTUAL(self));

    succeeded = mirage_fragment_chd_setup(
        MIRAGE_FRAGMENT_CHD(fragment),
        self->priv->chd_file_ptr,
        start_sector, /* start offset (in sectors / "units") */
        track_info->length, /* length (in sectors / "units") */
        header->hunkbytes, /* hunk size */
        header->unitbytes, /* sector size */
        track_info->main_data_size, /* main data size */
        track_info->main_data_format, /* main data format */
        track_info->subchannel_data_size, /* subchannel size */
        track_info->subchannel_data_format, /* subchannel format */
        &local_error
    );
    if (!succeeded) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set up CHD data fragment: %s!\n", __debug__, local_error->message);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Failed to set up CHD data fragment: %s!"), local_error->message);
        g_error_free(local_error);
        g_object_unref(fragment);
        return FALSE;
    }

    /* Create a NULL fragment for pregap, if applicable. */
    if (track_info->pregap_length > 0 && !track_info->pregap_has_data) {
        fragment_pregap = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL);
        mirage_fragment_set_length(fragment_pregap, track_info->pregap_length);
    }

    /* Create a NULL pregap for postgap, if applicable. */
    if (track_info->postgap_length > 0) {
        fragment_postgap = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL);
        mirage_fragment_set_length(fragment_postgap, track_info->postgap_length);
    }

    /* Create track */
    track = g_object_new(MIRAGE_TYPE_TRACK, NULL);

    mirage_track_set_sector_type(track, track_info->sector_type);

    if (fragment_pregap) {
        mirage_track_add_fragment(track, -1, fragment_pregap);
        g_object_unref(fragment_pregap);
    }

    mirage_track_add_fragment(track, -1, fragment);
    g_object_unref(fragment);

    if (fragment_postgap) {
        mirage_track_add_fragment(track, -1, fragment_postgap);
        g_object_unref(fragment_postgap);
    }

    mirage_track_set_track_start(track, track_info->pregap_length);

    return track;
}


static gboolean mirage_parser_chd_load_cd_image (MirageParserChd *self, GError **error)
{
    const chd_header *header = chd_get_header(self->priv->chd_file_ptr->chd_file);

    MirageSession *session;

    guint64 start_sector = 0; /* Track offset within CHD data (in sectors) */

    /* Sanity check */
    if (header->unitbytes != 2448) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unexpected unitbytes value in header: %u (only 2448 is supported for CD images)\n", __debug__, header->unitbytes);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Unsupported CHD image type!"));
        return FALSE;
    }

    /* Set medium type */
    mirage_disc_set_medium_type(self->priv->disc, MIRAGE_MEDIUM_CD);

    /* Create and add session */
    session = g_object_new(MIRAGE_TYPE_SESSION, NULL);
    mirage_disc_add_session_by_index(self->priv->disc, 0, session);
    mirage_session_set_session_type(session, MIRAGE_SESSION_CDROM); /* Will be determined later on */

    /* Process CD-ROM metadata and create tracks */
    for (guint i = 0;; i++) {
        uint32_t resultlen;
        uint32_t resulttag;
        uint8_t resultflags;
        chd_error status;

        struct track_info_t track_info;
        gboolean succeeded;
        GError *local_error = NULL;

        MirageTrack *track;

        gint pad_length = 0;

        /* Read metadata */
        status = chd_get_metadata(
            self->priv->chd_file_ptr->chd_file,
            self->priv->metadata_tag,
            i,
            self->priv->metadata_buffer,
            sizeof(self->priv->metadata_buffer),
            &resultlen,
            &resulttag,
            &resultflags
        );

        if (status == CHDERR_METADATA_NOT_FOUND) {
            break;
        } else if (status != CHDERR_NONE) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read CD-ROM metadata entry #%u: %s (%d)\n", __debug__, i, _chd_error_str(status), status);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Failed to read metadata!"));
            g_object_unref(session);
            return FALSE;
        }

        /* Parse metadata */
        succeeded = mirage_parser_chd_parse_cd_track_metadata(
            resulttag,
            self->priv->metadata_buffer,
            &track_info,
            &local_error
        );
        if (!succeeded) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse CD-ROM metadata entry #%u: %s\n", __debug__, i, local_error->message);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Failed to parse metadata!"));
            g_error_free(local_error);
            g_object_unref(session);
            return FALSE;
        }

        /* Create track */
        track = mirage_parser_chd_create_track(
            self,
            header,
            &track_info,
            start_sector,
            &local_error
        );
        if (!track) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create track for CD-ROM metadata entry #%u: %s\n", __debug__, i, local_error->message);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Failed to create track for CD-ROM metadata entry #%u: %s"), i, local_error->message);
            g_error_free(local_error);
            g_object_unref(session);
            return FALSE;
        }

        mirage_session_add_track_by_index(session, -1, track);
        g_object_unref(track);

        /* Update offset for next track */
        /* NOTE: in CHD image, the CD tracks are padded to multiples of
         * 4 units (sectors); the constant is defined in libchdr's
         * cdrom.h header as CD_TRACK_PADDING */
        start_sector += track_info.length;

        pad_length = track_info.length % 4;
        if (pad_length != 0) {
            pad_length = 4 - pad_length;
        }

        start_sector += pad_length;
    }

    /* Infer and update session type from its tracks */
    mirage_parser_chd_update_session_type(session);

    g_object_unref(session);

    /* Sanity check */
    if (start_sector != header->unitcount) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: sanity check failed: final sector offset (%" G_GINT64_MODIFIER "u) != header->unitcount (%" G_GINT64_MODIFIER "u)\n", __debug__, start_sector, header->unitcount);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Sanity check failed!"));
        return FALSE;
    }

    /* Add Red Book pregap */
     mirage_parser_add_redbook_pregap(MIRAGE_PARSER(self), self->priv->disc);

    return TRUE;
}

static gboolean mirage_parser_chd_load_cd_image_old (MirageParserChd *self, GError **error)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: CD-ROM image with old-style metadata is not supported yet!\n", __debug__);
    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Unsupported CHD image type!"));
    return FALSE;
}

static gboolean mirage_parser_chd_load_dvd_image (MirageParserChd *self, GError **error)
{
    const chd_header *header = chd_get_header(self->priv->chd_file_ptr->chd_file);

    MirageSession *session;
    MirageTrack *track;
    MirageFragmentChd *fragment;

    gboolean succeeded;
    GError *local_error = NULL;

    /* Sanity check */
    if (header->unitbytes != 2048) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unexpected unitbytes value in header: %u (only 2048 is supported for DVD images)\n", __debug__, header->unitbytes);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Unsupported CHD image type!"));
        return FALSE;
    }

    /* Create fragment for track */
    fragment = g_object_new(MIRAGE_TYPE_FRAGMENT_CHD, NULL);

    /* Immediately propagate context for debug purposes; so we may
     * see debug messages emitted during fragment setup. */
    mirage_contextual_inherit_context(MIRAGE_CONTEXTUAL(fragment), MIRAGE_CONTEXTUAL(self));

    succeeded = mirage_fragment_chd_setup(
        fragment,
        self->priv->chd_file_ptr,
        0, /* start offset (in sectors / "units") */
        header->unitcount, /* length (in sectors / "units") */
        header->hunkbytes, /* hunk size */
        header->unitbytes, /* sector size */
        2048, /* main data size */
        MIRAGE_MAIN_DATA_FORMAT_DATA, /* main data format */
        0, /* subchannel size */
        0, /* subchannel format */
        &local_error
    );
    if (!succeeded) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set up CHD data fragment: %s!\n", __debug__, local_error->message);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Failed to set up CHD data fragment: %s!"), local_error->message);
        g_error_free(local_error);
        g_object_unref(fragment);
        return FALSE;
    }

    /* Set medium type */
    mirage_disc_set_medium_type(self->priv->disc, MIRAGE_MEDIUM_DVD);

    /* Create and add session */
    session = g_object_new(MIRAGE_TYPE_SESSION, NULL);
    mirage_disc_add_session_by_index(self->priv->disc, 0, session);
    mirage_session_set_session_type(session, MIRAGE_SESSION_CDROM);

    /* Create and add track */
    track = g_object_new(MIRAGE_TYPE_TRACK, NULL);
    mirage_session_add_track_by_index(session, -1, track);
    mirage_track_set_sector_type(track, MIRAGE_SECTOR_MODE1);
    mirage_track_add_fragment(track, -1, MIRAGE_FRAGMENT(fragment));

    g_object_unref(fragment);
    g_object_unref(track);
    g_object_unref(session);

    return succeeded;
}


/**********************************************************************\
 *                MirageParser methods implementation                *
\**********************************************************************/
static MirageDisc *mirage_parser_chd_load_image (MirageParser *_self, MirageStream **streams, GError **error)
{
    MirageParserChd *self = MIRAGE_PARSER_CHD(_self);
    gboolean succeeded;

    const chd_header *header;
    chd_error status;

    /* Try to open CHD reader, and store the object pointer in pre-allocated
     * reference-counted box. If the open attempt fails, the close() function
     * (i.e., `_chd_fclose()`) is called - therefore, increment the reference
     * count on the stream! */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: checking if parser can handle given image...\n", __debug__);

    status = chd_open_core_file_callbacks(
        &_chd_file_adapter,
        g_object_ref(streams[0]),
        CHD_OPEN_READ,
        NULL,
        &self->priv->chd_file_ptr->chd_file
    );

    if (status != CHDERR_NONE) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: chd_open() failed with error code %d (%s)\n", __debug__, status, _chd_error_str(status));
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Parser cannot handle given image: chd_open() failed with error code %d (%s)!"), status, _chd_error_str(status));
        return NULL;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: parser can handle given image!\n", __debug__);

    /* Store pointer to the stream, so we can pass it on to fragment for
     * filename information; we do not need to increment the reference here,
     * as this has already been done when the chd_file reader was created, and
     * will be released once it is closed. */
    self->priv->chd_file_ptr->stream = streams[0];

    /* Grab header */
    header = chd_get_header(self->priv->chd_file_ptr->chd_file);

    /* Dump header */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CHD header:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  length: %u\n", __debug__, header->length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  version: %u\n", __debug__, header->version);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  flags: %u (0x%X)\n", __debug__, header->flags, header->flags);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  compression:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   %s (0x%X)\n", __debug__, _chd_tag_str(header->compression[0]), header->compression[0]);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   %s (0x%X)\n", __debug__, _chd_tag_str(header->compression[1]), header->compression[1]);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   %s (0x%X)\n", __debug__, _chd_tag_str(header->compression[2]), header->compression[2]);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   %s (0x%X)\n", __debug__, _chd_tag_str(header->compression[3]), header->compression[3]);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  hunkbytes: %u\n", __debug__, header->hunkbytes);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  totalhunks: %u\n", __debug__, header->totalhunks);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  logicalbytes: %" G_GINT64_MODIFIER "u\n", __debug__, header->logicalbytes);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  metaoffset: %" G_GINT64_MODIFIER "u\n", __debug__, header->metaoffset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  mapoffset: %" G_GINT64_MODIFIER "u\n", __debug__, header->mapoffset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  unitbytes: %u\n", __debug__, header->unitbytes);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  unitcount: %" G_GINT64_MODIFIER "u\n", __debug__, header->unitcount);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  hunkcount: %u\n", __debug__, header->hunkcount);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  mapentrybytes: %u\n", __debug__, header->mapentrybytes);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  rawmap: %p\n", __debug__, header->rawmap);

    /* Determine what kind of CD-ROM metadata is available, if any */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CHD metadata entries:\n", __debug__);
    for (guint i = 0;; i++) {
        uint32_t resultlen;
        uint32_t resulttag;
        uint8_t resultflags;

        memset(self->priv->metadata_buffer, 0, sizeof(self->priv->metadata_buffer));

        status = chd_get_metadata(
            self->priv->chd_file_ptr->chd_file,
            CHDMETATAG_WILDCARD,
            i,
            self->priv->metadata_buffer,
            sizeof(self->priv->metadata_buffer),
            &resultlen,
            &resulttag,
            &resultflags
        );

        if (status == CHDERR_METADATA_NOT_FOUND) {
            break;
        } else if (status != CHDERR_NONE) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read metadata entry #%u: %s (%d)\n", __debug__, i, _chd_error_str(status), status);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Failed to read metadata!"));
            return NULL;
        }

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  entry #%u: %s (0x%X), %d bytes, flags: 0x%X\n", __debug__, i, _chd_tag_str(resulttag), resulttag, resultlen, resultflags);
        switch (resulttag) {
            /* These tags are known to be text-based, so we can print them */
            case HARD_DISK_METADATA_TAG:
            case CDROM_TRACK_METADATA_TAG:
            case CDROM_TRACK_METADATA2_TAG:
            case GDROM_TRACK_METADATA_TAG:
            case AV_METADATA_TAG: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   %s\n", __debug__, self->priv->metadata_buffer);
                break;
            }
            /* Some sort of binary format; ignore for now */
            default: {
                break;
            }
        }

        /* It is probably safe to assume that different metadata tags
         * are not mixed within the same image. */
        switch (resulttag) {
            case CDROM_OLD_METADATA_TAG:
            case CDROM_TRACK_METADATA_TAG:
            case CDROM_TRACK_METADATA2_TAG:
            case DVD_METADATA_TAG: {
                /* Ensure the metadata buffer is large enough */
                if (resultlen >= sizeof(self->priv->metadata_buffer)) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: metadata buffer is too small - %u bytes required, %" G_GSIZE_MODIFIER "u bytes available!\n", __debug__, resultlen, sizeof(self->priv->metadata_buffer));
                    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Failed to read metadata!"));
                    return NULL;
                }

                self->priv->metadata_tag = resulttag;
                break;
            }
        }
    }

    if (self->priv->metadata_tag == 0) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: no supported CD-ROM / DVD-ROM metadata entries found!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Unsupported CHD image type!"));
        return NULL;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: using metadata tag: %s (0x%X)\n", __debug__, _chd_tag_str(self->priv->metadata_tag), self->priv->metadata_tag);

    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_set_parent(MIRAGE_OBJECT(self->priv->disc), self);

    mirage_disc_set_filename(self->priv->disc, mirage_stream_get_filename(streams[0]));

    /* Parse the metadata and reconstruct tracks */
    if (self->priv->metadata_tag == DVD_METADATA_TAG) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: loading DVD image...\n", __debug__);
        succeeded = mirage_parser_chd_load_dvd_image(self, error);
    } else if (self->priv->metadata_tag == CDROM_OLD_METADATA_TAG) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: loading CD image (old-style metadata)...\n", __debug__);
        succeeded = mirage_parser_chd_load_cd_image_old(self, error);
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: loading CD image...\n", __debug__);
        succeeded = mirage_parser_chd_load_cd_image(self, error);
    }

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
static void mirage_parser_chd_init (MirageParserChd *self)
{
    self->priv = mirage_parser_chd_get_instance_private(self);

    mirage_parser_generate_info(MIRAGE_PARSER(self),
        "PARSER-CHD",
        Q_("CHD Image Parser"),
        1,
        Q_("MAME CHD images (*.chd)"), "application/x-mame-chd"
    );

    /* Allocate box structure for libchdr file reader object */
    self->priv->chd_file_ptr = g_rc_box_new0(shared_chd_file_t);

    self->priv->metadata_tag = 0;
}

static void mirage_parser_chd_dispose (GObject *gobject)
{
    MirageParserChd *self = MIRAGE_PARSER_CHD(gobject);

    if (self->priv->chd_file_ptr) {
        g_rc_box_release_full(self->priv->chd_file_ptr, (GDestroyNotify)shared_chd_file_cleanup);
        self->priv->chd_file_ptr = NULL;
    }

    /* Chain up to the parent class */
    G_OBJECT_CLASS(mirage_parser_chd_parent_class)->dispose(gobject);
}

static void mirage_parser_chd_class_init (MirageParserChdClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageParserClass *parser_class = MIRAGE_PARSER_CLASS(klass);

    gobject_class->dispose = mirage_parser_chd_dispose;

    parser_class->load_image = mirage_parser_chd_load_image;
}

static void mirage_parser_chd_class_finalize (MirageParserChdClass *klass G_GNUC_UNUSED)
{
}
