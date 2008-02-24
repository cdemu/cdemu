/*
 *  libMirage: CUE image plugin lexer
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
 
/* We want reentrant lexer which we plan to use together with bison... */
%option reentrant

%option bison-bridge
%option bison-locations

/* Don't use these */
%option noyywrap
%option nounput

/* Use line numbers */
%option yylineno

/* For automake */
%option outfile="lex.yy.c"


/* Some general stuff */
COMMENT_        REM[^\n]*
NUMBER_         [0-9]+
WORD_           [A-Za-z_]+
STRING_         \"[^\n\"]*\"
MSF_            \"?[0-9]{1,3}:[0-9]{1,2}:[0-9]{1,2}\"?
/* Global tokens */
CATALOG_        CATALOG
CATALOG_DATA_   \"?[0-9]{13,13}\"?
CDTEXTFILE_     CDTEXTFILE
FILE_           FILE
FILE_TYPE_      BINARY|MOTOROLA|AIFF|WAVE|MP3
/* Track tokens */
TRACK_          TRACK
TRACK_TYPE_     AUDIO|CDG|MODE1\/2048|MODE1\/2352|MODE1\/2336|MODE2\/2336|MODE2\/2352|CDI\/2336|CDI\/2352
FLAGS_          FLAGS
FOURCH_         4CH
DCP_            DCP
PRE_            PRE
SCMS_           SCMS
INDEX_          INDEX
ISRC_           ISRC
ISRC_DATA_      \"?[A-Z0-9]{5,5}[0-9]{7,7}\"?
PREGAP_         PREGAP
POSTGAP_        POSTGAP

PERFORMER_      PERFORMER
SONGWRITER_     SONGWRITER
TITLE_          TITLE


%{
#include "image-cue.h"

#define YY_USER_ACTION yylloc->first_line = yylineno;
%}

%%

\n {
}

{CATALOG_} {
    return CATALOG_;
}

{CATALOG_DATA_} {
    if (yytext[0] == '\"') {
        yytext++;
        yytext[yyleng-2] = '\0';
    }
    yylval->string = g_strdup(yytext);
    return CATALOG_DATA_;
}

{CDTEXTFILE_} {
    return CDTEXTFILE_;
}

{FILE_} {
    return TFILE_;
}

{FILE_TYPE_} {
    yylval->string = g_strdup(yytext);    
    return FILE_TYPE_;
}

{TRACK_} {
    return TRACK_;
}

{TRACK_TYPE_} {
    yylval->string = g_strdup(yytext);        
    return TRACK_TYPE_;
}

{FLAGS_} {
    return FLAGS_;
}

{FOURCH_} {
    return FOURCH_;
}

{DCP_} {
    return DCP_;
}

{PRE_} {
    return PRE_;
}

{SCMS_} {
    return SCMS_;
}

{INDEX_} {
    return INDEX_;
}

{ISRC_} {
    return ISRC_;
}

{ISRC_DATA_} {
    if (yytext[0] == '\"') {
        yytext++;
        yytext[yyleng-2] = '\0';
    }
    yylval->string = g_strdup(yytext);
    return ISRC_DATA_;
}

{PREGAP_} {
    return PREGAP_;
}

{POSTGAP_} {
    return POSTGAP_;
}


{PERFORMER_} {
    return PERFORMER_;
}

{SONGWRITER_} {
    return SONGWRITER_;
}

{TITLE_} {
    return TITLE_;
}


{COMMENT_} {
}

{MSF_} {
    if (yytext[0] == '\"') {
        yytext++;
        yytext[yyleng-2] = '\0';
    }
    
    yylval->number = mirage_helper_msf2lba_str(yytext, FALSE);
    return NUMBER_;
}

{NUMBER_} {
    yylval->number = atoi(yytext);
    return NUMBER_;
}

{WORD_} {
    yylval->string = g_strdup(yytext);
    return WORD_;
}

{STRING_} {
    if (yytext[0] == '\"') {
        yytext++;
        yytext[yyleng-2] = '\0';
    }
    yylval->string = g_strdup(yytext);
    return STRING_;
}

. {
}

%%
