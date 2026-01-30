/*
 *  libMirage: ISO image: plugin exports
 *  Copyright (C) 2006-2026 Rok Mandeljc
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

#include "image-iso.h"

G_MODULE_EXPORT void mirage_plugin_load_plugin (MiragePlugin *plugin);
G_MODULE_EXPORT void mirage_plugin_unload_plugin (MiragePlugin *plugin);

G_MODULE_EXPORT guint mirage_plugin_soversion_major = MIRAGE_SOVERSION_MAJOR;
G_MODULE_EXPORT guint mirage_plugin_soversion_minor = MIRAGE_SOVERSION_MINOR;

G_MODULE_EXPORT void mirage_plugin_load_plugin (MiragePlugin *plugin)
{
    mirage_parser_iso_type_register(G_TYPE_MODULE(plugin));
    mirage_writer_iso_type_register(G_TYPE_MODULE(plugin));
}

G_MODULE_EXPORT void mirage_plugin_unload_plugin (MiragePlugin *plugin G_GNUC_UNUSED)
{
}
