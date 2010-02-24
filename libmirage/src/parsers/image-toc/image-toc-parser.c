/*
 *  libMirage: TOC image parser: Parser object
 *  Copyright (C) 2006-2009 Rok Mandeljc
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

#define __debug__ "TOC-Parser"


/* Regex engine */
typedef gboolean (*MIRAGE_RegexCallback) (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error);

typedef struct {
    GRegex *regex;
    MIRAGE_RegexCallback callback_func;
} MIRAGE_RegexRule;


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
    
    /* Regex engine */
    GList *regex_rules;
    
    GRegex *regex_cdtext;
    GRegex *regex_langmap;
    GRegex *regex_language;
    GRegex *regex_langdata;
    GRegex *regex_binary;

    /* Header matching for TOC file verification */
    GRegex *regex_header_ptr; /* Pointer, do not free! */
} MIRAGE_Parser_TOCPrivate;

/******************************************************************************\
 *                         Parser private functions                           *
\******************************************************************************/
static gboolean __mirage_parser_toc_set_session_type (MIRAGE_Parser *self, gchar *type_string, GError **error) {
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
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: session type: %s\n", __debug__, session_types[i].str);
            mirage_session_set_session_type(MIRAGE_SESSION(_priv->cur_session), session_types[i].type, NULL);
            break;
        }
    }
    
    return TRUE;
}

static gboolean __mirage_parser_toc_add_track (MIRAGE_Parser *self, gchar *mode_string, gchar *subchan_string, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);

    /* Add track */
    _priv->cur_track = NULL;
    if (!mirage_session_add_track_by_index(MIRAGE_SESSION(_priv->cur_session), -1, &_priv->cur_track, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __debug__);
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
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track mode: %s\n", __debug__, track_modes[i].str);
            
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
            gint format;
            gint sectsize;
        } subchan_modes[] = {
            {"RW_RAW", FR_BIN_SFILE_PW96_INT | FR_BIN_SFILE_INT, 96 },
            {"RW", FR_BIN_SFILE_RW96 | FR_BIN_SFILE_INT, 96 },
        };
        
        gint i;
        for (i = 0; i < G_N_ELEMENTS(subchan_modes); i++) {
            if (!strcasecmp(subchan_modes[i].str, subchan_string)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: subchannel mode: %s\n", __debug__, subchan_modes[i].str);
                _priv->cur_sfile_sectsize = subchan_modes[i].sectsize;
                _priv->cur_sfile_format = subchan_modes[i].format;
                break;
            }
        }
    }
    
    return TRUE;
};


static gboolean __mirage_parser_toc_track_add_fragment (MIRAGE_Parser *self, gint type, gchar *filename_string, gint base_offset, gint start, gint length, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);
    GObject *data_fragment;
    
    /* Create appropriate fragment */
    if (type == TOC_DATA_TYPE_NONE) {
        /* Empty fragment; we'd like a NULL fragment */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating NULL fragment\n", __debug__);
        data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FINTERFACE_NULL, "NULL", error);
        if (!data_fragment) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create NULL fragment!\n", __debug__);
            return FALSE;
        }
    } else {
        /* Find filename */
        gchar *filename = mirage_helper_find_data_file(filename_string, _priv->toc_filename);
        if (!filename) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to find data file!\n", __debug__);
            mirage_error(MIRAGE_E_DATAFILE, error);
            return FALSE;
        }
        
        /* BINARY can be either explicitly requested; or it can be assumed from
           *.bin suffix (with TOC_DATA_TYPE_AUDIO), which is a bit hacky, but
           should work for now... */
        if (type == TOC_DATA_TYPE_DATA || mirage_helper_has_suffix(filename_string, ".bin")) {
            /* Binary data; we'd like a BINARY fragment */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating BINARY fragment\n", __debug__);
            data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FINTERFACE_BINARY, filename, error);
            if (!data_fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create BINARY fragment!\n", __debug__);
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
        
            /* If we're dealing with BINARY AUDIO data, we need to swap it... 
               (type == TOC_DATA_TYPE_AUDIO) is not sufficient check, because
               apparently when .bin file contains subchannel data, it automatically
               gets listed as DATAFILE (hence type = TOC_DATA_TYPE_DATA); thus,
               we simply check whether we have an audio track or not... */
            gint mode;
            mirage_track_get_mode(MIRAGE_TRACK(_priv->cur_track), &mode, NULL);
            if (mode == MIRAGE_MODE_AUDIO) {
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
            
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: using base offset: 0x%lX\n", __debug__, base_offset);
            tfile_offset = base_offset + start * (_priv->cur_tfile_sectsize + _priv->cur_sfile_sectsize);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: calculated track file offset: 0x%llX\n", __debug__, tfile_offset);
  
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
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating AUDIO fragment\n", __debug__);
            data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FINTERFACE_AUDIO, filename, error);
            if (!data_fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create appropriate AUDIO fragment!\n", __debug__);
                return FALSE;
            }
            
            /* Set file */
            if (!mirage_finterface_audio_set_file(MIRAGE_FINTERFACE_AUDIO(data_fragment), filename, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set file to AUDIO fragment!\n", __debug__);
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
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting fragment's length: 0x%X\n", __debug__, length);
        mirage_fragment_set_length(MIRAGE_FRAGMENT(data_fragment), length, NULL);
    } else {
        /* Use whole file */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: using whole file\n", __debug__);
        if (!mirage_fragment_use_the_rest_of_file(MIRAGE_FRAGMENT(data_fragment), error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to use whole file!\n", __debug__);
            g_object_unref(data_fragment);
            return FALSE;
        }
    }
    
    /* Add fragment */
    mirage_track_add_fragment(MIRAGE_TRACK(_priv->cur_track), -1, &data_fragment, NULL);
    g_object_unref(data_fragment);
    
    return TRUE;
};

static gboolean __mirage_parser_toc_track_set_start (MIRAGE_Parser *self, gint start, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);
        
    /* If start is not given (-1), we use current track length */
    if (start == -1) {
        mirage_track_layout_get_length(MIRAGE_TRACK(_priv->cur_track), &start, NULL);
    }
    
    mirage_track_set_track_start(MIRAGE_TRACK(_priv->cur_track), start, NULL);
        
    return TRUE;
}

static gboolean __mirage_parser_toc_track_add_index (MIRAGE_Parser *self, gint address, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);
    gint track_start;
        
    /* Indices in TOC file are track-start relative... */
    mirage_track_get_track_start(MIRAGE_TRACK(_priv->cur_track), &track_start, NULL);
    mirage_track_add_index(MIRAGE_TRACK(_priv->cur_track), track_start + address, NULL, NULL);
        
    return TRUE;
}

static gboolean __mirage_parser_toc_track_set_flag (MIRAGE_Parser *self, gint flag, gboolean set, GError **error) {
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

static gboolean __mirage_parser_toc_track_set_isrc (MIRAGE_Parser *self, gchar *isrc, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting ISRC: <%s>\n", __debug__, isrc);
    mirage_track_set_isrc(MIRAGE_TRACK(_priv->cur_track), isrc, NULL);

    return TRUE;
}

/******************************************************************************\
 *                              CD-TEXT parsing                               *
\******************************************************************************/
static gboolean __mirage_parser_toc_cdtext_parse_binary (MIRAGE_Parser *self, gchar *bin_str, gchar **ret_str, gint *ret_len, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);
    gchar **elements;
    gchar *data_str;
    gint data_len;
    gint i;
    
    elements = g_regex_split(_priv->regex_binary, bin_str, 0);
    
    data_len = g_strv_length(elements);
    data_str = g_new(gchar, data_len);
    
    for (i = 0; i < data_len; i++) {
        data_str[i] = atoi(elements[i]);
    }
    
    g_strfreev(elements);
    
    *ret_str = data_str;
    if (ret_len) *ret_len = data_len;
    
    return TRUE;
}

static gboolean __mirage_parser_toc_cdtext_parse_langmaps (MIRAGE_Parser *self, gchar *langmaps_str, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);
    GMatchInfo *match_info;
    
    g_regex_match(_priv->regex_langmap, langmaps_str, 0, &match_info);
    while (g_match_info_matches(match_info)) {
        gchar *index_str = g_match_info_fetch_named(match_info, "lang_idx");
        gchar *code_str = g_match_info_fetch_named(match_info, "lang_code");
        gint index, code;
        
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: language map: index: %s, code: %s\n", __debug__, index_str, code_str);
        
        /* Index */
        index = atoi(index_str);
        
        /* Code; try to match known language names, then simply try to convert */
        if (!g_ascii_strcasecmp(code_str, "EN")) {
            code = 9; /* EN */
        } else {
            code = atoi(code_str);
        }
        
        g_hash_table_insert(_priv->lang_map, GINT_TO_POINTER(index), GINT_TO_POINTER(code));
        
        g_free(code_str);
        g_free(index_str);
        
        g_match_info_next(match_info, NULL);
    }

    g_match_info_free(match_info);
    
    return TRUE;
}

static gboolean __mirage_parser_toc_cdtext_parse_language (MIRAGE_Parser *self, gchar *data_str, GObject *language, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);
    GMatchInfo *match_info;
    
    gint i;
    static struct {
        gchar *pack_id;
        gint pack_type;
    } packs[] = {
        { "TITLE", MIRAGE_LANGUAGE_PACK_TITLE },
        { "PERFORMER", MIRAGE_LANGUAGE_PACK_PERFORMER },
        { "SONGWRITER", MIRAGE_LANGUAGE_PACK_SONGWRITER },
        { "COMPOSER", MIRAGE_LANGUAGE_PACK_COMPOSER },
        { "ARRANGER", MIRAGE_LANGUAGE_PACK_ARRANGER },
        { "MESSAGE", MIRAGE_LANGUAGE_PACK_MESSAGE },
        { "DISC_ID", MIRAGE_LANGUAGE_PACK_DISC_ID },
        { "GENRE", MIRAGE_LANGUAGE_PACK_GENRE },
        { "TOC_INFO1", MIRAGE_LANGUAGE_PACK_TOC },
        { "TOC_INFO2", MIRAGE_LANGUAGE_PACK_TOC2 },
        { "UPC_EAN", MIRAGE_LANGUAGE_PACK_UPC_ISRC },
        { "SIZE_INFO", MIRAGE_LANGUAGE_PACK_SIZE },
    };
    
    g_regex_match(_priv->regex_langdata, data_str, 0, &match_info);
    while (g_match_info_matches(match_info)) {
        gchar *type_str;
        gchar *content_str;
        
        gchar *content;
        gint content_len;
        
        type_str = g_match_info_fetch_named(match_info, "type1");
        if (type_str && strlen(type_str)) {
            content_str = g_match_info_fetch_named(match_info, "data1");
            
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: pack %s; string: %s\n", __debug__, type_str, content_str);
            
            content = g_strdup(content_str);
            content_len = strlen(content_str)+1;
        } else {
            g_free(type_str);
            type_str = g_match_info_fetch_named(match_info, "type2");
            content_str = g_match_info_fetch_named(match_info, "data2");
            
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: pack %s; binary data\n", __debug__, type_str);
            __mirage_parser_toc_cdtext_parse_binary(self, content_str, &content, &content_len, NULL);
        }
        
        /* Set appropriate pack */
        for (i = 0; i < G_N_ELEMENTS(packs); i++) {
            if (!strcmp(type_str, packs[i].pack_id)) {
                mirage_language_set_pack_data(MIRAGE_LANGUAGE(language), packs[i].pack_type, content, content_len, NULL);
                break;
            }
        }

        g_free(content);
        
        g_free(content_str);
        g_free(type_str);
        
        g_match_info_next(match_info, NULL);
    }
    
    g_match_info_free(match_info);
    
    return TRUE;
}

static gboolean __mirage_parser_toc_cdtext_parse_disc_languages (MIRAGE_Parser *self, gchar *languages_str, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);
    GMatchInfo *match_info;
   
    g_regex_match(_priv->regex_language, languages_str, 0, &match_info);
    while (g_match_info_matches(match_info)) {
        gchar *index_str = g_match_info_fetch_named(match_info, "lang_idx");
        gchar *data_str = g_match_info_fetch_named(match_info, "lang_data");
        gint index = atoi(index_str);
        gint code = GPOINTER_TO_INT(g_hash_table_lookup(_priv->lang_map, GINT_TO_POINTER(index)));

        GObject *language = NULL;
        
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding disc language: index %i -> code %i\n", __debug__, index, code);
        if (mirage_session_add_language(MIRAGE_SESSION(_priv->cur_session), code, &language, NULL)) {
            __mirage_parser_toc_cdtext_parse_language(self, data_str, language, NULL);
            g_object_unref(language);
        } else {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add disc language (index %i, code %i)!\n", __debug__, index, code);
        }
        
        g_free(data_str);
        g_free(index_str);
        
        g_match_info_next(match_info, NULL);
    }

    g_match_info_free(match_info);

    return TRUE;
}

static gboolean __mirage_parser_toc_cdtext_parse_track_languages (MIRAGE_Parser *self, gchar *languages_str, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);
    GMatchInfo *match_info;
   
    g_regex_match(_priv->regex_language, languages_str, 0, &match_info);
    while (g_match_info_matches(match_info)) {
        gchar *index_str = g_match_info_fetch_named(match_info, "lang_idx");
        gchar *data_str = g_match_info_fetch_named(match_info, "lang_data");
        gint index = atoi(index_str);
        gint code = GPOINTER_TO_INT(g_hash_table_lookup(_priv->lang_map, GINT_TO_POINTER(index)));

        GObject *language = NULL;
        
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding track language: index %i -> code %i\n", __debug__, index, code);
        if (mirage_track_add_language(MIRAGE_TRACK(_priv->cur_track), code, &language, NULL)) {
            __mirage_parser_toc_cdtext_parse_language(self, data_str, language, NULL);
            g_object_unref(language);
        } else {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track language (index %i, code %i)!\n", __debug__, index, code);
        }
        
        g_free(data_str);
        g_free(index_str);
        
        g_match_info_next(match_info, NULL);
    }

    g_match_info_free(match_info);
    
    return TRUE;
}

static gboolean __mirage_parser_toc_callback_cdtext (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);

    if (!_priv->cur_track) {
        /* Disc CD-TEXT */
        gchar *langmaps_str, *languages_str;
        
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: disc CD-TEXT\n", __debug__);
        
        langmaps_str = g_match_info_fetch_named(match_info, "langmaps");
        languages_str = g_match_info_fetch_named(match_info, "languages"); 
        
        __mirage_parser_toc_cdtext_parse_langmaps(self, langmaps_str, NULL);
        __mirage_parser_toc_cdtext_parse_disc_languages(self, languages_str, NULL);
        
        g_free(langmaps_str);
        g_free(languages_str);
    } else {
        /* Track CD-TEXT */
        gchar *languages_str;
        
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track CD-TEXT\n", __debug__);
        
        languages_str = g_match_info_fetch_named(match_info, "languages"); 
        __mirage_parser_toc_cdtext_parse_track_languages(self, languages_str, NULL);

        g_free(languages_str);        
    }
    
    return TRUE;
}


/******************************************************************************\
 *                           Regex parsing engine                             *
\******************************************************************************/
static gboolean __mirage_parser_toc_callback_comment (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gchar *comment = g_match_info_fetch_named(match_info, "comment");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n"); /* To make log more readable */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed COMMENT: %s\n", __debug__, comment);
    
    g_free(comment);
    
    return TRUE;
}

static gboolean __mirage_parser_toc_callback_session_type (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gboolean succeeded = TRUE;
    gchar *type = g_match_info_fetch_named(match_info, "type");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed SESSION TYPE: %s\n", __debug__, type);
    
    succeeded = __mirage_parser_toc_set_session_type(self, type, error);
    
    g_free(type);
    
    return succeeded;
}

static gboolean __mirage_parser_toc_callback_catalog (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);
    gchar *catalog = g_match_info_fetch_named(match_info, "catalog");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed CATALOG: %.13s\n", __debug__, catalog);
    
    mirage_disc_set_mcn(MIRAGE_DISC(_priv->disc), catalog, NULL);
    
    g_free(catalog);
    
    return TRUE;
}

static gboolean __mirage_parser_toc_callback_track (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gboolean succeeded = TRUE;
    gchar *type, *subchan;
    
    type = g_match_info_fetch_named(match_info, "type");
    subchan = g_match_info_fetch_named(match_info, "subchan");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n"); /* To make log more readable */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed TRACK: type: %s, sub: %s\n", __debug__, type, subchan);
    
    succeeded = __mirage_parser_toc_add_track(self, type, subchan, error);
    
    g_free(subchan);
    g_free(type);
    
    return succeeded;
}

static gboolean __mirage_parser_toc_callback_track_flag_copy (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gboolean succeeded = TRUE;
    gchar *no = g_match_info_fetch_named(match_info, "no");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed %s COPY track flag\n", __debug__, no ? no : "");
    
    if (!g_strcmp0(no, "NO")) {
        succeeded = __mirage_parser_toc_track_set_flag(self, MIRAGE_TRACKF_COPYPERMITTED, TRUE, error);
    } else {
        succeeded = __mirage_parser_toc_track_set_flag(self, MIRAGE_TRACKF_COPYPERMITTED, FALSE, error);
    }
    
    g_free(no);
    
    return succeeded;
}

static gboolean __mirage_parser_toc_callback_track_flag_preemphasis (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gboolean succeeded = TRUE;
    gchar *no = g_match_info_fetch_named(match_info, "no");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed %s PRE_EMPHASIS track flag\n", __debug__, no ? no : "");
    
    if (!g_strcmp0(no, "NO")) {
        succeeded = __mirage_parser_toc_track_set_flag(self, MIRAGE_TRACKF_PREEMPHASIS, TRUE, error);
    } else {
        succeeded = __mirage_parser_toc_track_set_flag(self, MIRAGE_TRACKF_PREEMPHASIS, FALSE, error);
    }
    
    g_free(no);
    
    return succeeded;
}

static gboolean __mirage_parser_toc_callback_track_flag_channels (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gboolean succeeded = TRUE;
    gchar *num = g_match_info_fetch_named(match_info, "num");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed %s_CHANNEL_AUDIO track flag\n", __debug__, num);
    
    if (!g_strcmp0(num, "FOUR")) {
        succeeded = __mirage_parser_toc_track_set_flag(self, MIRAGE_TRACKF_FOURCHANNEL, TRUE, error);
    } else {
        succeeded = __mirage_parser_toc_track_set_flag(self, MIRAGE_TRACKF_FOURCHANNEL, FALSE, error);
    }
    
    g_free(num);
    
    return succeeded;
}

static gboolean __mirage_parser_toc_callback_track_isrc (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gboolean succeeded = TRUE;
    gchar *isrc = g_match_info_fetch_named(match_info, "isrc");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed ISRC: %s\n", __debug__, isrc);
    
    succeeded = __mirage_parser_toc_track_set_isrc(self, isrc, error);
    
    g_free(isrc);
    
    return succeeded;
}

static gboolean __mirage_parser_toc_callback_track_index (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gchar *address_str = g_match_info_fetch_named(match_info, "address");
    gint address = mirage_helper_msf2lba_str(address_str, FALSE);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed INDEX: %s (0x%X)\n", __debug__, address_str, address);

    g_free(address_str);

    return __mirage_parser_toc_track_add_index(self, address, error);
}

static gboolean __mirage_parser_toc_callback_track_start (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gchar *address_str = g_match_info_fetch_named(match_info, "address");
    gint address;
    
    if (address_str) {
        address = mirage_helper_msf2lba_str(address_str, FALSE);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed START: %s (0x%X)\n", __debug__, address_str, address);
        g_free(address_str);
    } else {
        address = -1;
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed START: w/o address\n", __debug__);
    }
    
    return __mirage_parser_toc_track_set_start(self, address, error);
}

static gboolean __mirage_parser_toc_callback_track_pregap (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gchar *length_str = g_match_info_fetch_named(match_info, "length");
    gint length = mirage_helper_msf2lba_str(length_str, FALSE);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed PREGAP: %s (0x%X)\n", __debug__, length_str, length);
    
    g_free(length_str);
    
    if (!__mirage_parser_toc_track_add_fragment(self, TOC_DATA_TYPE_NONE, NULL, 0, 0, length, error)) {
        return FALSE;
    }
    
    return __mirage_parser_toc_track_set_start(self, -1, error);
}

static gboolean __mirage_parser_toc_callback_track_zero (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gchar *length_str = g_match_info_fetch_named(match_info, "length");
    gint length = mirage_helper_msf2lba_str(length_str, FALSE);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed ZERO: %s (0x%X)\n", __debug__, length_str, length);
    
    g_free(length_str);
    
    return __mirage_parser_toc_track_add_fragment(self, TOC_DATA_TYPE_NONE, NULL, 0, 0, length, error);
}

static gboolean __mirage_parser_toc_callback_track_silence (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gchar *length_str = g_match_info_fetch_named(match_info, "length");
    gint length = mirage_helper_msf2lba_str(length_str, FALSE);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed SILENCE: %s (0x%X)\n", __debug__, length_str, length);
    
    g_free(length_str);
    
    return __mirage_parser_toc_track_add_fragment(self, TOC_DATA_TYPE_NONE, NULL, 0, 0, length, error);
}

static gboolean __mirage_parser_toc_callback_track_audiofile (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gboolean succeeded = TRUE;
    gchar *filename, *base_offset_str, *start_str, *length_str;
    gint base_offset = 0;
    gint start = 0;
    gint length = 0;    
    
    /* Filename */
    filename = g_match_info_fetch_named(match_info, "filename");
    
    /* Base offset */
    base_offset_str = g_match_info_fetch_named(match_info, "base_offset");
    if (base_offset_str) {
        base_offset = atoi(base_offset_str);
    }
    
    /* Start; either as MSF or 0 */
    start_str = g_match_info_fetch_named(match_info, "start");
    if (start_str && strlen(start_str)) {
        start = mirage_helper_msf2lba_str(start_str, FALSE);
    } else {
        g_free(start_str);
        start_str = g_match_info_fetch_named(match_info, "start_num");
        start = atoi(start_str);
    }
    
    /* Length */
    length_str = g_match_info_fetch_named(match_info, "length");
    if (length_str) {
        length = mirage_helper_msf2lba_str(length_str, FALSE);
    }
 
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed AUDIOFILE: file: %s; base offset: %s; start: %s; length: %s\n", __debug__, filename, base_offset_str, start_str, length_str);
    
    succeeded = __mirage_parser_toc_track_add_fragment(self, TOC_DATA_TYPE_AUDIO, filename, base_offset, start, length, error);
    
    g_free(length_str);
    g_free(start_str);
    g_free(base_offset_str);
    g_free(filename);
    
    return succeeded;
}

static gboolean __mirage_parser_toc_callback_track_datafile (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    gboolean succeeded = TRUE;
    gchar *filename, *base_offset_str, *length_str;
    gint base_offset = 0;
    gint length = 0;    
    
    /* Filename */
    filename = g_match_info_fetch_named(match_info, "filename");
    
    /* Base offset */
    base_offset_str = g_match_info_fetch_named(match_info, "base_offset");
    if (base_offset_str) {
        base_offset = atoi(base_offset_str);
    }
        
    /* Length */
    length_str = g_match_info_fetch_named(match_info, "length");
    if (length_str) {
        length = mirage_helper_msf2lba_str(length_str, FALSE);
    }
 
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed DATAFILE: file: %s; base offset: %s; length: %s\n", __debug__, filename, base_offset_str, length_str);
    
    succeeded = __mirage_parser_toc_track_add_fragment(self, TOC_DATA_TYPE_DATA, filename, base_offset, 0, length, error);
    
    g_free(length_str);
    g_free(base_offset_str);
    g_free(filename);
    
    return succeeded;
}

#define APPEND_REGEX_RULE(list,rule,callback) {                         \
    MIRAGE_RegexRule *new_rule = g_new(MIRAGE_RegexRule, 1);            \
    new_rule->regex = g_regex_new(rule, G_REGEX_OPTIMIZE, 0, NULL);     \
    new_rule->callback_func = callback;                                 \
                                                                        \
    list = g_list_append(list, new_rule);                               \
}

static void __mirage_parser_toc_init_regex_parser (MIRAGE_Parser *self) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);

    /* Ignore empty lines */
    APPEND_REGEX_RULE(_priv->regex_rules, "^[\\s]*$", NULL);
    
    /* Comment */
    APPEND_REGEX_RULE(_priv->regex_rules, "^\\s*\\/{2}(?<comment>.+)$", __mirage_parser_toc_callback_comment);

    APPEND_REGEX_RULE(_priv->regex_rules, "^\\s*(?<type>(CD_DA|CD_ROM_XA|CD_ROM|CD_I))", __mirage_parser_toc_callback_session_type);
    /* Store pointer to header's regex rule */
    GList *elem_header = g_list_last(_priv->regex_rules);
    MIRAGE_RegexRule *rule_header = elem_header->data;
    _priv->regex_header_ptr = rule_header->regex;
    
    APPEND_REGEX_RULE(_priv->regex_rules, "^\\s*CATALOG\\s*\"(?<catalog>\\d{13,13})\"", __mirage_parser_toc_callback_catalog);

    APPEND_REGEX_RULE(_priv->regex_rules, "^\\s*TRACK\\s*(?<type>(AUDIO|MODE1_RAW|MODE1|MODE2_FORM1|MODE2_FORM2|MODE2_FORM_MIX|MODE2_RAW|MODE2))\\s*(?<subchan>(RW_RAW|RW))?", __mirage_parser_toc_callback_track);
    
    APPEND_REGEX_RULE(_priv->regex_rules, "^\\s*(?<no>NO)?\\s*COPY", __mirage_parser_toc_callback_track_flag_copy);
    APPEND_REGEX_RULE(_priv->regex_rules, "^\\s*(?<no>NO)?\\s*PRE_EMPHASIS", __mirage_parser_toc_callback_track_flag_preemphasis);
    APPEND_REGEX_RULE(_priv->regex_rules, "^\\s*(?<num>(TWO|FOUR))_CHANNEL_AUDIO", __mirage_parser_toc_callback_track_flag_channels);
    
    APPEND_REGEX_RULE(_priv->regex_rules, "^\\s*ISRC\\s*\"(?<isrc>[A-Z0-9]{5,5}[0-9]{7,7})\"", __mirage_parser_toc_callback_track_isrc);
    
    APPEND_REGEX_RULE(_priv->regex_rules, "^\\s*INDEX\\s*(?<address>\\d+:\\d+:\\d+)", __mirage_parser_toc_callback_track_index);

    APPEND_REGEX_RULE(_priv->regex_rules, "^\\s*START\\s*(?<address>\\d+:\\d+:\\d+)?", __mirage_parser_toc_callback_track_start);
    APPEND_REGEX_RULE(_priv->regex_rules, "^\\s*PREGAP\\s*(?<length>\\d+:\\d+:\\d+)", __mirage_parser_toc_callback_track_pregap);

    APPEND_REGEX_RULE(_priv->regex_rules, "^\\s*ZERO\\s*(?<length>\\d+:\\d+:\\d+)", __mirage_parser_toc_callback_track_zero);
    APPEND_REGEX_RULE(_priv->regex_rules, "^\\s*SILENCE\\s*(?<length>\\d+:\\d+:\\d+)", __mirage_parser_toc_callback_track_silence);

    APPEND_REGEX_RULE(_priv->regex_rules, "^\\s*(AUDIO)?FILE\\s*\"(?<filename>.+)\"\\s*(#(?<base_offset>\\d+))?\\s*((?<start>[\\d]+:[\\d]+:[\\d]+)|(?<start_num>\\d+))\\s*(?<length>[\\d]+:[\\d]+:[\\d]+)?", __mirage_parser_toc_callback_track_audiofile);
    APPEND_REGEX_RULE(_priv->regex_rules, "^\\s*DATAFILE\\s*\"(?<filename>.+)\"\\s*(#(?<base_offset>\\d+))?\\s*(?<length>[\\d]+:[\\d]+:[\\d]+)?", __mirage_parser_toc_callback_track_datafile);
    
    /* *** Special CD-TEXT block handling rules... *** */
    
    /* The one rule to match them all; matches the whole CD-TEXT block, and
       returns two (big) chunks of text; language maps and languages; both need
       to be further parsed by additional rules... (P.S.: there's groups everywhere
       because G_REGEX_MATCH_PARTIAL requires them) */       
    _priv->regex_cdtext = g_regex_new("CD_TEXT(\\s)*"
                                      "{(\\s)*"
                                      "("
                                       "LANGUAGE_MAP(\\s)*"
                                       "{(\\s)*"
                                      "(?<langmaps>((\\d)+([ \\t])*:([ \\t])*(\\w)+(\\s)*)+(\\s)*)"
                                       "}(\\s)*"
                                      ")?"
                                      "(?<languages>"
                                      "(LANGUAGE(\\s)*(\\d)+(\\s)*"
                                      "{(\\s)*"
                                       "("
                                        "("
                                         "((\\w)+( )*\"(.)*\"(\\s)*)" /* PACK_TYPE "DATA_STR" */
                                        "|"
                                         "((\\w)+( )*{([\\d,\\s])*}(\\s)*)" /* PACK_TYPE "DATA_STR" */
                                        ")"
                                       ")*" 
                                      "}(\\s)*)*"
                                      ")"
                                      "}", G_REGEX_OPTIMIZE|G_REGEX_MULTILINE, 0, NULL);
    
    /* Used for parsing language maps */
    _priv->regex_langmap = g_regex_new("\\s*(?<lang_idx>\\d+)[ \\t]*:[ \\t]*(?<lang_code>\\w+)\\s*", G_REGEX_OPTIMIZE, 0, NULL);
    
    /* Used for parsing languages */
    _priv->regex_language = g_regex_new("\\s*LANGUAGE\\s*(?<lang_idx>\\d+)\\s*"
                                        "{\\s*"
                                        "(?<lang_data>"
                                         "("
                                          "(\\w+[ \\t]*\".*\"\\s*)" /* PACK_TYPE "DATA_STR" */
                                         "|"
                                          "(\\w+[ \\t]*{[\\d,\\s]*}\\s*)" /* PACK_TYPE "DATA_STR" */
                                         ")*"
                                        ")" 
                                        "}\\s*",  G_REGEX_OPTIMIZE, 0, NULL);
    
    /* Used for parsing individual data fields */
    _priv->regex_langdata = g_regex_new("("
                                          "((?<type1>\\w+)[ \\t]*\"(?<data1>.*)\"\\s*)"
                                        "|"
                                          "((?<type2>\\w+)[ \\t]*{(?<data2>[\\d,\\s]*)}\\s*)"
                                        ")",  G_REGEX_OPTIMIZE, 0, NULL);
    
    /* Used for splitting binary data string */
    _priv->regex_binary = g_regex_new("\\s*,\\s*", G_REGEX_OPTIMIZE, 0, NULL);
    
    return;
}

static void __mirage_parser_toc_cleanup_regex_parser (MIRAGE_Parser *self) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);
    GList *entry;
    
    G_LIST_FOR_EACH(entry, _priv->regex_rules) {
        MIRAGE_RegexRule *rule = entry->data;
        g_regex_unref(rule->regex);
        g_free(rule);
    }
    
    g_list_free(_priv->regex_rules);
    
    /* CD-TEXT rules */
    g_regex_unref(_priv->regex_cdtext);
    g_regex_unref(_priv->regex_langmap);
    g_regex_unref(_priv->regex_language);
    g_regex_unref(_priv->regex_langdata);
    g_regex_unref(_priv->regex_binary);
}


static gboolean __mirage_parser_toc_parse_toc_file (MIRAGE_Parser *self, gchar *filename, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);
    GError *io_error = NULL;
    GIOChannel *io_channel;
    gboolean succeeded = TRUE;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: opening file: %s\n", __debug__, filename);
    
    /* Create IO channel for file */
    io_channel = g_io_channel_new_file(filename, "r", &io_error);
    if (!io_channel) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create IO channel: %s\n", __debug__, io_error->message);
        g_error_free(io_error);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }
    
    /* If provided, use the specified encoding; otherwise, use default (since 
       .toc file is linux-specific and should be fine with UTF-8 anyway) */
    gchar *encoding = NULL;
    if (mirage_parser_get_param_string(self, "encoding", (const gchar **)&encoding, NULL)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: using specified encoding: %s\n", __debug__, encoding);
        g_io_channel_set_encoding(io_channel, encoding, NULL);
    }
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing\n", __debug__);
    
    /* Couple of variables we'll need if we come across CDTEXT... */
    gboolean parsing_cdtext = FALSE;
    GString *cdtext_string = NULL;
        
    /* Read file line-by-line */
    gint line_nr;
    for (line_nr = 1; ; line_nr++) {
        GIOStatus status;
        gchar *line_str;
        gsize line_len;

        gboolean matched = FALSE;
        GMatchInfo *match_info = NULL;

        status = g_io_channel_read_line(io_channel, &line_str, &line_len, NULL, &io_error);
        
        /* Handle EOF */
        if (status == G_IO_STATUS_EOF) {
            break;
        }
        
        /* Handle abnormal status */
        if (status != G_IO_STATUS_NORMAL) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: status %d while reading line #%d from IO channel: %s\n", __debug__, status, line_nr, io_error ? io_error->message : "no error message");
            g_error_free(io_error);
            
            mirage_error(MIRAGE_E_IMAGEFILE, error);
            succeeded = FALSE;
            break;
        }
        
        /* If we're not in the middle of CD-TEXT parsing, use GRegex matching
           engine, otherwise do the custom stuff */
        if (!parsing_cdtext) {
            /* GRegex matching engine */
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
            
            /* Try to partially match CDTEXT; this one should *never* match in
               full, unless *everything* was in a single line... */
            if (!matched) {
                g_regex_match(_priv->regex_cdtext, line_str, G_REGEX_MATCH_PARTIAL, &match_info);
                if (g_match_info_is_partial_match(match_info)) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: partially matched CDTEXT; beginning CD-TEXT parsing\n", __debug__);
                    cdtext_string = g_string_new(line_str);
                    parsing_cdtext = TRUE;
                    matched = TRUE;
                }
                g_match_info_free(match_info);
            }
        } else {
            /* Append the line to CDTEXT string */
            g_string_append(cdtext_string, line_str);
            
            if (g_regex_match(_priv->regex_cdtext, cdtext_string->str, G_REGEX_MATCH_PARTIAL, &match_info)) {
                /* FIXME: can we live with failure? */
                __mirage_parser_toc_callback_cdtext(self, match_info, NULL);
                
                matched = TRUE;
                g_string_free(cdtext_string, TRUE);
                parsing_cdtext = FALSE;
            } else {
                if (g_match_info_is_partial_match(match_info)) {
                    matched = TRUE;
                } else {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: error while parsing CD-TEXT, expect trouble!\n", __debug__);
                    g_string_free(cdtext_string, TRUE);
                    parsing_cdtext = FALSE;
                }
            }
            
            g_match_info_free(match_info);
        }
              
        /* Complain if we failed to match the line (should it be fatal?) */
        if (!matched) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to match line #%d: %s\n", __debug__, line_nr, line_str);
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


static gboolean __check_toc_file (MIRAGE_Parser *self, const gchar *filename) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);
    gboolean succeeded = FALSE;

    /* Check suffix - must be .toc */
    if (!mirage_helper_has_suffix(filename, ".toc")) {
        return FALSE;
    }

    /* *** Additional check ***
       Because X-CD Roast also uses .toc for its images, we need to make
       sure this one was created by cdrdao... for that, we check for presence
       of CD_DA/CD_ROM_XA/CD_ROM/CD_I directive. */
    GIOChannel *io_channel;
        
    /* Create IO channel for file */
    io_channel = g_io_channel_new_file(filename, "r", NULL);
    if (!io_channel) {
        return FALSE;
    }
    
    /* If provided, use the specified encoding; otherwise, use default (since 
       .toc file is linux-specific and should be fine with UTF-8 anyway) */
    const gchar *encoding = NULL;
    if (mirage_parser_get_param_string(self, "encoding", &encoding, NULL)) {
        g_io_channel_set_encoding(io_channel, encoding, NULL);
    }
        
    /* Read file line-by-line */
    gint line_nr;
    for (line_nr = 1; ; line_nr++) {
        GIOStatus status;
        gchar *line_str;
        gsize line_len;

        GMatchInfo *match_info = NULL;

        status = g_io_channel_read_line(io_channel, &line_str, &line_len, NULL, NULL);
        
        /* Handle EOF */
        if (status == G_IO_STATUS_EOF) {
            break;
        }
        
        /* Handle abnormal status */
        if (status != G_IO_STATUS_NORMAL) {
            break;
        }
        
        /* Try to match the rule */
        if (g_regex_match(_priv->regex_header_ptr, line_str, 0, &match_info)) {
            /* Free match info */
            g_match_info_free(match_info);
            succeeded = TRUE;
        }
        
        g_free(line_str);
                
        /* If we found the header, break the loop */
        if (succeeded) {
            break;
        }
    }
    
    g_io_channel_unref(io_channel);
    
    return succeeded;
}

static gboolean __mirage_parser_toc_load_image (MIRAGE_Parser *self, gchar **filenames, GObject **disc, GError **error) {
    MIRAGE_Parser_TOCPrivate *_priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    gint i;
    
    /* Check if we can load file(s) */
    for (i = 0; i < g_strv_length(filenames); i++) {
        if (!__check_toc_file(self, filenames[i])) {
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
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: loading session #%i: '%s'!\n", __debug__, i, filenames[i]);
        __init_session_data(self);
        
        /* Store the TOC filename */
        g_free(_priv->toc_filename);
        _priv->toc_filename = g_strdup(filenames[i]);
        
        /* There's slight problem with multi-session TOC images, namely that each
           TOC can be used independently... in order words, there's no way to determine
           the length of leadouts for sessions (since all sessions start at sector 0).
           So we use what multisession FAQ from cdrecord docs tells us... */
        if (i > 0) {
            GObject *prev_session = NULL;
            gint leadout_length = 0;

            if (!mirage_disc_get_session_by_index(MIRAGE_DISC(_priv->disc), -1, &prev_session, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to get previous session!\n", __debug__);
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
                
        /* Create session */
        _priv->cur_session = NULL;
        if (!mirage_disc_add_session_by_index(MIRAGE_DISC(_priv->disc), -1, &_priv->cur_session, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to add session!\n", __debug__);
            succeeded = FALSE;
            goto end;
        }
        g_object_unref(_priv->cur_session); /* Don't keep reference */

        /* Parse TOC */
        succeeded = __mirage_parser_toc_parse_toc_file(self, filenames[i], error);
        
        /* Cleanup */
        __cleanup_session_data(self);
        
        /* If parsing failed, goto end */
        if (!succeeded) {
            goto end;
        }
    }
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finishing the layout\n", __debug__);
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
        "TOC files",
        "application/libmirage-toc"
    );
    
    __mirage_parser_toc_init_regex_parser(MIRAGE_PARSER(instance));
    
    return;
}

static void __mirage_parser_toc_finalize (GObject *obj) {
    MIRAGE_Parser_TOC *self = MIRAGE_PARSER_TOC(obj);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: finalizing object\n", __debug__);
    
    /* Cleanup regex parser engine */
    __mirage_parser_toc_cleanup_regex_parser(MIRAGE_PARSER(self));
    
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __debug__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __mirage_parser_toc_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_ParserClass *class_parser = MIRAGE_PARSER_CLASS(g_class);
    MIRAGE_Parser_TOCClass *klass = MIRAGE_PARSER_TOC_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Parser_TOCPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_parser_toc_finalize;
    
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
