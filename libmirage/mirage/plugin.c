/*
 *  libMirage: plugin
 *  Copyright (C) 2007-2014 Rok Mandeljc
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
 * SECTION: mirage-plugin
 * @title: MiragePlugin
 * @short_description: Plugin object.
 * @see_also: #MirageParser, #MirageWriter, #MirageFilterStream
 * @include: mirage-plugin.h
 *
 * #MiragePlugin object is a base object of libMirage's plugin system
 * and derives from #GTypeModule. It provides support for loadable
 * modules that contain implementations of image parsers, image writers
 * and/or filter streams.
 *
 * The plugin system is used internally by libMirage, and should
 * generally not be used elsewhere.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"

#include <glib/gi18n-lib.h>


/**********************************************************************\
 *                         Private structure                          *
\**********************************************************************/
#define MIRAGE_PLUGIN_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PLUGIN, MiragePluginPrivate))

struct _MiragePluginPrivate
{
    gchar *filename;
    GModule *library;

    void (*mirage_plugin_load_plugin) (MiragePlugin *module);
    void (*mirage_plugin_unload_plugin) (MiragePlugin *module);
} ;

typedef enum
{
    PROPERTY_FILENAME = 1,
} MiragePluginProperties;


/**********************************************************************\
 *                            Public API                              *
\**********************************************************************/
/**
 * mirage_plugin_new:
 * @filename: (in): plugin's filename
 *
 * Creates new plugin.
 *
 * Returns: a new #MiragePlugin object that represents plugin. It should be
 * released with g_object_unref() when no longer needed.
 */
MiragePlugin *mirage_plugin_new (const gchar *filename)
{
    MiragePlugin *plugin = NULL;

    g_return_val_if_fail(filename != NULL, NULL);
    plugin = g_object_new(MIRAGE_TYPE_PLUGIN, "filename", filename, NULL);

    return plugin;
}


/**********************************************************************\
 *                   GTypeModule methods implementations              *
\**********************************************************************/
static gboolean mirage_plugin_load_module (GTypeModule *_self)
{
    MiragePlugin *self = MIRAGE_PLUGIN(_self);
    gint *plugin_soversion_major;
    gint *plugin_soversion_minor;

    if (!self->priv->filename) {
        return FALSE;
    }

    /* Note: we bind locally to avoid potential name clashes, and we
       do *not* do lazy bind, because if there are unresolved symbols
       in the plugin, we want the loading to fail immediately */
    self->priv->library = g_module_open(self->priv->filename, G_MODULE_BIND_LOCAL);
    if (!self->priv->library) {
        return FALSE;
    }

    /* Make sure that the loaded library contains the 'mirage_plugin_soversion_major'
       and 'mirage_plugin_soversion_minor' symbols, which represent the
       ABI version that plugin was built against; make sure it is compatible
       with ABI version provided by the lib */
    if (!g_module_symbol(self->priv->library, "mirage_plugin_soversion_major", (gpointer *)&plugin_soversion_major)) {
        g_warning("%s: plugin %s: does not contain 'mirage_plugin_soversion_major'!\n", __func__, self->priv->filename);
        g_module_close(self->priv->library);
        return FALSE;
    }

    if (!g_module_symbol(self->priv->library, "mirage_plugin_soversion_minor", (gpointer *)&plugin_soversion_minor)) {
        g_warning("%s: plugin %s: does not contain 'mirage_plugin_soversion_minor'!\n", __func__, self->priv->filename);
        g_module_close(self->priv->library);
        return FALSE;
    }

    /* Major versions must match, and plugin's minor version must be less
       or equal to library's minor version */
    if (*plugin_soversion_major != mirage_soversion_major || *plugin_soversion_minor > mirage_soversion_minor) {
        g_warning("%s: plugin %s: is built against incompatible ABI (%d.%d vs. %d.%d)!\n", __func__, self->priv->filename, *plugin_soversion_major, *plugin_soversion_minor, mirage_soversion_major, mirage_soversion_minor);
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
    MiragePlugin *self = MIRAGE_PLUGIN(_self);

    self->priv->mirage_plugin_unload_plugin(self);

    g_module_close(self->priv->library);
    self->priv->library = NULL;

    self->priv->mirage_plugin_load_plugin = NULL;
    self->priv->mirage_plugin_unload_plugin = NULL;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE(MiragePlugin, mirage_plugin, G_TYPE_TYPE_MODULE);


static void mirage_plugin_init (MiragePlugin *self)
{
    self->priv = MIRAGE_PLUGIN_GET_PRIVATE(self);

    self->priv->filename = NULL;
    self->priv->library = NULL;
    self->priv->mirage_plugin_load_plugin = NULL;
    self->priv->mirage_plugin_unload_plugin = NULL;
}

static void mirage_plugin_finalize (GObject *gobject)
{
    MiragePlugin *self = MIRAGE_PLUGIN(gobject);

    g_free(self->priv->filename);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_plugin_parent_class)->finalize(gobject);
}

static void mirage_plugin_get_property (GObject *gobject, guint property_id, GValue *value, GParamSpec *pspec)
{
    MiragePlugin *self = MIRAGE_PLUGIN(gobject);
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
    MiragePlugin *self = MIRAGE_PLUGIN(gobject);
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


static void mirage_plugin_class_init (MiragePluginClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GTypeModuleClass *gmodule_class = G_TYPE_MODULE_CLASS(klass);

    gobject_class->finalize = mirage_plugin_finalize;
    gobject_class->get_property = mirage_plugin_get_property;
    gobject_class->set_property = mirage_plugin_set_property;

    gmodule_class->load = mirage_plugin_load_module;
    gmodule_class->unload = mirage_plugin_unload_module;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MiragePluginPrivate));

    /* Install properties */
    g_object_class_install_property(gobject_class, PROPERTY_FILENAME, g_param_spec_string ("filename", "Filename", "The filename of the module", NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}
