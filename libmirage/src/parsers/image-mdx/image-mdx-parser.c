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


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_PARSER_MDX_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_MDX, MIRAGE_Parser_MDXPrivate))

struct _MIRAGE_Parser_MDXPrivate
{
    GObject *disc;
};


static gboolean mirage_parser_mdx_determine_track_mode (MIRAGE_Parser_MDX *self, GObject *stream, guint64 offset, guint64 length, gint *track_mode,  gint *sector_size, gint *subchannel_type, gint *subchannel_size, GError **error)
{
    static const guint8 cd001_pattern[] = {0x01, 0x43, 0x44, 0x30, 0x30, 0x31, 0x01, 0x00};
    static const guint8 bea01_pattern[] = {0x00, 0x42, 0x45, 0x41, 0x30, 0x31, 0x01, 0x00};

    /* FIXME: add subchannel support */
    *subchannel_type = 0;
    *subchannel_size = 0;

    /* 2048-byte standard ISO9660/UDF image check */
    if (length % 2048 == 0) {
        guint8 buf[8];

        g_seekable_seek(G_SEEKABLE(stream), offset + 16*2048, G_SEEK_SET, NULL, NULL);

        if (g_input_stream_read(G_INPUT_STREAM(stream), buf, sizeof(buf), NULL, NULL) != sizeof(buf)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read 8-byte pattern!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read 8-byte pattern!");
            return FALSE;
        }

        if (!memcmp(buf, cd001_pattern, sizeof(cd001_pattern))
            || !memcmp(buf, bea01_pattern, sizeof(bea01_pattern))) {
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

        if (g_input_stream_read(G_INPUT_STREAM(stream), buf, sizeof(buf), NULL, NULL) != sizeof(buf)) {
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


static gboolean mirage_parser_mdx_get_track (MIRAGE_Parser_MDX *self, const gchar *filename, GObject **ret_track, GError **error)
{
    gboolean succeeded = TRUE;

    GObject *stream;
    gchar *data_file;
    guint64 offset;
    guint64 length;

    gint track_mode, sector_size, subchannel_type, subchannel_size, num_sectors;
    GObject *track;
    GObject *data_fragment;

    /* MDX file or MDS v.2? */
    if (mirage_helper_has_suffix(filename, ".mdx")) {
        MDX_Header header;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: MDX file; reading header...\n", __debug__);

        /* Open MDX file */
        stream = libmirage_create_file_stream(filename, G_OBJECT(self), error);
        if (!stream) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: could not open MDX file '%s'!\n", __debug__, filename);
            return FALSE;
        }

        /* Read header */
        if (g_input_stream_read(G_INPUT_STREAM(stream), &header, sizeof(header), NULL, NULL) != sizeof(header)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read MDX header!\n", __debug__, filename);
            g_object_unref(stream);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read header!");
            return FALSE;
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

        /* Data filename */
        data_file = g_strdup(filename);

        /* Offset: end of header */
        offset = g_seekable_tell(G_SEEKABLE(stream));

        /* Length: between header and footer */
        length = header.footer_offset - offset;
    } else if (mirage_helper_has_suffix(filename, ".mds")) {
        /* Find corresponding MDF file */
        data_file = __helper_find_binary_file("*.mdf", filename);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: MDS file; corresponding MDF: %s\n", __debug__, data_file);

        stream = libmirage_create_file_stream(data_file, G_OBJECT(self), error);
        if (!stream) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: could not open MDF file!\n", __debug__);
            g_free(data_file);
            return FALSE;
        }

        /* No offset */
        offset = 0;

        /* Get file length */
        g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_END, NULL, NULL);
        length = g_seekable_tell(G_SEEKABLE(stream));
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: invalid filename suffix; only 'mdx' and 'mds' are supported!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Parser cannot handle given image!");
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: data offset: %lld (0x%llX)\n", __debug__, offset, offset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: data length: %lld (0x%llX)\n", __debug__, length, length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");


    /* Try to guess the track mode */
    succeeded = mirage_parser_mdx_determine_track_mode(self, stream, offset, length, &track_mode, &sector_size, &subchannel_type, &subchannel_size, error);
    g_object_unref(stream);

    if (!succeeded) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to guess track type!\n", __debug__);
        g_free(data_file);
        return FALSE;
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
    mirage_track_set_mode(MIRAGE_TRACK(track), track_mode);

    /* Create data fragment */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating data fragment\n", __debug__);
    data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FRAG_IFACE_BINARY, data_file, error);
    if (!data_fragment) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create BINARY fragment!\n", __debug__);
        g_free(data_file);
        return FALSE;
    }

    /* Set file */
    if (!mirage_frag_iface_binary_track_file_set_file(MIRAGE_FRAG_IFACE_BINARY(data_fragment), data_file, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set track data file!\n", __debug__);
        g_free(data_file);
        g_object_unref(data_fragment);
        return FALSE;
    }
    g_free(data_file);

    mirage_frag_iface_binary_track_file_set_format(MIRAGE_FRAG_IFACE_BINARY(data_fragment), FR_BIN_TFILE_DATA);
	mirage_frag_iface_binary_track_file_set_offset(MIRAGE_FRAG_IFACE_BINARY(data_fragment), offset);
    mirage_frag_iface_binary_track_file_set_sectsize(MIRAGE_FRAG_IFACE_BINARY(data_fragment), sector_size);

    mirage_fragment_set_length(MIRAGE_FRAGMENT(data_fragment), num_sectors);

    /* Add fragment to track */
    mirage_track_add_fragment(MIRAGE_TRACK(track), -1, data_fragment);

    *ret_track = track;
    return TRUE;
}

static gboolean mirage_parser_mdx_load_disc (MIRAGE_Parser_MDX *self, gchar *filename, GError **error)
{
    GObject *session = NULL;
    GObject *track = NULL;

    /* Make sure users know what they're up against */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: *** WARNING ***\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: This parser provides only rudimentary functionality!\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: MDX image descriptors are obfuscated and possibly encrypted, making information about the image (layout, number of tracks, DPM, etc.) inaccessible.\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: This parser assumes a single-track image and provides only basic data reading functionality! Don't expect anything fancy.\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "\n");


    /* Get the track */
    if (!mirage_parser_mdx_get_track(self, filename, &track, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create track!\n", __debug__);
        return FALSE;
    }


    /* Session: one session */
    mirage_disc_add_session_by_index(MIRAGE_DISC(self->priv->disc), 0, &session);

    /* MDX image parser assumes single-track image, so we're dealing with regular CD-ROM session */
    mirage_session_set_session_type(MIRAGE_SESSION(session), MIRAGE_SESSION_CD_ROM);

    /* Add track */
    mirage_session_add_track_by_index(MIRAGE_SESSION(session), -1, &track);

    g_object_unref(session);
    g_object_unref(track);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finishing the layout\n", __debug__);

    /* Now guess medium type and if it's a CD-ROM, add Red Book pregap */
    gint medium_type = mirage_parser_guess_medium_type(MIRAGE_PARSER(self), self->priv->disc);
    mirage_disc_set_medium_type(MIRAGE_DISC(self->priv->disc), medium_type);
    if (medium_type == MIRAGE_MEDIUM_CD) {
        mirage_parser_add_redbook_pregap(MIRAGE_PARSER(self), self->priv->disc);
    }

    return TRUE;
}


/**********************************************************************\
 *                MIRAGE_Parser methods implementation                *
\**********************************************************************/
static GObject *mirage_parser_mdx_load_image (MIRAGE_Parser *_self, gchar **filenames, GError **error)
{
    MIRAGE_Parser_MDX *self = MIRAGE_PARSER_MDX(_self);

    gboolean succeeded = TRUE;
    GObject *stream;
    gchar signature[17];

    /* Check if we can load the image */
    stream = libmirage_create_file_stream(filenames[0], G_OBJECT(self), error);
    if (!stream) {
        return FALSE;
    }

    /* Read signature and version */
    if (g_input_stream_read(G_INPUT_STREAM(stream), signature, sizeof(signature), NULL, NULL) != sizeof(signature)) {
        g_object_unref(stream);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read signature and version!");
        return FALSE;
    }

    /* This parsers handles v.2.X images (new DT format) */
    if (memcmp(signature, "MEDIA DESCRIPTOR\x02", 17)) {
        g_object_unref(stream);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Parser cannot handle given image!");
        return FALSE;
    }

    g_object_unref(stream);


    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_attach_child(MIRAGE_OBJECT(self), self->priv->disc);

    /* Set filenames */
    mirage_disc_set_filename(MIRAGE_DISC(self->priv->disc), filenames[0]);


    /* Load disc */
    succeeded = mirage_parser_mdx_load_disc(self, filenames[0], error);


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
G_DEFINE_DYNAMIC_TYPE(MIRAGE_Parser_MDX, mirage_parser_mdx, MIRAGE_TYPE_PARSER);

void mirage_parser_mdx_type_register (GTypeModule *type_module)
{
    return mirage_parser_mdx_register_type(type_module);
}


static void mirage_parser_mdx_init (MIRAGE_Parser_MDX *self)
{
    self->priv = MIRAGE_PARSER_MDX_GET_PRIVATE(self);

    mirage_parser_generate_parser_info(MIRAGE_PARSER(self),
        "PARSER-MDX",
        "MDX Image Parser",
        "MDX images",
        "application/x-mdx"
    );
}

static void mirage_parser_mdx_class_init (MIRAGE_Parser_MDXClass *klass)
{
    MIRAGE_ParserClass *parser_class = MIRAGE_PARSER_CLASS(klass);

    parser_class->load_image = mirage_parser_mdx_load_image;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Parser_MDXPrivate));
}

static void mirage_parser_mdx_class_finalize (MIRAGE_Parser_MDXClass *klass G_GNUC_UNUSED)
{
}
