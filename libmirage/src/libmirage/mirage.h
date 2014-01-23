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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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

#include "mirage-types.h"

#include "mirage-object.h"
#include "mirage-stream.h"
#include "mirage-context.h"
#include "mirage-contextual.h"

#include "mirage-cdtext-coder.h"
#include "mirage-debug.h"
#include "mirage-disc.h"
#include "mirage-disc-structures.h"
#include "mirage-error.h"
#include "mirage-file-stream.h"
#include "mirage-filter-stream.h"
#include "mirage-fragment.h"
#include "mirage-index.h"
#include "mirage-language.h"
#include "mirage-parser.h"
#include "mirage-plugin.h"
#include "mirage-sector.h"
#include "mirage-session.h"
#include "mirage-track.h"
#include "mirage-utils.h"
#include "mirage-version.h"
#include "mirage-writer.h"

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
 * MirageEnumFilterStreamInfoCallback:
 * @info: (in): filter stream info
 * @user_data: (in) (closure): user data passed to enumeration function
 *
 * Callback function type used with mirage_enumerate_filter_streams().
 * A pointer to filter stream information structure is stored in @info; the
 * structure belongs to the filter stream object and should not be modified.
 * @user_data is user data passed to enumeration function.
 *
 * Returns: %TRUE on success, otherwise %FALSE
 */
typedef gboolean (*MirageEnumFilterStreamInfoCallback) (const MirageFilterStreamInfo *info, gpointer user_data);


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

gboolean mirage_get_parsers_type (const GType **types, gint *num_parsers, GError **error);
gboolean mirage_get_parsers_info (const MirageParserInfo **info, gint *num_parsers, GError **error);
gboolean mirage_enumerate_parsers (MirageEnumParserInfoCallback func, gpointer user_data, GError **error);

gboolean mirage_get_filter_streams_type (const GType **types, gint *num_filter_streams, GError **error);
gboolean mirage_get_filter_streams_info (const MirageFilterStreamInfo **info, gint *num_filter_streams, GError **error);
gboolean mirage_enumerate_filter_streams (MirageEnumFilterStreamInfoCallback func, gpointer user_data, GError **error);

gboolean mirage_get_supported_debug_masks (const MirageDebugMask **masks, gint *num_masks, GError **error);

G_END_DECLS

#endif /* __MIRAGE_H__ */
