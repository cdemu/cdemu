/*
 *  libMirage: Main library functions
 *  Copyright (C) 2008-2010 Rok Mandeljc
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

static struct {
    gboolean initialized;
    
    guint num_parsers;
    GType *parsers;
    
    guint num_fragments;
    GType *fragments;
        
    /* Password function */
    MIRAGE_PasswordFunction password_func;
    gpointer password_data;
} libmirage;

static const MIRAGE_DebugMask dbg_masks[] = {
    { "MIRAGE_DEBUG_GOBJECT", MIRAGE_DEBUG_GOBJECT },
    { "MIRAGE_DEBUG_CHAIN", MIRAGE_DEBUG_CHAIN },
    { "MIRAGE_DEBUG_PARSER", MIRAGE_DEBUG_PARSER },
    { "MIRAGE_DEBUG_DISC", MIRAGE_DEBUG_DISC },
    { "MIRAGE_DEBUG_SESSION", MIRAGE_DEBUG_SESSION },
    { "MIRAGE_DEBUG_TRACK", MIRAGE_DEBUG_TRACK },
    { "MIRAGE_DEBUG_SECTOR", MIRAGE_DEBUG_SECTOR },
    { "MIRAGE_DEBUG_FRAGMENT", MIRAGE_DEBUG_FRAGMENT },
    { "MIRAGE_DEBUG_CDTEXT", MIRAGE_DEBUG_CDTEXT },
};

/**
 * libmirage_init:
 * @error: location to store error, or %NULL
 *
 * <para>
 * Initializes libMirage library. It should be called before any other of 
 * libMirage functions.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean libmirage_init (GError **error) {
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
        mirage_error(MIRAGE_E_PLUGINDIR, error);
        return FALSE;
    }
    
    /* Check every file in the plugin dir */
    while ((plugin_file = g_dir_read_name(plugins_dir))) {
        if (g_str_has_suffix(plugin_file, ".so")) {
            MIRAGE_Plugin *plugin = NULL;
            gchar *fullpath = NULL;
            
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
    
    /* *** Get parsers and fragments *** */
    libmirage.parsers = g_type_children(MIRAGE_TYPE_PARSER, &libmirage.num_parsers);
    libmirage.fragments = g_type_children(MIRAGE_TYPE_FRAGMENT, &libmirage.num_fragments);
        
    /* Reset password function pointers */
    libmirage.password_func = NULL;
    libmirage.password_data = NULL;
    
    /* We're officially initialized now */
    libmirage.initialized = TRUE;
    
    return TRUE;
}


/**
 * libmirage_shutdown:
 * @error: location to store error, or %NULL
 *
 * <para>
 * Shuts down libMirage library. It should be called when libMirage is no longer
 * needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean libmirage_shutdown (GError **error) {
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        mirage_error(MIRAGE_E_NOTINIT, error);
        return FALSE;
    }
    
    /* Free parsers and fragments */
    g_free(libmirage.parsers);
    g_free(libmirage.fragments);
    
    /* We're not initialized anymore */
    libmirage.initialized = FALSE;
    
    return TRUE;
}

/**
 * libmirage_set_password_function:
 * @func: a password function pointer
 * @user_data: pointer to user data to be passed to the password function
 * @error: location to store error, or %NULL
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
gboolean libmirage_set_password_function (MIRAGE_PasswordFunction func, gpointer user_data, GError **error) {
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        mirage_error(MIRAGE_E_NOTINIT, error);
        return FALSE;
    }
    
    /* Store the pointers */
    libmirage.password_func = func;
    libmirage.password_data = user_data;
    
    return TRUE;
}

/**
 * libmirage_obtain_password:
 * @error: location to store error, or %NULL
 *
 * <para>
 * Obtains password string, using the #MIRAGE_PasswordFunction callback that was 
 * provided via libmirage_set_password_function().
 * </para>
 *
 * Returns: password string on success, %NULL on failure. The string should be
 * freed with g_free() when no longer needed.
 **/
gchar *libmirage_obtain_password (GError **error) {
    gchar *password;
    
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        mirage_error(MIRAGE_E_NOTINIT, error);
        return NULL;
    }
    
    if (!libmirage.password_func) {
        mirage_error(MIRAGE_E_DATANOTSET, error);
        return NULL;
    }
    
    /* Call the function pointer */
    password = (*libmirage.password_func)(libmirage.password_data);
    
    if (!password) {
        mirage_error(MIRAGE_E_NOPASSWORD, error);
    }
    
    return password;
}


/**
 * libmirage_create_disc:
 * @filenames: filename(s)
 * @debug_context: debug context to be attached to disc object, or %NULL
 * @params: parser parameters, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Creates a #MIRAGE_Disc object representing image stored in @filenames. @filenames
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
 * Returns: a #MIRAGE_Disc object on success, %NULL on failure. The reference to
 * the object should be released using g_object_unref() when no longer needed.
 **/
GObject *libmirage_create_disc (gchar **filenames, GObject *debug_context, GHashTable *params, GError **error) {
    GObject *disc;
    gint i;
    
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        mirage_error(MIRAGE_E_NOTINIT, error);
        return NULL;
    }
    
    /* Check if filename(s) is/are valid */
    for (i = 0; i < g_strv_length(filenames); i++) {
        if (!g_file_test(filenames[i], G_FILE_TEST_IS_REGULAR)) {
            mirage_error(MIRAGE_E_IMAGEFILE, error);
            return NULL;
        }
    }
    
    /* Go over all parsers */
    for (i = 0; i < libmirage.num_parsers; i++) {
        GError *local_error = NULL;
        gboolean succeeded;
        GObject *parser;

        /* Create parser object */
        parser = g_object_new(libmirage.parsers[i], NULL);
        
        /* If provided, attach the debug context to parser */
        if (debug_context) {
            mirage_object_set_debug_context(MIRAGE_OBJECT(parser), debug_context, NULL);
        }
        
        /* Pass the parameters to parser */
        if (params) {
            mirage_parser_set_params(MIRAGE_PARSER(parser), params, NULL);
        }
                
        /* Try loading image */
        succeeded = mirage_parser_load_image(MIRAGE_PARSER(parser), filenames, &disc, &local_error);

        /* Free parser */
        g_object_unref(parser);
        
        /* If loading succeeded, break the loop */
        if (succeeded) {          
            return disc;
        } else {
            /* MIRAGE_E_CANTHANDLE is the only acceptable error here; anything
               other indicates that parser attempted to handle image and failed */
            if (local_error->code == MIRAGE_E_CANTHANDLE) {
                g_error_free(local_error);
            } else {
                *error = local_error;
                return NULL;
            }
        }
    }
    
    /* No parser found */
    mirage_error(MIRAGE_E_NOPARSERFOUND, error);    
    return NULL;
}



/**
 * libmirage_create_fragment:
 * @fragment_interface: interface that fragment should implement
 * @filename: filename of data file that fragment should be able to handle
 * @error: location to store error, or %NULL
 *
 * <para>
 * Creates a #MIRAGE_Fragment implementation that implements interface specified 
 * by @fragment_interface and can handle data file with file name @filename.
 * </para>
 *
 * Returns: a #MIRAGE_Fragment object on success, %NULL on failure. The reference 
 * to the object should be released using g_object_unref() when no longer needed.
 **/
GObject *libmirage_create_fragment (GType fragment_interface, const gchar *filename, GError **error) {
    gboolean succeeded = TRUE;
    GObject *fragment;
    gint i;
    
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        mirage_error(MIRAGE_E_NOTINIT, error);
        return NULL;
    }

    /* Check if filename is valid, but only if we're not dealing with NULL fragment */
    if (fragment_interface != MIRAGE_TYPE_FINTERFACE_NULL && !g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
        mirage_error(MIRAGE_E_DATAFILE, error);
        return NULL;
    }
    
    /* Go over all fragments */    
    for (i = 0; i < libmirage.num_fragments; i++) {
        /* Create fragment; check if it implements requested interface, then
           try to load data... if we fail, we try next one */
        fragment = g_object_new(libmirage.fragments[i], NULL);
        /* Check if requested interface is supported */
        succeeded = G_TYPE_CHECK_INSTANCE_TYPE((fragment), fragment_interface);
        if (succeeded) {
            /* Check if fragment can handle file format */
            succeeded = mirage_fragment_can_handle_data_format(MIRAGE_FRAGMENT(fragment), filename, NULL);
            if (succeeded) {
                return fragment;
            }
        }
        
        g_object_unref(fragment);
        fragment = NULL;
    }
    
    /* No fragment found */
    mirage_error(MIRAGE_E_NOFRAGMENTFOUND, error);
    return NULL;
}


/**
 * libmirage_for_each_parser:
 * @func: callback function
 * @user_data: data to be passed to callback function
 * @error: location to store error, or %NULL
 *
 * <para>
 * Iterates over list of supported parsers, calling @func for each parser.
 * </para>
 *
 * <para>
 * If @func returns %FALSE, the function immediately returns %FALSE and @error 
 * is set to %MIRAGE_E_ITERCANCELLED.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean libmirage_for_each_parser (MIRAGE_CallbackFunction func, gpointer user_data, GError **error) {
    gint i;
    
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        mirage_error(MIRAGE_E_NOTINIT, error);
        return FALSE;
    }
    
    /* Make sure we've been given callback function */
    if (!func) {
        mirage_error(MIRAGE_E_INVALIDARG, error);
        return FALSE;
    }
    
    /* Go over all parsers */
    for (i = 0; i < libmirage.num_parsers; i++) {
        const MIRAGE_ParserInfo *parser_info;
        gboolean succeeded;
        GObject *parser;
        
        parser = g_object_new(libmirage.parsers[i], NULL);
        mirage_parser_get_parser_info(MIRAGE_PARSER(parser), &parser_info, NULL);
        succeeded = (*func)((const gpointer)parser_info, user_data);
        g_object_unref(parser);
        if (!succeeded) {
            mirage_error(MIRAGE_E_ITERCANCELLED, error);
            return FALSE;
        }
    }
     
    return TRUE;
}

/**
 * libmirage_for_each_fragment:
 * @func: callback function
 * @user_data: data to be passed to callback function
 * @error: location to store error, or %NULL
 *
 * <para>
 * Iterates over list of supported fragments, calling @func for each fragment.
 * </para>
 *
 * <para>
 * If @func returns %FALSE, the function immediately returns %FALSE and @error 
 * is set to %MIRAGE_E_ITERCANCELLED.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean libmirage_for_each_fragment (MIRAGE_CallbackFunction func, gpointer user_data, GError **error) {
    gint i;
    
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        mirage_error(MIRAGE_E_NOTINIT, error);
        return FALSE;
    }
    
    /* Make sure we've been given callback function */
    if (!func) {
        mirage_error(MIRAGE_E_INVALIDARG, error);
        return FALSE;
    }
    
    /* Go over all fragments */
    for (i = 0; i < libmirage.num_fragments; i++) {
        const MIRAGE_FragmentInfo *fragment_info;
        gboolean succeeded;
        GObject *fragment;
        
        fragment = g_object_new(libmirage.fragments[i], NULL);
        mirage_fragment_get_fragment_info(MIRAGE_FRAGMENT(fragment), &fragment_info, NULL);
        succeeded = (*func)((const gpointer)fragment_info, user_data);
        g_object_unref(fragment);
        if (!succeeded) {
            mirage_error(MIRAGE_E_ITERCANCELLED, error);
            return FALSE;
        }
    }
     
    return TRUE;
}


/**
 * libmirage_get_supported_debug_masks:
 * @masks: location to store pointer to masks array
 * @num_masks: location to store number of elements in masks array
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves the pointer to array of supported debug masks and stores it in @masks. 
 * The array consists of one or more structures of type #MIRAGE_DebugMask. The 
 * number of elements in the array is stored in @num_masks. The array belongs to
 * libMirage and should not be altered or freed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean libmirage_get_supported_debug_masks (const MIRAGE_DebugMask **masks, gint *num_masks, GError **error) {
    /* Make sure libMirage is initialized */
    if (!libmirage.initialized) {
        mirage_error(MIRAGE_E_NOTINIT, error);
        return FALSE;
    }
    
    /* Check arguments */
    if (!masks || !num_masks) {
        mirage_error(MIRAGE_E_INVALIDARG, error);
        return FALSE;
    }
    
    *masks = dbg_masks;
    *num_masks = G_N_ELEMENTS(dbg_masks);
    
    return TRUE;
}
