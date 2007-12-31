/*
 *  libMirage: TOC image parser: Session object
 *  Copyright (C) 2006-2007 Rok Mandeljc
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

#include "image-toc.h"


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_SESSION_TOC_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_SESSION_TOC, MIRAGE_Session_TOCPrivate))

typedef struct {   
    gchar *toc_filename;
        
    gint cur_tfile_sectsize;
    
    gint cur_sfile_sectsize;
    gint cur_sfile_format;
    
    gint cur_langcode;
    GHashTable *lang_map;
} MIRAGE_Session_TOCPrivate;

/******************************************************************************\
 *                       Session private functions                            *
\******************************************************************************/
gboolean __mirage_session_toc_set_toc_filename (MIRAGE_Session *self, gchar *filename, GError **error) {
    MIRAGE_Session_TOC *self_toc = MIRAGE_SESSION_TOC(self);
    MIRAGE_Session_TOCPrivate *_priv = MIRAGE_SESSION_TOC_GET_PRIVATE(self_toc);
    
    _priv->toc_filename = g_strdup(filename);
    
    return TRUE;
}

gboolean __mirage_session_toc_set_mcn (MIRAGE_Session *self, gchar *mcn, GError **error) {
    GObject *disc = NULL;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting MCN: <%s>\n", __func__, mcn);
    
    if (mirage_object_get_parent(MIRAGE_OBJECT(self), &disc, NULL)) {
        mirage_disc_set_mcn(MIRAGE_DISC(disc), mcn, NULL);
        g_object_unref(disc);
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get parent!\n", __func__);
    }
    
    return TRUE;
}

gboolean __mirage_session_toc_set_session_type (MIRAGE_Session *self, gchar *type_string, GError **error) {
    /* Decipher session type */
    static struct {
        gchar *str;
        gint type;
    } session_types[] = {
        {"CD_DA", MIRAGE_SESSION_CD_DA},
        {"CD_ROM", MIRAGE_SESSION_CD_ROM},
        {"CD_ROM_XA", MIRAGE_SESSION_CD_ROM_XA},
        {"CD_I", MIRAGE_SESSION_CD_I},
    };
    gint i;
    
    for (i = 0; i < G_N_ELEMENTS(session_types); i++) {
        if (!mirage_helper_strcasecmp(session_types[i].str, type_string)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: session type: %s\n", __func__, session_types[i].str);
            mirage_session_set_session_type(self, session_types[i].type, NULL);
            break;
        }
    }
    
    return TRUE;
}

gboolean __mirage_session_toc_add_track (MIRAGE_Session *self, gchar *mode_string, gchar *subchan_string, GError **error) {
    MIRAGE_Session_TOC *self_toc = MIRAGE_SESSION_TOC(self);
    MIRAGE_Session_TOCPrivate *_priv = MIRAGE_SESSION_TOC_GET_PRIVATE(self_toc);

    GObject *cur_track = NULL;
    
    if (!mirage_session_add_track_by_index(self, -1, &cur_track, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __func__);
        return FALSE;
    }
    
    /* Clear internal data */
    _priv->cur_tfile_sectsize = 0;
    _priv->cur_sfile_sectsize = 0;
    _priv->cur_sfile_format = 0;
    
    /* Decipher mode */
    struct {
        gchar *str;
        gint mode;
        gint sectsize;
    } track_modes[] = {
        {"AUDIO", MIRAGE_MODE_AUDIO, 2352},
        {"MODE1", MIRAGE_MODE_MODE1, 2048},
        {"MODE1_RAW", MIRAGE_MODE_MODE1, 2352},
        {"MODE2", MIRAGE_MODE_MODE2, 2336},
        {"MODE2_FORM1", MIRAGE_MODE_MODE2_FORM1, 2048},
        {"MODE2_FORM2", MIRAGE_MODE_MODE2_FORM2, 2324},
        {"MODE2_FORM_MIX", MIRAGE_MODE_MODE2_MIXED, 2336},
        {"MODE2_RAW", MIRAGE_MODE_MODE2_MIXED, 2352},
        
    };
    gint i;
    for (i = 0; i < G_N_ELEMENTS(track_modes); i++) {
        if (!mirage_helper_strcasecmp(track_modes[i].str, mode_string)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track mode: %s\n", __func__, track_modes[i].str);
            
            /* Set track mode */
            mirage_track_set_mode(MIRAGE_TRACK(cur_track), track_modes[i].mode, NULL);
            /* Store sector size */
            _priv->cur_tfile_sectsize = track_modes[i].sectsize;
            
            break;
        }
    }
    
    if (subchan_string) {
        /* Decipher subchannel (if provided) */
        static struct {
            gchar *str;
            gint sectsize;
            gint format;
        } subchan_modes[] = {
            {"RW_RAW", FR_BIN_SFILE_PW96_INT | FR_BIN_SFILE_INT, 96 },
            {"RW", FR_BIN_SFILE_RW96 | FR_BIN_SFILE_INT, 96 },
        };
        
        gint i;
        for (i = 0; i < G_N_ELEMENTS(subchan_modes); i++) {
            if (!strcasecmp(subchan_modes[i].str, subchan_string)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: subchannel mode: %s\n", __func__, subchan_modes[i].str);
                _priv->cur_sfile_sectsize = subchan_modes[i].sectsize;
                _priv->cur_sfile_format = subchan_modes[i].format;
                break;
            }
        }
    }
    
    g_object_unref(cur_track);
    
    return TRUE;
};


gboolean __mirage_session_toc_add_track_fragment (MIRAGE_Session *self, gint type, gchar *filename_string, gint base_offset, gint start, gint length, GError **error) {
    MIRAGE_Session_TOC *self_toc = MIRAGE_SESSION_TOC(self);
    MIRAGE_Session_TOCPrivate *_priv = MIRAGE_SESSION_TOC_GET_PRIVATE(self_toc);
       
    GObject *cur_track = NULL;
    
    if (!mirage_session_get_track_by_index(self, -1, &cur_track, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current track!\n", __func__);
        return FALSE;
    }
    
    /* Get Mirage and have it make us a fragment */
    GObject *mirage = NULL;
    if (!mirage_object_get_mirage(MIRAGE_OBJECT(self), &mirage, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get Mirage object!\n", __func__);
        g_object_unref(cur_track);
        return FALSE;
    }
    
    /* Create appropriate fragment */
    GObject *data_fragment = NULL;
    if (type == TOC_DATA_TYPE_NONE) {
        /* Empty fragment; we'd like a NULL fragment */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating NULL fragment\n", __func__);
        mirage_mirage_create_fragment(MIRAGE_MIRAGE(mirage), MIRAGE_TYPE_FINTERFACE_NULL, "NULL", &data_fragment, error);
        g_object_unref(mirage);
        if (!data_fragment) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create NULL fragment!\n", __func__);
            g_object_unref(cur_track);
            return FALSE;
        }
    } else {
        /* Find filename */
        gchar *filename = mirage_helper_find_data_file(filename_string, _priv->toc_filename);
        if (!filename) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to find data file!\n", __func__);
            g_object_unref(cur_track);
            mirage_error(MIRAGE_E_DATAFILE, error);
            return FALSE;
        }
        
        if (type == TOC_DATA_TYPE_DATA) {
            /* Binary data; we'd like a BINARY fragment */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating BINARY fragment\n", __func__);
            mirage_mirage_create_fragment(MIRAGE_MIRAGE(mirage), MIRAGE_TYPE_FINTERFACE_BINARY, filename, &data_fragment, error);
            g_object_unref(mirage);
            if (!data_fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create BINARY fragment!\n", __func__);
                g_object_unref(cur_track);
                return FALSE;
            }
            
            FILE *tfile_handle = NULL;
            gint tfile_sectsize = 0;
            gint tfile_format = 0;
            guint64 tfile_offset = 0;
            
            gint sfile_format = 0;
            gint sfile_sectsize = 0;
            
            /* Track file */
            tfile_handle = g_fopen(filename, "r");
            tfile_sectsize = _priv->cur_tfile_sectsize;
        
            if (type == TOC_DATA_TYPE_AUDIO) {
                tfile_format = FR_BIN_TFILE_AUDIO;
            } else {
                tfile_format = FR_BIN_TFILE_DATA;
            }
            
            tfile_offset = base_offset + start * (_priv->cur_tfile_sectsize + _priv->cur_sfile_sectsize);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: calculated track file offset: 0x%llX\n", __func__, tfile_offset);
  
            /* Subchannel file */
            sfile_sectsize = _priv->cur_sfile_sectsize;
            sfile_format = _priv->cur_sfile_format;
            
            mirage_finterface_binary_track_file_set_handle(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_handle, NULL);
            mirage_finterface_binary_track_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_sectsize, NULL);
            mirage_finterface_binary_track_file_set_offset(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_offset, NULL);
            mirage_finterface_binary_track_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_format, NULL);

            mirage_finterface_binary_subchannel_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), sfile_sectsize, NULL);
            mirage_finterface_binary_subchannel_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), sfile_format, NULL);
        } else {
            /* Audio data; we'd like an AUDIO fragment, and hopefully Mirage can
               find one that can handle given file format */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating AUDIO fragment\n", __func__);
            mirage_mirage_create_fragment(MIRAGE_MIRAGE(mirage), MIRAGE_TYPE_FINTERFACE_AUDIO, filename, &data_fragment, error);
            g_object_unref(mirage);
            if (!data_fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create appropriate AUDIO fragment!\n", __func__);
                g_object_unref(cur_track);
                return FALSE;
            }
            
            /* Set file */
            if (!mirage_finterface_audio_set_file(MIRAGE_FINTERFACE_AUDIO(data_fragment), filename, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set file to AUDIO fragment!\n", __func__);
                g_object_unref(data_fragment);
                g_object_unref(cur_track);
                return FALSE;
            }
            
            /* Set offset */
            mirage_finterface_audio_set_offset(MIRAGE_FINTERFACE_AUDIO(data_fragment), start, NULL);
        }
        
        g_free(filename);
    }
    
    /* Set length */
    if (length) {
        /* Use supplied length */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting fragment's length: 0x%X\n", __func__, length);
        mirage_fragment_set_length(MIRAGE_FRAGMENT(data_fragment), length, NULL);
    } else {
        /* Use whole file */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: using whole file\n", __func__);
        if (!mirage_fragment_use_the_rest_of_file(MIRAGE_FRAGMENT(data_fragment), error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to use whole file!\n", __func__);
            g_object_unref(data_fragment);
            g_object_unref(cur_track);
            return FALSE;
        }
    }
    
    /* Add fragment */
    mirage_track_add_fragment(MIRAGE_TRACK(cur_track), -1, &data_fragment, NULL);
    g_object_unref(data_fragment);
    
    g_object_unref(cur_track); /* Unref current track */

    return TRUE;
};

gboolean __mirage_session_toc_set_track_start (MIRAGE_Session *self, gint start, GError **error) {
    GObject *cur_track = NULL;
    
    if (!mirage_session_get_track_by_index(self, -1, &cur_track, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current track!\n", __func__);
        return FALSE;
    }
    
    /* If start is not given (-1), we use current track length */
    if (start == -1) {
        mirage_track_layout_get_length(MIRAGE_TRACK(cur_track), &start, NULL);
    }
    
    mirage_track_set_track_start(MIRAGE_TRACK(cur_track), start, NULL);
    
    g_object_unref(cur_track); /* Unref current track */
    
    return TRUE;
}

gboolean __mirage_session_toc_add_index (MIRAGE_Session *self, gint address, GError **error) {
    GObject *cur_track = NULL;
    gint track_start = 0;
    
    if (!mirage_session_get_track_by_index(self, -1, &cur_track, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current track!\n", __func__);
        return FALSE;
    }
    
    /* Indices in TOC file are track-start relative... */
    mirage_track_get_track_start(MIRAGE_TRACK(cur_track), &track_start, NULL);
    mirage_track_add_index(MIRAGE_TRACK(cur_track), track_start + address, NULL, NULL);
    
    g_object_unref(cur_track); /* Unref current track */
    
    return TRUE;
}

gboolean __mirage_session_toc_set_flag (MIRAGE_Session *self, gint flag, gboolean set, GError **error) {
    GObject *cur_track = NULL;
    
    if (!mirage_session_get_track_by_index(self, -1, &cur_track, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current track!\n", __func__);
        return FALSE;
    }
    
    gint flags = 0;
    mirage_track_get_flags(MIRAGE_TRACK(cur_track), &flags, NULL);
    if (set) {
        /* Set flag */
        flags |= flag;
    } else {
        /* Clear flag */
        flags &= ~flag;
    }
    mirage_track_set_flags(MIRAGE_TRACK(cur_track), flags, NULL);
    
    g_object_unref(cur_track); /* Unref current track */

    return TRUE;
}

gboolean __mirage_session_toc_set_isrc (MIRAGE_Session *self, gchar *isrc, GError **error) {
    GObject *cur_track = NULL;
       
    if (!mirage_session_get_track_by_index(self, -1, &cur_track, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current track!\n", __func__);
        return FALSE;
    }

    mirage_track_set_isrc(MIRAGE_TRACK(cur_track), isrc, NULL);
    
    g_object_unref(cur_track); /* Unref current track */

    return TRUE;
}

gboolean __mirage_session_toc_add_language_mapping (MIRAGE_Session *self, gint index, gint langcode, GError **error) {
    MIRAGE_Session_TOC *self_toc = MIRAGE_SESSION_TOC(self);
    MIRAGE_Session_TOCPrivate *_priv = MIRAGE_SESSION_TOC_GET_PRIVATE(self_toc);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding language map: index %i -> langcode %i\n", __func__, index, langcode);
    
    g_hash_table_insert(_priv->lang_map, GINT_TO_POINTER(index), GINT_TO_POINTER(langcode));
        
    return TRUE;
}

gboolean __mirage_session_toc_add_g_laguage (MIRAGE_Session *self, gint index, GError **error) {
    MIRAGE_Session_TOC *self_toc = MIRAGE_SESSION_TOC(self);
    MIRAGE_Session_TOCPrivate *_priv = MIRAGE_SESSION_TOC_GET_PRIVATE(self_toc);
    
    gint langcode = GPOINTER_TO_INT(g_hash_table_lookup(_priv->lang_map, GINT_TO_POINTER(index)));
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding global language: index %i -> langcode %i\n", __func__, index, langcode);
    
    mirage_session_add_language(self, langcode, NULL, NULL);
    _priv->cur_langcode = langcode;
    
    return TRUE;
}

gboolean __mirage_session_toc_add_t_laguage (MIRAGE_Session *self, gint index, GError **error) {
    MIRAGE_Session_TOC *self_toc = MIRAGE_SESSION_TOC(self);
    MIRAGE_Session_TOCPrivate *_priv = MIRAGE_SESSION_TOC_GET_PRIVATE(self_toc);
    
    gint langcode = GPOINTER_TO_INT(g_hash_table_lookup(_priv->lang_map, GINT_TO_POINTER(index)));
    
    GObject *track = NULL;
    if (!mirage_session_get_track_by_index(self, -1, &track, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current track!\n", __func__);
        return FALSE;
    }
   
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding track language: index %i -> langcode %i\n", __func__, index, langcode);
    mirage_track_add_language(MIRAGE_TRACK(track), langcode, NULL, NULL);
    _priv->cur_langcode = langcode;

    g_object_unref(track);
    
    return TRUE;
}

gboolean __mirage_session_toc_set_g_cdtext_data (MIRAGE_Session *self, gint pack_type, gchar *data, GError **error) {
    MIRAGE_Session_TOC *self_toc = MIRAGE_SESSION_TOC(self);
    MIRAGE_Session_TOCPrivate *_priv = MIRAGE_SESSION_TOC_GET_PRIVATE(self_toc);
    
    GObject *language = NULL;
    
    if (mirage_session_get_language_by_code(self, _priv->cur_langcode, &language, NULL)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: pack type: 0x%X, data: <%s>\n", __func__, pack_type, data);
        mirage_language_set_pack_data(MIRAGE_LANGUAGE(language), pack_type, data, strlen(data)+1, NULL);
        g_object_unref(language);
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get language object!\n", __func__);
    }
        
    return TRUE;
}

gboolean __mirage_session_toc_set_t_cdtext_data (MIRAGE_Session *self, gint pack_type, gchar *data, GError **error) {
    MIRAGE_Session_TOC *self_toc = MIRAGE_SESSION_TOC(self);
    MIRAGE_Session_TOCPrivate *_priv = MIRAGE_SESSION_TOC_GET_PRIVATE(self_toc);
    
    GObject *track = NULL;
    GObject *language = NULL;
            
    if (!mirage_session_get_track_by_index(self, -1, &track, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current track!\n", __func__);
        return FALSE;
    }
    
    if (mirage_track_get_language_by_code(MIRAGE_TRACK(track), _priv->cur_langcode, &language, NULL)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: pack type: 0x%X, data: <%s>\n", __func__, pack_type, data);
        mirage_language_set_pack_data(MIRAGE_LANGUAGE(language), pack_type, data, strlen(data)+1, NULL);
        g_object_unref(language);
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get language object!\n", __func__);
    }
    
    g_object_unref(track);
    return TRUE;
}

/******************************************************************************\
 *                                Object init                                 *
\******************************************************************************/
/* Our parent class */
static MIRAGE_SessionClass *parent_class = NULL;

static void __mirage_session_toc_instance_init (GTypeInstance *instance, gpointer g_class) {
    MIRAGE_Session *self = MIRAGE_SESSION(instance);
    MIRAGE_Session_TOC *self_toc = MIRAGE_SESSION_TOC(self);                                                
    MIRAGE_Session_TOCPrivate *_priv = MIRAGE_SESSION_TOC_GET_PRIVATE(self_toc);
    
    /* Create language map hash table */
    _priv->lang_map = g_hash_table_new(g_direct_hash, g_direct_equal);
    
    return;
}

static void __mirage_session_toc_finalize (GObject *obj) {
    MIRAGE_Session_TOC *self_toc = MIRAGE_SESSION_TOC(obj);                                                
    MIRAGE_Session_TOCPrivate *_priv = MIRAGE_SESSION_TOC_GET_PRIVATE(self_toc);
    
    MIRAGE_DEBUG(self_toc, MIRAGE_DEBUG_GOBJECT, "%s:\n", __func__);

    g_hash_table_destroy(_priv->lang_map);
    g_free(_priv->toc_filename);
    
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self_toc, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __func__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __mirage_session_toc_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_Session_TOCClass *klass = MIRAGE_SESSION_TOC_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Session_TOCPrivate));
        
    /* Initialize GObject members */
    class_gobject->finalize = __mirage_session_toc_finalize;
    
    return;
}

GType mirage_session_toc_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Session_TOCClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_session_toc_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Session_TOC),
            0,      /* n_preallocs */
            __mirage_session_toc_instance_init    /* instance_init */
        };
        
        type = g_type_module_register_type(module, MIRAGE_TYPE_SESSION, "MIRAGE_Session_TOC", &info, 0);
    }
    
    return type;
}
