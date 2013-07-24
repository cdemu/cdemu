/*
 *  libMirage: CUE image parser: Parser object
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "image-cue.h"

#define __debug__ "CUE-Parser"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_PARSER_CUE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_CUE, MirageParserCuePrivate))

struct _MirageParserCuePrivate
{
    MirageDisc *disc;

    const gchar *cue_filename;

    gchar *cur_data_filename; /* Data file filename */
    gchar *cur_data_type; /* Data type; determines which fragment type to use */
    gint cur_data_sectsize; /* Sector size in case BINARY fragment is used */
    gint cur_data_format; /* Format (AUDIO vs DATA) in case BINARY fragment is used */
    gint cur_track_start; /* Used to determine pregap */

    gint binary_offset; /* Offset within the binary file */

    gboolean cur_pregap_set;

    gint leadout_correction;

    /* Pointers to current session and current track object, so that we don't
       have to retrieve them all the time; note that no reference is not kept
       for them */
    MirageSession *cur_session;
    MirageTrack *cur_track;
    MirageTrack *prev_track;

    /* Regex engine */
    GList *regex_rules;
};


/**********************************************************************\
 *                     Parser private functions                       *
\**********************************************************************/
static gboolean mirage_parser_cue_finish_last_track (MirageParserCue *self, GError **error)
{
    MirageFragment *fragment;
    gboolean succeeded = TRUE;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finishing last track\n", __debug__);

    /* Current track needs to be set at this point */
    if (!self->priv->cur_track) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: current track is not set!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Current track is not set!");
        return FALSE;
    }

    /* Get last fragment and set its length... (actually, since there can be
       postgap fragment stuck at the end, we look for first data fragment that's
       not NULL... and of course, we go from behind) */
    /* FIXME: implement the latter part */
    fragment = mirage_track_get_fragment_by_index(self->priv->cur_track, -1, NULL);
    if (fragment) {
        mirage_fragment_use_the_rest_of_file(fragment, NULL);

        if (mirage_fragment_get_length(fragment) < 0) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: finishing last track resulted in negative fragment length!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Finishing last track resulted in negative fragment length!");
            succeeded = FALSE;
        }

        g_object_unref(fragment);
    }

    return succeeded;
}

static gboolean mirage_parser_cue_set_new_file (MirageParserCue *self, const gchar *filename_string, const gchar *file_type, GError **error)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: new file: %s\n", __debug__, filename_string);

    /* We got new file; either we got it for the first time, which means we don't
       have any tracks yet and don't have to do anything. If we got new file, it
       means means we already have some tracks and the last one needs to be
       finished */
    if (self->priv->cur_track && !mirage_parser_cue_finish_last_track(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to finish last track!\n", __debug__);
        return FALSE;
    }

    /* Set current file name */
    g_free(self->priv->cur_data_filename);
    self->priv->cur_data_filename = mirage_helper_find_data_file(filename_string, self->priv->cue_filename);
    if (!self->priv->cur_data_filename) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to find data file!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DATA_FILE_ERROR, "Failed to find data file!");
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: actual data file: %s\n", __debug__, self->priv->cur_data_filename);

    /* Set current data type */
    g_free(self->priv->cur_data_type);
    self->priv->cur_data_type = g_strdup(file_type);
    self->priv->cur_track_start = 0;
    self->priv->binary_offset = 0;

    return TRUE;
}

static gboolean mirage_parser_cue_add_track (MirageParserCue *self, gint number, const gchar *mode_string, GError **error)
{
    gint i;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding track %d\n", __debug__, number);

    /* Current track becomes previous one */
    self->priv->prev_track = self->priv->cur_track;

    /* Add new track, store the pointer and release reference */
    self->priv->cur_track = g_object_new(MIRAGE_TYPE_TRACK, NULL);
    if (!mirage_session_add_track_by_number(self->priv->cur_session, number, self->priv->cur_track, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __debug__);
        g_object_unref(self->priv->cur_track);
        return FALSE;
    }
    g_object_unref(self->priv->cur_track);

    /* Decipher mode */
    static const struct {
        gchar *str;
        gint mode;
        gint sectsize;
        gint format;
    } track_modes[] = {
        {"AUDIO",      MIRAGE_MODE_AUDIO, 2352, MIRAGE_MAIN_AUDIO},
        {"CDG",        MIRAGE_MODE_AUDIO, 2448, MIRAGE_MAIN_AUDIO},
        {"MODE1/2048", MIRAGE_MODE_MODE1, 2048, MIRAGE_MAIN_DATA},
        {"MODE1/2352", MIRAGE_MODE_MODE1, 2352, MIRAGE_MAIN_DATA},
        /* Not sure about the following ones, but MIXED should take care of them */
        {"MODE2/2336", MIRAGE_MODE_MODE2_MIXED, 2336, MIRAGE_MAIN_DATA},
        {"MODE2/2352", MIRAGE_MODE_MODE2_MIXED, 2352, MIRAGE_MAIN_DATA},
        {"CDI/2336",   MIRAGE_MODE_MODE2_MIXED, 2336, MIRAGE_MAIN_DATA},
        {"CDI/2352",   MIRAGE_MODE_MODE2_MIXED, 2352, MIRAGE_MAIN_DATA},
    };

    for (i = 0; i < G_N_ELEMENTS(track_modes); i++) {
        if (!g_strcmp0(track_modes[i].str, mode_string)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track mode: %s\n", __debug__, track_modes[i].str);
            /* Set track mode */
            mirage_track_set_mode(self->priv->cur_track, track_modes[i].mode);
            /* Set current sector size and format */
            self->priv->cur_data_sectsize = track_modes[i].sectsize;
            self->priv->cur_data_format = track_modes[i].format;
            break;
        }
    }

    if (i == G_N_ELEMENTS(track_modes)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: invalid track mode string: %s!\n", __debug__, mode_string);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Invalid track mode string '%s'!", mode_string);
        return FALSE;
    }

    /* Reset parser info on current track */
    self->priv->cur_pregap_set = FALSE;

    return TRUE;
};

static gboolean mirage_parser_cue_add_index (MirageParserCue *self, gint number, gint address, GError **error)
{
    /* Current track needs to be set at this point */
    if (!self->priv->cur_track) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: current track is not set!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Current track is not set!");
        return FALSE;
    }

    /* Both indexes 0 and 1 can mean we have start of a track... if there's 0,
       we have a pregap, if there's just 1, we don't have pregap */
    if (number == 0 || number == 1) {
        /* If index is 0, mark that we have a pregap */
        if (number == 0) {
            self->priv->cur_pregap_set = TRUE;
        }

        if (number == 1 && self->priv->cur_pregap_set) {
            /* If we have a pregap and this is index 1, we just need to
               set the address where the track really starts */
            gint track_start = mirage_track_get_track_start(self->priv->cur_track);
            track_start += address - self->priv->cur_track_start;
            mirage_track_set_track_start(self->priv->cur_track, track_start);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track with pregap; setting track start to 0x%X\n", __debug__, track_start);
        } else {
            /* Otherwise, we need to set up fragment... beginning with the
               last fragment of the previous track (which we might need to
               set length to) */
            if (!self->priv->prev_track) {
                /* This is first track on the disc; first track doesn't seem to
                   have index 0, so if its index 1 is non-zero, it indicates pregap */
                if (number == 1 && address != 0) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: first track has pregap; setting track start to 0x%X\n", __debug__, address);
                    mirage_track_set_track_start(self->priv->cur_track, address);
                    /* address is used later to determine the offset within track file;
                       in this case, we need to reset it to 0, as pregap seems to be
                       included in the track file */
                    address = 0;
                }
            } else {
                /* Get previous track's fragment to set its length */
                MirageFragment *fragment = mirage_track_get_fragment_by_index(self->priv->prev_track, -1, NULL);

                if (fragment) {
                    gint fragment_length = mirage_fragment_get_length(fragment);

                    /* If length is not set, we need to calculate it now; the
                       length will be already set if file has changed in between
                       or anything else happened that might've resulted in call
                       of mirage_session_cue_finish_last_track() */
                    if (!fragment_length) {
                        fragment_length = address - self->priv->cur_track_start;

                        /* In case we're dealing with UltraISO/IsoBuster's
                           multisession, we need this because index addresses
                           differences includes the leadout length */
                        if (self->priv->leadout_correction) {
                            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: using leadout correction %d\n", __debug__, self->priv->leadout_correction);
                            fragment_length -= self->priv->leadout_correction;
                            self->priv->leadout_correction = 0;
                        }

                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: previous fragment length determined to be: %i\n", __debug__, fragment_length);
                        mirage_fragment_set_length(fragment, fragment_length);

                        /* Since sector size can vary between the tracks,
                           we need to keep track of binary offset within
                           the file... */
                        gint main_size = mirage_fragment_main_data_get_size(fragment);
                        gint subchannel_size = mirage_fragment_subchannel_data_get_size(fragment);

                        self->priv->binary_offset += fragment_length * (main_size + subchannel_size);
                    } else {
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: previous fragment already has length (%i)\n", __debug__, fragment_length);
                    }

                    g_object_unref(fragment);
                }
            }

            /* Now current track; we only create fragment here and set its offset */
            MirageFragment *fragment;
            GInputStream *data_stream = mirage_contextual_create_file_stream(MIRAGE_CONTEXTUAL(self), self->priv->cur_data_filename, error);
            if (!data_stream) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create data stream on data file: %s!\n", __debug__, self->priv->cur_data_filename);
                return FALSE;
            }

            if (!g_strcmp0(self->priv->cur_data_type, "BINARY")) {
                /* Binary data */
                gint main_size = 0;
                gint subchannel_size = 0;

                /* Take into account possibility of having subchannel
                   (only for CD+G tracks, though) */
                if (self->priv->cur_data_sectsize == 2448) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: subchannel data present...\n", __debug__);
                    main_size = 2352;
                    subchannel_size = 96;
                } else {
                    main_size = self->priv->cur_data_sectsize;
                }

                fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL);

                mirage_fragment_main_data_set_stream(fragment, data_stream);
                mirage_fragment_main_data_set_size(fragment, main_size);
                mirage_fragment_main_data_set_offset(fragment, self->priv->binary_offset);
                mirage_fragment_main_data_set_format(fragment, self->priv->cur_data_format);

                if (subchannel_size) {
                    mirage_fragment_subchannel_data_set_size(fragment, subchannel_size);
                    /* FIXME: what format of subchannel is there anyway? */
                    mirage_fragment_subchannel_data_set_format(fragment, MIRAGE_SUBCHANNEL_PW96_INT | MIRAGE_SUBCHANNEL_INT);
                }
            } else {
                /* Audio data */
                fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL);

                mirage_fragment_main_data_set_stream(fragment, data_stream);
                mirage_fragment_main_data_set_size(fragment, 2352);
                mirage_fragment_main_data_set_offset(fragment, address*2352); /* Offset is equivalent to the address in CUE, times sector size */
                mirage_fragment_main_data_set_format(fragment, MIRAGE_MAIN_AUDIO);
            }

            mirage_track_add_fragment(self->priv->cur_track, -1, fragment);

            /* Store the current address... it is location at which the current
               track starts, and it will be used to calculate fragment's length
               (if file won't change) */
            self->priv->cur_track_start = address;

            g_object_unref(data_stream);
            g_object_unref(fragment);
        }
    } else {
        /* If index >= 2 is given, add it */
        mirage_track_add_index(self->priv->cur_track, address, NULL);
    }

    return TRUE;
}

static gboolean mirage_parser_cue_set_flags (MirageParserCue *self, gint flags, GError **error)
{
    /* Current track needs to be set at this point */
    if (!self->priv->cur_track) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: current track is not set!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Current track is not set!");
        return FALSE;
    }

    mirage_track_set_flags(self->priv->cur_track, flags);

    return TRUE;
}

static gboolean mirage_parser_cue_set_isrc (MirageParserCue *self, const gchar *isrc, GError **error)
{
    /* Current track needs to be set at this point */
    if (!self->priv->cur_track) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: current track is not set!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Current track is not set!");
        return FALSE;
    }

    if (!mirage_helper_validate_isrc(isrc)) {
        return FALSE;
    }

    mirage_track_set_isrc(self->priv->cur_track, isrc);

    return TRUE;
}


static gboolean mirage_parser_cue_add_empty_part (MirageParserCue *self, gint length, GError **error)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding empty part (0x%X)\n", __debug__, length);

    /* Current track needs to be set at this point */
    if (!self->priv->cur_track) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: current track is not set!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Current track is not set!");
        return FALSE;
    }

    /* Prepare NULL fragment */
    MirageFragment *fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL);
    mirage_fragment_set_length(fragment, length);

    /* Add fragment */
    mirage_track_add_fragment(self->priv->cur_track, -1, fragment);
    g_object_unref(fragment);

    return TRUE;
}

static gboolean mirage_parser_cue_add_pregap (MirageParserCue *self, gint length, GError **error)
{
    gint track_start = 0;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding pregap (0x%X)\n", __debug__, length);

    /* Current track needs to be set at this point */
    if (!self->priv->cur_track) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: current track is not set!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Current track is not set!");
        return FALSE;
    }

    /* Add empty part */
    if (!mirage_parser_cue_add_empty_part(self, length, error)) {
        return FALSE;
    }

    /* Adjust track start */
    track_start = mirage_track_get_track_start(self->priv->cur_track);
    track_start += length;
    mirage_track_set_track_start(self->priv->cur_track, track_start);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: readjusted track start to 0x%X (%i)\n", __debug__, track_start, track_start);

    return TRUE;
}

static void mirage_parser_cue_add_session (MirageParserCue *self, gint number)
{
    gint leadout_length = 0;

    /* We've already added first session, so don't bother */
    if (number == 1) {
        return;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding new session\n", __debug__);

    /* Set the lead-out length of current session */
    if (number == 2) {
        leadout_length = 11250; /* Actually, it should be 6750 previous leadout, 4500 current leadin */
    } else {
        leadout_length = 6750; /* Actually, it should be 2250 previous leadout, 4500 current leadin */
    }
    mirage_session_set_leadout_length(self->priv->cur_session, leadout_length);

    /* UltraISO/IsoBuster store leadout data in the binary file. We'll need to
       account for this when we're setting fragment length, which we calculate
       from index addresses... (150 sectors are added to account for pregap,
       which isn't indicated because only index 01 is used) */
    self->priv->leadout_correction = leadout_length + 150;

    /* Add new session, store the pointer but release the reference */
    self->priv->cur_session = g_object_new(MIRAGE_TYPE_SESSION, NULL);
    mirage_disc_add_session_by_index(self->priv->disc, -1, self->priv->cur_session);
    g_object_unref(self->priv->cur_session);

    /* Reset current track */
    self->priv->cur_track = NULL;
}

static gboolean mirage_parser_cue_set_pack_data (MirageParserCue *self, gint pack_type, const gchar *data, GError **error G_GNUC_UNUSED)
{
    MirageLanguage *language;

    /* FIXME: only one language code supported for now */
    gint code = 9;

    if (!self->priv->cur_track) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting pack data for disc; type: 0x%X, data: %s\n", __debug__, pack_type, data);
        language = mirage_session_get_language_by_code(self->priv->cur_session, code, NULL);
        if (!language) {
            language = g_object_new(MIRAGE_TYPE_LANGUAGE, NULL);
            mirage_session_add_language(self->priv->cur_session, code, language, NULL);
        }
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting pack data for track; type: 0x%X, data: %s\n", __debug__, pack_type, data);
        language = mirage_track_get_language_by_code(self->priv->cur_track, code, NULL);
        if (!language) {
            language = g_object_new(MIRAGE_TYPE_LANGUAGE, NULL);
            mirage_track_add_language(self->priv->cur_track, code, language, NULL);
        }
    }

    mirage_language_set_pack_data(language, pack_type, data, strlen(data)+1, NULL);
    g_object_unref(language);

    return TRUE;
}


/**********************************************************************\
 *                       Regex parsing engine                         *
\**********************************************************************/
typedef gboolean (*CUE_RegexCallback) (MirageParserCue *self, GMatchInfo *match_info, GError **error);

typedef struct
{
    GRegex *regex;
    CUE_RegexCallback callback_func;
} CUE_RegexRule;


static gchar *strip_quotes (gchar *str)
{
    gint len = strlen(str);

    /* Due to UTF-8 being multi-byte, we need to deal with string on byte level,
       not character level */

    /* Skip leading quote and trailing quote, but only if both are present */
    if (str[0] == '"' && str[len-1] == '"') {
        return g_strndup(str+1, len-2);
    }

    /* Otherwise copy the string, for consistency */
    return g_strdup(str);
}

static gboolean mirage_parser_cue_callback_session (MirageParserCue *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *number_raw = g_match_info_fetch_named(match_info, "number");
    gint number = g_strtod(number_raw, NULL);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed SESSION: %d\n", __debug__, number);
    mirage_parser_cue_add_session(self, number);

    g_free(number_raw);

    return TRUE;
}

static gboolean mirage_parser_cue_callback_comment (MirageParserCue *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *comment = g_match_info_fetch_named(match_info, "comment");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed COMMENT: %s\n", __debug__, comment);

    g_free(comment);

    return TRUE;
}

static gboolean mirage_parser_cue_callback_cdtext (MirageParserCue *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *filename_raw, *filename;

    filename_raw = g_match_info_fetch_named(match_info, "filename");
    filename = strip_quotes(filename_raw);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed CDTEXT: %s; FIXME: not handled yet!\n", __debug__, filename);

    g_free(filename);
    g_free(filename_raw);

    return TRUE;
}

static gboolean mirage_parser_cue_callback_catalog (MirageParserCue *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *catalog = g_match_info_fetch_named(match_info, "catalog");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed CATALOG: %.13s\n", __debug__, catalog);

    mirage_disc_set_mcn(self->priv->disc, catalog);

    g_free(catalog);

    return TRUE;
}

static gboolean mirage_parser_cue_callback_title (MirageParserCue *self, GMatchInfo *match_info, GError **error)
{
    gboolean succeeded = TRUE;
    gchar *title_raw, *title;

    title_raw = g_match_info_fetch_named(match_info, "title");
    title = strip_quotes(title_raw);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed TITLE: %s\n", __debug__, title);

    succeeded = mirage_parser_cue_set_pack_data(self, MIRAGE_LANGUAGE_PACK_TITLE, title, error);

    g_free(title);
    g_free(title_raw);

    return succeeded;
}

static gboolean mirage_parser_cue_callback_performer (MirageParserCue *self, GMatchInfo *match_info, GError **error)
{
    gboolean succeeded = TRUE;
    gchar *performer_raw, *performer;

    performer_raw = g_match_info_fetch_named(match_info, "performer");
    performer = strip_quotes(performer_raw);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed PERFORMER: %s\n", __debug__, performer);

    succeeded = mirage_parser_cue_set_pack_data(self, MIRAGE_LANGUAGE_PACK_PERFORMER, performer, error);

    g_free(performer);
    g_free(performer_raw);

    return succeeded;
}

static gboolean mirage_parser_cue_callback_songwriter (MirageParserCue *self, GMatchInfo *match_info, GError **error)
{
    gboolean succeeded = TRUE;
    gchar *songwriter_raw, *songwriter;

    songwriter_raw = g_match_info_fetch_named(match_info, "songwriter");
    songwriter = strip_quotes(songwriter_raw);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed SONGWRITER: %s\n", __debug__, songwriter);

    succeeded = mirage_parser_cue_set_pack_data(self, MIRAGE_LANGUAGE_PACK_SONGWRITER, songwriter, error);

    g_free(songwriter);
    g_free(songwriter_raw);

    return succeeded;
}

static gboolean mirage_parser_cue_callback_file (MirageParserCue *self, GMatchInfo *match_info, GError **error)
{
    gboolean succeeded = TRUE;
    gchar *filename_raw, *filename, *type;

    type = g_match_info_fetch_named(match_info, "type");
    filename_raw = g_match_info_fetch_named(match_info, "filename");
    filename = strip_quotes(filename_raw);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n"); /* To make log more readable */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed FILE; filename: %s, type: %s\n", __debug__, filename, type);

    succeeded = mirage_parser_cue_set_new_file(self, filename, type, error);

    g_free(filename);
    g_free(filename_raw);
    g_free(type);

    return succeeded;
}

static gboolean mirage_parser_cue_callback_track (MirageParserCue *self, GMatchInfo *match_info, GError **error)
{
    gboolean succeeded = TRUE;
    gchar *number_raw, *mode_string;
    gint number;

    number_raw = g_match_info_fetch_named(match_info, "number");
    number = g_strtod(number_raw, NULL);
    mode_string = g_match_info_fetch_named(match_info, "type");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n"); /* To make log more readable */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed TRACK; number: %d, mode_string: %s\n", __debug__, number, mode_string);

    succeeded = mirage_parser_cue_add_track(self, number, mode_string, error);

    g_free(mode_string);
    g_free(number_raw);

    return succeeded;
}

static gboolean mirage_parser_cue_callback_isrc (MirageParserCue *self, GMatchInfo *match_info, GError **error)
{
    gboolean succeeded = TRUE;
    gchar *isrc = g_match_info_fetch_named(match_info, "isrc");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed ISRC: %s\n", __debug__, isrc);

    succeeded = mirage_parser_cue_set_isrc(self, isrc, error);

    g_free(isrc);

    return succeeded;
}

static gboolean mirage_parser_cue_callback_index (MirageParserCue *self, GMatchInfo *match_info, GError **error)
 {
    gboolean succeeded = TRUE;
    gchar *number_raw, *address_raw;
    gint number, address;

    number_raw = g_match_info_fetch_named(match_info, "index");
    number = g_strtod(number_raw, NULL);
    address_raw = g_match_info_fetch_named(match_info, "msf");
    address = mirage_helper_msf2lba_str(address_raw, FALSE);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed INDEX; number: %d, address: %s (%d)\n", __debug__, number, address_raw, address);

    succeeded = mirage_parser_cue_add_index(self, number, address, error);

    g_free(address_raw);
    g_free(number_raw);

    return succeeded;
}

static gboolean mirage_parser_cue_callback_pregap (MirageParserCue *self, GMatchInfo *match_info, GError **error)
{
    gboolean succeeded = TRUE;
    gchar *length_raw;
    gint length;

    length_raw = g_match_info_fetch_named(match_info, "msf");
    length = mirage_helper_msf2lba_str(length_raw, FALSE);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed PREGAP; length: %s (%d)\n", __debug__, length_raw, length);

    succeeded = mirage_parser_cue_add_pregap(self, length, error);

    g_free(length_raw);

    return succeeded;
}

static gboolean mirage_parser_cue_callback_postgap (MirageParserCue *self, GMatchInfo *match_info, GError **error)
{
    gboolean succeeded = TRUE;
    gchar *length_raw;
    gint length;

    length_raw = g_match_info_fetch_named(match_info, "msf");
    length = mirage_helper_msf2lba_str(length_raw, FALSE);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed POSTGAP; length: %s (%d)\n", __debug__, g_match_info_fetch_named(match_info, "msf"), length);

    succeeded = mirage_parser_cue_add_empty_part(self, length, error);

    g_free(length_raw);

    return succeeded;
}

static gboolean mirage_parser_cue_callback_flags (MirageParserCue *self, GMatchInfo *match_info, GError **error)
{
    gchar *flags_dcp, *flags_4ch, *flags_pre, *flags_scms;
    gint flags = 0;

    flags_dcp = g_match_info_fetch_named(match_info, "dcp");
    flags_4ch = g_match_info_fetch_named(match_info, "4ch");
    flags_pre = g_match_info_fetch_named(match_info, "pre");
    flags_scms = g_match_info_fetch_named(match_info, "scms");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed FLAGS\n", __debug__);

    if (!g_strcmp0(flags_dcp, "DCP")) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting DCP flag\n", __debug__);
        flags |= MIRAGE_TRACK_FLAG_COPYPERMITTED;
    }
    if (!g_strcmp0(flags_4ch, "4CH")) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting 4CH flag\n", __debug__);
        flags |= MIRAGE_TRACK_FLAG_FOURCHANNEL;
    }
    if (!g_strcmp0(flags_pre, "PRE")) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting PRE flag\n", __debug__);
        flags |= MIRAGE_TRACK_FLAG_PREEMPHASIS;
    }
    if (!g_strcmp0(flags_scms, "SCMS")) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: SCMS flag not handled yet!\n", __debug__);
    }

    g_free(flags_dcp);
    g_free(flags_4ch);
    g_free(flags_pre);
    g_free(flags_scms);

    return mirage_parser_cue_set_flags(self, flags, error);
}


static inline void append_regex_rule (GList **list_ptr, const gchar *rule, CUE_RegexCallback callback)
{
    GList *list = *list_ptr;

    CUE_RegexRule *new_rule = g_new(CUE_RegexRule, 1);
    new_rule->regex = g_regex_new(rule, G_REGEX_OPTIMIZE, 0, NULL);
    new_rule->callback_func = callback;
    /* Append to the list */
    list = g_list_append(list, new_rule);

    *list_ptr = list;
}

static void mirage_parser_cue_init_regex_parser (MirageParserCue *self)
{
    /* Ignore empty lines */
    append_regex_rule(&self->priv->regex_rules, "^\\s*$", NULL);

    /* "Extensions" that are embedded in the comments must appear before general
       comment rule */
    append_regex_rule(&self->priv->regex_rules, "REM\\s+SESSION\\s+(?<number>\\d+)$", mirage_parser_cue_callback_session);

    append_regex_rule(&self->priv->regex_rules, "REM\\s+(?<comment>.+)$", mirage_parser_cue_callback_comment);

    append_regex_rule(&self->priv->regex_rules, "CDTEXMAIN\\s+(?<filename>.+)$", mirage_parser_cue_callback_cdtext);

    append_regex_rule(&self->priv->regex_rules, "CATALOG\\s+(?<catalog>\\d{13})$", mirage_parser_cue_callback_catalog);

    append_regex_rule(&self->priv->regex_rules, "TITLE\\s+(?<title>.+)$", mirage_parser_cue_callback_title);
    append_regex_rule(&self->priv->regex_rules, "PERFORMER\\s+(?<performer>.+)$", mirage_parser_cue_callback_performer);
    append_regex_rule(&self->priv->regex_rules, "SONGWRITER\\s+(?<songwriter>.+)$", mirage_parser_cue_callback_songwriter);

    append_regex_rule(&self->priv->regex_rules, "FILE\\s+(?<filename>.+)\\s+(?<type>\\S+)$", mirage_parser_cue_callback_file);
    append_regex_rule(&self->priv->regex_rules, "TRACK\\s+(?<number>\\d+)\\s+(?<type>\\S+)$", mirage_parser_cue_callback_track);
    append_regex_rule(&self->priv->regex_rules, "ISRC\\s+(?<isrc>\\w{12})$", mirage_parser_cue_callback_isrc);
    append_regex_rule(&self->priv->regex_rules, "INDEX\\s+(?<index>\\d+)\\s+(?<msf>[\\d]+:[\\d]+:[\\d]+)$", mirage_parser_cue_callback_index);

    append_regex_rule(&self->priv->regex_rules, "PREGAP\\s+(?<msf>[\\d]+:[\\d]+:[\\d]+)$", mirage_parser_cue_callback_pregap);
    append_regex_rule(&self->priv->regex_rules, "POSTGAP\\s+(?<msf>[\\d]+:[\\d]+:[\\d]+)$", mirage_parser_cue_callback_postgap);

    append_regex_rule(&self->priv->regex_rules, "FLAGS\\+(((?<dcp>DCP)|(?<4ch>4CH)|(?<pre>PRE)|(?<scms>SCMS))\\s*)+$", mirage_parser_cue_callback_flags);

    return;
}

static void mirage_parser_cue_cleanup_regex_parser (MirageParserCue *self)
{
    for (GList *entry = self->priv->regex_rules; entry; entry = entry->next) {
        CUE_RegexRule *rule = entry->data;
        g_regex_unref(rule->regex);
        g_free(rule);
    }

    g_list_free(self->priv->regex_rules);
}

static gboolean mirage_parser_cue_parse_cue_file (MirageParserCue *self, GInputStream *stream, GError **error)
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

    /* Read file line-by-line */
    for (gint line_number = 1; ; line_number++) {
        GError *local_error = NULL;
        gchar *line_string;
        gsize line_length;

        /* Read line */
        line_string = g_data_input_stream_read_line_utf8(data_stream, &line_length, NULL, &local_error);

        /* Handle error */
        if (!line_string) {
            if (!local_error) {
                /* EOF */
                break;
            } else {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read line #%d: %s\n", __debug__, line_number, local_error->message);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, "Failed to read line #%d: %s!", line_number, local_error->message);
                g_error_free(local_error);
                succeeded = FALSE;
                break;
            }
        }

        /* GRegex matching engine */
        GMatchInfo *match_info = NULL;
        gboolean matched = FALSE;

        /* Go over all matching rules */
        for (GList *entry = self->priv->regex_rules; entry; entry = entry->next) {
            CUE_RegexRule *regex_rule = entry->data;

            /* Try to match the given rule */
            if (g_regex_match(regex_rule->regex, line_string, 0, &match_info)) {
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
 *                 MirageParser methods implementation               *
\**********************************************************************/
static MirageDisc *mirage_parser_cue_load_image (MirageParser *_self, GInputStream **streams, GError **error)
{
    MirageParserCue *self = MIRAGE_PARSER_CUE(_self);

    gboolean succeeded = TRUE;

    /* Check if we can load the file; we check the suffix */
    self->priv->cue_filename = mirage_contextual_get_file_stream_filename(MIRAGE_CONTEXTUAL(self), streams[0]);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: checking if parser can handle given image...\n", __debug__);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: verifying image file's suffix...\n", __debug__);
    if (!mirage_helper_has_suffix(self->priv->cue_filename, ".cue")) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: parser cannot handle given image: invalid suffix (not a *.cue file!)!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Parser cannot handle given image: invalid suffix!");
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: parser can handle given image!\n", __debug__);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing the image...\n", __debug__);

    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_set_parent(MIRAGE_OBJECT(self->priv->disc), self);

    mirage_disc_set_filename(self->priv->disc, self->priv->cue_filename);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CUE filename: %s\n", __debug__, self->priv->cue_filename);

    /* First session is created manually (in case we're dealing with normal
       CUE file, which doesn't have session definitions anyway); note that we
       store only pointer, but release reference */
    self->priv->cur_session = g_object_new(MIRAGE_TYPE_SESSION, NULL);
    mirage_disc_add_session_by_index(self->priv->disc, -1, self->priv->cur_session);
    g_object_unref(self->priv->cur_session);

    /* Parse the CUE */
    if (!mirage_parser_cue_parse_cue_file(self, streams[0], error)) {
        succeeded = FALSE;
        goto end;
    }

    /* Finish last track */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finishing last track in the layout\n", __debug__);
    if (!mirage_parser_cue_finish_last_track(self, error)) {
        succeeded = FALSE;
        goto end;
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
G_DEFINE_DYNAMIC_TYPE(MirageParserCue, mirage_parser_cue, MIRAGE_TYPE_PARSER);

void mirage_parser_cue_type_register (GTypeModule *type_module)
{
    return mirage_parser_cue_register_type(type_module);
}


static void mirage_parser_cue_init (MirageParserCue *self)
{
    self->priv = MIRAGE_PARSER_CUE_GET_PRIVATE(self);

    mirage_parser_generate_info(MIRAGE_PARSER(self),
        "PARSER-CUE",
        "CUE Image Parser",
        1,
        "CUE images (*.cue)", "application/x-cue"
    );

    mirage_parser_cue_init_regex_parser(self);

    self->priv->cur_data_filename = NULL;
    self->priv->cur_data_type = NULL;
}

static void mirage_parser_cue_finalize (GObject *gobject)
{
    MirageParserCue *self = MIRAGE_PARSER_CUE(gobject);

    /* Free elements of private structure */
    g_free(self->priv->cur_data_filename);
    g_free(self->priv->cur_data_type);

    /* Cleanup regex parser engine */
    mirage_parser_cue_cleanup_regex_parser(self);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_parser_cue_parent_class)->finalize(gobject);
}

static void mirage_parser_cue_class_init (MirageParserCueClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageParserClass *parser_class = MIRAGE_PARSER_CLASS(klass);

    gobject_class->finalize = mirage_parser_cue_finalize;

    parser_class->load_image = mirage_parser_cue_load_image;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageParserCuePrivate));
}

static void mirage_parser_cue_class_finalize (MirageParserCueClass *klass G_GNUC_UNUSED)
{
}
