/*
 *  libMirage: Mirage object
 *  Copyright (C) 2006-2008 Rok Mandeljc
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

#ifndef __MIRAGE_MIRAGE_H__
#define __MIRAGE_MIRAGE_H__


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

#define MIRAGE_TYPE_MIRAGE            (mirage_mirage_get_type())
#define MIRAGE_MIRAGE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_MIRAGE, MIRAGE_Mirage))
#define MIRAGE_MIRAGE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_MIRAGE, MIRAGE_MirageClass))
#define MIRAGE_IS_MIRAGE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_MIRAGE))
#define MIRAGE_IS_MIRAGE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_MIRAGE))
#define MIRAGE_MIRAGE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_MIRAGE, MIRAGE_MirageClass))

/**
 * MIRAGE_Mirage:
 *
 * <para>
 * Contains private data only, and should be accessed using the functions below.
 * </para>
 **/
typedef struct {
    MIRAGE_Object parent;
} MIRAGE_Mirage;

typedef struct {
    MIRAGE_ObjectClass parent;
} MIRAGE_MirageClass;

/* Used by MIRAGE_TYPE_MIRAGE */
GType mirage_mirage_get_type (void);

/* Public API */
gboolean mirage_mirage_get_version (MIRAGE_Mirage *self, gchar **version, GError **error);
gboolean mirage_mirage_create_disc (MIRAGE_Mirage *self, gchar **filenames, GObject **ret_disc, GObject *debug_context, GError **error);
gboolean mirage_mirage_create_fragment (MIRAGE_Mirage *self, GType fragment_interface, gchar *filename, GObject **ret_fragment, GError **error);
gboolean mirage_mirage_for_each_parser (MIRAGE_Mirage *self, MIRAGE_CallbackFunction func, gpointer user_data, GError **error);
gboolean mirage_mirage_for_each_fragment (MIRAGE_Mirage *self, MIRAGE_CallbackFunction func, gpointer user_data, GError **error);
gboolean mirage_mirage_get_supported_debug_masks (MIRAGE_Mirage *self, GPtrArray **masks, GError **error);

G_END_DECLS

#endif /* __MIRAGE_MIRAGE_H__ */
