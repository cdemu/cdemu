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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"

#define __debug__ "FileFilter"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_FILE_FILTER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FILE_FILTER, MIRAGE_FileFilterPrivate))

struct _MIRAGE_FileFilterPrivate
{
    MIRAGE_FileFilterInfo *file_filter_info;
};


/**********************************************************************\
 *                          Private functions                         *
\**********************************************************************/
static void destroy_file_filter_info (MIRAGE_FileFilterInfo *info)
{
    /* Free info and its content */
    if (info) {
        g_free(info->id);
        g_free(info->name);

        g_free(info);
    }
}


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
/**
 * mirage_file_filter_generate_file_filter_info:
 * @self: a #MIRAGE_FileFilter
 * @id: file filter ID
 * @name: file filter name
 *
 * <para>
 * Generates file filter information from the input fields. It is intended as a function
 * for creating file filter information in file filter implementations.
 * </para>
 **/
void mirage_file_filter_generate_file_filter_info (MIRAGE_FileFilter *self, const gchar *id, const gchar *name)
{
    /* Free old info */
    destroy_file_filter_info(self->priv->file_filter_info);

    /* Create new info */
    self->priv->file_filter_info = g_new0(MIRAGE_FileFilterInfo, 1);

    self->priv->file_filter_info->id = g_strdup(id);
    self->priv->file_filter_info->name = g_strdup(name);

    return;
}

/**
 * mirage_file_filter_get_file_filter_info:
 * @self: a #MIRAGE_FileFilter
 * @file_filter_info: location to store file filter info
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves file filter information.
 * </para>
 *
 * <para>
 * A pointer to file filter information structure is stored in @file_filter_info; the
 * structure belongs to object and therefore should not be modified.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_file_filter_get_file_filter_info (MIRAGE_FileFilter *self, const MIRAGE_FileFilterInfo **file_filter_info, GError **error)
{
    if (!self->priv->file_filter_info) {
        mirage_error(MIRAGE_E_DATANOTSET, error);
        return FALSE;
    }

    *file_filter_info = self->priv->file_filter_info;
    return TRUE;
}


/**
 * mirage_file_filter_can_handle_data_format:
 * @self: a #MIRAGE_FileFilter
 * @error: location to store error, or %NULL
 *
 * <para>
 * Checks whether file info can handle data stored in underyling stream.
 * </para>
 *
 * Returns: %TRUE if file filter can handle data in underlying stream, %FALSE if not
 **/
gboolean mirage_file_filter_can_handle_data_format (MIRAGE_FileFilter *self, GError **error)
{
    /* Provided by implementation */
    return MIRAGE_FILE_FILTER_GET_CLASS(self)->can_handle_data_format(self, error);
}



/**********************************************************************\
 *                  GSeekable methods implementations                 *
\**********************************************************************/
static goffset mirage_file_filter_tell (GSeekable *_self)
{
    MIRAGE_FileFilter *self = MIRAGE_FILE_FILTER(_self);
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
    MIRAGE_FileFilter *self = MIRAGE_FILE_FILTER(_self);
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
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Function not implemented");
    return FALSE;
}




/**********************************************************************\
 *                GInputStream methods implementations                *
\**********************************************************************/
static gssize mirage_file_filter_read (GInputStream *_self, void *buffer, gsize count, GCancellable *cancellable G_GNUC_UNUSED, GError **error)
{
    MIRAGE_FileFilter *self = MIRAGE_FILE_FILTER(_self);
    return MIRAGE_FILE_FILTER_GET_CLASS(self)->read(self, buffer, count, error);
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
static void mirage_file_filter_gseekable_init (GSeekableIface *iface);

G_DEFINE_TYPE_EXTENDED(MIRAGE_FileFilter,
                       mirage_file_filter,
                       G_TYPE_INPUT_STREAM,
                       0,
                       G_IMPLEMENT_INTERFACE(G_TYPE_SEEKABLE,
                                             mirage_file_filter_gseekable_init));


static void mirage_file_filter_init (MIRAGE_FileFilter *self)
{
    self->priv = MIRAGE_FILE_FILTER_GET_PRIVATE(self);

    self->priv->file_filter_info = NULL;
}

static void mirage_file_filter_finalize (GObject *gobject)
{
    MIRAGE_FileFilter *self = MIRAGE_FILE_FILTER(gobject);

    /* Free file filter info */
    destroy_file_filter_info(self->priv->file_filter_info);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_file_filter_parent_class)->finalize(gobject);
}

static void mirage_file_filter_class_init (MIRAGE_FileFilterClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GInputStreamClass *ginputstream_class = G_INPUT_STREAM_CLASS(klass);

    gobject_class->finalize = mirage_file_filter_finalize;

    ginputstream_class->read_fn = mirage_file_filter_read;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_FileFilterPrivate));
}

static void mirage_file_filter_gseekable_init (GSeekableIface *iface)
{
    iface->tell = mirage_file_filter_tell;
    iface->can_seek = mirage_file_filter_can_seek;
    iface->seek = mirage_file_filter_seek;
    iface->can_truncate = mirage_file_filter_can_truncate;
    iface->truncate_fn = mirage_file_filter_truncate_fn;
}
