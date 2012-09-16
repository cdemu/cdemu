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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "image-cue.h"

#define __debug__ "CUE-Parser"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_PARSER_CUE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_CUE, MIRAGE_Parser_CUEPrivate))

struct _MIRAGE_Parser_CUEPrivate
{
    GObject *disc;

    gchar *cue_filename;

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
    GObject *cur_session;
    GObject *cur_track;
    GObject *prev_track;

    /* Regex engine */
    GList *regex_rules;
};


/**********************************************************************\
 *                     Parser private functions                       *
\**********************************************************************/
static gboolean mirage_parser_cue_finish_last_track (MIRAGE_Parser_CUE *self, GError **error)
{
    GObject *fragment;
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
    fragment = mirage_track_get_fragment_by_index(MIRAGE_TRACK(self->priv->cur_track), -1, NULL);
    if (fragment) {
        mirage_fragment_use_the_rest_of_file(MIRAGE_FRAGMENT(fragment), NULL);

        if (mirage_fragment_get_length(MIRAGE_FRAGMENT(fragment)) < 0) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: finishing last track resulted in negative fragment length!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Finishing last track resulted in negative fragment length!");
            succeeded = FALSE;
        }


        g_object_unref(fragment);
    }

    return succeeded;
}

static gboolean mirage_parser_cue_set_new_file (MIRAGE_Parser_CUE *self, gchar *filename_string, gchar *file_type, GError **error)
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

static gboolean mirage_parser_cue_add_track (MIRAGE_Parser_CUE *self, gint number, gchar *mode_string, GError **error)
{
    gint i;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding track %d\n", __debug__, number);

    /* Current track becomes previous one */
    self->priv->prev_track = self->priv->cur_track;

    /* Add new track, store the pointer and release reference */
    self->priv->cur_track = g_object_new(MIRAGE_TYPE_TRACK, NULL);
    if (!mirage_session_add_track_by_number(MIRAGE_SESSION(self->priv->cur_session), number, self->priv->cur_track, error)) {
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
        {"AUDIO",      MIRAGE_MODE_AUDIO, 2352, FR_BIN_TFILE_AUDIO},
        {"CDG",        MIRAGE_MODE_AUDIO, 2448, FR_BIN_TFILE_AUDIO},
        {"MODE1/2048", MIRAGE_MODE_MODE1, 2048, FR_BIN_TFILE_DATA},
        {"MODE1/2352", MIRAGE_MODE_MODE1, 2352, FR_BIN_TFILE_DATA},
        /* Not sure about the following ones, but MIXED should take care of them */
        {"MODE2/2336", MIRAGE_MODE_MODE2_MIXED, 2336, FR_BIN_TFILE_DATA},
        {"MODE2/2352", MIRAGE_MODE_MODE2_MIXED, 2352, FR_BIN_TFILE_DATA},
        {"CDI/2336",   MIRAGE_MODE_MODE2_MIXED, 2336, FR_BIN_TFILE_DATA},
        {"CDI/2352",   MIRAGE_MODE_MODE2_MIXED, 2352, FR_BIN_TFILE_DATA},
    };

    for (i = 0; i < G_N_ELEMENTS(track_modes); i++) {
        if (!g_strcmp0(track_modes[i].str, mode_string)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track mode: %s\n", __debug__, track_modes[i].str);
            /* Set track mode */
            mirage_track_set_mode(MIRAGE_TRACK(self->priv->cur_track), track_modes[i].mode);
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

static gboolean mirage_parser_cue_add_index (MIRAGE_Parser_CUE *self, gint number, gint address, GError **error)
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
            gint track_start = mirage_track_get_track_start(MIRAGE_TRACK(self->priv->cur_track));
            track_start += address - self->priv->cur_track_start;
            mirage_track_set_track_start(MIRAGE_TRACK(self->priv->cur_track), track_start);
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
                    mirage_track_set_track_start(MIRAGE_TRACK(self->priv->cur_track), address);
                    /* address is used later to determine the offset within track file;
                       in this case, we need to reset it to 0, as pregap seems to be
                       included in the track file */
                    address = 0;
                }
            } else {
                /* Get previous track's fragment to set its length */
                GObject *fragment = mirage_track_get_fragment_by_index(MIRAGE_TRACK(self->priv->prev_track), -1, NULL);

                if (fragment) {
                    gint fragment_length = mirage_fragment_get_length(MIRAGE_FRAGMENT(fragment));

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
                        mirage_fragment_set_length(MIRAGE_FRAGMENT(fragment), fragment_length);

                        /* Binary fragments/files are pain because sector size can
                           vary between the tracks; so in case we're dealing with
                           binary, we need to keep track of the offset within file */
                        if (MIRAGE_IS_FRAG_IFACE_BINARY(fragment)) {
                            gint tfile_sectsize = mirage_frag_iface_binary_track_file_get_sectsize(MIRAGE_FRAG_IFACE_BINARY(fragment));
                            gint sfile_sectsize = mirage_frag_iface_binary_subchannel_file_get_sectsize(MIRAGE_FRAG_IFACE_BINARY(fragment));

                            self->priv->binary_offset += fragment_length * (tfile_sectsize + sfile_sectsize);
                        }
                    } else {
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: previous fragment already has length (%i)\n", __debug__, fragment_length);
                    }

                    g_object_unref(fragment);
                }
            }

            /* Now current track; we only create fragment here and set its offset */
            GObject *fragment;
            GObject *data_stream = mirage_parser_get_cached_data_stream(MIRAGE_PARSER(self), self->priv->cur_data_filename, error);
            if (!data_stream) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create data strean on data file: %s!\n", __debug__, self->priv->cur_data_filename);
                return FALSE;
            }

            if (!g_strcmp0(self->priv->cur_data_type, "BINARY")) {
                /* Binary data; we'll request fragment with BINARY interface... */
                gint tfile_sectsize = 0;
                gint sfile_sectsize = 0;

                /* Take into account possibility of having subchannel
                   (only for CD+G tracks, though) */
                if (self->priv->cur_data_sectsize == 2448) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: subchannel data present...\n", __debug__);
                    tfile_sectsize = 2352;
                    sfile_sectsize = 96;
                } else {
                    tfile_sectsize = self->priv->cur_data_sectsize;
                }

                fragment = libmirage_create_fragment(MIRAGE_TYPE_FRAG_IFACE_BINARY, data_stream, G_OBJECT(self), error);
                if (!fragment) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create data fragment!\n", __debug__);
                    g_object_unref(data_stream);
                    return FALSE;
                }

                if (!mirage_frag_iface_binary_track_file_set_file(MIRAGE_FRAG_IFACE_BINARY(fragment), self->priv->cur_data_filename, data_stream, error)) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set track data file!\n", __debug__);
                    g_object_unref(data_stream);
                    g_object_unref(fragment);
                    return FALSE;
                }
                mirage_frag_iface_binary_track_file_set_sectsize(MIRAGE_FRAG_IFACE_BINARY(fragment), tfile_sectsize);
                mirage_frag_iface_binary_track_file_set_offset(MIRAGE_FRAG_IFACE_BINARY(fragment), self->priv->binary_offset);
                mirage_frag_iface_binary_track_file_set_format(MIRAGE_FRAG_IFACE_BINARY(fragment), self->priv->cur_data_format);

                if (sfile_sectsize) {
                    mirage_frag_iface_binary_subchannel_file_set_sectsize(MIRAGE_FRAG_IFACE_BINARY(fragment), sfile_sectsize);
                    /* FIXME: what format of subchannel is there anyway? */
                    mirage_frag_iface_binary_subchannel_file_set_format(MIRAGE_FRAG_IFACE_BINARY(fragment), FR_BIN_SFILE_PW96_INT | FR_BIN_SFILE_INT);
                }
            } else {
                /* One of the audio files; we'll request fragment with AUDIO
                   interface and hope Mirage finds one that can handle the file
                   for us */
                fragment = libmirage_create_fragment(MIRAGE_TYPE_FRAG_IFACE_AUDIO, data_stream, G_OBJECT(self), error);
                if (!fragment) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown/unsupported file type: %s\n", __debug__, self->priv->cur_data_type);
                    g_object_unref(data_stream);
                    return FALSE;
                }

                if (!mirage_frag_iface_audio_set_file(MIRAGE_FRAG_IFACE_AUDIO(fragment), self->priv->cur_data_filename, data_stream, error)) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set track data file!\n", __debug__);
                    g_object_unref(data_stream);
                    g_object_unref(fragment);
                    return FALSE;
                }
                /* Offset in audio file is equivalent to current address in CUE */
                mirage_frag_iface_audio_set_offset(MIRAGE_FRAG_IFACE_AUDIO(fragment), address);
            }

            mirage_track_add_fragment(MIRAGE_TRACK(self->priv->cur_track), -1, fragment);

            /* Store the current address... it is location at which the current
               track starts, and it will be used to calculate fragment's length
               (if file won't change) */
            self->priv->cur_track_start = address;

            g_object_unref(data_stream);
            g_object_unref(fragment);
        }
    } else {
        /* If index >= 2 is given, add it */
        mirage_track_add_index(MIRAGE_TRACK(self->priv->cur_track), address, NULL);
    }

    return TRUE;
}

static gboolean mirage_parser_cue_set_flags (MIRAGE_Parser_CUE *self, gint flags, GError **error)
{
    /* Current track needs to be set at this point */
    if (!self->priv->cur_track) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: current track is not set!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Current track is not set!");
        return FALSE;
    }

    mirage_track_set_flags(MIRAGE_TRACK(self->priv->cur_track), flags);

    return TRUE;
}

static gboolean mirage_parser_cue_set_isrc (MIRAGE_Parser_CUE *self, gchar *isrc, GError **error)
{
    /* Current track needs to be set at this point */
    if (!self->priv->cur_track) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: current track is not set!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Current track is not set!");
        return FALSE;
    }

    mirage_track_set_isrc(MIRAGE_TRACK(self->priv->cur_track), isrc);

    return TRUE;
}


static gboolean mirage_parser_cue_add_empty_part (MIRAGE_Parser_CUE *self, gint length, GError **error)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding empty part (0x%X)\n", __debug__, length);

    /* Current track needs to be set at this point */
    if (!self->priv->cur_track) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: current track is not set!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Current track is not set!");
        return FALSE;
    }

    /* Prepare NULL fragment - creation should never fail */
    GObject *fragment = libmirage_create_fragment(MIRAGE_TYPE_FRAG_IFACE_NULL, NULL, G_OBJECT(self), NULL);
    mirage_fragment_set_length(MIRAGE_FRAGMENT(fragment), length);

    /* Add fragment */
    mirage_track_add_fragment(MIRAGE_TRACK(self->priv->cur_track), -1, fragment);
    g_object_unref(fragment);

    return TRUE;
}

static gboolean mirage_parser_cue_add_pregap (MIRAGE_Parser_CUE *self, gint length, GError **error)
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
    track_start = mirage_track_get_track_start(MIRAGE_TRACK(self->priv->cur_track));
    track_start += length;
    mirage_track_set_track_start(MIRAGE_TRACK(self->priv->cur_track), track_start);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: readjusted track start to 0x%X (%i)\n", __debug__, track_start, track_start);

    return TRUE;
}

static void mirage_parser_cue_add_session (MIRAGE_Parser_CUE *self, gint number)
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
    mirage_session_set_leadout_length(MIRAGE_SESSION(self->priv->cur_session), leadout_length);

    /* UltraISO/IsoBuster store leadout data in the binary file. We'll need to
       account for this when we're setting fragment length, which we calculate
       from index addresses... (150 sectors are added to account for pregap,
       which isn't indicated because only index 01 is used) */
    self->priv->leadout_correction = leadout_length + 150;

    /* Add new session, store the pointer but release the reference */
    self->priv->cur_session = g_object_new(MIRAGE_TYPE_SESSION, NULL);
    mirage_disc_add_session_by_index(MIRAGE_DISC(self->priv->disc), -1, self->priv->cur_session);
    g_object_unref(self->priv->cur_session);

    /* Reset current track */
    self->priv->cur_track = NULL;
}

static gboolean mirage_parser_cue_set_pack_data (MIRAGE_Parser_CUE *self, gint pack_type, gchar *data, GError **error G_GNUC_UNUSED)
{
    GObject *language;

    /* FIXME: only one language code supported for now */
    gint langcode = 9;

    if (!self->priv->cur_track) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting pack data for disc; type: 0x%X, data: %s\n", __debug__, pack_type, data);
        language = mirage_session_get_language_by_code(MIRAGE_SESSION(self->priv->cur_session), langcode, NULL);
        if (!language) {
            language = g_object_new(MIRAGE_TYPE_LANGUAGE, NULL);
            mirage_session_add_language(MIRAGE_SESSION(self->priv->cur_session), langcode, language, NULL);
        }
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting pack data for track; type: 0x%X, data: %s\n", __debug__, pack_type, data);
        language = mirage_track_get_language_by_code(MIRAGE_TRACK(self->priv->cur_track), langcode, NULL);
        if (!language) {
            language = g_object_new(MIRAGE_TYPE_LANGUAGE, NULL);
            mirage_track_add_language(MIRAGE_TRACK(self->priv->cur_track), langcode, language, NULL);
        }
    }

    mirage_language_set_pack_data(MIRAGE_LANGUAGE(language), pack_type, data, strlen(data)+1, NULL);
    g_object_unref(language);

    return TRUE;
}


/**********************************************************************\
 *                       Regex parsing engine                         *
\**********************************************************************/
typedef gboolean (*CUE_RegexCallback) (MIRAGE_Parser_CUE *self, GMatchInfo *match_info, GError **error);

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

static gboolean mirage_parser_cue_callback_session (MIRAGE_Parser_CUE *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *number_raw = g_match_info_fetch_named(match_info, "number");
    gint number = g_strtod(number_raw, NULL);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed SESSION: %d\n", __debug__, number);
    mirage_parser_cue_add_session(self, number);

    g_free(number_raw);

    return TRUE;
}

static gboolean mirage_parser_cue_callback_comment (MIRAGE_Parser_CUE *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *comment = g_match_info_fetch_named(match_info, "comment");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed COMMENT: %s\n", __debug__, comment);

    g_free(comment);

    return TRUE;
}

static gboolean mirage_parser_cue_callback_cdtext (MIRAGE_Parser_CUE *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *filename_raw, *filename;

    filename_raw = g_match_info_fetch_named(match_info, "filename");
    filename = strip_quotes(filename_raw);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed CDTEXT: %s; FIXME: not handled yet!\n", __debug__, filename);

    g_free(filename);
    g_free(filename_raw);

    return TRUE;
}

static gboolean mirage_parser_cue_callback_catalog (MIRAGE_Parser_CUE *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *catalog = g_match_info_fetch_named(match_info, "catalog");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed CATALOG: %.13s\n", __debug__, catalog);

    mirage_disc_set_mcn(MIRAGE_DISC(self->priv->disc), catalog);

    g_free(catalog);

    return TRUE;
}

static gboolean mirage_parser_cue_callback_title (MIRAGE_Parser_CUE *self, GMatchInfo *match_info, GError **error)
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

static gboolean mirage_parser_cue_callback_performer (MIRAGE_Parser_CUE *self, GMatchInfo *match_info, GError **error)
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

static gboolean mirage_parser_cue_callback_songwriter (MIRAGE_Parser_CUE *self, GMatchInfo *match_info, GError **error)
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

static gboolean mirage_parser_cue_callback_file (MIRAGE_Parser_CUE *self, GMatchInfo *match_info, GError **error)
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

static gboolean mirage_parser_cue_callback_track (MIRAGE_Parser_CUE *self, GMatchInfo *match_info, GError **error)
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

static gboolean mirage_parser_cue_callback_isrc (MIRAGE_Parser_CUE *self, GMatchInfo *match_info, GError **error)
{
    gboolean succeeded = TRUE;
    gchar *isrc = g_match_info_fetch_named(match_info, "isrc");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed ISRC: %s\n", __debug__, isrc);

    succeeded = mirage_parser_cue_set_isrc(self, isrc, error);

    g_free(isrc);

    return succeeded;
}

static gboolean mirage_parser_cue_callback_index (MIRAGE_Parser_CUE *self, GMatchInfo *match_info, GError **error)
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

static gboolean mirage_parser_cue_callback_pregap (MIRAGE_Parser_CUE *self, GMatchInfo *match_info, GError **error)
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

static gboolean mirage_parser_cue_callback_postgap (MIRAGE_Parser_CUE *self, GMatchInfo *match_info, GError **error)
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

static gboolean mirage_parser_cue_callback_flags (MIRAGE_Parser_CUE *self, GMatchInfo *match_info, GError **error)
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
        flags |= MIRAGE_TRACKF_COPYPERMITTED;
    }
    if (!g_strcmp0(flags_4ch, "4CH")) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting 4CH flag\n", __debug__);
        flags |= MIRAGE_TRACKF_FOURCHANNEL;
    }
    if (!g_strcmp0(flags_pre, "PRE")) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting PRE flag\n", __debug__);
        flags |= MIRAGE_TRACKF_PREEMPHASIS;
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

static void mirage_parser_cue_init_regex_parser (MIRAGE_Parser_CUE *self)
{
    /* Ignore empty lines */
    append_regex_rule(&self->priv->regex_rules, "^\\s*$", NULL);

    /* "Extensions" that are embedded in the comments must appear before general
       comment rule */
    append_regex_rule(&self->priv->regex_rules, "REM\\s+SESSION\\s+(?<number>\\d+)$", mirage_parser_cue_callback_session);

    append_regex_rule(&self->priv->regex_rules, "REM\\s+(?<comment>.+)$", mirage_parser_cue_callback_comment);

    append_regex_rule(&self->priv->regex_rules, "CDTEXTFILE\\s+(?<filename>.+)$", mirage_parser_cue_callback_cdtext);

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

static void mirage_parser_cue_cleanup_regex_parser (MIRAGE_Parser_CUE *self)
{
    GList *entry;

    G_LIST_FOR_EACH(entry, self->priv->regex_rules) {
        CUE_RegexRule *rule = entry->data;
        g_regex_unref(rule->regex);
        g_free(rule);
    }

    g_list_free(self->priv->regex_rules);
}

static gboolean mirage_parser_cue_detect_and_set_encoding (MIRAGE_Parser_CUE *self, GIOChannel *io_channel, GError **error G_GNUC_UNUSED)
{
    static gchar bom_utf32_be[] = { 0x00, 0x00, 0xFE, 0xFF };
    static gchar bom_utf32_le[] = { 0xFF, 0xFE, 0x00, 0x00 };
    static gchar bom_utf16_be[] = { 0xFE, 0xFF };
    static gchar bom_utf16_le[] = { 0xFF, 0xFE };

    gchar bom[4] = "";

    /* Set position at the beginning, and set encoding to NULL (raw bytes) */
    g_io_channel_seek_position(io_channel, 0, G_SEEK_SET, NULL);
    g_io_channel_set_encoding(io_channel, NULL, NULL);

    /* Read first four bytes */
    g_io_channel_read_chars(io_channel, bom, sizeof(bom), NULL, NULL);

    /* Reset the position */
    g_io_channel_seek_position(io_channel, 0, G_SEEK_SET, NULL);

    /* Set the encoding */
    if (!memcmp(bom, bom_utf32_be, sizeof(bom_utf32_be))) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: UTF-32 BE BOM found\n", __debug__);
        g_io_channel_set_encoding(io_channel, "utf-32be", NULL);
    } else if (!memcmp(bom, bom_utf32_le, sizeof(bom_utf32_le))) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: UTF-32 LE BOM found\n", __debug__);
        g_io_channel_set_encoding(io_channel, "utf-32le", NULL);
    } else if (!memcmp(bom, bom_utf16_be, sizeof(bom_utf16_be))) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: UTF-16 BE BOM found\n", __debug__);
        g_io_channel_set_encoding(io_channel, "utf-16be", NULL);
    } else if (!memcmp(bom, bom_utf16_le, sizeof(bom_utf16_le))) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: UTF-16 LE BOM found\n", __debug__);
        g_io_channel_set_encoding(io_channel, "utf-16le", NULL);
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: no BOM found, assuming UTF-8\n", __debug__);
        g_io_channel_set_encoding(io_channel, "utf-8", NULL);
    }

    return TRUE;
}

static gboolean mirage_parser_cue_parse_cue_file (MIRAGE_Parser_CUE *self, gchar *filename, GError **error)
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

    /* If provided, use the specified encoding; otherwise, try to detect it */
    const gchar *encoding = mirage_parser_get_param_string(MIRAGE_PARSER(self), "encoding");;
    if (encoding) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: using specified encoding: %s\n", __debug__, encoding);
        g_io_channel_set_encoding(io_channel, encoding, NULL);
    } else {
        mirage_parser_cue_detect_and_set_encoding(self, io_channel, NULL);
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing\n", __debug__);

    /* Read file line-by-line */
    for (gint line_nr = 1; ; line_nr++) {
        GIOStatus status;
        gchar *line_str;
        gsize line_len;

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

        /* GRegex matching engine */
        GMatchInfo *match_info = NULL;
        gboolean matched = FALSE;
        GList *entry;

        /* Go over all matching rules */
        G_LIST_FOR_EACH(entry, self->priv->regex_rules) {
            CUE_RegexRule *regex_rule = entry->data;

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
 *                 MIRAGE_Parser methods implementation               *
\**********************************************************************/
static GObject *mirage_parser_cue_load_image (MIRAGE_Parser *_self, gchar **filenames, GError **error)
{
    MIRAGE_Parser_CUE *self = MIRAGE_PARSER_CUE(_self);

    gboolean succeeded = TRUE;

    /* Check if we can load the file; we check the suffix */
    if (!mirage_helper_has_suffix(filenames[0], ".cue")) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Parser cannot handle given image!");
        return FALSE;
    }

    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_attach_child(MIRAGE_OBJECT(self), self->priv->disc);

    mirage_disc_set_filename(MIRAGE_DISC(self->priv->disc), filenames[0]);
    self->priv->cue_filename = g_strdup(filenames[0]);

    /* First session is created manually (in case we're dealing with normal
       CUE file, which doesn't have session definitions anyway); note that we
       store only pointer, but release reference */
    self->priv->cur_session = g_object_new(MIRAGE_TYPE_SESSION, NULL);
    mirage_disc_add_session_by_index(MIRAGE_DISC(self->priv->disc), -1, self->priv->cur_session);
    g_object_unref(self->priv->cur_session);

    /* Parse the CUE */
    if (!mirage_parser_cue_parse_cue_file(self, self->priv->cue_filename, error)) {
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
G_DEFINE_DYNAMIC_TYPE(MIRAGE_Parser_CUE, mirage_parser_cue, MIRAGE_TYPE_PARSER);

void mirage_parser_cue_type_register (GTypeModule *type_module)
{
    return mirage_parser_cue_register_type(type_module);
}


static void mirage_parser_cue_init (MIRAGE_Parser_CUE *self)
{
    self->priv = MIRAGE_PARSER_CUE_GET_PRIVATE(self);

    mirage_parser_generate_parser_info(MIRAGE_PARSER(self),
        "PARSER-CUE",
        "CUE Image Parser",
        "CUE images",
        "application/x-cue"
    );

    mirage_parser_cue_init_regex_parser(self);

    self->priv->cue_filename = NULL;
    self->priv->cur_data_filename = NULL;
    self->priv->cur_data_type = NULL;
}

static void mirage_parser_cue_finalize (GObject *gobject)
{
    MIRAGE_Parser_CUE *self = MIRAGE_PARSER_CUE(gobject);

    /* Free elements of private structure */
    g_free(self->priv->cue_filename);
    g_free(self->priv->cur_data_filename);
    g_free(self->priv->cur_data_type);

    /* Cleanup regex parser engine */
    mirage_parser_cue_cleanup_regex_parser(self);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_parser_cue_parent_class)->finalize(gobject);
}

static void mirage_parser_cue_class_init (MIRAGE_Parser_CUEClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MIRAGE_ParserClass *parser_class = MIRAGE_PARSER_CLASS(klass);

    gobject_class->finalize = mirage_parser_cue_finalize;

    parser_class->load_image = mirage_parser_cue_load_image;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Parser_CUEPrivate));
}

static void mirage_parser_cue_class_finalize (MIRAGE_Parser_CUEClass *klass G_GNUC_UNUSED)
{
}
