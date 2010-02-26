/*
 *  libMirage: Base object
 *  Copyright (C) 2006-2010 Rok Mandeljc
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
 
#ifndef __MIRAGE_OBJECT_H__
#define __MIRAGE_OBJECT_H__


G_BEGIN_DECLS

#define MIRAGE_TYPE_OBJECT            (mirage_object_get_type())
#define MIRAGE_OBJECT(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_OBJECT, MIRAGE_Object))
#define MIRAGE_OBJECT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_OBJECT, MIRAGE_ObjectClass))
#define MIRAGE_IS_OBJECT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_OBJECT))
#define MIRAGE_IS_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_OBJECT))
#define MIRAGE_OBJECT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_OBJECT, MIRAGE_ObjectClass))

/**
 * MIRAGE_Object:
 *
 * <para>
 * Contains private data only, and should be accessed using the functions below.
 * </para>
 **/
typedef struct {
    GObject parent;
} MIRAGE_Object;

typedef struct {
    GObjectClass parent;
    
    /* Class members */
    gint signal_object_modified;
} MIRAGE_ObjectClass;

/* Used by MIRAGE_TYPE_OBJECT */
GType mirage_object_get_type (void);


/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
gboolean mirage_object_set_debug_context (MIRAGE_Object *self, GObject *debug_context, GError **error);
gboolean mirage_object_get_debug_context (MIRAGE_Object *self, GObject **debug_context, GError **error);

void mirage_object_debug_message (MIRAGE_Object *self, gint level, gchar *format, ...);
void mirage_object_debug_messagev (MIRAGE_Object *self, gint level, gchar *format, va_list args);

gboolean mirage_object_set_parent (MIRAGE_Object *self, GObject *parent, GError **error);
gboolean mirage_object_get_parent (MIRAGE_Object *self, GObject **parent, GError **error);

gboolean mirage_object_attach_child (MIRAGE_Object *self, GObject *child, GError **error);
gboolean mirage_object_detach_child (MIRAGE_Object *self, GObject *child, GError **error);

G_END_DECLS

#endif /* __MIRAGE_OBJECT_H__ */
