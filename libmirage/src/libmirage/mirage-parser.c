/*
 *  libMirage: Parser object
 *  Copyright (C) 2008-2012 Rok Mandeljc
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"

#define __debug__ "Parser"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_PARSER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER, MirageParserPrivate))

struct _MirageParserPrivate
{
    GHashTable *parser_params;

    MirageParserInfo *info;

    /* Data stream cache */
    GHashTable *stream_cache;
};


/**********************************************************************\
 *                          Private functions                         *
\**********************************************************************/
static void destroy_info (MirageParserInfo *info)
{
    /* Free info and its content */
    if (info) {
        g_free(info->id);
        g_free(info->name);
        g_free(info->description);
        g_free(info->mime_type);

        g_free(info);
    }
}


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
/**
 * mirage_parser_generate_info:
 * @self: a #MirageParser
 * @id: (in): parser ID
 * @name: (in): parser name
 * @description: (in): image file description
 * @mime_type: (in): image file MIME type
 *
 * <para>
 * Generates parser information from the input fields. It is intended as a function
 * for creating parser information in parser implementations.
 * </para>
 **/
void mirage_parser_generate_info (MirageParser *self, const gchar *id, const gchar *name, const gchar *description, const gchar *mime_type)
{
    /* Free old info */
    destroy_info(self->priv->info);

    /* Create new info */
    self->priv->info = g_new0(MirageParserInfo, 1);

    self->priv->info->id = g_strdup(id);
    self->priv->info->name = g_strdup(name);

    self->priv->info->description = g_strdup(description);
    self->priv->info->mime_type = g_strdup(mime_type);
}


/**
 * mirage_parser_get_info:
 * @self: a #MirageParser
 *
 * <para>
 * Retrieves parser information.
 * </para>
 *
 * Returns: (transfer none): a pointer to parser information structure.  The
 * structure belongs to object and should not be modified.
 **/
const MirageParserInfo *mirage_parser_get_info (MirageParser *self)
{
    return self->priv->info;
}


/**
 * mirage_parser_load_image:
 * @self: a #MirageParser
 * @streams: (in) (array zero-terminated=1): %NULL-terminated array of data streams
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Loads the image stored in @streams.
 * </para>
 *
 * Returns: (transfer full): a #MirageDisc object representing image on success, %NULL on failure
 **/
GObject *mirage_parser_load_image (MirageParser *self, GObject **streams, GError **error)
{
    GObject *disc;

    /* Load the image */
    disc = MIRAGE_PARSER_GET_CLASS(self)->load_image(self, streams, error);
    if (!disc) {
        return NULL;
    }

    /* If 'dvd-report-css' flag is passed to the parser, pass it on to
       the disc object */
    const GVariant *dvd_report_css = mirage_parser_get_param(self, "dvd-report-css", G_VARIANT_TYPE_BOOLEAN);
    if (dvd_report_css) {
        /* Convert GVariant to GValue... */
        GValue dvd_report_css2;
        g_value_init(&dvd_report_css2, G_TYPE_BOOLEAN);
        g_value_set_boolean(&dvd_report_css2, g_variant_get_boolean((GVariant *) dvd_report_css));

        g_object_set_property(disc, "dvd-report-css", &dvd_report_css2);
    }

    return disc;
}


/**
 * mirage_parser_guess_medium_type:
 * @self: a #MirageParser
 * @disc: (in): disc object
 *
 * <para>
 * Attempts to guess medium type by looking at the length of the disc layout.
 * Currently, it supports identification of CD-ROM media, which are assumed to
 * have layout length of 90 minutes or less.
 * </para>
 *
 * <para>
 * Note that this function does not set the medium type to disc object; you still
 * need to do it via mirage_disc_set_medium_type(). It is meant to be used in
 * simple parsers whose image files don't provide medium type information.
 * </para>
 *
 * Returns: a value from #MirageMediumTypes, according to the guessed medium type.
 **/
gint mirage_parser_guess_medium_type (MirageParser *self, GObject *disc)
{
    gint length = mirage_disc_layout_get_length(MIRAGE_DISC(disc));

    /* FIXME: add other media types? */
    if (length <= 90*60*75) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: disc layout size implies CD-ROM image\n", __debug__);
        return MIRAGE_MEDIUM_CD;
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: disc layout size implies DVD-ROM image\n", __debug__);
        return MIRAGE_MEDIUM_DVD;
    };
}

/**
 * mirage_parser_add_redbook_pregap:
 * @self: a #MirageParser
 * @disc: (in): disc object
 *
 * <para>
 * A helper function, intended to be used in simpler parsers that don't get proper
 * pregap information from the image file.
 * </para>
 *
 * <para>
 * First, it sets disc layout start to -150. Then, it adds 150-sector pregap to
 * first track of each session found on the layout; for this, a NULL fragment is
 * used. If track already has a pregap, then the pregaps are stacked.
 * </para>
 *
 * <para>
 * Note that the function works only on discs which have medium type set to
 * CD-ROM. On other discs, it does nothing.
 * </para>
 **/
void mirage_parser_add_redbook_pregap (MirageParser *self, GObject *disc)
{
    gint num_sessions;

    /* Red Book pregap is found only on CD-ROMs */
    if (mirage_disc_get_medium_type(MIRAGE_DISC(disc)) != MIRAGE_MEDIUM_CD) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Red Book pregap exists only on CD-ROMs!\n", __debug__);
        return;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding Red Book pregaps to the disc...\n", __debug__);

    /* CD-ROMs start at -150 as per Red Book... */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting disc layout start at -150\n", __debug__);
    mirage_disc_layout_set_start_sector(MIRAGE_DISC(disc), -150);

    num_sessions = mirage_disc_get_number_of_sessions(MIRAGE_DISC(disc));
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: %d session(s)\n", __debug__, num_sessions);

    /* Put 150 sector pregap into every first track of each session */
    for (gint i = 0; i < num_sessions; i++) {
        GObject *session;
        GObject *track;
        GObject *fragment;

        gint track_start;

        session = mirage_disc_get_session_by_index(MIRAGE_DISC(disc), i, NULL);
        if (!session) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to get session with index %i!\n", __debug__, i);
            return;
        }

        track = mirage_session_get_track_by_index(MIRAGE_SESSION(session), 0, NULL);
        if (!track) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to first track of session with index %i!\n", __debug__, i);
            g_object_unref(session);
            return;
        }

        /* Add pregap fragment - NULL fragment creation should never fail */
        fragment = mirage_create_fragment(MIRAGE_TYPE_FRAGMENT_IFACE_NULL, NULL, G_OBJECT(self), NULL);
        mirage_fragment_set_length(MIRAGE_FRAGMENT(fragment), 150);
        mirage_track_add_fragment(MIRAGE_TRACK(track), 0, fragment);
        g_object_unref(fragment);

        /* Track starts at 150... well, unless it already has a pregap, in
           which case they should stack */
        track_start = mirage_track_get_track_start(MIRAGE_TRACK(track));
        track_start += 150;
        mirage_track_set_track_start(MIRAGE_TRACK(track), track_start);

        g_object_unref(track);
        g_object_unref(session);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: added 150 pregap to first track in session %i\n", __debug__, i);
    }
}


/**
 * mirage_parser_set_params:
 * @self: a #MirageParser
 * @params: (in) (element-type gchar* GValue): a #GHashTable containing parameters
 *
 * <para>
 * An internal function that sets the parsing parameters to parser
 * (such as password, encoding, etc.). It is meant to be used by mirage_create_disc()
 * to pass the parsing parameters to parser before performing the parsing.
 * </para>
 *
 * <para>
 * @params is a #GHashTable that must have strings for its keys and values of
 * #GValue type.
 * </para>
 *
 * <para>
 * Note that only pointer to @params is stored; therefore, the hash table must
 * still be valid when mirage_parser_load_image() is called. Another thing to
 * note is that whether parameter is used or not is up to the parser implementation.
 * In case of unsupported parameter, the parser implementation should simply ignore it.
 * </para>
 **/
void mirage_parser_set_params (MirageParser *self, GHashTable *params)
{
    self->priv->parser_params = params; /* Just store pointer */
}

/**
 * mirage_parser_get_param_string:
 * @self: a #MirageParser
 * @name: (in): parameter name (key)
 *
 * <para>
 * An internal function that retrieves a string parameter named @name. It is meant
 * to be used by parser implementation to retrieve the parameter value during the
 * parsing.
 * </para>
 *
 * Returns: (transfer none): string value, or %NULL. The string belongs to whoever owns
 * the parameters hash table that was passed to the parser, and as such should
 * not be modified.
 **/
const gchar *mirage_parser_get_param_string (MirageParser *self, const gchar *name)
{
    /* Get value */
    const GVariant *value = mirage_parser_get_param(self, name, G_VARIANT_TYPE_STRING);

    if (!value) {
        return NULL;
    }

    return g_variant_get_string((GVariant *) value, NULL);
}


/**
 * mirage_parser_get_param:
 * @self: a #MirageParser
 * @name: (in): parameter name (key)
 * @type: (in): expected value type (set to %G_VARIANT_TYPE_ANY to disable type checking)
 *
 * <para>
 * An internal function that retrieves a boolean parameter named @name. It is meant
 * to be used by parser implementation to retrieve the parameter value during the
 * parsing.
 * </para>
 *
 * Returns: (transfer none): parameter variant. Note that variant belongs to whoever owns
 * the parameters hash table that was passed to the parser, and as such should
 * not be modified.
 **/
const GVariant *mirage_parser_get_param (MirageParser *self, const gchar *name, const GVariantType *type)
{
    GVariant *value;

    /* Make sure parameters are set */
    if (!self->priv->parser_params) {
        return NULL;
    }

    /* Lookup value */
    value = g_hash_table_lookup(self->priv->parser_params, name);
    if (!value) {
        return NULL;
    }

    /* Verify type */
    if (!g_variant_is_of_type(value, type)) {
        return NULL;
    }

    return value;
}


/**
 * mirage_parser_get_cached_data_stream:
 * @self: a #MirageParser
 * @filename: (in): filename
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * An internal function that implements data stream cache for a parser
 * object. It is intended for parsers that deal with multi-track images
 * where data stream reuse might prove beneficial. This function checks
 * if a stream with requested @filename already exists in the cache and
 * if it does, returns it. If it does not, it opens a new stream and
 * adds it to the cache.
 * </para>
 *
 * Returns: (transfer full): data stream object on success, %NULL on failure.
 * The reference to stream should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_parser_get_cached_data_stream (MirageParser *self, const gchar *filename, GError **error)
{
    GObject *stream = g_hash_table_lookup(self->priv->stream_cache, filename);

    if (!stream) {
        /* Stream not in cache, open a stream on filename... */
        stream = mirage_create_file_stream(filename, G_OBJECT(self), error);
        if (!stream) {
            return stream;
        }

        /* ... and add it to cache */
        g_hash_table_insert(self->priv->stream_cache, g_strdup(filename), stream);
    }

    g_object_ref(stream);
    return stream;
}



/**
 * mirage_parser_create_text_stream:
 * @self: a #MirageParser
 * @stream: (in) (transfer full): a #GInputStream
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Constructs a filter chain for reading text files on top of provided
 * @stream. The first filter is
 *
 * </para>
 *
 * Returns: (transfer full): a #GDataInputStream object on success,
 *  encoding, or %NULL on failure.
 **/
GDataInputStream *mirage_parser_create_text_stream (MirageParser *self, GObject *stream, GError **error)
{
    GDataInputStream *data_stream;
    const gchar *encoding;

    /* Add reference to provided input stream */
    g_object_ref(stream);

    /* If provided, use the specified encoding; otherwise, try to detect it */
    encoding = mirage_parser_get_param_string(MIRAGE_PARSER(self), "encoding");;
    if (encoding) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: using specified encoding: %s\n", __debug__, encoding);
    } else {
        /* Detect encoding */
        guint8 bom[4] = { 0 };

        g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);
        g_input_stream_read(G_INPUT_STREAM(stream), bom, sizeof(bom), NULL, NULL);

        encoding = mirage_helper_encoding_from_bom(bom);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: detect encoding: %s\n", __debug__, encoding);
    }

    /* Reset stream position, just in case */
    g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);

    /* If needed, set up charset converter */
    if (encoding) {
        GCharsetConverter *converter;
        GInputStream *converter_stream;

        /* Create converter */
        converter = g_charset_converter_new("UTF-8", encoding, error);
        if (!converter) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create converter from '%s'!\n", __debug__, encoding);
            g_object_unref(stream);
            return FALSE;
        }

        /* Create converter stream */
        converter_stream = g_converter_input_stream_new(G_INPUT_STREAM(stream), G_CONVERTER(converter));
        g_filter_input_stream_set_close_base_stream(G_FILTER_INPUT_STREAM(converter_stream), FALSE);

        g_object_unref(converter);

        /* Switch the stream */
        g_object_unref(stream);
        stream = G_OBJECT(converter_stream);
    }

    /* Create data stream */
    data_stream = g_data_input_stream_new(G_INPUT_STREAM(stream));
    if (!data_stream) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create data stream!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Failed to create data stream!");
        g_object_unref(stream);
        return FALSE;
    }
    g_filter_input_stream_set_close_base_stream(G_FILTER_INPUT_STREAM(data_stream), FALSE);
    g_data_input_stream_set_newline_type(data_stream, G_DATA_STREAM_NEWLINE_TYPE_ANY);

    g_object_unref(stream);

    return data_stream;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE(MirageParser, mirage_parser, MIRAGE_TYPE_OBJECT);


static void mirage_parser_init (MirageParser *self)
{
    self->priv = MIRAGE_PARSER_GET_PRIVATE(self);

    self->priv->parser_params = NULL;
    self->priv->info = NULL;

    /* Stream cache hash table */
    self->priv->stream_cache = g_hash_table_new_full(g_str_hash,
                                                     g_str_equal,
                                                     g_free,
                                                     g_object_unref);
}

static void mirage_parser_dispose (GObject *gobject)
{
    MirageParser *self = MIRAGE_PARSER(gobject);

    if (self->priv->stream_cache) {
        g_hash_table_unref(self->priv->stream_cache);
        self->priv->stream_cache = NULL;
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_parser_parent_class)->finalize(gobject);
}

static void mirage_parser_finalize (GObject *gobject)
{
    MirageParser *self = MIRAGE_PARSER(gobject);

    destroy_info(self->priv->info);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_parser_parent_class)->finalize(gobject);
}

static void mirage_parser_class_init (MirageParserClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = mirage_parser_dispose;
    gobject_class->finalize = mirage_parser_finalize;

    klass->load_image = NULL;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageParserPrivate));
}
