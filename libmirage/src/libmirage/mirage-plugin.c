/*
 *  libMirage: Plugin object
 *  Copyright (C) 2007-2008 Rok Mandeljc
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
#define MIRAGE_PLUGIN_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PLUGIN, MIRAGE_PluginPrivate))

typedef struct {    
    gchar *filename;
    GModule *library;
    
    void (*mirage_plugin_load_plugin) (MIRAGE_Plugin *module);
    void (*mirage_plugin_unload_plugin) (MIRAGE_Plugin *module);
} MIRAGE_PluginPrivate;

typedef enum {
    PROPERTY_FILENAME = 1,
} MIRAGE_PluginProperties;

/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
/**
 * mirage_plugin_new:
 * @filename: plugin's filename
 *
 * <para>
 * Creates new plugin.
 * </para>
 *
 * Returns: a new #MIRAGE_Plugin object that represents plugin. It should be 
 * released with g_object_unref() when no longer needed.
 **/
MIRAGE_Plugin *mirage_plugin_new (const gchar *filename) {
    MIRAGE_Plugin *plugin = NULL;
    
    g_return_val_if_fail(filename != NULL, NULL);
    plugin = g_object_new(MIRAGE_TYPE_PLUGIN, "filename", filename, NULL);
    
    return plugin;
}

/******************************************************************************\
 *                          GTypeModule methods implementations               *
\******************************************************************************/
static gboolean __mirage_plugin_load_module (GTypeModule *gmodule) {
    MIRAGE_Plugin *self = MIRAGE_PLUGIN(gmodule);
    MIRAGE_PluginPrivate *_priv = MIRAGE_PLUGIN_GET_PRIVATE(self);
    gint *plugin_lt_current;
    
    if (!_priv->filename) {
        return FALSE;
    }
    
    _priv->library = g_module_open(_priv->filename, G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
    
    if (!_priv->library) {
        return FALSE;
    }
    
    /* Make sure that the loaded library contains the 'mirage_plugin_lt_current'
       symbol which represents the ABI version that plugin was built against; make
       sure it matches ABI used by the lib */
    if (!g_module_symbol(_priv->library, "mirage_plugin_lt_current", (gpointer *)&plugin_lt_current)) {
        g_warning("%s: plugin %s: does not contain 'mirage_plugin_lt_current'!\n", __func__, _priv->filename);
        g_module_close(_priv->library);
        return FALSE;
    }
    
    if (*plugin_lt_current != MIRAGE_LT_CURRENT) {
        g_warning("%s: plugin %s: is not built against current ABI (%d vs. %d)!\n", __func__, _priv->filename, *plugin_lt_current, MIRAGE_LT_CURRENT);
        g_module_close(_priv->library);
        return FALSE;
    }
    
    /* Make sure that the loaded library contains the required methods */
    if (!g_module_symbol(_priv->library, "mirage_plugin_load_plugin", (gpointer *)&_priv->mirage_plugin_load_plugin) ||
        !g_module_symbol(_priv->library, "mirage_plugin_unload_plugin", (gpointer *)&_priv->mirage_plugin_unload_plugin)) {
        
        g_module_close(_priv->library);
        return FALSE;
    }
    
    /* Initialize the loaded module */
    _priv->mirage_plugin_load_plugin(self);
    
    return TRUE;
}

static void __mirage_plugin_unload_module (GTypeModule *gmodule) {
    MIRAGE_Plugin *self = MIRAGE_PLUGIN(gmodule);
    MIRAGE_PluginPrivate *_priv = MIRAGE_PLUGIN_GET_PRIVATE(self);
    
    _priv->mirage_plugin_unload_plugin(self);
    
    g_module_close(_priv->library);
    _priv->library = NULL;
    
    _priv->mirage_plugin_load_plugin = NULL;
    _priv->mirage_plugin_unload_plugin = NULL;
    
    return;
}


/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static GTypeModuleClass *parent_class = NULL;

static void __mirage_plugin_get_property (GObject *obj, guint param_id, GValue *value, GParamSpec *pspec) {
    MIRAGE_Plugin *self = MIRAGE_PLUGIN(obj);
    MIRAGE_PluginPrivate *_priv = MIRAGE_PLUGIN_GET_PRIVATE(self);
    
    switch (param_id) {
        case PROPERTY_FILENAME: {
            g_value_set_string(value, _priv->filename);
            break;
        }
        default: {
            G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, param_id, pspec);
            break;
        }
    }
    
    return;
}

static void __mirage_plugin_set_property (GObject *obj, guint param_id, const GValue *value, GParamSpec *pspec) {
    MIRAGE_Plugin *self = MIRAGE_PLUGIN(obj);
    MIRAGE_PluginPrivate *_priv = MIRAGE_PLUGIN_GET_PRIVATE(self);
    
    switch (param_id) {
        case PROPERTY_FILENAME: {
            g_free(_priv->filename);
            _priv->filename = g_value_dup_string(value);
            break;
        } 
        default: {
            G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, param_id, pspec);
            break;
        }
    }
    
    return;
}


static void __mirage_plugin_finalize (GObject *obj) {
    MIRAGE_Plugin *self = MIRAGE_PLUGIN(obj);
    MIRAGE_PluginPrivate *_priv = MIRAGE_PLUGIN_GET_PRIVATE(self);
        
    g_free(_priv->filename);
    
    /* Chain up to the parent class */
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __mirage_plugin_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    GTypeModuleClass *class_gmodule = G_TYPE_MODULE_CLASS(g_class);
    MIRAGE_PluginClass *klass = MIRAGE_PLUGIN_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_PluginPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_plugin_finalize;
    class_gobject->get_property = __mirage_plugin_get_property;
    class_gobject->set_property = __mirage_plugin_set_property;

    /* Initialize GTypeModule methods */
    class_gmodule->load = __mirage_plugin_load_module;
    class_gmodule->unload = __mirage_plugin_unload_module;

    /* Install properties */
    g_object_class_install_property(class_gobject, PROPERTY_FILENAME, g_param_spec_string ("filename", "Filename", "The filename of the module", NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    
    return;
}

GType mirage_plugin_get_type (void) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_PluginClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_plugin_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Plugin),
            0,      /* n_preallocs */
            NULL    /* instance_init */
        };
        
        type = g_type_register_static(G_TYPE_TYPE_MODULE, "MIRAGE_Plugin", &info, 0);
    }
    
    return type;
}
