/*
 *  libMirage: SNDFILE file filter: File filter object
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

#include "filter-sndfile.h"

#define __debug__ "SNDFILE-FileFilter"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_FILE_FILTER_SNDFILE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FILE_FILTER_SNDFILE, MirageFileFilterSndfilePrivate))

struct _MirageFileFilterSndfilePrivate
{
    SNDFILE *sndfile;
    SF_INFO format;

    gint buflen;
    guint8 *buffer;

    gint cached_frame;
};


/**********************************************************************\
 *                        libsndfile I/O bridge                        *
\**********************************************************************/
static sf_count_t sndfile_io_get_filelen (GInputStream *stream)
{
    goffset position, size;

    /* Store current position */
    position = g_seekable_tell(G_SEEKABLE(stream));

    /* Seek to the end of file and get position */
    g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_END, NULL, NULL);
    size = g_seekable_tell(G_SEEKABLE(stream));

    /* Restore position */
    g_seekable_seek(G_SEEKABLE(stream), position, G_SEEK_SET, NULL, NULL);

    return size;
}

static sf_count_t sndfile_io_seek (sf_count_t offset, int whence, GInputStream *stream)
{
    GSeekType seek_type;

    /* Map whence parameter */
    switch (whence) {
        case SEEK_CUR: {
            seek_type = G_SEEK_CUR;
            break;
        }
        case SEEK_SET: {
            seek_type = G_SEEK_SET;
            break;
        }
        case SEEK_END: {
            seek_type = G_SEEK_END;
            break;
        }
        default: {
            /* Should not happen... */
            seek_type = G_SEEK_SET;
            break;
        }
    }

    /* Seek */
    g_seekable_seek(G_SEEKABLE(stream), offset, seek_type, NULL, NULL);

    return g_seekable_tell(G_SEEKABLE(stream));
}

static sf_count_t sndfile_io_read (void *ptr, sf_count_t count, GInputStream *stream)
{
    return g_input_stream_read(stream, ptr, count, NULL, NULL);
}

static  sf_count_t sndfile_io_tell (GInputStream *stream)
{
    return g_seekable_tell(G_SEEKABLE(stream));
}

static SF_VIRTUAL_IO sndfile_io_bridge = {
    .get_filelen = (sf_vio_get_filelen)sndfile_io_get_filelen,
    .seek = (sf_vio_seek)sndfile_io_seek,
    .read = (sf_vio_read)sndfile_io_read,
    .write = NULL,
    .tell = (sf_vio_tell)sndfile_io_tell,
};


/**********************************************************************\
 *              MirageFileFilter methods implementations             *
\**********************************************************************/
static gboolean mirage_file_filter_sndfile_can_handle_data_format (MirageFileFilter *_self, GError **error)
{
    MirageFileFilterSndfile *self = MIRAGE_FILE_FILTER_SNDFILE(_self);
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));
    gsize length;

    /* Clear the format */
    memset(&self->priv->format, 0, sizeof(self->priv->format));

    /* Seek to beginning */
    g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);

    /* Try opening sndfile on top of stream */
    self->priv->sndfile = sf_open_virtual(&sndfile_io_bridge, SFM_READ, &self->priv->format, stream);
    if (!self->priv->sndfile) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Filter cannot handle given data: failed to open sndfile!");
        return FALSE;
    }

    /* Check some additional requirements (two channels and seekable) */
    if (self->priv->format.channels != 2 || self->priv->format.seekable == 0) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Filter cannot handle given data: invalid number of channels or non-seekable!");
        return FALSE;
    }

    /* Compute length in bytes */
    length = self->priv->format.frames * self->priv->format.channels * sizeof(guint16);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: raw stream length: %ld (0x%lX) bytes\n", __debug__, length, length);
    mirage_file_filter_set_file_size(MIRAGE_FILE_FILTER(self), length);

    /* Allocate read buffer; we wish to hold a single (multichannel) frame */
    self->priv->buflen = self->priv->format.channels * sizeof(guint16);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: buffer length: %d bytes\n", __debug__, self->priv->buflen);
    self->priv->buffer = g_try_malloc(self->priv->buflen);
    if (!self->priv->buffer) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Failed to allocate read buffer!");
        return FALSE;
    }

    return TRUE;
}

static gssize mirage_file_filter_sndfile_partial_read (MirageFileFilter *_self, void *buffer, gsize count)
{
    MirageFileFilterSndfile *self = MIRAGE_FILE_FILTER_SNDFILE(_self);
    goffset position = mirage_file_filter_get_position(MIRAGE_FILE_FILTER(self));
    gint frame;

    /* Find the frame corresponding to current position */
    frame = position / self->priv->buflen;
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: stream position: %ld (0x%lX) -> frame #%d (cached: #%d)\n", __debug__, position, position, frame, self->priv->cached_frame);

    /* If we do not have block in cache, uncompress it */
    if (frame != self->priv->cached_frame) {
        gsize read_length;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: frame not cached, reading...\n", __debug__);

        /* Seek to frame */
        sf_seek(self->priv->sndfile, frame, SEEK_SET);

        /* Read the frame */
        read_length = sf_readf_short(self->priv->sndfile, (short *)self->priv->buffer, 1);

        if (!read_length) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: frame not read; EOF reached?\n", __debug__);
            return 0;
        }

        /* Store the number of currently stored block */
        self->priv->cached_frame = frame;
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: frame already cached\n", __debug__);
    }

    /* Copy data */
    goffset frame_offset = position % self->priv->buflen;
    count = MIN(count, self->priv->buflen - frame_offset);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: offset within frame: %ld, copying %d bytes\n", __debug__, frame_offset, count);

    memcpy(buffer, self->priv->buffer + frame_offset, count);

    return count;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE(MirageFileFilterSndfile, mirage_file_filter_sndfile, MIRAGE_TYPE_FILE_FILTER);

void mirage_file_filter_sndfile_type_register (GTypeModule *type_module)
{
    return mirage_file_filter_sndfile_register_type(type_module);
}


static void mirage_file_filter_sndfile_init (MirageFileFilterSndfile *self)
{
    self->priv = MIRAGE_FILE_FILTER_SNDFILE_GET_PRIVATE(self);

    mirage_file_filter_generate_info(MIRAGE_FILE_FILTER(self),
        "FILTER-SNDFILE",
        "SNDFILE File Filter",
        0
    );

    self->priv->cached_frame = -1;

    self->priv->sndfile = NULL;
    self->priv->buffer = NULL;
}

static void mirage_file_filter_sndfile_finalize (GObject *gobject)
{
    MirageFileFilterSndfile *self = MIRAGE_FILE_FILTER_SNDFILE(gobject);

    /* Close sndfile */
    if (self->priv->sndfile) {
        sf_close(self->priv->sndfile);
    }

    /* Free read buffer */
    g_free(self->priv->buffer);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_file_filter_sndfile_parent_class)->finalize(gobject);
}

static void mirage_file_filter_sndfile_class_init (MirageFileFilterSndfileClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageFileFilterClass *file_filter_class = MIRAGE_FILE_FILTER_CLASS(klass);

    gobject_class->finalize = mirage_file_filter_sndfile_finalize;

    file_filter_class->can_handle_data_format = mirage_file_filter_sndfile_can_handle_data_format;

    file_filter_class->partial_read = mirage_file_filter_sndfile_partial_read;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageFileFilterSndfilePrivate));
}

static void mirage_file_filter_sndfile_class_finalize (MirageFileFilterSndfileClass *klass G_GNUC_UNUSED)
{
}
