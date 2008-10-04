/*
 *  libMirage: ISO image parser: Disc object
 *  Copyright (C) 2006-2008 Rok Mandeljc
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

#include "image-iso.h"


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_DISC_ISO_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_DISC_ISO, MIRAGE_Disc_ISOPrivate))

typedef struct {    
    /* Parser info */
    MIRAGE_ParserInfo *parser_info;
} MIRAGE_Disc_ISOPrivate;


/******************************************************************************\
 *                     MIRAGE_Disc methods implementation                     *
\******************************************************************************/
static gboolean __mirage_disc_iso_get_parser_info (MIRAGE_Disc *self, MIRAGE_ParserInfo **parser_info, GError **error) {
    MIRAGE_Disc_ISOPrivate *_priv = MIRAGE_DISC_ISO_GET_PRIVATE(self);
    *parser_info = _priv->parser_info;
    return TRUE;
}

static gchar *vd_type[] = {
    "BOOT",
    "PRI",
    "SUPP",
    "PART",
    "TERM",
    "N/A"
};

static gboolean __mirage_disc_iso_can_load_file (MIRAGE_Disc *self, gchar *filename, GError **error) {
    MIRAGE_Disc_ISOPrivate *_priv = MIRAGE_DISC_ISO_GET_PRIVATE(self);
    gboolean valid_iso = FALSE;

    /* Does file exist? */
    if (!g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
        return FALSE;
    }
    
    /* Check supported suffixes */
    if (!mirage_helper_match_suffixes(filename, _priv->parser_info->suffixes)) {
        return FALSE;
    }
    
    /* Mode 1/Mode 2 Form 1 Images need to pass extra check */
    if (mirage_helper_has_suffix(filename, ".iso") 
        || mirage_helper_has_suffix(filename, ".udf")
        || mirage_helper_has_suffix(filename, ".img")) {
        struct stat st;
        FILE *file = NULL;
        struct iso_volume_descriptor VSD;
        gchar *type_str = NULL;
        size_t blocks_read;

        /* Stat */
        if (g_stat(filename, &st) < 0) {
            return FALSE;
        }
        
        /* Since it's Mode 1/Mode 2 Form 1 track, its length should be divisible
           by 2048 */
        if (st.st_size % ISOFS_BLOCK_SIZE) {
            return FALSE;
        }
    
        /* Last test; ISO-9660 or UDF image has a valid ISO volume descriptor 
           at the beginning of the 16th sector. For the fun of it we list all
           volume descriptors. */
        file = g_fopen(filename, "r");
        if (!file) {
            return FALSE;
        }

        fseeko(file, 16 * ISOFS_BLOCK_SIZE, SEEK_SET);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Volume Descriptors:\n", __func__);
        do {
            blocks_read = fread(&VSD, sizeof(struct iso_volume_descriptor), 1, file);
            if (blocks_read < 1) return FALSE;
            switch(VSD.type) {
                case ISO_VD_BOOT_RECORD:
                case ISO_VD_PRIMARY:
                case ISO_VD_SUPPLEMENTARY:
                case ISO_VD_PARTITION:
                    type_str = vd_type[VSD.type];
                    break;
                case ISO_VD_END: /* 255 */
                    type_str = vd_type[4];
                    break;
                default:
                    type_str = vd_type[5];
                    break;
            }
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Type: %s (%i), ID: '%.5s', version: %i.\n", __func__, type_str, VSD.type, VSD.id, VSD.version);
            if (!memcmp(VSD.id, ISO_STANDARD_ID, sizeof(VSD.id)) && (VSD.type == ISO_VD_PRIMARY)) {
                valid_iso = TRUE;
            }
        } while((VSD.type != ISO_VD_END) && !feof(file));
        fclose(file);
    }

    return valid_iso;
}

static gboolean __mirage_disc_iso_load_track (MIRAGE_Disc *self, gchar *filename, GError **error) {
    gboolean succeeded = TRUE;
    GObject *session = NULL;
    GObject *track = NULL;
    
    if (!g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open file '%s'!\n", __func__, filename);
        mirage_error(MIRAGE_E_DATAFILE, error);
        return FALSE;
    }
    
    mirage_disc_get_session_by_index(self, -1, &session, NULL);
    
    /* Add track */
    succeeded = mirage_session_add_track_by_index(MIRAGE_SESSION(session), -1, &track, error);
    g_object_unref(session);
    if (!succeeded) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __func__);
        return succeeded;
    }
    
    /* Create fragment */
    GObject *mirage = NULL;
    if (!mirage_object_get_mirage(MIRAGE_OBJECT(self), &mirage, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get Mirage object!\n", __func__);
        g_object_unref(track);
        return FALSE;
    }
    
    GObject *data_fragment = NULL;
    
    /* Determine whether we have Mode 1 (ISO) or Audio (WAV) file */
    if (mirage_helper_has_suffix(filename, ".iso")
        || mirage_helper_has_suffix(filename, ".img")) {
        /* Mode 1/Mode 2 Form 1... BINARY fragment interface is used */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: data file, using BINARY fragment interface\n", __func__);

        mirage_mirage_create_fragment(MIRAGE_MIRAGE(mirage), MIRAGE_TYPE_FINTERFACE_BINARY, filename, &data_fragment, error);
        g_object_unref(mirage);
        if (!data_fragment) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create BINARY fragment!\n", __func__);
            g_object_unref(track);
            return FALSE;
        }
        
        /* Set track file */
        mirage_finterface_binary_track_file_set_handle(MIRAGE_FINTERFACE_BINARY(data_fragment), g_fopen(filename, "r"), NULL);
        mirage_finterface_binary_track_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), ISOFS_BLOCK_SIZE, NULL);
        mirage_finterface_binary_track_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), FR_BIN_TFILE_DATA, NULL);        
        
        /* Set track mode */
        mirage_track_set_mode(MIRAGE_TRACK(track), MIRAGE_MODE_MODE1, NULL);
    } else {
        /* Assume audio... AUDIO fragment interface is used */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: audio file, using AUDIO fragment interface\n", __func__);
        
        mirage_mirage_create_fragment(MIRAGE_MIRAGE(mirage), MIRAGE_TYPE_FINTERFACE_AUDIO, filename, &data_fragment, error);
        g_object_unref(mirage);
        if (!data_fragment) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create AUDIO fragment!\n", __func__);
            g_object_unref(track);
            return FALSE;
        }
        
        /* Set file */
        if (!mirage_finterface_audio_set_file(MIRAGE_FINTERFACE_AUDIO(data_fragment), filename, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set file!\n", __func__);
            g_object_unref(data_fragment);
            g_object_unref(track);
            return FALSE;
        }
        
        /* Set track mode */
        mirage_track_set_mode(MIRAGE_TRACK(track), MIRAGE_MODE_AUDIO, NULL);   
    }
    
    /* Use whole file */
    if (!mirage_fragment_use_the_rest_of_file(MIRAGE_FRAGMENT(data_fragment), error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to use the rest of file!\n", __func__);
        g_object_unref(data_fragment);
        g_object_unref(track);
        return FALSE;
    }
        
    /* Add fragment */
    if (!mirage_track_add_fragment(MIRAGE_TRACK(track), -1, &data_fragment, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add fragment!\n", __func__);
        g_object_unref(data_fragment);
        g_object_unref(track);
        return FALSE;
    }
        
    g_object_unref(data_fragment);
    
    g_object_unref(track);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished loading track\n", __func__);
    
    return TRUE;    
}

static gboolean __mirage_disc_iso_load_image (MIRAGE_Disc *self, gchar **filenames, GError **error) {
    GObject *session = NULL;
    gint length = 0;
    gint i;
    
    /* Set filenames */
    mirage_disc_set_filenames(self, filenames, NULL);
    
    /* Session: one session (with possibly multiple tracks) */
    if (!mirage_disc_add_session_by_number(self, 1, &session, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __func__);
        return FALSE;
    }
    
    /* Data track is always assumed to be Mode 1, and it can be combined with
       audio tracks, so we're more or less getting regular CD-ROM sessions, right? */
    mirage_session_set_session_type(MIRAGE_SESSION(session), MIRAGE_SESSION_CD_ROM, NULL);
    g_object_unref(session);

    /* Load track(s) */
    for (i = 0; i < g_strv_length(filenames); i++) {
        /* Load track(s) */
        if (!__mirage_disc_iso_load_track(self, filenames[i], error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to load track %i!\n", __func__, i);
            return FALSE;
        }
    }
    
    /* Now guess medium type and if it's a CD-ROM, add Red Book pregap */
    gint medium_type = mirage_helper_guess_medium_type(self);
    mirage_disc_set_medium_type(self, medium_type, NULL);
    if (medium_type == MIRAGE_MEDIUM_CD) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: disc length implies CD-ROM image; setting Red Book pregaps\n", __func__);
        mirage_helper_add_redbook_pregap(self);
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: disc length implies non CD-ROM image\n", __func__);
    }
    
    return TRUE;
}


/******************************************************************************\
 *                                Object init                                 *
\******************************************************************************/
/* Our parent class */
static MIRAGE_DiscClass *parent_class = NULL;

static void __mirage_disc_iso_instance_init (GTypeInstance *instance, gpointer g_class) {
    MIRAGE_Disc_ISO *self = MIRAGE_DISC_ISO(instance);
    MIRAGE_Disc_ISOPrivate *_priv = MIRAGE_DISC_ISO_GET_PRIVATE(self);
    
    /* Create parser info */
    _priv->parser_info = mirage_helper_create_parser_info(
        "PARSER-ISO",
        "ISO Image Parser",
        "1.0.0",
        "Rok Mandeljc",
        TRUE,
        "ISO images",
        5, ".iso", ".udf", ".img", ".wav", NULL
    );
    
    return;
}

static void __mirage_disc_iso_finalize (GObject *obj) {
    MIRAGE_Disc_ISO *self = MIRAGE_DISC_ISO(obj);
    MIRAGE_Disc_ISOPrivate *_priv = MIRAGE_DISC_ISO_GET_PRIVATE(self);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s:\n", __func__);
    
    /* Free parser info */
    mirage_helper_destroy_parser_info(_priv->parser_info);
    
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __func__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}


static void __mirage_disc_iso_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_DiscClass *class_disc = MIRAGE_DISC_CLASS(g_class);
    MIRAGE_Disc_ISOClass *klass = MIRAGE_DISC_ISO_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Disc_ISOPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_disc_iso_finalize;
    
    /* Initialize MIRAGE_Disc methods */
    class_disc->get_parser_info = __mirage_disc_iso_get_parser_info;
    class_disc->can_load_file = __mirage_disc_iso_can_load_file;
    class_disc->load_image = __mirage_disc_iso_load_image;
        
    return;
}

GType mirage_disc_iso_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Disc_ISOClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_disc_iso_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Disc_ISO),
            0,      /* n_preallocs */
            __mirage_disc_iso_instance_init    /* instance_init */
        };
        
        type = g_type_module_register_type(module, MIRAGE_TYPE_DISC, "MIRAGE_Disc_ISO", &info, 0);
    }
    
    return type;
}
