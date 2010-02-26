/*
 *  libMirage: Fragment object
 *  Copyright (C) 2006-2010 Rok Mandeljc
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


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_FRAGMENT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FRAGMENT, MIRAGE_FragmentPrivate))

typedef struct {        
    gint address; /* Address (relative to track start) */
    gint length; /* Length, in sectors */
    
    MIRAGE_FragmentInfo *fragment_info;
} MIRAGE_FragmentPrivate;


/******************************************************************************\
 *                              Private functions                             *
\******************************************************************************/
static void __destroy_fragment_info (MIRAGE_FragmentInfo *info) {
    /* Free info and its content */
    if (info) {
        g_free(info->id);
        g_free(info->name);
        
        g_free(info);
    }
    
    return;
}

static gboolean __mirage_fragment_commit_topdown_change (MIRAGE_Fragment *self, GError **error) {
    /* Nothing to do here */
    return TRUE;
}

static gboolean __mirage_fragment_commit_bottomup_change (MIRAGE_Fragment *self, GError **error) {
    /* Signal fragment change */
    g_signal_emit_by_name(MIRAGE_OBJECT(self), "object-modified", NULL);
    
    return TRUE;
}


/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
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
void mirage_fragment_generate_fragment_info (MIRAGE_Fragment *self, const gchar *id, const gchar *name) {
    MIRAGE_FragmentPrivate *_priv = MIRAGE_FRAGMENT_GET_PRIVATE(self);
    
    /* Free old info */
    __destroy_fragment_info(_priv->fragment_info);
    
    /* Create new info */
    _priv->fragment_info = g_new0(MIRAGE_FragmentInfo, 1);
    
    _priv->fragment_info->id = g_strdup(id);
    _priv->fragment_info->name = g_strdup(name);
        
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
gboolean mirage_fragment_get_fragment_info (MIRAGE_Fragment *self, const MIRAGE_FragmentInfo **fragment_info, GError **error) {
    MIRAGE_FragmentPrivate *_priv = MIRAGE_FRAGMENT_GET_PRIVATE(self);

    if (!_priv->fragment_info) {
        mirage_error(MIRAGE_E_DATANOTSET, error);
        return FALSE;
    }
    
    *fragment_info = _priv->fragment_info;
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
gboolean mirage_fragment_can_handle_data_format (MIRAGE_Fragment *self, const gchar *filename, GError **error) {
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
gboolean mirage_fragment_set_address (MIRAGE_Fragment *self, gint address, GError **error) {
    MIRAGE_FragmentPrivate *_priv = MIRAGE_FRAGMENT_GET_PRIVATE(self);
    /* Set address */
    _priv->address = address;
    /* Top-down change */
    __mirage_fragment_commit_topdown_change(self, NULL);
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
gboolean mirage_fragment_get_address (MIRAGE_Fragment *self, gint *address, GError **error) {
    MIRAGE_FragmentPrivate *_priv = MIRAGE_FRAGMENT_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(address);
    /* Return address */
    *address = _priv->address;
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
gboolean mirage_fragment_set_length (MIRAGE_Fragment *self, gint length, GError **error) {
    MIRAGE_FragmentPrivate *_priv = MIRAGE_FRAGMENT_GET_PRIVATE(self);
    /* Set length */
    _priv->length = length;
    /* Bottom-up change */
    __mirage_fragment_commit_bottomup_change(self, NULL);
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
gboolean mirage_fragment_get_length (MIRAGE_Fragment *self, gint *length, GError **error) {
    MIRAGE_FragmentPrivate *_priv = MIRAGE_FRAGMENT_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(length);
    /* Return length */
    *length = _priv->length;
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
gboolean mirage_fragment_use_the_rest_of_file (MIRAGE_Fragment *self, GError **error) {
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
gboolean mirage_fragment_read_main_data (MIRAGE_Fragment *self, gint address, guint8 *buf, gint *length, GError **error) {
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
gboolean mirage_fragment_read_subchannel_data (MIRAGE_Fragment *self, gint address, guint8 *buf, gint *length, GError **error) {
    /* Provided by implementation */
    return MIRAGE_FRAGMENT_GET_CLASS(self)->read_subchannel_data(self, address, buf, length, error);
}


/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static MIRAGE_ObjectClass *parent_class = NULL;

static void __mirage_fragment_finalize (GObject *obj) {
    MIRAGE_Fragment *self = MIRAGE_FRAGMENT(obj);
    MIRAGE_FragmentPrivate *_priv = MIRAGE_FRAGMENT_GET_PRIVATE(self);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: finalizing object\n", __debug__);
    
    /* Free fragment info */
    __destroy_fragment_info(_priv->fragment_info);
    
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __debug__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}


static void __mirage_fragment_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_FragmentClass *klass = MIRAGE_FRAGMENT_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_FragmentPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_fragment_finalize;
    
    /* Initialize MIRAGE_Fragment members */    
    klass->can_handle_data_format = NULL;
    klass->use_the_rest_of_file = NULL;
    klass->read_main_data = NULL;
    klass->read_subchannel_data = NULL;
    
    return;
}

GType mirage_fragment_get_type (void) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_FragmentClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_fragment_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Fragment),
            0,      /* n_preallocs */
            NULL    /* instance_init */
        };
        
        type = g_type_register_static(MIRAGE_TYPE_OBJECT, "MIRAGE_Fragment", &info, 0);
    }
    
    return type;
}

/******************************************************************************\
 *                                NULL interface                              *
\******************************************************************************/
GType mirage_finterface_null_get_type (void) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_FInterface_NULLClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            NULL,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            0,
            0,      /* n_preallocs */
            NULL    /* instance_init */
        };
        
        type = g_type_register_static(G_TYPE_INTERFACE, "MIRAGE_FInterface_NULL", &info, 0);
    }
    
    return type;
}


/******************************************************************************\
 *                               BINARY interface                             *
\******************************************************************************/
/**
 * mirage_finterface_binary_track_file_set_handle:
 * @self: a #MIRAGE_FInterface_BINARY
 * @file: track file handle
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets track file handle.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_finterface_binary_track_file_set_handle (MIRAGE_FInterface_BINARY *self, FILE *file, GError **error) {
    return MIRAGE_FINTERFACE_BINARY_GET_CLASS(self)->track_file_set_handle(self, file, error);
}

/**
 * mirage_finterface_binary_track_file_get_handle:
 * @self: a #MIRAGE_FInterface_BINARY
 * @file: location to store track file handle
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track file handle.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_finterface_binary_track_file_get_handle (MIRAGE_FInterface_BINARY *self, FILE **file, GError **error) {
    return MIRAGE_FINTERFACE_BINARY_GET_CLASS(self)->track_file_get_handle(self, file, error);
}

/**
 * mirage_finterface_binary_track_file_set_offset:
 * @self: a #MIRAGE_FInterface_BINARY
 * @offset: track file offset
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets track file offset.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_finterface_binary_track_file_set_offset (MIRAGE_FInterface_BINARY *self, guint64 offset, GError **error) {
    return MIRAGE_FINTERFACE_BINARY_GET_CLASS(self)->track_file_set_offset(self, offset, error);
}

/**
 * mirage_finterface_binary_track_file_get_offset:
 * @self: a #MIRAGE_FInterface_BINARY
 * @offset: location to store track file offset
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track file offset.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_finterface_binary_track_file_get_offset (MIRAGE_FInterface_BINARY *self, guint64 *offset, GError **error) {
    return MIRAGE_FINTERFACE_BINARY_GET_CLASS(self)->track_file_get_offset(self, offset, error);
}


/**
 * mirage_finterface_binary_track_file_set_sectsize:
 * @self: a #MIRAGE_FInterface_BINARY
 * @sectsize: track file sector size
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets track file sector size.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_finterface_binary_track_file_set_sectsize (MIRAGE_FInterface_BINARY *self, gint sectsize, GError **error) {
    return MIRAGE_FINTERFACE_BINARY_GET_CLASS(self)->track_file_set_sectsize(self, sectsize, error);    
}

/**
 * mirage_finterface_binary_track_file_get_sectsize:
 * @self: a #MIRAGE_FInterface_BINARY
 * @sectsize: location to store track file sector size.
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track file sector size.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_finterface_binary_track_file_get_sectsize (MIRAGE_FInterface_BINARY *self, gint *sectsize, GError **error) {
    return MIRAGE_FINTERFACE_BINARY_GET_CLASS(self)->track_file_get_sectsize(self, sectsize, error);    
}


/**
 * mirage_finterface_binary_track_file_set_format:
 * @self: a #MIRAGE_FInterface_BINARY
 * @format: track file data format
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets track file data format. @format must be one of #MIRAGE_BINARY_TrackFile_Format.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_finterface_binary_track_file_set_format (MIRAGE_FInterface_BINARY *self, gint format, GError **error) {
    return MIRAGE_FINTERFACE_BINARY_GET_CLASS(self)->track_file_set_format(self, format, error);    
}

/**
 * mirage_finterface_binary_track_file_get_format:
 * @self: a #MIRAGE_FInterface_BINARY
 * @format: location to store track file data format
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track file data format.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_finterface_binary_track_file_get_format (MIRAGE_FInterface_BINARY *self, gint *format, GError **error) {
    return MIRAGE_FINTERFACE_BINARY_GET_CLASS(self)->track_file_get_format(self, format, error);        
}


/**
 * mirage_finterface_binary_track_file_get_position:
 * @self: a #MIRAGE_FInterface_BINARY
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
gboolean mirage_finterface_binary_track_file_get_position (MIRAGE_FInterface_BINARY *self, gint address, guint64 *position, GError **error) {
    return MIRAGE_FINTERFACE_BINARY_GET_CLASS(self)->track_file_get_position(self, address, position, error);            
}


/**
 * mirage_finterface_binary_subchannel_file_set_handle:
 * @self: a #MIRAGE_FInterface_BINARY
 * @file: subchannel file handle
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets subchannel file handle.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_finterface_binary_subchannel_file_set_handle (MIRAGE_FInterface_BINARY *self, FILE *file, GError **error) {
    return MIRAGE_FINTERFACE_BINARY_GET_CLASS(self)->subchannel_file_set_handle(self, file, error);
}

/**
 * mirage_finterface_binary_subchannel_file_get_handle:
 * @self: a #MIRAGE_FInterface_BINARY
 * @file: location to store subchannel file handle
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves subchannel file handle.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_finterface_binary_subchannel_file_get_handle (MIRAGE_FInterface_BINARY *self, FILE **file, GError **error) {
    return MIRAGE_FINTERFACE_BINARY_GET_CLASS(self)->subchannel_file_get_handle(self, file, error);
}


/**
 * mirage_finterface_binary_subchannel_file_set_offset:
 * @self: a #MIRAGE_FInterface_BINARY
 * @offset: subchannel file offset
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets subchannel file offset.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_finterface_binary_subchannel_file_set_offset (MIRAGE_FInterface_BINARY *self, guint64 offset, GError **error) {
    return MIRAGE_FINTERFACE_BINARY_GET_CLASS(self)->subchannel_file_set_offset(self, offset, error);
}

/**
 * mirage_finterface_binary_subchannel_file_get_offset:
 * @self: a #MIRAGE_FInterface_BINARY
 * @offset: location to store subchannel file offset
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves subchannel file offset.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_finterface_binary_subchannel_file_get_offset (MIRAGE_FInterface_BINARY *self, guint64 *offset, GError **error) {
    return MIRAGE_FINTERFACE_BINARY_GET_CLASS(self)->subchannel_file_get_offset(self, offset, error);
}

/**
 * mirage_finterface_binary_subchannel_file_set_sectsize:
 * @self: a #MIRAGE_FInterface_BINARY
 * @sectsize: subchannel file sector size
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets subchannel file sector size.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_finterface_binary_subchannel_file_set_sectsize (MIRAGE_FInterface_BINARY *self, gint sectsize, GError **error) {
    return MIRAGE_FINTERFACE_BINARY_GET_CLASS(self)->subchannel_file_set_sectsize(self, sectsize, error);
}

/**
 * mirage_finterface_binary_subchannel_file_get_sectsize:
 * @self: a #MIRAGE_FInterface_BINARY
 * @sectsize: location to store subchannel file sector size
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves subchannel file sector size.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_finterface_binary_subchannel_file_get_sectsize (MIRAGE_FInterface_BINARY *self, gint *sectsize, GError **error) {
    return MIRAGE_FINTERFACE_BINARY_GET_CLASS(self)->subchannel_file_get_sectsize(self, sectsize, error);
}


/**
 * mirage_finterface_binary_subchannel_file_set_format:
 * @self: a #MIRAGE_FInterface_BINARY
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
gboolean mirage_finterface_binary_subchannel_file_set_format (MIRAGE_FInterface_BINARY *self, gint format, GError **error) {
    return MIRAGE_FINTERFACE_BINARY_GET_CLASS(self)->subchannel_file_set_format(self, format, error);
}

/**
 * mirage_finterface_binary_subchannel_file_get_format:
 * @self: a #MIRAGE_FInterface_BINARY
 * @format: location to store subchannel data format
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves subchannel file data format.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_finterface_binary_subchannel_file_get_format (MIRAGE_FInterface_BINARY *self, gint *format, GError **error) {
    return MIRAGE_FINTERFACE_BINARY_GET_CLASS(self)->subchannel_file_get_format(self, format, error);
}


/**
 * mirage_finterface_binary_subchannel_file_get_position:
 * @self: a #MIRAGE_FInterface_BINARY
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
gboolean mirage_finterface_binary_subchannel_file_get_position (MIRAGE_FInterface_BINARY *self, gint address, guint64 *position, GError **error) {
    return MIRAGE_FINTERFACE_BINARY_GET_CLASS(self)->subchannel_file_get_position(self, address, position, error);
}


GType mirage_finterface_binary_get_type (void) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_FInterface_BINARYClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            NULL,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            0,
            0,      /* n_preallocs */
            NULL    /* instance_init */
        };
        
        type = g_type_register_static(G_TYPE_INTERFACE, "MIRAGE_FInterface_BINARYClass", &info, 0);
    }
    
    return type;
}


/******************************************************************************\
 *                                AUDIO interface                             *
\******************************************************************************/
/**
 * mirage_finterface_audio_set_file:
 * @self: a #MIRAGE_FInterface_AUDIO
 * @filename: filename
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets audio file to @filename.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_finterface_audio_set_file (MIRAGE_FInterface_AUDIO *self, const gchar *filename, GError **error) {
    return MIRAGE_FINTERFACE_AUDIO_GET_CLASS(self)->set_file(self, filename, error);
}

/**
 * mirage_finterface_audio_get_file:
 * @self: a #MIRAGE_FInterface_AUDIO
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
gboolean mirage_finterface_audio_get_file (MIRAGE_FInterface_AUDIO *self, const gchar **filename, GError **error) {
    return MIRAGE_FINTERFACE_AUDIO_GET_CLASS(self)->get_file(self, filename, error);
}

/**
 * mirage_finterface_audio_set_offset:
 * @self: a #MIRAGE_FInterface_AUDIO
 * @offset: offset
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets offset within audio file, in sectors.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_finterface_audio_set_offset (MIRAGE_FInterface_AUDIO *self, gint offset, GError **error) {
    return MIRAGE_FINTERFACE_AUDIO_GET_CLASS(self)->set_offset(self, offset, error);
}

/**
 * mirage_finterface_audio_get_offset:
 * @self: a #MIRAGE_FInterface_AUDIO
 * @offset: location to store offset
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves offset within audio file, in sectors.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_finterface_audio_get_offset (MIRAGE_FInterface_AUDIO *self, gint *offset, GError **error) {
    return MIRAGE_FINTERFACE_AUDIO_GET_CLASS(self)->get_offset(self, offset, error);
}

GType mirage_finterface_audio_get_type (void) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_FInterface_AUDIOClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            NULL,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            0,
            0,      /* n_preallocs */
            NULL    /* instance_init */
        };
        
        type = g_type_register_static(G_TYPE_INTERFACE, "MIRAGE_FInterface_AUDIOClass", &info, 0);
    }
    
    return type;
}
