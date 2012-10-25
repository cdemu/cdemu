/*
 *  libMirage: SNDFILE fragment: Fragment object
 *  Copyright (C) 2007-2012 Rok Mandeljc
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

#include "fragment-sndfile.h"

#define __debug__ "SNDFILE-Fragment"


/**********************************************************************\
 *                          Private structure                         *
\***********************************************************************/
#define MIRAGE_FRAGMENT_SNDFILE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FRAGMENT_SNDFILE, MirageFragmentSndfilePrivate))

struct _MirageFragmentSndfilePrivate
{
    GInputStream *stream;

    SNDFILE *sndfile;
    SF_INFO format;
    sf_count_t offset;
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
 *                   Audio interface implementation                   *
\**********************************************************************/
static void mirage_fragment_sndfile_set_stream (MirageAudioFragment *_self, GInputStream *stream)
{
    MirageFragmentSndfile *self = MIRAGE_FRAGMENT_SNDFILE(_self);

    /* If stream is already set, release it and reset format */
    if (self->priv->stream) {
        g_object_unref(self->priv->stream);
        self->priv->stream = NULL;
    }
    if (self->priv->sndfile) {
        sf_close(self->priv->sndfile);
        memset(&self->priv->format, 0, sizeof(self->priv->format));
    }

    /* Set the provided stream */
    self->priv->stream = stream;
    g_object_ref(stream);

    /* Open sndfile - this is guaranteed to succeed, since it already
       passed the mirage_fragment_can_handle_data() */
    self->priv->sndfile = sf_open_virtual(&sndfile_io_bridge, SFM_READ, &self->priv->format, self->priv->stream);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: SNDFILE file format:\n"
            " -> frames = %lli\n"
            " -> samplerate = %i\n"
            " -> channels = %i\n"
            " -> format = 0x%X\n"
            " -> sections = %i\n"
            " -> seekable = %i\n",
            __debug__,
            self->priv->format.frames,
            self->priv->format.samplerate,
            self->priv->format.channels,
            self->priv->format.format,
            self->priv->format.sections,
            self->priv->format.seekable);
}

static const gchar *mirage_fragment_sndfile_get_filename (MirageAudioFragment *_self)
{
    MirageFragmentSndfile *self = MIRAGE_FRAGMENT_SNDFILE(_self);
    /* Return filename */
    return mirage_contextual_get_file_stream_filename(MIRAGE_CONTEXTUAL(self), self->priv->stream);
}

static void mirage_fragment_sndfile_set_offset (MirageAudioFragment *_self, gint offset)
{
    MirageFragmentSndfile *self = MIRAGE_FRAGMENT_SNDFILE(_self);
    /* Set offset */
    self->priv->offset = offset*SNDFILE_FRAMES_PER_SECTOR;
}

static gint mirage_fragment_sndfile_get_offset (MirageAudioFragment *_self)
{
    MirageFragmentSndfile *self = MIRAGE_FRAGMENT_SNDFILE(_self);
    /* Return */
    return self->priv->offset/SNDFILE_FRAMES_PER_SECTOR;
}


/**********************************************************************\
 *               MirageFragment methods implementations              *
\**********************************************************************/
static gboolean mirage_fragment_sndfile_can_handle_data_format (MirageFragment *_self G_GNUC_UNUSED, GInputStream *stream, GError **error)
{
    SNDFILE *sndfile;
    SF_INFO format;

    format.format = 0;

    /* Make sure stream is given */
    if (!stream) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Fragment cannot handle given data!");
        return FALSE;
    }

    /* Try opening sndfile on top of stream */
    sndfile = sf_open_virtual(&sndfile_io_bridge, SFM_READ, &format, stream);
    if (!sndfile) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Fragment cannot handle given data!");
        return FALSE;
    }
    sf_close(sndfile);

    /* Check some additional requirements (e.g. two channels) */
    if (format.channels != 2 || format.seekable == 0) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Fragment cannot handle given data!");
        return FALSE;
    }

    return TRUE;
}

static gboolean mirage_fragment_sndfile_use_the_rest_of_file (MirageFragment *_self, GError **error)
{
    MirageFragmentSndfile *self = MIRAGE_FRAGMENT_SNDFILE(_self);
    gint fragment_len;

    /* Make sure file's loaded */
    if (!self->priv->sndfile) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Audio file stream not set!");
        return FALSE;
    }

    fragment_len = (self->priv->format.frames - self->priv->offset)/SNDFILE_FRAMES_PER_SECTOR;
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: using the rest of file (%d sectors)\n", __debug__, fragment_len);

    /* Set the length */
    mirage_fragment_set_length(MIRAGE_FRAGMENT(self), fragment_len);
    return TRUE;
}

static gboolean mirage_fragment_sndfile_read_main_data (MirageFragment *_self, gint address, guint8 **buffer, gint *length, GError **error)
{
    MirageFragmentSndfile *self = MIRAGE_FRAGMENT_SNDFILE(_self);
    sf_count_t position;
    sf_count_t read_len;

    /* Clear variables */
    *length = 0;
    if (buffer) {
        *buffer = NULL;
    }

    /* We need file to read data from... but if it's missing, we don't read
       anything and this is not considered an error */
    if (!self->priv->sndfile) {
        return TRUE;
    }

    /* Determine position within file */
    position = self->priv->offset + address*SNDFILE_FRAMES_PER_SECTOR;

    /* Length */
    *length = 2352; /* Always */

    /* Data */
    if (buffer) {
        guint8 *data_buffer = g_malloc0(2352);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: reading from position 0x%llX (frames)\n", __debug__, position);
        sf_seek(self->priv->sndfile, position, SEEK_SET);
        read_len = sf_readf_short(self->priv->sndfile, (short *)data_buffer, SNDFILE_FRAMES_PER_SECTOR);

        if (read_len != SNDFILE_FRAMES_PER_SECTOR) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to read %ld bytes of audio data!", read_len);
            g_free(data_buffer);
            return FALSE;
        }

        *buffer = data_buffer;
    }

    return TRUE;
}

static gboolean mirage_fragment_sndfile_read_subchannel_data (MirageFragment *_self G_GNUC_UNUSED, gint address G_GNUC_UNUSED, guint8 **buffer G_GNUC_UNUSED, gint *length, GError **error G_GNUC_UNUSED)
{
    /* Nothing to read */
    *length = 0;
    if (buffer) {
        *buffer = NULL;
    }

    return TRUE;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
static void mirage_fragment_sndfile_audio_fragment_init (MirageAudioFragmentInterface *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED(MirageFragmentSndfile,
                               mirage_fragment_sndfile,
                               MIRAGE_TYPE_FRAGMENT,
                               0,
                               G_IMPLEMENT_INTERFACE_DYNAMIC(MIRAGE_TYPE_AUDIO_FRAGMENT,
                                                             mirage_fragment_sndfile_audio_fragment_init));

void mirage_fragment_sndfile_type_register (GTypeModule *type_module)
{
    return mirage_fragment_sndfile_register_type(type_module);
}


static void mirage_fragment_sndfile_init (MirageFragmentSndfile *self)
{
    self->priv = MIRAGE_FRAGMENT_SNDFILE_GET_PRIVATE(self);

    mirage_fragment_generate_info(MIRAGE_FRAGMENT(self),
        "FRAGMENT-SNDFILE",
        "libsndfile Fragment"
    );

    self->priv->stream = NULL;
    self->priv->sndfile = NULL;
}

static void mirage_fragment_sndfile_dispose (GObject *gobject)
{
    MirageFragmentSndfile *self = MIRAGE_FRAGMENT_SNDFILE(gobject);

    if (self->priv->stream) {
        g_object_unref(self->priv->stream);
        self->priv->stream = NULL;
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_fragment_sndfile_parent_class)->dispose(gobject);
}

static void mirage_fragment_sndfile_finalize (GObject *gobject)
{
    MirageFragmentSndfile *self = MIRAGE_FRAGMENT_SNDFILE(gobject);

    if (self->priv->sndfile) {
        sf_close(self->priv->sndfile);
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_fragment_sndfile_parent_class)->finalize(gobject);
}

static void mirage_fragment_sndfile_class_init (MirageFragmentSndfileClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageFragmentClass *fragment_class = MIRAGE_FRAGMENT_CLASS(klass);

    gobject_class->dispose = mirage_fragment_sndfile_dispose;
    gobject_class->finalize = mirage_fragment_sndfile_finalize;

    fragment_class->can_handle_data_format = mirage_fragment_sndfile_can_handle_data_format;
    fragment_class->use_the_rest_of_file = mirage_fragment_sndfile_use_the_rest_of_file;
    fragment_class->read_main_data = mirage_fragment_sndfile_read_main_data;
    fragment_class->read_subchannel_data = mirage_fragment_sndfile_read_subchannel_data;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageFragmentSndfilePrivate));
}

static void mirage_fragment_sndfile_class_finalize (MirageFragmentSndfileClass *klass G_GNUC_UNUSED)
{
}


static void mirage_fragment_sndfile_audio_fragment_init (MirageAudioFragmentInterface *iface)
{
    iface->set_stream = mirage_fragment_sndfile_set_stream;
    iface->get_filename = mirage_fragment_sndfile_get_filename;
    iface->set_offset = mirage_fragment_sndfile_set_offset;
    iface->get_offset = mirage_fragment_sndfile_get_offset;
}
