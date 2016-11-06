/*
 *  libMirage: main library functions
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
 * SECTION: mirage
 * @title: Main
 * @short_description: Core libMirage functions
 * @see_also: #MirageContext
 * @include: mirage.h
 *
 * These functions represent the core of the libMirage API. Before the
 * library can be used, it must be initialized using mirage_initialize(),
 * which loads the plugins containing image parsers, writers and filter
 * streams. When library is no longer needed, it can be shut down using
 * mirage_shutdown(), which unloads the plugins.
 *
 * The core functions listed in this section enable enumeration of
 * supported parsers, writers and filter streams. Most of the core functionality
 * of libMirage, such as loading images, is encapsulated in #MirageContext
 * object, which can be obtained using GLib's g_object_new().
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"

#include <glib/gi18n-lib.h>


static struct
{
    gboolean initialized;

    /* Parsers */
    guint num_parsers;
    GType *parsers;
    MirageParserInfo *parsers_info;

    /* Writers */
    guint num_writers;
    GType *writers;
    MirageWriterInfo *writers_info;

    /* Filter streams */
    guint num_filter_streams;
    GType *filter_streams;
    MirageFilterStreamInfo *filter_streams_info;
} libmirage;


static const MirageDebugMaskInfo dbg_masks[] = {
    { "MIRAGE_DEBUG_PARSER", MIRAGE_DEBUG_PARSER },
    { "MIRAGE_DEBUG_DISC", MIRAGE_DEBUG_DISC },
    { "MIRAGE_DEBUG_SESSION", MIRAGE_DEBUG_SESSION },
    { "MIRAGE_DEBUG_TRACK", MIRAGE_DEBUG_TRACK },
    { "MIRAGE_DEBUG_SECTOR", MIRAGE_DEBUG_SECTOR },
    { "MIRAGE_DEBUG_FRAGMENT", MIRAGE_DEBUG_FRAGMENT },
    { "MIRAGE_DEBUG_CDTEXT", MIRAGE_DEBUG_CDTEXT },
    { "MIRAGE_DEBUG_STREAM", MIRAGE_DEBUG_STREAM },
    { "MIRAGE_DEBUG_IMAGE_ID", MIRAGE_DEBUG_IMAGE_ID },
    { "MIRAGE_DEBUG_WRITER", MIRAGE_DEBUG_WRITER },
};


/**********************************************************************\
 *                   Parsers and filter streams                       *
\**********************************************************************/
static void initialize_parsers_list ()
{
    libmirage.parsers = g_type_children(MIRAGE_TYPE_PARSER, &libmirage.num_parsers);

    libmirage.parsers_info = g_new0(MirageParserInfo, libmirage.num_parsers);
    for (gint i = 0; i < libmirage.num_parsers; i++) {
        MirageParser *parser = g_object_new(libmirage.parsers[i], NULL);
        mirage_parser_info_copy(mirage_parser_get_info(parser), &libmirage.parsers_info[i]);
        g_object_unref(parser);
    }
}

static void initialize_writers_list ()
{
    libmirage.writers = g_type_children(MIRAGE_TYPE_WRITER, &libmirage.num_writers);

    libmirage.writers_info = g_new0(MirageWriterInfo, libmirage.num_writers);
    for (gint i = 0; i < libmirage.num_writers; i++) {
        MirageWriter *writer = g_object_new(libmirage.writers[i], NULL);
        mirage_writer_info_copy(mirage_writer_get_info(writer), &libmirage.writers_info[i]);
        g_object_unref(writer);
    }
}

static void initialize_filter_streams_list ()
{
    libmirage.filter_streams = g_type_children(MIRAGE_TYPE_FILTER_STREAM, &libmirage.num_filter_streams);

    libmirage.filter_streams_info = g_new0(MirageFilterStreamInfo, libmirage.num_filter_streams);
    for (gint i = 0; i < libmirage.num_filter_streams; i++) {
        MirageFilterStream *filter_stream = g_object_new(libmirage.filter_streams[i], NULL);
        mirage_filter_stream_info_copy(mirage_filter_stream_get_info(filter_stream), &libmirage.filter_streams_info[i]);
        g_object_unref(filter_stream);
    }
}


/**********************************************************************\
 *                         Public API                                 *
\**********************************************************************/
/**
 * mirage_initialize:
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Initializes libMirage library. It should be called before any other of
 * libMirage functions.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_initialize (GError **error)
{
    const gchar *plugin_file;
    GDir *plugins_dir;

    /* If already initialized, don't do anything */
    if (libmirage.initialized) {
        return TRUE;
    }

    /* *** I18n support *** */
    bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");

    /* *** Load plugins *** */
    /* Open plugins dir */
    plugins_dir = g_dir_open(MIRAGE_PLUGIN_DIR, 0, NULL);

    if (!plugins_dir) {
        g_error("Failed to open plugin directory '%s'!\n", MIRAGE_PLUGIN_DIR);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, Q_("Failed to open plugin directory '%s'!"), MIRAGE_PLUGIN_DIR);
        return FALSE;
    }

    /* Check every file in the plugin dir */
    while ((plugin_file = g_dir_read_name(plugins_dir))) {
        if (g_str_has_suffix(plugin_file, ".so")) {
            MiragePlugin *plugin;
            gchar *fullpath;

            /* Build full path */
            fullpath = g_build_filename(MIRAGE_PLUGIN_DIR, plugin_file, NULL);

            plugin = mirage_plugin_new(fullpath);

            if (!g_type_module_use(G_TYPE_MODULE(plugin))) {
                g_warning("Failed to load module: %s!\n", fullpath);
                g_object_unref(plugin);
                g_free(fullpath);
                continue;
            }

            g_type_module_unuse(G_TYPE_MODULE(plugin));
            g_free(fullpath);
        }
    }

    g_dir_close(plugins_dir);

    /* *** Get parsers and filter streams *** */
    initialize_parsers_list();
    initialize_writers_list();
    initialize_filter_streams_list();

    /* Allocate and initialize CRC look-up tables */
    crc16_1021_lut = mirage_helper_init_crc16_lut(0x1021);
    if (!crc16_1021_lut) {
        return FALSE;
    }

    crc32_d8018001_lut = mirage_helper_init_crc32_lut(0xd8018001, 8);
    if (!crc32_d8018001_lut) {
        return FALSE;
    }

    /* Allocate LUT for ECMA-130 sector scrambler */
    ecma_130_scrambler_lut = mirage_helper_init_ecma_130b_scrambler_lut();
    if (!ecma_130_scrambler_lut) {
        return FALSE;
    }

    /* We're officially initialized now */
    libmirage.initialized = TRUE;

    return TRUE;
}


/**
 * mirage_shutdown:
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Shuts down libMirage library. It should be called when libMirage is no longer
 * needed.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_shutdown (GError **error)
{
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, Q_("Library not initialized!"));
        return FALSE;
    }

    /* Free parser info */
    for (gint i = 0; i < libmirage.num_parsers; i++) {
        mirage_parser_info_free(&libmirage.parsers_info[i]);
    }
    g_free(libmirage.parsers_info);
    g_free(libmirage.parsers);

    /* Free writer info */
    for (gint i = 0; i < libmirage.num_writers; i++) {
        mirage_writer_info_free(&libmirage.writers_info[i]);
    }
    g_free(libmirage.writers_info);
    g_free(libmirage.writers);

    /* Free filter stream info */
    for (gint i = 0; i < libmirage.num_filter_streams; i++) {
        mirage_filter_stream_info_free(&libmirage.filter_streams_info[i]);
    }
    g_free(libmirage.filter_streams_info);
    g_free(libmirage.filter_streams);

    /* Free CRC look-up tables */
    g_free(crc16_1021_lut);
    crc16_1021_lut = NULL;

    g_free(crc32_d8018001_lut);
    crc32_d8018001_lut = NULL;

    /* Free ECMA-130 sector scrambler LUT */
    g_free(ecma_130_scrambler_lut);
    ecma_130_scrambler_lut = NULL;

    /* We're not initialized anymore */
    libmirage.initialized = FALSE;

    return TRUE;
}


/**
 * mirage_get_parsers_type:
 * @types: (out) (array length=num_parsers) (transfer none): array of parsers' #GType values
 * @num_parsers: (out): number of supported parsers
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves #GType values for supported parsers.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_get_parsers_type (const GType **types, gint *num_parsers, GError **error)
{
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, Q_("Library not initialized!"));
        return FALSE;
    }

    *types = libmirage.parsers;
    *num_parsers = libmirage.num_parsers;

    return TRUE;
}

/**
 * mirage_get_parsers_info:
 * @info: (out) (array length=num_parsers) (transfer none): array of parsers' information structures
 * @num_parsers: (out): number of supported parsers
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves information structures for supported parsers.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_get_parsers_info (const MirageParserInfo **info, gint *num_parsers, GError **error)
{
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, Q_("Library not initialized!"));
        return FALSE;
    }

    *info = libmirage.parsers_info;
    *num_parsers = libmirage.num_parsers;

    return TRUE;
}

/**
 * mirage_enumerate_parsers:
 * @func: (in) (scope call): callback function
 * @user_data: (in) (closure): data to be passed to callback function
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Iterates over list of supported parsers, calling @func for each parser.
 *
 * If @func returns %FALSE, the function immediately returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_enumerate_parsers (MirageEnumParserInfoCallback func, gpointer user_data, GError **error)
{
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, Q_("Library not initialized!"));
        return FALSE;
    }

    /* Go over all parsers */
    for (gint i = 0; i < libmirage.num_parsers; i++) {
        if (!(*func)(&libmirage.parsers_info[i], user_data)) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, Q_("Iteration has been cancelled!"));
            return FALSE;
        }
    }

    return TRUE;
}


/**
 * mirage_get_writers_type:
 * @types: (out) (array length=num_writers) (transfer none): array of writers' #GType values
 * @num_writers: (out): number of supported writers
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves #GType values for supported writers.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_get_writers_type (const GType **types, gint *num_writers, GError **error)
{
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, Q_("Library not initialized!"));
        return FALSE;
    }

    *types = libmirage.writers;
    *num_writers = libmirage.num_writers;

    return TRUE;
}

/**
 * mirage_get_writers_info:
 * @info: (out) (array length=num_writers) (transfer none): array of writers' information structures
 * @num_writers: (out): number of supported writers
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves information structures for supported parsers.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_get_writers_info (const MirageWriterInfo **info, gint *num_writers, GError **error)
{
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, Q_("Library not initialized!"));
        return FALSE;
    }

    *info = libmirage.writers_info;
    *num_writers = libmirage.num_writers;

    return TRUE;
}

/**
 * mirage_enumerate_writers:
 * @func: (in) (scope call): callback function
 * @user_data: (in) (closure): data to be passed to callback function
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Iterates over list of supported writers, calling @func for each writers.
 *
 * If @func returns %FALSE, the function immediately returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_enumerate_writers (MirageEnumWriterInfoCallback func, gpointer user_data, GError **error)
{
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, Q_("Library not initialized!"));
        return FALSE;
    }

    /* Go over all writers */
    for (gint i = 0; i < libmirage.num_writers; i++) {
        if (!(*func)(&libmirage.writers_info[i], user_data)) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, Q_("Iteration has been cancelled!"));
            return FALSE;
        }
    }

    return TRUE;
}


/**
 * mirage_get_filter_streams_type:
 * @types: (out) (array length=num_filter_streams) (transfer none): array of filter streams' #GType values
 * @num_filter_streams: (out): number of supported filter streams
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves #GType values for supported filter streams.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_get_filter_streams_type (const GType **types, gint *num_filter_streams, GError **error)
{
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, Q_("Library not initialized!"));
        return FALSE;
    }

    *types = libmirage.filter_streams;
    *num_filter_streams = libmirage.num_filter_streams;

    return TRUE;
}

/**
 * mirage_get_filter_streams_info:
 * @info: (out) (array length=num_filter_streams) (transfer none): array of filter streams' information structures
 * @num_filter_streams: (out): number of supported filter streams
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves information structures for supported filter streams.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_get_filter_streams_info (const MirageFilterStreamInfo **info, gint *num_filter_streams, GError **error)
{
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, Q_("Library not initialized!"));
        return FALSE;
    }

    *info = libmirage.filter_streams_info;
    *num_filter_streams = libmirage.num_filter_streams;

    return TRUE;
}

/**
 * mirage_enumerate_filter_streams:
 * @func: (in) (scope call): callback function
 * @user_data: (in) (closure): data to be passed to callback function
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Iterates over list of supported filter streams, calling @func for each filter stream.
 *
 * If @func returns %FALSE, the function immediately returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_enumerate_filter_streams (MirageEnumFilterStreamInfoCallback func, gpointer user_data, GError **error)
{
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, Q_("Library not initialized!"));
        return FALSE;
    }

    /* Go over all filter streams */
    for (gint i = 0; i < libmirage.num_filter_streams; i++) {
        if (!(*func)(&libmirage.filter_streams_info[i], user_data)) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, Q_("Iteration has been cancelled!"));
            return FALSE;
        }
    }

    return TRUE;
}


/**
 * mirage_get_supported_debug_masks:
 * @masks: (out) (transfer none) (array length=num_masks): location to store pointer to masks array
 * @num_masks: (out): location to store number of elements in masks array
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves the pointer to array of supported debug masks and stores it in @masks.
 * The array consists of one or more structures of type #MirageDebugMaskInfo. The
 * number of elements in the array is stored in @num_masks. The array belongs to
 * libMirage and should not be altered or freed.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_get_supported_debug_masks (const MirageDebugMaskInfo **masks, gint *num_masks, GError **error)
{
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, Q_("Library not initialized!"));
        return FALSE;
    }

    *masks = dbg_masks;
    *num_masks = G_N_ELEMENTS(dbg_masks);

    return TRUE;
}


/**
 * mirage_create_writer:
 * @writer_id: (in): ID of writer to create
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Attempts to create an instance of image writer whose ID is @writer_id.
 *
 * Returns: (transfer full): newly-created writer object on success, %NULL
 * on failure. The reference to the object should be released using
 * g_object_unref() when no longer needed.
 */
MirageWriter *mirage_create_writer (const gchar *writer_id, GError **error)
{
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, Q_("Library not initialized!"));
        return NULL;
    }

    for (gint i = 0; i < libmirage.num_writers; i++) {
        if (!g_ascii_strcasecmp(writer_id, libmirage.writers_info[i].id)) {
            return g_object_new(libmirage.writers[i], NULL);
        }
    }

    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, Q_("Writer '%s' not found!"), writer_id);
    return NULL;
}

