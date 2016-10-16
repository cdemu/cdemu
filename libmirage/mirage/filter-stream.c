/*
 *  libMirage: filter stream
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

/**
 * SECTION: mirage-filter-stream
 * @title: MirageFilterStream
 * @short_description: Filter I/O stream object.
 * @see_also: #MirageStream, #MirageFileStream
 * @include: mirage-filter-stream.h
 *
 * #MirageFilterStream is a basic unit of file access abstraction used in
 * libMirage. It implements #MirageStream interface to perform I/O operations.
 *
 * When opening a file with libMirage, mirage_context_create_input_stream()
 * function should be used. It creates a chain of #MirageFilterStream objects
 * on top of a #MirageFileStream, and returns the top object on the chain.
 * This allows transparent access to, for example, compressed data stored
 * in the file. Alternatively, you can create a #MirageFileStream yourself
 * and open additional #MirageFilterStream objects on top of it.
 *
 * There are two ways to implement a #MirageFilterStream. For full control
 * over the logic for reading from parts and managing position in the
 * stream, use "full interface", which requires implementation of three
 * virtual functions: read, seek and tell. The second option is to use
 * "simplified read interface", which provides framework for stream position
 * management and reading logic, and requires that filter stream implements
 * partial_read function. Additionally, it requires that filter stream
 * implementation sets the file stream size using
 * mirage_filter_stream_simplified_set_stream_length() function. In
 * simplified_partial_read, the current position in the stream, which is
 * managed by the framework, can be obtained using mirage_filter_stream_simplified_get_position().
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"

#include <glib/gi18n-lib.h>

#define __debug__ "FilterStream"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_FILTER_STREAM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FILTER_STREAM, MirageFilterStreamPrivate))

struct _MirageFilterStreamPrivate
{
    MirageFilterStreamInfo info;

    MirageStream *underlying_stream;

    /* Simplified interface */
    guint64 stream_length;
    goffset position;
};


/**********************************************************************\
 *                        Filter stream info API                      *
\**********************************************************************/
static void mirage_filter_stream_info_generate (MirageFilterStreamInfo *info, const gchar *id, const gchar *name, gboolean writable, gint num_types, va_list args)
{
    /* Free old fields */
    mirage_filter_stream_info_free(info);

    /* Copy ID and name */
    info->id = g_strdup(id);
    info->name = g_strdup(name);

    /* Set writable flag */
    info->writable = writable;

    /* Copy description and MIME type strings */
    info->description = g_new0(gchar *, num_types+1);
    info->mime_type = g_new0(gchar *, num_types+1);

    for (gint i = 0; i < num_types; i++) {
        info->description[i] = g_strdup(va_arg(args, const gchar *));
        info->mime_type[i] = g_strdup(va_arg(args, const gchar *));
    }
}

/**
 * mirage_filter_stream_info_copy:
 * @info: (in): a #MirageFilterStreamInfo to copy data from
 * @dest: (in): a #MirageFilterStreamInfo to copy data to
 *
 * Copies parser information from @info to @dest.
 */
void mirage_filter_stream_info_copy (const MirageFilterStreamInfo *info, MirageFilterStreamInfo *dest)
{
    dest->id = g_strdup(info->id);
    dest->name = g_strdup(info->name);
    dest->writable = info->writable;
    dest->description = g_strdupv(info->description);
    dest->mime_type = g_strdupv(info->mime_type);
}

/**
 * mirage_filter_stream_info_free:
 * @info: (in): a #MirageFilterStreamInfo to free
 *
 * Frees the allocated fields in @info (but not the structure itself!).
 */
void mirage_filter_stream_info_free (MirageFilterStreamInfo *info)
{
    g_free(info->id);
    g_free(info->name);

    g_strfreev(info->description);
    g_strfreev(info->mime_type);
}


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
/**
 * mirage_filter_stream_generate_info:
 * @self: a #MirageFilterStream
 * @id: (in): filter stream ID
 * @name: (in): filter stream name
 * @writable: (in): flag indicating whether filter stream supports write operation
 * @num_types: (in): number of MIME types
 * @...: (in): description and MIME type string pairs, one for each defined type
 *
 * Generates filter stream information from the input fields. It is intended as a function
 * for creating filter stream information in filter stream implementations.
 */
void mirage_filter_stream_generate_info (MirageFilterStream *self, const gchar *id, const gchar *name, gboolean writable, gint num_types, ...)
{
    va_list args;
    va_start(args, num_types);

    mirage_filter_stream_info_generate(&self->priv->info, id, name, writable, num_types, args);

    va_end(args);
}


/**
 * mirage_filter_stream_get_info:
 * @self: a #MirageFilterStream
 *
 * Retrieves filter stream information.
 *
 * Returns: (transfer none): a pointer to filter stream information structure. The
 * structure belongs to object and therefore should not be modified.
 */
const MirageFilterStreamInfo *mirage_filter_stream_get_info (MirageFilterStream *self)
{
    return &self->priv->info;
}


/**
 * mirage_filter_stream_get_underlying_stream:
 * @self: a #MirageFilterStream
 *
 * Retrieves filter stream's underlying stream.
 *
 * Returns: (transfer none): a pointer to filter stream's underlying stream.
 * The reference belongs to filter stream and should not be released.
 */
MirageStream *mirage_filter_stream_get_underlying_stream (MirageFilterStream *self)
{
    return self->priv->underlying_stream;
}


/**
 * mirage_filter_stream_open:
 * @self: a #MirageFilterStream
 * @stream: (in): an underlying stream
 * @writable: (in): a flag indicating whether the stream should be opened in read/write mode or not
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Opens stream on top of provided underlying stream.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_filter_stream_open (MirageFilterStream *self, MirageStream *stream, gboolean writable, GError **error)
{
    gboolean succeeded = TRUE;

    /* Store reference to stream as our underlying stream for the time
       of opening */
    self->priv->underlying_stream = g_object_ref(stream);

    /* Provided by implementation */
    succeeded = MIRAGE_FILTER_STREAM_GET_CLASS(self)->open(self, stream, writable, error);

    /* If opening failed, release our reference to underlying stream */
    if (!succeeded) {
        g_object_unref(self->priv->underlying_stream);
        self->priv->underlying_stream = FALSE;
    }

    return succeeded;
}


/**
 * mirage_filter_stream_simplified_set_stream_length:
 * @self: a #MirageFilterStream
 * @length: (in): length of the stream
 *
 * Sets size of the stream.
 *
 * This function is intented for use in filter stream implementations that
 * are based on the simplified interface. It should be used by the
 * implementation to set the stream size during stream parsing; the set
 * stream size is then used by the read function that is implemented by
 * the simplified interface.
 */
void mirage_filter_stream_simplified_set_stream_length (MirageFilterStream *self, gsize length)
{
    self->priv->stream_length = length;
}

/**
 * mirage_filter_stream_simplified_get_position:
 * @self: a #MirageFilterStream
 *
 * Retrieves position in the stream.
 *
 * This function is intented for use in filter stream implementations that
 * are based on the simplified interface. It should be used by the
 * implementation's simplified_partial_read function to determine position to
 * read from without having to worry about position management and update.
 *
 * Returns: position in the stream
 */
goffset mirage_filter_stream_simplified_get_position (MirageFilterStream *self)
{
    return self->priv->position;
}


/**********************************************************************\
 *                MirageStream methods implementations                *
\**********************************************************************/
static const gchar *mirage_filter_stream_get_filename (MirageStream *_self)
{
    MirageFilterStream *self = MIRAGE_FILTER_STREAM(_self);

    /* Filter stream does not store filename information; filename should
       be obtained from MirageFileFilter at the bottom of filter chain */
    if (!self->priv->underlying_stream) {
        return NULL;
    }

    return mirage_stream_get_filename(self->priv->underlying_stream);
}

static gboolean mirage_filter_stream_is_writable (MirageStream *_self)
{
    MirageFilterStream *self = MIRAGE_FILTER_STREAM(_self);

    /* This filter stream is writable if it supports write, and if its
       underlying stream is writable */
    if (!self->priv->underlying_stream) {
        return FALSE;
    }

    return self->priv->info.writable && mirage_stream_is_writable(self->priv->underlying_stream);
}


static gssize mirage_filter_stream_read (MirageStream *_self, void *buffer, gsize count, GError **error)
{
    MirageFilterStream *self = MIRAGE_FILTER_STREAM(_self);
    return MIRAGE_FILTER_STREAM_GET_CLASS(self)->read(self, buffer, count, error);
}

static gssize mirage_filter_stream_write (MirageStream *_self, const void *buffer, gsize count, GError **error)
{
    MirageFilterStream *self = MIRAGE_FILTER_STREAM(_self);
    return MIRAGE_FILTER_STREAM_GET_CLASS(self)->write(self, buffer, count, error);
}

static gboolean mirage_filter_stream_seek (MirageStream *_self, goffset offset, GSeekType type, GError **error)
{
    MirageFilterStream *self = MIRAGE_FILTER_STREAM(_self);
    /* Provided by implementation */
    return MIRAGE_FILTER_STREAM_GET_CLASS(self)->seek(self, offset, type, error);
}

static goffset mirage_filter_stream_tell (MirageStream *_self)
{
    MirageFilterStream *self = MIRAGE_FILTER_STREAM(_self);
    /* Provided by implementation */
    return MIRAGE_FILTER_STREAM_GET_CLASS(self)->tell(self);
}


static gboolean mirage_filter_stream_move_file (MirageStream *_self, const gchar *new_filename, GError **error)
{
    MirageFilterStream *self = MIRAGE_FILTER_STREAM(_self);

    /* We need an underlying stream, because only the file stream at
       the bottom of filter chain can perform a move */
    if (!self->priv->underlying_stream) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, Q_("No underlying stream!"));
        return FALSE;
    }

    return mirage_stream_move_file(self->priv->underlying_stream, new_filename, error);
}


/**********************************************************************\
 *   Default implementation of I/O functions (simplified interface)   *
\**********************************************************************/
static gssize mirage_filter_stream_read_impl (MirageFilterStream *self, void *buffer, gsize count, GError **error)
{
    gssize total_read, read_len;
    guint8 *ptr = buffer;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: read %ld (0x%lX) bytes from position %ld (0x%lX)!\n", __debug__, count, count, self->priv->position, self->priv->position);

    /* Make sure simplified_partial_read is provided */
    if (!MIRAGE_FILTER_STREAM_GET_CLASS(self)->simplified_partial_read) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: simplified partial read function is not implemented!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, Q_("Simplified partial read function is not implemented!"));
        return -1;
    }

    /* Read until all is read */
    total_read = 0;

    while (count > 0) {
        /* Check if we're at end of stream */
        if (self->priv->position >= self->priv->stream_length) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: end of stream reached!\n", __debug__);
            break;
        }

        /* Do a partial read using implementation's function */
        read_len = MIRAGE_FILTER_STREAM_GET_CLASS(self)->simplified_partial_read(self, ptr, count);
        if (read_len == -1) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to do a partial read!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, Q_("Failed to do a partial read."));
            return -1;
        }

        ptr += read_len;
        total_read += read_len;
        count -= read_len;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: read %ld (0x%lX) bytes... %ld (0x%lX) remaining\n", __debug__, read_len, read_len, count, count);

        /* Update position */
        self->priv->position += read_len;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: read complete\n", __debug__);

    return total_read;
}

static gssize mirage_filter_stream_write_impl (MirageFilterStream *self, const void *buffer, gsize count, GError **error)
{
    gssize total_write, write_len;
    const guint8 *ptr = buffer;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: write %ld (0x%lX) bytes at position %ld (0x%lX)!\n", __debug__, count, count, self->priv->position, self->priv->position);

    /* Make sure stream is writable */
    if (!mirage_stream_is_writable(MIRAGE_STREAM(self))) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: stream is not writable!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, Q_("Stream is not writable!"));
        return -1;
    }

    /* Make sure simplified_partial_write is provided */
    if (!MIRAGE_FILTER_STREAM_GET_CLASS(self)->simplified_partial_write) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: simplified partial write function is not implemented!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, Q_("Simplified partial write function is not implemented!"));
        return -1;
    }

    /* Write until all is written */
    total_write = 0;

    while (count > 0) {
        /* Do a partial write using implementation's function */
        write_len = MIRAGE_FILTER_STREAM_GET_CLASS(self)->simplified_partial_write(self, ptr, count);
        if (write_len == -1) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to do a partial write!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, Q_("Failed to do a partial write."));
            return -1;
        }

        ptr += write_len;
        total_write += write_len;
        count -= write_len;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: written %ld (0x%lX) bytes... %ld (0x%lX) remaining\n", __debug__, write_len, write_len, count, count);

        /* Update position */
        self->priv->position += write_len;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: write complete\n", __debug__);

    /* Update stream length */
    self->priv->stream_length = MAX(self->priv->stream_length, self->priv->position);

    return total_write;
}

static gboolean mirage_filter_stream_seek_impl (MirageFilterStream *self, goffset offset, GSeekType type, GError **error)
{
    goffset new_position;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: seek: %ld (0x%lX), type %d\n", __debug__, offset, offset, type);

    /* Compute new position */
    switch (type) {
        case G_SEEK_SET: {
            new_position = offset;
            break;
        }
        case G_SEEK_CUR: {
            new_position = self->priv->position + offset;
            break;
        }
        case G_SEEK_END: {
            new_position = self->priv->stream_length + offset;
            break;
        }
        default: {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, Q_("Invalid seek type."));
            return FALSE;
        }
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: seeking to position %ld (0x%lX)\n", __debug__, new_position, new_position);

    /* Validate new position */
    if (new_position < 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, Q_("Seek before beginning of file not allowed!"));
        return FALSE;
    }

    /* Set new position */
    self->priv->position = new_position;

    return TRUE;
}

static goffset mirage_filter_stream_tell_impl (MirageFilterStream *self)
{
    /* Return stored position */
    return self->priv->position;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
static void mirage_filter_stream_stream_init (MirageStreamInterface *iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE(MirageFilterStream,
                                 mirage_filter_stream,
                                 MIRAGE_TYPE_OBJECT,
                                 G_IMPLEMENT_INTERFACE(MIRAGE_TYPE_STREAM, mirage_filter_stream_stream_init));


static void mirage_filter_stream_init (MirageFilterStream *self)
{
    self->priv = MIRAGE_FILTER_STREAM_GET_PRIVATE(self);

    /* Make sure all fields are empty */
    memset(&self->priv->info, 0, sizeof(self->priv->info));

    self->priv->underlying_stream = NULL;

    self->priv->stream_length = 0;
    self->priv->position = 0;
}

static void mirage_filter_stream_dispose (GObject *gobject)
{
    MirageFilterStream *self = MIRAGE_FILTER_STREAM(gobject);

    /* Unref underlying stream (if we have it) */
    if (self->priv->underlying_stream) {
        g_object_unref(self->priv->underlying_stream);
        self->priv->underlying_stream = NULL;
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_filter_stream_parent_class)->dispose(gobject);
}


static void mirage_filter_stream_finalize (GObject *gobject)
{
    MirageFilterStream *self = MIRAGE_FILTER_STREAM(gobject);

    /* Free info structure */
    mirage_filter_stream_info_free(&self->priv->info);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_filter_stream_parent_class)->finalize(gobject);
}

static void mirage_filter_stream_class_init (MirageFilterStreamClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = mirage_filter_stream_dispose;
    gobject_class->finalize = mirage_filter_stream_finalize;

    /* Default I/O functions implementations for simplified interface */
    klass->read = mirage_filter_stream_read_impl;
    klass->write = mirage_filter_stream_write_impl;
    klass->tell = mirage_filter_stream_tell_impl;
    klass->seek = mirage_filter_stream_seek_impl;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageFilterStreamPrivate));
}

static void mirage_filter_stream_stream_init (MirageStreamInterface *iface)
{
    iface->get_filename = mirage_filter_stream_get_filename;
    iface->is_writable = mirage_filter_stream_is_writable;

    iface->read = mirage_filter_stream_read;
    iface->write = mirage_filter_stream_write;
    iface->seek = mirage_filter_stream_seek;
    iface->tell = mirage_filter_stream_tell;

    iface->move_file = mirage_filter_stream_move_file;
}
