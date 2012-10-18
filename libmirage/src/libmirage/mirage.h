/*
 *  libMirage: Main header
 *  Copyright (C) 2006-2012 Rok Mandeljc
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


#ifndef __MIRAGE_H__
#define __MIRAGE_H__

#include <glib.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#include <gmodule.h>

#include <gio/gio.h>

#include <math.h>
#include <string.h>

#include "mirage-context.h"
#include "mirage-contextual.h"
#include "mirage-object.h"

#include "mirage-cdtext-coder.h"
#include "mirage-debug.h"
#include "mirage-disc.h"
#include "mirage-disc-structures.h"
#include "mirage-error.h"
#include "mirage-file-filter.h"
#include "mirage-fragment.h"
#include "mirage-fragment-iface-audio.h"
#include "mirage-fragment-iface-binary.h"
#include "mirage-index.h"
#include "mirage-language.h"
#include "mirage-parser.h"
#include "mirage-plugin.h"
#include "mirage-sector.h"
#include "mirage-session.h"
#include "mirage-track.h"
#include "mirage-utils.h"
#include "mirage-version.h"

G_BEGIN_DECLS


/**
 * MirageEnumParserInfoCallback:
 * @info: (in): parser info
 * @user_data: (in) (closure): user data passed to enumeration function
 *
 * Callback function type used with mirage_enumerate_parsers().
 * A pointer to parser information structure is stored in @info; the
 * structure belongs to the parser object and should not be modified.
 * @user_data is user data passed to enumeration function.
 *
 * Returns: %TRUE on success, otherwise %FALSE
 */
typedef gboolean (*MirageEnumParserInfoCallback) (const MirageParserInfo *info, gpointer user_data);

/**
 * MirageEnumFragmentInfoCallback:
 * @info: (in): fragment info
 * @user_data: (in) (closure): user data passed to enumeration function
 *
 * Callback function type used with mirage_enumerate_fragments().
 * A pointer to fragment information structure is stored in @info; the
 * structure belongs to the fragment object and should not be modified.
 * @user_data is user data passed to enumeration function.
 *
 * Returns: %TRUE on success, otherwise %FALSE
 */
typedef gboolean (*MirageEnumFragmentInfoCallback) (const MirageFragmentInfo *info, gpointer user_data);

/**
 * MirageEnumFileFilterInfoCallback:
 * @info: (in): file filter info
 * @user_data: (in) (closure): user data passed to enumeration function
 *
 * Callback function type used with mirage_enumerate_file_filters().
 * A pointer to file filter information structure is stored in @info; the
 * structure belongs to the file filter object and should not be modified.
 * @user_data is user data passed to enumeration function.
 *
 * Returns: %TRUE on success, otherwise %FALSE
 */
typedef gboolean (*MirageEnumFileFilterInfoCallback) (const MirageFileFilterInfo *info, gpointer user_data);


/**
 * MirageDebugMask:
 * @name: name
 * @value: value
 *
 * Structure containing debug mask information.
 */
typedef struct {
    gchar *name;
    gint value;
} MirageDebugMask;


/* *** libMirage API *** */
gboolean mirage_initialize (GError **error);
gboolean mirage_shutdown (GError **error);

gboolean mirage_get_parsers_type (GType **types, gint *num_parsers, GError **error);
gboolean mirage_get_parsers_info (const MirageParserInfo **info, gint *num_parsers, GError **error);
gboolean mirage_enumerate_parsers (MirageEnumParserInfoCallback func, gpointer user_data, GError **error);

gboolean mirage_get_fragments_type (GType **types, gint *num_fragments, GError **error);
gboolean mirage_get_fragments_info (const MirageFragmentInfo **info, gint *num_fragments, GError **error);
gboolean mirage_enumerate_fragments (MirageEnumFragmentInfoCallback func, gpointer user_data, GError **error);

gboolean mirage_get_file_filters_type (GType **types, gint *num_file_filters, GError **error);
gboolean mirage_get_file_filters_info (const MirageFileFilterInfo **info, gint *num_file_filters, GError **error);
gboolean mirage_enumerate_file_filters (MirageEnumFileFilterInfoCallback func, gpointer user_data, GError **error);

gboolean mirage_get_supported_debug_masks (const MirageDebugMask **masks, gint *num_masks, GError **error);

G_END_DECLS

#endif /* __MIRAGE_H__ */
