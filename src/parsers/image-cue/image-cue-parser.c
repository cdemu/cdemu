/*
 *  libMirage: CUE image parser: Parser object
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

/* Regex engine */
typedef gboolean (*MIRAGE_RegexCallback) (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error);

typedef struct {
    GRegex *regex;
    MIRAGE_RegexCallback callback_func;
} MIRAGE_RegexRule;


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_PARSER_CUE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_CUE, MIRAGE_Parser_CUEPrivate))

typedef struct {
    GObject *disc;
    
    gchar *cue_filename;

    gchar *cur_data_filename; /* Data file filename */
    gchar *cur_data_type; /* Data type; determines which fragment type to use */
    gint cur_data_sectsize; /* Sector size in case BINARY fragment is used */
    gint cur_data_format; /* Format (AUDIO vs DATA) in case BINARY fragment is used */
    gint cur_track_start; /* Used to determine pregap */
    
    gint binary_offset; /* Offset within the binary file */
    
    gboolean cur_pregap_set;
    
    gint leadout_correction;
    
    /* Pointers to current session and current track object, so that we don't
       have to retrieve them all the time; note that no reference is not kept 
       for them */
    GObject *cur_session;
    GObject *cur_track;
    GObject *prev_track;
        
    /* Regex engine */
    GList *regex_rules;
} MIRAGE_Parser_CUEPrivate;


/******************************************************************************\
 *                         Parser private functions                           *
\******************************************************************************/
static gboolean __mirage_parser_cue_finish_last_track (MIRAGE_Parser *self, GError **error) {
    MIRAGE_Parser_CUEPrivate *_priv = MIRAGE_PARSER_CUE_GET_PRIVATE(self);
    GObject *data_fragment;
    gboolean succeeded = TRUE;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finishing last track\n", __func__);

    /* Current track needs to be set at this point */
    if (!_priv->cur_track) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: current track is not set!\n", __func__);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }
    
    /* Get last fragment and set its length... (actually, since there can be
       postgap fragment stuck at the end, we look for first data fragment that's 
       not NULL... and of course, we go from behind) */
    /* FIXME: implement the latter part */
    if (mirage_track_get_fragment_by_index(MIRAGE_TRACK(_priv->cur_track), -1, &data_fragment, NULL)) {
        gint fragment_length;
        
        mirage_fragment_use_the_rest_of_file(MIRAGE_FRAGMENT(data_fragment), NULL);
        mirage_fragment_get_length(MIRAGE_FRAGMENT(data_fragment), &fragment_length, NULL);
        
        if (fragment_length < 0) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: finishing last track resulted in negative fragment length!\n", __func__);
            mirage_error(MIRAGE_E_PARSER, error);
            succeeded = FALSE;
        }
        
        
        g_object_unref(data_fragment);
    }

    return succeeded;
}

static gboolean __mirage_parser_cue_set_new_file (MIRAGE_Parser *self, gchar *filename_string, gchar *file_type, GError **error) {
    MIRAGE_Parser_CUEPrivate *_priv = MIRAGE_PARSER_CUE_GET_PRIVATE(self);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: new file...\n", __func__);
    
    /* We got new file; either we got it for the first time, which means we don't
       have any tracks yet and don't have to do anything. If we got new file, it
       means means we already have some tracks and the last one needs to be
       finished */
    if (_priv->cur_track && !__mirage_parser_cue_finish_last_track(self, error)) {
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
    _priv->binary_offset = 0;
    
    return TRUE;
}

static gboolean __mirage_parser_cue_add_track (MIRAGE_Parser *self, gint number, gchar *mode_string, GError **error) {
    MIRAGE_Parser_CUEPrivate *_priv = MIRAGE_PARSER_CUE_GET_PRIVATE(self);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding track %d\n", __func__, number);
    
    /* Current track becomes previous one */
    _priv->prev_track = _priv->cur_track;
    
    /* Add new track, store the pointer and release reference */
    _priv->cur_track = NULL; /* Need to reset it, otherwise it gets added */
    if (!mirage_session_add_track_by_number(MIRAGE_SESSION(_priv->cur_session), number, &_priv->cur_track, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __func__);
        return FALSE;
    }
    g_object_unref(_priv->cur_track);

    /* Decipher mode */
    static const struct {
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
            mirage_track_set_mode(MIRAGE_TRACK(_priv->cur_track), track_modes[i].mode, NULL);
            /* Set current sector size and format */
            _priv->cur_data_sectsize = track_modes[i].sectsize;
            _priv->cur_data_format = track_modes[i].format;
            break;
        }
    }
    
    if (i == G_N_ELEMENTS(track_modes)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: invalid track mode string: %s!\n", __func__, mode_string);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }
    
    /* Reset parser info on current track */
    _priv->cur_pregap_set = FALSE;
    
    return TRUE;
};

static gboolean __mirage_parser_cue_add_index (MIRAGE_Parser *self, gint number, gint address, GError **error) {
    MIRAGE_Parser_CUEPrivate *_priv = MIRAGE_PARSER_CUE_GET_PRIVATE(self);    
    
    /* Current track needs to be set at this point */
    if (!_priv->cur_track) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: current track is not set!\n", __func__);
        mirage_error(MIRAGE_E_PARSER, error);
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
            mirage_track_get_track_start(MIRAGE_TRACK(_priv->cur_track), &track_start, NULL);
            track_start += address - _priv->cur_track_start;
            mirage_track_set_track_start(MIRAGE_TRACK(_priv->cur_track), track_start, NULL);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track with pregap; setting track start to 0x%X\n", __func__, track_start);
        } else {
            /* Otherwise, we need to set up fragment... beginning with the 
               last fragment of the previous track (which we might need to
               set length to) */            
            if (!_priv->prev_track) {
                /* This is first track on the parser; first track doesn't seem to
                   have index 0, so if its index 1 is non-zero, it indicates pregap */
                if (number == 1 && address != 0) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: first track has pregap; setting track start to 0x%X\n", __func__, address);
                    mirage_track_set_track_start(MIRAGE_TRACK(_priv->cur_track), address, NULL);
                    /* address is used later to determine the offset within track file;
                       in this case, we need to reset it to 0, as pregap seems to be
                       included in the track file */
                    address = 0;
                }
            } else {
                GObject *lfragment = NULL;
                
                /* Get previous track's fragment to set its length */
                if (mirage_track_get_fragment_by_index(MIRAGE_TRACK(_priv->prev_track), -1, &lfragment, NULL)) {
                    gint fragment_length = 0;
                    
                    /* If length is not set, we need to calculate it now; the 
                       length will be already set if file has changed in between 
                       or anything else happened that might've resulted in call 
                       of __mirage_session_cue_finish_last_track() */
                    mirage_fragment_get_length(MIRAGE_FRAGMENT(lfragment), &fragment_length, NULL);
                    if (!fragment_length) {
                        fragment_length = address - _priv->cur_track_start;
                        
                        /* In case we're dealing with UltraISO/IsoBuster's 
                           multisession, we need this because index addresses 
                           differences includes the leadout length */
                        if (_priv->leadout_correction) {
                            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: using leadout correction %d\n", __func__, _priv->leadout_correction);
                            fragment_length -= _priv->leadout_correction;
                            _priv->leadout_correction = 0;
                        }
                        
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: previous fragment length determined to be: %i\n", __func__, fragment_length);
                        mirage_fragment_set_length(MIRAGE_FRAGMENT(lfragment), fragment_length, NULL);
                    } else {
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: previous fragment already has length (%i)\n", __func__, fragment_length);
                    }
                    
                    /* Binary fragments/files are pain because sector size can
                       vary between the tracks; so in case we're dealing with
                       binary, we need to keep track of the offset within file */
                    if (MIRAGE_IS_FINTERFACE_BINARY(lfragment)) {
                        gint tfile_sectsize, sfile_sectsize;
                        
                        mirage_finterface_binary_track_file_get_sectsize(MIRAGE_FINTERFACE_BINARY(lfragment), &tfile_sectsize, NULL);
                        mirage_finterface_binary_subchannel_file_get_sectsize(MIRAGE_FINTERFACE_BINARY(lfragment), &sfile_sectsize, NULL);
                        
                        _priv->binary_offset += fragment_length * (tfile_sectsize + sfile_sectsize);
                    }
                    
                    g_object_unref(lfragment);
                }
            }
            
            /* Now current track; we only create fragment here and set its offset */
            GObject *data_fragment = NULL;
            if (!strcmp(_priv->cur_data_type, "BINARY")) {
                /* Binary data; we'll request fragment with BINARY interface... */
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
                
                data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FINTERFACE_BINARY, _priv->cur_data_filename, error);
                if (!data_fragment) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create data fragment!\n", __func__);
                    return FALSE;
                }
                
                mirage_finterface_binary_track_file_set_handle(MIRAGE_FINTERFACE_BINARY(data_fragment), g_fopen(_priv->cur_data_filename, "r"), NULL);
                mirage_finterface_binary_track_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_sectsize, NULL);
                mirage_finterface_binary_track_file_set_offset(MIRAGE_FINTERFACE_BINARY(data_fragment), _priv->binary_offset, NULL);
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
                data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FINTERFACE_AUDIO, _priv->cur_data_filename, error);
                if (!data_fragment) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown/unsupported file type: %s\n", __func__, _priv->cur_data_type);
                    return FALSE;
                }
                
                mirage_finterface_audio_set_file(MIRAGE_FINTERFACE_AUDIO(data_fragment), _priv->cur_data_filename, NULL);
                /* Offset in audio file is equivalent to current address in CUE */
                mirage_finterface_audio_set_offset(MIRAGE_FINTERFACE_AUDIO(data_fragment), address, NULL);
            }
            
            mirage_track_add_fragment(MIRAGE_TRACK(_priv->cur_track), -1, &data_fragment, NULL);
            
            /* Store the current address... it is location at which the current 
               track starts, and it will be used to calculate fragment's length 
               (if file won't change) */
            _priv->cur_track_start = address;
            
            g_object_unref(data_fragment);
        }
    } else {
        /* If index >= 2 is given, add it */
        mirage_track_add_index(MIRAGE_TRACK(_priv->cur_track), address, NULL, NULL);
    }
        
    return TRUE;
}

static gboolean __mirage_parser_cue_set_flags (MIRAGE_Parser *self, gint flags, GError **error) {
    MIRAGE_Parser_CUEPrivate *_priv = MIRAGE_PARSER_CUE_GET_PRIVATE(self);    

    /* Current track needs to be set at this point */
    if (!_priv->cur_track) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: current track is not set!\n", __func__);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }
    
    mirage_track_set_flags(MIRAGE_TRACK(_priv->cur_track), flags, NULL);
    
    return TRUE;
}

static gboolean __mirage_parser_cue_set_isrc (MIRAGE_Parser *self, gchar *isrc, GError **error) {
    MIRAGE_Parser_CUEPrivate *_priv = MIRAGE_PARSER_CUE_GET_PRIVATE(self);    

    /* Current track needs to be set at this point */
    if (!_priv->cur_track) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current track!\n", __func__);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }
    
    mirage_track_set_isrc(MIRAGE_TRACK(_priv->cur_track), isrc, NULL);
        
    return TRUE;
}


static gboolean __mirage_parser_cue_add_empty_part (MIRAGE_Parser *self, gint length, GError **error) {
    MIRAGE_Parser_CUEPrivate *_priv = MIRAGE_PARSER_CUE_GET_PRIVATE(self);    
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding empty part (0x%X)\n", __func__, length);

    /* Current track needs to be set at this point */
    if (!_priv->cur_track) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: current track is not set!\n", __func__);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }
    
    /* Prepare NULL fragment */
    GObject *data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FINTERFACE_NULL, "NULL", error);
    if (!data_fragment) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create NULL fragment!\n", __func__);
        return FALSE;
    }
    
    mirage_fragment_set_length(MIRAGE_FRAGMENT(data_fragment), length, NULL);
    
    /* Add fragment */
    mirage_track_add_fragment(MIRAGE_TRACK(_priv->cur_track), -1, &data_fragment, NULL);
    g_object_unref(data_fragment);
        
    return TRUE;
}

static gboolean __mirage_parser_cue_add_pregap (MIRAGE_Parser *self, gint length, GError **error) {
    MIRAGE_Parser_CUEPrivate *_priv = MIRAGE_PARSER_CUE_GET_PRIVATE(self);    
    gint track_start = 0;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding pregap (0x%X)\n", __func__, length);
    
    /* Current track needs to be set at this point */
    if (!_priv->cur_track) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: current track is not set!\n", __func__);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }
    
    /* Add empty part */
    if (!__mirage_parser_cue_add_empty_part(self, length, error)) {
        return FALSE;
    }
    
    /* Adjust track start */
    mirage_track_get_track_start(MIRAGE_TRACK(_priv->cur_track), &track_start, NULL);
    track_start += length;
    mirage_track_set_track_start(MIRAGE_TRACK(_priv->cur_track), track_start, NULL);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: readjusted track start to 0x%X (%i)\n", __func__, track_start, track_start);
        
    return TRUE;
}

static gboolean __mirage_parser_cue_add_session (MIRAGE_Parser *self, gint number, GError **error) {
    MIRAGE_Parser_CUEPrivate *_priv = MIRAGE_PARSER_CUE_GET_PRIVATE(self);    
    gint leadout_length = 0;
    
    /* We've already added first session, so don't bother */
    if (number == 1) {
        return TRUE;
    }
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding new session\n", __func__);
    
    /* Set the lead-out length of current session */
    if (number == 2) {
        leadout_length = 11250; /* Actually, it should be 6750 previous leadout, 4500 current leadin */
    } else {
        leadout_length = 6750; /* Actually, it should be 2250 previous leadout, 4500 current leadin */
    }
    mirage_session_set_leadout_length(MIRAGE_SESSION(_priv->cur_session), leadout_length, NULL);
    
    /* UltraISO/IsoBuster store leadout data in the binary file. We'll need to
       account for this when we're setting fragment length, which we calculate
       from index addresses... (150 sectors are added to account for pregap, 
       which isn't indicated because only index 01 is used) */
    _priv->leadout_correction = leadout_length + 150;
    
    /* Add new session, store the pointer but release the reference */
    _priv->cur_session = NULL; /* Need to reset it, otherwise it gets added */
    if (!mirage_disc_add_session_by_index(MIRAGE_DISC(_priv->disc), -1, &_priv->cur_session, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __func__);
        return FALSE;
    }
    g_object_unref(_priv->cur_session);
    
    /* Reset current track */
    _priv->cur_track = NULL;
    
    return TRUE;
}

static gboolean __mirage_parser_cue_set_pack_data (MIRAGE_Parser *self, gint pack_type, gchar *data, GError **error) {
    MIRAGE_Parser_CUEPrivate *_priv = MIRAGE_PARSER_CUE_GET_PRIVATE(self);    
    GObject *language = NULL;
    
    /* FIXME: only one language code supported for now */
    gint langcode = 9; 
    
    if (!_priv->cur_track) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting pack data for disc; type: 0x%X, data: %s\n", __func__, pack_type, data);
        if (!mirage_session_get_language_by_code(MIRAGE_SESSION(_priv->cur_session), langcode, &language, NULL)) {
            mirage_session_add_language(MIRAGE_SESSION(_priv->cur_session), langcode, &language, NULL);
        }
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting pack data for track; type: 0x%X, data: %s\n", __func__, pack_type, data);        
        if (!mirage_track_get_language_by_code(MIRAGE_TRACK(_priv->cur_track), langcode, &language, NULL)) {
            mirage_track_add_language(MIRAGE_TRACK(_priv->cur_track), langcode, &language, NULL);
        }
    }
    
    mirage_language_set_pack_data(MIRAGE_LANGUAGE(language), pack_type, data, strlen(data)+1, NULL);
    g_object_unref(language);
    
    return TRUE;
}


/******************************************************************************\
 *                           Regex parsing engine                             *
\******************************************************************************/
static gchar *__strip_quotes (gchar *str) {
    gint len = strlen(str);
    
    /* Due to UTF-8 being multi-byte, we need to deal with string on byte level,
       not character level */
    
    /* Skip leading quote and trailing quote, but only if both are present */
    if (str[0] == '"' && str[len-1] == '"') {
        return g_strndup(str+1, len-2);
    }
    
    /* Otherwise copy the string, for consistency */
    return g_strdup(str);
}

static gboolean __mirage_parser_cue_callback_session (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gboolean succeeded = TRUE;
    gchar *number_raw = g_match_info_fetch_named(match_info, "number");
    gint number = g_strtod(number_raw, NULL);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed SESSION: %d\n", __func__, number);
    succeeded = __mirage_parser_cue_add_session(self, number, error);
    
    g_free(number_raw);
    
    return TRUE;
}

static gboolean __mirage_parser_cue_callback_comment (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gchar *comment = g_match_info_fetch_named(match_info, "comment");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed COMMENT: %s\n", __func__, comment);
    
    g_free(comment);
    
    return TRUE;
}

static gboolean __mirage_parser_cue_callback_cdtext (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gchar *filename_raw, *filename;
    
    filename_raw = g_match_info_fetch_named(match_info, "filename");
    filename = __strip_quotes(filename_raw);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed CDTEXT: %s; FIXME: not handled yet!\n", __func__, filename);
    
    g_free(filename);
    g_free(filename_raw);
    
    return TRUE;
}

static gboolean __mirage_parser_cue_callback_catalog (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {    
    MIRAGE_Parser_CUEPrivate *_priv = MIRAGE_PARSER_CUE_GET_PRIVATE(self);
    gchar *catalog = g_match_info_fetch_named(match_info, "catalog");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed CATALOG: %.13s\n", __func__, catalog);
    
    mirage_disc_set_mcn(MIRAGE_DISC(_priv->disc), catalog, NULL);
    
    g_free(catalog);
    
    return TRUE;
}

static gboolean __mirage_parser_cue_callback_title (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gboolean succeeded = TRUE;
    gchar *title_raw, *title;
    
    title_raw = g_match_info_fetch_named(match_info, "title");
    title = __strip_quotes(title_raw);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed TITLE: %s\n", __func__, title);
    
    succeeded = __mirage_parser_cue_set_pack_data(self, MIRAGE_LANGUAGE_PACK_TITLE, title, error);
    
    g_free(title);
    g_free(title_raw);
    
    return succeeded;
}

static gboolean __mirage_parser_cue_callback_performer (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gboolean succeeded = TRUE;
    gchar *performer_raw, *performer;
    
    performer_raw = g_match_info_fetch_named(match_info, "performer");
    performer = __strip_quotes(performer_raw);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed PERFORMER: %s\n", __func__, performer);
    
    succeeded = __mirage_parser_cue_set_pack_data(self, MIRAGE_LANGUAGE_PACK_PERFORMER, performer, error);

    g_free(performer);
    g_free(performer_raw);
    
    return succeeded;
}

static gboolean __mirage_parser_cue_callback_songwriter (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gboolean succeeded = TRUE;
    gchar *songwriter_raw, *songwriter;
    
    songwriter_raw = g_match_info_fetch_named(match_info, "songwriter");
    songwriter = __strip_quotes(songwriter_raw);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed SONGWRITER: %s\n", __func__, songwriter);
    
    succeeded = __mirage_parser_cue_set_pack_data(self, MIRAGE_LANGUAGE_PACK_SONGWRITER, songwriter, error);

    g_free(songwriter);
    g_free(songwriter_raw);

    return succeeded;
}

static gboolean __mirage_parser_cue_callback_file (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gboolean succeeded = TRUE;
    gchar *filename_raw, *filename, *type; 
    
    type = g_match_info_fetch_named(match_info, "type");
    filename_raw = g_match_info_fetch_named(match_info, "filename");
    filename = __strip_quotes(filename_raw);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n"); /* To make log more readable */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed FILE; filename: %s, type: %s\n", __func__, filename, type);
    
    succeeded = __mirage_parser_cue_set_new_file(self, filename, type, error);
    
    g_free(filename);
    g_free(filename_raw);
    g_free(type);
    
    return succeeded;
}

static gboolean __mirage_parser_cue_callback_track (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gboolean succeeded = TRUE;
    gchar *number_raw, *mode_string;
    gint number;
    
    number_raw = g_match_info_fetch_named(match_info, "number");
    number = g_strtod(number_raw, NULL);
    mode_string = g_match_info_fetch_named(match_info, "type");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n"); /* To make log more readable */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed TRACK; number: %d, mode_string: %s\n", __func__, number, mode_string);
    
    succeeded = __mirage_parser_cue_add_track(self, number, mode_string, error);
    
    g_free(mode_string);
    g_free(number_raw);
    
    return succeeded;
}

static gboolean __mirage_parser_cue_callback_isrc (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gboolean succeeded = TRUE;
    gchar *isrc = g_match_info_fetch_named(match_info, "isrc");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed ISRC: %s\n", __func__, isrc);
    
    succeeded = __mirage_parser_cue_set_isrc(self, isrc, error);
    
    g_free(isrc);
    
    return succeeded;
}

static gboolean __mirage_parser_cue_callback_index (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gboolean succeeded = TRUE;
    gchar *number_raw, *address_raw;
    gint number, address;
    
    number_raw = g_match_info_fetch_named(match_info, "index");
    number = g_strtod(number_raw, NULL);
    address_raw = g_match_info_fetch_named(match_info, "msf");
    address = mirage_helper_msf2lba_str(address_raw, FALSE);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed INDEX; number: %d, address: %s (%d)\n", __func__, number, address_raw, address);
    
    succeeded = __mirage_parser_cue_add_index(self, number, address, error);
    
    g_free(address_raw);
    g_free(number_raw);
    
    return succeeded;
}

static gboolean __mirage_parser_cue_callback_pregap (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gboolean succeeded = TRUE;
    gchar *length_raw;
    gint length;
    
    length_raw = g_match_info_fetch_named(match_info, "msf");
    length = mirage_helper_msf2lba_str(length_raw, FALSE);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed PREGAP; length: %s (%d)\n", __func__, length_raw, length);
    
    succeeded = __mirage_parser_cue_add_pregap(self, length, error);
    
    g_free(length_raw);
    
    return succeeded;
}

static gboolean __mirage_parser_cue_callback_postgap (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gboolean succeeded = TRUE;
    gchar *length_raw;
    gint length;
    
    length_raw = g_match_info_fetch_named(match_info, "msf");
    length = mirage_helper_msf2lba_str(length_raw, FALSE);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed POSTGAP; length: %s (%d)\n", __func__, g_match_info_fetch_named(match_info, "msf"), length);
    
    succeeded = __mirage_parser_cue_add_empty_part(self, length, error);
    
    g_free(length_raw);
    
    return succeeded;
}

static gboolean __mirage_parser_cue_callback_flags (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gchar *flags_dcp, *flags_4ch, *flags_pre, *flags_scms;
    gint flags = 0;
    
    flags_dcp = g_match_info_fetch_named(match_info, "dcp");
    flags_4ch = g_match_info_fetch_named(match_info, "4ch");
    flags_pre = g_match_info_fetch_named(match_info, "pre");
    flags_scms = g_match_info_fetch_named(match_info, "scms");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed FLAGS\n", __func__);
    
    if (!g_strcmp0(flags_dcp, "DCP")) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting DCP flag\n", __func__);
        flags |= MIRAGE_TRACKF_COPYPERMITTED;
    }
    if (!g_strcmp0(flags_4ch, "4CH")) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting 4CH flag\n", __func__);
        flags |= MIRAGE_TRACKF_FOURCHANNEL;
    }
    if (!g_strcmp0(flags_pre, "PRE")) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting PRE flag\n", __func__);
        flags |= MIRAGE_TRACKF_PREEMPHASIS;
    }
    if (!g_strcmp0(flags_scms, "SCMS")) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: SCMS flag not handled yet!\n", __func__);
    }
    
    g_free(flags_dcp);
    g_free(flags_4ch);
    g_free(flags_pre);
    g_free(flags_scms);
    
    return __mirage_parser_cue_set_flags(self, flags, error);
}

#define APPEND_REGEX_RULE(list,rule,callback) {                         \
    MIRAGE_RegexRule *new_rule = g_new(MIRAGE_RegexRule, 1);            \
    new_rule->regex = g_regex_new(rule, G_REGEX_OPTIMIZE, 0, NULL);     \
    new_rule->callback_func = callback;                                 \
                                                                        \
    list = g_list_append(list, new_rule);                               \
}

static void __mirage_parser_cue_init_regex_parser (MIRAGE_Parser *self) {
    MIRAGE_Parser_CUEPrivate *_priv = MIRAGE_PARSER_CUE_GET_PRIVATE(self);

    /* Ignore empty lines */
    APPEND_REGEX_RULE(_priv->regex_rules, "^[\\s]*$", NULL);
    
    /* "Extensions" that are embedded in the comments must appear before general
       comment rule */
    APPEND_REGEX_RULE(_priv->regex_rules, "REM SESSION (?<number>\\d+)$", __mirage_parser_cue_callback_session);
    
    APPEND_REGEX_RULE(_priv->regex_rules, "REM (?<comment>.+)$", __mirage_parser_cue_callback_comment);
    
    APPEND_REGEX_RULE(_priv->regex_rules, "CDTEXTFILE (?<filename>.+)$", __mirage_parser_cue_callback_cdtext);
    
    APPEND_REGEX_RULE(_priv->regex_rules, "CATALOG (?<catalog>\\d{13})$", __mirage_parser_cue_callback_catalog);
    
    APPEND_REGEX_RULE(_priv->regex_rules, "TITLE (?<title>.+)$", __mirage_parser_cue_callback_title);   
    APPEND_REGEX_RULE(_priv->regex_rules, "PERFORMER (?<performer>.+)$", __mirage_parser_cue_callback_performer);
    APPEND_REGEX_RULE(_priv->regex_rules, "SONGWRITER (?<songwriter>.+)$", __mirage_parser_cue_callback_songwriter);
    
    APPEND_REGEX_RULE(_priv->regex_rules, "FILE (?<filename>.+) (?<type>\\S+)$", __mirage_parser_cue_callback_file);
    APPEND_REGEX_RULE(_priv->regex_rules, "TRACK (?<number>\\d+) (?<type>\\S+)$", __mirage_parser_cue_callback_track);
    APPEND_REGEX_RULE(_priv->regex_rules, "ISRC (?<isrc>\\w{12})$", __mirage_parser_cue_callback_isrc);
    APPEND_REGEX_RULE(_priv->regex_rules, "INDEX (?<index>\\d+) (?<msf>[\\d]+:[\\d]+:[\\d]+)$", __mirage_parser_cue_callback_index);
    
    APPEND_REGEX_RULE(_priv->regex_rules, "PREGAP (?<msf>[\\d]+:[\\d]+:[\\d]+)$", __mirage_parser_cue_callback_pregap);
    APPEND_REGEX_RULE(_priv->regex_rules, "POSTGAP (?<msf>[\\d]+:[\\d]+:[\\d]+)$", __mirage_parser_cue_callback_postgap);
    
    APPEND_REGEX_RULE(_priv->regex_rules, "FLAGS (((?<dcp>DCP)|(?<4ch>4CH)|(?<pre>PRE)|(?<scms>SCMS)) *)+$", __mirage_parser_cue_callback_flags);
    
    return;
}

static void __mirage_parser_cue_cleanup_regex_parser (MIRAGE_Parser *self) {
    MIRAGE_Parser_CUEPrivate *_priv = MIRAGE_PARSER_CUE_GET_PRIVATE(self);
    GList *entry;
    
    G_LIST_FOR_EACH(entry, _priv->regex_rules) {
        MIRAGE_RegexRule *rule = entry->data;
        g_regex_unref(rule->regex);
        g_free(rule);
    }
    
    g_list_free(_priv->regex_rules);
}

static gboolean __mirage_parser_detect_and_set_encoding (MIRAGE_Parser *self, GIOChannel *io_channel, GError **error) {
    static gchar bom_utf32_be[] = { 0x00, 0x00, 0xFE, 0xFF };
    static gchar bom_utf32_le[] = { 0xFF, 0xFE, 0x00, 0x00 };
    static gchar bom_utf16_be[] = { 0xFE, 0xFF };
    static gchar bom_utf16_le[] = { 0xFF, 0xFE };
    
    gchar bom[4] = "";
    
    /* Set position at the beginning, and set encoding to NULL (raw bytes) */
    g_io_channel_seek_position(io_channel, 0, G_SEEK_SET, NULL);
    g_io_channel_set_encoding(io_channel, NULL, NULL);
    
    /* Read first four bytes */
    g_io_channel_read_chars(io_channel, bom, sizeof(bom), NULL, NULL);
    
    /* Reset the position */
    g_io_channel_seek_position(io_channel, 0, G_SEEK_SET, NULL);

    /* Set the encoding */
    if (!memcmp(bom, bom_utf32_be, sizeof(bom_utf32_be))) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: UTF-32 BE BOM found\n", __func__);
        g_io_channel_set_encoding(io_channel, "utf-32be", NULL);
    } else if (!memcmp(bom, bom_utf32_le, sizeof(bom_utf32_le))) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: UTF-32 LE BOM found\n", __func__);
        g_io_channel_set_encoding(io_channel, "utf-32le", NULL);        
    } else if (!memcmp(bom, bom_utf16_be, sizeof(bom_utf16_be))) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: UTF-16 BE BOM found\n", __func__);
        g_io_channel_set_encoding(io_channel, "utf-16be", NULL);
    } else if (!memcmp(bom, bom_utf16_le, sizeof(bom_utf16_le))) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: UTF-16 LE BOM found\n", __func__);
        g_io_channel_set_encoding(io_channel, "utf-16le", NULL);
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: no BOM found, assuming UTF-8\n", __func__);
        g_io_channel_set_encoding(io_channel, "utf-8", NULL);
    }
    
    return TRUE;
}

static gboolean __mirage_parser_parse_cue_file (MIRAGE_Parser *self, gchar *filename, GError **error) {
    MIRAGE_Parser_CUEPrivate *_priv = MIRAGE_PARSER_CUE_GET_PRIVATE(self);
    GError *io_error = NULL;
    GIOChannel *io_channel;
    gboolean succeeded = TRUE;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: opening file: %s\n", __func__, filename);
    
    /* Create IO channel for file */
    io_channel = g_io_channel_new_file(filename, "r", &io_error);
    if (!io_channel) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create IO channel: %s\n", __func__, io_error->message);
        g_error_free(io_error);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }
    
    /* FIXME: get encoding via params */
    __mirage_parser_detect_and_set_encoding(self, io_channel, NULL);
    
    /* Read file line-by-line */
    gint line_nr;
    for (line_nr = 1; ; line_nr++) {
        GIOStatus status;
        gchar *line_str;
        gsize line_len;
        
        status = g_io_channel_read_line(io_channel, &line_str, &line_len, NULL, &io_error);
        
        /* Handle EOF */
        if (status == G_IO_STATUS_EOF) {
            break;
        }
        
        /* Handle abnormal status */
        if (status != G_IO_STATUS_NORMAL) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: status %d while reading line #%d from IO channel: %s\n", __func__, status, line_nr, io_error ? io_error->message : "no error message");
            g_error_free(io_error);
            
            mirage_error(MIRAGE_E_IMAGEFILE, error);
            succeeded = FALSE;
            break;
        }
        
        /* GRegex matching engine */
        GMatchInfo *match_info = NULL;
        gboolean matched = FALSE;
        GList *entry;
        
        /* Go over all matching rules */
        G_LIST_FOR_EACH(entry, _priv->regex_rules) {
            MIRAGE_RegexRule *regex_rule = entry->data;
                        
            /* Try to match the given rule */
            if (g_regex_match(regex_rule->regex, line_str, 0, &match_info)) {
                if (regex_rule->callback_func) {
                    succeeded = regex_rule->callback_func(self, match_info, error);
                }
                matched = TRUE;
            }
            
            /* Must be freed in any case */
            g_match_info_free(match_info);
            
            /* Break if we had a match */
            if (matched) {
                break;
            }
        }
        
        /* Complain if we failed to match the line (should it be fatal?) */
        if (!matched) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to match line #%d: %s\n", __func__, line_nr, line_str);
            /* succeeded = FALSE */
        }
        
        g_free(line_str);
                
        /* In case callback didn't succeed... */
        if (!succeeded) {
            break;
        }
    }
    
    g_io_channel_unref(io_channel);
    
    return succeeded;
}

/******************************************************************************\
 *                     MIRAGE_Parser methods implementation                   *
\******************************************************************************/
static gboolean __mirage_parser_cue_load_image (MIRAGE_Parser *self, gchar **filenames, GObject **disc, GError **error) {
    MIRAGE_Parser_CUEPrivate *_priv = MIRAGE_PARSER_CUE_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    
    /* Check if we can load the file; we check the suffix */
    if (!mirage_helper_has_suffix(filenames[0], ".cue")) {
        mirage_error(MIRAGE_E_CANTHANDLE, error);
        return FALSE;
    }

    /* Create disc */
    _priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_attach_child(MIRAGE_OBJECT(self), _priv->disc, NULL);

    mirage_disc_set_filename(MIRAGE_DISC(_priv->disc), filenames, NULL);
    _priv->cue_filename = g_strdup(filenames[0]);

    /* First session is created manually (in case we're dealing with normal
       CUE file, which doesn't have session definitions anyway); note that we
       store only pointer, but release reference */
    if (!mirage_disc_add_session_by_index(MIRAGE_DISC(_priv->disc), -1, &_priv->cur_session, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __func__);
        succeeded = FALSE;
        goto end;
    }
    g_object_unref(_priv->cur_session);
    
    /* Parse the CUE */
    if (!__mirage_parser_parse_cue_file(self, _priv->cue_filename, error)) {
        succeeded = FALSE;
        goto end;
    }
    
    /* Finish last track */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finishing last track in the layout\n", __func__);
    if (!__mirage_parser_cue_finish_last_track(self, error)) {
        succeeded = FALSE;
        goto end;
    }

    /* Now guess medium type and if it's a CD-ROM, add Red Book pregap */
    gint medium_type = mirage_parser_guess_medium_type(self, _priv->disc);
    mirage_disc_set_medium_type(MIRAGE_DISC(_priv->disc), medium_type, NULL);
    if (medium_type == MIRAGE_MEDIUM_CD) {
        mirage_parser_add_redbook_pregap(self, _priv->disc, NULL);
    }

end:    
    /* Return disc */
    mirage_object_detach_child(MIRAGE_OBJECT(self), _priv->disc, NULL);
    if (succeeded) {
        *disc = _priv->disc;
    } else {
        g_object_unref(_priv->disc);
        *disc = NULL;
    }
        
    return succeeded;
}


/******************************************************************************\
 *                                Object init                                 *
\******************************************************************************/
/* Our parent class */
static MIRAGE_ParserClass *parent_class = NULL;

static void __mirage_parser_cue_instance_init (GTypeInstance *instance, gpointer g_class) {
    mirage_parser_generate_parser_info(MIRAGE_PARSER(instance),
        "PARSER-CUE",
        "CUE Image Parser",
        "1.0.0",
        "Rok Mandeljc",
        TRUE,
        "CUE images",
        2, ".cue", NULL
    );
    
    __mirage_parser_cue_init_regex_parser(MIRAGE_PARSER(instance));
    
    return;
}

static void __mirage_parser_cue_finalize (GObject *obj) {
    MIRAGE_Parser_CUE *self = MIRAGE_PARSER_CUE(obj);
    MIRAGE_Parser_CUEPrivate *_priv = MIRAGE_PARSER_CUE_GET_PRIVATE(self);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s:\n", __func__);
    
    /* Free elements of private structure */
    g_free(_priv->cue_filename);
    g_free(_priv->cur_data_filename);
    g_free(_priv->cur_data_type);
    
    /* Cleanup regex parser engine */
    __mirage_parser_cue_cleanup_regex_parser(MIRAGE_PARSER(self));
    
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __func__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}


static void __mirage_parser_cue_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_ParserClass *class_parser = MIRAGE_PARSER_CLASS(g_class);
    MIRAGE_Parser_CUEClass *klass = MIRAGE_PARSER_CUE_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Parser_CUEPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_parser_cue_finalize;
    
    /* Initialize MIRAGE_Parser methods */
    class_parser->load_image = __mirage_parser_cue_load_image;
        
    return;
}

GType mirage_parser_cue_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Parser_CUEClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_parser_cue_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Parser_CUE),
            0,      /* n_preallocs */
            __mirage_parser_cue_instance_init    /* instance_init */
        };
        
        type = g_type_module_register_type(module, MIRAGE_TYPE_PARSER, "MIRAGE_Parser_CUE", &info, 0);
    }
    
    return type;
}
