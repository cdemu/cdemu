/*
 *  libMirage: object
 *  Copyright (C) 2006-2014 Rok Mandeljc
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

#ifndef __MIRAGE_OBJECT_H__
#define __MIRAGE_OBJECT_H__

#include "mirage-types.h"


G_BEGIN_DECLS


/**********************************************************************\
 *                          MirageObject object                       *
\**********************************************************************/
#define MIRAGE_TYPE_OBJECT            (mirage_object_get_type())
#define MIRAGE_OBJECT(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_OBJECT, MirageObject))
#define MIRAGE_OBJECT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_OBJECT, MirageObjectClass))
#define MIRAGE_IS_OBJECT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_OBJECT))
#define MIRAGE_IS_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_OBJECT))
#define MIRAGE_OBJECT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_OBJECT, MirageObjectClass))

typedef struct _MirageObject           MirageObject;
typedef struct _MirageObjectClass      MirageObjectClass;
typedef struct _MirageObjectPrivate    MirageObjectPrivate;


/**
 * MirageObject:
 *
 * All the fields in the <structname>MirageObject</structname>
 * structure are private to the #MirageObject implementation and
 * should never be accessed directly.
 */
struct _MirageObject
{
    GObject parent_instance;

    /*< private >*/
    MirageObjectPrivate *priv;
};

/**
 * MirageObjectClass:
 * @parent_class: the parent class
 *
 * The class structure for the <structname>MirageObject</structname> type.
 */
struct _MirageObjectClass
{
    GObjectClass parent_class;
};

/* Used by MIRAGE_TYPE_OBJECT */
GType mirage_object_get_type (void);

void mirage_object_set_parent (MirageObject *self, gpointer parent);
gpointer mirage_object_get_parent (MirageObject *self);

G_END_DECLS

#endif /* __MIRAGE_OBJECT_H__ */
