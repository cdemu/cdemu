/*
 *  libMirage: Main library functions
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


static struct
{
    gboolean initialized;

    guint num_parsers;
    GType *parsers;

    guint num_fragments;
    GType *fragments;

    guint num_file_filters;
    GType *file_filters;

    /* Password function */
    MiragePasswordFunction password_function;
    gpointer password_data;

    /* GFileInputStream -> filename mapping */
    GHashTable *file_streams_map;
} libmirage;


static const MirageDebugMask dbg_masks[] = {
    { "MIRAGE_DEBUG_PARSER", MIRAGE_DEBUG_PARSER },
    { "MIRAGE_DEBUG_DISC", MIRAGE_DEBUG_DISC },
    { "MIRAGE_DEBUG_SESSION", MIRAGE_DEBUG_SESSION },
    { "MIRAGE_DEBUG_TRACK", MIRAGE_DEBUG_TRACK },
    { "MIRAGE_DEBUG_SECTOR", MIRAGE_DEBUG_SECTOR },
    { "MIRAGE_DEBUG_FRAGMENT", MIRAGE_DEBUG_FRAGMENT },
    { "MIRAGE_DEBUG_CDTEXT", MIRAGE_DEBUG_CDTEXT },
    { "MIRAGE_DEBUG_FILE_IO", MIRAGE_DEBUG_FILE_IO },
};



/**********************************************************************\
 *          GFileInputStream -> filename mapping management           *
\**********************************************************************/
static inline void stream_destroyed_handler (gpointer unused G_GNUC_UNUSED, gpointer stream)
{
    g_hash_table_remove(libmirage.file_streams_map, stream);
}

static inline void mirage_file_streams_map_add (gpointer stream, const gchar *filename)
{
    /* Insert into the table */
    g_hash_table_insert(libmirage.file_streams_map, stream, g_strdup(filename));

    /* Enable cleanup when stream is destroyed */
    g_object_weak_ref(stream, (GWeakNotify)stream_destroyed_handler, NULL);
}



/**********************************************************************\
 *                         Public API                                 *
\**********************************************************************/
/**
 * mirage_initialize:
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Initializes libMirage library. It should be called before any other of
 * libMirage functions.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_initialize (GError **error)
{
    const gchar *plugin_file;
    GDir *plugins_dir;

    /* If already initialized, don't do anything */
    if (libmirage.initialized) {
        return TRUE;
    }

    /* *** Load plugins *** */
    /* Open plugins dir */
    plugins_dir = g_dir_open(MIRAGE_PLUGIN_DIR, 0, NULL);

    if (!plugins_dir) {
        g_error("Failed to open plugin directory '%s'!\n", MIRAGE_PLUGIN_DIR);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Failed to open plugin directory '%s'!\n", MIRAGE_PLUGIN_DIR);
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

    /* *** Get parsers, fragments and file filters *** */
    libmirage.parsers = g_type_children(MIRAGE_TYPE_PARSER, &libmirage.num_parsers);
    libmirage.fragments = g_type_children(MIRAGE_TYPE_FRAGMENT, &libmirage.num_fragments);
    libmirage.file_filters = g_type_children(MIRAGE_TYPE_FILE_FILTER, &libmirage.num_file_filters);

    /* Reset password function pointers */
    libmirage.password_function = NULL;
    libmirage.password_data = NULL;

    /* GFileInputStream -> filename map */
    libmirage.file_streams_map = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);

    /* We're officially initialized now */
    libmirage.initialized = TRUE;

    return TRUE;
}


/**
 * mirage_shutdown:
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Shuts down libMirage library. It should be called when libMirage is no longer
 * needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_shutdown (GError **error)
{
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Library not initialized!");
        return FALSE;
    }

    /* Free parsers, fragments and file filters */
    g_free(libmirage.parsers);
    g_free(libmirage.fragments);
    g_free(libmirage.file_filters);

    /* Free GFileInputStream -> filename map */
    g_hash_table_unref(libmirage.file_streams_map);

    /* We're not initialized anymore */
    libmirage.initialized = FALSE;

    return TRUE;
}

/**
 * mirage_set_password_function:
 * @func: (in) (allow-none) (scope call): a password function pointer
 * @user_data: (in) (closure): pointer to user data to be passed to the password function
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Sets the password function to libMirage. The function is used by parsers
 * that support encrypted images to obtain password for unlocking such images.
 * </para>
 *
 * <para>
 * Both @func and @user_data can be %NULL; in that case the appropriate setting
 * will be reset.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_set_password_function (MiragePasswordFunction func, gpointer user_data, GError **error)
{
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Library not initialized!");
        return FALSE;
    }

    /* Store the pointers */
    libmirage.password_function = func;
    libmirage.password_data = user_data;

    return TRUE;
}

/**
 * mirage_obtain_password:
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Obtains password string, using the #MiragePasswordFunction callback that was
 * provided via mirage_set_password_function().
 * </para>
 *
 * Returns: password string on success, %NULL on failure. The string should be
 * freed with g_free() when no longer needed.
 **/
gchar *mirage_obtain_password (GError **error)
{
    gchar *password;

    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Library not initialized!");
        return NULL;
    }

    if (!libmirage.password_function) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Password function has not been set!");
        return NULL;
    }

    /* Call the function pointer */
    password = (*libmirage.password_function)(libmirage.password_data);

    if (!password) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Password has not been provided!");
    }

    return password;
}


/**
 * mirage_create_disc:
 * @filenames: (in) (array zero-terminated=1): filename(s)
 * @debug_context: (in) (allow-none): debug context to be attached to disc object, or %NULL
 * @params: (in) (allow-none) (element-type gchar* GValue): parser parameters, or %NULL
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Creates a #MirageDisc object representing image stored in @filenames. @filenames
 * is a NULL-terminated list of filenames containing image data. The function tries
 * to find a parser that can handle give filename(s), creates disc implementation,
 * attaches @debug_context to it (if provided) and attempts to load the data into
 * disc object.
 * </para>
 *
 * <para>
 * @params, if not %NULL, is a #GHashTable containing parser parameters (such as
 * password, encoding, etc.) - it must have strings for its keys and values of
 * #GValue type. The hash table is passed to the parser; whether parameters are
 * actually used (or supported) by the parser, however, depends on the parser
 * implementation. If parser does not support a parameter, it will be ignored.
 * </para>
 *
 * <para>
 * If multiple filenames are provided and parser supports only single-file images,
 * only the first filename is used.
 * </para>
 *
 * Returns: (transfer full): a #MirageDisc object on success, %NULL on failure. The reference to
 * the object should be released using g_object_unref() when no longer needed.
 **/
MirageDisc *mirage_create_disc (gchar **filenames, GObject *debug_context, GHashTable *params, GError **error)
{
    MirageDisc *disc = NULL;
    GInputStream **streams;

    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Library not initialized!");
        return NULL;
    }

    /* Create streams */
    streams = g_new0(GInputStream *, g_strv_length(filenames)+1);
    for (gint i = 0; filenames[i]; i++) {
        streams[i] = mirage_create_file_stream(filenames[i], debug_context, error);
        if (!streams[i]) {
            goto end;
        }
    }

    /* Go over all parsers */
    for (gint i = 0; i < libmirage.num_parsers; i++) {
        GError *local_error = NULL;
        MirageParser *parser;

        /* Create parser object */
        parser = g_object_new(libmirage.parsers[i], NULL);

        /* Attach the debug context to parser */
        mirage_debuggable_set_debug_context(MIRAGE_DEBUGGABLE(parser), MIRAGE_DEBUG_CONTEXT(debug_context));

        /* Pass the parameters to parser */
        mirage_parser_set_params(parser, params);

        /* Try loading image */
        disc = mirage_parser_load_image(parser, streams, &local_error);

        /* Free parser */
        g_object_unref(parser);

        /* If loading succeeded, break the loop */
        if (disc) {
            goto end;
        } else {
            /* MIRAGE_ERROR_CANNOT_HANDLE is the only acceptable error here; anything
               other indicates that parser attempted to handle image and failed */
            if (local_error->code == MIRAGE_ERROR_CANNOT_HANDLE) {
                g_error_free(local_error);
            } else {
                g_propagate_error(error, local_error);
                goto end;
            }
        }
    }

    /* No parser found */
    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "No parser can handle the image file!");

end:
    /* Close streams */
    for (gint i = 0; streams[i]; i++) {
        g_object_unref(streams[i]);
    }
    g_free(streams);

    return disc;
}



/**
 * mirage_create_fragment:
 * @fragment_interface: (in): interface that fragment should implement
 * @stream: (in): the data stream that fragment should be able to handle
 * @debug_context: (in) (allow-none): debug context or debuggable object to set to fragment, or %NULL
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Creates a #MirageFragment implementation that implements interface specified
 * by @fragment_interface and can handle data stored in @stream. If provided,
 * @debug_context is set to the fragment.
 * </para>
 *
 * <para>
 * If @debug_context is a #MirageDebugContext object, it is set to the file stream's
 * #MirageDebuggable interface. If @debug is an object implementing #MirageDebuggable
 * interface, then its debug context is retrieved and set to the file stream.
 * </para>
 *
 * Returns: (transfer full): a #MirageFragment object on success, %NULL on failure. The reference
 * to the object should be released using g_object_unref() when no longer needed.
 **/
MirageFragment *mirage_create_fragment (GType fragment_interface, GInputStream *stream, GObject *debug_context, GError **error)
{
    gboolean succeeded = TRUE;
    MirageFragment *fragment;

    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Library not initialized!");
        return NULL;
    }

    /* debug_context can be either a MirageDebugContext or an object implementing
       MirageDebuggable interface... in the latter case, fetch its actual
       debug context */
    if (debug_context) {
        if (MIRAGE_IS_DEBUGGABLE(debug_context)) {
            debug_context = G_OBJECT(mirage_debuggable_get_debug_context(MIRAGE_DEBUGGABLE(debug_context)));
            g_object_unref(debug_context); /* Keep just pointer */
        } else if (!MIRAGE_IS_DEBUG_CONTEXT(debug_context)) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Invalid debug context or debuggable object!");
            return NULL;
        }
    }

    /* Go over all fragments */
    for (gint i = 0; i < libmirage.num_fragments; i++) {
        /* Create fragment; check if it implements requested interface, then
           try to load data... if we fail, we try next one */
        fragment = g_object_new(libmirage.fragments[i], NULL);
        /* Check if requested interface is supported */
        succeeded = G_TYPE_CHECK_INSTANCE_TYPE((fragment), fragment_interface);
        if (succeeded) {
            /* Set debug context */
            mirage_debuggable_set_debug_context(MIRAGE_DEBUGGABLE(fragment), MIRAGE_DEBUG_CONTEXT(debug_context));

            /* Check if fragment can handle file format */
            succeeded = mirage_fragment_can_handle_data_format(fragment, stream, NULL);
            if (succeeded) {
                return fragment;
            }
        }

        g_object_unref(fragment);
        fragment = NULL;
    }

    /* No fragment found */
    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "No fragment can handle the given data file!");
    return NULL;
}

/**
 * mirage_create_file_stream:
 * @filename: (in): filename to create stream on
 * @debug_context: (in) (allow-none): debug context or debuggable object to set to file stream, or %NULL
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Opens a file pointed to by @filename and creates a chain of file filters
 * on top of it. If provided, @debug_context is set to the file stream.
 * </para>
 *
 * <para>
 * If @debug_context is a #MirageDebugContext object, it is set to the file stream's
 * #MirageDebuggable interface. If @debug is an object implementing #MirageDebuggable
 * interface, then its debug context is retrieved and set to the file stream.
 * </para>
 *
 * Returns: (transfer full): on success, an object inheriting #GFilterInputStream (and therefore
 * #GInputStream) and implementing #GSeekable interface is returned, which
 * can be used to access data stored in file. On failure, %NULL is returned.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GInputStream *mirage_create_file_stream (const gchar *filename, GObject *debug_context, GError **error)
{
    GInputStream *stream;
    GFile *file;
    GFileType file_type;
    GError *local_error = NULL;

    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Library not initialized!");
        return NULL;
    }

    /* debug_context can be either a MirageDebugContext or an object implementing
       MirageDebuggable interface... in the latter case, fetch its actual
       debug context */
    if (debug_context) {
        if (MIRAGE_IS_DEBUGGABLE(debug_context)) {
            debug_context = G_OBJECT(mirage_debuggable_get_debug_context(MIRAGE_DEBUGGABLE(debug_context)));
            g_object_unref(debug_context); /* Keep just pointer */
        } else if (!MIRAGE_IS_DEBUG_CONTEXT(debug_context)) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Invalid debug context or debuggable object!");
            return NULL;
        }
    }

    /* Open file; at the bottom of the chain, there's always a GFileStream */
    file = g_file_new_for_path(filename);

    /* Make sure file is either a valid regular file or a symlink/shortcut */
    file_type = g_file_query_file_type(file, G_FILE_QUERY_INFO_NONE, NULL);
    if (!(file_type == G_FILE_TYPE_REGULAR || file_type == G_FILE_TYPE_SYMBOLIC_LINK || file_type == G_FILE_TYPE_SHORTCUT)) {
        g_object_unref(file);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DATA_FILE_ERROR, "Invalid data file provided for stream!");
        return FALSE;
    }

    /* Create stream */
    stream = G_INPUT_STREAM(g_file_read(file, NULL, &local_error));

    g_object_unref(file);

    if (!stream) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DATA_FILE_ERROR, "Failed to open file stream on data file: %s!", local_error->message);
        g_error_free(local_error);
        return FALSE;
    }

    /* Store the GFileInputStream -> filename mapping */
    mirage_file_streams_map_add(stream, filename);

    /* Construct a chain of filters */
    MirageFileFilter *filter;
    gboolean found_new;

    do {
        found_new = FALSE;

        for (gint i = 0; i < libmirage.num_file_filters; i++) {
            /* Create filter object and check if it can handle data */
            filter = g_object_new(libmirage.file_filters[i], "base-stream", stream, "close-base-stream", FALSE, NULL);

            mirage_debuggable_set_debug_context(MIRAGE_DEBUGGABLE(filter), MIRAGE_DEBUG_CONTEXT(debug_context));

            if (!mirage_file_filter_can_handle_data_format(filter, NULL)) {
                /* Cannot handle data format... */
                g_object_unref(filter);
            } else {
                /* Release reference to (now) underlying stream */
                g_object_unref(stream);

                /* Now the underlying stream should be closed when we close filter's stream */
                g_filter_input_stream_set_close_base_stream (G_FILTER_INPUT_STREAM(filter), TRUE);

                /* Filter becomes new underlying stream */
                stream = G_INPUT_STREAM(filter);

                /* Repeat the whole cycle */
                found_new = TRUE;
                break;
            }
        }
    } while (found_new);

    /* Make sure that the stream we're returning is rewound to the beginning */
    g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);

    return stream;
}


/**
 * mirage_get_file_stream_filename:
 * @stream: (in): a #GInputStream
 *
 * <para>
 * Traverses the chain of file filters and retrieves the filename on which
 * the #GFileInputStream, located at the bottom of the chain, was opened.
 * </para>
 *
 * Returns: (transfer none): on success, a pointer to filename on which
 * the underyling file stream was opened. On failure, %NULL is returned.
 **/
const gchar *mirage_get_file_stream_filename (GInputStream *stream)
{
    if (G_IS_FILTER_INPUT_STREAM(stream)) {
        /* Recursively traverse the filter stream chain */
        GInputStream *base_stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(stream));
        if (!base_stream) {
            return NULL;
        } else {
            return mirage_get_file_stream_filename(base_stream);
        }
    } else if (G_IS_FILE_INPUT_STREAM(stream)) {
        /* We are at the bottom; get filename from our mapping table */
        return g_hash_table_lookup(libmirage.file_streams_map, stream);
    }

    /* Invalid stream type */
    return NULL;
}


/**
 * mirage_enumerate_parsers:
 * @func: (in) (scope call): callback function
 * @user_data: (in) (closure): data to be passed to callback function
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Iterates over list of supported parsers, calling @func for each parser.
 * </para>
 *
 * <para>
 * If @func returns %FALSE, the function immediately returns %FALSE.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_enumerate_parsers (MirageEnumParserInfoCallback func, gpointer user_data, GError **error)
{
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Library not initialized!");
        return FALSE;
    }

    /* Go over all parsers */
    for (gint i = 0; i < libmirage.num_parsers; i++) {
        const MirageParserInfo *parser_info;
        gboolean succeeded;
        GObject *parser;

        parser = g_object_new(libmirage.parsers[i], NULL);
        parser_info = mirage_parser_get_info(MIRAGE_PARSER(parser));
        succeeded = (*func)(parser_info, user_data);
        g_object_unref(parser);
        if (!succeeded) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Iteration has been cancelled!");
            return FALSE;
        }
    }

    return TRUE;
}

/**
 * mirage_enumerate_fragments:
 * @func: (in) (scope call): callback function
 * @user_data: (in) (closure): data to be passed to callback function
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Iterates over list of supported fragments, calling @func for each fragment.
 * </para>
 *
 * <para>
 * If @func returns %FALSE, the function immediately returns %FALSE.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_enumerate_fragments (MirageEnumFragmentInfoCallback func, gpointer user_data, GError **error)
{
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Library not initialized!");
        return FALSE;
    }

    /* Go over all fragments */
    for (gint i = 0; i < libmirage.num_fragments; i++) {
        const MirageFragmentInfo *fragment_info;
        gboolean succeeded;
        GObject *fragment;

        fragment = g_object_new(libmirage.fragments[i], NULL);
        fragment_info = mirage_fragment_get_info(MIRAGE_FRAGMENT(fragment));
        succeeded = (*func)(fragment_info, user_data);
        g_object_unref(fragment);
        if (!succeeded) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Iteration has been cancelled!");
            return FALSE;
        }
    }

    return TRUE;
}

/**
 * mirage_enumerate_file_filters:
 * @func: (in) (scope call): callback function
 * @user_data: (in) (closure): data to be passed to callback function
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Iterates over list of supported file filters, calling @func for each fragment.
 * </para>
 *
 * <para>
 * If @func returns %FALSE, the function immediately returns %FALSE.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_enumerate_file_filters (MirageEnumFileFilterInfoCallback func, gpointer user_data, GError **error)
{
    gboolean succeeded = TRUE;

    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Library not initialized!");
        return FALSE;
    }

    /* Create a dummy stream - because at gio 2.32, unreferencing the
       GFilterInputStream apparently tries to unref the underlying stream */
    GInputStream *dummy_stream = g_memory_input_stream_new();

    /* Go over all file filters */
    for (gint i = 0; i < libmirage.num_file_filters; i++) {
        const MirageFileFilterInfo *file_filter_info;
        GObject *filter;

        filter = g_object_new(libmirage.file_filters[i], "base-stream", dummy_stream, "close-base-stream", FALSE, NULL);
        file_filter_info = mirage_file_filter_get_info(MIRAGE_FILE_FILTER(filter));
        succeeded = (*func)(file_filter_info, user_data);
        g_object_unref(filter);
        if (!succeeded) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Iteration has been cancelled!");
            break;
        }
    }

    g_object_unref(dummy_stream);

    return succeeded;
}


/**
 * mirage_get_supported_debug_masks:
 * @masks: (out) (transfer none) (array length=num_masks): location to store pointer to masks array
 * @num_masks: (out): location to store number of elements in masks array
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves the pointer to array of supported debug masks and stores it in @masks.
 * The array consists of one or more structures of type #MirageDebugMask. The
 * number of elements in the array is stored in @num_masks. The array belongs to
 * libMirage and should not be altered or freed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_get_supported_debug_masks (const MirageDebugMask **masks, gint *num_masks, GError **error)
{
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Library not initialized!");
        return FALSE;
    }

    *masks = dbg_masks;
    *num_masks = G_N_ELEMENTS(dbg_masks);

    return TRUE;
}
