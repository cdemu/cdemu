/*
 *  libMirage: Filter stream object
 *  Copyright (C) 2012-2014 Rok Mandeljc
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

#ifndef __MIRAGE_FILTER_STREAM_H__
#define __MIRAGE_FILTER_STREAM_H__

#include "mirage.h"


G_BEGIN_DECLS

/**
 * MirageFilterStreamInfo:
 * @id: filter stream ID
 * @name: filter stream name
 * @writable: whether filter stream supports write operation
 * @description: (array zero-terminated=1): zero-terminated array of file type description strings
 * @mime_type: (array zero-terminated=1): zero-terminated array of file type MIME strings
 *
 * A structure containing filter stream information. It can be obtained
 * with call to mirage_filter_stream_get_info().
 */
typedef struct _MirageFilterStreamInfo MirageFilterStreamInfo;
struct _MirageFilterStreamInfo
{
    gchar *id;
    gchar *name;
    gboolean writable;
    gchar **description;
    gchar **mime_type;
};

void mirage_filter_stream_info_copy (const MirageFilterStreamInfo *info, MirageFilterStreamInfo *dest);
void mirage_filter_stream_info_free (MirageFilterStreamInfo *info);


/**********************************************************************\
 *                    MirageFilterStream object                       *
\**********************************************************************/
#define MIRAGE_TYPE_FILTER_STREAM            (mirage_filter_stream_get_type())
#define MIRAGE_FILTER_STREAM(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FILTER_STREAM, MirageFilterStream))
#define MIRAGE_FILTER_STREAM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_FILTER_STREAM, MirageFilterStreamClass))
#define MIRAGE_IS_FILTER_STREAM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FILTER_STREAM))
#define MIRAGE_IS_FILTER_STREAM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_FILTER_STREAM))
#define MIRAGE_FILTER_STREAM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_FILTER_STREAM, MirageFilterStreamClass))

typedef struct _MirageFilterStream         MirageFilterStream;
typedef struct _MirageFilterStreamClass    MirageFilterStreamClass;
typedef struct _MirageFilterStreamPrivate  MirageFilterStreamPrivate;

/**
 * MirageFilterStream:
 *
 * All the fields in the <structname>MirageFilterStream</structname>
 * structure are private to the #MirageFilterStream implementation and
 * should never be accessed directly.
 */
struct _MirageFilterStream
{
    MirageObject parent_instance;

    /*< private >*/
    MirageFilterStreamPrivate *priv;
};

/**
 * MirageFilterStreamClass:
 * @parent_class: the parent class
 * @open: opens a filter stream on top underyling stream
 * @read: reads data from stream
 * @write: wrties data to stream
 * @tell: tells the current location within stream
 * @seek: seeks to a location within stream
 * @partial_read: reads a chunk of requested data from stream
 *
 * The class structure for the <structname>MirageFilterStream</structname> type.
 */
struct _MirageFilterStreamClass
{
    MirageObjectClass parent_class;

    /* Class members */
    gboolean (*open) (MirageFilterStream *self, MirageStream *stream, GError **error);

    /* Functions implemented for MirageStream: */
    gssize (*read) (MirageFilterStream *self, void *buffer, gsize count, GError **error);
    gssize (*write) (MirageFilterStream *self, const void *buffer, gsize count, GError **error);
    gboolean (*seek) (MirageFilterStream *self, goffset offset, GSeekType type, GError **error);
    goffset (*tell) (MirageFilterStream *self);

    /* Simplified read interface */
    gssize (*partial_read) (MirageFilterStream *self, void *buffer, gsize count);
};

/* Used by MIRAGE_TYPE_FILTER_STREAM */
GType mirage_filter_stream_get_type (void);

void mirage_filter_stream_generate_info (MirageFilterStream *self, const gchar *id, const gchar *name, gboolean writable, gint num_types, ...);
const MirageFilterStreamInfo *mirage_filter_stream_get_info (MirageFilterStream *self);

MirageStream *mirage_filter_stream_get_underlying_stream (MirageFilterStream *self);

gboolean mirage_filter_stream_open (MirageFilterStream *self, MirageStream *stream, GError **error);

void mirage_filter_stream_set_stream_length (MirageFilterStream *self, gsize length);
goffset mirage_filter_stream_get_position (MirageFilterStream *self);


G_END_DECLS

#endif /* __MIRAGE_FILTER_STREAM_H__ */
