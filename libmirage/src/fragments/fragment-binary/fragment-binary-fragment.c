/*
 *  libMirage: BINARY fragment: Fragment object
 *  Copyright (C) 2007-2008 Rok Mandeljc
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


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_FRAGMENT_BINARY_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FRAGMENT_BINARY, MIRAGE_Fragment_BINARYPrivate))

typedef struct {    
    FILE *tfile_handle;   /* Handle of track file */
    gint tfile_sectsize;  /* Track file sector size */
    gint tfile_format;    /* Track file format */
    guint64 tfile_offset; /* Track offset in track file */
        
    FILE *sfile_handle;   /* Handle of subchannel file */
    gint sfile_sectsize;  /* Subchannel file sector size*/
    gint sfile_format;    /* Subchannel file format */
    guint64 sfile_offset; /* Subchannel offset in subchannel file */
} MIRAGE_Fragment_BINARYPrivate;


/******************************************************************************\
 *                      Interface implementation: BINARY                      *
\******************************************************************************/
static gboolean __mirage_fragment_binary_track_file_set_handle (MIRAGE_FInterface_BINARY *self, FILE *file, GError **error) {
    MIRAGE_Fragment_BINARYPrivate *_priv = MIRAGE_FRAGMENT_BINARY_GET_PRIVATE(self);
    /* Set file handle, but free old one first (if provided) */
    if (_priv->tfile_handle) {
        fclose(_priv->tfile_handle);
    }
    _priv->tfile_handle = file;
    return TRUE;
}

static gboolean __mirage_fragment_binary_track_file_get_handle (MIRAGE_FInterface_BINARY *self, FILE **file, GError **error) {
    MIRAGE_Fragment_BINARYPrivate *_priv = MIRAGE_FRAGMENT_BINARY_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(file);
    /* Return file handle */
    *file = _priv->tfile_handle;
    return TRUE;
}


static gboolean __mirage_fragment_binary_track_file_set_offset (MIRAGE_FInterface_BINARY *self, guint64 offset, GError **error) {
    MIRAGE_Fragment_BINARYPrivate *_priv = MIRAGE_FRAGMENT_BINARY_GET_PRIVATE(self);
    /* Set offset */
    _priv->tfile_offset = offset;
    return TRUE;
}

static gboolean __mirage_fragment_binary_track_file_get_offset (MIRAGE_FInterface_BINARY *self, guint64 *offset, GError **error) {
    MIRAGE_Fragment_BINARYPrivate *_priv = MIRAGE_FRAGMENT_BINARY_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(offset);
    /* Return offset */
    *offset = _priv->tfile_offset;
    return TRUE;
}


static gboolean __mirage_fragment_binary_track_file_set_sectsize (MIRAGE_FInterface_BINARY *self, gint sectsize, GError **error) {
    MIRAGE_Fragment_BINARYPrivate *_priv = MIRAGE_FRAGMENT_BINARY_GET_PRIVATE(self);
    /* Set sector size */
    _priv->tfile_sectsize = sectsize;
    return TRUE;
}

static gboolean __mirage_fragment_binary_track_file_get_sectsize (MIRAGE_FInterface_BINARY *self, gint *sectsize, GError **error) {
    MIRAGE_Fragment_BINARYPrivate *_priv = MIRAGE_FRAGMENT_BINARY_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(sectsize);
    /* Return sector size */
    *sectsize = _priv->tfile_sectsize;
    return TRUE;
}


static gboolean __mirage_fragment_binary_track_file_set_format (MIRAGE_FInterface_BINARY *self, gint format, GError **error) {
    MIRAGE_Fragment_BINARYPrivate *_priv = MIRAGE_FRAGMENT_BINARY_GET_PRIVATE(self);
    /* Set format */
    _priv->tfile_format = format;
    return TRUE;
}

static gboolean __mirage_fragment_binary_track_file_get_format (MIRAGE_FInterface_BINARY *self, gint *format, GError **error) {
    MIRAGE_Fragment_BINARYPrivate *_priv = MIRAGE_FRAGMENT_BINARY_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(format);
    /* Return format */
    *format = _priv->tfile_format;
    return TRUE;
}


static gboolean __mirage_fragment_binary_track_file_get_position (MIRAGE_FInterface_BINARY *self, gint address, guint64 *position, GError **error) {
    MIRAGE_Fragment_BINARYPrivate *_priv = MIRAGE_FRAGMENT_BINARY_GET_PRIVATE(self);
    guint64 tmp_offset = 0;
    gint sectsize_full = 0;

    MIRAGE_CHECK_ARG(position);
            
    /* Calculate 'full' sector size:
        -> track data + subchannel data, if there's internal subchannel
        -> track data, if there's external or no subchannel
    */
    
    sectsize_full = _priv->tfile_sectsize;
    if (_priv->sfile_format & FR_BIN_SFILE_INT) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: internal subchannel, adding %d to sector size %d\n", __debug__, _priv->sfile_sectsize, sectsize_full);
        sectsize_full += _priv->sfile_sectsize;
    }
    
    /* We assume address is relative address */
    /* guint64 casts are required so that the product us 64-bit; product of two
       32-bit integers would be 32-bit, which would be truncated at overflow... */
    tmp_offset = _priv->tfile_offset + (guint64)address * (guint64)sectsize_full;

    *position = tmp_offset;
    return TRUE;
}


static gboolean __mirage_fragment_binary_subchannel_file_set_handle (MIRAGE_FInterface_BINARY *self, FILE *file, GError **error) {
    MIRAGE_Fragment_BINARYPrivate *_priv = MIRAGE_FRAGMENT_BINARY_GET_PRIVATE(self);
    /* Set file handle, but free old one first (if provided) */
    if (_priv->sfile_handle) {
        fclose(_priv->sfile_handle);
    }
    _priv->sfile_handle = file;
    return TRUE;
}

static gboolean __mirage_fragment_binary_subchannel_file_get_handle (MIRAGE_FInterface_BINARY *self, FILE **file, GError **error) {
    MIRAGE_Fragment_BINARYPrivate *_priv = MIRAGE_FRAGMENT_BINARY_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(file);
    /* Return file handle */
    *file = _priv->sfile_handle;
    return TRUE;
}


static gboolean __mirage_fragment_binary_subchannel_file_set_offset (MIRAGE_FInterface_BINARY *self, guint64 offset, GError **error) {
    MIRAGE_Fragment_BINARYPrivate *_priv = MIRAGE_FRAGMENT_BINARY_GET_PRIVATE(self);
    /* Set offset */
    _priv->sfile_offset = offset;
    return TRUE;
}

static gboolean __mirage_fragment_binary_subchannel_file_get_offset (MIRAGE_FInterface_BINARY *self, guint64 *offset, GError **error) {
    MIRAGE_Fragment_BINARYPrivate *_priv = MIRAGE_FRAGMENT_BINARY_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(offset);
    /* Return offset */
    *offset = _priv->sfile_offset;
    return TRUE;
}

static gboolean __mirage_fragment_binary_subchannel_file_set_sectsize (MIRAGE_FInterface_BINARY *self, gint sectsize, GError **error) {
    MIRAGE_Fragment_BINARYPrivate *_priv = MIRAGE_FRAGMENT_BINARY_GET_PRIVATE(self);
    /* Set sector size */
    _priv->sfile_sectsize = sectsize;
    return TRUE;
}

static gboolean __mirage_fragment_binary_subchannel_file_get_sectsize (MIRAGE_FInterface_BINARY *self, gint *sectsize, GError **error) {
    MIRAGE_Fragment_BINARYPrivate *_priv = MIRAGE_FRAGMENT_BINARY_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(sectsize);
    /* Return sector size */
    *sectsize = _priv->sfile_sectsize;
    return TRUE;
}


static gboolean __mirage_fragment_binary_subchannel_file_set_format (MIRAGE_FInterface_BINARY *self, gint format, GError **error) {
    MIRAGE_Fragment_BINARYPrivate *_priv = MIRAGE_FRAGMENT_BINARY_GET_PRIVATE(self);
    /* Set format */
    _priv->sfile_format = format;
    return TRUE;
}

static gboolean __mirage_fragment_binary_subchannel_file_get_format (MIRAGE_FInterface_BINARY *self, gint *format, GError **error) {
    MIRAGE_Fragment_BINARYPrivate *_priv = MIRAGE_FRAGMENT_BINARY_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(format);
    /* Return format */
    *format = _priv->sfile_format;
    return TRUE;
}


static gboolean __mirage_fragment_binary_subchannel_file_get_position (MIRAGE_FInterface_BINARY *self, gint address, guint64 *position, GError **error) {
    MIRAGE_Fragment_BINARYPrivate *_priv = MIRAGE_FRAGMENT_BINARY_GET_PRIVATE(self);
    guint64 tmp_offset = 0;

    MIRAGE_CHECK_ARG(position);
    
    /* Either we have internal or external subchannel */
    if (_priv->sfile_format & FR_BIN_SFILE_INT) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: internal subchannel, position is at end of main channel data\n", __debug__);
        /* Subchannel is contained in track file; get position in track file
           for that sector, and add to it length of track data sector */
        if (!mirage_finterface_binary_track_file_get_position(MIRAGE_FINTERFACE_BINARY(self), address, &tmp_offset, error)) {
            return FALSE;
        }
        
        tmp_offset += _priv->tfile_sectsize;
    } else if (_priv->sfile_format & FR_BIN_SFILE_EXT) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: external subchannel, calculating position\n", __debug__);
        /* We assume address is relative address */
        /* guint64 casts are required so that the product us 64-bit; product of two
           32-bit integers would be 32-bit, which would be truncated at overflow... */
        tmp_offset = _priv->sfile_offset + (guint64)address * (guint64)_priv->sfile_sectsize;
    }
    
    *position = tmp_offset;
    return TRUE;
}


/******************************************************************************\
 *                   MIRAGE_Fragment methods implementations                  *
\******************************************************************************/
static gboolean __mirage_fragment_binary_can_handle_data_format (MIRAGE_Fragment *self, gchar *filename, GError **error) {
    /* BINARY doesn't need any data file checks; what's important is interface type,
       which is filtered out elsewhere */
    return TRUE;
}

static gboolean __mirage_fragment_binary_use_the_rest_of_file (MIRAGE_Fragment *self, GError **error) {
    MIRAGE_Fragment_BINARYPrivate *_priv = MIRAGE_FRAGMENT_BINARY_GET_PRIVATE(MIRAGE_FRAGMENT_BINARY(self));
    struct stat st;
    gint full_sectsize = 0;
    gint fragment_len = 0;
    
    if (!_priv->tfile_handle) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: file not set!\n", __debug__);
        mirage_error(MIRAGE_E_FILENOTSET, error);
        return FALSE;
    }
    
    /* Get file length */
    if (fstat(fileno(_priv->tfile_handle), &st) < 0) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to stat data file!\n", __debug__);
        mirage_error(MIRAGE_E_DATAFILE, error);
        return FALSE;
    }
    
    /* Use all data from offset on... */
    full_sectsize = _priv->tfile_sectsize;
    if (_priv->sfile_format & FR_BIN_SFILE_INT) {
        full_sectsize += _priv->sfile_sectsize;
    }
    
    fragment_len = (st.st_size - _priv->tfile_offset) / full_sectsize;
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: using the rest of file (%d sectors)\n", __debug__, fragment_len);
    
    /* Set the length */
    return mirage_fragment_set_length(self, fragment_len, error);
}

static gboolean __mirage_fragment_binary_read_main_data (MIRAGE_Fragment *self, gint address, guint8 *buf, gint *length, GError **error) {
    MIRAGE_Fragment_BINARYPrivate *_priv = MIRAGE_FRAGMENT_BINARY_GET_PRIVATE(MIRAGE_FRAGMENT_BINARY(self));
    guint64 position = 0;
    gint read_len = 0;
    
    /* We need file to read data from... but if it's missing, we don't read
       anything and this is not considered an error */
    if (!_priv->tfile_handle) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: no file handle!\n", __debug__);
        if (length) {
            *length = 0;
        }
        return TRUE;
    }
    
    /* Determine position within file */
    if (!mirage_finterface_binary_track_file_get_position(MIRAGE_FINTERFACE_BINARY(self), address, &position, error)) {
        return FALSE;
    }
    
    if (buf) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: reading from position 0x%llX\n", __debug__, position);
        fseeko(_priv->tfile_handle, position, SEEK_SET);
        read_len = fread(buf, 1, _priv->tfile_sectsize, _priv->tfile_handle);
        
        if (read_len != _priv->tfile_sectsize) {
            /* If we were really strict, we'd bail with error here. However, since
               we live in a world with mini images, we should turn our backs here
               and hope that things work out for the best... If we're indeed reading
               from mini image, then at this point, data we should've read won't 
               matter at anyway. */
            /*mirage_error(MIRAGE_E_READFAILED, error);
            return FALSE;*/
        }
        
        /* Binary audio files may need to be swapped from BE to LE */
        if (_priv->tfile_format == FR_BIN_TFILE_AUDIO_SWAP) {
            gint i;
            for (i = 0; i < read_len; i+=2) {
                guint16 *ptr = (guint16 *)&buf[i];
                *ptr = GUINT16_SWAP_LE_BE(*ptr);
            }
        }
    }
    
    if (length) {
        *length = _priv->tfile_sectsize;
    }
    
    return TRUE;
}

static gboolean __mirage_fragment_binary_read_subchannel_data (MIRAGE_Fragment *self, gint address, guint8 *buf, gint *length, GError **error) {
    MIRAGE_Fragment_BINARYPrivate *_priv = MIRAGE_FRAGMENT_BINARY_GET_PRIVATE(MIRAGE_FRAGMENT_BINARY(self));
    FILE *file_handle = NULL;
    guint64 position = 0;
    gint read_len = 0;
    
    
    /* If there's no subchannel, return 0 for the length */
    if (!_priv->sfile_sectsize) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: no subchannel (sectsize = 0)!\n", __debug__);
        if (length) {
            *length = 0;
        }
        return TRUE;
    }
    
    /* We need file to read data from... but if it's missing, we don't read
       anything and this is not considered an error */
    if (_priv->sfile_format & FR_BIN_SFILE_INT) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: internal subchannel, using track file handle\n", __debug__);
        file_handle = _priv->tfile_handle;
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: external subchannel, using track file handle\n", __debug__);
        file_handle = _priv->sfile_handle;        
    }
    
    if (!file_handle) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: no file handle!\n", __debug__);
        if (length) {
            *length = 0;
        }
        return TRUE;
    }
    
    
    /* Determine position within file */
    if (!mirage_finterface_binary_subchannel_file_get_position(MIRAGE_FINTERFACE_BINARY(self), address, &position, error)) {
        return FALSE;
    }
    
    /* Read only if there's buffer to read into */
    if (buf) {
        guint8 tmp_buf[96];
                
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: reading from position 0x%llX\n", __debug__, position);
        /* We read into temporary buffer, because we might need to perform some
           magic on the data */
        fseeko(file_handle, position, SEEK_SET);
        read_len = fread(tmp_buf, 1, _priv->sfile_sectsize, file_handle);

        if (read_len != _priv->sfile_sectsize) {
            mirage_error(MIRAGE_E_READFAILED, error);
            return FALSE;
        }
        
        /* If we happen to deal with anything that's not RAW 96-byte interleaved PW,
           we transform it into that here... less fuss for upper level stuff this way */
        if (_priv->sfile_format & FR_BIN_SFILE_PW96_LIN) {
            /* 96-byte deinterleaved PW; grab each subchannel and interleave it 
               into destination buffer */
            gint i;
            
            for (i = 0; i < 8; i++) {
                guint8 *ptr = tmp_buf + i*12;
                mirage_helper_subchannel_interleave(7 - i, ptr, buf);
            }            
        } else if (_priv->sfile_format & FR_BIN_SFILE_PW96_INT) {
            /* 96-byte interleaved PW; just copy it */
            memcpy(buf, tmp_buf, 96);        
        } else if (_priv->sfile_format & FR_BIN_SFILE_PQ16) {
            /* 16-byte PQ; interleave it and pretend everything else's 0 */
            mirage_helper_subchannel_interleave(SUBCHANNEL_Q, tmp_buf, buf);
        }
    }
    
    if (length) {
        *length = 96; /* Always 96, because we do the processing here */
    }
    
    return TRUE;
}


/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static MIRAGE_FragmentClass *parent_class = NULL;

static void __mirage_fragment_binary_instance_init (GTypeInstance *instance, gpointer g_class) {
    /* Create fragment info */
    mirage_fragment_generate_fragment_info(MIRAGE_FRAGMENT(instance),
        "FRAGMENT-BINARY",
        "Binary Fragment"
    );
    
    return;
}

static void __mirage_fragment_binary_finalize (GObject *obj) {
    MIRAGE_Fragment_BINARY *self = MIRAGE_FRAGMENT_BINARY(obj);
    MIRAGE_Fragment_BINARYPrivate *_priv = MIRAGE_FRAGMENT_BINARY_GET_PRIVATE(self);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: finalizing object\n", __debug__);

    if (_priv->tfile_handle) {
        fclose(_priv->tfile_handle);
    }
    if (_priv->sfile_handle) {
        fclose(_priv->sfile_handle);
    }
        
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __debug__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __mirage_fragment_binary_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_FragmentClass *class_fragment = MIRAGE_FRAGMENT_CLASS(g_class);
    MIRAGE_Fragment_BINARYClass *klass = MIRAGE_FRAGMENT_BINARY_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Fragment_BINARYPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_fragment_binary_finalize;
    
    /* Initialize MIRAGE_Fragment methods */
    class_fragment->can_handle_data_format = __mirage_fragment_binary_can_handle_data_format;
    class_fragment->use_the_rest_of_file = __mirage_fragment_binary_use_the_rest_of_file;
    class_fragment->read_main_data = __mirage_fragment_binary_read_main_data;
    class_fragment->read_subchannel_data = __mirage_fragment_binary_read_subchannel_data;
        
    return;
}

static void __mirage_fragment_binary_interface_init (gpointer g_iface, gpointer iface_data) {
    MIRAGE_FInterface_BINARYClass *klass = (MIRAGE_FInterface_BINARYClass *)g_iface;
    
    /* Initialize MIRAGE_FInterface_BINARY methods */
    klass->track_file_set_handle = __mirage_fragment_binary_track_file_set_handle;
    klass->track_file_get_handle = __mirage_fragment_binary_track_file_get_handle;
    klass->track_file_set_offset = __mirage_fragment_binary_track_file_set_offset;
    klass->track_file_get_offset = __mirage_fragment_binary_track_file_get_offset;
    klass->track_file_set_sectsize = __mirage_fragment_binary_track_file_set_sectsize;
    klass->track_file_get_sectsize = __mirage_fragment_binary_track_file_get_sectsize;
    klass->track_file_set_format = __mirage_fragment_binary_track_file_set_format;
    klass->track_file_get_format = __mirage_fragment_binary_track_file_get_format;

    klass->track_file_get_position = __mirage_fragment_binary_track_file_get_position;

    klass->subchannel_file_set_handle = __mirage_fragment_binary_subchannel_file_set_handle;
    klass->subchannel_file_get_handle = __mirage_fragment_binary_subchannel_file_get_handle;
    klass->subchannel_file_set_offset = __mirage_fragment_binary_subchannel_file_set_offset;
    klass->subchannel_file_get_offset = __mirage_fragment_binary_subchannel_file_get_offset;
    klass->subchannel_file_set_sectsize = __mirage_fragment_binary_subchannel_file_set_sectsize;
    klass->subchannel_file_get_sectsize = __mirage_fragment_binary_subchannel_file_get_sectsize;
    klass->subchannel_file_set_format = __mirage_fragment_binary_subchannel_file_set_format;
    klass->subchannel_file_get_format = __mirage_fragment_binary_subchannel_file_get_format;

    klass->subchannel_file_get_position = __mirage_fragment_binary_subchannel_file_get_position;

    return;
}

GType mirage_fragment_binary_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Fragment_BINARYClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_fragment_binary_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Fragment_BINARY),
            0,      /* n_preallocs */
            __mirage_fragment_binary_instance_init    /* instance_init */
        };
        
        static const GInterfaceInfo interface_info = {
            (GInterfaceInitFunc) __mirage_fragment_binary_interface_init,   /* interface_init */
            NULL,   /* interface_finalize */
            NULL    /* interface_data */
        };

        type = g_type_module_register_type(module, MIRAGE_TYPE_FRAGMENT, "MIRAGE_Fragment_BINARY", &info, 0);
        
        g_type_module_add_interface(module, type, MIRAGE_TYPE_FINTERFACE_BINARY, &interface_info);
    }
    
    return type;
}
