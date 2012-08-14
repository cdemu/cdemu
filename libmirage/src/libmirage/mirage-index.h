/*
 *  libMirage: Index object
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

#ifndef __MIRAGE_INDEX_H__
#define __MIRAGE_INDEX_H__


G_BEGIN_DECLS

#define MIRAGE_TYPE_INDEX            (mirage_index_get_type())
#define MIRAGE_INDEX(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_INDEX, MIRAGE_Index))
#define MIRAGE_INDEX_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_INDEX, MIRAGE_IndexClass))
#define MIRAGE_IS_INDEX(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_INDEX))
#define MIRAGE_IS_INDEX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_INDEX))
#define MIRAGE_INDEX_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_INDEX, MIRAGE_IndexClass))

typedef struct _MIRAGE_Index        MIRAGE_Index;
typedef struct _MIRAGE_IndexClass   MIRAGE_IndexClass;
typedef struct _MIRAGE_IndexPrivate MIRAGE_IndexPrivate;

/**
 * MIRAGE_Index:
 *
 * <para>
 * Contains private data only, and should be accessed using the functions below.
 * </para>
 **/
struct _MIRAGE_Index
{
    MIRAGE_Object parent_instance;

    /*< private >*/
    MIRAGE_IndexPrivate *priv;
};

struct _MIRAGE_IndexClass
{
    MIRAGE_ObjectClass parent_class;
};

/* Used by MIRAGE_TYPE_INDEX */
GType mirage_index_get_type (void);


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
void mirage_index_set_number (MIRAGE_Index *self, gint number);
gint mirage_index_get_number (MIRAGE_Index *self);

void mirage_index_set_address (MIRAGE_Index *self, gint address);
gint mirage_index_get_address (MIRAGE_Index *self);

G_END_DECLS

#endif /* __MIRAGE_INDEX_H__ */
