/*
 *  libMirage: Mirage object
 *  Copyright (C) 2006-2008 Rok Mandeljc
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


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_MIRAGE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_MIRAGE, MIRAGE_MiragePrivate))

typedef struct {
    gchar *version;
    
    GList *plugins_list;
} MIRAGE_MiragePrivate;


/******************************************************************************\
 *                              Private functions                             *
\******************************************************************************/
static gboolean __mirage_mirage_is_valid_plugin_name (MIRAGE_Mirage *self, const gchar *filename) {
    return g_str_has_suffix(filename, ".so");
}

static gboolean __mirage_mirage_load_plugins (MIRAGE_Mirage *self, GError **error) {
    MIRAGE_MiragePrivate *_priv = MIRAGE_MIRAGE_GET_PRIVATE(self);

    const gchar *plugin_file = NULL;
    GDir *plugins_dir = NULL;
    
    /* Open plugins dir */
    plugins_dir = g_dir_open(MIRAGE_PLUGIN_DIR, 0, NULL);
    
    if (!plugins_dir) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open plugin directory '%s'!\n", __func__, MIRAGE_PLUGIN_DIR);
        mirage_error(MIRAGE_E_PLUGINDIR, error);
        return FALSE;
    }
    
    /* Check every file in the plugin dir */
    while ((plugin_file = g_dir_read_name(plugins_dir))) {
        if (__mirage_mirage_is_valid_plugin_name(self, plugin_file)) {
            MIRAGE_Plugin *plugin = NULL;
            gchar *fullpath = NULL;
            
            /* Build full path */
            fullpath = g_build_filename(MIRAGE_PLUGIN_DIR, plugin_file, NULL);
            
            plugin = mirage_plugin_new(fullpath);
            
            if (!g_type_module_use(G_TYPE_MODULE(plugin))) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to load module: %s!\n", __func__, fullpath);
                g_object_unref(plugin);
                g_free(fullpath);
                continue;
            }
                        
            /* Add plugin to plugins list */
            _priv->plugins_list = g_list_append(_priv->plugins_list, plugin);
            
            g_type_module_unuse(G_TYPE_MODULE(plugin));
            g_free(fullpath);
        }
    }
    
    g_dir_close(plugins_dir);
    
    return TRUE;
}


/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
/**
 * mirage_mirage_get_version:
 * @self: a #MIRAGE_Mirage
 * @version: location to return version string
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves libMirage version string.
 * </para>
 *
 * <para>
 * A copy of version string is stored into @version; it should be freed with
 * g_free() when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_mirage_get_version (MIRAGE_Mirage *self, gchar **version, GError **error) {
    MIRAGE_MiragePrivate *_priv = MIRAGE_MIRAGE_GET_PRIVATE(self);
    
    MIRAGE_CHECK_ARG(version);
    *version = g_strdup(_priv->version);
    
    return TRUE;
}


/**
 * mirage_mirage_create_disc:
 * @self: a #MIRAGE_Mirage
 * @filenames: filename(s)
 * @ret_disc: location to store disc object
 * @debug_context: debug context to be attached to disc object, or %NULL
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
 * If multiple filenames are provided and parser supports only single-file images,
 * the function fails.
 * </para>
 *
 * <para>
 * A reference to disc object is stored in @ret_disc; it should be released with 
 * g_object_unref() when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_mirage_create_disc (MIRAGE_Mirage *self, gchar **filenames, GObject **ret_disc, GObject *debug_context, GError **error) {
    gboolean succeeded = TRUE;
    GType *parsers = NULL;
    guint num_parsers = 0;
    GObject *disc = NULL;
    gint i;
    
    MIRAGE_CHECK_ARG(filenames);
    MIRAGE_CHECK_ARG(ret_disc);
        
    /* Check if filename(s) is/are valid */
    for (i = 0; i < g_strv_length(filenames); i++) {
        if (!g_file_test(filenames[i], G_FILE_TEST_IS_REGULAR)) {
            mirage_error(MIRAGE_E_IMAGEFILE, error);
            return FALSE;
        }
    }
    
    /* Get all registered children of type MIRAGE_TYPE_DISC... these are our
       parsers */
    parsers = g_type_children(MIRAGE_TYPE_DISC, &num_parsers);
    for (i = 0; i < num_parsers; i++) {
        gint j;
        
        /* Create disc object and try to load the image... if we fail, we try 
           next one */
        disc = g_object_new(parsers[i], NULL);
        
        /* Set Mirage (= self) to disc */
        mirage_object_set_mirage(MIRAGE_OBJECT(disc), G_OBJECT(self), NULL);
        /* If provided, attach the debug context to disc */
        if (debug_context) {
            mirage_object_set_debug_context(MIRAGE_OBJECT(disc), debug_context, NULL);
        }
        
        /* Check whether given parser can handle given filename(s) */
        gboolean can_handle_one_image = FALSE;
        for (j = 0; j < g_strv_length(filenames); j++) {
            succeeded = mirage_disc_can_load_file(MIRAGE_DISC(disc), filenames[j], NULL);
            /* We break the loop in case previous filename could be handled,
               but this one cannot be */
            if (can_handle_one_image && !succeeded) {
                break;
            }
            can_handle_one_image = TRUE;
        }
        
        /* If the parser can handle all of 'em, go ahead and load the disc */
        if (succeeded) {
            /* Try loading image */
            succeeded = mirage_disc_load_image(MIRAGE_DISC(disc), filenames, error);
            if (!succeeded) {
                /* Loading failed; delete disc and return false */
                g_object_unref(disc);
                break;
            }
            
            break;
        }
    }
    
    g_free(parsers);

    if (!succeeded) {
        mirage_error(MIRAGE_E_NOPARSERFOUND, error);
    }
    
    *ret_disc = disc;    
    return succeeded;
}


/**
 * mirage_mirage_create_fragment:
 * @self: a #MIRAGE_Mirage
 * @fragment_interface: interface that fragment should implement
 * @filename: filename of data file that fragment should be able to handle
 * @ret_fragment: location to store fragment
 * @error: location to store error, or %NULL
 *
 * <para>
 * Creates a #MIRAGE_Fragment implementation that implements interface specified 
 * by @fragment_interface and can handle data file with file name @filename.
 * </para>
 *
 * <para>
 * A reference to fragment object is stored in @ret_fragment; it should be 
 * released with g_object_unref() when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_mirage_create_fragment (MIRAGE_Mirage *self, GType fragment_interface, gchar *filename, GObject **ret_fragment, GError **error) {
    gboolean succeeded = TRUE;
    GType *fragments = NULL;
    guint num_fragments = 0;
    GObject *fragment = NULL;
    gint i;
    
    MIRAGE_CHECK_ARG(filename);
    MIRAGE_CHECK_ARG(ret_fragment);
    
    /* Get all registered children of type MIRAGE_TYPE_FRAGMENT... these are our
       fragments */
    fragments = g_type_children(MIRAGE_TYPE_FRAGMENT, &num_fragments);
    
    for (i = 0; i < num_fragments; i++) {
        /* Create fragment; check if it implements requested interface, then
           try to load data... if we fail, we try next one */
        fragment = g_object_new(fragments[i], NULL);
        
        /* Set Mirage (= self) to fragment */
        mirage_object_set_mirage(MIRAGE_OBJECT(fragment), G_OBJECT(self), NULL);
        
        /* Check if requested interface is supported */
        succeeded = G_TYPE_CHECK_INSTANCE_TYPE((fragment), fragment_interface);
        if (succeeded) {
            /* Check if fragment can handle file format */
            succeeded = mirage_fragment_can_handle_data_format(MIRAGE_FRAGMENT(fragment), filename, NULL);
            if (succeeded) {
                break;
            }
        }
        
        g_object_unref(fragment);
        fragment = NULL;
    }
    
    g_free(fragments);

    if (!succeeded) {
        mirage_error(MIRAGE_E_NOFRAGMENTFOUND, error);
    }
    
    *ret_fragment = fragment;    
    return succeeded;
}


/**
 * mirage_mirage_for_each_parser:
 * @self: a #MIRAGE_Mirage
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
gboolean mirage_mirage_for_each_parser (MIRAGE_Mirage *self, MIRAGE_CallbackFunction func, gpointer user_data, GError **error) {
    GType *parsers = NULL;
    guint num_parsers = 0;
    gint i;
    
    MIRAGE_CHECK_ARG(func);
    
    parsers = g_type_children(MIRAGE_TYPE_DISC, &num_parsers);
    
    for (i = 0; i < num_parsers; i++) {
        MIRAGE_ParserInfo *parser_info = NULL;
        GObject *disc = g_object_new(parsers[i], NULL);
        gboolean succeeded = TRUE;

        mirage_disc_get_parser_info(MIRAGE_DISC(disc), &parser_info, NULL);
        
        succeeded = (*func)(parser_info, user_data);
        
        g_object_unref(disc);
        if (!succeeded) {
            mirage_error(MIRAGE_E_ITERCANCELLED, error);
            return FALSE;
        }
    }
    
    g_free(parsers);
 
    return TRUE;
}

/**
 * mirage_mirage_for_each_fragment:
 * @self: a #MIRAGE_Mirage
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
gboolean mirage_mirage_for_each_fragment (MIRAGE_Mirage *self, MIRAGE_CallbackFunction func, gpointer user_data, GError **error) {
    GType *fragments = NULL;
    guint num_fragments = 0;
    gint i;
    
    MIRAGE_CHECK_ARG(func);
    
    fragments = g_type_children(MIRAGE_TYPE_FRAGMENT, &num_fragments);
    
    for (i = 0; i < num_fragments; i++) {
        MIRAGE_FragmentInfo *fragment_info = NULL;
        GObject *fragment = g_object_new(fragments[i], NULL);
        gboolean succeeded = TRUE;
        
        mirage_fragment_get_fragment_info(MIRAGE_FRAGMENT(fragment), &fragment_info, NULL);
        
        succeeded = (*func)(fragment_info, user_data);
        
        g_object_unref(fragment);
        if (!succeeded) {
            mirage_error(MIRAGE_E_ITERCANCELLED, error);
            return FALSE;
        }
    }
    
    g_free(fragments);
 
    return TRUE;
}


/**
 * mirage_mirage_get_supported_debug_masks:
 * @self: a #MIRAGE_Mirage
 * @masks: location to store masks array
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves array of supported debug masks and stores it in @masks. The array
 * is a @GPtrArray, containing multiple @GValueArray containers, each containing
 * a string and an integer representing debug mask's name and value.
 * </para>
 *
 * <para>
 * Both @GPtrArray array and @GValueArray containers should be freed with 
 * g_ptr_array_free() and g_value_array_free() when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_mirage_get_supported_debug_masks (MIRAGE_Mirage *self, GPtrArray **masks, GError **error) {
    gint i = 0;
    
    static const struct {
        gint mask_value;
        gchar *mask_name;
    } dbg_masks[] = {
        { MIRAGE_DEBUG_GOBJECT, "MIRAGE_DEBUG_GOBJECT" },
        { MIRAGE_DEBUG_CHAIN, "MIRAGE_DEBUG_CHAIN" },
        { MIRAGE_DEBUG_PARSER, "MIRAGE_DEBUG_PARSER" },
        { MIRAGE_DEBUG_DISC, "MIRAGE_DEBUG_DISC" },
        { MIRAGE_DEBUG_SESSION, "MIRAGE_DEBUG_SESSION" },
        { MIRAGE_DEBUG_TRACK, "MIRAGE_DEBUG_TRACK" },
        { MIRAGE_DEBUG_SECTOR, "MIRAGE_DEBUG_SECTOR" },
        { MIRAGE_DEBUG_FRAGMENT, "MIRAGE_DEBUG_FRAGMENT" },
        { MIRAGE_DEBUG_CDTEXT, "MIRAGE_DEBUG_CDTEXT" },
    };
    
    MIRAGE_CHECK_ARG(masks);
    
    *masks = g_ptr_array_new();
    
    for (i = 0; i < G_N_ELEMENTS(dbg_masks); i++) {
        /* Create value array */
        GValueArray *mask_entry = g_value_array_new(2);
        /* Mask name */
        g_value_array_append(mask_entry, NULL);
        g_value_init(g_value_array_get_nth(mask_entry, 0), G_TYPE_STRING);
        g_value_set_string(g_value_array_get_nth(mask_entry, 0), dbg_masks[i].mask_name);
        /* Mask value */
        g_value_array_append(mask_entry, NULL);
        g_value_init(g_value_array_get_nth(mask_entry, 1), G_TYPE_INT);
        g_value_set_int(g_value_array_get_nth(mask_entry, 1), dbg_masks[i].mask_value);
        /* Add mask's value array to masks' pointer array */
        g_ptr_array_add(*masks, mask_entry);
    }
    
    return TRUE;
}


/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static MIRAGE_ObjectClass *parent_class = NULL;

static void __mirage_mirage_instance_init (GTypeInstance *instance, gpointer g_class) {
    MIRAGE_Mirage *self = MIRAGE_MIRAGE(instance);
    MIRAGE_MiragePrivate *_priv = MIRAGE_MIRAGE_GET_PRIVATE(self);
    
    GObject *debug_context = NULL;
    
    /* Set version string */
    _priv->version = g_strdup(PACKAGE_VERSION);
    
    /* Mirage should get a default debug context, so we can catch error messages */
    debug_context = g_object_new(MIRAGE_TYPE_DEBUG_CONTEXT, NULL);
    mirage_object_set_debug_context(MIRAGE_OBJECT(self), debug_context, NULL);
    g_object_unref(debug_context);
    
    /* Load Plug-Ins */
    __mirage_mirage_load_plugins(self, NULL);

    return;
}

static void __mirage_mirage_finalize (GObject *obj) {
    MIRAGE_Mirage *self = MIRAGE_MIRAGE(obj);
    MIRAGE_MiragePrivate *_priv = MIRAGE_MIRAGE_GET_PRIVATE(self);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s:\n", __func__);
    
    /* Free version string */
    g_free(_priv->version);
    
    /* Free plugins list; GTypeModules most not be finalized, don't unref them! */
    g_list_free(_priv->plugins_list);

    /* Chain up to the parent class */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __func__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __mirage_mirage_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_MirageClass *klass = MIRAGE_MIRAGE_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_MiragePrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_mirage_finalize;
    
    return;
}

GType mirage_mirage_get_type (void) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_MirageClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_mirage_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Mirage),
            0,      /* n_preallocs */
            __mirage_mirage_instance_init    /* instance_init */
        };
        
        type = g_type_register_static(MIRAGE_TYPE_OBJECT, "MIRAGE_Mirage", &info, 0);
    }
    
    return type;
}
