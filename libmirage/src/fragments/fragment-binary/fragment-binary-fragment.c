/*
 *  libMirage: BINARY fragment: Fragment object
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

#include "fragment-binary.h"

#define __debug__ "BINARY-Fragment"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_FRAGMENT_BINARY_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FRAGMENT_BINARY, MirageFragmentBinaryPrivate))

struct _MirageFragmentBinaryPrivate
{
    GObject *main_stream; /* Main data stream */
    gint main_size; /* Main data sector size */
    gint main_format; /* Main data format */
    guint64 main_offset; /* Offset in main data file */

    GObject *subchannel_stream; /* Subchannel data stream */
    gint subchannel_size; /* Subchannel data sector size*/
    gint subchannel_format; /* Subchannel data format */
    guint64 subchannel_offset; /* Offset in subchannel data file */
};


/**********************************************************************\
 *                     Binary Interface implementation                *
\**********************************************************************/
static void mirage_fragment_binary_main_data_set_stream (MirageFragmentIfaceBinary *_self, GObject *stream)
{
    MirageFragmentBinary *self = MIRAGE_FRAGMENT_BINARY(_self);

    /* Release old stream */
    if (self->priv->main_stream) {
        g_object_unref(self->priv->main_stream);
        self->priv->main_stream = NULL;
    }

    /* Set new stream */
    self->priv->main_stream = stream;
    g_object_ref(stream);
}

static const gchar *mirage_fragment_binary_main_data_get_filename (MirageFragmentIfaceBinary *_self)
{
    MirageFragmentBinary *self = MIRAGE_FRAGMENT_BINARY(_self);
    /* Return file name */
    return mirage_get_file_stream_filename(self->priv->main_stream);
}


static void mirage_fragment_binary_main_data_set_offset (MirageFragmentIfaceBinary *_self, guint64 offset)
{
    MirageFragmentBinary *self = MIRAGE_FRAGMENT_BINARY(_self);
    /* Set offset */
    self->priv->main_offset = offset;
}

static guint64 mirage_fragment_binary_main_data_get_offset (MirageFragmentIfaceBinary *_self)
{
    MirageFragmentBinary *self = MIRAGE_FRAGMENT_BINARY(_self);
    /* Return offset */
    return self->priv->main_offset;
}


static void mirage_fragment_binary_main_data_set_size (MirageFragmentIfaceBinary *_self, gint size)
{
    MirageFragmentBinary *self = MIRAGE_FRAGMENT_BINARY(_self);
    /* Set sector size */
    self->priv->main_size = size;
}

static gint mirage_fragment_binary_main_data_get_size (MirageFragmentIfaceBinary *_self)
{
    MirageFragmentBinary *self = MIRAGE_FRAGMENT_BINARY(_self);
    /* Return sector size */
    return self->priv->main_size;
}


static void mirage_fragment_binary_main_data_set_format (MirageFragmentIfaceBinary *_self, gint format)
{
    MirageFragmentBinary *self = MIRAGE_FRAGMENT_BINARY(_self);
    /* Set format */
    self->priv->main_format = format;
}

static gint mirage_fragment_binary_main_data_get_format (MirageFragmentIfaceBinary *_self)
{
    MirageFragmentBinary *self = MIRAGE_FRAGMENT_BINARY(_self);
    /* Return format */
    return self->priv->main_format;
}


static guint64 mirage_fragment_binary_main_data_get_position (MirageFragmentIfaceBinary *_self, gint address)
{
    MirageFragmentBinary *self = MIRAGE_FRAGMENT_BINARY(_self);
    gint size_full;

    /* Calculate 'full' sector size:
        -> track data + subchannel data, if there's internal subchannel
        -> track data, if there's external or no subchannel
    */

    size_full = self->priv->main_size;
    if (self->priv->subchannel_format & MIRAGE_SUBCHANNEL_INT) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: internal subchannel, adding %d to sector size %d\n", __debug__, self->priv->subchannel_size, size_full);
        size_full += self->priv->subchannel_size;
    }

    /* We assume address is relative address */
    /* guint64 casts are required so that the product us 64-bit; product of two
       32-bit integers would be 32-bit, which would be truncated at overflow... */
    return self->priv->main_offset + (guint64)address * (guint64)size_full;
}


static void mirage_fragment_binary_subchannel_data_set_stream (MirageFragmentIfaceBinary *_self, GObject *stream)
{
    MirageFragmentBinary *self = MIRAGE_FRAGMENT_BINARY(_self);

    /* Release old stream */
    if (self->priv->subchannel_stream) {
        g_object_unref(self->priv->subchannel_stream);
        self->priv->subchannel_stream = NULL;
    }

    /* Set new stream */
    self->priv->subchannel_stream = stream;
    g_object_ref(stream);
}

static const gchar *mirage_fragment_binary_subchannel_data_get_filename (MirageFragmentIfaceBinary *_self)
{
    MirageFragmentBinary *self = MIRAGE_FRAGMENT_BINARY(_self);
    /* Return file name */
    return mirage_get_file_stream_filename(self->priv->subchannel_stream);
}


static void mirage_fragment_binary_subchannel_data_set_offset (MirageFragmentIfaceBinary *_self, guint64 offset)
{
    MirageFragmentBinary *self = MIRAGE_FRAGMENT_BINARY(_self);
    /* Set offset */
    self->priv->subchannel_offset = offset;
}

static guint64 mirage_fragment_binary_subchannel_data_get_offset (MirageFragmentIfaceBinary *_self)
{
    MirageFragmentBinary *self = MIRAGE_FRAGMENT_BINARY(_self);
    /* Return offset */
    return self->priv->subchannel_offset;
}

static void mirage_fragment_binary_subchannel_data_set_size (MirageFragmentIfaceBinary *_self, gint size)
{
    MirageFragmentBinary *self = MIRAGE_FRAGMENT_BINARY(_self);
    /* Set sector size */
    self->priv->subchannel_size = size;
}

static gint mirage_fragment_binary_subchannel_data_get_size (MirageFragmentIfaceBinary *_self)
{
    MirageFragmentBinary *self = MIRAGE_FRAGMENT_BINARY(_self);
    /* Return sector size */
    return self->priv->subchannel_size;
}


static void mirage_fragment_binary_subchannel_data_set_format (MirageFragmentIfaceBinary *_self, gint format)
{
    MirageFragmentBinary *self = MIRAGE_FRAGMENT_BINARY(_self);
    /* Set format */
    self->priv->subchannel_format = format;
}

static gint mirage_fragment_binary_subchannel_data_get_format (MirageFragmentIfaceBinary *_self)
{
    MirageFragmentBinary *self = MIRAGE_FRAGMENT_BINARY(_self);
    /* Return format */
    return self->priv->subchannel_format;
}


static guint64 mirage_fragment_binary_subchannel_data_get_position (MirageFragmentIfaceBinary *_self, gint address)
{
    MirageFragmentBinary *self = MIRAGE_FRAGMENT_BINARY(_self);
    guint64 offset = 0;

    /* Either we have internal or external subchannel */
    if (self->priv->subchannel_format & MIRAGE_SUBCHANNEL_INT) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: internal subchannel, position is at end of main channel data\n", __debug__);
        /* Subchannel is contained in track file; get position in track file
           for that sector, and add to it length of track data sector */
        offset = mirage_fragment_iface_binary_main_data_get_position(MIRAGE_FRAGMENT_IFACE_BINARY(self), address);
        offset += self->priv->main_size;
    } else if (self->priv->subchannel_format & MIRAGE_SUBCHANNEL_EXT) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: external subchannel, calculating position\n", __debug__);
        /* We assume address is relative address */
        /* guint64 casts are required so that the product us 64-bit; product of two
           32-bit integers would be 32-bit, which would be truncated at overflow... */
        offset = self->priv->subchannel_offset + (guint64)address * (guint64)self->priv->subchannel_size;
    }

    return offset;
}


/**********************************************************************\
 *               MirageFragment methods implementations              *
\**********************************************************************/
static gboolean mirage_fragment_binary_can_handle_data_format (MirageFragment *_self G_GNUC_UNUSED, GObject *stream, GError **error)
{
    /* Make sure stream is provided */
    if (!stream) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Fragment cannot handle given data!");
        return FALSE;
    }

    /* BINARY doesn't need any other data checks; what's important is interface type,
       which is filtered out elsewhere */
    return TRUE;
}

static gboolean mirage_fragment_binary_use_the_rest_of_file (MirageFragment *_self, GError **error)
{
    MirageFragmentBinary *self = MIRAGE_FRAGMENT_BINARY(_self);
    GError *local_error = NULL;

    goffset file_size;
    gint full_size;
    gint fragment_len;

    if (!self->priv->main_stream) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: data file stream not set!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Track data file stream not set!");
        return FALSE;
    }

    /* Get file length */
    if (!g_seekable_seek(G_SEEKABLE(self->priv->main_stream), 0, G_SEEK_END, NULL, &local_error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to the end of track data file stream: %s\n", __debug__, local_error->message);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to seek to the end of track data file stream: %s", local_error->message);
        g_error_free(local_error);
        return FALSE;
    }
    file_size = g_seekable_tell(G_SEEKABLE(self->priv->main_stream));

    /* Use all data from offset on... */
    full_size = self->priv->main_size;
    if (self->priv->subchannel_format & MIRAGE_SUBCHANNEL_INT) {
        full_size += self->priv->subchannel_size;
    }

    fragment_len = (file_size - self->priv->main_offset) / full_size;
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: using the rest of file (%d sectors)\n", __debug__, fragment_len);

    /* Set the length */
    mirage_fragment_set_length(MIRAGE_FRAGMENT(self), fragment_len);
    return TRUE;
}

static gboolean mirage_fragment_binary_read_main_data (MirageFragment *_self, gint address, guint8 **buffer, gint *length, GError **error G_GNUC_UNUSED)
{
    MirageFragmentBinary *self = MIRAGE_FRAGMENT_BINARY(_self);

    guint64 position;
    gint read_len;

    /* Clear both variables */
    *length = 0;
    if (buffer) {
        *buffer = NULL;
    }

    /* We need file to read data from... but if it's missing, we don't read
       anything and this is not considered an error */
    if (!self->priv->main_stream) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: no data file stream!\n", __debug__);
        return TRUE;
    }

    /* Determine position within file */
    position = mirage_fragment_iface_binary_main_data_get_position(MIRAGE_FRAGMENT_IFACE_BINARY(self), address);

    /* Length */
    *length = self->priv->main_size;

    /* Data */
    if (buffer) {
        guint8 *data_buffer = g_malloc0(self->priv->main_size);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: reading from position 0x%llX\n", __debug__, position);

        /* Note: we ignore all errors here in order to be able to cope with truncated mini images */
        g_seekable_seek(G_SEEKABLE(self->priv->main_stream), position, G_SEEK_SET, NULL, NULL);
        read_len = g_input_stream_read(G_INPUT_STREAM(self->priv->main_stream), data_buffer, self->priv->main_size, NULL, NULL);

        /*if (read_len != self->priv->main_size) {
            mirage_error(MIRAGE_E_READFAILED, error);
            g_fre(data_buffer);
            return FALSE;
        }*/

        /* Binary audio files may need to be swapped from BE to LE */
        if (self->priv->main_format == MIRAGE_MAIN_AUDIO_SWAP) {
            for (gint i = 0; i < read_len; i+=2) {
                guint16 *ptr = (guint16 *)&data_buffer[i];
                *ptr = GUINT16_SWAP_LE_BE(*ptr);
            }
        }

        *buffer = data_buffer;
    }

    return TRUE;
}

static gboolean mirage_fragment_binary_read_subchannel_data (MirageFragment *_self, gint address, guint8 **buffer, gint *length, GError **error G_GNUC_UNUSED)
{
    MirageFragmentBinary *self = MIRAGE_FRAGMENT_BINARY(_self);

    GObject *stream;
    guint64 position;
    gint read_len;

    /* Clear both variables */
    *length = 0;
    if (buffer) {
        *buffer = NULL;
    }

    /* If there's no subchannel, return 0 for the length */
    if (!self->priv->subchannel_size) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: no subchannel (size = 0)!\n", __debug__);
        return TRUE;
    }

    /* We need file to read data from... but if it's missing, we don't read
       anything and this is not considered an error */
    if (self->priv->subchannel_format & MIRAGE_SUBCHANNEL_INT) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: internal subchannel, using track file handle\n", __debug__);
        stream = self->priv->main_stream;
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: external subchannel, using track file handle\n", __debug__);
        stream = self->priv->subchannel_stream;
    }

    if (!stream) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: no stream!\n", __debug__);
        return TRUE;
    }


    /* Determine position within file */
    position = mirage_fragment_iface_binary_subchannel_data_get_position(MIRAGE_FRAGMENT_IFACE_BINARY(self), address);


    /* Length */
    *length = 96; /* Always 96, because we do the processing here */

    /* Data */
    if (buffer) {
        guint8 *data_buffer = g_malloc0(96);
        guint8 *raw_buffer = g_malloc0(self->priv->subchannel_size);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: reading from position 0x%llX\n", __debug__, position);
        /* We read into temporary buffer, because we might need to perform some
           magic on the data */
        g_seekable_seek(G_SEEKABLE(stream), position, G_SEEK_SET, NULL, NULL);
        read_len = g_input_stream_read(G_INPUT_STREAM(stream), raw_buffer, self->priv->subchannel_size, NULL, NULL);

        if (read_len != self->priv->subchannel_size) {
            /*mirage_error(MIRAGE_E_READFAILED, error);
            g_free(raw_buffer);
            g_free(data_buffer);
            return FALSE;*/
        }

        /* If we happen to deal with anything that's not RAW 96-byte interleaved PW,
           we transform it into that here... less fuss for upper level stuff this way */
        if (self->priv->subchannel_format & MIRAGE_SUBCHANNEL_PW96_LIN) {
            /* 96-byte deinterleaved PW; grab each subchannel and interleave it
               into destination buffer */
            for (gint i = 0; i < 8; i++) {
                mirage_helper_subchannel_interleave(7 - i, raw_buffer + i*12, data_buffer);
            }
        } else if (self->priv->subchannel_format & MIRAGE_SUBCHANNEL_PW96_INT) {
            /* 96-byte interleaved PW; just copy it */
            memcpy(data_buffer, raw_buffer, 96);
        } else if (self->priv->subchannel_format & MIRAGE_SUBCHANNEL_PQ16) {
            /* 16-byte PQ; interleave it and pretend everything else's 0 */
            mirage_helper_subchannel_interleave(SUBCHANNEL_Q, raw_buffer, data_buffer);
        }

        g_free(raw_buffer);

        *buffer = data_buffer;
    }

    return TRUE;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
static void mirage_fragment_binary_fragment_iface_binary_init (MirageFragmentIfaceBinaryInterface *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED(MirageFragmentBinary,
                               mirage_fragment_binary,
                               MIRAGE_TYPE_FRAGMENT,
                               0,
                               G_IMPLEMENT_INTERFACE_DYNAMIC(MIRAGE_TYPE_FRAGMENT_IFACE_BINARY,
                                                             mirage_fragment_binary_fragment_iface_binary_init));

void mirage_fragment_binary_type_register (GTypeModule *type_module)
{
    return mirage_fragment_binary_register_type(type_module);
}


static void mirage_fragment_binary_init (MirageFragmentBinary *self)
{
    self->priv = MIRAGE_FRAGMENT_BINARY_GET_PRIVATE(self);

    mirage_fragment_generate_info(MIRAGE_FRAGMENT(self),
        "FRAGMENT-BINARY",
        "Binary Fragment"
    );

    self->priv->main_stream = NULL;
    self->priv->subchannel_stream = NULL;
}

static void mirage_fragment_binary_dispose (GObject *gobject)
{
    MirageFragmentBinary *self = MIRAGE_FRAGMENT_BINARY(gobject);

    if (self->priv->main_stream) {
        g_object_unref(self->priv->main_stream);
        self->priv->main_stream = NULL;
    }
    if (self->priv->subchannel_stream) {
        g_object_unref(self->priv->subchannel_stream);
        self->priv->subchannel_stream = NULL;
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_fragment_binary_parent_class)->dispose(gobject);
}

static void mirage_fragment_binary_class_init (MirageFragmentBinaryClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageFragmentClass *fragment_class = MIRAGE_FRAGMENT_CLASS(klass);

    gobject_class->dispose = mirage_fragment_binary_dispose;

    fragment_class->can_handle_data_format = mirage_fragment_binary_can_handle_data_format;
    fragment_class->use_the_rest_of_file = mirage_fragment_binary_use_the_rest_of_file;
    fragment_class->read_main_data = mirage_fragment_binary_read_main_data;
    fragment_class->read_subchannel_data = mirage_fragment_binary_read_subchannel_data;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageFragmentBinaryPrivate));
}

static void mirage_fragment_binary_class_finalize (MirageFragmentBinaryClass *klass G_GNUC_UNUSED)
{
}


static void mirage_fragment_binary_fragment_iface_binary_init (MirageFragmentIfaceBinaryInterface *iface)
{
    iface->main_data_set_stream = mirage_fragment_binary_main_data_set_stream;
    iface->main_data_get_filename = mirage_fragment_binary_main_data_get_filename;
    iface->main_data_set_offset = mirage_fragment_binary_main_data_set_offset;
    iface->main_data_get_offset = mirage_fragment_binary_main_data_get_offset;
    iface->main_data_set_size = mirage_fragment_binary_main_data_set_size;
    iface->main_data_get_size = mirage_fragment_binary_main_data_get_size;
    iface->main_data_set_format = mirage_fragment_binary_main_data_set_format;
    iface->main_data_get_format = mirage_fragment_binary_main_data_get_format;

    iface->main_data_get_position = mirage_fragment_binary_main_data_get_position;

    iface->subchannel_data_set_stream = mirage_fragment_binary_subchannel_data_set_stream;
    iface->subchannel_data_get_filename = mirage_fragment_binary_subchannel_data_get_filename;
    iface->subchannel_data_set_offset = mirage_fragment_binary_subchannel_data_set_offset;
    iface->subchannel_data_get_offset = mirage_fragment_binary_subchannel_data_get_offset;
    iface->subchannel_data_set_size = mirage_fragment_binary_subchannel_data_set_size;
    iface->subchannel_data_get_size = mirage_fragment_binary_subchannel_data_get_size;
    iface->subchannel_data_set_format = mirage_fragment_binary_subchannel_data_set_format;
    iface->subchannel_data_get_format = mirage_fragment_binary_subchannel_data_get_format;

    iface->subchannel_data_get_position = mirage_fragment_binary_subchannel_data_get_position;
}
