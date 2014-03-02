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

/**
 * SECTION: mirage-stream
 * @title: MirageStream
 * @short_description: Interface for I/O streams.
 * @see_also: #MirageFileStream, #MirageFilterStream
 * @include: mirage-stream.h
 *
 * #MirageStream is a basic unit of file access abstraction used in
 * libMirage. It supports basic I/O operations, such as read, write,
 * seek and tell.
 *
 * Streams in libMirage are designed around the idea of filter stream
 * chains, where several filter streams (#MirageFilterStream) can be
 * chained on top of a stream that abstracts direct access to the file
 * (#MirageFileStream).
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"
#include "mirage-compat-input-stream.h"


/**********************************************************************\
 *                        Stream information                          *
\**********************************************************************/
/**
 * mirage_stream_get_filename:
 * @self: a #MirageFileStream
 *
 * Retrieves the name to file on which the stream is opened. If @self is
 * a filter stream in the filter stream chain, the filename is obtained from
 * the stream at the bottom of the chain.
 *
 * Returns: (transfer none): pointer to a buffer containing the filename.
 * The buffer belongs to the stream object and should not be modified.
 */
const gchar *mirage_stream_get_filename (MirageStream *self)
{
    return MIRAGE_STREAM_GET_INTERFACE(self)->get_filename(self);
}

/**
 * mirage_stream_is_writable:
 * @self: a #MirageFileStream
 *
 * Queries the stream (chain) for write support. For the stream to be
 * writable, the stream object implementation itself must support write
 * operations, and any stream objects below it in the stream chain must
 * also be writable.
 *
 * Returns: %TRUE if the stream (chain) is writable, %FALSE if it is not.
 */
gboolean mirage_stream_is_writable (MirageStream *self)
{
    return MIRAGE_STREAM_GET_INTERFACE(self)->is_writable(self);
}


/**********************************************************************\
 *                           I/O functions                            *
\**********************************************************************/
/**
 * mirage_stream_read:
 * @self: a #MirageFileStream
 * @buffer: (in): a buffer to read data into
 * @count: (in): number of bytes to read from stream
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Attempts to read @count bytes from stream into the buffer starting at
 * @buffer. Will block during the operation.
 *
 * Returns: number of bytes read, or -1 on error, or 0 on end of file.
 */
gssize mirage_stream_read (MirageStream *self, void *buffer, gsize count, GError **error)
{
    return MIRAGE_STREAM_GET_INTERFACE(self)->read(self, buffer, count, error);
}

/**
 * mirage_stream_write:
 * @self: a #MirageFileStream
 * @buffer: (in): a buffer to write data from
 * @count: (in): number of bytes to write to stream
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Attempts to write @count bytes to stream from the buffer starting at
 * @buffer. Will block during the operation.
 *
 * Returns: number of bytes written, or -1 on error.
 */
gssize mirage_stream_write (MirageStream *self, const void *buffer, gsize count, GError **error)
{
    return MIRAGE_STREAM_GET_INTERFACE(self)->write(self, buffer, count, error);
}

/**
 * mirage_stream_seek:
 * @self: a #MirageFileStream
 * @offset: (in): offset to seek
 * @type: (in): seek type
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Seeks in the stream by the given @offset, modified by @type.
 *
 * Returns: %TRUE on success, %FALSE on failure.
 */
gboolean mirage_stream_seek (MirageStream *self, goffset offset, GSeekType type, GError **error)
{
    return MIRAGE_STREAM_GET_INTERFACE(self)->seek(self, offset, type, error);
}

/**
 * mirage_stream_tell:
 * @self: a #MirageFileStream
 *
 * Retrieves the current position within the stream.
 *
 * Returns: the offset from the beginning of the stream.
 */
goffset mirage_stream_tell (MirageStream *self)
{
    return MIRAGE_STREAM_GET_INTERFACE(self)->tell(self);
}


/**
 * mirage_stream_move_file:
 * @self: a #MirageFileStream
 * @new_filename: (in): the new filename
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Attempts to move the file on top of which the stream (chain) is opened
 * to @new_filename. If supported, native move operations are used,
 * otherwise a copy + delete fallback is used.
 *
 * Returns: %TRUE on success, %FALSE on failure.
 */
gboolean mirage_stream_move_file (MirageStream *self, const gchar *new_filename, GError **error)
{
    return MIRAGE_STREAM_GET_INTERFACE(self)->move_file(self, new_filename, error);
}


/**
 * mirage_stream_get_g_input_stream:
 * @self: a #MirageFileStream
 *
 * Constructs and returns a compatibility object inheriting a #GInputStream.
 * This is to allow regular GIO stream objects (for example, a
 * #GDataInputStream) to be chained on top of our filter stream chain.
 *
 * Returns: (transfer full): a #GInputStream. The reference should be
 * released using g_object_unref() when no longer needed.
 */
GInputStream *mirage_stream_get_g_input_stream (MirageStream *self)
{
    return g_object_new(MIRAGE_TYPE_COMPAT_INPUT_STREAM, "stream", self, NULL);
}


/**********************************************************************\
 *                           Interface init                           *
\**********************************************************************/
GType mirage_stream_get_type (void) {
    static GType iface_type = 0;
    if (iface_type == 0) {
        static const GTypeInfo info = {
            sizeof(MirageStreamInterface),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            NULL,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            0,
            0,      /* n_preallocs */
            NULL,   /* instance_init */
            NULL    /* value_table */
        };

        iface_type = g_type_register_static(G_TYPE_INTERFACE, "MirageStream", &info, 0);
    }

    return iface_type;
}
