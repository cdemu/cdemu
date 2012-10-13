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
    MirageParserInfo *parsers_info;

    guint num_fragments;
    GType *fragments;
    MirageFragmentInfo *fragments_info;

    guint num_file_filters;
    GType *file_filters;
    MirageFileFilterInfo *file_filters_info;

    /* Password function */
    MiragePasswordFunction password_function;
    gpointer password_data;
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
 *            Parsers, fragments and file filters list                *
\**********************************************************************/
static void initialize_parsers_list ()
{
    libmirage.parsers = g_type_children(MIRAGE_TYPE_PARSER, &libmirage.num_parsers);

    libmirage.parsers_info = g_new(MirageParserInfo, libmirage.num_parsers);
    for (gint i = 0; i < libmirage.num_parsers; i++) {
        MirageParser *parser = g_object_new(libmirage.parsers[i], NULL);
        libmirage.parsers_info[i] = *mirage_parser_get_info(parser);
        g_object_unref(parser);
    }
}

static void initialize_fragments_list ()
{
    libmirage.fragments = g_type_children(MIRAGE_TYPE_FRAGMENT, &libmirage.num_fragments);

    libmirage.fragments_info = g_new(MirageFragmentInfo, libmirage.num_fragments);
    for (gint i = 0; i < libmirage.num_fragments; i++) {
        MirageFragment *fragment = g_object_new(libmirage.fragments[i], NULL);
        libmirage.fragments_info[i] = *mirage_fragment_get_info(fragment);
        g_object_unref(fragment);
    }
}

static void initialize_file_filters_list ()
{
    /* Create a dummy stream - because at gio 2.32, unreferencing the
       GFilterInputStream apparently tries to unref the underlying stream */
    GInputStream *dummy_stream = g_memory_input_stream_new();

    libmirage.file_filters = g_type_children(MIRAGE_TYPE_FILE_FILTER, &libmirage.num_file_filters);

    libmirage.file_filters_info = g_new(MirageFileFilterInfo, libmirage.num_file_filters);
    for (gint i = 0; i < libmirage.num_file_filters; i++) {
        MirageFileFilter *file_filter = g_object_new(libmirage.file_filters[i], "base-stream", dummy_stream, "close-base-stream", FALSE, NULL);
        libmirage.file_filters_info[i] = *mirage_file_filter_get_info(file_filter);
        g_object_unref(file_filter);
    }

    g_object_unref(dummy_stream);
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
    initialize_parsers_list();
    initialize_fragments_list();
    initialize_file_filters_list();

    /* Reset password function pointers */
    libmirage.password_function = NULL;
    libmirage.password_data = NULL;

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
 * mirage_get_parsers_type:
 * @types: (out) (array length=num_parsers): array of parsers' #GType values
 * @num_parsers: (out): number of supported parsers
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves #GType values for supported parsers.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_get_parsers_type (GType **types, gint *num_parsers, GError **error)
{
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Library not initialized!");
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
 * <para>
 * Retrieves information structures for supported parsers.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_get_parsers_info (const MirageParserInfo **info, gint *num_parsers, GError **error)
{
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Library not initialized!");
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
        if (!(*func)(&libmirage.parsers_info[i], user_data)) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Iteration has been cancelled!");
            return FALSE;
        }
    }

    return TRUE;
}


/**
 * mirage_get_fragments_type:
 * @types: (out) (array length=num_fragments): array of fragments' #GType values
 * @num_fragments: (out): number of supported fragments
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves #GType values for supported fragments.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_get_fragments_type (GType **types, gint *num_fragments, GError **error)
{
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Library not initialized!");
        return FALSE;
    }

    *types = libmirage.fragments;
    *num_fragments = libmirage.num_fragments;

    return TRUE;
}

/**
 * mirage_get_fragments_info:
 * @info: (out) (array length=num_fragments) (transfer none): array of fragments' information structures
 * @num_fragments: (out): number of supported fragments
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves information structures for supported fragments.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_get_fragments_info (const MirageFragmentInfo **info, gint *num_fragments, GError **error)
{
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Library not initialized!");
        return FALSE;
    }

    *info = libmirage.fragments_info;
    *num_fragments = libmirage.num_fragments;

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
        if (!(*func)(&libmirage.fragments_info[i], user_data)) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Iteration has been cancelled!");
            return FALSE;
        }
    }

    return TRUE;
}


/**
 * mirage_get_file_filters_type:
 * @types: (out) (array length=num_file_filters): array of file filters' #GType values
 * @num_file_filters: (out): number of supported file filters
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves #GType values for supported file filters.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_get_file_filters_type (GType **types, gint *num_file_filters, GError **error)
{
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Library not initialized!");
        return FALSE;
    }

    *types = libmirage.file_filters;
    *num_file_filters = libmirage.num_file_filters;

    return TRUE;
}

/**
 * mirage_get_file_filters_info:
 * @info: (out) (array length=num_file_filters) (transfer none): array of file filters' information structures
 * @num_file_filters: (out): number of supported file filters
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves information structures for supported file filters.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_get_file_filters_info (const MirageFileFilterInfo **info, gint *num_file_filters, GError **error)
{
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Library not initialized!");
        return FALSE;
    }

    *info = libmirage.file_filters_info;
    *num_file_filters = libmirage.num_file_filters;

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
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Library not initialized!");
        return FALSE;
    }

    /* Go over all file filters */
    for (gint i = 0; i < libmirage.num_file_filters; i++) {
        if (!(*func)(&libmirage.file_filters_info[i], user_data)) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Iteration has been cancelled!");
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
