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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
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
    MirageFragmentInfo info;

    gint address; /* Address (relative to track start) */
    gint length; /* Length, in sectors */
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
 *                             Public API                             *
\**********************************************************************/
/**
 * mirage_fragment_generate_info:
 * @self: a #MirageFragment
 * @id: (in): fragment ID
 * @name: (in): fragment name
 *
 * <para>
 * Generates fragment information from the input fields. It is intended as a function
 * for creating fragment information in fragment implementations.
 * </para>
 **/
void mirage_fragment_generate_info (MirageFragment *self, const gchar *id, const gchar *name)
{
    g_snprintf(self->priv->info.id, sizeof(self->priv->info.id), "%s", id);
    g_snprintf(self->priv->info.name, sizeof(self->priv->info.name), "%s", name);
}

/**
 * mirage_fragment_get_info:
 * @self: a #MirageFragment
 *
 * <para>
 * Retrieves fragment information.
 * </para>
 *
 * Returns: (transfer none): a pointer to fragment information structure. The
 * structure belongs to object and should not be modified.
 **/
const MirageFragmentInfo *mirage_fragment_get_info (MirageFragment *self)
{
    return &self->priv->info;
}


/**
 * mirage_fragment_can_handle_data_format:
 * @self: a #MirageFragment
 * @stream: (in): data stream
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Checks whether parser can handle data stored in @stream.
 * </para>
 *
 * Returns: %TRUE if fragment can handle data file, %FALSE if not
 **/
gboolean mirage_fragment_can_handle_data_format (MirageFragment *self, GInputStream *stream, GError **error)
{
    /* Provided by implementation */
    return MIRAGE_FRAGMENT_GET_CLASS(self)->can_handle_data_format(self, stream, error);
}


/**
 * mirage_fragment_set_address:
 * @self: a #MirageFragment
 * @address: (in): start address
 *
 * <para>
 * Sets fragment's start address. The @address is given in sectors.
 * </para>
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * <note>
 * Causes top-down change.
 * </note>
 **/
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
 * <para>
 * Retrieves fragment's start address. The @address is given in sectors.
 * </para>
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: start address
 **/
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
 * <para>
 * Sets fragment's length. The @length is given in sectors.
 * </para>
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 **/
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
 * <para>
 * Retrieves fragment's length. The returned @length is given in sectors.
 * </para>
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: length
 **/
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
 * <para>
 * Uses the rest of data file. It automatically calculates and sets fragment's
 * length.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_fragment_use_the_rest_of_file (MirageFragment *self, GError **error)
{
    /* Provided by implementation */
    return MIRAGE_FRAGMENT_GET_CLASS(self)->use_the_rest_of_file(self, error);
}


/**
 * mirage_fragment_read_main_data:
 * @self: a #MirageFragment
 * @address: (in): address
 * @buffer: (out) (allow-none) (array length=length): location to store pointer to buffer with read data, or %NULL
 * @length: (out): location to store read data length
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Reads main channel data for sector at fragment-relative @address (given
 * in sectors). The buffer for reading data into is allocated by function
 * and should be freed using g_free() when no longer needed. The pointer
 * to buffer is stored into @buffer and the length of read data is stored into
 * @length.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_fragment_read_main_data (MirageFragment *self, gint address, guint8 **buffer, gint *length, GError **error)
{
    /* Provided by implementation */
    return MIRAGE_FRAGMENT_GET_CLASS(self)->read_main_data(self, address, buffer, length, error);
}

/**
 * mirage_fragment_read_subchannel_data:
 * @self: a #MirageFragment
 * @address: (in): address
 * @buffer: (out) (allow-none) (array length=length): location to store pointer to buffer with read data, or %NULL
 * @length: (out): location to store read data length
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Reads subchannel data for sector at fragment-relative @address (given
 * in sectors). The buffer for reading data into is allocated by function
 * and should be freed using g_free() when no longer needed. The pointer
 * to buffer is stored into @buffer and the length of read data is stored into
 * @length.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_fragment_read_subchannel_data (MirageFragment *self, gint address, guint8 **buffer, gint *length, GError **error)
{
    /* Provided by implementation */
    return MIRAGE_FRAGMENT_GET_CLASS(self)->read_subchannel_data(self, address, buffer, length, error);
}


/**********************************************************************\
 *                Default implementation: NULL fragment               *
\**********************************************************************/
static gboolean mirage_fragment_null_can_handle_data_format (MirageFragment *self G_GNUC_UNUSED, GInputStream *stream G_GNUC_UNUSED, GError **error)
{
    /* This should never get called, anyway */
    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Fragment cannot handle given data!");
    return FALSE;
}

static gboolean mirage_fragment_null_use_the_rest_of_file (MirageFragment *self G_GNUC_UNUSED, GError **error G_GNUC_UNUSED)
{
    /* No file, nothing to use */
    return TRUE;
}

static gboolean mirage_fragment_null_read_main_data (MirageFragment *self, gint address G_GNUC_UNUSED, guint8 **buffer, gint *length, GError **error G_GNUC_UNUSED)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: no data in NULL fragment\n", __debug__);

    /* Nothing to read */
    *length = 0;
    if (buffer) {
        *buffer = NULL;
    }

    return TRUE;
}

static gboolean mirage_fragment_null_read_subchannel_data (MirageFragment *self, gint address G_GNUC_UNUSED, guint8 **buffer, gint *length, GError **error G_GNUC_UNUSED)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: no data in NULL fragment\n", __debug__);

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
G_DEFINE_TYPE(MirageFragment, mirage_fragment, MIRAGE_TYPE_OBJECT);


static void mirage_fragment_init (MirageFragment *self)
{
    self->priv = MIRAGE_FRAGMENT_GET_PRIVATE(self);

    /* Default fragment implementation is NULL fragment */
    mirage_fragment_generate_info(self,
        "FRAGMENT-NULL",
        "NULL Fragment"
    );
}

static void mirage_fragment_class_init (MirageFragmentClass *klass)
{
    /* Default implementation: NULL fragment */
    klass->can_handle_data_format = mirage_fragment_null_can_handle_data_format;
    klass->use_the_rest_of_file = mirage_fragment_null_use_the_rest_of_file;
    klass->read_main_data = mirage_fragment_null_read_main_data;
    klass->read_subchannel_data = mirage_fragment_null_read_subchannel_data;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageFragmentPrivate));

    /* Signals */
    /**
     * MirageFragment::layout-changed:
     * @fragment: a #MirageFragment
     *
     * <para>
     * Emitted when a layout of #MirageFragment changed in a way that causes a bottom-up change.
     * </para>
     */
    klass->signal_layout_changed = g_signal_new("layout-changed", G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, NULL);
}


/**********************************************************************\
 *                           Binary interface                         *
\**********************************************************************/
/**
 * mirage_fragment_iface_binary_main_data_set_stream:
 * @self: a #MirageFragmentIfaceBinary
 * @stream: (in) (transfer full): a #GInputStream on main data file
 *
 * <para>
 * Sets main data stream.
 * </para>
 **/
void mirage_fragment_iface_binary_main_data_set_stream (MirageFragmentIfaceBinary *self, GInputStream *stream)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->main_data_set_stream(self, stream);
}

/**
 * mirage_fragment_iface_binary_main_data_get_filename:
 * @self: a #MirageFragmentIfaceBinary
 *
 * <para>
 * Retrieves filename of main data file.
 * </para>
 *
 * Returns: (transfer none): pointer to main data file name string.
 * The string belongs to object and should not be modified.
 **/
const gchar *mirage_fragment_iface_binary_main_data_get_filename (MirageFragmentIfaceBinary *self)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->main_data_get_filename(self);
}

/**
 * mirage_fragment_iface_binary_main_data_set_offset:
 * @self: a #MirageFragmentIfaceBinary
 * @offset: (in): main data file offset
 *
 * <para>
 * Sets main data file offset.
 * </para>
 **/
void mirage_fragment_iface_binary_main_data_set_offset (MirageFragmentIfaceBinary *self, guint64 offset)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->main_data_set_offset(self, offset);
}

/**
 * mirage_fragment_iface_binary_main_data_get_offset:
 * @self: a #MirageFragmentIfaceBinary
 *
 * <para>
 * Retrieves main data file offset.
 * </para>
 *
 * Returns: main data file offset
 **/
guint64 mirage_fragment_iface_binary_main_data_get_offset (MirageFragmentIfaceBinary *self)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->main_data_get_offset(self);
}


/**
 * mirage_fragment_iface_binary_main_data_set_size:
 * @self: a #MirageFragmentIfaceBinary
 * @size: (in): main data file sector size
 *
 * <para>
 * Sets main data file sector size.
 * </para>
 **/
void mirage_fragment_iface_binary_main_data_set_size (MirageFragmentIfaceBinary *self, gint size)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->main_data_set_size(self, size);
}

/**
 * mirage_fragment_iface_binary_main_data_get_size:
 * @self: a #MirageFragmentIfaceBinary
 *
 * <para>
 * Retrieves main data file sector size.
 * </para>
 *
 * Returns: main data file sector size
 **/
gint mirage_fragment_iface_binary_main_data_get_size (MirageFragmentIfaceBinary *self)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->main_data_get_size(self);
}


/**
 * mirage_fragment_iface_binary_main_data_set_format:
 * @self: a #MirageFragmentIfaceBinary
 * @format: (in): main data file format
 *
 * <para>
 * Sets main data file format. @format must be one of #MirageMainDataFormat.
 * </para>
 **/
void mirage_fragment_iface_binary_main_data_set_format (MirageFragmentIfaceBinary *self, gint format)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->main_data_set_format(self, format);
}

/**
 * mirage_fragment_iface_binary_main_data_get_format:
 * @self: a #MirageFragmentIfaceBinary
 *
 * <para>
 * Retrieves main data file format.
 * </para>
 *
 * Returns: main data file format
 **/
gint mirage_fragment_iface_binary_main_data_get_format (MirageFragmentIfaceBinary *self)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->main_data_get_format(self);
}


/**
 * mirage_fragment_iface_binary_main_data_get_position:
 * @self: a #MirageFragmentIfaceBinary
 * @address: (in): address
 *
 * <para>
 * Calculates position of data for sector at address @address within
 * main data file and stores it in @position.
 * </para>
 *
 * Returns: position in main data file
 **/
guint64 mirage_fragment_iface_binary_main_data_get_position (MirageFragmentIfaceBinary *self, gint address)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->main_data_get_position(self, address);
}

/**
 * mirage_fragment_iface_binary_subchannel_data_set_stream:
 * @self: a #MirageFragmentIfaceBinary
 * @stream: (in) (transfer full): a #GInputStream on subchannel data file
 *
 * <para>
 * Sets subchannel data stream.
 * </para>
 **/
void mirage_fragment_iface_binary_subchannel_data_set_stream (MirageFragmentIfaceBinary *self, GInputStream *stream)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->subchannel_data_set_stream(self, stream);
}

/**
 * mirage_fragment_iface_binary_subchannel_data_get_filename:
 * @self: a #MirageFragmentIfaceBinary
 *
 * <para>
 * Retrieves subchannel data file name.
 * </para>
 *
 * Returns: (transfer none): pointer to subchannel data file name string.
 * The string belongs to object and should not be modified.
 **/
const gchar *mirage_fragment_iface_binary_subchannel_data_get_filename (MirageFragmentIfaceBinary *self)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->subchannel_data_get_filename(self);
}


/**
 * mirage_fragment_iface_binary_subchannel_data_set_offset:
 * @self: a #MirageFragmentIfaceBinary
 * @offset: (in): subchannel data file offset
 *
 * <para>
 * Sets subchannel data file offset.
 * </para>
 **/
void mirage_fragment_iface_binary_subchannel_data_set_offset (MirageFragmentIfaceBinary *self, guint64 offset)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->subchannel_data_set_offset(self, offset);
}

/**
 * mirage_fragment_iface_binary_subchannel_data_get_offset:
 * @self: a #MirageFragmentIfaceBinary
 *
 * <para>
 * Retrieves subchannel data file offset.
 * </para>
 *
 * Returns: subchannel data file offset
 **/
guint64 mirage_fragment_iface_binary_subchannel_data_get_offset (MirageFragmentIfaceBinary *self)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->subchannel_data_get_offset(self);
}

/**
 * mirage_fragment_iface_binary_subchannel_data_set_size:
 * @self: a #MirageFragmentIfaceBinary
 * @size: (in): subchannel data file sector size
 *
 * <para>
 * Sets subchannel data file sector size.
 * </para>
 **/
void mirage_fragment_iface_binary_subchannel_data_set_size (MirageFragmentIfaceBinary *self, gint size)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->subchannel_data_set_size(self, size);
}

/**
 * mirage_fragment_iface_binary_subchannel_data_get_size:
 * @self: a #MirageFragmentIfaceBinary
 *
 * <para>
 * Retrieves subchannel data file sector size.
 * </para>
 *
 * Returns: subchannel data file sector size
 **/
gint mirage_fragment_iface_binary_subchannel_data_get_size (MirageFragmentIfaceBinary *self)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->subchannel_data_get_size(self);
}


/**
 * mirage_fragment_iface_binary_subchannel_data_set_format:
 * @self: a #MirageFragmentIfaceBinary
 * @format: (in): subchannel data file format
 *
 * <para>
 * Sets subchannel data file format. @format must be a combination of
 * #MirageSubchannelDataFormat.
 * </para>
 **/
void mirage_fragment_iface_binary_subchannel_data_set_format (MirageFragmentIfaceBinary *self, gint format)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->subchannel_data_set_format(self, format);
}

/**
 * mirage_fragment_iface_binary_subchannel_data_get_format:
 * @self: a #MirageFragmentIfaceBinary
 *
 * <para>
 * Retrieves subchannel data file format.
 * </para>
 *
 * Returns: subchannel data file format
 **/
gint mirage_fragment_iface_binary_subchannel_data_get_format (MirageFragmentIfaceBinary *self)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->subchannel_data_get_format(self);
}


/**
 * mirage_fragment_iface_binary_subchannel_data_get_position:
 * @self: a #MirageFragmentIfaceBinary
 * @address: (in): address
 *
 * <para>
 * Calculates position of data for sector at address @address within
 * subchannel data file and stores it in @position.
 * </para>
 *
 * Returns: position in subchannel data file
 **/
guint64 mirage_fragment_iface_binary_subchannel_data_get_position (MirageFragmentIfaceBinary *self, gint address)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->subchannel_data_get_position(self, address);
}


GType mirage_fragment_iface_binary_get_type (void) {
    static GType iface_type = 0;
    if (iface_type == 0) {
        static const GTypeInfo info = {
            sizeof(MirageFragmentIfaceBinaryInterface),
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

        iface_type = g_type_register_static(G_TYPE_INTERFACE, "MirageFragmentIfaceBinary", &info, 0);
    }

    return iface_type;
}


/**********************************************************************\
 *                            Audio interface                         *
\**********************************************************************/
/**
 * mirage_fragment_iface_audio_set_stream:
 * @self: a #MirageFragmentIfaceAudio
 * @stream: (in) (transfer full): a #GInputStream on audio file
 *
 * <para>
 * Sets audio file stream.
 * </para>
 **/
void mirage_fragment_iface_audio_set_stream (MirageFragmentIfaceAudio *self, GInputStream *stream)
{
    return MIRAGE_FRAGMENT_IFACE_AUDIO_GET_INTERFACE(self)->set_stream(self, stream);
}

/**
 * mirage_fragment_iface_audio_get_filename:
 * @self: a #MirageFragmentIfaceAudio
 *
 * <para>
 * Retrieves filename of audio file.
 * </para>
 *
 * Returns: (transfer none): pointer to audio file name string. The
 * string belongs to object and should not be modified.
 **/
const gchar *mirage_fragment_iface_audio_get_filename (MirageFragmentIfaceAudio *self)
{
    return MIRAGE_FRAGMENT_IFACE_AUDIO_GET_INTERFACE(self)->get_filename(self);
}

/**
 * mirage_fragment_iface_audio_set_offset:
 * @self: a #MirageFragmentIfaceAudio
 * @offset: (in): offset
 *
 * <para>
 * Sets offset within audio file, in sectors.
 * </para>
 **/
void mirage_fragment_iface_audio_set_offset (MirageFragmentIfaceAudio *self, gint offset)
{
    return MIRAGE_FRAGMENT_IFACE_AUDIO_GET_INTERFACE(self)->set_offset(self, offset);
}

/**
 * mirage_fragment_iface_audio_get_offset:
 * @self: a #MirageFragmentIfaceAudio
 *
 * <para>
 * Retrieves offset within audio file, in sectors.
 * </para>
 *
 * Returns: offset
 **/
gint mirage_fragment_iface_audio_get_offset (MirageFragmentIfaceAudio *self)
{
    return MIRAGE_FRAGMENT_IFACE_AUDIO_GET_INTERFACE(self)->get_offset(self);
}

GType mirage_fragment_iface_audio_get_type (void) {
    static GType iface_type = 0;
    if (iface_type == 0) {
        static const GTypeInfo info = {
            sizeof(MirageFragmentIfaceAudioInterface),
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

        iface_type = g_type_register_static(G_TYPE_INTERFACE, "MirageFragmentIfaceAudio", &info, 0);
    }

    return iface_type;
}
