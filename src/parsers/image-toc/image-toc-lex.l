/*
 *  libMirage: TOC image plugin lexer
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
%option noinput
%option nounput

/* Use line numbers */
%option yylineno

/* For automake */
%option outfile="lex.yy.c"


/* Some general stuff */
COMMENT_            \/{2}[^\n]*
HASH_               #
LBRACE_             \{
RBRACE_             \}
COLON_              :
NUMBER_             [0-9]+
WORD_               [A-Za-z_]+
STRING_             \"[^\n\"]*\"
MSF_                \"?[0-9]{1,3}:[0-9]{1,2}:[0-9]{1,2}\"?

/* Global tokens */
SESSION_TYPE_       CD_DA|CD_ROM|CD_ROM_XA|CD_I
CATALOG_            CATALOG
CATALOG_DATA_       \"[0-9]{13,13}\"

/* Track tokens */
TRACK_              TRACK
TRACK_MODE_         AUDIO|MODE1|MODE1_RAW|MODE2|MODE2_FORM1|MODE2_FORM2|MODE2_FORM_MIX|MODE2_RAW
SUBCHAN_MODE_       RW_RAW|RW
NO_                 NO
COPY_               COPY
PRE_EMPHASIS_       PRE_EMPHASIS
TWO_CHANNEL_        TWO_CHANNEL_AUDIO
FOUR_CHANNEL_       FOUR_CHANNEL_AUDIO
ISRC_               ISRC
ISRC_DATA_          \"[A-Z0-9]{5,5}[0-9]{7,7}\"
SILENCE_            SILENCE
ZERO_               ZERO
AUDIO_FILE_         FILE|AUDIOFILE
DATA_FILE_          DATAFILE
START_              START
PREGAP_             PREGAP
INDEX_              INDEX

/* CD-TEXT tokens */
CD_TEXT_            CD_TEXT
LANGUAGE_MAP_       LANGUAGE_MAP
LANGUAGE_           LANGUAGE

/* CD-TEXT pack tokens; note that ISRC has been declared before */
CDTEXT_PACK_        TITLE|PERFORMER|SONGWRITER|COMPOSER|ARRANGER|MESSAGE|DISC_ID|GENRE|TOC_INFO1|TOC_INFO2|UPC_EAN|SIZE_INFO
       
%{
#include "image-toc.h"

#define YY_USER_ACTION yylloc->first_line = yylineno;

int yyget_column (yyscan_t yyscanner);
void yyset_column (int column_no, yyscan_t yyscanner);
%}

%%

\n {
}

{COMMENT_} {
}

{HASH_} {
    return HASH_;
}

{LBRACE_} {
    return LBRACE_;
}

{RBRACE_} {
    return RBRACE_;
}

{SESSION_TYPE_} {
    yylval->string = g_strdup(yytext);
    return SESSION_TYPE_;
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

{TRACK_} {
    return TRACK_;
}

{TRACK_MODE_} {
    yylval->string = g_strdup(yytext);    
    return TRACK_MODE_;
}

{SUBCHAN_MODE_} {
    yylval->string = g_strdup(yytext);
    return SUBCHAN_MODE_;
}

{NO_} {
    return NO_;
}

{COPY_} {
    return COPY_;
}

{PRE_EMPHASIS_} {
    return PRE_EMPHASIS_;
}

{FOUR_CHANNEL_} {
    return FOUR_CHANNEL_;
}

{TWO_CHANNEL_} {
    return TWO_CHANNEL_;
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

{SILENCE_} {
    return SILENCE_;
}

{ZERO_} {
    return ZERO_;
}

{START_} {
    return START_;
}

{AUDIO_FILE_} {
    return AUDIO_FILE_;
}

{DATA_FILE_} {
    return DATA_FILE_;
}

{CD_TEXT_} {
    return CD_TEXT_;
}

{LANGUAGE_MAP_} {
    return LANGUAGE_MAP_;
}

{LANGUAGE_} {
    return LANGUAGE_;
}

{CDTEXT_PACK_} {
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
    
    for (i = 0; i < G_N_ELEMENTS(packs); i++) {
        if (!strcmp(yytext, packs[i].pack_id)) {
            yylval->number = packs[i].pack_type;
            return CDTEXT_PACK_;
        }
    }
    
    /* Otherwise return nothing */
}


{INDEX_} {
    return INDEX_;
}

{MSF_} {
    if (yytext[0] == '\"') {
        yytext++;
        yytext[yyleng-2] = '\0';
    }
    
    yylval->number = mirage_helper_msf2lba_str(yytext, FALSE);
    return NUMBER_;
}

{COLON_} {
    return COLON_;
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
