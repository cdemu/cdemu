/*
 *  libMirage: TOC image parser: Parser object
 *  Copyright (C) 2006-2012 Rok Mandeljc
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


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_PARSER_TOC_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_TOC, MIRAGE_Parser_TOCPrivate))

struct _MIRAGE_Parser_TOCPrivate
{
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
};


enum
{
    TOC_DATA_TYPE_NONE,
    TOC_DATA_TYPE_AUDIO,
    TOC_DATA_TYPE_DATA,
};


/**********************************************************************\
 *                     Parser private functions                       *
\**********************************************************************/
static void mirage_parser_toc_set_session_type (MIRAGE_Parser_TOC *self, gchar *type_string)
{
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
            mirage_session_set_session_type(MIRAGE_SESSION(self->priv->cur_session), session_types[i].type);
            break;
        }
    }
}

static void mirage_parser_toc_add_track (MIRAGE_Parser_TOC *self, gchar *mode_string, gchar *subchan_string)
{
    /* Add track */
    self->priv->cur_track = NULL;
    mirage_session_add_track_by_index(MIRAGE_SESSION(self->priv->cur_session), -1, &self->priv->cur_track);
    g_object_unref(self->priv->cur_track); /* Don't keep reference */

    /* Clear internal data */
    self->priv->cur_tfile_sectsize = 0;
    self->priv->cur_sfile_sectsize = 0;
    self->priv->cur_sfile_format = 0;

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
            mirage_track_set_mode(MIRAGE_TRACK(self->priv->cur_track), track_modes[i].mode);
            /* Store sector size */
            self->priv->cur_tfile_sectsize = track_modes[i].sectsize;

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
                self->priv->cur_sfile_sectsize = subchan_modes[i].sectsize;
                self->priv->cur_sfile_format = subchan_modes[i].format;
                break;
            }
        }
    }
};


static gboolean mirage_parser_toc_track_add_fragment (MIRAGE_Parser_TOC *self, gint type, gchar *filename_string, gint base_offset, gint start, gint length, GError **error)
{
    GObject *data_fragment;

    /* Create appropriate fragment */
    if (type == TOC_DATA_TYPE_NONE) {
        /* Empty fragment; we'd like a NULL fragment - creation should never fail */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating NULL fragment\n", __debug__);
        data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FRAG_IFACE_NULL, NULL, G_OBJECT(self), NULL);
    } else {
        /* Find filename */
        gchar *filename = mirage_helper_find_data_file(filename_string, self->priv->toc_filename);
        if (!filename) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to find data file!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DATA_FILE_ERROR, "Failed to find data file!");
            return FALSE;
        }
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: using data file: %s\n", __debug__, filename);

        /* Create strean */
        GObject *stream = mirage_parser_get_cached_data_stream(MIRAGE_PARSER(self), filename_string, error);
        if (!stream) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create stream on data file!\n", __debug__);
            return FALSE;
        }

        /* BINARY can be either explicitly requested; or it can be assumed from
           *.bin suffix (with TOC_DATA_TYPE_AUDIO), which is a bit hacky, but
           should work for now... */
        if (type == TOC_DATA_TYPE_DATA || mirage_helper_has_suffix(filename_string, ".bin")) {
            /* Binary data; we'd like a BINARY fragment */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating BINARY fragment\n", __debug__);
            data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FRAG_IFACE_BINARY, stream, G_OBJECT(self), error);
            if (!data_fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create BINARY fragment!\n", __debug__);
                g_free(filename);
                g_object_unref(stream);
                return FALSE;
            }

            gint tfile_sectsize;
            gint tfile_format;
            guint64 tfile_offset;

            gint sfile_format;
            gint sfile_sectsize;

            /* Track file */
            tfile_sectsize = self->priv->cur_tfile_sectsize;

            /* If we're dealing with BINARY AUDIO data, we need to swap it...
               (type == TOC_DATA_TYPE_AUDIO) is not sufficient check, because
               apparently when .bin file contains subchannel data, it automatically
               gets listed as DATAFILE (hence type = TOC_DATA_TYPE_DATA); thus,
               we simply check whether we have an audio track or not... */
            gint mode = mirage_track_get_mode(MIRAGE_TRACK(self->priv->cur_track));
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
                if (!self->priv->mixed_mode_bin || mirage_helper_strcasecmp(self->priv->mixed_mode_bin, filename)) {
                    self->priv->mixed_mode_offset = 0;
                    g_free(self->priv->mixed_mode_bin);
                    self->priv->mixed_mode_bin = g_strdup(filename);
                }

                base_offset = self->priv->mixed_mode_offset;

                /* I guess it's safe to calculate this here; if length isn't
                   provided, it means whole file is used, so most likely we'll
                   get file changed next time we're called...*/
                if (type == TOC_DATA_TYPE_DATA) {
                    /* Increase only if it's data... */
                    self->priv->mixed_mode_offset += length * (self->priv->cur_tfile_sectsize + self->priv->cur_sfile_sectsize);
                }
            }

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: using base offset: 0x%lX\n", __debug__, base_offset);
            tfile_offset = base_offset + start * (self->priv->cur_tfile_sectsize + self->priv->cur_sfile_sectsize);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: calculated track file offset: 0x%llX\n", __debug__, tfile_offset);

            /* Subchannel file */
            sfile_sectsize = self->priv->cur_sfile_sectsize;
            sfile_format = self->priv->cur_sfile_format;

            /* Set file */
            if (!mirage_frag_iface_binary_track_file_set_file(MIRAGE_FRAG_IFACE_BINARY(data_fragment), filename, stream, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set track data file!\n", __debug__);
                g_free(filename);
                g_object_unref(stream);
                g_object_unref(data_fragment);
                return FALSE;
            }
            mirage_frag_iface_binary_track_file_set_sectsize(MIRAGE_FRAG_IFACE_BINARY(data_fragment), tfile_sectsize);
            mirage_frag_iface_binary_track_file_set_offset(MIRAGE_FRAG_IFACE_BINARY(data_fragment), tfile_offset);
            mirage_frag_iface_binary_track_file_set_format(MIRAGE_FRAG_IFACE_BINARY(data_fragment), tfile_format);

            mirage_frag_iface_binary_subchannel_file_set_sectsize(MIRAGE_FRAG_IFACE_BINARY(data_fragment), sfile_sectsize);
            mirage_frag_iface_binary_subchannel_file_set_format(MIRAGE_FRAG_IFACE_BINARY(data_fragment), sfile_format);
        } else {
            /* Audio data; we'd like an AUDIO fragment, and hopefully Mirage can
               find one that can handle given file format */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating AUDIO fragment\n", __debug__);
            data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FRAG_IFACE_AUDIO, stream, G_OBJECT(self), error);
            if (!data_fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create appropriate AUDIO fragment!\n", __debug__);
                g_free(filename);
                g_object_unref(stream);
                return FALSE;
            }

            /* Set file */
            if (!mirage_frag_iface_audio_set_file(MIRAGE_FRAG_IFACE_AUDIO(data_fragment), filename, stream, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set track data file!\n", __debug__);
                g_free(filename);
                g_object_unref(stream);
                g_object_unref(data_fragment);
                return FALSE;
            }

            /* Set offset */
            mirage_frag_iface_audio_set_offset(MIRAGE_FRAG_IFACE_AUDIO(data_fragment), start);
        }

        g_free(filename);
        g_object_unref(stream);
    }

    /* Set length */
    if (length) {
        /* Use supplied length */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting fragment's length: 0x%X\n", __debug__, length);
        mirage_fragment_set_length(MIRAGE_FRAGMENT(data_fragment), length);
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
    mirage_track_add_fragment(MIRAGE_TRACK(self->priv->cur_track), -1, data_fragment);
    g_object_unref(data_fragment);

    return TRUE;
};

static void mirage_parser_toc_track_set_start (MIRAGE_Parser_TOC *self, gint start)
{
    /* If start is not given (-1), we use current track length */
    if (start == -1) {
        start = mirage_track_layout_get_length(MIRAGE_TRACK(self->priv->cur_track));
    }

    mirage_track_set_track_start(MIRAGE_TRACK(self->priv->cur_track), start);
}

static void mirage_parser_toc_track_add_index (MIRAGE_Parser_TOC *self, gint address)
{
    gint track_start = mirage_track_get_track_start(MIRAGE_TRACK(self->priv->cur_track));

    /* Indices in TOC file are track-start relative... */
    mirage_track_add_index(MIRAGE_TRACK(self->priv->cur_track), track_start + address, NULL, NULL);
}

static void mirage_parser_toc_track_set_flag (MIRAGE_Parser_TOC *self, gint flag, gboolean set)
{
    gint flags = mirage_track_get_flags(MIRAGE_TRACK(self->priv->cur_track));
    if (set) {
        /* Set flag */
        flags |= flag;
    } else {
        /* Clear flag */
        flags &= ~flag;
    }
    mirage_track_set_flags(MIRAGE_TRACK(self->priv->cur_track), flags);
}

static void mirage_parser_toc_track_set_isrc (MIRAGE_Parser_TOC *self, gchar *isrc)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting ISRC: <%s>\n", __debug__, isrc);
    mirage_track_set_isrc(MIRAGE_TRACK(self->priv->cur_track), isrc);
}


/**********************************************************************\
 *                          CD-TEXT parsing                           *
\**********************************************************************/
static gboolean mirage_parser_toc_cdtext_parse_binary (MIRAGE_Parser_TOC *self, gchar *bin_str, gchar **ret_str, gint *ret_len, GError **error G_GNUC_UNUSED)
{
    gchar **elements;
    gchar *data_str;
    gint data_len;
    gint i;

    elements = g_regex_split(self->priv->regex_binary, bin_str, 0);

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

static gboolean mirage_parser_toc_cdtext_parse_langmaps (MIRAGE_Parser_TOC *self, gchar *langmaps_str, GError **error G_GNUC_UNUSED)
{
    GMatchInfo *match_info;

    g_regex_match(self->priv->regex_langmap, langmaps_str, 0, &match_info);
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

        g_hash_table_insert(self->priv->lang_map, GINT_TO_POINTER(index), GINT_TO_POINTER(code));

        g_free(code_str);
        g_free(index_str);

        g_match_info_next(match_info, NULL);
    }

    g_match_info_free(match_info);

    return TRUE;
}

static gboolean mirage_parser_toc_cdtext_parse_language (MIRAGE_Parser_TOC *self, gchar *data_str, GObject *language, GError **error G_GNUC_UNUSED)
{
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

    g_regex_match(self->priv->regex_langdata, data_str, 0, &match_info);
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
            mirage_parser_toc_cdtext_parse_binary(self, content_str, &content, &content_len, NULL);
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

static gboolean mirage_parser_toc_cdtext_parse_disc_languages (MIRAGE_Parser_TOC *self, gchar *languages_str, GError **error G_GNUC_UNUSED)
{
    GMatchInfo *match_info;

    g_regex_match(self->priv->regex_language, languages_str, 0, &match_info);
    while (g_match_info_matches(match_info)) {
        gchar *index_str = g_match_info_fetch_named(match_info, "lang_idx");
        gchar *data_str = g_match_info_fetch_named(match_info, "lang_data");
        gint index = atoi(index_str);
        gint code = GPOINTER_TO_INT(g_hash_table_lookup(self->priv->lang_map, GINT_TO_POINTER(index)));

        GObject *language = NULL;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding disc language: index %i -> code %i\n", __debug__, index, code);
        if (mirage_session_add_language(MIRAGE_SESSION(self->priv->cur_session), code, &language, NULL)) {
            mirage_parser_toc_cdtext_parse_language(self, data_str, language, NULL);
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

static gboolean mirage_parser_toc_cdtext_parse_track_languages (MIRAGE_Parser_TOC *self, gchar *languages_str, GError **error G_GNUC_UNUSED)
{
    GMatchInfo *match_info;

    g_regex_match(self->priv->regex_language, languages_str, 0, &match_info);
    while (g_match_info_matches(match_info)) {
        gchar *index_str = g_match_info_fetch_named(match_info, "lang_idx");
        gchar *data_str = g_match_info_fetch_named(match_info, "lang_data");
        gint index = atoi(index_str);
        gint code = GPOINTER_TO_INT(g_hash_table_lookup(self->priv->lang_map, GINT_TO_POINTER(index)));

        GObject *language = NULL;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding track language: index %i -> code %i\n", __debug__, index, code);
        if (mirage_track_add_language(MIRAGE_TRACK(self->priv->cur_track), code, &language, NULL)) {
            mirage_parser_toc_cdtext_parse_language(self, data_str, language, NULL);
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

static gboolean mirage_parser_toc_callback_cdtext (MIRAGE_Parser_TOC *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    if (!self->priv->cur_track) {
        /* Disc CD-TEXT */
        gchar *langmaps_str, *languages_str;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: disc CD-TEXT\n", __debug__);

        langmaps_str = g_match_info_fetch_named(match_info, "langmaps");
        languages_str = g_match_info_fetch_named(match_info, "languages");

        mirage_parser_toc_cdtext_parse_langmaps(self, langmaps_str, NULL);
        mirage_parser_toc_cdtext_parse_disc_languages(self, languages_str, NULL);

        g_free(langmaps_str);
        g_free(languages_str);
    } else {
        /* Track CD-TEXT */
        gchar *languages_str;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track CD-TEXT\n", __debug__);

        languages_str = g_match_info_fetch_named(match_info, "languages");
        mirage_parser_toc_cdtext_parse_track_languages(self, languages_str, NULL);

        g_free(languages_str);
    }

    return TRUE;
}


/**********************************************************************\
 *                       Regex parsing engine                         *
\**********************************************************************/
typedef gboolean (*TOC_RegexCallback) (MIRAGE_Parser_TOC *self, GMatchInfo *match_info, GError **error);

typedef struct
{
    GRegex *regex;
    TOC_RegexCallback callback_func;
} TOC_RegexRule;


static gboolean mirage_parser_toc_callback_comment (MIRAGE_Parser_TOC *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *comment = g_match_info_fetch_named(match_info, "comment");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n"); /* To make log more readable */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed COMMENT: %s\n", __debug__, comment);

    g_free(comment);

    return TRUE;
}

static gboolean mirage_parser_toc_callback_session_type (MIRAGE_Parser_TOC *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *type = g_match_info_fetch_named(match_info, "type");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed SESSION TYPE: %s\n", __debug__, type);

    mirage_parser_toc_set_session_type(self, type);

    g_free(type);

    return TRUE;
}

static gboolean mirage_parser_toc_callback_catalog (MIRAGE_Parser_TOC *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *catalog = g_match_info_fetch_named(match_info, "catalog");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed CATALOG: %.13s\n", __debug__, catalog);

    mirage_disc_set_mcn(MIRAGE_DISC(self->priv->disc), catalog);

    g_free(catalog);

    return TRUE;
}

static gboolean mirage_parser_toc_callback_track (MIRAGE_Parser_TOC *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *type, *subchan;

    type = g_match_info_fetch_named(match_info, "type");
    subchan = g_match_info_fetch_named(match_info, "subchan");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n"); /* To make log more readable */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed TRACK: type: %s, sub: %s\n", __debug__, type, subchan);

    mirage_parser_toc_add_track(self, type, subchan);

    g_free(subchan);
    g_free(type);

    return TRUE;
}

static gboolean mirage_parser_toc_callback_track_flag_copy (MIRAGE_Parser_TOC *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *no = g_match_info_fetch_named(match_info, "no");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed %s COPY track flag\n", __debug__, no ? no : "");

    if (!g_strcmp0(no, "NO")) {
        mirage_parser_toc_track_set_flag(self, MIRAGE_TRACKF_COPYPERMITTED, TRUE);
    } else {
        mirage_parser_toc_track_set_flag(self, MIRAGE_TRACKF_COPYPERMITTED, FALSE);
    }

    g_free(no);

    return TRUE;
}

static gboolean mirage_parser_toc_callback_track_flag_preemphasis (MIRAGE_Parser_TOC *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *no = g_match_info_fetch_named(match_info, "no");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed %s PRE_EMPHASIS track flag\n", __debug__, no ? no : "");

    if (!g_strcmp0(no, "NO")) {
        mirage_parser_toc_track_set_flag(self, MIRAGE_TRACKF_PREEMPHASIS, TRUE);
    } else {
        mirage_parser_toc_track_set_flag(self, MIRAGE_TRACKF_PREEMPHASIS, FALSE);
    }

    g_free(no);

    return TRUE;
}

static gboolean mirage_parser_toc_callback_track_flag_channels (MIRAGE_Parser_TOC *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *num = g_match_info_fetch_named(match_info, "num");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed %s_CHANNEL_AUDIO track flag\n", __debug__, num);

    if (!g_strcmp0(num, "FOUR")) {
        mirage_parser_toc_track_set_flag(self, MIRAGE_TRACKF_FOURCHANNEL, TRUE);
    } else {
        mirage_parser_toc_track_set_flag(self, MIRAGE_TRACKF_FOURCHANNEL, FALSE);
    }

    g_free(num);

    return TRUE;
}

static gboolean mirage_parser_toc_callback_track_isrc (MIRAGE_Parser_TOC *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *isrc = g_match_info_fetch_named(match_info, "isrc");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed ISRC: %s\n", __debug__, isrc);

    mirage_parser_toc_track_set_isrc(self, isrc);

    g_free(isrc);

    return TRUE;
}

static gboolean mirage_parser_toc_callback_track_index (MIRAGE_Parser_TOC *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *address_str = g_match_info_fetch_named(match_info, "address");
    gint address = mirage_helper_msf2lba_str(address_str, FALSE);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed INDEX: %s (0x%X)\n", __debug__, address_str, address);

    g_free(address_str);

    mirage_parser_toc_track_add_index(self, address);

    return TRUE;
}

static gboolean mirage_parser_toc_callback_track_start (MIRAGE_Parser_TOC *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
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

    mirage_parser_toc_track_set_start(self, address);

    return TRUE;
}

static gboolean mirage_parser_toc_callback_track_pregap (MIRAGE_Parser_TOC *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *length_str = g_match_info_fetch_named(match_info, "length");
    gint length = mirage_helper_msf2lba_str(length_str, FALSE);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed PREGAP: %s (0x%X)\n", __debug__, length_str, length);

    g_free(length_str);

    mirage_parser_toc_track_add_fragment(self, TOC_DATA_TYPE_NONE, NULL, 0, 0, length, NULL);

    mirage_parser_toc_track_set_start(self, -1);

    return TRUE;
}

static gboolean mirage_parser_toc_callback_track_zero (MIRAGE_Parser_TOC *self, GMatchInfo *match_info, GError **error)
{
    gchar *length_str = g_match_info_fetch_named(match_info, "length");
    gint length = mirage_helper_msf2lba_str(length_str, FALSE);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed ZERO: %s (0x%X)\n", __debug__, length_str, length);

    g_free(length_str);

    return mirage_parser_toc_track_add_fragment(self, TOC_DATA_TYPE_NONE, NULL, 0, 0, length, error);
}

static gboolean mirage_parser_toc_callback_track_silence (MIRAGE_Parser_TOC *self, GMatchInfo *match_info, GError **error)
{
    gchar *length_str = g_match_info_fetch_named(match_info, "length");
    gint length = mirage_helper_msf2lba_str(length_str, FALSE);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed SILENCE: %s (0x%X)\n", __debug__, length_str, length);

    g_free(length_str);

    return mirage_parser_toc_track_add_fragment(self, TOC_DATA_TYPE_NONE, NULL, 0, 0, length, error);
}

static gboolean mirage_parser_toc_callback_track_audiofile (MIRAGE_Parser_TOC *self, GMatchInfo *match_info, GError **error)
{
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

    succeeded = mirage_parser_toc_track_add_fragment(self, TOC_DATA_TYPE_AUDIO, filename, base_offset, start, length, error);

    g_free(length_str);
    g_free(start_str);
    g_free(base_offset_str);
    g_free(filename);

    return succeeded;
}

static gboolean mirage_parser_toc_callback_track_datafile (MIRAGE_Parser_TOC *self, GMatchInfo *match_info, GError **error)
{
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

    succeeded = mirage_parser_toc_track_add_fragment(self, TOC_DATA_TYPE_DATA, filename, base_offset, 0, length, error);

    g_free(length_str);
    g_free(base_offset_str);
    g_free(filename);

    return succeeded;
}


#define APPEND_REGEX_RULE(list,rule,callback) { \
    TOC_RegexRule *new_rule = g_new(TOC_RegexRule, 1); \
    new_rule->regex = g_regex_new(rule, G_REGEX_OPTIMIZE, 0, NULL); \
    new_rule->callback_func = callback; \
    /* Append to the list */ \
    list = g_list_append(list, new_rule); \
}

static void mirage_parser_toc_init_regex_parser (MIRAGE_Parser_TOC *self)
{
    /* Ignore empty lines */
    APPEND_REGEX_RULE(self->priv->regex_rules, "^[\\s]*$", NULL);

    /* Comment */
    APPEND_REGEX_RULE(self->priv->regex_rules, "^\\s*\\/{2}(?<comment>.+)$", mirage_parser_toc_callback_comment);

    APPEND_REGEX_RULE(self->priv->regex_rules, "^\\s*(?<type>(CD_DA|CD_ROM_XA|CD_ROM|CD_I))", mirage_parser_toc_callback_session_type);
    /* Store pointer to header's regex rule */
    GList *elem_header = g_list_last(self->priv->regex_rules);
    TOC_RegexRule *rule_header = elem_header->data;
    self->priv->regex_header_ptr = rule_header->regex;

    APPEND_REGEX_RULE(self->priv->regex_rules, "^\\s*CATALOG\\s*\"(?<catalog>\\d{13,13})\"", mirage_parser_toc_callback_catalog);

    APPEND_REGEX_RULE(self->priv->regex_rules, "^\\s*TRACK\\s*(?<type>(AUDIO|MODE1_RAW|MODE1|MODE2_FORM1|MODE2_FORM2|MODE2_FORM_MIX|MODE2_RAW|MODE2))\\s*(?<subchan>(RW_RAW|RW))?", mirage_parser_toc_callback_track);

    APPEND_REGEX_RULE(self->priv->regex_rules, "^\\s*(?<no>NO)?\\s*COPY", mirage_parser_toc_callback_track_flag_copy);
    APPEND_REGEX_RULE(self->priv->regex_rules, "^\\s*(?<no>NO)?\\s*PRE_EMPHASIS", mirage_parser_toc_callback_track_flag_preemphasis);
    APPEND_REGEX_RULE(self->priv->regex_rules, "^\\s*(?<num>(TWO|FOUR))_CHANNEL_AUDIO", mirage_parser_toc_callback_track_flag_channels);

    APPEND_REGEX_RULE(self->priv->regex_rules, "^\\s*ISRC\\s*\"(?<isrc>[A-Z0-9]{5,5}[0-9]{7,7})\"", mirage_parser_toc_callback_track_isrc);

    APPEND_REGEX_RULE(self->priv->regex_rules, "^\\s*INDEX\\s*(?<address>\\d+:\\d+:\\d+)", mirage_parser_toc_callback_track_index);

    APPEND_REGEX_RULE(self->priv->regex_rules, "^\\s*START\\s*(?<address>\\d+:\\d+:\\d+)?", mirage_parser_toc_callback_track_start);
    APPEND_REGEX_RULE(self->priv->regex_rules, "^\\s*PREGAP\\s*(?<length>\\d+:\\d+:\\d+)", mirage_parser_toc_callback_track_pregap);

    APPEND_REGEX_RULE(self->priv->regex_rules, "^\\s*ZERO\\s*(?<length>\\d+:\\d+:\\d+)", mirage_parser_toc_callback_track_zero);
    APPEND_REGEX_RULE(self->priv->regex_rules, "^\\s*SILENCE\\s*(?<length>\\d+:\\d+:\\d+)", mirage_parser_toc_callback_track_silence);

    APPEND_REGEX_RULE(self->priv->regex_rules, "^\\s*(AUDIO)?FILE\\s*\"(?<filename>.+)\"\\s*(#(?<base_offset>\\d+))?\\s*((?<start>[\\d]+:[\\d]+:[\\d]+)|(?<start_num>\\d+))\\s*(?<length>[\\d]+:[\\d]+:[\\d]+)?", mirage_parser_toc_callback_track_audiofile);
    APPEND_REGEX_RULE(self->priv->regex_rules, "^\\s*DATAFILE\\s*\"(?<filename>.+)\"\\s*(#(?<base_offset>\\d+))?\\s*(?<length>[\\d]+:[\\d]+:[\\d]+)?", mirage_parser_toc_callback_track_datafile);

    /* *** Special CD-TEXT block handling rules... *** */

    /* The one rule to match them all; matches the whole CD-TEXT block, and
       returns two (big) chunks of text; language maps and languages; both need
       to be further parsed by additional rules... (P.S.: there's groups everywhere
       because G_REGEX_MATCH_PARTIAL requires them) */
    self->priv->regex_cdtext = g_regex_new("CD_TEXT(\\s)*"
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
    self->priv->regex_langmap = g_regex_new("\\s*(?<lang_idx>\\d+)[ \\t]*:[ \\t]*(?<lang_code>\\w+)\\s*", G_REGEX_OPTIMIZE, 0, NULL);

    /* Used for parsing languages */
    self->priv->regex_language = g_regex_new("\\s*LANGUAGE\\s*(?<lang_idx>\\d+)\\s*"
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
    self->priv->regex_langdata = g_regex_new("("
                                          "((?<type1>\\w+)[ \\t]*\"(?<data1>.*)\"\\s*)"
                                        "|"
                                          "((?<type2>\\w+)[ \\t]*{(?<data2>[\\d,\\s]*)}\\s*)"
                                        ")",  G_REGEX_OPTIMIZE, 0, NULL);

    /* Used for splitting binary data string */
    self->priv->regex_binary = g_regex_new("\\s*,\\s*", G_REGEX_OPTIMIZE, 0, NULL);
}

static void mirage_parser_toc_cleanup_regex_parser (MIRAGE_Parser_TOC *self)
{
    GList *entry;

    G_LIST_FOR_EACH(entry, self->priv->regex_rules) {
        TOC_RegexRule *rule = entry->data;
        g_regex_unref(rule->regex);
        g_free(rule);
    }

    g_list_free(self->priv->regex_rules);

    /* CD-TEXT rules */
    g_regex_unref(self->priv->regex_cdtext);
    g_regex_unref(self->priv->regex_langmap);
    g_regex_unref(self->priv->regex_language);
    g_regex_unref(self->priv->regex_langdata);
    g_regex_unref(self->priv->regex_binary);
}


static gboolean mirage_parser_toc_parse_toc_file (MIRAGE_Parser_TOC *self, gchar *filename, GError **error)
{
    GError *io_error = NULL;
    GIOChannel *io_channel;
    gboolean succeeded = TRUE;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: opening file: %s\n", __debug__, filename);

    /* Create IO channel for file */
    io_channel = g_io_channel_new_file(filename, "r", &io_error);
    if (!io_channel) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create IO channel: %s\n", __debug__, io_error->message);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to create I/O channel on file '%s': %s", filename, io_error->message);
        g_error_free(io_error);
        return FALSE;
    }

    /* If provided, use the specified encoding; otherwise, use default (since
       .toc file is linux-specific and should be fine with UTF-8 anyway) */
    const gchar *encoding = mirage_parser_get_param_string(MIRAGE_PARSER(self), "encoding");
    if (encoding) {
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
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Status %d while reading line #%d from IO channel: %s", status, line_nr, io_error ? io_error->message : "no error message");
            g_error_free(io_error);
            succeeded = FALSE;
            break;
        }

        /* If we're not in the middle of CD-TEXT parsing, use GRegex matching
           engine, otherwise do the custom stuff */
        if (!parsing_cdtext) {
            /* GRegex matching engine */
            GList *entry;

            /* Go over all matching rules */
            G_LIST_FOR_EACH(entry, self->priv->regex_rules) {
                TOC_RegexRule *regex_rule = entry->data;

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
                g_regex_match(self->priv->regex_cdtext, line_str, G_REGEX_MATCH_PARTIAL, &match_info);
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

            if (g_regex_match(self->priv->regex_cdtext, cdtext_string->str, G_REGEX_MATCH_PARTIAL, &match_info)) {
                /* FIXME: can we live with failure? */
                mirage_parser_toc_callback_cdtext(self, match_info, NULL);

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


/**********************************************************************\
 *                            Helper functions                        *
\**********************************************************************/
static void mirage_parser_toc_init_session_data (MIRAGE_Parser_TOC *self)
{
    /* Init langmap */
    self->priv->lang_map = g_hash_table_new(g_direct_hash, g_direct_equal);
}

static void mirage_parser_toc_cleanup_session_data (MIRAGE_Parser_TOC *self)
{
    /* No reference is kept for these, so just set them to NULL */
    self->priv->cur_session = NULL;
    self->priv->cur_track = NULL;

    /* Cleanup per-session data */
    g_free(self->priv->toc_filename);
    self->priv->toc_filename = NULL;

    self->priv->cur_tfile_sectsize = 0;

    self->priv->cur_sfile_sectsize = 0;
    self->priv->cur_sfile_format = 0;

    self->priv->cur_langcode = 0;
    g_hash_table_destroy(self->priv->lang_map);

    g_free(self->priv->mixed_mode_bin);
    self->priv->mixed_mode_bin = NULL;
    self->priv->mixed_mode_offset = 0;
}


static gboolean mirage_parser_toc_check_toc_file (MIRAGE_Parser_TOC *self, const gchar *filename)
{
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
    const gchar *encoding = mirage_parser_get_param_string(MIRAGE_PARSER(self), "encoding");
    if (encoding) {
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
        if (g_regex_match(self->priv->regex_header_ptr, line_str, 0, &match_info)) {
            succeeded = TRUE;
        }
        /* Free match info */
        g_match_info_free(match_info);

        g_free(line_str);

        /* If we found the header, break the loop */
        if (succeeded) {
            break;
        }
    }

    g_io_channel_unref(io_channel);

    return succeeded;
}


/**********************************************************************\
 *                 MIRAGE_Parser methods implementation               *
\**********************************************************************/
static GObject *mirage_parser_toc_load_image (MIRAGE_Parser *_self, gchar **filenames, GError **error)
{
    MIRAGE_Parser_TOC *self = MIRAGE_PARSER_TOC(_self);

    gboolean succeeded = TRUE;
    gint i;

    /* Check if we can load file(s) */
    for (i = 0; i < g_strv_length(filenames); i++) {
        if (!mirage_parser_toc_check_toc_file(self, filenames[i])) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Parser cannot handle given image!");
            return FALSE;
        }
    }

    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_attach_child(MIRAGE_OBJECT(self), self->priv->disc);

    mirage_disc_set_filenames(MIRAGE_DISC(self->priv->disc), filenames);

    /* Each TOC/BIN is one session, so we load all given filenames */
    for (i = 0; i < g_strv_length(filenames); i++) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: loading session #%i: '%s'!\n", __debug__, i, filenames[i]);
        mirage_parser_toc_init_session_data(self);

        /* Store the TOC filename */
        g_free(self->priv->toc_filename);
        self->priv->toc_filename = g_strdup(filenames[i]);

        /* There's slight problem with multi-session TOC images, namely that each
           TOC can be used independently... in order words, there's no way to determine
           the length of leadouts for sessions (since all sessions start at sector 0).
           So we use what multisession FAQ from cdrecord docs tells us... */
        if (i > 0) {
            GObject *prev_session = NULL;
            gint leadout_length = 0;

            if (!mirage_disc_get_session_by_index(MIRAGE_DISC(self->priv->disc), -1, &prev_session, error)) {
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

            mirage_session_set_leadout_length(MIRAGE_SESSION(prev_session), leadout_length);

            g_object_unref(prev_session);
        }

        /* Create session */
        self->priv->cur_session = NULL;
        mirage_disc_add_session_by_index(MIRAGE_DISC(self->priv->disc), -1, &self->priv->cur_session);
        g_object_unref(self->priv->cur_session); /* Don't keep reference */

        /* Parse TOC */
        succeeded = mirage_parser_toc_parse_toc_file(self, filenames[i], error);

        /* Cleanup */
        mirage_parser_toc_cleanup_session_data(self);

        /* If parsing failed, goto end */
        if (!succeeded) {
            goto end;
        }
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finishing the layout\n", __debug__);
    /* Now guess medium type and if it's a CD-ROM, add Red Book pregap */
    gint medium_type = mirage_parser_guess_medium_type(MIRAGE_PARSER(self), self->priv->disc);
    mirage_disc_set_medium_type(MIRAGE_DISC(self->priv->disc), medium_type);
    if (medium_type == MIRAGE_MEDIUM_CD) {
        mirage_parser_add_redbook_pregap(MIRAGE_PARSER(self), self->priv->disc);
    }

end:
    /* Return disc */
    mirage_object_detach_child(MIRAGE_OBJECT(self), self->priv->disc);
    if (succeeded) {
        return self->priv->disc;
    } else {
        g_object_unref(self->priv->disc);
        return NULL;
    }
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE(MIRAGE_Parser_TOC, mirage_parser_toc, MIRAGE_TYPE_PARSER);

void mirage_parser_toc_type_register (GTypeModule *type_module)
{
    return mirage_parser_toc_register_type(type_module);
}


static void mirage_parser_toc_init (MIRAGE_Parser_TOC *self)
{
    self->priv = MIRAGE_PARSER_TOC_GET_PRIVATE(self);

    mirage_parser_generate_parser_info(MIRAGE_PARSER(self),
        "PARSER-TOC",
        "TOC Image Parser",
        "Cdrdao TOC files",
        "application/x-cdrdao-toc"
    );

    mirage_parser_toc_init_regex_parser(self);
}

static void mirage_parser_toc_finalize (GObject *gobject)
{
    MIRAGE_Parser_TOC *self = MIRAGE_PARSER_TOC(gobject);

    /* Cleanup regex parser engine */
    mirage_parser_toc_cleanup_regex_parser(self);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_parser_toc_parent_class)->finalize(gobject);
}

static void mirage_parser_toc_class_init (MIRAGE_Parser_TOCClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MIRAGE_ParserClass *parser_class = MIRAGE_PARSER_CLASS(klass);

    gobject_class->finalize = mirage_parser_toc_finalize;

    parser_class->load_image = mirage_parser_toc_load_image;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Parser_TOCPrivate));
}

static void mirage_parser_toc_class_finalize (MIRAGE_Parser_TOCClass *klass G_GNUC_UNUSED)
{
}
