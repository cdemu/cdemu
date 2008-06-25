/*
 *  libMirage: CCD image plugin parser
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
#include "image-ccd.h"

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

/* Parser parameters */
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
%token            HEAD_CLONECD_
%token  <number>  VERSION_

%token            HEAD_DISC_
%token  <number>  TOCENTRIES_
%token  <number>  SESSIONS_
%token  <number>  DATATRACKSSCRAMBLED_
%token  <number>  CDTEXTLENGTH_

%token  <number>  HEAD_SESSION_
%token  <number>  PREGAPMODE_
%token  <number>  PREGAPSUBC_

%token  <number>  HEAD_ENTRY_
%token  <number>  SESSION_
%token  <number>  POINT_
%token  <number>  ADR_
%token  <number>  CONTROL_
%token  <number>  TRACKNO_
%token  <number>  AMIN_
%token  <number>  ASEC_
%token  <number>  AFRAME_
%token  <number>  ALBA_
%token  <number>  ZERO_
%token  <number>  PMIN_
%token  <number>  PSEC_
%token  <number>  PFRAME_
%token  <number>  PLBA_

%token  <number>  HEAD_TRACK_
%token  <number>  MODE_
%token  <number>  INDEX0_
%token  <number>  INDEX1_
%token  <string>  ISRC_

%start ccd_file

%%

ccd_file        :   ccd_elements;

ccd_elements    :   ccd_element;
                |   ccd_elements ccd_element;

ccd_element     :   clonecd_section;
                |   disc_section;
                |   session_section;
                |   entry_section;
                |   track_section;

clonecd_section :   HEAD_CLONECD_ VERSION_ { /* nothing to be done at the moment */ }
                    
disc_section    :   HEAD_DISC_ TOCENTRIES_ SESSIONS_ DATATRACKSSCRAMBLED_ CDTEXTLENGTH_ {
                        if (!__mirage_disc_ccd_decode_disc_section(self, $3, error)) {
                            return -1;
                        }
                    }
                    
session_section :   HEAD_SESSION_ PREGAPMODE_ PREGAPSUBC_ { /* nothing to be done at the moment */}
                    
entry_section   :   HEAD_ENTRY_ SESSION_ POINT_ ADR_ CONTROL_ TRACKNO_ 
                    AMIN_ ASEC_ AFRAME_ ALBA_ ZERO_ PMIN_ PSEC_ PFRAME_ 
                    PLBA_ {
                        if (!__mirage_disc_ccd_decode_entry_section(self, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, error)) {
                            return -1;
                        }
                    }

track_section   :   HEAD_TRACK_ {
                        if (!__mirage_disc_ccd_set_current_track(self, $1, error)) {
                            return -1;
                        }
                    } track_elements

track_elements  :   track_element
                |   track_elements track_element

track_element   :   track_mode;
                |   track_index0;
                |   track_index1;
                |   track_isrc;

track_mode      :   MODE_ { /* Nothing to do here */ }
track_index0    :   INDEX0_ { /* Nothing to do here */ }
track_index1    :   INDEX1_ { /* Nothing to do here */ }

track_isrc      :   ISRC_ {
                        gboolean succeeded = __mirage_disc_ccd_track_set_isrc(self, $1, error);
                        g_free($1); /* Free ISRC_ */
                        if (!succeeded) {
                            return -1;
                        }
                    }

%%
