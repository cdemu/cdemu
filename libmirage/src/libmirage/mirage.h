/*
 *  libMirage: Main header
 *  Copyright (C) 2006-2009 Rok Mandeljc
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

#include <math.h>
#include <string.h>

#include "mirage-version.h"

#include "mirage-types.h"
#include "mirage-debug.h"
#include "mirage-error.h"
#include "mirage-plugin.h"

G_BEGIN_DECLS

/**
 * MIRAGE_CallbackFunction:
 * @data: data
 * @user_data: user data passed to iteration function
 *
 * <para>
 * Callback function type used in libMirage's iteration functions. @data is data 
 * that the iteration function iterates for. Depending on the iteration function, 
 * it may need to be freed or released. @user_data is user data passed to iteration
 * function.
 * </para>
 *
 * Returns: %TRUE on success, otherwise %FALSE
 **/
typedef gboolean (*MIRAGE_CallbackFunction) (gpointer data, gpointer user_data);

/**
 * MIRAGE_PasswordFunction:
 * @user_data: user data passed to password function
 *
 * <para>
 * Password function type used in libMirage's to obtain password for encrypted
 * images. A password function needs to be set to libMirage via 
 * libmirage_set_password_function(), along with @user_data that the password 
 * function should be called with.
 * </para>
 *
 * Returns: password string on success, otherwise %NULL. Password string should
 * be a copy, allocated via function such as g_strdup(), and will be freed after
 * it is used.
 **/
typedef gchar *(*MIRAGE_PasswordFunction) (gpointer user_data);

/**
 * MIRAGE_DebugMask:
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
} MIRAGE_DebugMask;


/* *** libMirage API *** */
gboolean libmirage_init (GError **error);
gboolean libmirage_shutdown (GError **error);

gboolean libmirage_set_password_function (MIRAGE_PasswordFunction func, gpointer user_data, GError **error);
gchar *libmirage_obtain_password (GError **error);

GObject *libmirage_create_disc (gchar **filenames, GObject *debug_context, GHashTable *params, GError **error);
GObject *libmirage_create_fragment (GType fragment_interface, gchar *filename, GError **error);

gboolean libmirage_for_each_parser (MIRAGE_CallbackFunction func, gpointer user_data, GError **error);
gboolean libmirage_for_each_fragment (MIRAGE_CallbackFunction func, gpointer user_data, GError **error);

gboolean libmirage_get_supported_debug_masks (const MIRAGE_DebugMask **masks, gint *num_masks, GError **error);

G_END_DECLS

#include "mirage-object.h"

#include "mirage-parser.h"
#include "mirage-disc.h"
#include "mirage-fragment.h"
#include "mirage-index.h"
#include "mirage-language.h"
#include "mirage-session.h"
#include "mirage-track.h"
#include "mirage-sector.h"

#include "mirage-disc-structures.h"

#include "mirage-cdtext-encdec.h"

#include "mirage-utils.h"

#endif /* __MIRAGE_H__ */
