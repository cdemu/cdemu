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

#include "mirage-version.h"

#include "mirage-debug.h"
#include "mirage-error.h"
#include "mirage-plugin.h"

G_BEGIN_DECLS

/**
 * MirageCallbackFunction:
 * @data: (out): data
 * @user_data: (closure): user data passed to iteration function
 *
 * <para>
 * Callback function type used in libMirage's iteration functions. A pointer to
 * data buffer is stored in @data; the buffer usually belongs to the object and
 * therefore should not be modified. @user_data is user data passed to iteration
 * function.
 * </para>
 *
 * Returns: %TRUE on success, otherwise %FALSE
 **/
typedef gboolean (*MirageCallbackFunction) (const gpointer data, gpointer user_data);

/**
 * MiragePasswordFunction:
 * @user_data: (closure): user data passed to password function
 *
 * <para>
 * Password function type used in libMirage's to obtain password for encrypted
 * images. A password function needs to be set to libMirage via
 * mirage_set_password_function(), along with @user_data that the password
 * function should be called with.
 * </para>
 *
 * Returns: password string on success, otherwise %NULL. Password string should
 * be a copy, allocated via function such as g_strdup(), and will be freed after
 * it is used.
 **/
typedef gchar *(*MiragePasswordFunction) (gpointer user_data);

/**
 * MirageDebugMask:
 * @name: name
 * @value: value
 *
 * <para>
 * Structure containing debug mask information.
 * </para>
 **/
typedef struct {
    gchar *name;
    gint value;
} MirageDebugMask;


/* *** libMirage API *** */
gboolean mirage_init (GError **error);
gboolean mirage_shutdown (GError **error);

gboolean mirage_set_password_function (MiragePasswordFunction func, gpointer user_data, GError **error);
gchar *mirage_obtain_password (GError **error);

GObject *mirage_create_disc (gchar **filenames, GObject *debug_context, GHashTable *params, GError **error);
GObject *mirage_create_fragment (GType fragment_interface, GObject *stream, GObject *debug_context, GError **error);
GObject *mirage_create_file_stream (const gchar *filename, GObject *debug_context, GError **error);

gboolean mirage_for_each_parser (MirageCallbackFunction func, gpointer user_data, GError **error);
gboolean mirage_for_each_fragment (MirageCallbackFunction func, gpointer user_data, GError **error);
gboolean mirage_for_each_file_filter (MirageCallbackFunction func, gpointer user_data, GError **error);

gboolean mirage_get_supported_debug_masks (const MirageDebugMask **masks, gint *num_masks, GError **error);

G_END_DECLS

#include "mirage-object.h"

#include "mirage-file-filter.h"
#include "mirage-parser.h"
#include "mirage-disc.h"
#include "mirage-fragment.h"
#include "mirage-index.h"
#include "mirage-language.h"
#include "mirage-session.h"
#include "mirage-track.h"
#include "mirage-sector.h"

#include "mirage-disc-structures.h"

#include "mirage-cdtext-coder.h"

#include "mirage-utils.h"

#endif /* __MIRAGE_H__ */
