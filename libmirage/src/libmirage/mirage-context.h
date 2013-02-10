/*
 *  libMirage: Context object
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

#ifndef __MIRAGE_CONTEXT_H__
#define __MIRAGE_CONTEXT_H__

#include "mirage-types.h"


G_BEGIN_DECLS


/**
 * MiragePasswordFunction:
 * @user_data: (in) (closure): user data passed to password function
 *
 * Password function type used to obtain password for encrypted
 * images. A password function needs to be set to #MirageContext via
 * mirage_context_set_password_function(), along with @user_data that
 * the password function should be called with.
 *
 * Returns: password string on success, otherwise %NULL. Password string should
 * be a copy, allocated via function such as g_strdup(), and will be freed after
 * it is used.
 */
typedef gchar *(*MiragePasswordFunction) (gpointer user_data);


/**********************************************************************\
 *                        MirageContext object                        *
\**********************************************************************/
#define MIRAGE_TYPE_CONTEXT            (mirage_context_get_type())
#define MIRAGE_CONTEXT(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_CONTEXT, MirageContext))
#define MIRAGE_CONTEXT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_CONTEXT, MirageContextClass))
#define MIRAGE_IS_CONTEXT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_CONTEXT))
#define MIRAGE_IS_CONTEXT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_CONTEXT))
#define MIRAGE_CONTEXT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_CONTEXT, MirageContextClass))

typedef struct _MirageContext         MirageContext;
typedef struct _MirageContextClass    MirageContextClass;
typedef struct _MirageContextPrivate  MirageContextPrivate;

/**
 * MirageContext:
 *
 * All the fields in the <structname>MirageContext</structname>
 * structure are private to the #MirageContext implementation and
 * should never be accessed directly.
 */
struct _MirageContext
{
    GObject parent_instance;

    /*< private >*/
    MirageContextPrivate *priv;
};

/**
 * MirageContextClass:
 * @parent_class: the parent class
 *
 * The class structure for the <structname>MirageContext</structname> type.
 */
struct _MirageContextClass
{
    GObjectClass parent_class;
};

/* Used by MIRAGE_TYPE_CONTEXT */
GType mirage_context_get_type (void);

void mirage_context_set_debug_mask (MirageContext *self, gint debug_mask);
gint mirage_context_get_debug_mask (MirageContext *self);

void mirage_context_set_debug_domain (MirageContext *self, const gchar *domain);
const gchar *mirage_context_get_debug_domain (MirageContext *self);

void mirage_context_set_debug_name (MirageContext *self, const gchar *name);
const gchar *mirage_context_get_debug_name (MirageContext *self);

void mirage_context_clear_options (MirageContext *self);
void mirage_context_set_option (MirageContext *self, const gchar *name, GVariant *value);
GVariant *mirage_context_get_option (MirageContext *self, const gchar *name);

void mirage_context_set_password_function (MirageContext *self, MiragePasswordFunction func, gpointer user_data);
gchar *mirage_context_obtain_password (MirageContext *self, GError **error);

MirageDisc *mirage_context_load_image (MirageContext *self, gchar **filenames, GError **error);

GInputStream *mirage_context_create_file_stream (MirageContext *self, const gchar *filename, GError **error);
const gchar *mirage_context_get_file_stream_filename (MirageContext *self, GInputStream *stream);

G_END_DECLS

#endif /* __MIRAGE_CONTEXT_H__ */
