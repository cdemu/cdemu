/*
 *  libMirage: Plugin object
 *  Copyright (C) 2007-2012 Rok Mandeljc
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


/**********************************************************************\
 *                         Private structure                          *
\**********************************************************************/
#define MIRAGE_PLUGIN_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PLUGIN, MIRAGE_PluginPrivate))

struct _MIRAGE_PluginPrivate
{    
    gchar *filename;
    GModule *library;
    
    void (*mirage_plugin_load_plugin) (MIRAGE_Plugin *module);
    void (*mirage_plugin_unload_plugin) (MIRAGE_Plugin *module);
} ;

typedef enum
{
    PROPERTY_FILENAME = 1,
} MIRAGE_PluginProperties;


/**********************************************************************\
 *                            Public API                              *
\**********************************************************************/
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
MIRAGE_Plugin *mirage_plugin_new (const gchar *filename)
{
    MIRAGE_Plugin *plugin = NULL;
    
    g_return_val_if_fail(filename != NULL, NULL);
    plugin = g_object_new(MIRAGE_TYPE_PLUGIN, "filename", filename, NULL);
    
    return plugin;
}


/**********************************************************************\
 *                   GTypeModule methods implementations              *
\**********************************************************************/
static gboolean mirage_plugin_load_module (GTypeModule *_self)
{
    MIRAGE_Plugin *self = MIRAGE_PLUGIN(_self);
    gint *plugin_lt_current;
    
    if (!self->priv->filename) {
        return FALSE;
    }
    
    self->priv->library = g_module_open(self->priv->filename, G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
    if (!self->priv->library) {
        return FALSE;
    }
    
    /* Make sure that the loaded library contains the 'mirage_plugin_lt_current'
       symbol which represents the ABI version that plugin was built against; make
       sure it matches ABI used by the lib */
    if (!g_module_symbol(self->priv->library, "mirage_plugin_lt_current", (gpointer *)&plugin_lt_current)) {
        g_warning("%s: plugin %s: does not contain 'mirage_plugin_lt_current'!\n", __func__, self->priv->filename);
        g_module_close(self->priv->library);
        return FALSE;
    }
    
    if (*plugin_lt_current != mirage_lt_current) {
        g_warning("%s: plugin %s: is not built against current ABI (%d vs. %d)!\n", __func__, self->priv->filename, *plugin_lt_current, MIRAGE_LT_CURRENT);
        g_module_close(self->priv->library);
        return FALSE;
    }
    
    /* Make sure that the loaded library contains the required methods */
    if (!g_module_symbol(self->priv->library, "mirage_plugin_load_plugin", (gpointer *)&self->priv->mirage_plugin_load_plugin) ||
        !g_module_symbol(self->priv->library, "mirage_plugin_unload_plugin", (gpointer *)&self->priv->mirage_plugin_unload_plugin)) {
        
        g_module_close(self->priv->library);
        return FALSE;
    }
    
    /* Initialize the loaded module */
    self->priv->mirage_plugin_load_plugin(self);
    
    return TRUE;
}

static void mirage_plugin_unload_module (GTypeModule *_self)
{
    MIRAGE_Plugin *self = MIRAGE_PLUGIN(_self);
    
    self->priv->mirage_plugin_unload_plugin(self);
    
    g_module_close(self->priv->library);
    self->priv->library = NULL;

    self->priv->mirage_plugin_load_plugin = NULL;
    self->priv->mirage_plugin_unload_plugin = NULL;
}


/**********************************************************************\
 *                             Object init                            * 
\**********************************************************************/
G_DEFINE_TYPE(MIRAGE_Plugin, mirage_plugin, G_TYPE_TYPE_MODULE);


static void mirage_plugin_init (MIRAGE_Plugin *self)
{
    self->priv = MIRAGE_PLUGIN_GET_PRIVATE(self);

    self->priv->filename = NULL;
    self->priv->library = NULL;
    self->priv->mirage_plugin_load_plugin = NULL;
    self->priv->mirage_plugin_unload_plugin = NULL;
}

static void mirage_plugin_finalize (GObject *gobject)
{
    MIRAGE_Plugin *self = MIRAGE_PLUGIN(gobject);

    g_free(self->priv->filename);
    
    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_plugin_parent_class)->finalize(gobject);
}

static void mirage_plugin_get_property (GObject *gobject, guint property_id, GValue *value, GParamSpec *pspec)
{
    MIRAGE_Plugin *self = MIRAGE_PLUGIN(gobject);
    switch (property_id) {
        case PROPERTY_FILENAME: {
            g_value_set_string(value, self->priv->filename);
            break;
        }
        default: {
            /* We don't have any other property... */
            G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, property_id, pspec);
            break;
        }
    }
}

static void mirage_plugin_set_property (GObject *gobject, guint property_id, const GValue *value, GParamSpec *pspec)
{
    MIRAGE_Plugin *self = MIRAGE_PLUGIN(gobject);
    switch (property_id) {
        case PROPERTY_FILENAME: {
            g_free(self->priv->filename);
            self->priv->filename = g_value_dup_string(value);
            break;
        }
        default: {
            /* We don't have any other property... */
            G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, property_id, pspec);
            break;
        }
    }
}


static void mirage_plugin_class_init (MIRAGE_PluginClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GTypeModuleClass *gmodule_class = G_TYPE_MODULE_CLASS(klass);

    gobject_class->finalize = mirage_plugin_finalize;
    gobject_class->get_property = mirage_plugin_get_property;
    gobject_class->set_property = mirage_plugin_set_property;

    gmodule_class->load = mirage_plugin_load_module;
    gmodule_class->unload = mirage_plugin_unload_module;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_PluginPrivate));

    /* Install properties */
    g_object_class_install_property(gobject_class, PROPERTY_FILENAME, g_param_spec_string ("filename", "Filename", "The filename of the module", NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}
