/*
 *  Image Analyzer: Disc tree dump
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <mirage.h>

#include "disc-tree-dump.h"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define IA_DISC_TREE_DUMP_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), IA_TYPE_DISC_TREE_DUMP, IaDiscTreeDumpPrivate))

struct _IaDiscTreeDumpPrivate
{
    GtkTreeStore *treestore;
};


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE(IaDiscTreeDump, ia_disc_tree_dump, G_TYPE_OBJECT);

static void ia_disc_tree_dump_init (IaDiscTreeDump *self)
{
    self->priv = IA_DISC_TREE_DUMP_GET_PRIVATE(self);
}

static void ia_disc_tree_dump_class_init (IaDiscTreeDumpClass *klass)
{
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(IaDiscTreeDumpPrivate));
}
