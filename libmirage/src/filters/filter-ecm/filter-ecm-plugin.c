/*
 *  libMirage: ECM file filter: Plugin exports
 *  Copyright (C) 2012 Rok Mandeljc
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

#include "filter-ecm.h"

G_MODULE_EXPORT void mirage_plugin_load_plugin (MIRAGE_Plugin *plugin);
G_MODULE_EXPORT void mirage_plugin_unload_plugin (MIRAGE_Plugin *plugin);

G_MODULE_EXPORT guint mirage_plugin_lt_current = MIRAGE_LT_CURRENT;

G_MODULE_EXPORT void mirage_plugin_load_plugin (MIRAGE_Plugin *plugin)
{
    mirage_file_filter_ecm_type_register(G_TYPE_MODULE(plugin));
}

G_MODULE_EXPORT void mirage_plugin_unload_plugin (MIRAGE_Plugin *plugin G_GNUC_UNUSED)
{
}