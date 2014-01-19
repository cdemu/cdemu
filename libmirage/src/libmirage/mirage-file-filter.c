/*
 *  libMirage: File filter object
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * SECTION: mirage-file-filter
 * @title: MirageFileFilter
 * @short_description: File filter object.
 * @see_also: #MirageContext
 * @include: mirage-file-filter.h
 *
 * #MirageFileFilter is a basic unit of file access abstraction used in
 * libMirage. It inherits from #GFilterInputStream and implements #GSeekable
 * interface.
 *
 * When opening a file with libMirage, mirage_context_create_file_stream()
 * function should be used. It creates a chain of #MirageFileFilter objects
 * on top of a #GFileInputStream, and returns the top object on the chain.
 * This allows transparent access to, for example, compressed data stored
 * in the file.
 *
 * There are two ways to implement a #MirageFileFilter. For full control
 * over the logic for reading from parts and managing position in the
 * stream, use "full interface", which requires implementation of three
 * virtual functions: read, seek and tell. The second option is to use
 * "simplified interface", which provides framework for stream position
 * management and reading logic, and requires that file filter implements
 * partial_read function. Additionally, it requires that file filter
 * implementation sets the file stream size using
 * mirage_file_filter_set_file_size() function. In partial_read, the
 * current position in the stream, which is managed by the framework, can
 * be obtained using mirage_file_filter_get_position().
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"

#define __debug__ "FileFilter"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_FILE_FILTER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FILE_FILTER, MirageFileFilterPrivate))

struct _MirageFileFilterPrivate
{
    MirageFileFilterInfo info;

    MirageContext *context;

    /* Simplified interface */
    guint64 file_size;
    goffset position;
};


/**********************************************************************\
 *                         File filter info API                       *
\**********************************************************************/
static void mirage_file_filter_info_generate (MirageFileFilterInfo *info, const gchar *id, const gchar *name, gint num_types, va_list args)
{
    /* Free old fields */
    mirage_file_filter_info_free(info);

    /* Copy ID and name */
    info->id = g_strdup(id);
    info->name = g_strdup(name);

    /* Copy description and MIME type strings */
    info->description = g_new0(gchar *, num_types+1);
    info->mime_type = g_new0(gchar *, num_types+1);

    for (gint i = 0; i < num_types; i++) {
        info->description[i] = g_strdup(va_arg(args, const gchar *));
        info->mime_type[i] = g_strdup(va_arg(args, const gchar *));
    }
}

/**
 * mirage_file_filter_info_copy:
 * @info: (in): a #MirageFileFilterInfo to copy data from
 * @dest: (in): a #MirageFileFilterInfo to copy data to
 *
 * Copies parser information from @info to @dest.
 */
void mirage_file_filter_info_copy (const MirageFileFilterInfo *info, MirageFileFilterInfo *dest)
{
    dest->id = g_strdup(info->id);
    dest->name = g_strdup(info->name);
    dest->description = g_strdupv(info->description);
    dest->mime_type = g_strdupv(info->mime_type);
}

/**
 * mirage_file_filter_info_free:
 * @info: (in): a #MirageFileFilterInfo to free
 *
 * Frees the allocated fields in @info (but not the structure itself!).
 */
void mirage_file_filter_info_free (MirageFileFilterInfo *info)
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
 * mirage_file_filter_generate_info:
 * @self: a #MirageFileFilter
 * @id: (in): file filter ID
 * @name: (in): file filter name
 * @num_types: (in): number of MIME types
 * @...: (in): description and MIME type string pairs, one for each defined type
 *
 * Generates file filter information from the input fields. It is intended as a function
 * for creating file filter information in file filter implementations.
 */
void mirage_file_filter_generate_info (MirageFileFilter *self, const gchar *id, const gchar *name, gint num_types, ...)
{
    va_list args;
    va_start(args, num_types);

    mirage_file_filter_info_generate(&self->priv->info, id, name, num_types, args);

    va_end(args);
}


/**
 * mirage_file_filter_get_info:
 * @self: a #MirageFileFilter
 *
 * Retrieves file filter information.
 *
 * Returns: (transfer none): a pointer to file filter information structure. The
 * structure belongs to object and therefore should not be modified.
 */
const MirageFileFilterInfo *mirage_file_filter_get_info (MirageFileFilter *self)
{
    return &self->priv->info;
}


/**
 * mirage_file_filter_can_handle_data_format:
 * @self: a #MirageFileFilter
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Checks whether file filter can handle data stored in underyling stream.
 *
 * Returns: %TRUE if file filter can handle data in underlying stream, %FALSE if not
 */
gboolean mirage_file_filter_can_handle_data_format (MirageFileFilter *self, GError **error)
{
    /* Provided by implementation */
    return MIRAGE_FILE_FILTER_GET_CLASS(self)->can_handle_data_format(self, error);
}


/**
 * mirage_file_filter_set_file_size:
 * @self: a #MirageFileFilter
 * @size: (in): size of the stream
 *
 * Sets size of the stream.
 *
 * This function is intented for use in file filter implementations that
 * are based on the simplified interface. It should be used by the
 * implementation to set the stream size during stream parsing; the set
 * stream size is then used by the read function that is implemented by
 * the simplified interface.
 */
void mirage_file_filter_set_file_size (MirageFileFilter *self, gsize size)
{
    self->priv->file_size = size;
}

/**
 * mirage_file_filter_get_position:
 * @self: a #MirageFileFilter
 *
 * Retrieves position in the stream.
 *
 * This function is intented for use in file filter implementations that
 * are based on the simplified interface. It should be used by the
 * implementation's partial_read function to determine position to
 * read from without having to worry about position management and update.
 *
 * Returns: position in the stream
 */
goffset mirage_file_filter_get_position (MirageFileFilter *self)
{
    return self->priv->position;
}


/**********************************************************************\
 *                  GSeekable methods implementations                 *
\**********************************************************************/
static goffset mirage_file_filter_tell (GSeekable *_self)
{
    MirageFileFilter *self = MIRAGE_FILE_FILTER(_self);
    /* Provided by implementation */
    return MIRAGE_FILE_FILTER_GET_CLASS(self)->tell(self);
}

static gboolean mirage_file_filter_can_seek (GSeekable *_self G_GNUC_UNUSED)
{
    /* Should be always seekable */
    return TRUE;
}

static gboolean mirage_file_filter_seek (GSeekable *_self, goffset offset, GSeekType type, GCancellable *cancellable G_GNUC_UNUSED, GError **error)
{
    MirageFileFilter *self = MIRAGE_FILE_FILTER(_self);
    /* Provided by implementation */
    return MIRAGE_FILE_FILTER_GET_CLASS(self)->seek(self, offset, type, error);
}

static gboolean mirage_file_filter_can_truncate (GSeekable *_self G_GNUC_UNUSED)
{
    /* Truncation is not implemented */
    return FALSE;
}

static gboolean mirage_file_filter_truncate_fn (GSeekable *_self G_GNUC_UNUSED, goffset offset G_GNUC_UNUSED, GCancellable *cancellable G_GNUC_UNUSED, GError **error)
{
    /* Truncation is not implemented */
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Function not implemented!");
    return FALSE;
}


/**********************************************************************\
 *   Default implementation of I/O functions (simplified interface)   *
\**********************************************************************/
static gboolean mirage_file_filter_seek_impl (MirageFileFilter *self, goffset offset, GSeekType type, GError **error)
{
    goffset new_position;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: seek: %ld (0x%lX), type %d\n", __debug__, offset, offset, type);

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
            new_position = self->priv->file_size + offset;
            break;
        }
        default: {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid seek type.");
            return FALSE;
        }
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: seeking to position %ld (0x%lX)\n", __debug__, new_position, new_position);

    /* Validate new position */
    if (new_position < 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Seek before beginning of file not allowed!");
        return FALSE;
    }

    /* Set new position */
    self->priv->position = new_position;

    return TRUE;
}

static goffset mirage_file_filter_tell_impl (MirageFileFilter *self)
{
    /* Return stored position */
    return self->priv->position;
}

static gssize mirage_file_filter_read_impl (MirageFileFilter *self, void *buffer, gsize count, GError **error)
{
    gssize total_read, read_len;
    guint8 *ptr = buffer;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: read %ld (0x%lX) bytes from position %ld (0x%lX)!\n", __debug__, count, count, self->priv->position, self->priv->position);

    /* Read until all is read */
    total_read = 0;

    while (count > 0) {
        /* Check if we're at end of stream */
        if (self->priv->position >= self->priv->file_size) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: end of stream reached!\n", __debug__);
            break;
        }

        /* Do a partial read using implementation's function */
        read_len = MIRAGE_FILE_FILTER_GET_CLASS(self)->partial_read(self, ptr, count);
        if (read_len == -1) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to do a partial read!\n", __debug__);
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to do a partial read.");
            return -1;
        }

        ptr += read_len;
        total_read += read_len;
        count -= read_len;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: read %ld (0x%lX) bytes... %ld (0x%lX) remaining\n", __debug__, read_len, read_len, count, count);

        /* Update position */
        self->priv->position += read_len;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: read complete\n", __debug__);

    return total_read;
}


/**********************************************************************\
 *                GInputStream methods implementations                *
\**********************************************************************/
static gssize mirage_file_filter_read (GInputStream *_self, void *buffer, gsize count, GCancellable *cancellable G_GNUC_UNUSED, GError **error)
{
    MirageFileFilter *self = MIRAGE_FILE_FILTER(_self);
    return MIRAGE_FILE_FILTER_GET_CLASS(self)->read(self, buffer, count, error);
}


/**********************************************************************\
 *              MirageContextual methods implementation               *
\**********************************************************************/
static void mirage_file_filter_set_context (MirageContextual *_self, MirageContext *context)
{
    MirageFileFilter *self = MIRAGE_FILE_FILTER(_self);

    if (context == self->priv->context) {
        /* Don't do anything if we're trying to set the same context */
        return;
    }

    /* If context is already set, free it */
    if (self->priv->context) {
        g_object_unref(self->priv->context);
        self->priv->context = NULL;
    }

    /* Set context and ref it */
    if (context) {
        self->priv->context = g_object_ref(context);
    }
}

static MirageContext *mirage_file_filter_get_context (MirageContextual *_self)
{
    MirageFileFilter *self = MIRAGE_FILE_FILTER(_self);
    if (self->priv->context) {
        g_object_ref(self->priv->context);
    }
    return self->priv->context;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
static void mirage_file_filter_gseekable_init (GSeekableIface *iface);
static void mirage_file_filter_contextual_init (MirageContextualInterface *iface);

G_DEFINE_ABSTRACT_TYPE_WITH_CODE(MirageFileFilter,
                                 mirage_file_filter,
                                 G_TYPE_FILTER_INPUT_STREAM,
                                 G_IMPLEMENT_INTERFACE(G_TYPE_SEEKABLE, mirage_file_filter_gseekable_init);
                                 G_IMPLEMENT_INTERFACE(MIRAGE_TYPE_CONTEXTUAL, mirage_file_filter_contextual_init));


static void mirage_file_filter_init (MirageFileFilter *self)
{
    self->priv = MIRAGE_FILE_FILTER_GET_PRIVATE(self);

    /* Make sure all fields are empty */
    memset(&self->priv->info, 0, sizeof(self->priv->info));

    self->priv->context = NULL;

    self->priv->file_size = 0;
    self->priv->position = 0;
}

static void mirage_file_filter_dispose (GObject *gobject)
{
    MirageFileFilter *self = MIRAGE_FILE_FILTER(gobject);

    /* Unref debug context (if we have it) */
    if (self->priv->context) {
        g_object_unref(self->priv->context);
        self->priv->context = NULL;
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_file_filter_parent_class)->dispose(gobject);
}


static void mirage_file_filter_finalize (GObject *gobject)
{
    MirageFileFilter *self = MIRAGE_FILE_FILTER(gobject);

    /* Free info structure */
    mirage_file_filter_info_free(&self->priv->info);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_file_filter_parent_class)->finalize(gobject);
}

static void mirage_file_filter_class_init (MirageFileFilterClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GInputStreamClass *ginputstream_class = G_INPUT_STREAM_CLASS(klass);

    gobject_class->dispose = mirage_file_filter_dispose;
    gobject_class->finalize = mirage_file_filter_finalize;

    ginputstream_class->read_fn = mirage_file_filter_read;

    /* Default I/O functions implementations for simplified interface */
    klass->read = mirage_file_filter_read_impl;
    klass->tell = mirage_file_filter_tell_impl;
    klass->seek = mirage_file_filter_seek_impl;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageFileFilterPrivate));
}

static void mirage_file_filter_gseekable_init (GSeekableIface *iface)
{
    iface->tell = mirage_file_filter_tell;
    iface->can_seek = mirage_file_filter_can_seek;
    iface->seek = mirage_file_filter_seek;
    iface->can_truncate = mirage_file_filter_can_truncate;
    iface->truncate_fn = mirage_file_filter_truncate_fn;
}

static void mirage_file_filter_contextual_init (MirageContextualInterface *iface)
{
    iface->set_context = mirage_file_filter_set_context;
    iface->get_context = mirage_file_filter_get_context;
}
