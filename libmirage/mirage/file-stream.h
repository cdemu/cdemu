/*
 *  libMirage: file stream
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

#ifndef __MIRAGE_FILE_STREAM_H__
#define __MIRAGE_FILE_STREAM_H__

#include <mirage/mirage.h>


G_BEGIN_DECLS


/**********************************************************************\
 *                     MirageFileStream object                        *
\**********************************************************************/
#define MIRAGE_TYPE_FILE_STREAM            (mirage_file_stream_get_type())
#define MIRAGE_FILE_STREAM(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FILE_STREAM, MirageFileStream))
#define MIRAGE_FILE_STREAM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_FILE_STREAM, MirageFileStreamClass))
#define MIRAGE_IS_FILE_STREAM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FILE_STREAM))
#define MIRAGE_IS_FILE_STREAM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_FILE_STREAM))
#define MIRAGE_FILE_STREAM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_FILE_STREAM, MirageFileStreamClass))

typedef struct _MirageFileStream         MirageFileStream;
typedef struct _MirageFileStreamClass    MirageFileStreamClass;
typedef struct _MirageFileStreamPrivate  MirageFileStreamPrivate;

/**
 * MirageFileStream:
 *
 * All the fields in the <structname>MirageFileStream</structname>
 * structure are private to the #MirageFileStream implementation and
 * should never be accessed directly.
 */
struct _MirageFileStream
{
    MirageObject parent_instance;

    /*< private >*/
    MirageFileStreamPrivate *priv;
};

/**
 * MirageFileStreamClass:
 * @parent_class: the parent class
 *
 * The class structure for the <structname>MirageFileStream</structname> type.
 */
struct _MirageFileStreamClass
{
    MirageObjectClass parent_class;
};

/* Used by MIRAGE_TYPE_FILE_STREAM */
GType mirage_file_stream_get_type (void);

gboolean mirage_file_stream_open (MirageFileStream *self, const gchar *filename, gboolean writable, GError **error);


G_END_DECLS

#endif /* __MIRAGE_FILE_STREAM_H__ */
