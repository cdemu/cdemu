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
#define MIRAGE_FRAGMENT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FRAGMENT, MIRAGE_FragmentPrivate))

struct _MIRAGE_FragmentPrivate
{
    gint address; /* Address (relative to track start) */
    gint length; /* Length, in sectors */
    
    MIRAGE_FragmentInfo *fragment_info;
};


/**********************************************************************\
 *                          Private functions                         *
\**********************************************************************/
static void destroy_fragment_info (MIRAGE_FragmentInfo *info)
{
    /* Free info and its content */
    if (info) {
        g_free(info->id);
        g_free(info->name);
        
        g_free(info);
    }
}

static void mirage_fragment_commit_topdown_change (MIRAGE_Fragment *self G_GNUC_UNUSED)
{
    /* Nothing to do here */
}

static void mirage_fragment_commit_bottomup_change (MIRAGE_Fragment *self)
{
    /* Signal fragment change */
    g_signal_emit_by_name(MIRAGE_OBJECT(self), "object-modified", NULL);
}


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
/**
 * mirage_fragment_generate_fragment_info:
 * @self: a #MIRAGE_Fragment
 * @id: fragment ID
 * @name: fragment name
 *
 * <para>
 * Generates fragment information from the input fields. It is intended as a function
 * for creating fragment information in fragment implementations.
 * </para>
 **/
void mirage_fragment_generate_fragment_info (MIRAGE_Fragment *self, const gchar *id, const gchar *name)
{
    /* Free old info */
    destroy_fragment_info(self->priv->fragment_info);
    
    /* Create new info */
    self->priv->fragment_info = g_new0(MIRAGE_FragmentInfo, 1);
    
    self->priv->fragment_info->id = g_strdup(id);
    self->priv->fragment_info->name = g_strdup(name);
        
    return;
}

/**
 * mirage_fragment_get_fragment_info:
 * @self: a #MIRAGE_Fragment
 * @fragment_info: location to store fragment info
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves fragment information.
 * </para>
 *
 * <para>
 * A pointer to fragment information structure is stored in @fragment_info; the
 * structure belongs to object and therefore should not be modified.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_fragment_get_fragment_info (MIRAGE_Fragment *self, const MIRAGE_FragmentInfo **fragment_info, GError **error)
{
    if (!self->priv->fragment_info) {
        mirage_error(MIRAGE_E_DATANOTSET, error);
        return FALSE;
    }
    
    *fragment_info = self->priv->fragment_info;
    return TRUE;
}


/**
 * mirage_fragment_can_handle_data_format:
 * @self: a #MIRAGE_Fragment
 * @filename: filename
 * @error: location to store error, or %NULL
 *
 * <para>
 * Checks whether parser can handle data stored in @filename.
 * </para>
 *
 * Returns: %TRUE if fragment can handle data file, %FALSE if not
 **/
gboolean mirage_fragment_can_handle_data_format (MIRAGE_Fragment *self, const gchar *filename, GError **error)
{
    /* Provided by implementation */
    return MIRAGE_FRAGMENT_GET_CLASS(self)->can_handle_data_format(self, filename, error);
}


/**
 * mirage_fragment_set_address:
 * @self: a #MIRAGE_Fragment
 * @address: start address
 * @error: location to store error, or %NULL
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
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_fragment_set_address (MIRAGE_Fragment *self, gint address, GError **error G_GNUC_UNUSED)
{
    /* Set address */
    self->priv->address = address;
    /* Top-down change */
    mirage_fragment_commit_topdown_change(self);
    return TRUE;
}

/**
 * mirage_fragment_get_address:
 * @self: a #MIRAGE_Fragment
 * @address: location to store start address
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves fragment's start address. The @address is given in sectors.
 * </para>
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_fragment_get_address (MIRAGE_Fragment *self, gint *address, GError **error)
{
    MIRAGE_CHECK_ARG(address);
    /* Return address */
    *address = self->priv->address;
    return TRUE;
}

/**
 * mirage_fragment_set_length:
 * @self: a #MIRAGE_Fragment
 * @length: length
 * @error: location to store error, or %NULL
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
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_fragment_set_length (MIRAGE_Fragment *self, gint length, GError **error G_GNUC_UNUSED)
{
    /* Set length */
    self->priv->length = length;
    /* Bottom-up change */
    mirage_fragment_commit_bottomup_change(self);
    return TRUE;
}

/**
 * mirage_fragment_get_length:
 * @self: a #MIRAGE_Fragment
 * @length: location to store length
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves fragment's length. The returned @length is given in sectors.
 * </para>
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_fragment_get_length (MIRAGE_Fragment *self, gint *length, GError **error)
{
    MIRAGE_CHECK_ARG(length);
    /* Return length */
    *length = self->priv->length;
    return TRUE;
}
   

/**
 * mirage_fragment_use_the_rest_of_file:
 * @self: a #MIRAGE_Fragment
 * @error: location to store error, or %NULL
 *
 * <para>
 * Uses the rest of data file. It automatically calculates and sets fragment's
 * length.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_fragment_use_the_rest_of_file (MIRAGE_Fragment *self, GError **error)
{
    /* Provided by implementation */
    return MIRAGE_FRAGMENT_GET_CLASS(self)->use_the_rest_of_file(self, error);
}

    
/**
 * mirage_fragment_read_main_data:
 * @self: a #MIRAGE_Fragment
 * @address: address
 * @buf: buffer to read data into
 * @length: location to store read data length
 * @error: location to store error, or %NULL
 *
 * <para>
 * Reads main channel selection data for sector at fragment-relative 
 * @address into @buf and stores read length into @length.
 * Both @address and and @length is given in sectors.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_fragment_read_main_data (MIRAGE_Fragment *self, gint address, guint8 *buf, gint *length, GError **error)
{
    /* Provided by implementation */
    return MIRAGE_FRAGMENT_GET_CLASS(self)->read_main_data(self, address, buf, length, error);
}

/**
 * mirage_fragment_read_subchannel_data:
 * @self: a #MIRAGE_Fragment
 * @address: address
 * @buf: buffer to read data into
 * @length: location to store read data length
 * @error: location to store error, or %NULL
 *
 * <para>
 * Reads subchannel channel selection data for sector at fragment-relative 
 * @address into @buf and stores read length into @length.
 * Both @address and @length is given in sectors.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_fragment_read_subchannel_data (MIRAGE_Fragment *self, gint address, guint8 *buf, gint *length, GError **error)
{
    /* Provided by implementation */
    return MIRAGE_FRAGMENT_GET_CLASS(self)->read_subchannel_data(self, address, buf, length, error);
}


/**********************************************************************\
 *                             Object init                            * 
\**********************************************************************/
G_DEFINE_TYPE(MIRAGE_Fragment, mirage_fragment, MIRAGE_TYPE_OBJECT);


static void mirage_fragment_init (MIRAGE_Fragment *self)
{
    self->priv = MIRAGE_FRAGMENT_GET_PRIVATE(self);

    self->priv->fragment_info = NULL;
}

static void mirage_fragment_finalize (GObject *gobject)
{
    MIRAGE_Fragment *self = MIRAGE_FRAGMENT(gobject);

    /* Free fragment info */
    destroy_fragment_info(self->priv->fragment_info);
  
    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_fragment_parent_class)->finalize(gobject);
}

static void mirage_fragment_class_init (MIRAGE_FragmentClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = mirage_fragment_finalize;

    klass->can_handle_data_format = NULL;
    klass->use_the_rest_of_file = NULL;
    klass->read_main_data = NULL;
    klass->read_subchannel_data = NULL;
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_FragmentPrivate));
}


/**********************************************************************\
 *                            Null interface                          *
\**********************************************************************/
GType mirage_frag_iface_null_get_type (void) {
    static GType iface_type = 0;
    if (iface_type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_FragIface_NullInterface),
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
        
        iface_type = g_type_register_static(G_TYPE_INTERFACE, "MIRAGE_FragIface_Null", &info, 0);
    }
    
    return iface_type;
}


/**********************************************************************\
 *                           Binary interface                         *
\**********************************************************************/
/**
 * mirage_frag_iface_binary_track_file_set_file:
 * @self: a #MIRAGE_FragIface_Binary
 * @filename: track file filename
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets track file.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_frag_iface_binary_track_file_set_file (MIRAGE_FragIface_Binary *self, const gchar *filename, GError **error)
{
    return MIRAGE_FRAG_IFACE_BINARY_GET_INTERFACE(self)->track_file_set_file(self, filename, error);
}

/**
 * mirage_frag_iface_binary_track_file_get_file:
 * @self: a #MIRAGE_FragIface_Binary
 * @filename: location to store track file filename
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track file filename.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_frag_iface_binary_track_file_get_file (MIRAGE_FragIface_Binary *self, const gchar **filename, GError **error)
{
    return MIRAGE_FRAG_IFACE_BINARY_GET_INTERFACE(self)->track_file_get_file(self, filename, error);
}

/**
 * mirage_frag_iface_binary_track_file_set_offset:
 * @self: a #MIRAGE_FragIface_Binary
 * @offset: track file offset
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets track file offset.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_frag_iface_binary_track_file_set_offset (MIRAGE_FragIface_Binary *self, guint64 offset, GError **error)
{
    return MIRAGE_FRAG_IFACE_BINARY_GET_INTERFACE(self)->track_file_set_offset(self, offset, error);
}

/**
 * mirage_frag_iface_binary_track_file_get_offset:
 * @self: a #MIRAGE_FragIface_Binary
 * @offset: location to store track file offset
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track file offset.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_frag_iface_binary_track_file_get_offset (MIRAGE_FragIface_Binary *self, guint64 *offset, GError **error)
{
    return MIRAGE_FRAG_IFACE_BINARY_GET_INTERFACE(self)->track_file_get_offset(self, offset, error);
}


/**
 * mirage_frag_iface_binary_track_file_set_sectsize:
 * @self: a #MIRAGE_FragIface_Binary
 * @sectsize: track file sector size
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets track file sector size.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_frag_iface_binary_track_file_set_sectsize (MIRAGE_FragIface_Binary *self, gint sectsize, GError **error)
{
    return MIRAGE_FRAG_IFACE_BINARY_GET_INTERFACE(self)->track_file_set_sectsize(self, sectsize, error);    
}

/**
 * mirage_frag_iface_binary_track_file_get_sectsize:
 * @self: a #MIRAGE_FragIface_Binary
 * @sectsize: location to store track file sector size.
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track file sector size.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_frag_iface_binary_track_file_get_sectsize (MIRAGE_FragIface_Binary *self, gint *sectsize, GError **error)
{
    return MIRAGE_FRAG_IFACE_BINARY_GET_INTERFACE(self)->track_file_get_sectsize(self, sectsize, error);    
}


/**
 * mirage_frag_iface_binary_track_file_set_format:
 * @self: a #MIRAGE_FragIface_Binary
 * @format: track file data format
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets track file data format. @format must be one of #MIRAGE_BINARY_TrackFile_Format.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_frag_iface_binary_track_file_set_format (MIRAGE_FragIface_Binary *self, gint format, GError **error)
{
    return MIRAGE_FRAG_IFACE_BINARY_GET_INTERFACE(self)->track_file_set_format(self, format, error);    
}

/**
 * mirage_frag_iface_binary_track_file_get_format:
 * @self: a #MIRAGE_FragIface_Binary
 * @format: location to store track file data format
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track file data format.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_frag_iface_binary_track_file_get_format (MIRAGE_FragIface_Binary *self, gint *format, GError **error)
{
    return MIRAGE_FRAG_IFACE_BINARY_GET_INTERFACE(self)->track_file_get_format(self, format, error);        
}


/**
 * mirage_frag_iface_binary_track_file_get_position:
 * @self: a #MIRAGE_FragIface_Binary
 * @address: address
 * @position: location to store position
 * @error: location to store error, or %NULL
 *
 * <para>
 * Calculates position of data for sector at address @address within track file
 * and stores it in @position.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_frag_iface_binary_track_file_get_position (MIRAGE_FragIface_Binary *self, gint address, guint64 *position, GError **error)
{
    return MIRAGE_FRAG_IFACE_BINARY_GET_INTERFACE(self)->track_file_get_position(self, address, position, error);            
}


/**
 * mirage_frag_iface_binary_subchannel_file_set_file:
 * @self: a #MIRAGE_FragIface_Binary
 * @filename: subchannel file filename
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets subchannel file filename.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_frag_iface_binary_subchannel_file_set_file (MIRAGE_FragIface_Binary *self, const gchar *filename, GError **error)
{
    return MIRAGE_FRAG_IFACE_BINARY_GET_INTERFACE(self)->subchannel_file_set_file(self, filename, error);
}

/**
 * mirage_frag_iface_binary_subchannel_file_get_file:
 * @self: a #MIRAGE_FragIface_Binary
 * @filename: location to store subchannel file filename
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves subchannel file filename.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_frag_iface_binary_subchannel_file_get_file (MIRAGE_FragIface_Binary *self, const gchar **filename, GError **error)
{
    return MIRAGE_FRAG_IFACE_BINARY_GET_INTERFACE(self)->subchannel_file_get_file(self, filename, error);
}


/**
 * mirage_frag_iface_binary_subchannel_file_set_offset:
 * @self: a #MIRAGE_FragIface_Binary
 * @offset: subchannel file offset
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets subchannel file offset.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_frag_iface_binary_subchannel_file_set_offset (MIRAGE_FragIface_Binary *self, guint64 offset, GError **error)
{
    return MIRAGE_FRAG_IFACE_BINARY_GET_INTERFACE(self)->subchannel_file_set_offset(self, offset, error);
}

/**
 * mirage_frag_iface_binary_subchannel_file_get_offset:
 * @self: a #MIRAGE_FragIface_Binary
 * @offset: location to store subchannel file offset
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves subchannel file offset.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_frag_iface_binary_subchannel_file_get_offset (MIRAGE_FragIface_Binary *self, guint64 *offset, GError **error)
{
    return MIRAGE_FRAG_IFACE_BINARY_GET_INTERFACE(self)->subchannel_file_get_offset(self, offset, error);
}

/**
 * mirage_frag_iface_binary_subchannel_file_set_sectsize:
 * @self: a #MIRAGE_FragIface_Binary
 * @sectsize: subchannel file sector size
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets subchannel file sector size.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_frag_iface_binary_subchannel_file_set_sectsize (MIRAGE_FragIface_Binary *self, gint sectsize, GError **error)
{
    return MIRAGE_FRAG_IFACE_BINARY_GET_INTERFACE(self)->subchannel_file_set_sectsize(self, sectsize, error);
}

/**
 * mirage_frag_iface_binary_subchannel_file_get_sectsize:
 * @self: a #MIRAGE_FragIface_Binary
 * @sectsize: location to store subchannel file sector size
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves subchannel file sector size.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_frag_iface_binary_subchannel_file_get_sectsize (MIRAGE_FragIface_Binary *self, gint *sectsize, GError **error)
{
    return MIRAGE_FRAG_IFACE_BINARY_GET_INTERFACE(self)->subchannel_file_get_sectsize(self, sectsize, error);
}


/**
 * mirage_frag_iface_binary_subchannel_file_set_format:
 * @self: a #MIRAGE_FragIface_Binary
 * @format: subchannel file data format
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets subchannel file data format. @format must be a combination of ž
 * #MIRAGE_BINARY_SubchannelFile_Format.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_frag_iface_binary_subchannel_file_set_format (MIRAGE_FragIface_Binary *self, gint format, GError **error)
{
    return MIRAGE_FRAG_IFACE_BINARY_GET_INTERFACE(self)->subchannel_file_set_format(self, format, error);
}

/**
 * mirage_frag_iface_binary_subchannel_file_get_format:
 * @self: a #MIRAGE_FragIface_Binary
 * @format: location to store subchannel data format
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves subchannel file data format.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_frag_iface_binary_subchannel_file_get_format (MIRAGE_FragIface_Binary *self, gint *format, GError **error)
{
    return MIRAGE_FRAG_IFACE_BINARY_GET_INTERFACE(self)->subchannel_file_get_format(self, format, error);
}


/**
 * mirage_frag_iface_binary_subchannel_file_get_position:
 * @self: a #MIRAGE_FragIface_Binary
 * @address: address
 * @position: location to store position
 * @error: location to store error, or %NULL
 *
 * <para>
 * Calculates position of data for sector at address @address within subchannel file
 * and stores it in @position.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_frag_iface_binary_subchannel_file_get_position (MIRAGE_FragIface_Binary *self, gint address, guint64 *position, GError **error)
{
    return MIRAGE_FRAG_IFACE_BINARY_GET_INTERFACE(self)->subchannel_file_get_position(self, address, position, error);
}


GType mirage_frag_iface_binary_get_type (void) {
    static GType iface_type = 0;
    if (iface_type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_FragIface_BinaryInterface),
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
        
        iface_type = g_type_register_static(G_TYPE_INTERFACE, "MIRAGE_FragIface_Binary", &info, 0);
    }
    
    return iface_type;
}


/**********************************************************************\
 *                            Audio interface                         *
\**********************************************************************/
/**
 * mirage_frag_iface_audio_set_file:
 * @self: a #MIRAGE_FragIface_Audio
 * @filename: filename
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets audio file to @filename.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_frag_iface_audio_set_file (MIRAGE_FragIface_Audio *self, const gchar *filename, GError **error)
{
    return MIRAGE_FRAG_IFACE_AUDIO_GET_INTERFACE(self)->set_file(self, filename, error);
}

/**
 * mirage_frag_iface_audio_get_file:
 * @self: a #MIRAGE_FragIface_Audio
 * @filename: location to store filename
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves filename of audio file. A pointer to filename string is stored to @filename;
 * the string belongs to the object and therefore should not be modified.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_frag_iface_audio_get_file (MIRAGE_FragIface_Audio *self, const gchar **filename, GError **error)
{
    return MIRAGE_FRAG_IFACE_AUDIO_GET_INTERFACE(self)->get_file(self, filename, error);
}

/**
 * mirage_frag_iface_audio_set_offset:
 * @self: a #MIRAGE_FragIface_Audio
 * @offset: offset
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets offset within audio file, in sectors.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_frag_iface_audio_set_offset (MIRAGE_FragIface_Audio *self, gint offset, GError **error)
{
    return MIRAGE_FRAG_IFACE_AUDIO_GET_INTERFACE(self)->set_offset(self, offset, error);
}

/**
 * mirage_frag_iface_audio_get_offset:
 * @self: a #MIRAGE_FragIface_Audio
 * @offset: location to store offset
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves offset within audio file, in sectors.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_frag_iface_audio_get_offset (MIRAGE_FragIface_Audio *self, gint *offset, GError **error)
{
    return MIRAGE_FRAG_IFACE_AUDIO_GET_INTERFACE(self)->get_offset(self, offset, error);
}

GType mirage_frag_iface_audio_get_type (void) {
    static GType iface_type = 0;
    if (iface_type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_FragIface_AudioInterface),
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
        
        iface_type = g_type_register_static(G_TYPE_INTERFACE, "MIRAGE_FragIface_Audio", &info, 0);
    }
    
    return iface_type;
}
