/*
 *  libMirage: CCD image plugin lexer
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
NUMBER_     \-?[0-9]+|0x[0-9a-f]+
WORD_       [A-Za-z_0-9]+

HEADER_     \[[a-zA-Z0-9 ]+\]
KEY_        [a-zA-Z0-9 ]+={NUMBER_}|[a-zA-Z0-9 ]+={WORD_}

%{
#include "image-ccd.h"

#define YY_USER_ACTION yylloc->first_line = yylineno;
%}

%%

{HEADER_} {
    if (!strcmp("[CloneCD]", yytext)) {
        return HEAD_CLONECD_;
    } else if (!strcmp("[Disc]", yytext)) {
        return HEAD_DISC_;
    } else {
        int value;
        if (sscanf(yytext, "[Session %i]", &value)) {
            yylval->number = value;
            return HEAD_SESSION_;
        } else if (sscanf(yytext, "[Entry %i]", &value)) {
            yylval->number = value;
            return HEAD_ENTRY_;
        } else if (sscanf(yytext, "[TRACK %i]", &value)) {
            yylval->number = value;
            return HEAD_TRACK_;
        }
    }
}

{KEY_}        {
    int value;
    char str[256] = "";
    /* from [CloneCD] */
    if (sscanf(yytext, "Version = %i", &value)) {
        yylval->number = value;
        return VERSION_;
    }
    /* from [Disc] */
    if (sscanf(yytext, "TocEntries = %i", &value)) {
        yylval->number = value;
        return TOCENTRIES_;
    } else if (sscanf(yytext, "Sessions = %i", &value)) {
        yylval->number = value;
        return SESSIONS_;
    } else if (sscanf(yytext, "DataTracksScrambled = %i", &value)) {
        yylval->number = value;
        return DATATRACKSSCRAMBLED_;
    } else if (sscanf(yytext, "CDTextLength = %i", &value)) {
        yylval->number = value;
        return CDTEXTLENGTH_;
    } else if (sscanf(yytext, "CATALOG = %s", str)) {
        yylval->string = g_strdup(str);
        return CATALOG_;
    }
    /* from [Session X] */
    if (sscanf(yytext, "PreGapMode = %i", &value)) {
        yylval->number = value;
        return PREGAPMODE_;
    } else if (sscanf(yytext, "PreGapSubC = %i", &value)) {
        yylval->number = value;
        return PREGAPSUBC_;
    }
    /* from [Entry X] */
    if (sscanf(yytext, "Session = %i", &value)) {
        yylval->number = value;
        return SESSION_;
    } else if (sscanf(yytext, "Point = %i", &value)) {
        yylval->number = value;
        return POINT_;
    } else if (sscanf(yytext, "ADR = %i", &value)) {
        yylval->number = value;
        return ADR_;
    } else if (sscanf(yytext, "Control = %i", &value)) {
        yylval->number = value;
        return CONTROL_;
    } else if (sscanf(yytext, "TrackNo = %i", &value)) {
        yylval->number = value;
        return TRACKNO_;
    } else if (sscanf(yytext, "AMin = %i", &value)) {
        yylval->number = value;
        return AMIN_;
    } else if (sscanf(yytext, "ASec = %i", &value)) {
        yylval->number = value;
        return ASEC_;
    } else if (sscanf(yytext, "AFrame = %i", &value)) {
        yylval->number = value;
        return AFRAME_;
    } else if (sscanf(yytext, "ALBA = %i", &value)) {
        yylval->number = value;
        return ALBA_;
    } else if (sscanf(yytext, "Zero = %i", &value)) {
        yylval->number = value;
        return ZERO_;
    } else if (sscanf(yytext, "PMin = %i", &value)) {
        yylval->number = value;
        return PMIN_;
    } else if (sscanf(yytext, "PSec = %i", &value)) {
        yylval->number = value;
        return PSEC_;
    } else if (sscanf(yytext, "PFrame = %i", &value)) {
        yylval->number = value;
        return PFRAME_;
    } else if (sscanf(yytext, "PLBA = %i", &value)) {
        yylval->number = value;
        return PLBA_;
    }
    /* from [TRACK X] */
    if (sscanf(yytext, "MODE = %i", &value)) {
        yylval->number = value;
        return MODE_;
    } else if (sscanf(yytext, "INDEX 0 = %i", &value)) {
        yylval->number = value;
        return INDEX0_;
    } else if (sscanf(yytext, "INDEX 1 = %i", &value)) {
        yylval->number = value;
        return INDEX1_;
    } else if (sscanf(yytext, "ISRC=%s", str)) {
        yylval->string = g_strdup(str);
        return ISRC_;
    }
}

\n {
}

. {
}

%%
