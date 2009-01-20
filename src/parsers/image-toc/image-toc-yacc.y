/*
 *  libMirage: TOC image plugin parser
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

%{
#include "image-toc.h"

/* Prototypes */
gint yyerror (YYLTYPE *locp, void *scanner, MIRAGE_Parser *self, GError **error, const gchar *msg);
gint yylex (YYSTYPE* yylval_param, YYLTYPE* yylloc_param, void *yyscanner);

/* Error function */
gint yyerror (YYLTYPE *locp, void *scanner, MIRAGE_Parser *self, GError **error, const gchar *msg) {
    g_warning("Parse error in line %i: %s\n", locp->first_line, msg);
    g_set_error(error, MIRAGE_ERROR, MIRAGE_E_PARSER, "Parse error in line %i: %s\n", locp->first_line, msg);
    return 0;
}

#define YYLEX_PARAM scanner
%}

/* Parser parameters */
%parse-param {void *scanner}
%parse-param {MIRAGE_Parser *self}
%parse-param {GError **error}

%union {
    int number;
    char *string;
}

%locations
%pure_parser
%error-verbose


/* Tokens */
%token  <string>  STRING_
%token  <string>  WORD_
%token  <number>  NUMBER_

%token            COMMA_
%token            LBRACE_
%token            RBRACE_
%token            COLON_
%token            HASH_

%token  <string>  SESSION_TYPE_
/* Catalog */
%token            CATALOG_
%token  <string>  CATALOG_DATA_
/* Track */
%token            TRACK_
%token  <string>  TRACK_MODE_
%token  <string>  SUBCHAN_MODE_

%token            NO_
%token            COPY_
%token            PRE_EMPHASIS_
%token            TWO_CHANNEL_
%token            FOUR_CHANNEL_

%token            ISRC_
%token  <string>  ISRC_DATA_

%token            START_

%token            PREGAP_

%token            SILENCE_
%token            ZERO_
%token            AUDIO_FILE_
%token            DATA_FILE_

%token            INDEX_

%token            CD_TEXT_
%token            LANGUAGE_MAP_

%token            LANGUAGE_

%token  <number>  CDTEXT_PACK_

%start toc_file

%%

toc_file        :   toc_elements;

toc_elements    :   /* Empty */
                |   toc_elements toc_element;

toc_element     :   toc_header;
                |   catalog;
                |   cdtext_g;
                |   track_element;

toc_header      :   SESSION_TYPE_ {
                        gboolean succeeded = __mirage_parser_toc_set_session_type(self, $1, error);
                        g_free($1); /* Free SESSION_TYPE_ */
                        if (!succeeded) {
                            return -1;
                        }
                    }
                    
catalog         :   CATALOG_ CATALOG_DATA_ { 
                        gboolean succeeded = __mirage_parser_toc_set_mcn(self, $2, error);
                        g_free($2); /* Free CATALOG_DATA_ */
                        if (!succeeded) {
                            return -1;
                        }
                    }

track_element   :   track_header;
                |   track_flag;
                |   track_fragment;
                |   track_index;
                |   track_cdtext;

                    /* TRACK track-mode subchan-mode */
track_header    :   TRACK_ TRACK_MODE_ SUBCHAN_MODE_ { 
                        gboolean succeeded = __mirage_parser_toc_add_track(self, $2, $3, error); 
                        g_free($2); /* Free TRACK_MODE_ */
                        g_free($3); /* Free SUBCHAN_MODE_ */
                        if (!succeeded) {
                            return -1;
                        }
                    }
                    /* TRACK track-mode */
                |   TRACK_ TRACK_MODE_ {
                        gboolean succeeded = __mirage_parser_toc_add_track(self, $2, NULL, error);
                        g_free($2); /* Free TRACK_MODE_ */
                        if (!succeeded) {
                            return -1;
                        }
                    }

track_flag      :   NO_ COPY_ {
                        if (!__mirage_parser_toc_set_flag(self, MIRAGE_TRACKF_COPYPERMITTED, 0, error)) {
                            return -1;
                        }
                    }
                |   COPY_ {
                        if (!__mirage_parser_toc_set_flag(self, MIRAGE_TRACKF_COPYPERMITTED, 1, error)) {
                            return -1;
                        }
                    }
                |   NO_ PRE_EMPHASIS_ {
                        if (!__mirage_parser_toc_set_flag(self, MIRAGE_TRACKF_PREEMPHASIS, 0, error)) {
                            return -1;
                        }
                    }
                |   PRE_EMPHASIS_ {
                        if (!__mirage_parser_toc_set_flag(self, MIRAGE_TRACKF_PREEMPHASIS, 1, error)) {
                            return -1;
                        }
                    }
                |   FOUR_CHANNEL_ {
                        if (!__mirage_parser_toc_set_flag(self, MIRAGE_TRACKF_FOURCHANNEL, 1, error)) {
                            return -1;
                        }
                    }
                |   TWO_CHANNEL_ {
                        if (!__mirage_parser_toc_set_flag(self, MIRAGE_TRACKF_FOURCHANNEL, 0, error)) {
                            return -1;
                        }
                    }
                |   ISRC_ ISRC_DATA_ {
                        gboolean succeeded = __mirage_parser_toc_set_isrc(self, $2, error);
                        g_free($2); /* Free ISRC_DATA_ */
                        if (!succeeded) {
                            return -1;
                        }
                    }
                
                
                    /* ZERO length */
track_fragment  :   ZERO_ NUMBER_ {
                        if (!__mirage_parser_toc_add_track_fragment(self, TOC_DATA_TYPE_NONE, NULL, 0, 0, $2, error)) {
                            return -1;
                        }
                    }
                    /* SILENCE length */
                |   SILENCE_ NUMBER_ {
                        if (!__mirage_parser_toc_add_track_fragment(self, TOC_DATA_TYPE_NONE, NULL, 0, 0, $2, error)) {
                            return -1;
                        }
                    }
                    /* (AUDIO)FILE filename #base_offset start length */
                |   AUDIO_FILE_ STRING_ HASH_ NUMBER_ NUMBER_ NUMBER_ {
                        if (!__mirage_parser_toc_add_track_fragment(self, TOC_DATA_TYPE_AUDIO, $2, $4, $5, $6, error)) {
                            return -1;
                        }
                    }
                    /* (AUDIO)FILE filename start length */
                |   AUDIO_FILE_ STRING_ NUMBER_ NUMBER_ {
                        if (!__mirage_parser_toc_add_track_fragment(self, TOC_DATA_TYPE_AUDIO, $2, 0, $3, $4, error)) {
                            return -1;
                        }
                    }
                    /* (AUDIO)FILE filename start */
                |   AUDIO_FILE_ STRING_ NUMBER_ {
                        if (!__mirage_parser_toc_add_track_fragment(self, TOC_DATA_TYPE_AUDIO, $2, 0, $3, 0, error)) {
                            return -1;
                        }
                    }
                    /* DATAFILE filename length */
                |   DATA_FILE_ STRING_ NUMBER_ {
                        if (!__mirage_parser_toc_add_track_fragment(self, TOC_DATA_TYPE_DATA, $2, 0, 0, $3, error)) {
                            return -1;
                        }
                    }
                    /* DATAFILE filename #base_offset length */
                |   DATA_FILE_ STRING_ HASH_ NUMBER_ NUMBER_  {
                        if (!__mirage_parser_toc_add_track_fragment(self, TOC_DATA_TYPE_DATA, $2, $4, 0, $5, error)) {
                            return -1;
                        }
                    }
                    /* DATAFILE filename #base_offset */
                |   DATA_FILE_ STRING_ HASH_ NUMBER_  {
                        if (!__mirage_parser_toc_add_track_fragment(self, TOC_DATA_TYPE_DATA, $2, $4, 0, 0, error)) {
                            return -1;
                        }
                    }
                    /* DATAFILE filename */
                |   DATA_FILE_ STRING_ {
                        if (!__mirage_parser_toc_add_track_fragment(self, TOC_DATA_TYPE_DATA, $2, 0, 0, 0, error)) {
                            return -1;
                        }
                    }
                    /* START address */
                |   START_ NUMBER_ {
                        if (!__mirage_parser_toc_set_track_start(self, $2, error)) {
                            return -1;
                        }
                    }
                    /* START */
                |   START_ {
                        if (!__mirage_parser_toc_set_track_start(self, -1, error)) {
                            return -1;
                        }
                    }
                    /* PREGAP length */
                |   PREGAP_ NUMBER_ {
                        /* equivalent to:
                            SILENCE length
                            START
                        */
                        if (!__mirage_parser_toc_add_track_fragment(self, TOC_DATA_TYPE_NONE, NULL, 0, 0, $2, error)) {
                            return -1;
                        }
                        if (!__mirage_parser_toc_set_track_start(self, -1, error)) {
                            return -1;
                        }
                    }
                    
track_index     :   INDEX_ NUMBER_ {
                        if (!__mirage_parser_toc_add_index(self, $2, error)) {
                            return -1;
                        }
                    }
                    
cdtext_g        :   CD_TEXT_ LBRACE_ {
                    } langmap languages_g RBRACE_;

langmap         :   LANGUAGE_MAP_ LBRACE_ {} mappings RBRACE_ 

mappings        :   /* Empty */
                |   mappings mapping;

mapping         :   NUMBER_ COLON_ NUMBER_ {
                        __mirage_parser_toc_add_language_mapping(self, $1, $3, error);
                    }
                |   NUMBER_ COLON_ WORD_ {
                        /* We need to decode strings like EN */
                        static struct {
                            gchar *code_str;
                            gint code_num;
                        } codes[] = {
                            { "EN", 9 },
                        };
                        gint i;
                        
                        for (i = 0; i < G_N_ELEMENTS(codes); i++) {
                            if (!strcmp($3, codes[i].code_str)) {
                                __mirage_parser_toc_add_language_mapping(self, $1, codes[i].code_num, error);
                                break;
                            }
                        }
                        
                        g_free($3); /* Free WORD_ */
                    }
languages_g     :   /* Empty */
                |   languages_g language_g

language_g      :   LANGUAGE_ NUMBER_ LBRACE_ {
                        /* Create language */
                        __mirage_parser_toc_add_g_laguage(self, $2, NULL);
                    } cdtext_g_elements RBRACE_;

cdtext_binary_data  :   NUMBER_ COMMA_ cdtext_binary_data;
                    |   NUMBER_;

cdtext_g_elements   :   /* Empty */
                    |   cdtext_g_elements cdtext_g_element;
                    
cdtext_g_element    :   CDTEXT_PACK_ STRING_ {
                            __mirage_parser_toc_set_g_cdtext_data(self, $1, $2, NULL);
                            g_free($2); /* Free STRING_ */
                        }
                    |   CDTEXT_PACK_ LBRACE_ cdtext_binary_data RBRACE_ {
                            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: skipping binary global CD-Text element 0x%X\n", __func__, $1);
                        }

track_cdtext        :   CD_TEXT_ LBRACE_ { 
                        } languages_t RBRACE_;

languages_t         :   /* Empty */
                    |   languages_t language_t;

language_t          :   LANGUAGE_ NUMBER_ LBRACE_ {
                            /* Create language */
                            __mirage_parser_toc_add_t_laguage(self, $2, NULL);
                        } cdtext_t_elements RBRACE_;

cdtext_t_elements   :   /* Empty */
                    |   cdtext_t_elements cdtext_t_element;
                    
cdtext_t_element    :   CDTEXT_PACK_ STRING_ {
                            __mirage_parser_toc_set_t_cdtext_data(self, $1, $2, NULL);
                            g_free($2); /* Free STRING_ */
                        }
                    |   CDTEXT_PACK_ LBRACE_ cdtext_binary_data RBRACE_ {
                            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: skipping binary track CD-Text element 0x%X\n", __func__, $1);
                        }
                    |   ISRC_ STRING_ {
                            __mirage_parser_toc_set_t_cdtext_data(self, MIRAGE_LANGUAGE_PACK_UPC_ISRC, $2, NULL);
                            g_free($2); /* Free STRING_ */
                        }
%%
