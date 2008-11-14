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


/* Some prototypes from flex/bison */
int yylex_init (void *scanner);
void yyset_in  (FILE *in_str, void *yyscanner);
int yylex_destroy (void *yyscanner);
int yyparse (void *scanner, MIRAGE_Parser *self, GError **error);


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
    
    gboolean cur_pregap_set;
    
    gint leadout_correction;
    
    /* Pointers to current session and current track object, so that we don't
       have to retrieve them all the time; note that no reference is not kept 
       for them */
    GObject *cur_session;
    GObject *cur_track;
    GObject *prev_track;
} MIRAGE_Parser_CUEPrivate;


/******************************************************************************\
 *                         Parser private functions                           *
\******************************************************************************/
gboolean __mirage_parser_cue_finish_last_track (MIRAGE_Parser *self, GError **error) {
    MIRAGE_Parser_CUEPrivate *_priv = MIRAGE_PARSER_CUE_GET_PRIVATE(self);
    GObject *data_fragment = NULL;
    
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
        mirage_fragment_use_the_rest_of_file(MIRAGE_FRAGMENT(data_fragment), NULL);
        g_object_unref(data_fragment);
    }

    return TRUE;
}

gboolean __mirage_parser_cue_set_mcn (MIRAGE_Parser *self, gchar *mcn, GError **error) {    
    MIRAGE_Parser_CUEPrivate *_priv = MIRAGE_PARSER_CUE_GET_PRIVATE(self);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: MCN: %.13s\n", __func__, mcn);
    mirage_disc_set_mcn(MIRAGE_DISC(_priv->disc), mcn, NULL);
    
    return TRUE;
}

gboolean __mirage_parser_cue_set_new_file (MIRAGE_Parser *self, gchar *filename_string, gchar *file_type, GError **error) {
    MIRAGE_Parser_CUEPrivate *_priv = MIRAGE_PARSER_CUE_GET_PRIVATE(self);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
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
    
    return TRUE;
}

gboolean __mirage_parser_cue_add_track (MIRAGE_Parser *self, gint number, gchar *mode_string, GError **error) {
    MIRAGE_Parser_CUEPrivate *_priv = MIRAGE_PARSER_CUE_GET_PRIVATE(self);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
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
    
    /* Reset parser info on current track */
    _priv->cur_pregap_set = FALSE;
    
    return TRUE;
};

gboolean __mirage_parser_cue_add_index (MIRAGE_Parser *self, gint number, gint address, GError **error) {
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
                    
                    g_object_unref(lfragment);
                }
            }
            
            /* Now current track; we only create fragment here and set its offset */
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
                
                data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FINTERFACE_BINARY, _priv->cur_data_filename, error);
                if (!data_fragment) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create data fragment!\n", __func__);
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

gboolean __mirage_parser_cue_set_flag (MIRAGE_Parser *self, gint flag, GError **error) {
    MIRAGE_Parser_CUEPrivate *_priv = MIRAGE_PARSER_CUE_GET_PRIVATE(self);    

    /* Current track needs to be set at this point */
    if (!_priv->cur_track) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: current track is not set!\n", __func__);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }
    
    gint flags = 0;
    
    mirage_track_get_flags(MIRAGE_TRACK(_priv->cur_track), &flags, NULL);
    flags |= flag;
    mirage_track_set_flags(MIRAGE_TRACK(_priv->cur_track), flags, NULL);
        
    return TRUE;
}

gboolean __mirage_parser_cue_set_isrc (MIRAGE_Parser *self, gchar *isrc, GError **error) {
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

gboolean __mirage_parser_cue_add_pregap (MIRAGE_Parser *self, gint length, GError **error) {
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

gboolean __mirage_parser_cue_add_empty_part (MIRAGE_Parser *self, gint length, GError **error) {
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

gboolean __mirage_parser_cue_add_session (MIRAGE_Parser *self, gint number, GError **error) {
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
    
    return TRUE;
}

/******************************************************************************\
 *                     MIRAGE_Parser methods implementation                   *
\******************************************************************************/
static gboolean __mirage_parser_cue_load_image (MIRAGE_Parser *self, gchar **filenames, GObject **disc, GError **error) {
    MIRAGE_Parser_CUEPrivate *_priv = MIRAGE_PARSER_CUE_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    void *scanner;
    FILE *file;
    
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
    
    /* Open file */
    file = g_fopen(filenames[0], "r");
    if (!file) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open file '%s'!\n", __func__, filenames[0]);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        succeeded = FALSE;
        goto end;
    }
    
    /* Prepare scanner */
    yylex_init(&scanner);
    yyset_in(file, scanner);
        
    /* Load */
    if (yyparse(scanner, self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse CUE file!\n", __func__);
        fclose(file);
        succeeded = FALSE;
        goto end;
    }
        
    /* Destroy scanner */
    yylex_destroy(scanner);
    fclose(file);
    
    /* Finish last track */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finishing last track in the layout\n", __func__);
    __mirage_parser_cue_finish_last_track(self, NULL);

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
        PACKAGE_VERSION,
        "Rok Mandeljc",
        TRUE,
        "CUE images",
        2, ".cue", NULL
    );
    
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
