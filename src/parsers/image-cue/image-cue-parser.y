/*
 *  libMirage: CUE image plugin parser
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
#include "image-cue.h"

/* Prototypes */
gint yyerror (YYLTYPE *locp, void *scanner, MIRAGE_Disc *self, GError **error, const gchar *msg);
gint yylex (YYSTYPE* yylval_param, YYLTYPE* yylloc_param, void *yyscanner);

/* Error function */
gint yyerror (YYLTYPE *locp, void *scanner, MIRAGE_Disc *self, GError **error, const gchar *msg) {
    g_warning("Parse error in line %i: %s\n", locp->first_line, msg);
    g_set_error(error, MIRAGE_ERROR, MIRAGE_E_PARSER, "Parse error in line %i: %s\n", locp->first_line, msg);
    return 0;
}

#define YYLEX_PARAM scanner
%}

/* parser parameters */
%parse-param {void *scanner}
%parse-param {MIRAGE_Disc *self}
%parse-param {GError **error}

%union {
    int number;
    char *string;
}

%locations
%pure_parser
%error-verbose


/* Tokens */
%token  <number>  NUMBER_
%token  <string>  WORD_
%token  <string>  STRING_
/* Global tokens */
%token            CATALOG_
%token  <string>  CATALOG_DATA_
%token            CDTEXTFILE_
%token            TFILE_
%token  <string>  FILE_TYPE_
/* Track tokens */
%token            TRACK_
%token  <string>  TRACK_TYPE_
%token            FLAGS_
%token            FOURCH_
%token            DCP_
%token            PRE_
%token            SCMS_
%token            INDEX_
%token            ISRC_
%token  <string>  ISRC_DATA_
%token            PREGAP_
%token            POSTGAP_

%token            PERFORMER_
%token            SONGWRITER_
%token            TITLE_

%token  <number>  SESSION_

%start cue_file

%%

cue_file        :   cue_elements body;
                |   body;

cue_elements    :   cue_element;
                |   cue_elements cue_element;

cue_element     :   catalog;
                |   cdtextfile;
                |   performer_g;
                |   songwriter_g;
                |   title_g;

catalog         :   CATALOG_ CATALOG_DATA_ {
                        gboolean succeeded = __mirage_disc_cue_set_mcn(self, $2, error);
                        g_free($2); /* Free CATALOG_DATA_ */
                        if (!succeeded) {
                            return -1;
                        }
                    };
file            :   TFILE_ STRING_ FILE_TYPE_ {
                        gboolean succeeded = TRUE;
                        succeeded = __mirage_disc_cue_set_new_file(self, $2, $3, error);
                        g_free($2); /* Free STRING_ */
                        g_free($3); /* Free FILE_TYPE_ */
                        if (!succeeded) {
                            return -1;
                        }
                    }
cdtextfile      :   CDTEXTFILE_ STRING_ { g_free($2); /* Free STRING_ */ };
performer_g     :   PERFORMER_ STRING_ { g_free($2); /* Free STRING_ */ };
songwriter_g    :   SONGWRITER_ STRING_ { g_free($2); /* Free STRING_ */ };
title_g         :   TITLE_ STRING_ { g_free($2); /* Free STRING_ */};


session         :   SESSION_ {
                        if (!__mirage_disc_cue_add_session(self, $1, error)) {
                            return -1;
                        }
                }

body            :   track;
                |   session;
                |   file;
                |   body session;
                |   body file;
                |   body track;
                
track           :   track_header track_elements

track_elements  :   track_element
                |   track_elements track_element

track_element   :   track_index;
                |   track_flags;
                |   track_isrc;    
                |   performer_t;
                |   songwriter_t;
                |   title_t;
                |   track_empty_part;

track_header    :   TRACK_ NUMBER_ TRACK_TYPE_ {
                        gboolean succeeded = __mirage_disc_cue_add_track(self, $2, $3, error);
                        g_free($3); /* Free TRACK_TYPE_ */
                        if (!succeeded) {
                            return -1;
                        }
                    }
track_index     :   INDEX_ NUMBER_ NUMBER_ {
                        if (!__mirage_disc_cue_add_index(self, $2, $3, error)) {
                            return -1;
                        }
                    }
                    
track_flags     :   FLAGS_ track_flag_elements
track_flag_elements :   track_flag_element
                    |   track_flag_elements track_flag_element
track_flag_element  :   FOURCH_ {
                            if (!__mirage_disc_cue_set_flag(self, MIRAGE_TRACKF_FOURCHANNEL, error)) {
                                return -1;
                            }
                        }
                    |   DCP_ {
                            if (!__mirage_disc_cue_set_flag(self, MIRAGE_TRACKF_COPYPERMITTED, error)) {
                                return -1;
                            }
                        }
                    |   PRE_ {
                            if (!__mirage_disc_cue_set_flag(self, MIRAGE_TRACKF_PREEMPHASIS, error)) {
                                return -1;
                            }
                        }
                    |   SCMS_ {}
track_isrc      :   ISRC_ ISRC_DATA_ {
                        gboolean succeeded = __mirage_disc_cue_set_isrc(self, $2, error);
                        g_free($2); /* Free ISRC_DATA_ */
                        if (!succeeded) {
                            return -1;
                        }
                    }
performer_t     :   PERFORMER_ STRING_ { g_free($2); /* Free STRING_ */ }

songwriter_t    :   SONGWRITER_ STRING_ { g_free($2); /* Free STRING_ */ }
title_t         :   TITLE_ STRING_ { g_free($2); /* Free STRING_ */ }

track_empty_part:   PREGAP_ NUMBER_ {
                        /* Pregap takes some more work than postgap... */
                        if (!__mirage_disc_cue_add_pregap(self, $2, error)) {
                            return -1;
                        }
                    }
                |   POSTGAP_ NUMBER_ {
                        if (!__mirage_disc_cue_add_empty_part(self, $2, error)) {
                            return -1;
                        }
                    }

%%
