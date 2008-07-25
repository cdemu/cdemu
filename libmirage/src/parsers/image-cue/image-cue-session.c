/*
 *  libMirage: CUE image parser: Session object
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

#include "image-cue.h"


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_SESSION_CUE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_SESSION_CUE, MIRAGE_Session_CUEPrivate))

typedef struct {    
    gchar *cue_filename;

    gchar *cur_data_filename; /* Data file filename */
    gchar *cur_data_type;     /* Data type; determines which fragment type to use */
    gint cur_data_sectsize;   /* Sector size in case BINARY fragment is used */
    gint cur_data_format;     /* Format (AUDIO vs DATA) in case BINARY fragment is used */
    gint prev_track_start;    /* Used to determine fragment lengths */
    gint cur_track_start;     /* Used to determine pregap */
    
    gboolean cur_pregap_set;
} MIRAGE_Session_CUEPrivate;


/******************************************************************************\
 *                       Session private functions                            *
\******************************************************************************/
gboolean __mirage_session_cue_finish_last_track (MIRAGE_Session *self, GError **error) {
    GObject *ltrack = NULL;
    
    /* Get last track in the whole session */
    if (mirage_session_get_track_by_index(self, -1, &ltrack, NULL)) {
        /* Get last fragment and set its length... (actually, since there
           can be postgap fragment stuck at the end, we look for first data
           fragment that's not NULL... and of course, we go from behind) */
        /* FIXME: implement the latter part */
        GObject *data_fragment = NULL;
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finishing last track\n", __func__);
        if (mirage_track_get_fragment_by_index(MIRAGE_TRACK(ltrack), -1, &data_fragment, NULL)) {
            mirage_fragment_use_the_rest_of_file(MIRAGE_FRAGMENT(data_fragment), NULL);
            g_object_unref(data_fragment);
        }
                
        g_object_unref(ltrack); /* Unref last track */
    }

    return TRUE;
}


gboolean __mirage_session_cue_set_cue_filename (MIRAGE_Session *self, gchar *filename, GError **error) {
    MIRAGE_Session_CUEPrivate *_priv = MIRAGE_SESSION_CUE_GET_PRIVATE(self);
    
    _priv->cue_filename = g_strdup(filename);
    
    return TRUE;
}

gboolean __mirage_session_cue_set_mcn (MIRAGE_Session *self, gchar *mcn, GError **error) {
    GObject *disc = NULL;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: MCN: %.13s\n", __func__, mcn);
    if (mirage_object_get_parent(MIRAGE_OBJECT(self), &disc, NULL)) {
        mirage_disc_set_mcn(MIRAGE_DISC(disc), mcn, NULL);
        g_object_unref(disc);
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get parent disc!\n", __func__);
    }
    
    return TRUE;
}

gboolean __mirage_session_cue_set_new_file (MIRAGE_Session *self, gchar *filename_string, gchar *file_type, GError **error) {
    MIRAGE_Session_CUEPrivate *_priv = MIRAGE_SESSION_CUE_GET_PRIVATE(self);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: new file...\n", __func__);
    
    /* If we got file, we got it either for first time, or it has changed... if the
       latter, it means we already have some tracks and the last one's missing its
       length... and it should be determined from the file size */
    if (!__mirage_session_cue_finish_last_track(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to finish last track!\n", __func__);
        return FALSE;
    }
    
    /* Set current file name */
    g_free(_priv->cur_data_filename);
    _priv->cur_data_filename = mirage_helper_find_data_file(filename_string, _priv->cue_filename);
    if (!_priv->cur_data_filename) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to find data file!\n", __func__);
        mirage_error(MIRAGE_E_DATAFILE, error);
        return FALSE;
    }
    
    /* Set current data type */
    g_free(_priv->cur_data_type);
    _priv->cur_data_type = g_strdup(file_type);
    _priv->cur_track_start = 0;
    
    return TRUE;
}

gboolean __mirage_session_cue_add_track (MIRAGE_Session *self, gint number, gchar *mode_string, GError **error) {
    MIRAGE_Session_CUEPrivate *_priv = MIRAGE_SESSION_CUE_GET_PRIVATE(self);
    GObject *cur_track = NULL;
    
    if (!mirage_session_add_track_by_number(self, number, &cur_track, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __func__);
        return FALSE;
    }
    
    /* Decipher mode */
    struct {
        gchar *str;
        gint mode;
        gint sectsize;
        gint format;
    } track_modes[] = {
        {"AUDIO",      MIRAGE_MODE_AUDIO, 2352, FR_BIN_TFILE_AUDIO},
        {"CDG",        MIRAGE_MODE_AUDIO, 2448, FR_BIN_TFILE_AUDIO},
        {"MODE1/2048", MIRAGE_MODE_MODE1, 2048, FR_BIN_TFILE_DATA},
        {"MODE1/2352", MIRAGE_MODE_MODE1, 2352, FR_BIN_TFILE_DATA},
        /* Not sure about the following ones, but MIXED should take care of them */
        {"MODE2/2336", MIRAGE_MODE_MODE2_MIXED, 2336, FR_BIN_TFILE_DATA},
        {"MODE2/2352", MIRAGE_MODE_MODE2_MIXED, 2352, FR_BIN_TFILE_DATA},
        {"CDI/2336",   MIRAGE_MODE_MODE2_MIXED, 2336, FR_BIN_TFILE_DATA},
        {"CDI/2352",   MIRAGE_MODE_MODE2_MIXED, 2352, FR_BIN_TFILE_DATA},
    };
    gint i;
    for (i = 0; i < G_N_ELEMENTS(track_modes); i++) {
        if (!strcmp(track_modes[i].str, mode_string)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track mode: %s\n", __func__, track_modes[i].str);
            /* Set track mode */
            mirage_track_set_mode(MIRAGE_TRACK(cur_track), track_modes[i].mode, NULL);
            /* Set current sector size and format */
            _priv->cur_data_sectsize = track_modes[i].sectsize;
            _priv->cur_data_format = track_modes[i].format;
            break;
        }
    }
        
    g_object_unref(cur_track); /* Unref current track */
    
    /* Reset parser info on current track */
    _priv->cur_pregap_set = FALSE;
    
    return TRUE;
};

gboolean __mirage_session_cue_add_index (MIRAGE_Session *self, gint number, gint address, GError **error) {
    MIRAGE_Session_CUEPrivate *_priv = MIRAGE_SESSION_CUE_GET_PRIVATE(self);
    
    GObject *cur_track = NULL;
    
    if (!mirage_session_get_track_by_index(self, -1, &cur_track, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current track!\n", __func__);
        return FALSE;
    }
    
    /* Both indexes 0 and 1 can mean we have start of a track... if there's 0,
       we have a pregap, if there's just 1, we don't have pregap */
    if (number == 0 || number == 1) {
        /* If index is 0, mark that we have a pregap */
        if (number == 0) {
            _priv->cur_pregap_set = TRUE;
        }
        
        if (number == 1 && _priv->cur_pregap_set) {
            /* If we have a pregap and this is index 1, we just need to 
               set the address where the track really starts */
            gint track_start = 0;
            mirage_track_get_track_start(MIRAGE_TRACK(cur_track), &track_start, NULL);
            track_start += address - _priv->cur_track_start;
            mirage_track_set_track_start(MIRAGE_TRACK(cur_track), track_start, NULL);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track with pregap; setting track start to 0x%X\n", __func__, track_start);
        } else {
            /* Otherwise, we need to set up fragment... beginning with the 
               last fragment of the previous track (which we might need to
               set length to) */
            GObject *prev_track = NULL;
            
            if (mirage_track_get_prev(MIRAGE_TRACK(cur_track), &prev_track, NULL)) {
                GObject *lfragment = NULL;
                
                if (mirage_track_get_fragment_by_index(MIRAGE_TRACK(prev_track), -1, &lfragment, NULL)) {
                    gint fragment_length = 0;
                    
                    /* Get fragment's length; if it's not set, we need to calculate
                       it now... if file has changed in between or anything else
                       happened that might've resulted in call of __mirage_session_cue_finish_last_track, 
                       then fragment will have its length already set */
                    mirage_fragment_get_length(MIRAGE_FRAGMENT(lfragment), &fragment_length, NULL);
                    if (!fragment_length) {
                        fragment_length = address - _priv->cur_track_start;
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: previous fragment length determined to be: %i\n", __func__, fragment_length);
                        mirage_fragment_set_length(MIRAGE_FRAGMENT(lfragment), fragment_length, NULL);
                    } else {
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: previous fragment already has length (%i)\n", __func__, fragment_length);
                    }
                    
                    g_object_unref(lfragment);
                }
                
                g_object_unref(prev_track);
            } else {
                /* If this is the first track, index is 1 and its address isn't
                   zero, we also have a pregap (first track doesn't seem to have
                   index 0) */
                if (number == 1 && address != 0) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: first track has pregap; setting track start to 0x%X\n", __func__, address);
                    mirage_track_set_track_start(MIRAGE_TRACK(cur_track), address, NULL);
                    /* address is used later to determine the offset within track file;
                       in this case, we need to reset it to 0, as pregap seems to be
                       included in the track file */
                    address = 0;
                }
            }
            
            /* Now current track; we only create fragment here and set its offset */
            GObject *mirage = NULL;
            if (!mirage_object_get_mirage(MIRAGE_OBJECT(self), &mirage, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get Mirage object!\n", __func__);
                g_object_unref(cur_track);
                return FALSE;
            }
            
            GObject *data_fragment = NULL;
            if (!strcmp(_priv->cur_data_type, "BINARY")) {
                /* Binary data; we'll request fragment with BINARY interface... */
                gint offset = address*_priv->cur_data_sectsize;
                
                gint tfile_sectsize = 0;
                gint sfile_sectsize = 0;
                
                /* Take into account possibility of having subchannel
                   (only for CD+G tracks, though) */
                if (_priv->cur_data_sectsize == 2448) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: subchannel data present...\n", __func__);
                    tfile_sectsize = 2352;
                    sfile_sectsize = 96;
                } else {
                    tfile_sectsize = _priv->cur_data_sectsize;
                }
                
                mirage_mirage_create_fragment(MIRAGE_MIRAGE(mirage), MIRAGE_TYPE_FINTERFACE_BINARY, _priv->cur_data_filename, &data_fragment, error);
                g_object_unref(mirage);
                if (!data_fragment) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create data fragment!\n", __func__);
                    g_object_unref(cur_track);
                    return FALSE;
                }
                
                mirage_finterface_binary_track_file_set_handle(MIRAGE_FINTERFACE_BINARY(data_fragment), g_fopen(_priv->cur_data_filename, "r"), NULL);
                mirage_finterface_binary_track_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_sectsize, NULL);
                mirage_finterface_binary_track_file_set_offset(MIRAGE_FINTERFACE_BINARY(data_fragment), offset, NULL);
                mirage_finterface_binary_track_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), _priv->cur_data_format, NULL);
                    
                if (sfile_sectsize) {
                    mirage_finterface_binary_subchannel_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), sfile_sectsize, NULL);
                    /* FIXME: what format of subchannel is there anyway? */
                    mirage_finterface_binary_subchannel_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), FR_BIN_SFILE_PW96_INT | FR_BIN_SFILE_INT, NULL);                        
                }
            } else {
                /* One of the audio files; we'll request fragment with AUDIO
                   interface and hope Mirage finds one that can handle the file
                   for us */
                mirage_mirage_create_fragment(MIRAGE_MIRAGE(mirage), MIRAGE_TYPE_FINTERFACE_AUDIO, _priv->cur_data_filename, &data_fragment, error);
                g_object_unref(mirage);
                if (!data_fragment) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown/unsupported file type: %s\n", __func__, _priv->cur_data_type);
                    g_object_unref(cur_track);
                    return FALSE;
                }
                
                mirage_finterface_audio_set_file(MIRAGE_FINTERFACE_AUDIO(data_fragment), _priv->cur_data_filename, NULL);
                /* Offset in audio file is equivalent to current address in CUE */
                mirage_finterface_audio_set_offset(MIRAGE_FINTERFACE_AUDIO(data_fragment), address, NULL);
            }
            
            mirage_track_add_fragment(MIRAGE_TRACK(cur_track), -1, &data_fragment, NULL);
            
            /* Store the current address... it is location at which the current 
               track starts, and it will be used to calculate fragment's length 
               (if file won't change) */
            _priv->cur_track_start = address;
            
            g_object_unref(data_fragment);
        }
    } else {
        /* If index >= 2 is given, add it */
        mirage_track_add_index(MIRAGE_TRACK(cur_track), address, NULL, NULL);
    }
    
    g_object_unref(cur_track);
    
    return TRUE;
}

gboolean __mirage_session_cue_set_flag (MIRAGE_Session *self, gint flag, GError **error) {
    GObject *cur_track = NULL;
    
    if (!mirage_session_get_track_by_index(self, -1, &cur_track, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current track!\n", __func__);
        return FALSE;
    }
    
    gint flags = 0;
    
    mirage_track_get_flags(MIRAGE_TRACK(cur_track), &flags, NULL);
    flags |= flag;
    mirage_track_set_flags(MIRAGE_TRACK(cur_track), flags, NULL);
    
    g_object_unref(cur_track); /* Unref current track */
    
    return TRUE;
}

gboolean __mirage_session_cue_set_isrc (MIRAGE_Session *self, gchar *isrc, GError **error) {
    GObject *cur_track = NULL;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: ISRC: %.12s\n", __func__, isrc);
    if (!mirage_session_get_track_by_index(self, -1, &cur_track, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current track!\n", __func__);
        return FALSE;
    }
    
    mirage_track_set_isrc(MIRAGE_TRACK(cur_track), isrc, NULL);
    
    g_object_unref(cur_track); /* Unref current track */
    
    return TRUE;
}

gboolean __mirage_session_cue_add_pregap (MIRAGE_Session *self, gint length, GError **error) {
    GObject *cur_track = NULL;
    gint track_start = 0;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding pregap (0x%X)\n", __func__, length);
    
    /* Add empty part */
    if (!__mirage_session_cue_add_empty_part(self, length, error)) {
        return FALSE;
    }
    
    /* Adjust track start */
    if (!mirage_session_get_track_by_index(self, -1, &cur_track, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current track!\n", __func__);
        return FALSE;
    }
    
    mirage_track_get_track_start(MIRAGE_TRACK(cur_track), &track_start, NULL);
    track_start += length;
    mirage_track_set_track_start(MIRAGE_TRACK(cur_track), track_start, NULL);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: readjusted track start to 0x%X (%i)\n", __func__, track_start, track_start);
    
    g_object_unref(cur_track); /* Unref current track */
    
    return TRUE;
}

gboolean __mirage_session_cue_add_empty_part (MIRAGE_Session *self, gint length, GError **error) {
    GObject *cur_track = NULL;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding empty part (0x%X)\n", __func__, length);

    if (!mirage_session_get_track_by_index(self, -1, &cur_track, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current track!\n", __func__);
        return FALSE;
    }
    
    /* Prepare NULL fragment */
    GObject *mirage = NULL;
    GObject *data_fragment = NULL;
    
    if (!mirage_object_get_mirage(MIRAGE_OBJECT(self), &mirage, NULL)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get Mirage object!\n", __func__);
        g_object_unref(cur_track);
        return FALSE;
    }
        
    mirage_mirage_create_fragment(MIRAGE_MIRAGE(mirage), MIRAGE_TYPE_FINTERFACE_NULL, "NULL", &data_fragment, error);
    g_object_unref(mirage);
    if (!data_fragment) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create NULL fragment!\n", __func__);
        g_object_unref(cur_track);
        return FALSE;
    }
    
    mirage_fragment_set_length(MIRAGE_FRAGMENT(data_fragment), length, NULL);
    
    /* Add fragment */
    mirage_track_add_fragment(MIRAGE_TRACK(cur_track), -1, &data_fragment, NULL);
    g_object_unref(data_fragment);
    
    g_object_unref(cur_track); /* Unref current track */
    
    return TRUE;
}


/******************************************************************************\
 *                                Object init                                 *
\******************************************************************************/
/* Our parent class */
static MIRAGE_SessionClass *parent_class = NULL;

static void __mirage_session_cue_finalize (GObject *obj) {
    MIRAGE_Session_CUE *self = MIRAGE_SESSION_CUE(obj);
    MIRAGE_Session_CUEPrivate *_priv = MIRAGE_SESSION_CUE_GET_PRIVATE(self);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s:\n", __func__);

    /* Free elements of private structure */
    g_free(_priv->cue_filename);
    g_free(_priv->cur_data_filename);
    g_free(_priv->cur_data_type);
            
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __func__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __mirage_session_cue_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_Session_CUEClass *klass = MIRAGE_SESSION_CUE_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Session_CUEPrivate));
    
    /* Initialize GObject functions */
    class_gobject->finalize = __mirage_session_cue_finalize;
    
    return;
}

GType mirage_session_cue_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Session_CUEClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_session_cue_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Session_CUE),
            0,      /* n_preallocs */
            NULL    /* instance_init */
        };
        
        type = g_type_module_register_type(module, MIRAGE_TYPE_SESSION, "MIRAGE_Session_CUE", &info, 0);
    }
    
    return type;
}
