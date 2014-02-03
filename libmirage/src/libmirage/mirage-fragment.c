/*
 *  libMirage: Fragment object
 *  Copyright (C) 2006-2012 Rok Mandeljc
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
 * SECTION: mirage-fragment
 * @title: MirageFragment
 * @short_description: Fragment object.
 * @include: mirage-fragment.h
 *
 * #MirageFragment object represents an interface between a #MirageTrack
 * and data stream(s) containing the data. It allows tracks to read main
 * and subchannel data from the streams, which is then fed to sectors.
 * When constructing a track, a parser will typically create and append
 * at least one fragment.
 *
 * A #MirageFragment object is obtained using g_object_new() function.
 * Both main data file stream and subchannel data file stream can be set
 * using mirage_fragment_main_data_set_stream() and mirage_fragment_subchannel_data_set_stream()
 * functions. If no streams are set, a fragment acts as a "NULL" fragment,
 * and can be used to represent zero-filled pregaps and postgaps in
 * tracks.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"

#define __debug__ "Fragment"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_FRAGMENT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FRAGMENT, MirageFragmentPrivate))

struct _MirageFragmentPrivate
{
    gint address; /* Address (relative to track start) */
    gint length; /* Length, in sectors */

    MirageStream *main_stream; /* Main data stream */
    gint main_size; /* Main data sector size */
    gint main_format; /* Main data format */
    guint64 main_offset; /* Offset in main data file */

    MirageStream *subchannel_stream; /* Subchannel data stream */
    gint subchannel_size; /* Subchannel data sector size*/
    gint subchannel_format; /* Subchannel data format */
    guint64 subchannel_offset; /* Offset in subchannel data file */
};


/**********************************************************************\
 *                          Private functions                         *
\**********************************************************************/
static void mirage_fragment_commit_topdown_change (MirageFragment *self G_GNUC_UNUSED)
{
    /* Nothing to do here */
}

static void mirage_fragment_commit_bottomup_change (MirageFragment *self)
{
    /* Signal fragment change */
    g_signal_emit_by_name(self, "layout-changed", NULL);
}


/**********************************************************************\
 *                      Address/length functions                      *
\**********************************************************************/
/**
 * mirage_fragment_set_address:
 * @self: a #MirageFragment
 * @address: (in): start address
 *
 * Sets fragment's start address. The @address is given in sectors.
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * <note>
 * Causes top-down change.
 * </note>
 */
void mirage_fragment_set_address (MirageFragment *self, gint address)
{
    /* Set address */
    self->priv->address = address;
    /* Top-down change */
    mirage_fragment_commit_topdown_change(self);
}

/**
 * mirage_fragment_get_address:
 * @self: a #MirageFragment
 *
 * Retrieves fragment's start address. The @address is given in sectors.
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: start address
 */
gint mirage_fragment_get_address (MirageFragment *self)
{
    /* Return address */
    return self->priv->address;
}

/**
 * mirage_fragment_set_length:
 * @self: a #MirageFragment
 * @length: (in): length
 *
 * Sets fragment's length. The @length is given in sectors.
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 */
void mirage_fragment_set_length (MirageFragment *self, gint length)
{
    /* Set length */
    self->priv->length = length;
    /* Bottom-up change */
    mirage_fragment_commit_bottomup_change(self);
}

/**
 * mirage_fragment_get_length:
 * @self: a #MirageFragment
 *
 * Retrieves fragment's length. The returned @length is given in sectors.
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: length
 */
gint mirage_fragment_get_length (MirageFragment *self)
{
    /* Return length */
    return self->priv->length;
}


/**
 * mirage_fragment_use_the_rest_of_file:
 * @self: a #MirageFragment
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Uses the rest of data file. It automatically calculates and sets fragment's
 * length.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_fragment_use_the_rest_of_file (MirageFragment *self, GError **error)
{
    GError *local_error = NULL;

    goffset file_size;
    gint full_size;
    gint fragment_len;

    if (!self->priv->main_stream) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: main channel data input stream not set!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Main channel data input stream not set!");
        return FALSE;
    }

    /* Get file length */
    if (!mirage_stream_seek(self->priv->main_stream, 0, G_SEEK_END, &local_error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to the end of main channel data input stream: %s\n", __debug__, local_error->message);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to seek to the end of main channel data input stream: %s", local_error->message);
        g_error_free(local_error);
        return FALSE;
    }
    file_size = mirage_stream_tell(self->priv->main_stream);

    /* Use all data from offset on... */
    full_size = self->priv->main_size;
    if (self->priv->subchannel_format & MIRAGE_SUBCHANNEL_DATA_FORMAT_INTERNAL) {
        full_size += self->priv->subchannel_size;
    }

    fragment_len = (file_size - self->priv->main_offset) / full_size;
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: using the rest of file (%d sectors)\n", __debug__, fragment_len);

    /* Set the length */
    mirage_fragment_set_length(MIRAGE_FRAGMENT(self), fragment_len);
    return TRUE;
}


/**
 * mirage_fragment_contains_address:
 * @self: a #MirageFragment
 * @address: address to be checked
 *
 * Checks whether the fragment contains the given address or not.
 *
 * Returns: %TRUE if @address falls inside fragment, %FALSE if it does not
 */
gboolean mirage_fragment_contains_address (MirageFragment *self, gint address)
{
    return address >= self->priv->address && address < self->priv->address + self->priv->length;
}


/**********************************************************************\
 *                           Main data functions                      *
\**********************************************************************/
/**
 * mirage_fragment_main_data_set_stream:
 * @self: a #MirageFragment
 * @stream: (in) (transfer full): a #MirageStream on main channel data file
 *
 * Sets main channel data stream.
 */
void mirage_fragment_main_data_set_stream (MirageFragment *self, MirageStream *stream)
{
    /* Release old stream */
    if (self->priv->main_stream) {
        g_object_unref(self->priv->main_stream);
        self->priv->main_stream = NULL;
    }

    /* Set new stream */
    self->priv->main_stream = g_object_ref(stream);
}

/**
 * mirage_fragment_main_data_get_filename:
 * @self: a #MirageFragment
 *
 * Retrieves filename of main channel data stream.
 *
 * Returns: (transfer none): pointer to main channel data file name string.
 * The string belongs to object and should not be modified.
 */
const gchar *mirage_fragment_main_data_get_filename (MirageFragment *self)
{
    /* Return stream's file name */
    if (!self->priv->main_stream) {
        return NULL;
    }
    return mirage_stream_get_filename(self->priv->main_stream);
}

/**
 * mirage_fragment_main_data_set_offset:
 * @self: a #MirageFragment
 * @offset: (in): main data file offset
 *
 * Sets main data file offset.
 */
void mirage_fragment_main_data_set_offset (MirageFragment *self, guint64 offset)
{
    /* Set offset */
    self->priv->main_offset = offset;
}

/**
 * mirage_fragment_main_data_get_offset:
 * @self: a #MirageFragment
 *
 * Retrieves main data file offset.
 *
 * Returns: main data file offset
 */
guint64 mirage_fragment_main_data_get_offset (MirageFragment *self)
{
    /* Return offset */
    return self->priv->main_offset;
}

/**
 * mirage_fragment_main_data_set_size:
 * @self: a #MirageFragment
 * @size: (in): main data file sector size
 *
 * Sets main data file sector size.
 */
void mirage_fragment_main_data_set_size (MirageFragment *self, gint size)
{
    /* Set sector size */
    self->priv->main_size = size;
}

/**
 * mirage_fragment_main_data_get_size:
 * @self: a #MirageFragment
 *
 * Retrieves main data file sector size.
 *
 * Returns: main data file sector size
 */
gint mirage_fragment_main_data_get_size (MirageFragment *self)
{
    /* Return sector size */
    return self->priv->main_size;
}

/**
 * mirage_fragment_main_data_set_format:
 * @self: a #MirageFragment
 * @format: (in): main data file format
 *
 * Sets main data file format. @format must be one of #MirageMainDataFormat.
 */
void mirage_fragment_main_data_set_format (MirageFragment *self, gint format)
{
    /* Set format */
    self->priv->main_format = format;
}

/**
 * mirage_fragment_main_data_get_format:
 * @self: a #MirageFragment
 *
 * Retrieves main data file format.
 *
 * Returns: main data file format
 */
gint mirage_fragment_main_data_get_format (MirageFragment *self)
{
    /* Return format */
    return self->priv->main_format;
}

/**
 * mirage_fragment_main_data_get_position:
 * @self: a #MirageFragment
 * @address: (in): address
 *
 * Calculates position of data for sector at address @address within
 * main data file and stores it in @position.
 *
 * Returns: position in main data file
 */
static guint64 mirage_fragment_main_data_get_position (MirageFragment *self, gint address)
{
    gint size_full;

    /* Calculate 'full' sector size:
        -> track data + subchannel data, if there's internal subchannel
        -> track data, if there's external or no subchannel
    */

    size_full = self->priv->main_size;
    if (self->priv->subchannel_format & MIRAGE_SUBCHANNEL_DATA_FORMAT_INTERNAL) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: internal subchannel, adding %d to sector size %d\n", __debug__, self->priv->subchannel_size, size_full);
        size_full += self->priv->subchannel_size;
    }

    /* We assume address is relative address */
    /* guint64 casts are required so that the product us 64-bit; product of two
       32-bit integers would be 32-bit, which would be truncated at overflow... */
    return self->priv->main_offset + (guint64)address * (guint64)size_full;
}


/**
 * mirage_fragment_read_main_data:
 * @self: a #MirageFragment
 * @address: (in): address
 * @buffer: (out) (allow-none) (array length=length): location to store pointer to buffer with read data, or %NULL
 * @length: (out): location to store read data length
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Reads main channel data for sector at fragment-relative @address (given
 * in sectors). The buffer for reading data into is allocated by function
 * and should be freed using g_free() when no longer needed. The pointer
 * to buffer is stored into @buffer and the length of read data is stored into
 * @length.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_fragment_read_main_data (MirageFragment *self, gint address, guint8 **buffer, gint *length, GError **error G_GNUC_UNUSED)
{
    guint64 position;
    gint read_len;

    /* Clear both variables */
    *length = 0;
    if (buffer) {
        *buffer = NULL;
    }

    /* We need a stream to read data from... but if it's missing, we
       don't read anything and this is not considered an error */
    if (!self->priv->main_stream) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: no main channel data input stream!\n", __debug__);
        return TRUE;
    }

    /* Determine position within file */
    position = mirage_fragment_main_data_get_position(self, address);

    /* Length */
    *length = self->priv->main_size;

    /* Data */
    if (buffer) {
        guint8 *data_buffer = g_malloc0(self->priv->main_size);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: reading from position 0x%llX\n", __debug__, position);

        /* Note: we ignore all errors here in order to be able to cope with truncated mini images */
        mirage_stream_seek(self->priv->main_stream, position, G_SEEK_SET, NULL);
        read_len = mirage_stream_read(self->priv->main_stream, data_buffer, self->priv->main_size, NULL);

        /*if (read_len != self->priv->main_size) {
            mirage_error(MIRAGE_E_READFAILED, error);
            g_free(data_buffer);
            return FALSE;
        }*/

        /* Binary audio files may need to be swapped from BE to LE */
        if (self->priv->main_format == MIRAGE_MAIN_DATA_FORMAT_AUDIO_SWAP) {
            for (gint i = 0; i < read_len; i+=2) {
                guint16 *ptr = (guint16 *)&data_buffer[i];
                *ptr = GUINT16_SWAP_LE_BE(*ptr);
            }
        }

        *buffer = data_buffer;
    }

    return TRUE;
}


gboolean mirage_fragment_write_main_data (MirageFragment *self, gint address, const guint8 *buffer, gint length, GError **error)
{
    guint64 position;
    GError *local_error = NULL;

    /* If there is no data to be written, do nothing */
    if (!length || !buffer) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: no data to be written!\n", __debug__);
        return TRUE;
    }

    /* Validate the size of data we are given with our set main channel
       data size */
    if (length != self->priv->main_size) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: mismatch between given data (%d) and set main channel data size (%d)!\n", __debug__, length, self->priv->main_size);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Mismatch between given data (%d) and set main channel data size (%d)!\n", length, self->priv->main_size);
        return FALSE;
    }

    /* We need a stream to write data... but if it's missing, we don't
       write anything and this is not considered an error */
    if (!self->priv->main_stream) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: no main channel data output stream!\n", __debug__);
        return TRUE;
    }

    /* Binary audio files may need to be swapped from BE to LE */
    if (self->priv->main_format == MIRAGE_MAIN_DATA_FORMAT_AUDIO_SWAP) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: FIXME: audio data swapping on write not implemented yet!\n", __debug__);
        /*for (gint i = 0; i < self->priv->main_size; i += 2) {
            guint16 *ptr = (guint16 *)&buffer[i];
            *ptr = GUINT16_SWAP_LE_BE(*ptr);
        }*/
    }

    /* Determine position within file */
    position = mirage_fragment_main_data_get_position(self, address);

    /* Write */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: writing %d bytes at position 0x%llX\n", __debug__, self->priv->main_size, position);

    mirage_stream_seek(self->priv->main_stream, position, G_SEEK_SET, NULL);
    if (mirage_stream_tell(self->priv->main_stream) != position) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: failed to seek to position 0x%zX\n", __debug__, position);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to seek to position 0x%zX\n", position);
        return FALSE;
    }

    if (mirage_stream_write(self->priv->main_stream, buffer, self->priv->main_size, &local_error) != self->priv->main_size) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: failed to write data: %s\n", __debug__, local_error->message);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to write data: %s\n", local_error->message);
        g_error_free(local_error);
        return FALSE;
    }

    return TRUE;
}


/**********************************************************************\
 *                        Subchannel data functions                   *
\**********************************************************************/
/**
 * mirage_fragment_subchannel_data_set_stream:
 * @self: a #MirageFragment
 * @stream: (in) (transfer full): a #MirageStream on subchannel data file
 *
 * Sets subchannel data stream.
 */
void mirage_fragment_subchannel_data_set_stream (MirageFragment *self, MirageStream *stream)
{
    /* Release old stream */
    if (self->priv->subchannel_stream) {
        g_object_unref(self->priv->subchannel_stream);
        self->priv->subchannel_stream = NULL;
    }

    /* Set new stream */
    self->priv->subchannel_stream = g_object_ref(stream);
}

/**
 * mirage_fragment_subchannel_data_get_filename:
 * @self: a #MirageFragment
 *
 * Retrieves filename of subchannel data input stream.
 *
 * Returns: (transfer none): pointer to subchannel data file name string.
 * The string belongs to object and should not be modified.
 */
const gchar *mirage_fragment_subchannel_data_get_filename (MirageFragment *self)
{
    /* Return stream's file name */
    if (!self->priv->subchannel_stream) {
        return NULL;
    }
    return mirage_stream_get_filename(self->priv->subchannel_stream);
}

/**
 * mirage_fragment_subchannel_data_set_offset:
 * @self: a #MirageFragment
 * @offset: (in): subchannel data file offset
 *
 * Sets subchannel data file offset.
 */
void mirage_fragment_subchannel_data_set_offset (MirageFragment *self, guint64 offset)
{
    /* Set offset */
    self->priv->subchannel_offset = offset;
}

/**
 * mirage_fragment_subchannel_data_get_offset:
 * @self: a #MirageFragment
 *
 * Retrieves subchannel data file offset.
 *
 * Returns: subchannel data file offset
 */
guint64 mirage_fragment_subchannel_data_get_offset (MirageFragment *self)
{
    /* Return offset */
    return self->priv->subchannel_offset;
}

/**
 * mirage_fragment_subchannel_data_set_size:
 * @self: a #MirageFragment
 * @size: (in): subchannel data file sector size
 *
 * Sets subchannel data file sector size.
 */
void mirage_fragment_subchannel_data_set_size (MirageFragment *self, gint size)
{
    /* Set sector size */
    self->priv->subchannel_size = size;
}

/**
 * mirage_fragment_subchannel_data_get_size:
 * @self: a #MirageFragment
 *
 * Retrieves subchannel data file sector size.
 *
 * Returns: subchannel data file sector size
 */
gint mirage_fragment_subchannel_data_get_size (MirageFragment *self)
{
    /* Return sector size */
    return self->priv->subchannel_size;
}

/**
 * mirage_fragment_subchannel_data_set_format:
 * @self: a #MirageFragment
 * @format: (in): subchannel data file format
 *
 * Sets subchannel data file format. @format must be a combination of
 * #MirageSubchannelDataFormat.
 */
void mirage_fragment_subchannel_data_set_format (MirageFragment *self, gint format)
{
    /* Set format */
    self->priv->subchannel_format = format;
}

/**
 * mirage_fragment_subchannel_data_get_format:
 * @self: a #MirageFragment
 *
 * Retrieves subchannel data file format.
 *
 * Returns: subchannel data file format
 */
gint mirage_fragment_subchannel_data_get_format (MirageFragment *self)
{
    /* Return format */
    return self->priv->subchannel_format;
}

/**
 * mirage_fragment_subchannel_data_get_position:
 * @self: a #MirageFragment
 * @address: (in): address
 *
 * Calculates position of data for sector at address @address within
 * subchannel data file and stores it in @position.
 *
 * Returns: position in subchannel data file
 */
static guint64 mirage_fragment_subchannel_data_get_position (MirageFragment *self, gint address)
{
    guint64 offset = 0;

    /* Either we have internal or external subchannel */
    if (self->priv->subchannel_format & MIRAGE_SUBCHANNEL_DATA_FORMAT_INTERNAL) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: internal subchannel, position is at end of main channel data\n", __debug__);
        /* Subchannel is contained in track file; get position in track file
           for that sector, and add to it length of track data sector */
        offset = mirage_fragment_main_data_get_position(self, address);
        offset += self->priv->main_size;
    } else if (self->priv->subchannel_format & MIRAGE_SUBCHANNEL_DATA_FORMAT_EXTERNAL) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: external subchannel, calculating position\n", __debug__);
        /* We assume address is relative address */
        /* guint64 casts are required so that the product us 64-bit; product of two
           32-bit integers would be 32-bit, which would be truncated at overflow... */
        offset = self->priv->subchannel_offset + (guint64)address * (guint64)self->priv->subchannel_size;
    }

    return offset;
}

/**
 * mirage_fragment_read_subchannel_data:
 * @self: a #MirageFragment
 * @address: (in): address
 * @buffer: (out) (allow-none) (array length=length): location to store pointer to buffer with read data, or %NULL
 * @length: (out): location to store read data length
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Reads subchannel data for sector at fragment-relative @address (given
 * in sectors). The buffer for reading data into is allocated by function
 * and should be freed using g_free() when no longer needed. The pointer
 * to buffer is stored into @buffer and the length of read data is stored into
 * @length.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_fragment_read_subchannel_data (MirageFragment *self, gint address, guint8 **buffer, gint *length, GError **error G_GNUC_UNUSED)
{
    MirageStream *stream;
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

    /* We need a stream to read data from... but if it's missing, we
       don't read anything and this is not considered an error */
    if (self->priv->subchannel_format & MIRAGE_SUBCHANNEL_DATA_FORMAT_INTERNAL) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: internal subchannel, using main channel stream\n", __debug__);
        stream = self->priv->main_stream;
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: external subchannel, using subchannel stream\n", __debug__);
        stream = self->priv->subchannel_stream;
    }

    if (!stream) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: no input stream!\n", __debug__);
        return TRUE;
    }


    /* Determine position within file */
    position = mirage_fragment_subchannel_data_get_position(self, address);


    /* Length */
    *length = 96; /* Always 96, because we do the processing here */

    /* Data */
    if (buffer) {
        guint8 *data_buffer = g_malloc0(96);
        guint8 *raw_buffer = g_malloc0(self->priv->subchannel_size);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: reading from position 0x%llX\n", __debug__, position);
        /* We read into temporary buffer, because we might need to perform some
           magic on the data */
        mirage_stream_seek(stream, position, G_SEEK_SET, NULL);
        read_len = mirage_stream_read(stream, raw_buffer, self->priv->subchannel_size, NULL);

        if (read_len != self->priv->subchannel_size) {
            /*mirage_error(MIRAGE_E_READFAILED, error);
            g_free(raw_buffer);
            g_free(data_buffer);
            return FALSE;*/
        }

        /* If we happen to deal with anything that's not RAW 96-byte interleaved PW,
           we transform it into that here... less fuss for upper level stuff this way */
        if (self->priv->subchannel_format & MIRAGE_SUBCHANNEL_DATA_FORMAT_PW96_LINEAR) {
            /* 96-byte deinterleaved PW; grab each subchannel and interleave it
               into destination buffer */
            for (gint i = 0; i < 8; i++) {
                mirage_helper_subchannel_interleave(7 - i, raw_buffer + i*12, data_buffer);
            }
        } else if (self->priv->subchannel_format & MIRAGE_SUBCHANNEL_DATA_FORMAT_PW96_INTERLEAVED) {
            /* 96-byte interleaved PW; just copy it */
            memcpy(data_buffer, raw_buffer, 96);
        } else if (self->priv->subchannel_format & MIRAGE_SUBCHANNEL_DATA_FORMAT_Q16) {
            /* 16-byte Q; interleave it and pretend everything else's 0 */
            mirage_helper_subchannel_interleave(SUBCHANNEL_Q, raw_buffer, data_buffer);
        }

        g_free(raw_buffer);

        *buffer = data_buffer;
    }

    return TRUE;
}

gboolean mirage_fragment_write_subchannel_data (MirageFragment *self, gint address, const guint8 *buffer, gint length, GError **error)
{
    MirageStream *stream;
    guint64 position;
    GError *local_error = NULL;

    /* If there is no data to be written, do nothing */
    if (!length || !buffer) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: no data to be written!\n", __debug__);
        return TRUE;
    }

    /* Validate the size of data we are given with 96, which is what
       we accept here */
    if (length != 96) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: mismatch between given data (%d) and accepted subchannel size (%d)!\n", __debug__, length, 96);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Mismatch between given data (%d) and accepted subchannel size (%d)!\n", length, 96);
        return FALSE;
    }

    /* We need a stream to write data to... but if it's missing, we
       don't write anything and this is not considered an error */
    if (self->priv->subchannel_format & MIRAGE_SUBCHANNEL_DATA_FORMAT_INTERNAL) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: internal subchannel, using main channel stream\n", __debug__);
        stream = self->priv->main_stream;
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: external subchannel, using subchannel stream\n", __debug__);
        stream = self->priv->subchannel_stream;
    }

    if (!stream) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: no output stream!\n", __debug__);
        return TRUE;
    }

    /* Convert subchannel if necessary */
    if (!(self->priv->subchannel_format & MIRAGE_SUBCHANNEL_DATA_FORMAT_PW96_INTERLEAVED)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: FIXME: subchannel data conversion on write not implemented yet!\n", __debug__);
    }

    /* Determine position within file */
    position = mirage_fragment_subchannel_data_get_position(self, address);

    /* Write */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: writing %d bytes at position 0x%llX\n", __debug__, self->priv->subchannel_size, position);

    mirage_stream_seek(stream, position, G_SEEK_SET, NULL);
    if (mirage_stream_tell(stream) != position) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: failed to seek to position 0x%zX\n", __debug__, position);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to seek to position 0x%zX\n", position);
        return FALSE;
    }

    if (mirage_stream_write(stream, buffer, self->priv->subchannel_size, &local_error) != self->priv->subchannel_size) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: failed to write data: %s\n", __debug__, local_error->message);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to write data: %s\n", local_error->message);
        g_error_free(local_error);
        return FALSE;
    }

    return TRUE;
}


gboolean mirage_fragment_is_writable (MirageFragment *self)
{
    return (self->priv->main_stream != NULL && mirage_stream_is_writable(self->priv->main_stream));
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE(MirageFragment, mirage_fragment, MIRAGE_TYPE_OBJECT);


static void mirage_fragment_init (MirageFragment *self)
{
    self->priv = MIRAGE_FRAGMENT_GET_PRIVATE(self);

    self->priv->main_stream = NULL;
    self->priv->main_size = 0;
    self->priv->main_format = 0;
    self->priv->main_offset = 0;

    self->priv->subchannel_stream = NULL;
    self->priv->subchannel_size = 0;
    self->priv->subchannel_format = 0;
    self->priv->subchannel_offset = 0;
}

static void mirage_fragment_dispose (GObject *gobject)
{
    MirageFragment*self = MIRAGE_FRAGMENT(gobject);

    if (self->priv->main_stream) {
        g_object_unref(self->priv->main_stream);
        self->priv->main_stream = NULL;
    }

    if (self->priv->subchannel_stream) {
        g_object_unref(self->priv->subchannel_stream);
        self->priv->subchannel_stream = NULL;
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_fragment_parent_class)->dispose(gobject);
}

static void mirage_fragment_class_init (MirageFragmentClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = mirage_fragment_dispose;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageFragmentPrivate));

    /* Signals */
    /**
     * MirageFragment::layout-changed:
     * @fragment: a #MirageFragment
     *
     * Emitted when a layout of #MirageFragment changed in a way that causes a bottom-up change.
     */
    g_signal_new("layout-changed", G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, NULL);
}
