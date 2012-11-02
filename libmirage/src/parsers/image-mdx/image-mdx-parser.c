/*
 *  libMirage: MDX image parser: Parser object
 *  Copyright (C) 2006-2012 Henrik Stokseth
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

#include "image-mdx.h"

#define __debug__ "MDX-Parser"


static const guint8 mdx_signature[17] = { 'M', 'E', 'D', 'I', 'A', ' ', 'D', 'E', 'S', 'C', 'R', 'I', 'P', 'T', 'O', 'R', 0x02 };


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_PARSER_MDX_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_MDX, MirageParserMdxPrivate))

struct _MirageParserMdxPrivate
{
    MirageDisc *disc;

    const gchar *mdx_filename;
    GInputStream *stream;
};


static gboolean mirage_parser_mdx_determine_track_mode (MirageParserMdx *self, GInputStream *stream, guint64 offset, guint64 length, gint *track_mode,  gint *sector_size, gint *subchannel_type, gint *subchannel_size, GError **error)
{
    /* FIXME: add subchannel support */
    *subchannel_type = 0;
    *subchannel_size = 0;

    /* 2048-byte standard ISO9660/UDF image check */
    if (length % 2048 == 0) {
        guint8 buf[8];

        g_seekable_seek(G_SEEKABLE(stream), offset + 16*2048, G_SEEK_SET, NULL, NULL);

        if (g_input_stream_read(stream, buf, sizeof(buf), NULL, NULL) != sizeof(buf)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read 8-byte pattern!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read 8-byte pattern!");
            return FALSE;
        }

        if (!memcmp(buf, mirage_pattern_cd001, sizeof(mirage_pattern_cd001))
            || !memcmp(buf, mirage_pattern_bea01, sizeof(mirage_pattern_bea01))) {
            *track_mode = MIRAGE_MODE_MODE1;
            *sector_size = 2048;

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: standard 2048-byte ISO9660/UDF track, Mode 1 assumed\n", __debug__);

            return TRUE;
        }
    }

    /* 2352-byte image check */
    if (length % 2352 == 0) {
        guint8 buf[16];

        g_seekable_seek(G_SEEKABLE(stream), offset + 16*2352, G_SEEK_SET, NULL, NULL);

        if (g_input_stream_read(stream, buf, sizeof(buf), NULL, NULL) != sizeof(buf)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read sync pattern!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read sync pattern!");
            return FALSE;
        }

        *track_mode = mirage_helper_determine_sector_type(buf);
        *sector_size = 2352;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 2352-byte track, mode: %d\n", __debug__, *track_mode);

        return TRUE;
    }

    /* 2332/2336-byte image check */
    if (length % 2332 == 0) {
        *track_mode = MIRAGE_MODE_MODE2_MIXED;
        *sector_size = 2332;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 2332-byte track, Mode 2 Mixed assumed (unreliable!)\n", __debug__);

        return TRUE;
    }
    if (length % 2336 == 0) {
        *track_mode = MIRAGE_MODE_MODE2_MIXED;
        *sector_size = 2336;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 2336-byte track, Mode 2 Mixed assumed (unreliable!)\n", __debug__);

        return TRUE;
    }

    /* Nope, can't load the file */
    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Parser cannot handle given image!");
    return FALSE;
}


static gchar *__helper_find_binary_file (const gchar *declared_filename, const gchar *mds_filename)
{
    gchar *bin_filename;
    gchar *bin_fullpath;

    /* Is the filename in form of '*.mdf'? */
    GRegex *ext_regex = g_regex_new("\\*\\.(?<ext>\\w+)", 0, 0, NULL);
    GMatchInfo *match_info = NULL;
    if (g_regex_match(ext_regex, declared_filename, 0, &match_info)) {
        /* Replace the extension in mds_filename */
        gchar *ext = g_match_info_fetch_named(match_info, "ext");
        GRegex *mds_regex = g_regex_new("(?<ext>\\w+)$", 0, 0, NULL);
        bin_filename = g_regex_replace(mds_regex, mds_filename, -1, 0, ext, 0, NULL);

        g_regex_unref(mds_regex);
        g_free(ext);
        g_match_info_free(match_info);
    } else {
        bin_filename = g_strdup(declared_filename);
    }
    g_regex_unref(ext_regex);

    bin_fullpath = mirage_helper_find_data_file(bin_filename, mds_filename);
    g_free(bin_filename);

    return bin_fullpath;
}


static MirageTrack *mirage_parser_mdx_get_track (MirageParserMdx *self, GError **error)
{
    gboolean succeeded = TRUE;

    GInputStream *data_stream;
    guint64 offset;
    guint64 length;

    gint track_mode, sector_size, subchannel_type, subchannel_size, num_sectors;
    MirageTrack *track;
    MirageFragment *data_fragment;

    /* MDX file or MDS v.2? */
    if (mirage_helper_has_suffix(self->priv->mdx_filename, ".mdx")) {
        MDX_Header header;

        /* Reuse the already-opened stream */
        data_stream = self->priv->stream;
        g_object_ref(data_stream);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: MDX file; reading header...\n", __debug__);

        /* Seek to beginning */
        g_seekable_seek(G_SEEKABLE(data_stream), 0, G_SEEK_SET, NULL, NULL);

        /* Read header */
        if (g_input_stream_read(data_stream, &header, sizeof(header), NULL, NULL) != sizeof(header)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read MDX header!\n", __debug__);
            g_object_unref(data_stream);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read header!");
            return NULL;
        }

        header.__dummy__ = GINT32_FROM_LE(header.__dummy__);
        header.footer_offset = GUINT64_FROM_LE(header.footer_offset);
        header.blockinfo_size = GUINT64_FROM_LE(header.blockinfo_size);

        /* Print header */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: MDX header:\n", __debug__);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - dummy: %d (0x%X)\n", __debug__, header.__dummy__);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - footer offset: %lld (0x%llX)\n", __debug__, header.footer_offset);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - blocksize size: %lld (0x%llX)\n", __debug__, header.blockinfo_size);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");

        /* Offset: end of header */
        offset = g_seekable_tell(G_SEEKABLE(data_stream));

        /* Length: between header and footer */
        length = header.footer_offset - offset;
    } else if (mirage_helper_has_suffix(self->priv->mdx_filename, ".mds")) {
        /* Find corresponding MDF file */
        gchar *data_file = __helper_find_binary_file("*.mdf", self->priv->mdx_filename);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: MDS file; corresponding MDF: %s\n", __debug__, data_file);

        data_stream = mirage_contextual_create_file_stream(MIRAGE_CONTEXTUAL(self), data_file, error);
        g_free(data_file);
        if (!data_stream) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: could not open MDF file!\n", __debug__);
            return NULL;
        }

        /* No offset */
        offset = 0;

        /* Get file length */
        g_seekable_seek(G_SEEKABLE(data_stream), 0, G_SEEK_END, NULL, NULL);
        length = g_seekable_tell(G_SEEKABLE(data_stream));
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: invalid filename suffix; only 'mdx' and 'mds' are supported!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Parser cannot handle given image!");
        return NULL;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: data offset: %lld (0x%llX)\n", __debug__, offset, offset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: data length: %lld (0x%llX)\n", __debug__, length, length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");


    /* Try to guess the track mode */
    succeeded = mirage_parser_mdx_determine_track_mode(self, data_stream, offset, length, &track_mode, &sector_size, &subchannel_type, &subchannel_size, error);
    if (!succeeded) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to guess track type!\n", __debug__);
        g_object_unref(data_stream);
        return NULL;
    }

    num_sectors = length / (sector_size + subchannel_size);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track mode: %d\n", __debug__, track_mode);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: sector size: %d\n", __debug__, sector_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: subchannel type: %d\n", __debug__, subchannel_type);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: subchannel size: %d\n", __debug__, subchannel_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of sectors: %d\n", __debug__, num_sectors);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");


    /* Create and prepare track */
    track = g_object_new(MIRAGE_TYPE_TRACK, NULL);
    mirage_track_set_mode(track, track_mode);

    /* Create data fragment */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating data fragment\n", __debug__);
    data_fragment = mirage_contextual_create_fragment(MIRAGE_CONTEXTUAL(self), MIRAGE_TYPE_DATA_FRAGMENT, data_stream, error);
    if (!data_fragment) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create BINARY fragment!\n", __debug__);
        return NULL;
    }

    /* Set stream */
    mirage_data_fragment_main_data_set_stream(MIRAGE_DATA_FRAGMENT(data_fragment), data_stream);
    g_object_unref(data_stream);

    mirage_data_fragment_main_data_set_format(MIRAGE_DATA_FRAGMENT(data_fragment), MIRAGE_MAIN_DATA);
    mirage_data_fragment_main_data_set_offset(MIRAGE_DATA_FRAGMENT(data_fragment), offset);
    mirage_data_fragment_main_data_set_size(MIRAGE_DATA_FRAGMENT(data_fragment), sector_size);

    mirage_fragment_set_length(data_fragment, num_sectors);

    /* Add fragment to track */
    mirage_track_add_fragment(track, -1, data_fragment);

    return track;
}

static gboolean mirage_parser_mdx_load_disc (MirageParserMdx *self, GError **error)
{
    MirageSession *session;
    MirageTrack *track;

    /* Make sure users know what they're up against */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: *** WARNING ***\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: This parser provides only rudimentary functionality!\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: MDX image descriptors are obfuscated and possibly encrypted, making information about the image (layout, number of tracks, DPM, etc.) inaccessible.\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: This parser assumes a single-track image and provides only basic data reading functionality! Don't expect anything fancy.\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "\n");


    /* Get the track */
    track = mirage_parser_mdx_get_track(self, error);
    if (!track) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create track!\n", __debug__);
        return FALSE;
    }

    /* Session: one session */
    session = g_object_new(MIRAGE_TYPE_SESSION, NULL);
    mirage_disc_add_session_by_index(self->priv->disc, 0, session);

    /* MDX image parser assumes single-track image, so we're dealing with regular CD-ROM session */
    mirage_session_set_session_type(session, MIRAGE_SESSION_CD_ROM);

    /* Add track */
    track = g_object_new(MIRAGE_TYPE_TRACK, NULL);
    mirage_session_add_track_by_index(session, -1, track);

    g_object_unref(session);
    g_object_unref(track);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finishing the layout\n", __debug__);

    /* Now guess medium type and if it's a CD-ROM, add Red Book pregap */
    gint medium_type = mirage_parser_guess_medium_type(MIRAGE_PARSER(self), self->priv->disc);
    mirage_disc_set_medium_type(self->priv->disc, medium_type);
    if (medium_type == MIRAGE_MEDIUM_CD) {
        mirage_parser_add_redbook_pregap(MIRAGE_PARSER(self), self->priv->disc);
    }

    return TRUE;
}


/**********************************************************************\
 *                MirageParser methods implementation                *
\**********************************************************************/
static MirageDisc *mirage_parser_mdx_load_image (MirageParser *_self, GInputStream **streams, GError **error)
{
    MirageParserMdx *self = MIRAGE_PARSER_MDX(_self);

    gboolean succeeded = TRUE;
    gchar signature[17];

    /* Check if we can load the image */
    self->priv->stream = streams[0];
    g_object_ref(self->priv->stream);

    /* Read signature and version */
    g_seekable_seek(G_SEEKABLE(self->priv->stream), 0, G_SEEK_SET, NULL, NULL);
    if (g_input_stream_read(self->priv->stream, signature, sizeof(signature), NULL, NULL) != sizeof(signature)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read signature and version!");
        return FALSE;
    }

    /* This parsers handles v.2.X images (new DT format) */
    if (memcmp(signature, mdx_signature, sizeof(mdx_signature))) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Parser cannot handle given image!");
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing the image...\n", __debug__);

    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_set_parent(MIRAGE_OBJECT(self->priv->disc), self);

    /* Set filenames */
    self->priv->mdx_filename = mirage_contextual_get_file_stream_filename(MIRAGE_CONTEXTUAL(self), self->priv->stream);
    mirage_disc_set_filename(self->priv->disc, self->priv->mdx_filename);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: MDX filename: %s\n", __debug__, self->priv->mdx_filename);

    /* Load disc */
    succeeded = mirage_parser_mdx_load_disc(self, error);


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
G_DEFINE_DYNAMIC_TYPE(MirageParserMdx, mirage_parser_mdx, MIRAGE_TYPE_PARSER);

void mirage_parser_mdx_type_register (GTypeModule *type_module)
{
    return mirage_parser_mdx_register_type(type_module);
}


static void mirage_parser_mdx_init (MirageParserMdx *self)
{
    self->priv = MIRAGE_PARSER_MDX_GET_PRIVATE(self);

    mirage_parser_generate_info(MIRAGE_PARSER(self),
        "PARSER-MDX",
        "MDX Image Parser",
        1,
        "DaemonTools images (*.mdx, *.mds)", "application/x-mdx"
    );

    self->priv->stream = NULL;
}

static void mirage_parser_mdx_dispose (GObject *gobject)
{
    MirageParserMdx *self = MIRAGE_PARSER_MDX(gobject);

    if (self->priv->stream) {
        g_object_unref(self->priv->stream);
        self->priv->stream = NULL;
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_parser_mdx_parent_class)->dispose(gobject);
}

static void mirage_parser_mdx_class_init (MirageParserMdxClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageParserClass *parser_class = MIRAGE_PARSER_CLASS(klass);

    gobject_class->dispose = mirage_parser_mdx_dispose;

    parser_class->load_image = mirage_parser_mdx_load_image;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageParserMdxPrivate));
}

static void mirage_parser_mdx_class_finalize (MirageParserMdxClass *klass G_GNUC_UNUSED)
{
}
