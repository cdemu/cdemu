/*
 *  libMirage: TOC image: parser
 *  Copyright (C) 2006-2014 Rok Mandeljc
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "image-toc.h"

#define __debug__ "TOC-Parser"


/**********************************************************************\
 *                  Object and its private structure                  *
\**********************************************************************/
struct _MirageParserTocPrivate
{
    MirageDisc *disc;

    /* Pointers to current session and current track object, so that we don't
     * have to retrieve them all the time; note that no reference is not kept
     * for them */
    MirageSession *cur_session;
    MirageTrack *cur_track;

    /* Per-session data */
    const gchar *toc_filename;

    gint cur_main_size;

    gint cur_subchannel_size;
    gint cur_subchannel_format;

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


G_DEFINE_DYNAMIC_TYPE_EXTENDED(
    MirageParserToc,
    mirage_parser_toc,
    MIRAGE_TYPE_PARSER,
    0,
    G_ADD_PRIVATE_DYNAMIC(MirageParserToc)
)

void mirage_parser_toc_type_register (GTypeModule *type_module)
{
    mirage_parser_toc_register_type(type_module);
}


enum
{
    TOC_DATA_TYPE_NONE,
    TOC_DATA_TYPE_AUDIO,
    TOC_DATA_TYPE_DATA,
};


/**********************************************************************\
 *                     Parser private functions                       *
\**********************************************************************/
static void mirage_parser_toc_set_session_type (MirageParserToc *self, gchar *type_string)
{
    /* Decipher session type */
    static const struct {
        gchar *str;
        gint type;
    } session_types[] = {
        {"CD_DA", MIRAGE_SESSION_CDDA},
        {"CD_ROM", MIRAGE_SESSION_CDROM},
        {"CD_ROM_XA", MIRAGE_SESSION_CDROM_XA},
        {"CD_I", MIRAGE_SESSION_CDI},
    };

    for (guint i = 0; i < G_N_ELEMENTS(session_types); i++) {
        if (!mirage_helper_strcasecmp(session_types[i].str, type_string)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: session type: %s\n", __debug__, session_types[i].str);
            mirage_session_set_session_type(self->priv->cur_session, session_types[i].type);
            break;
        }
    }
}

static void mirage_parser_toc_add_track (MirageParserToc *self, gchar *mode_string, gchar *subchan_string)
{
    /* Add track */
    self->priv->cur_track = g_object_new(MIRAGE_TYPE_TRACK, NULL);
    mirage_session_add_track_by_index(self->priv->cur_session, -1, self->priv->cur_track);
    g_object_unref(self->priv->cur_track); /* Don't keep reference */

    /* Clear internal data */
    self->priv->cur_main_size = 0;
    self->priv->cur_subchannel_size = 0;
    self->priv->cur_subchannel_format = 0;

    /* Decipher mode */
    static const struct {
        gchar *str;
        gint mode;
        gint sectsize;
    } track_modes[] = {
        {"AUDIO", MIRAGE_SECTOR_AUDIO, 2352},
        {"MODE1", MIRAGE_SECTOR_MODE1, 2048},
        {"MODE1_RAW", MIRAGE_SECTOR_MODE1, 2352},
        {"MODE2", MIRAGE_SECTOR_MODE2, 2336},
        {"MODE2_FORM1", MIRAGE_SECTOR_MODE2_FORM1, 2048},
        {"MODE2_FORM2", MIRAGE_SECTOR_MODE2_FORM2, 2324},
        {"MODE2_FORM_MIX", MIRAGE_SECTOR_MODE2_MIXED, 2336},
        {"MODE2_RAW", MIRAGE_SECTOR_MODE2_MIXED, 2352},

    };

    for (guint i = 0; i < G_N_ELEMENTS(track_modes); i++) {
        if (!mirage_helper_strcasecmp(track_modes[i].str, mode_string)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track mode: %s\n", __debug__, track_modes[i].str);

            /* Set track mode */
            mirage_track_set_sector_type(self->priv->cur_track, track_modes[i].mode);
            /* Store sector size */
            self->priv->cur_main_size = track_modes[i].sectsize;

            break;
        }
    }

    if (subchan_string) {
        /* Decipher subchannel (if provided) */
        static const struct {
            gchar *str;
            gint format;
            gint sectsize;
        } subchan_modes[] = {
            {"RW_RAW", MIRAGE_SUBCHANNEL_DATA_FORMAT_PW96_INTERLEAVED | MIRAGE_SUBCHANNEL_DATA_FORMAT_INTERNAL, 96},
            {"RW", MIRAGE_SUBCHANNEL_DATA_FORMAT_RW96 | MIRAGE_SUBCHANNEL_DATA_FORMAT_INTERNAL, 96},
        };

        for (guint i = 0; i < G_N_ELEMENTS(subchan_modes); i++) {
            if (!mirage_helper_strcasecmp(subchan_modes[i].str, subchan_string)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: subchannel mode: %s\n", __debug__, subchan_modes[i].str);
                self->priv->cur_subchannel_size = subchan_modes[i].sectsize;
                self->priv->cur_subchannel_format = subchan_modes[i].format;
                break;
            }
        }
    }
}


static gboolean mirage_parser_toc_track_add_fragment (MirageParserToc *self, gint type, const gchar *filename_string, gint base_offset, gint start, gint length, GError **error)
{
    MirageFragment *fragment;

    /* Create appropriate fragment */
    if (type == TOC_DATA_TYPE_NONE) {
        /* Empty fragment; create NULL fragment */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating NULL fragment\n", __debug__);
        fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL);
    } else {
        /* Find filename */
        gchar *filename = mirage_helper_find_data_file(filename_string, self->priv->toc_filename);
        if (!filename) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to find data file!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DATA_FILE_ERROR, Q_("Failed to find data file!"));
            return FALSE;
        }
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: using data file: %s\n", __debug__, filename);

        /* Create strean */
        MirageStream *stream = mirage_contextual_create_input_stream(MIRAGE_CONTEXTUAL(self), filename, error);
        if (!stream) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create stream on data file!\n", __debug__);
            return FALSE;
        }

        /* BINARY can be either explicitly requested, or it can be assumed from
         * *.bin suffix (with TOC_DATA_TYPE_AUDIO). Note that we check 'filename_string',
         * which is the original filename, and not the 'filename', which is result of
         * our search. */
        if (type == TOC_DATA_TYPE_DATA || mirage_helper_has_suffix(filename_string, ".bin")) {
            /* Binary data; we'd like a BINARY fragment */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating fragment for binary data\n", __debug__);
            fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL);

            gint main_size;
            gint main_format;
            guint64 main_offset;

            gint subchannel_format;
            gint subchannel_size;

            /* Track file */
            main_size = self->priv->cur_main_size;

            /* If we're dealing with BINARY AUDIO data, we need to swap it...
             * (type == TOC_DATA_TYPE_AUDIO) is not sufficient check, because
             * apparently when .bin file contains subchannel data, it automatically
             * gets listed as DATAFILE (hence type = TOC_DATA_TYPE_DATA); thus,
             * we simply check whether we have an audio track or not... */
            gint mode = mirage_track_get_sector_type(self->priv->cur_track);
            if (mode == MIRAGE_SECTOR_AUDIO) {
                main_format = MIRAGE_MAIN_DATA_FORMAT_AUDIO_SWAP;
            } else {
                main_format = MIRAGE_MAIN_DATA_FORMAT_DATA;
            }

            /* Some TOC files don't seem to contain #base_offset entries that
             * are used in case of mixed mode CD... which means we have to
             * calculate base_offset ourselves :( */
            if (!base_offset) {
                /* If we don't have mixed mode BIN filename set yet or if it
                 * differs from  the one currently set, we're dealing with new
                 * file... so we reset offset and store the filename */
                if (!self->priv->mixed_mode_bin || mirage_helper_strcasecmp(self->priv->mixed_mode_bin, filename)) {
                    self->priv->mixed_mode_offset = 0;
                    g_free(self->priv->mixed_mode_bin);
                    self->priv->mixed_mode_bin = g_strdup(filename);
                }

                base_offset = self->priv->mixed_mode_offset;

                /* I guess it's safe to calculate this here; if length isn't
                 * provided, it means whole file is used, so most likely we'll
                 * get file changed next time we're called...*/
                if (type == TOC_DATA_TYPE_DATA) {
                    /* Increase only if it's data... */
                    self->priv->mixed_mode_offset += length * (self->priv->cur_main_size + self->priv->cur_subchannel_size);
                }
            }

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: using base offset: 0x%X\n", __debug__, base_offset);
            main_offset = base_offset + start * (self->priv->cur_main_size + self->priv->cur_subchannel_size);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: calculated track file offset: 0x%" G_GINT64_MODIFIER "X\n", __debug__, main_offset);

            /* Subchannel */
            subchannel_size = self->priv->cur_subchannel_size;
            subchannel_format = self->priv->cur_subchannel_format;

            /* Set stream */
            mirage_fragment_main_data_set_stream(fragment, stream);
            mirage_fragment_main_data_set_size(fragment, main_size);
            mirage_fragment_main_data_set_offset(fragment, main_offset);
            mirage_fragment_main_data_set_format(fragment, main_format);

            mirage_fragment_subchannel_data_set_size(fragment, subchannel_size);
            mirage_fragment_subchannel_data_set_format(fragment, subchannel_format);
        } else {
            /* Audio data */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating fragment for audio daa\n", __debug__);
            fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL);

            /* Set stream */
            mirage_fragment_main_data_set_stream(fragment, stream);
            mirage_fragment_main_data_set_size(fragment, 2352);
            mirage_fragment_main_data_set_offset(fragment, start*2352);
            mirage_fragment_main_data_set_format(fragment, MIRAGE_MAIN_DATA_FORMAT_AUDIO);
        }

        g_free(filename);
        g_object_unref(stream);
    }

    /* Set length */
    if (length) {
        /* Use supplied length */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting fragment's length: 0x%X\n", __debug__, length);
        mirage_fragment_set_length(fragment, length);
    } else {
        /* Use whole file */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: using whole file\n", __debug__);
        if (!mirage_fragment_use_the_rest_of_file(fragment, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to use whole file!\n", __debug__);
            g_object_unref(fragment);
            return FALSE;
        }
    }

    /* Add fragment */
    mirage_track_add_fragment(self->priv->cur_track, -1, fragment);
    g_object_unref(fragment);

    return TRUE;
}

static void mirage_parser_toc_track_set_start (MirageParserToc *self, gint start)
{
    /* If start is not given (-1), we use current track length */
    if (start == -1) {
        start = mirage_track_layout_get_length(self->priv->cur_track);
    }

    mirage_track_set_track_start(self->priv->cur_track, start);
}

static void mirage_parser_toc_track_add_index (MirageParserToc *self, gint address)
{
    gint track_start = mirage_track_get_track_start(self->priv->cur_track);

    /* Indices in TOC file are track-start relative... */
    mirage_track_add_index(self->priv->cur_track, track_start + address, NULL);
}

static void mirage_parser_toc_track_set_flag (MirageParserToc *self, gint flag, gboolean set)
{
    gint flags = mirage_track_get_flags(self->priv->cur_track);
    if (set) {
        /* Set flag */
        flags |= flag;
    } else {
        /* Clear flag */
        flags &= ~flag;
    }
    mirage_track_set_flags(self->priv->cur_track, flags);
}

static gboolean mirage_parser_toc_track_set_isrc (MirageParserToc *self, const gchar *isrc, GError **error G_GNUC_UNUSED)
{
    if (mirage_helper_validate_isrc(isrc)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting ISRC: <%s>\n", __debug__, isrc);
        mirage_track_set_isrc(self->priv->cur_track, isrc);
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to validate ISRC: <%s>!\n", __debug__, isrc);
    }

    return TRUE;
}


/**********************************************************************\
 *                          CD-TEXT parsing                           *
\**********************************************************************/
static gboolean mirage_parser_toc_cdtext_parse_binary (MirageParserToc *self, gchar *bin_str, gchar **ret_str, gint *ret_len, GError **error G_GNUC_UNUSED)
{
    gchar **elements;
    gchar *data_str;
    gint data_len;

    elements = g_regex_split(self->priv->regex_binary, bin_str, 0);

    data_len = g_strv_length(elements);
    data_str = g_new(gchar, data_len);

    for (gint i = 0; i < data_len; i++) {
        data_str[i] = atoi(elements[i]);
    }

    g_strfreev(elements);

    *ret_str = data_str;
    if (ret_len) *ret_len = data_len;

    return TRUE;
}

static gboolean mirage_parser_toc_cdtext_parse_langmaps (MirageParserToc *self, gchar *langmaps_str, GError **error G_GNUC_UNUSED)
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

static gboolean mirage_parser_toc_cdtext_parse_language (MirageParserToc *self, gchar *data_str, MirageLanguage *language, GError **error G_GNUC_UNUSED)
{
    GMatchInfo *match_info;

    static const struct {
        gchar *pack_id;
        gint pack_type;
    } packs[] = {
        {"TITLE", MIRAGE_LANGUAGE_PACK_TITLE},
        {"PERFORMER", MIRAGE_LANGUAGE_PACK_PERFORMER},
        {"SONGWRITER", MIRAGE_LANGUAGE_PACK_SONGWRITER},
        {"COMPOSER", MIRAGE_LANGUAGE_PACK_COMPOSER},
        {"ARRANGER", MIRAGE_LANGUAGE_PACK_ARRANGER},
        {"MESSAGE", MIRAGE_LANGUAGE_PACK_MESSAGE},
        {"DISC_ID", MIRAGE_LANGUAGE_PACK_DISC_ID},
        {"GENRE", MIRAGE_LANGUAGE_PACK_GENRE},
        {"TOC_INFO1", MIRAGE_LANGUAGE_PACK_TOC},
        {"TOC_INFO2", MIRAGE_LANGUAGE_PACK_TOC2},
        {"UPC_EAN", MIRAGE_LANGUAGE_PACK_UPC_ISRC},
        {"SIZE_INFO", MIRAGE_LANGUAGE_PACK_SIZE},
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
        for (guint i = 0; i < G_N_ELEMENTS(packs); i++) {
            if (!g_strcmp0(type_str, packs[i].pack_id)) {
                mirage_language_set_pack_data(language, packs[i].pack_type, (const guint8 *)content, content_len, NULL);
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

static gboolean mirage_parser_toc_cdtext_parse_disc_languages (MirageParserToc *self, gchar *languages_str, GError **error G_GNUC_UNUSED)
{
    GMatchInfo *match_info;

    g_regex_match(self->priv->regex_language, languages_str, 0, &match_info);
    while (g_match_info_matches(match_info)) {
        gchar *index_str = g_match_info_fetch_named(match_info, "lang_idx");
        gchar *data_str = g_match_info_fetch_named(match_info, "lang_data");
        gint index = atoi(index_str);
        gint code = GPOINTER_TO_INT(g_hash_table_lookup(self->priv->lang_map, GINT_TO_POINTER(index)));

        MirageLanguage *language;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding disc language: index %i -> code %i\n", __debug__, index, code);

        language = g_object_new(MIRAGE_TYPE_LANGUAGE, NULL);
        if (mirage_session_add_language(self->priv->cur_session, code, language, NULL)) {
            mirage_parser_toc_cdtext_parse_language(self, data_str, language, NULL);
        } else {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add disc language (index %i, code %i)!\n", __debug__, index, code);
        }
        g_object_unref(language);

        g_free(data_str);
        g_free(index_str);

        g_match_info_next(match_info, NULL);
    }

    g_match_info_free(match_info);

    return TRUE;
}

static gboolean mirage_parser_toc_cdtext_parse_track_languages (MirageParserToc *self, gchar *languages_str, GError **error G_GNUC_UNUSED)
{
    GMatchInfo *match_info;

    g_regex_match(self->priv->regex_language, languages_str, 0, &match_info);
    while (g_match_info_matches(match_info)) {
        gchar *index_str = g_match_info_fetch_named(match_info, "lang_idx");
        gchar *data_str = g_match_info_fetch_named(match_info, "lang_data");
        gint index = atoi(index_str);
        gint code = GPOINTER_TO_INT(g_hash_table_lookup(self->priv->lang_map, GINT_TO_POINTER(index)));

        MirageLanguage *language;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding track language: index %i -> code %i\n", __debug__, index, code);

        language = g_object_new(MIRAGE_TYPE_LANGUAGE, NULL);
        if (mirage_track_add_language(self->priv->cur_track, code, language, NULL)) {
            mirage_parser_toc_cdtext_parse_language(self, data_str, language, NULL);
        } else {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track language (index %i, code %i)!\n", __debug__, index, code);
        }
        g_object_unref(language);

        g_free(data_str);
        g_free(index_str);

        g_match_info_next(match_info, NULL);
    }

    g_match_info_free(match_info);

    return TRUE;
}

static gboolean mirage_parser_toc_callback_cdtext (MirageParserToc *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
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
typedef gboolean (*TOC_RegexCallback) (MirageParserToc *self, GMatchInfo *match_info, GError **error);

typedef struct
{
    GRegex *regex;
    TOC_RegexCallback callback_func;
} TOC_RegexRule;


static gboolean mirage_parser_toc_callback_comment (MirageParserToc *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *comment = g_match_info_fetch_named(match_info, "comment");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n"); /* To make log more readable */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed COMMENT: %s\n", __debug__, comment);

    g_free(comment);

    return TRUE;
}

static gboolean mirage_parser_toc_callback_session_type (MirageParserToc *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *type = g_match_info_fetch_named(match_info, "type");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed SESSION TYPE: %s\n", __debug__, type);

    mirage_parser_toc_set_session_type(self, type);

    g_free(type);

    return TRUE;
}

static gboolean mirage_parser_toc_callback_catalog (MirageParserToc *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *catalog = g_match_info_fetch_named(match_info, "catalog");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed CATALOG: %.13s\n", __debug__, catalog);

    mirage_session_set_mcn(self->priv->cur_session, catalog);

    g_free(catalog);

    return TRUE;
}

static gboolean mirage_parser_toc_callback_track (MirageParserToc *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
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

static gboolean mirage_parser_toc_callback_track_flag_copy (MirageParserToc *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *no = g_match_info_fetch_named(match_info, "no");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed %s COPY track flag\n", __debug__, no ? no : "");

    if (!g_strcmp0(no, "NO")) {
        mirage_parser_toc_track_set_flag(self, MIRAGE_TRACK_FLAG_COPYPERMITTED, FALSE);
    } else {
        mirage_parser_toc_track_set_flag(self, MIRAGE_TRACK_FLAG_COPYPERMITTED, TRUE);
    }

    g_free(no);

    return TRUE;
}

static gboolean mirage_parser_toc_callback_track_flag_preemphasis (MirageParserToc *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *no = g_match_info_fetch_named(match_info, "no");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed %s PRE_EMPHASIS track flag\n", __debug__, no ? no : "");

    if (!g_strcmp0(no, "NO")) {
        mirage_parser_toc_track_set_flag(self, MIRAGE_TRACK_FLAG_PREEMPHASIS, FALSE);
    } else {
        mirage_parser_toc_track_set_flag(self, MIRAGE_TRACK_FLAG_PREEMPHASIS, TRUE);
    }

    g_free(no);

    return TRUE;
}

static gboolean mirage_parser_toc_callback_track_flag_channels (MirageParserToc *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *num = g_match_info_fetch_named(match_info, "num");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed %s_CHANNEL_AUDIO track flag\n", __debug__, num);

    if (!g_strcmp0(num, "FOUR")) {
        mirage_parser_toc_track_set_flag(self, MIRAGE_TRACK_FLAG_FOURCHANNEL, TRUE);
    } else {
        mirage_parser_toc_track_set_flag(self, MIRAGE_TRACK_FLAG_FOURCHANNEL, FALSE);
    }

    g_free(num);

    return TRUE;
}

static gboolean mirage_parser_toc_callback_track_isrc (MirageParserToc *self, GMatchInfo *match_info, GError **error)
{
    gboolean success;

    gchar *isrc = g_match_info_fetch_named(match_info, "isrc");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed ISRC: %s\n", __debug__, isrc);

    success = mirage_parser_toc_track_set_isrc(self, isrc, error);

    g_free(isrc);

    return success;
}

static gboolean mirage_parser_toc_callback_track_index (MirageParserToc *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *address_str = g_match_info_fetch_named(match_info, "address");
    gint address = mirage_helper_msf2lba_str(address_str, FALSE);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed INDEX: %s (0x%X)\n", __debug__, address_str, address);

    g_free(address_str);

    mirage_parser_toc_track_add_index(self, address);

    return TRUE;
}

static gboolean mirage_parser_toc_callback_track_start (MirageParserToc *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
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

static gboolean mirage_parser_toc_callback_track_pregap (MirageParserToc *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *length_str = g_match_info_fetch_named(match_info, "length");
    gint length = mirage_helper_msf2lba_str(length_str, FALSE);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed PREGAP: %s (0x%X)\n", __debug__, length_str, length);

    g_free(length_str);

    mirage_parser_toc_track_add_fragment(self, TOC_DATA_TYPE_NONE, NULL, 0, 0, length, NULL);

    mirage_parser_toc_track_set_start(self, -1);

    return TRUE;
}

static gboolean mirage_parser_toc_callback_track_zero (MirageParserToc *self, GMatchInfo *match_info, GError **error)
{
    gchar *length_str = g_match_info_fetch_named(match_info, "length");
    gint length = mirage_helper_msf2lba_str(length_str, FALSE);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed ZERO: %s (0x%X)\n", __debug__, length_str, length);

    g_free(length_str);

    return mirage_parser_toc_track_add_fragment(self, TOC_DATA_TYPE_NONE, NULL, 0, 0, length, error);
}

static gboolean mirage_parser_toc_callback_track_silence (MirageParserToc *self, GMatchInfo *match_info, GError **error)
{
    gchar *length_str = g_match_info_fetch_named(match_info, "length");
    gint length = mirage_helper_msf2lba_str(length_str, FALSE);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed SILENCE: %s (0x%X)\n", __debug__, length_str, length);

    g_free(length_str);

    return mirage_parser_toc_track_add_fragment(self, TOC_DATA_TYPE_NONE, NULL, 0, 0, length, error);
}

static gboolean mirage_parser_toc_callback_track_audiofile (MirageParserToc *self, GMatchInfo *match_info, GError **error)
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

static gboolean mirage_parser_toc_callback_track_datafile (MirageParserToc *self, GMatchInfo *match_info, GError **error)
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


static inline void append_regex_rule (GList **list_ptr, const gchar *rule, TOC_RegexCallback callback)
{
    GList *list = *list_ptr;

    TOC_RegexRule *new_rule = g_new(TOC_RegexRule, 1);
    new_rule->regex = g_regex_new(rule, G_REGEX_OPTIMIZE, 0, NULL);
    g_assert(new_rule->regex != NULL);
    new_rule->callback_func = callback;
    /* Append to the list */
    list = g_list_append(list, new_rule);

    *list_ptr = list;
}

static void mirage_parser_toc_init_regex_parser (MirageParserToc *self)
{
    /* Ignore empty lines */
    append_regex_rule(&self->priv->regex_rules, "^[\\s]*$", NULL);

    /* Comment */
    append_regex_rule(&self->priv->regex_rules, "^\\s*\\/{2}(?<comment>.+)$", mirage_parser_toc_callback_comment);

    append_regex_rule(&self->priv->regex_rules, "^\\s*(?<type>(CD_DA|CD_ROM_XA|CD_ROM|CD_I))", mirage_parser_toc_callback_session_type);
    /* Store pointer to header's regex rule */
    GList *elem_header = g_list_last(self->priv->regex_rules);
    TOC_RegexRule *rule_header = elem_header->data;
    self->priv->regex_header_ptr = rule_header->regex;

    append_regex_rule(&self->priv->regex_rules, "^\\s*CATALOG\\s*\"(?<catalog>\\d{13,13})\"", mirage_parser_toc_callback_catalog);

    append_regex_rule(&self->priv->regex_rules, "^\\s*TRACK\\s*(?<type>(AUDIO|MODE1_RAW|MODE1|MODE2_FORM1|MODE2_FORM2|MODE2_FORM_MIX|MODE2_RAW|MODE2))\\s*(?<subchan>(RW_RAW|RW))?", mirage_parser_toc_callback_track);

    append_regex_rule(&self->priv->regex_rules, "^\\s*(?<no>NO)?\\s*COPY", mirage_parser_toc_callback_track_flag_copy);
    append_regex_rule(&self->priv->regex_rules, "^\\s*(?<no>NO)?\\s*PRE_EMPHASIS", mirage_parser_toc_callback_track_flag_preemphasis);
    append_regex_rule(&self->priv->regex_rules, "^\\s*(?<num>(TWO|FOUR))_CHANNEL_AUDIO", mirage_parser_toc_callback_track_flag_channels);

    append_regex_rule(&self->priv->regex_rules, "^\\s*ISRC\\s*\"(?<isrc>[A-Z0-9]{5,5}[0-9]{7,7})\"", mirage_parser_toc_callback_track_isrc);

    append_regex_rule(&self->priv->regex_rules, "^\\s*INDEX\\s*(?<address>\\d+:\\d+:\\d+)", mirage_parser_toc_callback_track_index);

    append_regex_rule(&self->priv->regex_rules, "^\\s*START\\s*(?<address>\\d+:\\d+:\\d+)?", mirage_parser_toc_callback_track_start);
    append_regex_rule(&self->priv->regex_rules, "^\\s*PREGAP\\s*(?<length>\\d+:\\d+:\\d+)", mirage_parser_toc_callback_track_pregap);

    append_regex_rule(&self->priv->regex_rules, "^\\s*ZERO\\s*(?<length>\\d+:\\d+:\\d+)", mirage_parser_toc_callback_track_zero);
    append_regex_rule(&self->priv->regex_rules, "^\\s*SILENCE\\s*(?<length>\\d+:\\d+:\\d+)", mirage_parser_toc_callback_track_silence);

    append_regex_rule(&self->priv->regex_rules, "^\\s*(AUDIO)?FILE\\s*\"(?<filename>.+)\"\\s*(#(?<base_offset>\\d+))?\\s*((?<start>[\\d]+:[\\d]+:[\\d]+)|(?<start_num>\\d+))\\s*(?<length>[\\d]+:[\\d]+:[\\d]+)?", mirage_parser_toc_callback_track_audiofile);
    append_regex_rule(&self->priv->regex_rules, "^\\s*DATAFILE\\s*\"(?<filename>.+)\"\\s*(#(?<base_offset>\\d+))?\\s*(?<length>[\\d]+:[\\d]+:[\\d]+)?", mirage_parser_toc_callback_track_datafile);

    /* *** Special CD-TEXT block handling rules... *** */

    /* The one rule to match them all; matches the whole CD-TEXT block, and
     * returns two (big) chunks of text; language maps and languages; both need
     * to be further parsed by additional rules... (P.S.: there's groups everywhere
     * because G_REGEX_MATCH_PARTIAL requires them) */
    self->priv->regex_cdtext = g_regex_new(
        "CD_TEXT(\\s)*"
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
        "}",
        G_REGEX_OPTIMIZE|G_REGEX_MULTILINE, 0, NULL);

    /* Used for parsing language maps */
    self->priv->regex_langmap = g_regex_new("\\s*(?<lang_idx>\\d+)[ \\t]*:[ \\t]*(?<lang_code>\\w+)\\s*", G_REGEX_OPTIMIZE, 0, NULL);

    /* Used for parsing languages */
    self->priv->regex_language = g_regex_new(
        "\\s*LANGUAGE\\s*(?<lang_idx>\\d+)\\s*"
        "{\\s*"
            "(?<lang_data>"
                "("
                    "(\\w+[ \\t]*\".*\"\\s*)" /* PACK_TYPE "DATA_STR" */
                "|"
                    "(\\w+[ \\t]*{[\\d,\\s]*}\\s*)" /* PACK_TYPE "DATA_STR" */
                ")*"
            ")"
        "}\\s*",
        G_REGEX_OPTIMIZE, 0, NULL
    );

    /* Used for parsing individual data fields */
    self->priv->regex_langdata = g_regex_new(
        "("
            "((?<type1>\\w+)[ \\t]*\"(?<data1>.*)\"\\s*)"
        "|"
            "((?<type2>\\w+)[ \\t]*{(?<data2>[\\d,\\s]*)}\\s*)"
        ")",
        G_REGEX_OPTIMIZE, 0, NULL
    );

    /* Used for splitting binary data string */
    self->priv->regex_binary = g_regex_new("\\s*,\\s*", G_REGEX_OPTIMIZE, 0, NULL);
}

static void mirage_parser_toc_cleanup_regex_parser (MirageParserToc *self)
{
    for (GList *entry = self->priv->regex_rules; entry; entry = entry->next) {
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


static gboolean mirage_parser_toc_parse_toc_file (MirageParserToc *self, MirageStream *stream, GError **error)
{
    GDataInputStream *data_stream;
    gboolean succeeded = TRUE;

    /* Create GDataInputStream */
    data_stream = mirage_parser_create_text_stream(MIRAGE_PARSER(self), stream, error);
    if (!data_stream) {
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing\n", __debug__);

    /* Couple of variables we'll need if we come across CDTEXT... */
    gboolean parsing_cdtext = FALSE;
    GString *cdtext_string = NULL;

    /* Read file line-by-line */
    for (gint line_number = 1; ; line_number++) {
        GError *local_error = NULL;
        gchar *line_string;
        gsize line_length;

        gboolean matched = FALSE;
        GMatchInfo *match_info = NULL;

        /* Read line */
        line_string = g_data_input_stream_read_line_utf8(data_stream, &line_length, NULL, &local_error);

        /* Handle error */
        if (!line_string) {
            if (!local_error) {
                /* EOF */
                break;
            } else {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read line #%d: %s\n", __debug__, line_number, local_error->message);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Failed to read line #%d: %s!"), line_number, local_error->message);
                g_error_free(local_error);
                succeeded = FALSE;
                break;
            }
        }

        /* If we're not in the middle of CD-TEXT parsing, use GRegex matching
         * engine, otherwise do the custom stuff */
        if (!parsing_cdtext) {
            /* Go over all matching rules */
            for (GList *entry = self->priv->regex_rules; entry; entry = entry->next) {
                TOC_RegexRule *regex_rule = entry->data;

                /* Try to match the given rule */
                if (g_regex_match(regex_rule->regex, line_string, 0, &match_info)) {
                    if (regex_rule->callback_func) {
                        succeeded = regex_rule->callback_func(self, match_info, error);
                    }
                    matched = TRUE;
                }

                /* Must be freed in any case */
                g_match_info_free(match_info);
                match_info = NULL;

                /* Break if we had a match */
                if (matched) {
                    break;
                }
            }

            /* Try to partially match CDTEXT; this one should *never* match in
             * full, unless *everything* was in a single line... */
            if (!matched) {
                g_regex_match(self->priv->regex_cdtext, line_string, G_REGEX_MATCH_PARTIAL, &match_info);
                if (g_match_info_is_partial_match(match_info)) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: partially matched CDTEXT; beginning CD-TEXT parsing\n", __debug__);
                    cdtext_string = g_string_new(line_string);
                    parsing_cdtext = TRUE;
                    matched = TRUE;
                }
                g_match_info_free(match_info);
                match_info = NULL;
            }
        } else {
            /* Append the line to CDTEXT string */
            g_string_append(cdtext_string, line_string);
            g_string_append(cdtext_string, "\n");

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
            match_info = NULL;
        }

        /* Complain if we failed to match the line (should it be fatal?) */
        if (!matched) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to match line #%d: %s\n", __debug__, line_number, line_string);
            /* succeeded = FALSE */
        }

        g_free(line_string);

        /* In case callback didn't succeed... */
        if (!succeeded) {
            break;
        }
    }

    g_object_unref(data_stream);

    return succeeded;
}


/**********************************************************************\
 *                            Helper functions                        *
\**********************************************************************/
static void mirage_parser_toc_init_session_data (MirageParserToc *self)
{
    /* Init langmap */
    self->priv->lang_map = g_hash_table_new(g_direct_hash, g_direct_equal);
}

static void mirage_parser_toc_cleanup_session_data (MirageParserToc *self)
{
    /* No reference is kept for these, so just set them to NULL */
    self->priv->cur_session = NULL;
    self->priv->cur_track = NULL;

    /* Cleanup per-session data */
    self->priv->toc_filename = NULL;

    self->priv->cur_main_size = 0;

    self->priv->cur_subchannel_size = 0;
    self->priv->cur_subchannel_format = 0;

    self->priv->cur_langcode = 0;
    g_hash_table_destroy(self->priv->lang_map);

    g_free(self->priv->mixed_mode_bin);
    self->priv->mixed_mode_bin = NULL;
    self->priv->mixed_mode_offset = 0;
}

static gboolean mirage_parser_toc_check_toc_file (MirageParserToc *self, MirageStream *stream)
{
    gboolean succeeded = FALSE;
    GDataInputStream *data_stream;

    /* Check suffix - must be .toc */
    if (!mirage_helper_has_suffix(mirage_stream_get_filename(stream), ".toc")) {
        return FALSE;
    }

    /* *** Additional check ***
     * Because X-CD Roast also uses .toc for its images, we need to make
     * sure this one was created by cdrdao... for that, we check for presence
     * of CD_DA/CD_ROM_XA/CD_ROM/CD_I directive. */
    data_stream = mirage_parser_create_text_stream(MIRAGE_PARSER(self), stream, NULL);
    if (!data_stream) {
        return FALSE;
    }

    /* Read file line-by-line */
    for (gint line_number = 1; ; line_number++) {
        GError *local_error = NULL;
        gchar *line_string;
        gsize line_length;

        GMatchInfo *match_info = NULL;

        /* Read line */
        line_string = g_data_input_stream_read_line_utf8(data_stream, &line_length, NULL, &local_error);

        /* Handle error */
        if (!line_string) {
            if (!local_error) {
                /* EOF */
                break;
            } else {
                break;
            }
        }

        /* Try to match the rule */
        if (g_regex_match(self->priv->regex_header_ptr, line_string, 0, &match_info)) {
            succeeded = TRUE;
        }
        /* Free match info */
        g_match_info_free(match_info);

        g_free(line_string);

        /* If we found the header, break the loop */
        if (succeeded) {
            break;
        }
    }

    g_object_unref(data_stream);

    return succeeded;
}


/**********************************************************************\
 *                 MirageParser methods implementation               *
\**********************************************************************/
static MirageDisc *mirage_parser_toc_load_image (MirageParser *_self, MirageStream **streams, GError **error)
{
    MirageParserToc *self = MIRAGE_PARSER_TOC(_self);
    gint num_streams;
    gchar **filenames;
    gboolean succeeded = TRUE;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: checking if parser can handle given image...\n", __debug__);

    /* Determine number of streams */
    for (num_streams = 0; streams[num_streams]; num_streams++);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: number of files provided: %d\n", __debug__, num_streams);

    /* Allocate array of filename pointers */
    filenames = g_new0(gchar *, num_streams + 1);

    /* Check if all streams are valid */
    for (gint i = 0; i < num_streams; i++) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: verifying suffix of file #%d...\n", __debug__, i);
        if (!mirage_parser_toc_check_toc_file(self, streams[i])) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: parser cannot handle given image: file #%d has invalid suffix (not a *.toc file)!\n", __debug__, i);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Parser cannot handle given image: invalid TOC file!"));
            g_strfreev(filenames);
            return FALSE;
        }

        filenames[i] = g_strdup(mirage_stream_get_filename(streams[i]));
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: parser can handle given image!\n", __debug__);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing the image...\n", __debug__);

    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_set_parent(MIRAGE_OBJECT(self->priv->disc), self);

    mirage_disc_set_filenames(self->priv->disc, filenames);
    g_strfreev(filenames);

    /* Each TOC/BIN is one session, so we load all given filenames */
    for (gint i = 0; i < num_streams; i++) {
        /* Store the TOC filename */
        self->priv->toc_filename = mirage_stream_get_filename(streams[i]);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: loading session #%i: TOC file '%s'!\n", __debug__, i, self->priv->toc_filename);
        mirage_parser_toc_init_session_data(self);

        /* There's slight problem with multi-session TOC images, namely that each
         * TOC can be used independently... in order words, there's no way to determine
         * the length of leadouts for sessions (since all sessions start at sector 0).
         * So we use what multisession FAQ from cdrecord docs tells us... */
        if (i > 0) {
            MirageSession *prev_session;
            gint leadout_length = 0;

            prev_session = mirage_disc_get_session_by_index(self->priv->disc, -1, error);
            if (!prev_session) {
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

            mirage_session_set_leadout_length(prev_session, leadout_length);

            g_object_unref(prev_session);
        }

        /* Create session */
        self->priv->cur_session = g_object_new(MIRAGE_TYPE_SESSION, NULL);
        mirage_disc_add_session_by_index(self->priv->disc, -1, self->priv->cur_session);
        g_object_unref(self->priv->cur_session); /* Don't keep reference */

        /* Parse TOC */
        succeeded = mirage_parser_toc_parse_toc_file(self, streams[i], error);

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
    mirage_disc_set_medium_type(self->priv->disc, medium_type);
    if (medium_type == MIRAGE_MEDIUM_CD) {
        mirage_parser_add_redbook_pregap(MIRAGE_PARSER(self), self->priv->disc);
    }

end:
    /* Return disc */
    if (succeeded) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing completed successfully\n\n", __debug__);
        return self->priv->disc;
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing failed!\n\n", __debug__);
        g_object_unref(self->priv->disc);
        return NULL;
    }
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
static void mirage_parser_toc_init (MirageParserToc *self)
{
    self->priv = mirage_parser_toc_get_instance_private(self);

    mirage_parser_generate_info(MIRAGE_PARSER(self),
        "PARSER-TOC",
        Q_("TOC Image Parser"),
        1,
        Q_("cdrdao images (*.toc)"), "application/x-cdrdao-toc"
    );

    mirage_parser_toc_init_regex_parser(self);
}

static void mirage_parser_toc_finalize (GObject *gobject)
{
    MirageParserToc *self = MIRAGE_PARSER_TOC(gobject);

    /* Cleanup regex parser engine */
    mirage_parser_toc_cleanup_regex_parser(self);

    /* Chain up to the parent class */
    G_OBJECT_CLASS(mirage_parser_toc_parent_class)->finalize(gobject);
}

static void mirage_parser_toc_class_init (MirageParserTocClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageParserClass *parser_class = MIRAGE_PARSER_CLASS(klass);

    gobject_class->finalize = mirage_parser_toc_finalize;

    parser_class->load_image = mirage_parser_toc_load_image;
}

static void mirage_parser_toc_class_finalize (MirageParserTocClass *klass G_GNUC_UNUSED)
{
}
