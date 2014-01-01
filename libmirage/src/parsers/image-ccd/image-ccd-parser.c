/*
 *  libMirage: CCD image parser: Parser object
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

#include "image-ccd.h"

#define __debug__ "CCD-Parser"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_PARSER_CCD_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_CCD, MirageParserCcdPrivate))

struct _MirageParserCcdPrivate
{
    MirageDisc *disc;

    /* Data and subchannel filenames */
    gchar *img_filename;
    GInputStream *img_stream;

    gchar *sub_filename;
    GInputStream *sub_stream;

    /* Offset within data/subchannel file */
    gint offset;

    /* Parsed data */
    CCD_CloneCD *header;
    CCD_Disc *disc_data;
    GList *sessions_list;
    GList *entries_list;

    /* CDText */
    gint cdtext_entries;
    guint8 *cdtext_data;

    /* Regex engine */
    gpointer cur_data;
    GList *cur_rules;

    GList *regex_rules;
    GList *regex_rules_clonecd;
    GList *regex_rules_disc;
    GList *regex_rules_session;
    GList *regex_rules_entry;
    GList *regex_rules_track;
    GList *regex_rules_cdtext;
};


/**********************************************************************\
 *                     Parser private functions                       *
\**********************************************************************/
static gint find_redundant_entries (CCD_Entry *entry, gconstpointer not_used G_GNUC_UNUSED)
{
    return ((entry->Point > 0 && entry->Point < 99) || entry->Point == 0xA0 || entry->Point == 0xA2);
}

static gint sort_entries (CCD_Entry *entry1, CCD_Entry *entry2)
{
    /* We sort entries by session; then, we put 0xA0 before 1-99, and we put
       0xA2 at the end. NOTE: the function compares newly added entry1 to already
       existing entry2... */
    if (entry2->Session == entry1->Session) {
        if (entry1->Point == 0xA0) {
            /* Put entry1 (0xA0) before entry2 */
            return -1;
        } else if (entry1->Point == 0xA2) {
            /* Put entry1 (0xA2) after entry2 */
            return 1;
        } else {
            return (entry1->Point < entry2->Point)*(-1) + (entry1->Point > entry2->Point)*(1);
        }
    } else {
        return (entry1->Session < entry2->Session)*(-1) + (entry1->Session > entry2->Session)*(1);
    }
}

static void mirage_parser_ccd_sort_entries (MirageParserCcd *self)
{
    GList *entry;

    /* First, remove the entries that we won't need */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: removing redundant entries\n", __debug__);
    entry = g_list_find_custom(self->priv->entries_list, NULL, (GCompareFunc)find_redundant_entries);
    while (entry) {
        CCD_Entry *data = entry->data;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: removing entry #%d, point 0x%X\n", __debug__, data->number, data->Point);

        g_free(data->ISRC);
        g_free(data);

        self->priv->entries_list = g_list_delete_link(self->priv->entries_list, entry);
        entry = g_list_find_custom(self->priv->entries_list, NULL, (GCompareFunc)find_redundant_entries);
    }

    /* Now, reorder the entries */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reordering entries\n", __debug__);
    self->priv->entries_list = g_list_sort(self->priv->entries_list, (GCompareFunc)sort_entries);
}

static gboolean mirage_parser_ccd_determine_track_mode (MirageParserCcd *self, MirageTrack *track, GError **error)
{
    MirageFragment *fragment;
    gint track_mode;
    guint8 *buffer;
    gint length;

    /* Get last fragment */
    fragment = mirage_track_get_fragment_by_index(track, -1, error);
    if (!fragment) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get fragment\n", __debug__);
        return FALSE;
    }

    /* Read main sector data from fragment; 2352-byte sectors are assumed */
    if (!mirage_fragment_read_main_data(fragment, 0, &buffer, &length, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to read data from fragment to determine track mode!\n", __debug__);
        g_object_unref(fragment);
        return FALSE;
    }
    g_object_unref(fragment);

    /* Determine track mode*/
    track_mode = mirage_helper_determine_sector_type(buffer);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track mode determined to be: %d\n", __debug__, track_mode);
    mirage_track_set_mode(track, track_mode);

    g_free(buffer);

    return TRUE;
}

static void mirage_parser_ccd_clean_parsed_structures (MirageParserCcd *self)
{
    /* CloneCD header */
    g_free(self->priv->header);

    /* Disc */
    g_free(self->priv->disc_data->Catalog);
    g_free(self->priv->disc_data);

    /* Sessions list */
    for (GList *entry = self->priv->sessions_list; entry; entry = entry->next) {
        CCD_Session *ccd_session = entry->data;
        g_free(ccd_session);
    }
    g_list_free(self->priv->sessions_list);

    /* Entries list */
    for (GList *entry = self->priv->entries_list; entry; entry = entry->next) {
        CCD_Entry *ccd_entry = entry->data;
        g_free(ccd_entry->ISRC);
        g_free(ccd_entry);
    }
    g_list_free(self->priv->entries_list);
}

static gboolean mirage_parser_ccd_build_disc_layout (MirageParserCcd *self, GError **error)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: building disc layout\n", __debug__);

    if (self->priv->disc_data->Catalog) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting disc catalog to %.13s\n", __debug__, self->priv->disc_data->Catalog);
        mirage_disc_set_mcn(self->priv->disc, self->priv->disc_data->Catalog);
    }

    mirage_parser_ccd_sort_entries(self);

    /* Go over stored entries and build the layout */
    for (GList *entry = self->priv->entries_list; entry; entry = entry->next) {
        CCD_Entry *ccd_cur_entry = entry->data;
        CCD_Entry *ccd_next_entry = entry->next ? entry->next->data : NULL;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n", __debug__);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: processing entry; point %02X, session %d\n", __debug__, ccd_cur_entry->Point, ccd_cur_entry->Session);

        if (ccd_cur_entry->Point == 0xA0) {
            /* 0xA0 is entry each session should begin with... so add the session here */
            MirageSession *session;

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding session #%i\n", __debug__, ccd_cur_entry->Session);

            session = g_object_new(MIRAGE_TYPE_SESSION, NULL);
            if (!mirage_disc_add_session_by_number(self->priv->disc, ccd_cur_entry->Session, session, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __debug__);
                g_object_unref(session);
                return FALSE;
            }
            mirage_session_set_session_type(session, ccd_cur_entry->PSec); /* PSEC = Disc Type */

            g_object_unref(session);
        } else if (ccd_cur_entry->Point == 0xA2) {
            /* 0xA2 is entry each session should end with... */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: closing session #%i\n", __debug__, ccd_cur_entry->Session);

            /* If there is next entry, we're dealing with multi-session disc; in
               this case, we need to set leadout length */
            if (ccd_next_entry) {
                MirageSession *session;
                gint leadout_length;

                session = mirage_disc_get_session_by_number(self->priv->disc, ccd_cur_entry->Session, error);
                if (!session) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get session %i!\n", __debug__, ccd_cur_entry->Session);
                    return FALSE;
                }

                if (ccd_cur_entry->Session == 1) {
                    leadout_length = 11250;
                } else {
                    leadout_length = 6750;
                }

                mirage_session_set_leadout_length(session, leadout_length);

                g_object_unref(session);
            }
        } else {
            /* Track */
            MirageSession *session;
            MirageTrack *track;
            MirageFragment *fragment;
            gint fragment_length;

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding track #%d\n", __debug__, ccd_cur_entry->Point);

            /* Shouldn't really happen... */
            if (!ccd_next_entry) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: ccd_next_entry == NULL; should not happen!\n", __debug__);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "ccd_next_entry == NULL; should not happen!");
                return FALSE;
            }

            /* Grab the session */
            session = mirage_disc_get_session_by_number(self->priv->disc, ccd_cur_entry->Session, error);
            if (!session) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get session %i!\n", __debug__, ccd_cur_entry->Session);
                return FALSE;
            }

            /* Add the track */
            track = g_object_new(MIRAGE_TYPE_TRACK, NULL);
            if (!mirage_session_add_track_by_number(session, ccd_cur_entry->Point, track, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __debug__);
                g_object_unref(track);
                g_object_unref(session);
                return FALSE;
            }


            /* Data fragment */
            fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL);

            mirage_fragment_main_data_set_stream(fragment, self->priv->img_stream);
            mirage_fragment_main_data_set_size(fragment, 2352);
            mirage_fragment_main_data_set_offset(fragment, self->priv->offset*2352);
            mirage_fragment_main_data_set_format(fragment, MIRAGE_MAIN_DATA_FORMAT_DATA);

            mirage_fragment_subchannel_data_set_stream(fragment, self->priv->sub_stream);
            mirage_fragment_subchannel_data_set_size(fragment, 96);
            mirage_fragment_subchannel_data_set_offset(fragment, self->priv->offset*96);
            mirage_fragment_subchannel_data_set_format(fragment, MIRAGE_SUBCHANNEL_DATA_FORMAT_PW96_LINEAR | MIRAGE_SUBCHANNEL_DATA_FORMAT_EXTERNAL);

            mirage_track_add_fragment(track, -1, fragment);

            /* Always determine track mode manually, because I've come across some images with
               [Track] entry containing wrong mode... */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: determining track mode\n", __debug__);
            mirage_parser_ccd_determine_track_mode(self, track, NULL);

            /* If track mode is determined to be audio, set fragment's format accordingly */
            if (mirage_track_get_mode(track) == MIRAGE_MODE_AUDIO) {
                mirage_fragment_main_data_set_format(fragment, MIRAGE_MAIN_DATA_FORMAT_AUDIO);
            }

            /* Validate and set ISRC */
            if (mirage_helper_validate_isrc(ccd_cur_entry->ISRC)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting ISRC to %.12s\n", __debug__, ccd_cur_entry->ISRC);
                mirage_track_set_isrc(track, ccd_cur_entry->ISRC);
            }

            /* Pregap of current track; note that first track in the session does
               not seem to need Index 0 entry. Another thing to note: Index addresses
               seem to be relative to session start; so we use their difference
               to calculate the pregap and then subtract it from PLBA, which is
               relative to disc start */
            gint cur_pregap = 0;
            gint num_tracks = mirage_session_get_number_of_tracks(session);
            if ((num_tracks == 1 && ccd_cur_entry->Index1) ||
                (ccd_cur_entry->Index0 && ccd_cur_entry->Index1)) {
                /* If Index 0 is not set (first track in session), it's 0 and
                   the formula still works */
                cur_pregap = ccd_cur_entry->Index1 - ccd_cur_entry->Index0;

                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: pregap determined to be 0x%X (%i)\n", __debug__, cur_pregap, cur_pregap);
                mirage_track_set_track_start(track, cur_pregap);
            }

            /* Pregap of next track; this one is needed to properly calculate
               fragment length (otherwise pregap of next track gets appended
               to current track) */
            gint next_pregap = 0;
            if (ccd_next_entry->Index0 && ccd_next_entry->Index1) {
                next_pregap = ccd_next_entry->Index1 - ccd_next_entry->Index0;
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: next track's pregap determined to be 0x%X (%i)\n", __debug__, next_pregap, next_pregap);
            }

            /* Fragment length calculation magic... */
            gint track_start = ccd_cur_entry->PLBA - cur_pregap;
            gint track_end = ccd_next_entry->PLBA - next_pregap;

            fragment_length = track_end - track_start;

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: fragment length determined to be 0x%X (%i)\n", __debug__, fragment_length, fragment_length);
            mirage_fragment_set_length(fragment, fragment_length);

            /* Update offset */
            self->priv->offset += fragment_length;

            g_object_unref(fragment);
            g_object_unref(track);
            g_object_unref(session);
        }

    }

    /* If CD-TEXT data is available, decode it */
    if (self->priv->cdtext_data) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting CD-TEXT\n", __debug__);

        /* Decode CD-TEXT by setting it to first session */
        MirageSession *session = mirage_disc_get_session_by_index(self->priv->disc, 0, NULL);
        if (!mirage_session_set_cdtext_data(session, self->priv->cdtext_data, self->priv->cdtext_entries*18, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set CD-TEXT data!\n", __debug__);
            /* Don't bail on error... */
        }
        g_object_unref(session);
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finishing the layout\n", __debug__);
    /* Finish disc layout (i.e. guess medium type and set pregaps if necessary) */
    gint medium_type = mirage_parser_guess_medium_type(MIRAGE_PARSER(self), self->priv->disc);
    mirage_disc_set_medium_type(self->priv->disc, medium_type);
    if (medium_type == MIRAGE_MEDIUM_CD) {
        mirage_parser_add_redbook_pregap(MIRAGE_PARSER(self), self->priv->disc);
    }

    return TRUE;
}


/**********************************************************************\
 *                       Regex parsing engine                         *
\**********************************************************************/
typedef gboolean (*CCD_RegexCallback) (MirageParserCcd *self, GMatchInfo *match_info, GError **error);

typedef struct
{
    GRegex *regex;
    CCD_RegexCallback callback_func;
} CCD_RegexRule;


/*** [CloneCD] ***/
static gboolean mirage_parser_ccd_callback_clonecd (MirageParserCcd *self, GMatchInfo *match_info G_GNUC_UNUSED, GError **error G_GNUC_UNUSED)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n"); /* To make log more readable */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed [CloneCD] header\n", __debug__);

    self->priv->header = g_new0(CCD_CloneCD, 1);
    self->priv->cur_data = self->priv->header;

    self->priv->cur_rules = self->priv->regex_rules_clonecd;

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_clonecd_version (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    CCD_CloneCD *clonecd = self->priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: Version = %s\n", __debug__, value_str);
    clonecd->Version = g_strtod(value_str, NULL);

    g_free(value_str);

    return TRUE;
}


/*** [Disc] ***/
static gboolean mirage_parser_ccd_callback_disc (MirageParserCcd *self, GMatchInfo *match_info G_GNUC_UNUSED, GError **error G_GNUC_UNUSED)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n"); /* To make log more readable */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed [Disc] header\n", __debug__);

    self->priv->disc_data = g_new0(CCD_Disc, 1);
    self->priv->cur_data = self->priv->disc_data;

    self->priv->cur_rules = self->priv->regex_rules_disc;

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_disc_toc_entries (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    CCD_Disc *disc = self->priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: TocEntries = %s\n", __debug__, value_str);
    disc->TocEntries = g_strtod(value_str, NULL);

    g_free(value_str);

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_disc_sessions (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    CCD_Disc *disc = self->priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: Sessions = %s\n", __debug__, value_str);
    disc->Sessions = g_strtod(value_str, NULL);

    g_free(value_str);

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_disc_data_tracks_scrambled (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    CCD_Disc *disc = self->priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: DataTracksScrambled = %s\n", __debug__, value_str);
    disc->DataTracksScrambled = g_strtod(value_str, NULL);

    g_free(value_str);

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_disc_cdtext_length (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    CCD_Disc *disc = self->priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: CDTextLength = %s\n", __debug__, value_str);
    disc->CDTextLength = g_strtod(value_str, NULL);

    g_free(value_str);

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_disc_catalog (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    CCD_Disc *disc = self->priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: Catalog = %s\n", __debug__, value_str);
    disc->Catalog = value_str;

    /*g_free(value_str);*/

    return TRUE;
}


/*** [Session X] ***/
static gboolean mirage_parser_ccd_callback_session (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *number_str = g_match_info_fetch_named(match_info, "number");
    CCD_Session *session;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n"); /* To make log more readable */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed [Session %s] header\n", __debug__, number_str);

    session = g_new0(CCD_Session, 1);
    session->number = g_strtod(number_str, NULL);

    self->priv->sessions_list = g_list_append(self->priv->sessions_list, session);
    self->priv->cur_data = session;

    self->priv->cur_rules = self->priv->regex_rules_session;

    g_free(number_str);

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_session_pregap_mode (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    CCD_Session *session = self->priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: PreGapMode = %s\n", __debug__, value_str);
    session->PreGapMode = g_strtod(value_str, NULL);

    g_free(value_str);

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_session_pregap_subc (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    CCD_Session *session = self->priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: PreGapSubC = %s\n", __debug__, value_str);
    session->PreGapSubC = g_strtod(value_str, NULL);

    g_free(value_str);

    return TRUE;
}


/*** [Entry X] ***/
static gboolean mirage_parser_ccd_callback_entry (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    gchar *number_str = g_match_info_fetch_named(match_info, "number");
    CCD_Entry *entry;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n"); /* To make log more readable */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed [Entry %s] header\n", __debug__, number_str);

    entry = g_new0(CCD_Entry, 1);
    entry->number = g_strtod(number_str, NULL);

    self->priv->entries_list = g_list_append(self->priv->entries_list, entry);
    self->priv->cur_data = entry;

    self->priv->cur_rules = self->priv->regex_rules_entry;

    g_free(number_str);

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_entry_session (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    CCD_Entry *entry = self->priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: Session = %s\n", __debug__, value_str);
    entry->Session = g_strtod(value_str, NULL);

    g_free(value_str);

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_entry_point (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    CCD_Entry *entry = self->priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: Point = %s\n", __debug__, value_str);
    entry->Point = g_strtod(value_str, NULL);

    g_free(value_str);

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_entry_adr (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    CCD_Entry *entry = self->priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: ADR = %s\n", __debug__, value_str);
    entry->ADR = g_strtod(value_str, NULL);

    g_free(value_str);

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_entry_control (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    CCD_Entry *entry = self->priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: Control = %s\n", __debug__, value_str);
    entry->Control = g_strtod(value_str, NULL);

    g_free(value_str);

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_entry_trackno (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    CCD_Entry *entry = self->priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: TrackNo = %s\n", __debug__, value_str);
    entry->TrackNo = g_strtod(value_str, NULL);

    g_free(value_str);

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_entry_amin (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    CCD_Entry *entry = self->priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: AMin = %s\n", __debug__, value_str);
    entry->AMin = g_strtod(value_str, NULL);

    g_free(value_str);

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_entry_asec (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    CCD_Entry *entry = self->priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: ASec = %s\n", __debug__, value_str);
    entry->ASec = g_strtod(value_str, NULL);

    g_free(value_str);

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_entry_aframe (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    CCD_Entry *entry = self->priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: AFrame = %s\n", __debug__, value_str);
    entry->AFrame = g_strtod(value_str, NULL);

    g_free(value_str);

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_entry_alba (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    CCD_Entry *entry = self->priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: ALBA = %s\n", __debug__, value_str);
    entry->ALBA = g_strtod(value_str, NULL);

    g_free(value_str);

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_entry_zero (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    CCD_Entry *entry = self->priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: Zero = %s\n", __debug__, value_str);
    entry->Zero = g_strtod(value_str, NULL);

    g_free(value_str);

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_entry_pmin (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    CCD_Entry *entry = self->priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: PMin = %s\n", __debug__, value_str);
    entry->PMin = g_strtod(value_str, NULL);

    g_free(value_str);

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_entry_psec (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    CCD_Entry *entry = self->priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: PSec = %s\n", __debug__, value_str);
    entry->PSec = g_strtod(value_str, NULL);

    g_free(value_str);

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_entry_pframe (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    CCD_Entry *entry = self->priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: PFrame = %s\n", __debug__, value_str);
    entry->PFrame = g_strtod(value_str, NULL);

    g_free(value_str);

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_entry_plba (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    CCD_Entry *entry = self->priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: PLBA = %s\n", __debug__, value_str);
    entry->PLBA = g_strtod(value_str, NULL);

    g_free(value_str);

    return TRUE;
}

/*** [TRACK X] ***/
static gint find_entry_by_point (CCD_Entry *entry, gpointer data)
{
    gint point = GPOINTER_TO_INT(data);
    return !(entry->Point == point);
}

static gboolean mirage_parser_ccd_callback_track (MirageParserCcd *self, GMatchInfo *match_info, GError **error)
{
    gchar *number_str = g_match_info_fetch_named(match_info, "number");
    gint number = g_strtod(number_str, NULL);
    GList *entry;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n"); /* To make log more readable */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed [TRACK %s] header\n", __debug__, number_str);

    /* Get corresponding entry data and store the pointer */
    entry = g_list_find_custom(self->priv->entries_list, GINT_TO_POINTER(number), (GCompareFunc)find_entry_by_point);
    if (!entry) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get entry with point #%d!\n", __debug__, number);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Failed to get entry with point #%d!", number);
        return FALSE;
    }
    self->priv->cur_data = entry->data;

    self->priv->cur_rules = self->priv->regex_rules_track;

    g_free(number_str);

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_track_mode (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    CCD_Entry *entry = self->priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: MODE = %s\n", __debug__, value_str);
    entry->Mode = g_strtod(value_str, NULL);

    g_free(value_str);

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_track_index0 (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    CCD_Entry *entry = self->priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: INDEX 0 = %s\n", __debug__, value_str);
    entry->Index0 = g_strtod(value_str, NULL);

    g_free(value_str);

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_track_index1 (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    CCD_Entry *entry = self->priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: INDEX 1 = %s\n", __debug__, value_str);
    entry->Index1 = g_strtod(value_str, NULL);

    g_free(value_str);

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_track_isrc (MirageParserCcd *self, GMatchInfo *match_info, GError **error G_GNUC_UNUSED)
{
    CCD_Entry *entry = self->priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: ISRC = %s\n", __debug__, value_str);
    entry->ISRC = value_str;

    /*g_free(value_str);*/

    return TRUE;
}


/*** [CDText] ***/
static gboolean mirage_parser_ccd_callback_cdtext (MirageParserCcd *self, GMatchInfo *match_info G_GNUC_UNUSED, GError **error G_GNUC_UNUSED)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n"); /* To make log more readable */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed [CDText] header\n", __debug__);

    self->priv->cur_rules = self->priv->regex_rules_cdtext;

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_cdtext_entries (MirageParserCcd *self, GMatchInfo *match_info G_GNUC_UNUSED, GError **error G_GNUC_UNUSED)
{
    gchar *value_str = g_match_info_fetch_named(match_info, "value");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: entries = %s\n", __debug__, value_str);

    self->priv->cdtext_entries = g_strtod(value_str, NULL);

    /* Validate declared CD-TEXT length against declared number of entries;
       each entry corresponds to an 18-byte pack with last two bytes (CRC)
       removed, so it contains 16 bytes */
    if (self->priv->disc_data->CDTextLength != self->priv->cdtext_entries * 18) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: declared CD-TEXT size (%d) does not match declared number of entries (%d)!\n", __debug__, self->priv->disc_data->CDTextLength, self->priv->cdtext_entries);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Ddeclared CD-TEXT size (%d) does not match declared number of entries (%d)!\n", self->priv->disc_data->CDTextLength, self->priv->cdtext_entries);
        return FALSE;
    }

    self->priv->cdtext_data = g_try_malloc0(self->priv->disc_data->CDTextLength);

    g_free(value_str);

    return TRUE;
}

static gboolean mirage_parser_ccd_callback_cdtext_entry (MirageParserCcd *self, GMatchInfo *match_info G_GNUC_UNUSED, GError **error G_GNUC_UNUSED)
{
    gchar *number_str = g_match_info_fetch_named(match_info, "number");
    gchar *data_str = g_match_info_fetch_named(match_info, "data");

    gint number;
    gchar **data_tokens;

    guint8 *data_ptr;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: entry #%s: data: %s\n", __debug__, number_str, data_str);

    /* Validate entry number */
    number = g_strtod(number_str, NULL);
    if (number >= self->priv->cdtext_entries) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: invalid CD-TEXT entry #%d (expecting only %d entries)!\n", __debug__, number, self->priv->cdtext_entries);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "Invalid CD-TEXT entry #%d (expecting only %d entries)!\n", number, self->priv->cdtext_entries);
        return FALSE;
    }

    data_ptr = self->priv->cdtext_data + number*18;

    /* Split data string on whitespaces */
    data_tokens = g_strsplit(data_str, " ", -1);

    for (gint i = 0; data_tokens[i]; i++) {
        data_ptr[i] = g_ascii_strtoll(data_tokens[i], NULL, 16);
    }

    g_strfreev(data_tokens);

    g_free(number_str);
    g_free(data_str);

    return TRUE;
}



static inline void append_regex_rule (GList **list_ptr, const gchar *rule, CCD_RegexCallback callback)
{
    GList *list = *list_ptr;

    CCD_RegexRule *new_rule = g_new(CCD_RegexRule, 1);
    new_rule->regex = g_regex_new(rule, G_REGEX_OPTIMIZE, 0, NULL);
    new_rule->callback_func = callback;
    /* Append to the list */
    list = g_list_append(list, new_rule);

    *list_ptr = list;
}

static void mirage_parser_ccd_init_regex_parser (MirageParserCcd *self)
{
    /* Ignore empty lines */
    append_regex_rule(&self->priv->regex_rules, "^[\\s]*$", NULL);

    /* Section rules */
    append_regex_rule(&self->priv->regex_rules, "^\\s*\\[CloneCD\\]", mirage_parser_ccd_callback_clonecd);
    append_regex_rule(&self->priv->regex_rules, "^\\s*\\[Disc\\]", mirage_parser_ccd_callback_disc);
    append_regex_rule(&self->priv->regex_rules, "^\\s*\\[Session\\s*(?<number>\\d+)\\]", mirage_parser_ccd_callback_session);
    append_regex_rule(&self->priv->regex_rules, "^\\s*\\[Entry\\s*(?<number>\\d+)\\]", mirage_parser_ccd_callback_entry);
    append_regex_rule(&self->priv->regex_rules, "^\\s*\\[TRACK\\s*(?<number>\\d+)\\]", mirage_parser_ccd_callback_track);
    append_regex_rule(&self->priv->regex_rules, "^\\s*\\[CDText\\]", mirage_parser_ccd_callback_cdtext);

    /* [CloneCD] rules */
    append_regex_rule(&self->priv->regex_rules_clonecd, "^\\s*Version\\s*=\\s*(?<value>\\d+)", mirage_parser_ccd_callback_clonecd_version);

    /* [Disc] rules */
    append_regex_rule(&self->priv->regex_rules_disc, "^\\s*TocEntries\\s*=\\s*(?<value>\\d+)", mirage_parser_ccd_callback_disc_toc_entries);
    append_regex_rule(&self->priv->regex_rules_disc, "^\\s*Sessions\\s*=\\s*(?<value>\\d+)", mirage_parser_ccd_callback_disc_sessions);
    append_regex_rule(&self->priv->regex_rules_disc, "^\\s*DataTracksScrambled\\s*=\\s*(?<value>\\d+)", mirage_parser_ccd_callback_disc_data_tracks_scrambled);
    append_regex_rule(&self->priv->regex_rules_disc, "^\\s*CDTextLength\\s*=\\s*(?<value>\\d+)", mirage_parser_ccd_callback_disc_cdtext_length);
    append_regex_rule(&self->priv->regex_rules_disc, "^\\s*CATALOG\\s*=\\s*(?<value>\\w+)", mirage_parser_ccd_callback_disc_catalog);

    /* [Session X] rules */
    append_regex_rule(&self->priv->regex_rules_session, "^\\s*PreGapMode\\s*=\\s*(?<value>\\d+)", mirage_parser_ccd_callback_session_pregap_mode);
    append_regex_rule(&self->priv->regex_rules_session, "^\\s*PreGapSubC\\s*=\\s*(?<value>\\d+)", mirage_parser_ccd_callback_session_pregap_subc);

    /* [Entry X] rules */
    append_regex_rule(&self->priv->regex_rules_entry, "^\\s*Session\\s*=\\s*(?<value>\\d+)", mirage_parser_ccd_callback_entry_session);
    append_regex_rule(&self->priv->regex_rules_entry, "^\\s*Point\\s*=\\s*(?<value>[\\w+]+)", mirage_parser_ccd_callback_entry_point);
    append_regex_rule(&self->priv->regex_rules_entry, "^\\s*ADR\\s*=\\s*(?<value>\\w+)", mirage_parser_ccd_callback_entry_adr);
    append_regex_rule(&self->priv->regex_rules_entry, "^\\s*Control\\s*=\\s*(?<value>\\w+)", mirage_parser_ccd_callback_entry_control);
    append_regex_rule(&self->priv->regex_rules_entry, "^\\s*TrackNo\\s*=\\s*(?<value>\\d+)", mirage_parser_ccd_callback_entry_trackno);
    append_regex_rule(&self->priv->regex_rules_entry, "^\\s*AMin\\s*=\\s*(?<value>\\d+)", mirage_parser_ccd_callback_entry_amin);
    append_regex_rule(&self->priv->regex_rules_entry, "^\\s*ASec\\s*=\\s*(?<value>\\d+)", mirage_parser_ccd_callback_entry_asec);
    append_regex_rule(&self->priv->regex_rules_entry, "^\\s*AFrame\\s*=\\s*(?<value>\\d+)", mirage_parser_ccd_callback_entry_aframe);
    append_regex_rule(&self->priv->regex_rules_entry, "^\\s*ALBA\\s*=\\s*(?<value>-?\\d+)", mirage_parser_ccd_callback_entry_alba);
    append_regex_rule(&self->priv->regex_rules_entry, "^\\s*Zero\\s*=\\s*(?<value>\\d+)", mirage_parser_ccd_callback_entry_zero);
    append_regex_rule(&self->priv->regex_rules_entry, "^\\s*PMin\\s*=\\s*(?<value>\\d+)", mirage_parser_ccd_callback_entry_pmin);
    append_regex_rule(&self->priv->regex_rules_entry, "^\\s*PSec\\s*=\\s*(?<value>\\d+)", mirage_parser_ccd_callback_entry_psec);
    append_regex_rule(&self->priv->regex_rules_entry, "^\\s*PFrame\\s*=\\s*(?<value>\\d+)", mirage_parser_ccd_callback_entry_pframe);
    append_regex_rule(&self->priv->regex_rules_entry, "^\\s*PLBA\\s*=\\s*(?<value>\\d+)", mirage_parser_ccd_callback_entry_plba);

    /* [TRACK X] rules */
    append_regex_rule(&self->priv->regex_rules_track, "^\\s*MODE\\s*=\\s*(?<value>\\d+)", mirage_parser_ccd_callback_track_mode);
    append_regex_rule(&self->priv->regex_rules_track, "^\\s*INDEX\\s*0\\s*=\\s*(?<value>\\d+)", mirage_parser_ccd_callback_track_index0);
    append_regex_rule(&self->priv->regex_rules_track, "^\\s*INDEX\\s*1\\s*=\\s*(?<value>\\d+)", mirage_parser_ccd_callback_track_index1);
    append_regex_rule(&self->priv->regex_rules_track, "^\\s*ISRC\\s*=\\s*(?<value>\\w+)", mirage_parser_ccd_callback_track_isrc);

    /* [CDText] rules */
    append_regex_rule(&self->priv->regex_rules_cdtext, "^\\s*Entries\\s*=\\s*(?<value>\\d+)", mirage_parser_ccd_callback_cdtext_entries);
    append_regex_rule(&self->priv->regex_rules_cdtext, "^\\s*Entry\\s*(?<number>\\d+)\\s*=\\s*(?<data>[[:xdigit:]\\s]+)", mirage_parser_ccd_callback_cdtext_entry);

}

static void free_regex_rules (GList *rules)
{
    for (GList *entry = rules; entry; entry = entry->next) {
        CCD_RegexRule *rule = entry->data;
        g_regex_unref(rule->regex);
        g_free(rule);
    }
    g_list_free(rules);
}

static void mirage_parser_ccd_cleanup_regex_parser (MirageParserCcd *self)
{
    free_regex_rules(self->priv->regex_rules);
    free_regex_rules(self->priv->regex_rules_clonecd);
    free_regex_rules(self->priv->regex_rules_disc);
    free_regex_rules(self->priv->regex_rules_session);
    free_regex_rules(self->priv->regex_rules_entry);
    free_regex_rules(self->priv->regex_rules_track);
    free_regex_rules(self->priv->regex_rules_cdtext);
}


static gboolean mirage_parser_ccd_parse_ccd_file (MirageParserCcd *self, GInputStream *stream, GError **error)
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

        /* If current rules are active, use those */
        if (self->priv->cur_rules) {
            for (GList *entry = self->priv->cur_rules; entry; entry = entry->next) {
                CCD_RegexRule *regex_rule = entry->data;

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
        }

        /* If no match was found, try base rules */
        if (!matched) {
            for (GList *entry = self->priv->regex_rules; entry; entry = entry->next) {
                CCD_RegexRule *regex_rule = entry->data;

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
static MirageDisc *mirage_parser_ccd_load_image (MirageParser *_self, GInputStream **streams, GError **error)
{
    MirageParserCcd *self = MIRAGE_PARSER_CCD(_self);

    gboolean succeeded = TRUE;
    const gchar *ccd_filename = mirage_contextual_get_file_stream_filename(MIRAGE_CONTEXTUAL(self), streams[0]);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: checking if parser can handle given image...\n", __debug__);

    /* Check if we can load the file; we check the suffix */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: verifying image file's suffix...\n", __debug__);
    if (!mirage_helper_has_suffix(ccd_filename, ".ccd")) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: parser cannot handle given image: invalid suffix (not a *.ccd file!)!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Parser cannot handle given image: invalid suffix!");
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: parser can handle given image!\n", __debug__);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing the image...\n", __debug__);

    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_set_parent(MIRAGE_OBJECT(self->priv->disc), self);

    mirage_disc_set_filename(self->priv->disc, ccd_filename);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CCD filename: %s\n", __debug__, ccd_filename);

    /* Compose image and subchannel filename */
    gint len = strlen(ccd_filename);
    gchar *tmp_img_filename = g_strdup(ccd_filename);
    gchar *tmp_sub_filename = g_strdup(ccd_filename);

    g_snprintf(tmp_img_filename+len-3, 4, "img");
    g_snprintf(tmp_sub_filename+len-3, 4, "sub");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: assumed data file: %s\n", __debug__, tmp_img_filename);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: assumed subchannel file: %s\n", __debug__, tmp_sub_filename);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");

    self->priv->img_filename = mirage_helper_find_data_file(tmp_img_filename, ccd_filename);
    self->priv->sub_filename = mirage_helper_find_data_file(tmp_sub_filename, ccd_filename);

    g_free(tmp_img_filename);
    g_free(tmp_sub_filename);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: actual data file: %s\n", __debug__, self->priv->img_filename);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: actual subchannel file: %s\n", __debug__, self->priv->sub_filename);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");

    if (!self->priv->img_filename) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DATA_FILE_ERROR, "Could not find main data file!");
        return FALSE;
    }
    if (!self->priv->sub_filename) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DATA_FILE_ERROR, "Could not find subchannel data file!");
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating data file streams...\n", __debug__);

    /* Open streams */
    self->priv->img_stream = mirage_contextual_create_file_stream(MIRAGE_CONTEXTUAL(self), self->priv->img_filename, error);
    if (!self->priv->img_stream) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to create stream on main data file '%s'!\n", __debug__, self->priv->img_filename);
        return FALSE;
    }

    self->priv->sub_stream = mirage_contextual_create_file_stream(MIRAGE_CONTEXTUAL(self), self->priv->sub_filename, error);
    if (!self->priv->sub_stream) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to create stream on subchannel data file '%s'!\n", __debug__, self->priv->sub_filename);
        return FALSE;
    }

    /* Parse the CCD */
    if (!mirage_parser_ccd_parse_ccd_file(self, streams[0], error)) {
        succeeded = FALSE;
        goto end;
    }

    /* Build the layout */
    succeeded = mirage_parser_ccd_build_disc_layout(self, error);

    /* Clean the parsed structures as they aren't needed anymore */
    mirage_parser_ccd_clean_parsed_structures(self);

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
G_DEFINE_DYNAMIC_TYPE(MirageParserCcd, mirage_parser_ccd, MIRAGE_TYPE_PARSER);

void mirage_parser_ccd_type_register (GTypeModule *type_module)
{
    return mirage_parser_ccd_register_type(type_module);
}


static void mirage_parser_ccd_init (MirageParserCcd *self)
{
    self->priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);

    mirage_parser_generate_info(MIRAGE_PARSER(self),
        "PARSER-CCD",
        "CCD Image Parser",
        1,
        "CloneCD images (*.ccd)", "application/x-ccd"
    );

    /* Init regex parser engine */
    mirage_parser_ccd_init_regex_parser(self);

    self->priv->img_filename = NULL;
    self->priv->img_stream = NULL;

    self->priv->sub_filename = NULL;
    self->priv->sub_stream = NULL;

    self->priv->cdtext_data = NULL;
}

static void mirage_parser_ccd_dispose (GObject *gobject)
{
    MirageParserCcd *self = MIRAGE_PARSER_CCD(gobject);

    if (self->priv->img_stream) {
        g_object_unref(self->priv->img_stream);
        self->priv->img_stream = NULL;
    }

    if (self->priv->sub_stream) {
        g_object_unref(self->priv->sub_stream);
        self->priv->sub_stream = NULL;
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_parser_ccd_parent_class)->dispose(gobject);
}


static void mirage_parser_ccd_finalize (GObject *gobject)
{
    MirageParserCcd *self = MIRAGE_PARSER_CCD(gobject);

    g_free(self->priv->img_filename);
    g_free(self->priv->sub_filename);

    g_free(self->priv->cdtext_data);

    /* Cleanup regex parser engine */
    mirage_parser_ccd_cleanup_regex_parser(self);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_parser_ccd_parent_class)->finalize(gobject);
}

static void mirage_parser_ccd_class_init (MirageParserCcdClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageParserClass *parser_class = MIRAGE_PARSER_CLASS(klass);

    gobject_class->dispose = mirage_parser_ccd_dispose;
    gobject_class->finalize = mirage_parser_ccd_finalize;

    parser_class->load_image = mirage_parser_ccd_load_image;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageParserCcdPrivate));
}

static void mirage_parser_ccd_class_finalize (MirageParserCcdClass *klass G_GNUC_UNUSED)
{
}
