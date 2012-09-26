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
    gint address; /* Address (relative to track start) */
    gint length; /* Length, in sectors */

    MirageFragmentInfo *fragment_info;
};


/**********************************************************************\
 *                          Private functions                         *
\**********************************************************************/
static void destroy_fragment_info (MirageFragmentInfo *info)
{
    /* Free info and its content */
    if (info) {
        g_free(info->id);
        g_free(info->name);

        g_free(info);
    }
}

static void mirage_fragment_commit_topdown_change (MirageFragment *self G_GNUC_UNUSED)
{
    /* Nothing to do here */
}

static void mirage_fragment_commit_bottomup_change (MirageFragment *self)
{
    /* Signal fragment change */
    g_signal_emit_by_name(MIRAGE_OBJECT(self), "object-modified", NULL);
}


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
/**
 * mirage_fragment_generate_fragment_info:
 * @self: a #MirageFragment
 * @id: (in): fragment ID
 * @name: (in): fragment name
 *
 * <para>
 * Generates fragment information from the input fields. It is intended as a function
 * for creating fragment information in fragment implementations.
 * </para>
 **/
void mirage_fragment_generate_fragment_info (MirageFragment *self, const gchar *id, const gchar *name)
{
    /* Free old info */
    destroy_fragment_info(self->priv->fragment_info);

    /* Create new info */
    self->priv->fragment_info = g_new0(MirageFragmentInfo, 1);

    self->priv->fragment_info->id = g_strdup(id);
    self->priv->fragment_info->name = g_strdup(name);

    return;
}

/**
 * mirage_fragment_get_fragment_info:
 * @self: a #MirageFragment
 *
 * <para>
 * Retrieves fragment information.
 * </para>
 *
 * Returns: (transfer none): a pointer to fragment information structure. The
 * structure belongs to object and should not be modified.
 **/
const MirageFragmentInfo *mirage_fragment_get_fragment_info (MirageFragment *self)
{
    return self->priv->fragment_info;
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
gboolean mirage_fragment_can_handle_data_format (MirageFragment *self, GObject *stream, GError **error)
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
 * @buf: (out caller-allocates) (array length=length): buffer to read data into
 * @length: (out): location to store read data length
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Reads main channel selection data for sector at fragment-relative
 * @address into @buf and stores read length into @length.
 * Both @address and and @length is given in sectors.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_fragment_read_main_data (MirageFragment *self, gint address, guint8 *buf, gint *length, GError **error)
{
    /* Provided by implementation */
    return MIRAGE_FRAGMENT_GET_CLASS(self)->read_main_data(self, address, buf, length, error);
}

/**
 * mirage_fragment_read_subchannel_data:
 * @self: a #MirageFragment
 * @address: (in): address
 * @buf: (out caller-allocates) (array length=length): buffer to read data into
 * @length: (out): location to store read data length
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Reads subchannel channel selection data for sector at fragment-relative
 * @address into @buf and stores read length into @length.
 * Both @address and @length is given in sectors.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_fragment_read_subchannel_data (MirageFragment *self, gint address, guint8 *buf, gint *length, GError **error)
{
    /* Provided by implementation */
    return MIRAGE_FRAGMENT_GET_CLASS(self)->read_subchannel_data(self, address, buf, length, error);
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE(MirageFragment, mirage_fragment, MIRAGE_TYPE_OBJECT);


static void mirage_fragment_init (MirageFragment *self)
{
    self->priv = MIRAGE_FRAGMENT_GET_PRIVATE(self);

    self->priv->fragment_info = NULL;
}

static void mirage_fragment_finalize (GObject *gobject)
{
    MirageFragment *self = MIRAGE_FRAGMENT(gobject);

    /* Free fragment info */
    destroy_fragment_info(self->priv->fragment_info);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_fragment_parent_class)->finalize(gobject);
}

static void mirage_fragment_class_init (MirageFragmentClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = mirage_fragment_finalize;

    klass->can_handle_data_format = NULL;
    klass->use_the_rest_of_file = NULL;
    klass->read_main_data = NULL;
    klass->read_subchannel_data = NULL;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageFragmentPrivate));
}


/**********************************************************************\
 *                            Null interface                          *
\**********************************************************************/
GType mirage_fragment_iface_null_get_type (void) {
    static GType iface_type = 0;
    if (iface_type == 0) {
        static const GTypeInfo info = {
            sizeof(MirageFragmentIfaceNullInterface),
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

        iface_type = g_type_register_static(G_TYPE_INTERFACE, "MirageFragmentIfaceNull", &info, 0);
    }

    return iface_type;
}


/**********************************************************************\
 *                           Binary interface                         *
\**********************************************************************/
/**
 * mirage_fragment_iface_binary_track_file_set_file:
 * @self: a #MirageFragmentIfaceBinary
 * @filename: (in): track file filename
 * @stream: (in) (allow-none) (transfer full): a #GInputStream on file
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Sets track file. If @stream is provided, the existing stream is used.
 * If @stream is %NULL, a new stream is opened on @filename. @filename
 * needs to be provided in either case.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_fragment_iface_binary_track_file_set_file (MirageFragmentIfaceBinary *self, const gchar *filename, GObject *stream, GError **error)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->track_file_set_file(self, filename, stream, error);
}

/**
 * mirage_fragment_iface_binary_track_file_get_file:
 * @self: a #MirageFragmentIfaceBinary
 *
 * <para>
 * Retrieves track file name.
 * </para>
 *
 * Returns: (transfer none): track file name string. The string belongs to
 * object and should not be modified.
 **/
const gchar *mirage_fragment_iface_binary_track_file_get_file (MirageFragmentIfaceBinary *self)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->track_file_get_file(self);
}

/**
 * mirage_fragment_iface_binary_track_file_set_offset:
 * @self: a #MirageFragmentIfaceBinary
 * @offset: (in): track file offset
 *
 * <para>
 * Sets track file offset.
 * </para>
 **/
void mirage_fragment_iface_binary_track_file_set_offset (MirageFragmentIfaceBinary *self, guint64 offset)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->track_file_set_offset(self, offset);
}

/**
 * mirage_fragment_iface_binary_track_file_get_offset:
 * @self: a #MirageFragmentIfaceBinary
 *
 * <para>
 * Retrieves track file offset.
 * </para>
 *
 * Returns: track file offset
 **/
guint64 mirage_fragment_iface_binary_track_file_get_offset (MirageFragmentIfaceBinary *self)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->track_file_get_offset(self);
}


/**
 * mirage_fragment_iface_binary_track_file_set_sectsize:
 * @self: a #MirageFragmentIfaceBinary
 * @sectsize: (in): track file sector size
 *
 * <para>
 * Sets track file sector size.
 * </para>
 **/
void mirage_fragment_iface_binary_track_file_set_sectsize (MirageFragmentIfaceBinary *self, gint sectsize)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->track_file_set_sectsize(self, sectsize);
}

/**
 * mirage_fragment_iface_binary_track_file_get_sectsize:
 * @self: a #MirageFragmentIfaceBinary
 *
 * <para>
 * Retrieves track file sector size.
 * </para>
 *
 * Returns: track file sector size
 **/
gint mirage_fragment_iface_binary_track_file_get_sectsize (MirageFragmentIfaceBinary *self)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->track_file_get_sectsize(self);
}


/**
 * mirage_fragment_iface_binary_track_file_set_format:
 * @self: a #MirageFragmentIfaceBinary
 * @format: (in): track file data format
 *
 * <para>
 * Sets track file data format. @format must be one of #MirageTrackFileFormat.
 * </para>
 **/
void mirage_fragment_iface_binary_track_file_set_format (MirageFragmentIfaceBinary *self, gint format)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->track_file_set_format(self, format);
}

/**
 * mirage_fragment_iface_binary_track_file_get_format:
 * @self: a #MirageFragmentIfaceBinary
 *
 * <para>
 * Retrieves track file data format.
 * </para>
 *
 * Returns: track file data format
 **/
gint mirage_fragment_iface_binary_track_file_get_format (MirageFragmentIfaceBinary *self)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->track_file_get_format(self);
}


/**
 * mirage_fragment_iface_binary_track_file_get_position:
 * @self: a #MirageFragmentIfaceBinary
 * @address: (in): address
 *
 * <para>
 * Calculates position of data for sector at address @address within track file
 * and stores it in @position.
 * </para>
 *
 * Returns: position in track file
 **/
guint64 mirage_fragment_iface_binary_track_file_get_position (MirageFragmentIfaceBinary *self, gint address)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->track_file_get_position(self, address);
}

/**
 * mirage_fragment_iface_binary_subchannel_file_set_file:
 * @self: a #MirageFragmentIfaceBinary
 * @filename: (in): subchannel file filename
 * @stream: (in) (allow-none) (transfer full): a #GInputStream on file
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Sets subchannel file. If @stream is provided, the existing stream
 * is used. If @stream is %NULL, a new stream is opened on @filename.
 * @filename needs to be provided in either case.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_fragment_iface_binary_subchannel_file_set_file (MirageFragmentIfaceBinary *self, const gchar *filename, GObject *stream, GError **error)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->subchannel_file_set_file(self, filename, stream, error);
}

/**
 * mirage_fragment_iface_binary_subchannel_file_get_file:
 * @self: a #MirageFragmentIfaceBinary
 *
 * <para>
 * Retrieves subchannel file name.
 * </para>
 *
 * Returns: (transfer none): subchannel file name string. The string belongs to
 * object and should not be modified.
 **/
const gchar *mirage_fragment_iface_binary_subchannel_file_get_file (MirageFragmentIfaceBinary *self)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->subchannel_file_get_file(self);
}


/**
 * mirage_fragment_iface_binary_subchannel_file_set_offset:
 * @self: a #MirageFragmentIfaceBinary
 * @offset: (in): subchannel file offset
 *
 * <para>
 * Sets subchannel file offset.
 * </para>
 **/
void mirage_fragment_iface_binary_subchannel_file_set_offset (MirageFragmentIfaceBinary *self, guint64 offset)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->subchannel_file_set_offset(self, offset);
}

/**
 * mirage_fragment_iface_binary_subchannel_file_get_offset:
 * @self: a #MirageFragmentIfaceBinary
 *
 * <para>
 * Retrieves subchannel file offset.
 * </para>
 *
 * Returns: subchannel file offset
 **/
guint64 mirage_fragment_iface_binary_subchannel_file_get_offset (MirageFragmentIfaceBinary *self)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->subchannel_file_get_offset(self);
}

/**
 * mirage_fragment_iface_binary_subchannel_file_set_sectsize:
 * @self: a #MirageFragmentIfaceBinary
 * @sectsize: (in): subchannel file sector size
 *
 * <para>
 * Sets subchannel file sector size.
 * </para>
 **/
void mirage_fragment_iface_binary_subchannel_file_set_sectsize (MirageFragmentIfaceBinary *self, gint sectsize)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->subchannel_file_set_sectsize(self, sectsize);
}

/**
 * mirage_fragment_iface_binary_subchannel_file_get_sectsize:
 * @self: a #MirageFragmentIfaceBinary
 *
 * <para>
 * Retrieves subchannel file sector size.
 * </para>
 *
 * Returns: subchannel file sector size
 **/
gint mirage_fragment_iface_binary_subchannel_file_get_sectsize (MirageFragmentIfaceBinary *self)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->subchannel_file_get_sectsize(self);
}


/**
 * mirage_fragment_iface_binary_subchannel_file_set_format:
 * @self: a #MirageFragmentIfaceBinary
 * @format: (in): subchannel file data format
 *
 * <para>
 * Sets subchannel file data format. @format must be a combination of
 * #MirageSubchannelFileFormat.
 * </para>
 **/
void mirage_fragment_iface_binary_subchannel_file_set_format (MirageFragmentIfaceBinary *self, gint format)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->subchannel_file_set_format(self, format);
}

/**
 * mirage_fragment_iface_binary_subchannel_file_get_format:
 * @self: a #MirageFragmentIfaceBinary
 *
 * <para>
 * Retrieves subchannel file data format.
 * </para>
 *
 * Returns: subchannel data format
 **/
gint mirage_fragment_iface_binary_subchannel_file_get_format (MirageFragmentIfaceBinary *self)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->subchannel_file_get_format(self);
}


/**
 * mirage_fragment_iface_binary_subchannel_file_get_position:
 * @self: a #MirageFragmentIfaceBinary
 * @address: (in): address
 *
 * <para>
 * Calculates position of data for sector at address @address within subchannel file
 * and stores it in @position.
 * </para>
 *
 * Returns: position in subchannel file
 **/
guint64 mirage_fragment_iface_binary_subchannel_file_get_position (MirageFragmentIfaceBinary *self, gint address)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->subchannel_file_get_position(self, address);
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
 * mirage_fragment_iface_audio_set_file:
 * @self: a #MirageFragmentIfaceAudio
 * @filename: (in): filename
 * @stream: (in) (allow-none) (transfer full): a #GInputStream on file
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Sets audio file. If @stream is provided, the existing stream is used.
 * If @stream in %NULL, a new stream is opened on @filename. @filename
 * needs to be provided in either case.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_fragment_iface_audio_set_file (MirageFragmentIfaceAudio *self, const gchar *filename, GObject *stream, GError **error)
{
    return MIRAGE_FRAGMENT_IFACE_AUDIO_GET_INTERFACE(self)->set_file(self, filename, stream, error);
}

/**
 * mirage_fragment_iface_audio_get_file:
 * @self: a #MirageFragmentIfaceAudio
 *
 * <para>
 * Retrieves filename of audio file.
 * </para>
 *
 * Returns: (transfer none): audio file name string. The string belongs to
 * object and should not be modified.
 **/
const gchar *mirage_fragment_iface_audio_get_file (MirageFragmentIfaceAudio *self)
{
    return MIRAGE_FRAGMENT_IFACE_AUDIO_GET_INTERFACE(self)->get_file(self);
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
