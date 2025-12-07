/*
 *  libMirage: stream interface
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

#pragma once

#include <mirage/types.h>

G_BEGIN_DECLS


/**********************************************************************\
 *                       MirageStream interface                       *
\**********************************************************************/
#define MIRAGE_TYPE_STREAM                 (mirage_stream_get_type())
#define MIRAGE_STREAM(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_STREAM, MirageStream))
#define MIRAGE_IS_STREAM(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_STREAM))
#define MIRAGE_STREAM_GET_INTERFACE(inst)  (G_TYPE_INSTANCE_GET_INTERFACE((inst), MIRAGE_TYPE_STREAM, MirageStreamInterface))

/**
 * MirageStream:
 *
 * A stream object.
 */
typedef struct _MirageStream             MirageStream;
typedef struct _MirageStreamInterface    MirageStreamInterface;

/**
 * MirageStreamInterface:
 * @parent_iface: the parent interface
 * @get_filename: retrieves the filename of the underlying file
 * @is_writable: determines whether the stream (chain) is writable
 * @move_file: moves the underlying file
 * @read: reads from stream
 * @write: writes to stream
 * @seek: seeks to specified position in stream
 * @tell: retrieves current position in stream
 *
 * Provides an interface for implementing I/O streams.
 */
struct _MirageStreamInterface
{
    GTypeInterface parent_iface;

    /* Interface methods */
    const gchar *(*get_filename) (MirageStream *self);
    gboolean (*is_writable) (MirageStream *self);

    gboolean (*move_file) (MirageStream *self, const gchar *new_filename, GError **error);

    gssize (*read) (MirageStream *self, void *buffer, gsize count, GError **error);
    gssize (*write) (MirageStream *self, const void *buffer, gsize count, GError **error);
    gboolean (*seek) (MirageStream *self, goffset offset, GSeekType type, GError **error);
    goffset (*tell) (MirageStream *self);
};

/* Used by MIRAGE_TYPE_STREAM */
GType mirage_stream_get_type (void);


const gchar *mirage_stream_get_filename (MirageStream *self);
gboolean mirage_stream_is_writable (MirageStream *self);

gssize mirage_stream_read (MirageStream *self, void *buffer, gsize count, GError **error);
gssize mirage_stream_write (MirageStream *self, const void *buffer, gsize count, GError **error);
gboolean mirage_stream_seek (MirageStream *self, goffset offset, GSeekType type, GError **error);
goffset mirage_stream_tell (MirageStream *self);

gboolean mirage_stream_move_file (MirageStream *self, const gchar *new_filename, GError **error);

GInputStream *mirage_stream_get_g_input_stream (MirageStream *self);


G_END_DECLS
