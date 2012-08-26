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
#define MIRAGE_FRAGMENT_SNDFILE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FRAGMENT_SNDFILE, MIRAGE_Fragment_SNDFILEPrivate))

struct _MIRAGE_Fragment_SNDFILEPrivate
{
    gchar *filename;
    GObject *stream;

    SNDFILE *sndfile;
    SF_INFO format;
    sf_count_t offset;
};


/**********************************************************************\
 *                        libsndfile I/O bridge                        *
\**********************************************************************/
static sf_count_t sndfile_io_get_filelen (GObject *stream)
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

static sf_count_t sndfile_io_seek (sf_count_t offset, int whence, GObject *stream)
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

static sf_count_t sndfile_io_read (void *ptr, sf_count_t count, GObject *stream)
{
    return g_input_stream_read(G_INPUT_STREAM(stream), ptr, count, NULL, NULL);
}

static  sf_count_t sndfile_io_tell (GObject *stream)
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
static gboolean mirage_fragment_sndfile_set_file (MIRAGE_FragIface_Audio *_self, const gchar *filename, GObject *stream, GError **error)
{
    MIRAGE_Fragment_SNDFILE *self = MIRAGE_FRAGMENT_SNDFILE(_self);
    GError *local_error = NULL;

    /* If stream is already set, release it and reset format */
    if (self->priv->stream) {
        g_object_unref(self->priv->stream);
        self->priv->stream = NULL;
    }
    if (self->priv->sndfile) {
        sf_close(self->priv->sndfile);
        memset(&self->priv->format, 0, sizeof(self->priv->format));
    }
    if (self->priv->filename) {
        g_free(self->priv->filename);
        self->priv->filename = NULL;
    }

    /* Set new stream */
    if (stream) {
        /* Set the provided stream */
        self->priv->stream = stream;
        g_object_ref(stream);
    } else {
        /* Open new stream */
        self->priv->stream = libmirage_create_file_stream(filename, G_OBJECT(self), &local_error);
        if (!self->priv->stream) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to create file stream on audio file '%s': %s", filename, local_error->message);
            g_error_free(local_error);
            return FALSE;
        }
    }

    /* Open sndfile */
    self->priv->sndfile = sf_open_virtual(&sndfile_io_bridge, SFM_READ, &self->priv->format, self->priv->stream);
    if (!self->priv->sndfile) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to open audio file!");
        return FALSE;
    }

    self->priv->filename = g_strdup(filename);

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

    return TRUE;
}

static const gchar *mirage_fragment_sndfile_get_file (MIRAGE_FragIface_Audio *_self)
{
    MIRAGE_Fragment_SNDFILE *self = MIRAGE_FRAGMENT_SNDFILE(_self);
    /* Return filename */
    return self->priv->filename;
}

static void mirage_fragment_sndfile_set_offset (MIRAGE_FragIface_Audio *_self, gint offset)
{
    MIRAGE_Fragment_SNDFILE *self = MIRAGE_FRAGMENT_SNDFILE(_self);
    /* Set offset */
    self->priv->offset = offset*SNDFILE_FRAMES_PER_SECTOR;
}

static gint mirage_fragment_sndfile_get_offset (MIRAGE_FragIface_Audio *_self)
{
    MIRAGE_Fragment_SNDFILE *self = MIRAGE_FRAGMENT_SNDFILE(_self);
    /* Return */
    return self->priv->offset/SNDFILE_FRAMES_PER_SECTOR;
}


/**********************************************************************\
 *               MIRAGE_Fragment methods implementations              *
\**********************************************************************/
static gboolean mirage_fragment_sndfile_can_handle_data_format (MIRAGE_Fragment *_self G_GNUC_UNUSED, const gchar *filename, GError **error)
{
    SNDFILE *sndfile;
    SF_INFO format;

    format.format = 0;

    /* Make sure filename is given */
    if (!filename) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Fragment cannot handle given data!");
        return FALSE;
    }

    /* Try opening the file */
    sndfile = sf_open(filename, SFM_READ, &format);
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

static gboolean mirage_fragment_sndfile_use_the_rest_of_file (MIRAGE_Fragment *_self, GError **error)
{
    MIRAGE_Fragment_SNDFILE *self = MIRAGE_FRAGMENT_SNDFILE(_self);
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

static gboolean mirage_fragment_sndfile_read_main_data (MIRAGE_Fragment *_self, gint address, guint8 *buf, gint *length, GError **error)
{
    MIRAGE_Fragment_SNDFILE *self = MIRAGE_FRAGMENT_SNDFILE(_self);
    sf_count_t position;
    sf_count_t read_len;

    /* We need file to read data from... but if it's missing, we don't read
       anything and this is not considered an error */
    if (!self->priv->sndfile) {
        if (length) {
            *length = 0;
        }
        return TRUE;
    }

    /* Determine position within file */
    position = self->priv->offset + address*SNDFILE_FRAMES_PER_SECTOR;

    if (buf) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: reading from position 0x%llX (frames)\n", __debug__, position);
        sf_seek(self->priv->sndfile, position, SEEK_SET);
        read_len = sf_readf_short(self->priv->sndfile, (short *)buf, SNDFILE_FRAMES_PER_SECTOR);

        if (read_len != SNDFILE_FRAMES_PER_SECTOR) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to read %ld bytes of audio data!", read_len);
            return FALSE;
        }
    }

    if (length) {
        *length = 2352; /* Always */
    }

    return TRUE;
}

static gboolean mirage_fragment_sndfile_read_subchannel_data (MIRAGE_Fragment *_self G_GNUC_UNUSED, gint address G_GNUC_UNUSED, guint8 *buf G_GNUC_UNUSED, gint *length, GError **error G_GNUC_UNUSED)
{
    /* Nothing to read */
    if (length) {
        *length = 0;
    }
    return TRUE;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
static void mirage_fragment_sndfile_frag_iface_audio_init (MIRAGE_FragIface_AudioInterface *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED(MIRAGE_Fragment_SNDFILE,
                               mirage_fragment_sndfile,
                               MIRAGE_TYPE_FRAGMENT,
                               0,
                               G_IMPLEMENT_INTERFACE_DYNAMIC(MIRAGE_TYPE_FRAG_IFACE_AUDIO,
                                                             mirage_fragment_sndfile_frag_iface_audio_init));

void mirage_fragment_sndfile_type_register (GTypeModule *type_module)
{
    return mirage_fragment_sndfile_register_type(type_module);
}


static void mirage_fragment_sndfile_init (MIRAGE_Fragment_SNDFILE *self)
{
    self->priv = MIRAGE_FRAGMENT_SNDFILE_GET_PRIVATE(self);

    mirage_fragment_generate_fragment_info(MIRAGE_FRAGMENT(self),
        "FRAGMENT-SNDFILE",
        "libsndfile Fragment"
    );

    self->priv->filename = NULL;
    self->priv->sndfile = NULL;
}

static void mirage_fragment_sndfile_dispose (GObject *gobject)
{
    MIRAGE_Fragment_SNDFILE *self = MIRAGE_FRAGMENT_SNDFILE(gobject);

    if (self->priv->stream) {
        g_object_unref(self->priv->stream);
        self->priv->stream = 0;
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_fragment_sndfile_parent_class)->dispose(gobject);
}

static void mirage_fragment_sndfile_finalize (GObject *gobject)
{
    MIRAGE_Fragment_SNDFILE *self = MIRAGE_FRAGMENT_SNDFILE(gobject);

    g_free(self->priv->filename);
    if (self->priv->sndfile) {
        sf_close(self->priv->sndfile);
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_fragment_sndfile_parent_class)->finalize(gobject);
}

static void mirage_fragment_sndfile_class_init (MIRAGE_Fragment_SNDFILEClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MIRAGE_FragmentClass *fragment_class = MIRAGE_FRAGMENT_CLASS(klass);

    gobject_class->dispose = mirage_fragment_sndfile_dispose;
    gobject_class->finalize = mirage_fragment_sndfile_finalize;

    fragment_class->can_handle_data_format = mirage_fragment_sndfile_can_handle_data_format;
    fragment_class->use_the_rest_of_file = mirage_fragment_sndfile_use_the_rest_of_file;
    fragment_class->read_main_data = mirage_fragment_sndfile_read_main_data;
    fragment_class->read_subchannel_data = mirage_fragment_sndfile_read_subchannel_data;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Fragment_SNDFILEPrivate));
}

static void mirage_fragment_sndfile_class_finalize (MIRAGE_Fragment_SNDFILEClass *klass G_GNUC_UNUSED)
{
}


static void mirage_fragment_sndfile_frag_iface_audio_init (MIRAGE_FragIface_AudioInterface *iface)
{
    iface->set_file = mirage_fragment_sndfile_set_file;
    iface->get_file = mirage_fragment_sndfile_get_file;
    iface->set_offset = mirage_fragment_sndfile_set_offset;
    iface->get_offset = mirage_fragment_sndfile_get_offset;
}
