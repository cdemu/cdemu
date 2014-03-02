/*
 *  libMirage: compatibility input stream
 *  Copyright (C) 2014 Rok Mandeljc
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

#ifndef __MIRAGE_COMPAT_INPUT_STREAM_H__
#define __MIRAGE_COMPAT_INPUT_STREAM_H__

#include <mirage/mirage.h>


G_BEGIN_DECLS


/**********************************************************************\
 *                 MirageCompatInputStream object                     *
\**********************************************************************/
#define MIRAGE_TYPE_COMPAT_INPUT_STREAM            (mirage_compat_input_stream_get_type())
#define MIRAGE_COMPAT_INPUT_STREAM(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_COMPAT_INPUT_STREAM, MirageCompatInputStream))
#define MIRAGE_COMPAT_INPUT_STREAM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_COMPAT_INPUT_STREAM, MirageCompatInputStreamClass))
#define MIRAGE_IS_COMPAT_INPUT_STREAM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_COMPAT_INPUT_STREAM))
#define MIRAGE_IS_COMPAT_INPUT_STREAM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_COMPAT_INPUT_STREAM))
#define MIRAGE_COMPAT_INPUT_STREAM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_COMPAT_INPUT_STREAM, MirageCompatInputStreamClass))

typedef struct _MirageCompatInputStream         MirageCompatInputStream;
typedef struct _MirageCompatInputStreamClass    MirageCompatInputStreamClass;
typedef struct _MirageCompatInputStreamPrivate  MirageCompatInputStreamPrivate;

/**
 * MirageCompatInputStream:
 *
 * All the fields in the <structname>MirageCompatInputStream</structname>
 * structure are private to the #MirageCompatInputStream implementation and
 * should never be accessed directly.
 */
struct _MirageCompatInputStream
{
    GInputStream parent_instance;

    /*< private >*/
    MirageCompatInputStreamPrivate *priv;
};

/**
 * MirageCompatInputStreamClass:
 * @parent_class: the parent class
 *
 * The class structure for the <structname>MirageCompatInputStream</structname> type.
 */
struct _MirageCompatInputStreamClass
{
    GInputStreamClass parent_class;
};

/* Used by MIRAGE_TYPE_COMPAT_INPUT_STREAM */
GType mirage_compat_input_stream_get_type (void);


G_END_DECLS

#endif /* __MIRAGE_COMPAT_INPUT_STREAM_H__ */
