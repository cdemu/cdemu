/*
 *  libMirage: parser
 *  Copyright (C) 2008-2014 Rok Mandeljc
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

/**
 * SECTION: mirage-parser
 * @title: MirageParser
 * @short_description: Base object for image parser implementations.
 * @see_also: #MirageDisc, #MirageContext
 * @include: mirage-parser.h
 *
 * #MirageParser object is a base object for image parser implementations.
 * In addition to providing function for image loading and obtaining
 * parser information, it also provides some helper functions that can
 * be used in parser implementations.
 *
 * #MirageParser provides a single virtual function - mirage_parser_load_image().
 * This function must be implemented by image parsers, which derive from
 * #MirageParser object. The function must first check if given file(s)
 * are supported by the given parser, and then the actual loading is
 * performed. The result is a #MirageDisc object, which represents the
 * disc stored in the image file(s).
 */

#include "mirage/config.h"
#include "mirage/mirage.h"

#include <glib/gi18n-lib.h>

#define __debug__ "Parser"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
struct _MirageParserPrivate
{
    MirageParserInfo info;
};


/**********************************************************************\
 *                           Parser info API                          *
\**********************************************************************/
static void mirage_parser_info_generate (MirageParserInfo *info, const gchar *id, const gchar *name, gint num_types, va_list args)
{
    /* Free old fields */
    mirage_parser_info_free(info);

    /* Copy ID and name */
    info->id = g_strdup(id);
    info->name = g_strdup(name);

    /* Copy description and MIME type strings */
    info->description = g_new0(gchar *, num_types+1);
    info->mime_type = g_new0(gchar *, num_types+1);

    for (gint i = 0; i < num_types; i++) {
        info->description[i] = g_strdup(va_arg(args, const gchar *));
        info->mime_type[i] = g_strdup(va_arg(args, const gchar *));
    }
}

/**
 * mirage_parser_info_copy:
 * @info: (in): a #MirageParserInfo to copy data from
 * @dest: (in): a #MirageParserInfo to copy data to
 *
 * Copies parser information from @info to @dest.
 */
void mirage_parser_info_copy (const MirageParserInfo *info, MirageParserInfo *dest)
{
    dest->id = g_strdup(info->id);
    dest->name = g_strdup(info->name);
    dest->description = g_strdupv(info->description);
    dest->mime_type = g_strdupv(info->mime_type);
}

/**
 * mirage_parser_info_free:
 * @info: (in): a #MirageParserInfo to free
 *
 * Frees the allocated fields in @info (but not the structure itself!).
 */
void mirage_parser_info_free (MirageParserInfo *info)
{
    g_free(info->id);
    g_free(info->name);

    g_strfreev(info->description);
    g_strfreev(info->mime_type);
}


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
/**
 * mirage_parser_generate_info:
 * @self: a #MirageParser
 * @id: (in): parser ID
 * @name: (in): parser name
 * @num_types: (in): number of MIME types
 * @...: (in): description and MIME type string pairs, one for each defined type
 *
 * Generates parser information from the input fields. It is intended as a function
 * for creating parser information in parser implementations.
 */
void mirage_parser_generate_info (MirageParser *self, const gchar *id, const gchar *name, gint num_types, ...)
{
    va_list args;
    va_start(args, num_types);

    mirage_parser_info_generate(&self->priv->info, id, name, num_types, args);

    va_end(args);
}

/**
 * mirage_parser_get_info:
 * @self: a #MirageParser
 *
 * Retrieves parser information.
 *
 * Returns: (transfer none): a pointer to parser information structure.  The
 * structure belongs to object and should not be modified.
 */
const MirageParserInfo *mirage_parser_get_info (MirageParser *self)
{
    return &self->priv->info;
}


/**
 * mirage_parser_load_image:
 * @self: a #MirageParser
 * @streams: (in) (array zero-terminated=1): %NULL-terminated array of data streams
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Loads the image stored in @streams.
 *
 * Returns: (transfer full): a #MirageDisc object representing image on success, %NULL on failure
 */
MirageDisc *mirage_parser_load_image (MirageParser *self, MirageStream **streams, GError **error)
{
    return MIRAGE_PARSER_GET_CLASS(self)->load_image(self, streams, error);
}


/**
 * mirage_parser_guess_medium_type:
 * @self: a #MirageParser
 * @disc: (in): disc object
 *
 * Attempts to guess medium type by looking at the length of the disc layout.
 * Currently, it supports identification of CD-ROM media, which are assumed to
 * have layout length of 90 minutes or less.
 *
 * Note that this function does not set the medium type to disc object; you still
 * need to do it via mirage_disc_set_medium_type(). It is meant to be used in
 * simple parsers whose image files don't provide medium type information.
 *
 * Returns: a value from #MirageMediumType, according to the guessed medium type.
 */
gint mirage_parser_guess_medium_type (MirageParser *self, MirageDisc *disc)
{
    gint length = mirage_disc_layout_get_length(disc);

    /* FIXME: add other media types? */
    if (length <= 90*60*75) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: disc layout size implies CD-ROM image\n", __debug__);
        return MIRAGE_MEDIUM_CD;
    } else if (length <= 2295104) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: disc layout size implies single-layer DVD-ROM image\n", __debug__);
        return MIRAGE_MEDIUM_DVD;
    } else if (length <= 4173824) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: disc layout size implies dual-layer DVD-ROM image\n", __debug__);
        return MIRAGE_MEDIUM_DVD;
    } else if (length <= 12219392) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: disc layout size implies single-layer BD-ROM image\n", __debug__);
        return MIRAGE_MEDIUM_BD;
    } else if (length <= 24438784) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: disc layout size implies dual-layer BD-ROM image\n", __debug__);
        return MIRAGE_MEDIUM_BD;
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: disc layout size (%d) exceeds all known media types - assuming BD-ROM!\n", __debug__, length);
        return MIRAGE_MEDIUM_BD;
    }
}

/**
 * mirage_parser_add_redbook_pregap:
 * @self: a #MirageParser
 * @disc: (in): disc object
 *
 * A helper function, intended to be used in simpler parsers that don't get proper
 * pregap information from the image file.
 *
 * First, it sets disc layout start to -150. Then, it adds 150-sector pregap to
 * first track of each session found on the layout; for this, a NULL fragment is
 * used. If track already has a pregap, then the pregaps are stacked.
 *
 * Note that the function works only on discs which have medium type set to
 * CD-ROM. On other discs, it does nothing.
 */
void mirage_parser_add_redbook_pregap (MirageParser *self, MirageDisc *disc)
{
    gint num_sessions;

    /* Red Book pregap is found only on CD-ROMs */
    if (mirage_disc_get_medium_type(disc) != MIRAGE_MEDIUM_CD) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Red Book pregap exists only on CD-ROMs!\n", __debug__);
        return;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding Red Book pregaps to the disc...\n", __debug__);

    /* CD-ROMs start at -150 as per Red Book... */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting disc layout start at -150\n", __debug__);
    mirage_disc_layout_set_start_sector(disc, -150);

    num_sessions = mirage_disc_get_number_of_sessions(disc);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: %d session(s)\n", __debug__, num_sessions);

    /* Put 150 sector pregap into every first track of each session */
    for (gint i = 0; i < num_sessions; i++) {
        MirageSession *session;
        MirageTrack *track;
        MirageFragment *fragment;

        gint track_start;

        session = mirage_disc_get_session_by_index(disc, i, NULL);
        if (!session) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to get session with index %i!\n", __debug__, i);
            return;
        }

        track = mirage_session_get_track_by_index(session, 0, NULL);
        if (!track) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to first track of session with index %i!\n", __debug__, i);
            g_object_unref(session);
            return;
        }

        /* Add pregap fragment */
        fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL);
        mirage_fragment_set_length(fragment, 150);
        mirage_track_add_fragment(track, 0, fragment);
        g_object_unref(fragment);

        /* Track starts at 150... well, unless it already has a pregap, in
           which case they should stack */
        track_start = mirage_track_get_track_start(track);
        track_start += 150;
        mirage_track_set_track_start(track, track_start);

        g_object_unref(track);
        g_object_unref(session);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: added 150 pregap to first track in session %i\n", __debug__, i);
    }
}


/**
 * mirage_parser_create_text_stream:
 * @self: a #MirageParser
 * @stream: (in) (transfer full): a #MirageStream
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Constructs a filter chain for reading text files on top of provided
 * @stream. First, if encoding is provided via parser parameters, or if
 * a multi-byte encoding is detected, a #GConverterInputStream with a
 * #GCharsetConverter is applied. Then on top of it, a #GDataInputStream
 * is created, which can be used to read text file line-by-line.
 *
 * Returns: (transfer full): a #GDataInputStream object on success,
 * or %NULL on failure.
 */
GDataInputStream *mirage_parser_create_text_stream (MirageParser *self, MirageStream *stream, GError **error)
{
    GDataInputStream *data_stream;
    GVariant *encoding_value;
    const gchar *encoding;

    /* If provided, use the specified encoding; otherwise, try to detect it */
    encoding_value = mirage_contextual_get_option(MIRAGE_CONTEXTUAL(self), "encoding");;
    if (encoding_value) {
        encoding = g_variant_get_string(encoding_value, NULL);
        g_variant_unref(encoding_value);
        //MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: using specified encoding: %s\n", __debug__, encoding);
    } else {
        /* Detect encoding */
        guint8 bom[4] = { 0 };

        mirage_stream_seek(stream, 0, G_SEEK_SET, NULL);
        mirage_stream_read(stream, bom, sizeof(bom), NULL);

        encoding = mirage_helper_encoding_from_bom(bom);

        //MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: detect encoding: %s\n", __debug__, encoding);
    }

    /* Reset stream position, just in case */
    mirage_stream_seek(stream, 0, G_SEEK_SET, NULL);

    /* Create MirageStream -> GInputStream bridge */
    GInputStream *input_stream = mirage_stream_get_g_input_stream(stream);

    /* If needed, set up charset converter */
    if (encoding) {
        GCharsetConverter *converter;
        GInputStream *converter_stream;

        /* Create converter */
        converter = g_charset_converter_new("UTF-8", encoding, error);
        if (!converter) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create converter from '%s'!\n", __debug__, encoding);
            g_object_unref(input_stream);
            return FALSE;
        }

        /* Create converter stream */
        converter_stream = g_converter_input_stream_new(input_stream, G_CONVERTER(converter));
        g_filter_input_stream_set_close_base_stream(G_FILTER_INPUT_STREAM(converter_stream), TRUE);

        g_object_unref(converter);

        /* Switch the stream */
        g_object_unref(input_stream);
        input_stream = converter_stream;
    }

    /* Create data stream */
    data_stream = g_data_input_stream_new(input_stream);
    if (!data_stream) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create data stream!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Failed to create data stream!"));
        g_object_unref(stream);
        return FALSE;
    }
    g_filter_input_stream_set_close_base_stream(G_FILTER_INPUT_STREAM(data_stream), TRUE);
    g_data_input_stream_set_newline_type(data_stream, G_DATA_STREAM_NEWLINE_TYPE_ANY);

    g_object_unref(input_stream);

    return data_stream;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE(MirageParser, mirage_parser, MIRAGE_TYPE_OBJECT)


static void mirage_parser_init (MirageParser *self)
{
    self->priv = mirage_parser_get_instance_private(self);

    /* Make sure all fields are empty */
    memset(&self->priv->info, 0, sizeof(self->priv->info));
}

static void mirage_parser_finalize (GObject *gobject)
{
    MirageParser *self = MIRAGE_PARSER(gobject);

    /* Free info structure */
    mirage_parser_info_free(&self->priv->info);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_parser_parent_class)->finalize(gobject);
}

static void mirage_parser_class_init (MirageParserClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = mirage_parser_finalize;

    klass->load_image = NULL;
}
