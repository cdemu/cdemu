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

#ifndef __MIRAGE_PLUGIN_H__
#define __MIRAGE_PLUGIN_H__


G_BEGIN_DECLS


/**********************************************************************\
 *                          MiragePlugin object                       *
\**********************************************************************/
#define MIRAGE_TYPE_PLUGIN            (mirage_plugin_get_type())
#define MIRAGE_PLUGIN(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_PLUGIN, MiragePlugin))
#define MIRAGE_PLUGIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_PLUGIN, MiragePluginClass))
#define MIRAGE_IS_PLUGIN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_PLUGIN))
#define MIRAGE_IS_PLUGIN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_PLUGIN))
#define MIRAGE_PLUGIN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_PLUGIN, MiragePluginClass))

typedef struct _MiragePlugin           MiragePlugin;
typedef struct _MiragePluginClass      MiragePluginClass;
typedef struct _MiragePluginPrivate    MiragePluginPrivate;

/**
 * MiragePlugin:
 *
 * <para>
 * Contains private data only, and should be accessed using the functions below.
 * </para>
 **/
struct _MiragePlugin
{
    GTypeModule parent_instance;

    /*< private >*/
    MiragePluginPrivate *priv;
};

struct _MiragePluginClass
{
    GTypeModuleClass parent_class;
};

/* Used by MIRAGE_TYPE_PLUGIN */
GType mirage_plugin_get_type (void);

MiragePlugin *mirage_plugin_new (const gchar *filename);

G_END_DECLS

#endif /* __MIRAGE_PLUGIN_H__ */
