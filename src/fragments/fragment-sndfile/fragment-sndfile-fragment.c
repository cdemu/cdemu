/*
 *  libMirage: SNDFILE fragment: Fragment object
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

#include "fragment-sndfile.h"


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_FRAGMENT_SNDFILE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FRAGMENT_SNDFILE, MIRAGE_Fragment_SNDFILEPrivate))

typedef struct {   
    gchar *filename;
    SNDFILE *sndfile;
    SF_INFO format;
    sf_count_t offset;
    
    /* Fragment info */
    MIRAGE_FragmentInfo *fragment_info;
} MIRAGE_Fragment_SNDFILEPrivate;


/******************************************************************************\
 *                      Interface implementation: AUDIO                       *
\******************************************************************************/
static gboolean __mirage_fragment_sndfile_set_file (MIRAGE_FInterface_AUDIO *self, gchar *filename, GError **error) {
    MIRAGE_Fragment_SNDFILEPrivate *_priv = MIRAGE_FRAGMENT_SNDFILE_GET_PRIVATE(self);
    
    /* If file's already set, close it and reset format */
    if (_priv->sndfile) {
        sf_close(_priv->sndfile);
        memset(&_priv->format, 0, sizeof(_priv->format));
    }
    
    /* Open file */
    _priv->sndfile = sf_open(filename, SFM_READ, &_priv->format);
    if (!_priv->sndfile) {
        mirage_error(MIRAGE_E_DATAFILE, error);
        return FALSE;
    }
    
    g_free(_priv->filename);
    _priv->filename = g_strdup(filename);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: SNDFILE file format:\n"
            " -> frames = %lli\n"
            " -> samplerate = %i\n"
            " -> channels = %i\n"
            " -> format = 0x%X\n"
            " -> sections = %i\n"
            " -> seekable = %i\n",
            __func__,
            _priv->format.frames, 
            _priv->format.samplerate, 
            _priv->format.channels, 
            _priv->format.format, 
            _priv->format.sections, 
            _priv->format.seekable);
    
    return TRUE;    
}

static gboolean __mirage_fragment_sndfile_get_file (MIRAGE_FInterface_AUDIO *self, gchar **filename, GError **error) {
    MIRAGE_Fragment_SNDFILEPrivate *_priv = MIRAGE_FRAGMENT_SNDFILE_GET_PRIVATE(self);
    /* Return filename */
    *filename = g_strdup(_priv->filename);
    return TRUE;
}

static gboolean __mirage_fragment_sndfile_set_offset (MIRAGE_FInterface_AUDIO *self, gint offset, GError **error) {
    MIRAGE_Fragment_SNDFILEPrivate *_priv = MIRAGE_FRAGMENT_SNDFILE_GET_PRIVATE(self);
    /* Set offset */
    _priv->offset = offset*SNDFILE_FRAMES_PER_SECTOR;
    return TRUE;    
}

static gboolean __mirage_fragment_sndfile_get_offset (MIRAGE_FInterface_AUDIO *self, gint *offset, GError **error) {
    MIRAGE_Fragment_SNDFILEPrivate *_priv = MIRAGE_FRAGMENT_SNDFILE_GET_PRIVATE(self);
    /* Return */
    *offset = _priv->offset/SNDFILE_FRAMES_PER_SECTOR;
    return TRUE;    
}


/******************************************************************************\
 *                   MIRAGE_Fragment methods implementations                  *
\******************************************************************************/
static gboolean __mirage_fragment_sndfile_get_fragment_info (MIRAGE_Fragment *self, MIRAGE_FragmentInfo **fragment_info, GError **error) {
    MIRAGE_Fragment_SNDFILEPrivate *_priv = MIRAGE_FRAGMENT_SNDFILE_GET_PRIVATE(self);
    *fragment_info = _priv->fragment_info;
    return TRUE;
}

static gboolean __mirage_fragment_sndfile_can_handle_data_format (MIRAGE_Fragment *self, gchar *filename, GError **erro) {
    MIRAGE_Fragment_SNDFILEPrivate *_priv = MIRAGE_FRAGMENT_SNDFILE_GET_PRIVATE(self);
    /* Check supported file suffixes */
    /* FIXME: maybe calling a sf_open() and checking its return is a better idea? */
    return mirage_helper_match_suffixes(filename, _priv->fragment_info->suffixes);
}

static gboolean __mirage_fragment_sndfile_use_the_rest_of_file (MIRAGE_Fragment *self, GError **error) {
    MIRAGE_Fragment_SNDFILEPrivate *_priv = MIRAGE_FRAGMENT_SNDFILE_GET_PRIVATE(MIRAGE_FRAGMENT_SNDFILE(self));
    gint fragment_len = 0;
    
    /* Make sure file's loaded */
    if (!_priv->sndfile) {
        mirage_error(MIRAGE_E_FILENOTSET, error);
        return FALSE;
    }
    
    fragment_len = (_priv->format.frames - _priv->offset)/SNDFILE_FRAMES_PER_SECTOR; 
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: using the rest of file (%d sectors)\n", __func__, fragment_len);

    /* Set the length */
    return mirage_fragment_set_length(self, fragment_len, error);
}

static gboolean __mirage_fragment_sndfile_read_main_data (MIRAGE_Fragment *self, gint address, guint8 *buf, gint *length, GError **error) {
    MIRAGE_Fragment_SNDFILEPrivate *_priv = MIRAGE_FRAGMENT_SNDFILE_GET_PRIVATE(MIRAGE_FRAGMENT_SNDFILE(self));
    sf_count_t position = 0;
    sf_count_t read_len = 0;
    
    /* We need file to read data from... but if it's missing, we don't read
       anything and this is not considered an error */
    if (!_priv->sndfile) {
        if (length) {
            *length = 0;
        }
        return TRUE;
    }
    
    /* Determine position within file */
    position = _priv->offset + address*SNDFILE_FRAMES_PER_SECTOR;
       
    if (buf) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: reading from position 0x%llX (frames)\n", __func__, position);
        sf_seek(_priv->sndfile, position, SEEK_SET);
        read_len = sf_readf_short(_priv->sndfile, (short *)buf, SNDFILE_FRAMES_PER_SECTOR);
        
        if (read_len != SNDFILE_FRAMES_PER_SECTOR) {
            mirage_error(MIRAGE_E_READFAILED, error);
            return FALSE;
        }
    }
    
    if (length) {
        *length = 2352; /* Always */
    }
    
    return TRUE;
}

static gboolean __mirage_fragment_sndfile_read_subchannel_data (MIRAGE_Fragment *self, gint address, guint8 *buf, gint *length, GError **error) {
    /* Nothing to read */
    if (length) {
        *length = 0;
    }
    return TRUE;
}


/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static MIRAGE_FragmentClass *parent_class = NULL;

static void __mirage_fragment_sndfile_instance_init (GTypeInstance *instance, gpointer g_class) {
    MIRAGE_Fragment_SNDFILE *self = MIRAGE_FRAGMENT_SNDFILE(instance);
    MIRAGE_Fragment_SNDFILEPrivate *_priv = MIRAGE_FRAGMENT_SNDFILE_GET_PRIVATE(self);
    
    /* Create fragment info */
    _priv->fragment_info = mirage_helper_create_fragment_info(
        "FRAGMENT-SNDFILE",
        "libsndfile Fragment",
        "1.0.0",
        "Rok Mandeljc",
        "MIRAGE_TYPE_FINTERFACE_AUDIO",
        3, ".wav", ".aiff", NULL
    );
    
    return;
}

static void __mirage_fragment_sndfile_finalize (GObject *obj) {
    MIRAGE_Fragment_SNDFILE *self = MIRAGE_FRAGMENT_SNDFILE(obj);
    MIRAGE_Fragment_SNDFILEPrivate *_priv = MIRAGE_FRAGMENT_SNDFILE_GET_PRIVATE(self);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s:\n", __func__);
    
    g_free(_priv->filename);
    if (_priv->sndfile) {
        sf_close(_priv->sndfile);
    }
    
    /* Free fragment info */
    mirage_helper_destroy_fragment_info(_priv->fragment_info);
    
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __func__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __mirage_fragment_sndfile_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_FragmentClass *class_fragment = MIRAGE_FRAGMENT_CLASS(g_class);
    MIRAGE_Fragment_SNDFILEClass *klass = MIRAGE_FRAGMENT_SNDFILE_CLASS(g_class);

    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Fragment_SNDFILEPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_fragment_sndfile_finalize;
    
    /* Initialize MIRAGE_Fragment methods */
    class_fragment->get_fragment_info = __mirage_fragment_sndfile_get_fragment_info;
    class_fragment->can_handle_data_format = __mirage_fragment_sndfile_can_handle_data_format;
    class_fragment->use_the_rest_of_file = __mirage_fragment_sndfile_use_the_rest_of_file;
    class_fragment->read_main_data = __mirage_fragment_sndfile_read_main_data;
    class_fragment->read_subchannel_data = __mirage_fragment_sndfile_read_subchannel_data;
        
    return;
}

static void __mirage_fragment_sndfile_interface_init (gpointer g_iface, gpointer iface_data) {
    MIRAGE_FInterface_AUDIOClass *klass = (MIRAGE_FInterface_AUDIOClass *)g_iface;
    
    /* Initialize MIRAGE_FInterface_AUDIO methods */
    klass->set_file = __mirage_fragment_sndfile_set_file;
    klass->get_file = __mirage_fragment_sndfile_get_file;
    klass->set_offset = __mirage_fragment_sndfile_set_offset;
    klass->get_offset = __mirage_fragment_sndfile_get_offset;

    return;
}

GType mirage_fragment_sndfile_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Fragment_SNDFILEClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_fragment_sndfile_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Fragment_SNDFILE),
            0,      /* n_preallocs */
            __mirage_fragment_sndfile_instance_init    /* instance_init */
        };
        
        static const GInterfaceInfo interface_info = {
            (GInterfaceInitFunc) __mirage_fragment_sndfile_interface_init,   /* interface_init */
            NULL,   /* interface_finalize */
            NULL    /* interface_data */
        };

        type = g_type_module_register_type(module, MIRAGE_TYPE_FRAGMENT, "MIRAGE_Fragment_SNDFILE", &info, 0);
        
        g_type_module_add_interface(module, type, MIRAGE_TYPE_FINTERFACE_AUDIO, &interface_info);
    }
    
    return type;
}
