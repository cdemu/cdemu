/*
 *  libMirage: TOC image parser: Parser object
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

#include "image-toc.h"


/* Some prototypes from flex/bison */
int yylex_init (void *scanner);
void yyset_in  (FILE *in_str, void *yyscanner);
int yylex_destroy (void *yyscanner);
int yyparse (void *scanner, MIRAGE_Parser *self, GError **error);


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_PARSER_TOC_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_TOC, MIRAGE_Parser_TOCPrivate))

typedef struct {  
    GObject *disc;
    
    /* Pointers to current session and current track object, so that we don't
       have to retrieve them all the time; note that no reference is not kept 
       for them */
    GObject *cur_session;
    GObject *cur_track;
    
    /* Per-session data */
    gchar *toc_filename;
    
    gint cur_tfile_sectsize;
    
    gint cur_sfile_sectsize;
    gint cur_sfile_format;
    
    gint cur_langcode;
    GHashTable *lang_map;
    
    gchar *mixed_mode_bin;
    gint mixed_mode_offset;
} MIRAGE_Parser_TOCPrivate;

/******************************************************************************\
 *                         Parser private functions                           *
\******************************************************************************/
gboolean __mirage_parser_toc_set_toc_filename (MIRAGE_Parser *self, gchar *filename, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);
    
    g_free(_priv->toc_filename);
    _priv->toc_filename = g_strdup(filename);
    
    return TRUE;
}

gboolean __mirage_parser_toc_set_mcn (MIRAGE_Parser *self, gchar *mcn, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting MCN: <%s>\n", __func__, mcn);
    mirage_disc_set_mcn(MIRAGE_DISC(_priv->disc), mcn, NULL);
    
    return TRUE;
}

gboolean __mirage_parser_toc_set_session_type (MIRAGE_Parser *self, gchar *type_string, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);

    /* Decipher session type */
    static const struct {
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
            mirage_session_set_session_type(MIRAGE_SESSION(_priv->cur_session), session_types[i].type, NULL);
            break;
        }
    }
    
    return TRUE;
}

gboolean __mirage_parser_toc_add_track (MIRAGE_Parser *self, gchar *mode_string, gchar *subchan_string, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);

    /* Add track */
    _priv->cur_track = NULL;
    if (!mirage_session_add_track_by_index(MIRAGE_SESSION(_priv->cur_session), -1, &_priv->cur_track, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __func__);
        return FALSE;
    }
    g_object_unref(_priv->cur_track); /* Don't keep reference */

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
            mirage_track_set_mode(MIRAGE_TRACK(_priv->cur_track), track_modes[i].mode, NULL);
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
    
    return TRUE;
};


gboolean __mirage_parser_toc_add_track_fragment (MIRAGE_Parser *self, gint type, gchar *filename_string, gint base_offset, gint start, gint length, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);
    GObject *data_fragment;
    
    /* Create appropriate fragment */
    if (type == TOC_DATA_TYPE_NONE) {
        /* Empty fragment; we'd like a NULL fragment */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating NULL fragment\n", __func__);
        data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FINTERFACE_NULL, "NULL", error);
        if (!data_fragment) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create NULL fragment!\n", __func__);
            return FALSE;
        }
    } else {
        /* Find filename */
        gchar *filename = mirage_helper_find_data_file(filename_string, _priv->toc_filename);
        if (!filename) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to find data file!\n", __func__);
            mirage_error(MIRAGE_E_DATAFILE, error);
            return FALSE;
        }
        
        /* BINARY can be either explicitly requested; or it can be assumed from
           *.bin suffix (with TOC_DATA_TYPE_AUDIO), which is a bit hacky, but
           should work for now... */
        if (type == TOC_DATA_TYPE_DATA || mirage_helper_has_suffix(filename_string, ".bin")) {
            /* Binary data; we'd like a BINARY fragment */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating BINARY fragment\n", __func__);
            data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FINTERFACE_BINARY, filename, error);
            if (!data_fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create BINARY fragment!\n", __func__);
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
                /* By default TOC's BIN audio files need to be swapped */
                tfile_format = FR_BIN_TFILE_AUDIO_SWAP;
            } else {
                tfile_format = FR_BIN_TFILE_DATA;
            }
            
            
            /* Some TOC files don't seem to contain #base_offset entries that 
               are used in case of mixed mode CD... which means we have to 
               calculate base_offset ourselves :( */
            if (!base_offset) {
                /* If we don't have mixed mode BIN filename set yet or if it 
                   differs from  the one currently set, we're dealing with new 
                   file... so we reset offset and store the filename */
                if (!_priv->mixed_mode_bin || mirage_helper_strcasecmp(_priv->mixed_mode_bin, filename)) {
                    _priv->mixed_mode_offset = 0;
                    g_free(_priv->mixed_mode_bin);
                    _priv->mixed_mode_bin = g_strdup(filename);
                }
                
                base_offset = _priv->mixed_mode_offset;
                
                /* I guess it's safe to calculate this here; if length isn't
                   provided, it means whole file is used, so most likely we'll 
                   get file changed next time we're called...*/
                if (type == TOC_DATA_TYPE_DATA) {
                    /* Increase only if it's data... */
                    _priv->mixed_mode_offset += length * (_priv->cur_tfile_sectsize + _priv->cur_sfile_sectsize);
                }
            }
            
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: using base offset: 0x%lX\n", __func__, base_offset);
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
            data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FINTERFACE_AUDIO, filename, error);
            if (!data_fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create appropriate AUDIO fragment!\n", __func__);
                return FALSE;
            }
            
            /* Set file */
            if (!mirage_finterface_audio_set_file(MIRAGE_FINTERFACE_AUDIO(data_fragment), filename, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set file to AUDIO fragment!\n", __func__);
                g_object_unref(data_fragment);
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
            return FALSE;
        }
    }
    
    /* Add fragment */
    mirage_track_add_fragment(MIRAGE_TRACK(_priv->cur_track), -1, &data_fragment, NULL);
    g_object_unref(data_fragment);
    
    return TRUE;
};

gboolean __mirage_parser_toc_set_track_start (MIRAGE_Parser *self, gint start, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);
        
    /* If start is not given (-1), we use current track length */
    if (start == -1) {
        mirage_track_layout_get_length(MIRAGE_TRACK(_priv->cur_track), &start, NULL);
    }
    
    mirage_track_set_track_start(MIRAGE_TRACK(_priv->cur_track), start, NULL);
        
    return TRUE;
}

gboolean __mirage_parser_toc_add_index (MIRAGE_Parser *self, gint address, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);
    gint track_start;
        
    /* Indices in TOC file are track-start relative... */
    mirage_track_get_track_start(MIRAGE_TRACK(_priv->cur_track), &track_start, NULL);
    mirage_track_add_index(MIRAGE_TRACK(_priv->cur_track), track_start + address, NULL, NULL);
        
    return TRUE;
}

gboolean __mirage_parser_toc_set_flag (MIRAGE_Parser *self, gint flag, gboolean set, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);
    gint flags;
    
    mirage_track_get_flags(MIRAGE_TRACK(_priv->cur_track), &flags, NULL);
    if (set) {
        /* Set flag */
        flags |= flag;
    } else {
        /* Clear flag */
        flags &= ~flag;
    }
    mirage_track_set_flags(MIRAGE_TRACK(_priv->cur_track), flags, NULL);
    
    return TRUE;
}

gboolean __mirage_parser_toc_set_isrc (MIRAGE_Parser *self, gchar *isrc, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting ISRC: <%s>\n", __func__, isrc);
    mirage_track_set_isrc(MIRAGE_TRACK(_priv->cur_track), isrc, NULL);

    return TRUE;
}

gboolean __mirage_parser_toc_add_language_mapping (MIRAGE_Parser *self, gint index, gint langcode, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding language map: index %i -> langcode %i\n", __func__, index, langcode);
    
    g_hash_table_insert(_priv->lang_map, GINT_TO_POINTER(index), GINT_TO_POINTER(langcode));
        
    return TRUE;
}

gboolean __mirage_parser_toc_add_g_laguage (MIRAGE_Parser *self, gint index, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);
    gint langcode = GPOINTER_TO_INT(g_hash_table_lookup(_priv->lang_map, GINT_TO_POINTER(index)));
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding global language: index %i -> langcode %i\n", __func__, index, langcode);
    
    mirage_session_add_language(MIRAGE_SESSION(_priv->cur_session), langcode, NULL, NULL);
    _priv->cur_langcode = langcode;
    
    return TRUE;
}

gboolean __mirage_parser_toc_add_t_laguage (MIRAGE_Parser *self, gint index, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);
    gint langcode = GPOINTER_TO_INT(g_hash_table_lookup(_priv->lang_map, GINT_TO_POINTER(index)));
       
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding track language: index %i -> langcode %i\n", __func__, index, langcode);
    mirage_track_add_language(MIRAGE_TRACK(_priv->cur_track), langcode, NULL, NULL);
    _priv->cur_langcode = langcode;
    
    return TRUE;
}

gboolean __mirage_parser_toc_set_g_cdtext_data (MIRAGE_Parser *self, gint pack_type, gchar *data, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);
    GObject *language = NULL;
    
    if (mirage_session_get_language_by_code(MIRAGE_SESSION(_priv->cur_session), _priv->cur_langcode, &language, NULL)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: pack type: 0x%X, data: <%s>\n", __func__, pack_type, data);
        mirage_language_set_pack_data(MIRAGE_LANGUAGE(language), pack_type, data, strlen(data)+1, NULL);
        g_object_unref(language);
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get language object!\n", __func__);
    }
        
    return TRUE;
}

gboolean __mirage_parser_toc_set_t_cdtext_data (MIRAGE_Parser *self, gint pack_type, gchar *data, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);
    GObject *language = NULL;
    
    if (mirage_track_get_language_by_code(MIRAGE_TRACK(_priv->cur_track), _priv->cur_langcode, &language, NULL)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: pack type: 0x%X, data: <%s>\n", __func__, pack_type, data);
        mirage_language_set_pack_data(MIRAGE_LANGUAGE(language), pack_type, data, strlen(data)+1, NULL);
        g_object_unref(language);
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get language object!\n", __func__);
    }
    
    return TRUE;
}


/******************************************************************************\
 *                     MIRAGE_Parser methods implementation                     *
\******************************************************************************/
static void __init_session_data (MIRAGE_Parser *self) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);

    /* Init langmap */
    _priv->lang_map = g_hash_table_new(g_direct_hash, g_direct_equal);

    return;
}

static void __cleanup_session_data (MIRAGE_Parser *self) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);

    /* No reference is kept for these, so just set them to NULL */
    _priv->cur_session = NULL;
    _priv->cur_track = NULL;
    
    /* Cleanup per-session data */
    g_free(_priv->toc_filename);
    _priv->toc_filename = NULL;
    
    _priv->cur_tfile_sectsize = 0;
    
    _priv->cur_sfile_sectsize = 0;
    _priv->cur_sfile_format = 0;
    
    _priv->cur_langcode = 0;
    g_hash_table_destroy(_priv->lang_map);

    g_free(_priv->mixed_mode_bin);
    _priv->mixed_mode_bin = NULL;
    _priv->mixed_mode_offset = 0;
    
    return;
}

static gboolean __mirage_parser_toc_load_image (MIRAGE_Parser *self, gchar **filenames, GObject **disc, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    gint i;
    
    /* Check if we can load file(s); we check the suffix */
    for (i = 0; i < g_strv_length(filenames); i++) {
        if (!mirage_helper_has_suffix(filenames[i], ".toc")) {
            mirage_error(MIRAGE_E_CANTHANDLE, error);
            return FALSE;
        }
    }
    
    /* Create disc */
    _priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_attach_child(MIRAGE_OBJECT(self), _priv->disc, NULL);

    mirage_disc_set_filenames(MIRAGE_DISC(_priv->disc), filenames, NULL);
    
    /* Each TOC/BIN is one session, so we load all given filenames */
    for (i = 0; i < g_strv_length(filenames); i++) {
        void *scanner;
        FILE *file;
        
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: loading session #%i: '%s'!\n", __func__, i, filenames[i]);
        __init_session_data(self);
        __mirage_parser_toc_set_toc_filename(self, filenames[i], NULL);
        
        /* There's slight problem with multi-session TOC images, namely that each
           TOC can be used independently... in order words, there's no way to determine
           the length of leadouts for sessions (since all sessions start at sector 0).
           So we use what multisession FAQ from cdrecord docs tells us... */
        if (i > 0) {
            GObject *prev_session = NULL;
            gint leadout_length = 0;

            if (!mirage_disc_get_session_by_index(MIRAGE_DISC(_priv->disc), -1, &prev_session, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to get previous session!\n", __func__);
                succeeded = FALSE;
                goto end;
            }
            
            /* Second session has index 1... */
            if (i == 1) {
                leadout_length = 11250; /* Actually, it should be 6750 previous leadout, 4500 current leadin */
            } else {
                leadout_length = 6750; /* Actually, it should be 2250 previous leadout, 4500 current leadin */                
            }
            
            mirage_session_set_leadout_length(MIRAGE_SESSION(prev_session), leadout_length, NULL);
            
            g_object_unref(prev_session);
        }
        
        /* Open file */
        file = g_fopen(filenames[i], "r");
        if (!file) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to open file '%s'!\n", __func__, filenames[i]);
            mirage_error(MIRAGE_E_IMAGEFILE, error);
            succeeded = FALSE;
            goto end;
        }
        
        /* Create session */
        _priv->cur_session = NULL;
        if (!mirage_disc_add_session_by_index(MIRAGE_DISC(_priv->disc), -1, &_priv->cur_session, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to add session!\n", __func__);
            succeeded = FALSE;
            goto end;
        }
        g_object_unref(_priv->cur_session); /* Don't keep reference */

        /* Prepare scanner */
        yylex_init(&scanner);
        yyset_in(file, scanner);
        
        /* Load */
        if (yyparse(scanner, self, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to parse TOC file!\n", __func__);
            fclose(file);
            succeeded = FALSE;
            goto end;
        }
        
        /* Destroy scanner */
        yylex_destroy(scanner);        
        fclose(file);
        
        __cleanup_session_data(self);
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

static void __mirage_parser_toc_instance_init (GTypeInstance *instance, gpointer g_class) {
    mirage_parser_generate_parser_info(MIRAGE_PARSER(instance),
        "PARSER-TOC",
        "TOC Image Parser",
        "1.0.0",
        "Rok Mandeljc",
        TRUE,
        "TOC files",
        2, ".toc", NULL
    );
    
    return;
}

static void __mirage_parser_toc_class_init (gpointer g_class, gpointer g_class_data) {
    MIRAGE_ParserClass *class_parser = MIRAGE_PARSER_CLASS(g_class);
    MIRAGE_Parser_TOCClass *klass = MIRAGE_PARSER_TOC_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Parser_TOCPrivate));
        
    /* Initialize MIRAGE_Parser methods */
    class_parser->load_image = __mirage_parser_toc_load_image;
        
    return;
}

GType mirage_parser_toc_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Parser_TOCClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_parser_toc_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Parser_TOC),
            0,      /* n_preallocs */
            __mirage_parser_toc_instance_init    /* instance_init */
        };
        
        type = g_type_module_register_type(module, MIRAGE_TYPE_PARSER, "MIRAGE_Parser_TOC", &info, 0);
    }
    
    return type;
}
