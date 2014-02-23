/*
 *  libMirage: Session object
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

/**
 * SECTION: mirage-session
 * @title: MirageSession
 * @short_description: Object representing a session.
 * @see_also: #MirageDisc, #MirageTrack, #MirageLanguage, #MirageCdTextCoder
 * @include: mirage-session.h
 *
 * #MirageSession object represents a session in the disc layout. It
 * provides functions for manipulating session layout; setting session
 * type, adding and removing tracks and languages, setting CD-TEXT data
 * and MCN, etc.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"

#define __debug__ "Session"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_SESSION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_SESSION, MirageSessionPrivate))

struct _MirageSessionPrivate
{
    /* MCN */
    gchar *mcn;
    gboolean mcn_fixed; /* Is MCN fixed due to one of track's fragments having subchannel? */
    gboolean mcn_scan_complete; /* Have we performed scan for MCN in track's fragments' subchannel? */

    /* Layout settings */
    gint session_number; /* Session number */
    gint start_sector; /* Start sector */
    gint first_track; /* Number of the first track in session */
    gint length; /* Length of session (sum of tracks' length) */

    /* Session type */
    MirageSessionType session_type;

    /* Tracks list */
    GList *tracks_list;

    /* CD-Text list */
    GList *languages_list;
} ;


/**********************************************************************\
 *                          Private functions                         *
\**********************************************************************/
static MirageTrack *mirage_session_find_track_with_subchannel (MirageSession *self)
{
    MirageTrack *track = NULL;

    /* Go over all tracks, and find the first one with fragment that contains
       subchannel... */
    gint num_tracks = mirage_session_get_number_of_tracks(self);
    for (gint i = 0; i < num_tracks; i++) {
        track = mirage_session_get_track_by_index(self, i, NULL);
        if (track) {
            MirageFragment *fragment = mirage_track_find_fragment_with_subchannel(track, NULL);
            if (fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_SESSION, "%s: track %i contains subchannel\n", __debug__, i);
                g_object_unref(fragment);
                break;
            } else {
                /* Unref the track we just checked */
                g_object_unref(track);
                track = NULL;
            }
        }
    }

    return track;
}

static gchar *mirage_session_scan_for_mcn (MirageSession *self)
{
    MirageTrack *track = mirage_session_find_track_with_subchannel(self);
    gchar *mcn = NULL;

    if (!track) {
        return mcn;
    }

    /* According to INF8090, MCN, if present, must be encoded in at least
       one sector in 100 consequtive sectors. So we read first hundred
       sectors' subchannel, and extract MCN if we find it. */
    MirageFragment *fragment = mirage_track_find_fragment_with_subchannel(track, NULL);
    gint start_address = mirage_fragment_get_address(fragment);
    g_object_unref(fragment);

    for (gint address = start_address; address < start_address+100; address++) {
        MirageSector *sector;
        const guint8 *buf;
        gint buflen;

        /* Get sector */
        sector = mirage_track_get_sector(track, address, FALSE, NULL);
        if (!sector) {
            break;
        }

        /* Get Q subchannel */
        if (!mirage_sector_get_subchannel(sector, MIRAGE_SUBCHANNEL_Q, &buf, &buflen, NULL)) {
            g_object_unref(sector);
            break;
        }

        if ((buf[0] & 0x0F) == 0x02) {
            /* Mode-2 Q found */
            gchar tmp_mcn[13];

            mirage_helper_subchannel_q_decode_mcn(&buf[1], tmp_mcn);

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: found MCN: <%s>\n", __debug__, tmp_mcn);

            /* Set MCN */
            mcn = g_strndup(tmp_mcn, 13);
        }

        g_object_unref(sector);

        if (mcn) {
            break;
        }
    }

    g_object_unref(track);

    return mcn;
}

static void mirage_session_commit_topdown_change (MirageSession *self)
{
    /* Rearrange tracks: set numbers, set start sectors */
    gint cur_track_address = self->priv->start_sector;
    gint cur_track_number  = self->priv->first_track;

    for (GList *entry = self->priv->tracks_list; entry; entry = entry->next) {
        MirageTrack *track = entry->data;

        /* Set track's number */
        gint old_number = mirage_track_layout_get_track_number(track);
        if ((old_number != MIRAGE_TRACK_LEADIN) && (old_number != MIRAGE_TRACK_LEADOUT)) {
            mirage_track_layout_set_track_number(track, cur_track_number);
            cur_track_number++;
        }

        /* Set track's start address */
        mirage_track_layout_set_start_sector(track, cur_track_address);
        cur_track_address += mirage_track_layout_get_length(track);
    }
}

static void mirage_session_commit_bottomup_change (MirageSession *self)
{
    MirageDisc *disc;

    /* Calculate session length */
    self->priv->length = 0; /* Reset; it'll be recalculated */

    for (GList *entry = self->priv->tracks_list; entry; entry = entry->next) {
        MirageTrack *track = entry->data;
        self->priv->length += mirage_track_layout_get_length(track);
    }

    /* Bottom-up change = eventual change in fragments, so MCN could've changed... */
    MirageTrack *track = mirage_session_find_track_with_subchannel(self);
    if (track) {
        self->priv->mcn_fixed = TRUE;
        self->priv->mcn_scan_complete = FALSE; /* Will trigger scan in mirage_session_get_mcn() */
        g_object_unref(track);
    } else {
        self->priv->mcn_fixed = FALSE;
    }

    /* Signal session change */
    g_signal_emit_by_name(self, "layout-changed", NULL);
    /* If we don't have parent, we should complete the arc by committing top-down change */
    disc = mirage_object_get_parent(MIRAGE_OBJECT(self));
    if (!disc) {
        mirage_session_commit_topdown_change(self);
    } else {
        g_object_unref(disc);
    }
}

static void mirage_session_track_layout_changed_handler (MirageSession *self, MirageTrack *track G_GNUC_UNUSED)
{
    /* Bottom-up change */
    mirage_session_commit_bottomup_change(self);
}

static void mirage_session_remove_track (MirageSession *self, MirageTrack *track)
{
    /* Disconnect signal handler (find it by handler function and user data) */
    g_signal_handlers_disconnect_by_func(track, mirage_session_track_layout_changed_handler, self);

    /* Remove track from list and unref it */
    self->priv->tracks_list = g_list_remove(self->priv->tracks_list, track);
    g_object_unref(track);

    /* Bottom-up change */
    mirage_session_commit_bottomup_change(self);
}

static void mirage_session_remove_language (MirageSession *self, MirageLanguage *language)
{
    /* Remove it from list and unref it */
    self->priv->languages_list = g_list_remove(self->priv->languages_list, language);
    g_object_unref(language);
}


static gint sort_languages_by_code (MirageLanguage *language1, MirageLanguage *language2)
{
    gint code1 = mirage_language_get_code(language1);
    gint code2 = mirage_language_get_code(language2);

    if (code1 < code2) {
        return -1;
    } else if (code1 > code2) {
        return 1;
    } else {
        return 0;
    }
}

static gint sort_tracks_by_number (MirageTrack *track1, MirageTrack *track2)
{
    gint number1 = mirage_track_layout_get_track_number(track1);
    gint number2 = mirage_track_layout_get_track_number(track2);

    if (number1 == MIRAGE_TRACK_LEADIN) {
        /* Track 1 is lead-in; always before the rest */
        return -1;
    } else if (number2 == MIRAGE_TRACK_LEADIN) {
        /* Track 2 is lead-in; place Track 1 behind it */
        return 1;
    } else if (number1 == MIRAGE_TRACK_LEADOUT) {
        /* Track 1 is lead-out; always after the rest */
        return 1;
    } else if (number2 == MIRAGE_TRACK_LEADOUT) {
        /* Track 2 is lead-out; place Track 1 before it */
        return -1;
    } else {
        if (number1 < number2) {
            return -1;
        } else if (number1 > number2) {
            return 1;
        } else {
            return 0;
        }
    }
}


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
/**
 * mirage_session_set_session_type:
 * @self: a #MirageSession
 * @type: (in): session type
 *
 * Sets session type. @type must be one of #MirageSessionTypes.
 */
void mirage_session_set_session_type (MirageSession *self, MirageSessionType type)
{
    /* Set session type */
    self->priv->session_type =type;
}

/**
 * mirage_session_get_session_type:
 * @self: a #MirageSession
 *
 * Retrieves session type.
 *
 * Returns: session type
 */
MirageSessionType mirage_session_get_session_type (MirageSession *self)
{
    /* Return session number */
    return self->priv->session_type;
}


/**
 * mirage_session_set_mcn:
 * @self: a #MirageSession
 * @mcn: (in): MCN
 *
 * Sets MCN (Media Catalogue Number).
 *
 * Because MCN is stored in subchannel data, this function silently
 * fails if any of session's tracks contains fragments with subchannel
 * data provided.
 *
 * <note>
 * Intended for internal use only.
 * </note>
 */
void mirage_session_set_mcn (MirageSession *self, const gchar *mcn)
{
    /* MCN can be set only if none of the tracks have fragments that contain
       subchannel; this is because MCN is encoded in the subchannel, and cannot
       be altered... */
    if (self->priv->mcn_fixed) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_SESSION, "%s: MCN is already encoded in subchannel!\n", __debug__);
    } else {
        g_free(self->priv->mcn);
        self->priv->mcn = g_strndup(mcn, 13);
    }
}

/**
 * mirage_session_get_mcn:
 * @self: a #MirageSession
 *
 * Retrieves MCN.
 *
 * Returns: (transfer none): pointer to MCN string, or %NULL. The string
 * belongs to the object and should not be modified.
 */
const gchar *mirage_session_get_mcn (MirageSession *self)
{
    /* Do we need to scan for MCN first? */
    if (self->priv->mcn_fixed && !self->priv->mcn_scan_complete) {
        g_free(self->priv->mcn);
        self->priv->mcn = mirage_session_scan_for_mcn(self);

        self->priv->mcn_scan_complete = TRUE;
    }

    /* Return pointer to MCN */
    return self->priv->mcn;
}


/**
 * mirage_session_layout_set_session_number:
 * @self: a #MirageSession
 * @number: (in): session number
 *
 * Sets sessions's session number.
 *
 * <note>
 * Intended for internal use only.
 * </note>
 */
void mirage_session_layout_set_session_number (MirageSession *self, gint number)
{
    /* Set session number */
    self->priv->session_number = number;
}

/**
 * mirage_session_layout_get_session_number:
 * @self: a #MirageSession
 *
 * Retrieves sessions's session number.
 *
 * Returns: session number
 */
gint mirage_session_layout_get_session_number (MirageSession *self)
{
    /* Return session number */
    return self->priv->session_number;
}

/**
 * mirage_session_layout_set_first_track:
 * @self: a #MirageSession
 * @first_track: (in): first track number
 *
 * Sets first track number to @first_track. This is a number that is
 * assigned to the first track in the session layout.
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * <note>
 * Causes top-down change.
 * </note>
 */
void mirage_session_layout_set_first_track (MirageSession *self, gint first_track)
{
    /* Set first track */
    self->priv->first_track = first_track;
    /* Top-down change */
    mirage_session_commit_topdown_change(self);
}

/**
 * mirage_session_layout_get_first_track:
 * @self: a #MirageSession
 *
 * Retrieves track number of the first track in the session layout.
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: first track number
 */
gint mirage_session_layout_get_first_track (MirageSession *self)
{
    /* Return first track */
    return self->priv->first_track;
}

/**
 * mirage_session_layout_set_start_sector:
 * @self: a #MirageSession
 * @start_sector: (in): start sector
 *
 * Sets start sector of the session layout to @start_sector. This is a sector at which
 * the first track in the session layout will start.
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * <note>
 * Causes top-down change.
 * </note>
 */
void mirage_session_layout_set_start_sector (MirageSession *self, gint start_sector)
{
    /* Set start sector */
    self->priv->start_sector = start_sector;
    /* Top-down change */
    mirage_session_commit_topdown_change(self);
}

/**
 * mirage_session_layout_get_start_sector:
 * @self: a #MirageSession
 *
 * Retrieves start sector of the session layout.
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: start sector
 */
gint mirage_session_layout_get_start_sector (MirageSession *self)
{
    /* Return start sector */
    return self->priv->start_sector;
}

/**
 * mirage_session_layout_get_length:
 * @self: a #MirageSession
 *
 * Retrieves length of the session layout. This means the length of
 * all tracks combined, including lead-in and lead-out tracks. The returned
 * length is given in sectors.
 *
 * Returns: session layout length
 */
gint mirage_session_layout_get_length (MirageSession *self)
{
    /* Return length */
    return self->priv->length;
}


/**
 * mirage_session_layout_contains_address:
 * @self: a #MirageSession
 * @address: address to be checked
 *
 * Checks whether the session contains the given address or not.
 *
 * Returns: %TRUE if @address falls inside session, %FALSE if it does not
 */
gboolean mirage_session_layout_contains_address (MirageSession *self, gint address)
{
    return address >= self->priv->start_sector && address < self->priv->start_sector + self->priv->length;
}


/**
 * mirage_session_set_leadout_length:
 * @self: a #MirageSession
 * @length: (in): leadout length
 *
 * Sets session's leadout length to @length. It does so by creating NULL fragment
 * and adding it to leadout. This function is internally used to properly handle
 * multi-session disc layouts. The length is given in sectors.
 *
 * <note>
 * Intended for internal use only.
 * </note>
 */
void mirage_session_set_leadout_length (MirageSession *self, gint length)
{
    MirageTrack *leadout;
    MirageFragment *fragment;

    /* Get leadout - should never fail */
    leadout = mirage_session_get_track_by_number(self, MIRAGE_TRACK_LEADOUT, NULL);

    /* Now, check if leadout already has a fragment... if it has (and it should
       have only one, unless I screwed up somewhere), then we'll simply readjust
       its length. If not, we need to create it... The simplest way is to try to
       get the last fragment in track */
    fragment = mirage_track_get_fragment_by_index(leadout, -1, NULL);
    if (!fragment) {
        /* Create NULL fragment */
        fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL);
        mirage_track_add_fragment(leadout, 0, fragment);
    }

    /* Set fragment's new length */
    mirage_fragment_set_length(fragment, length);

    /* Unref fragment */
    g_object_unref(fragment);

    /* Unref leadout */
    g_object_unref(leadout);
}

/**
 * mirage_session_get_leadout_length:
 * @self: a #MirageSession
 *
 * Retrieves session's leadout length. The returned length is given in sectors.
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: leadout length
 */
gint mirage_session_get_leadout_length (MirageSession *self)
{
    MirageTrack *leadout;
    gint length;

    /* Get leadout */
    leadout = mirage_session_get_track_by_number(self, MIRAGE_TRACK_LEADOUT, NULL);
    if (!leadout) {
        return -1;
    }
    /* Get leadout's length */
    length = mirage_track_layout_get_length(leadout);
    /* Unref leadout */
    g_object_unref(leadout);

    return length;
}


/**
 * mirage_session_get_number_of_tracks:
 * @self: a #MirageSession
 *
 * Retrieves number of tracks in the session layout.
 *
 * Returns: number of tracks
 */
gint mirage_session_get_number_of_tracks (MirageSession *self)
{
    /* Return number of tracks */
    return g_list_length(self->priv->tracks_list) - 2; /* Length of list, without lead-in and lead-out */
}

/**
 * mirage_session_add_track_by_index:
 * @self: a #MirageSession
 * @index: (in): index at which track should be added
 * @track: (in) (transfer full): a #MirageTrack to be added
 *
 * Adds track to session layout.
 *
 * @index is the index at which track is added. Negative index denotes
 * index going backwards (i.e. -1 adds track at the end, -2 adds track
 * second-to-last, etc.). If index, either negative or positive, is too big,
 * track is respectively added at the beginning or at the end of the layout.
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 */
void mirage_session_add_track_by_index (MirageSession *self, gint index, MirageTrack *track)
{
    gint num_tracks;

    /* First track, last track... allow negative indexes to go from behind */
    num_tracks = mirage_session_get_number_of_tracks(self);
    if (index < -num_tracks) {
        /* If negative index is too big, put it at the beginning */
        index = 0;
    }
    if (index > num_tracks) {
        /* If positive index is too big, put it at the end */
        index = num_tracks;
    }
    if (index < 0) {
        index += num_tracks + 1;
    }

    /* We don't set track number here, because layout recalculation will do it for us */
    /* Increment reference counter */
    g_object_ref(track);
    /* Set parent */
    mirage_object_set_parent(MIRAGE_OBJECT(track), self);

    /* Insert track into track list... take into account that lead-in has index 0,
       thus all indexes should be increased by 1 */
    self->priv->tracks_list = g_list_insert(self->priv->tracks_list, track, index + 1);

    /* Connect track modified signal */
    g_signal_connect_swapped(track, "layout-changed", (GCallback)mirage_session_track_layout_changed_handler, self);

    /* Bottom-up change */
    mirage_session_commit_bottomup_change(self);
}

/**
 * mirage_session_add_track_by_number:
 * @self: a #MirageSession
 * @number: (in): track number for the added track
 * @track: (in) (transfer full): a #MirageTrack to be added
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Adds track to session layout.
 *
 * @number is track number that should be assigned to added track. It determines
 * track's position in the layout. If track with that number already exists in
 * the layout, the function fails.
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_session_add_track_by_number (MirageSession *self, gint number, MirageTrack *track, GError **error)
{
    MirageTrack *tmp_track;

    /* Check if track with that number already exists */
    tmp_track = mirage_session_get_track_by_number(self, number, NULL);
    if (tmp_track) {
        g_object_unref(tmp_track);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SESSION_ERROR, "Track with number %d already exists!", number);
        return FALSE;
    }

    /* Increment reference counter */
    g_object_ref(track);
    /* Set track number */
    mirage_track_layout_set_track_number(track, number);
    /* Set parent */
    mirage_object_set_parent(MIRAGE_OBJECT(track), self);

    /* Insert track into track list */
    self->priv->tracks_list = g_list_insert_sorted(self->priv->tracks_list, track, (GCompareFunc)sort_tracks_by_number);

    /* Connect track modified signal */
    g_signal_connect_swapped(track, "layout-changed", (GCallback)mirage_session_track_layout_changed_handler, self);

    /* Bottom-up change */
    mirage_session_commit_bottomup_change(self);

    return TRUE;
}

/**
 * mirage_session_remove_track_by_index:
 * @self: a #MirageSession
 * @index: (in): index of track to be removed
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Removes track from session layout.
 *
 * @index is the index of the track to be removed. This function calls
 * mirage_session_get_track_by_index() so @index behavior is determined by that
 * function.
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_session_remove_track_by_index (MirageSession *self, gint index, GError **error)
{
    MirageTrack *track;

    /* Find track by index */
    track = mirage_session_get_track_by_index(self, index, error);
    if (!track) {
        return FALSE;
    }

    /* Remove track from list */
    mirage_session_remove_track(self, track);
    g_object_unref(track); /* This one's from get */

    return TRUE;
}

/**
 * mirage_session_remove_track_by_number:
 * @self: a #MirageSession
 * @number: (in): track number of track to be removed
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Removes track from session layout.
 *
 * @number is track number of the track to be removed.
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_session_remove_track_by_number (MirageSession *self, gint number, GError **error)
{
    MirageTrack *track;

    /* You can't delete lead-in/lead-out */
    if (number == MIRAGE_TRACK_LEADIN || number == MIRAGE_TRACK_LEADOUT) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SESSION_ERROR, "Invalid track number %d!", number);
        return FALSE;
    }

    /* Find track in layout */
    track = mirage_session_get_track_by_number(self, number, error);
    if (!track) {
        return FALSE;
    }

    /* Remove track from list */
    mirage_session_remove_track(self, track);
    g_object_unref(track); /* This one's from get */

    return TRUE;
}

/**
 * mirage_session_remove_track_by_object:
 * @self: a #MirageSession
 * @track: (in): track object to be removed
 *
 * Removes track from session layout.
 *
 * @track is a #MirageTrack object to be removed.
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 */
void mirage_session_remove_track_by_object (MirageSession *self, MirageTrack *track)
{
    mirage_session_remove_track(self, track);
}

/**
 * mirage_session_get_track_by_index:
 * @self: a #MirageSession
 * @index: (in): index of track to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves track by index. If @index is negative, tracks from the end of
 * layout are retrieved (e.g. -1 is for last track, -2 for second-to-last
 * track, etc.). If @index is out of range, regardless of the sign, the
 * function fails.
 *
 * Returns: (transfer full): a #MirageTrack on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
MirageTrack *mirage_session_get_track_by_index (MirageSession *self, gint index, GError **error)
{
    MirageTrack *track;
    gint num_tracks;

    /* First track, last track... allow negative indexes to go from behind */
    num_tracks = mirage_session_get_number_of_tracks(self);
    if (index < -num_tracks || index >= num_tracks) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SESSION_ERROR, "Track index %d out of range!", index);
        return NULL;
    } else if (index < 0) {
        index += num_tracks;
    }

    /* Get index-th item from list... take into account the fact that index needs
       to be increased by one, because first track we'll get with following function
       will be the lead-in... */
    track = g_list_nth_data(self->priv->tracks_list, index + 1);

    if (!track) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SESSION_ERROR, "Track with index %d not found!", index);
        return NULL;
    }

    return g_object_ref(track);
}

/**
 * mirage_session_get_track_by_number:
 * @self: a #MirageSession
 * @number: (in): number of track to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves track by track number.
 *
 * Returns: (transfer full): a #MirageTrack on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
MirageTrack *mirage_session_get_track_by_number (MirageSession *self, gint track_number, GError **error)
{
    MirageTrack *track = NULL;

    /* Go over all tracks */
    for (GList *entry = self->priv->tracks_list; entry; entry = entry->next) {
        track = entry->data;

        /* Break the loop if number matches */
        if (track_number == mirage_track_layout_get_track_number(track)) {
            break;
        } else {
            track = NULL;
        }
    }

    /* If we didn't find anything... */
    if (!track) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SESSION_ERROR, "Track with number %d not found!", track_number);
        return NULL;
    }

    return g_object_ref(track);
}

/**
 * mirage_session_get_track_by_address:
 * @self: a #MirageSession
 * @address: (in): address belonging to track to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves track by address. @address must be valid (disc-relative) sector
 * address that is part of the track to be retrieved (i.e. lying between tracks's
 * start and end sector).
 *
 * Returns: (transfer full): a #MirageTrack on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
MirageTrack *mirage_session_get_track_by_address (MirageSession *self, gint address, GError **error)
{
    MirageTrack *track = NULL;

    if (!mirage_session_layout_contains_address(self, address)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SESSION_ERROR, "Track address %d out of range!", address);
        return NULL;
    }

    /* Go over all tracks */
    for (GList *entry = self->priv->tracks_list; entry; entry = entry->next) {
        track = entry->data;

        /* Break the loop if address lies within track boundaries */
        if (mirage_track_layout_contains_address(track, address)) {
            break;
        } else {
            track = NULL;
        }
    }

    /* If we didn't find anything... */
    if (!track) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SESSION_ERROR, "Track containing address %d not found!", address);
        return NULL;
    }

    return g_object_ref(track);
}

/**
 * mirage_session_enumerate_tracks:
 * @self: a #MirageSession
 * @func: (in) (scope call): callback function
 * @user_data: (in) (closure): data to be passed to callback function
 *
 * Iterates over tracks list, calling @func for each track in the layout.
 *
 * If @func returns %FALSE, the function immediately returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_session_enumerate_tracks (MirageSession *self, MirageEnumTrackCallback func, gpointer user_data)
{
    for (GList *entry = self->priv->tracks_list; entry; entry = entry->next) {
        gboolean succeeded = (*func)(entry->data, user_data);
        if (!succeeded) {
            return FALSE;
        }
    }

    return TRUE;
}

/**
 * mirage_session_get_track_before:
 * @self: a #MirageSession
 * @track: (in): a track
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves track that comes before @track.
 *
 * Returns: (transfer full): a #MirageTrack on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
MirageTrack *mirage_session_get_track_before (MirageSession *self, MirageTrack *track, GError **error)
{
    gint index;

    /* Get index of given track in the list */
    index = g_list_index(self->priv->tracks_list, track);
    if (index == -1) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SESSION_ERROR, "Track %p is not in session layout!", track);
        return NULL;
    }
    index -= 1; /* Because lead-in has index 0... */

    /* Now check if we didn't pass the first track (index = 0) and return previous one */
    if (index > 0) {
        return mirage_session_get_track_by_index(self, index - 1, error);
    }

    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, "Track before track %p not found!", track);
    return NULL;
}

/**
 * mirage_session_get_track_after:
 * @self: a #MirageSession
 * @track: (in): a track
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves track that comes after @track.
 *
 * Returns: (transfer full): a #MirageTrack on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
MirageTrack *mirage_session_get_track_after (MirageSession *self, MirageTrack *track, GError **error)
{
    gint num_tracks;
    gint index;

    /* Get index of given track in the list */
    index = g_list_index(self->priv->tracks_list, track);
    if (index == -1) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SESSION_ERROR, "Track %p is not in session layout!", track);
        return NULL;
    }
    index -= 1; /* Because lead-in has index 0... */

    /* Now check if we didn't pass the last track (index = num_tracks - 1) and return previous one */
    num_tracks = mirage_session_get_number_of_tracks(self);
    if (index < num_tracks - 1) {
        return mirage_session_get_track_by_index(self, index + 1, error);
    }

    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, "Track after track %p not found!", track);
    return NULL;
}


/**
 * mirage_session_get_number_of_languages:
 * @self: a #MirageSession
 *
 * Retrieves number of languages the session contains.
 *
 * Returns: number of languages
 */
gint mirage_session_get_number_of_languages (MirageSession *self)
{
    /* Return number of languages */
    return g_list_length(self->priv->languages_list); /* Length of list */
}

/**
 * mirage_session_add_language:
 * @self: a #MirageSession
 * @code: (in): language code for the added language
 * @language: (in) (transfer full): a #MirageLanguage to be added
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Adds language to session.
 *
 * @code is language code that should be assigned to added language. If
 * language with that code is already present in the session, the function fails.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_session_add_language (MirageSession *self, gint code, MirageLanguage *language, GError **error)
{
    MirageLanguage *tmp_language;

    /* Check if language already exists */
    tmp_language = mirage_session_get_language_by_code(self, code, NULL);
    if (tmp_language) {
        g_object_unref(tmp_language);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SESSION_ERROR, "Language with language code %d already exists!", code);
        return FALSE;
    }

    /* Increment reference counter */
    g_object_ref(language);
    /* Set language code */
    mirage_language_set_code(language, code);
    /* Set parent */
    mirage_object_set_parent(MIRAGE_OBJECT(language), self);

    /* Insert language into language list */
    self->priv->languages_list = g_list_insert_sorted(self->priv->languages_list, language, (GCompareFunc)sort_languages_by_code);

    return TRUE;
}

/**
 * mirage_session_remove_language_by_index:
 * @self: a #MirageSession
 * @index: (in): index of language to be removed
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Removes language from session.
 *
 * @index is the index of the language to be removed. This function calls
 * mirage_session_get_language_by_index() so @index behavior is determined by that
 * function.
 *
 * Returns: %TRUE on success, %FALSE on failure

 */
gboolean mirage_session_remove_language_by_index (MirageSession *self, gint index, GError **error)
{
    /* Find language by index */
    MirageLanguage *language = mirage_session_get_language_by_index(self, index, error);
    if (!language) {
        return FALSE;
    }

    /* Remove language */
    mirage_session_remove_language(self, language);
    g_object_unref(language); /* This one's from get */

    return TRUE;
}

/**
 * mirage_session_remove_language_by_code:
 * @self: a #MirageSession
 * @code: (in): language code of language to be removed
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Removes language from session.
 *
 * @code is language code the language to be removed.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_session_remove_language_by_code (MirageSession *self, gint code, GError **error)
{
    /* Find language by code */
    MirageLanguage *language = mirage_session_get_language_by_code(self, code, error);
    if (!language) {
        return FALSE;
    }

    /* Remove language */
    mirage_session_remove_language(self, language);
    g_object_unref(language); /* This one's from get */

    return TRUE;
}

/**
 * mirage_session_remove_language_by_object:
 * @self: a #MirageSession
 * @language: (in): language object to be removed
 *
 * Removes language from session.
 *
 * @language is a #MirageLanguage object to be removed.
 */
void mirage_session_remove_language_by_object (MirageSession *self, MirageLanguage *language)
{
    mirage_session_remove_language(self, language);
}

/**
 * mirage_session_get_language_by_index:
 * @self: a #MirageSession
 * @index: (in): index of language to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves language by index. If @index is negative, languages from the end of
 * session are retrieved (e.g. -1 is for last language, -2 for second-to-last
 * language, etc.). If @index is out of range, regardless of the sign, the
 * function fails.
 *
 * Returns: (transfer full): a #MirageLanguage on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
MirageLanguage *mirage_session_get_language_by_index (MirageSession *self, gint index, GError **error)
{
    MirageLanguage *language;
    gint num_languages;

    /* First language, last language... allow negative indexes to go from behind */
    num_languages = mirage_session_get_number_of_languages(self);
    if (index < -num_languages || index >= num_languages) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SESSION_ERROR, "Invalid language index %d!", index);
        return NULL;
    } else if (index < 0) {
        index += num_languages;
    }

    /* Get index-th item from list... */
    language = g_list_nth_data(self->priv->languages_list, index);

    if (!language) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SESSION_ERROR, "Language with index %d not found!", index);
        return NULL;
    }

    return g_object_ref(language);
}

/**
 * mirage_session_get_language_by_code:
 * @self: a #MirageSession
 * @code: (in): language code of language to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves language by language code.
 *
 * Returns: (transfer full): a #MirageLanguage on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
MirageLanguage *mirage_session_get_language_by_code (MirageSession *self, gint code, GError **error)
{
    MirageLanguage *language = NULL;

    /* Go over all languages */
    for (GList *entry = self->priv->languages_list; entry; entry = entry->next) {
        language = entry->data;

        /* Break the loop if code matches */
        if (code == mirage_language_get_code(language)) {
            break;
        } else {
            language = NULL;
        }
    }

    /* If we didn't find anything... */
    if (!language) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SESSION_ERROR, "Language with language code %d not found!", code);
        return FALSE;
    }

    return g_object_ref(language);
}

/**
 * mirage_session_enumerate_languages:
 * @self: a #MirageSession
 * @func: (in) (scope call): callback function
 * @user_data: (in) (closure): data to be passed to callback function
 *
 * Iterates over languages list, calling @func for each language.
 *
 * If @func returns %FALSE, the function immediately returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_session_enumerate_languages (MirageSession *self, MirageEnumLanguageCallback func, gpointer user_data)
{
    for (GList *entry = self->priv->languages_list; entry; entry = entry->next) {
        gboolean succeeded = (*func)(entry->data, user_data);
        if (!succeeded) {
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean set_cdtext_data (gint code, gint type, gint track_number, guint8 *data, gint len, MirageSession *self)
{
    gboolean succeeded;
    MirageLanguage *language;

    if (track_number == 0) {
        /* Session */
        language = mirage_session_get_language_by_code(self, code, NULL);
        if (!language) {
            /* If language does not exist, create it */
            language = g_object_new(MIRAGE_TYPE_LANGUAGE, NULL);
            if (!mirage_session_add_language(self, code, language, NULL)) {
                g_object_unref(language);
                return FALSE;
            }
        }
    } else {
        /* Track */
        MirageTrack *track = mirage_session_get_track_by_number(self, track_number, NULL);
        if (!track) {
            return FALSE;
        }

        language = mirage_track_get_language_by_code(track, code, NULL);
        if (!language) {
            /* If language does not exist, create it */
            language = g_object_new(MIRAGE_TYPE_LANGUAGE, NULL);
            if (!mirage_track_add_language(track, code, language, NULL)) {
                g_object_unref(language);
                g_object_unref(track);
                return FALSE;
            }
        }

        g_object_unref(track);
    }

    succeeded = mirage_language_set_pack_data(language, type, data, len, NULL);

    g_object_unref(language);

    return succeeded;
}

/**
 * mirage_session_set_cdtext_data:
 * @self: a #MirageSession
 * @data: (in) (array length=len): buffer containing encoded CD-TEXT data
 * @len: (in): length of data in buffer
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Sets CD-TEXT data for session. It internally creates and uses #MirageCdTextCoder
 * object as a decoder to decode data in @data. Decoded data is stored in #MirageLanguage
 * objects in both session and its tracks. Therefore session must have same number of tracks
 * as the encoded CD-TEXT data.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_session_set_cdtext_data (MirageSession *self, guint8 *data, gint len, GError **error)
{
    MirageCdTextCoder *decoder;
    gboolean succeeded = TRUE;

    /* Create decoder object and hope it'll do all the dirty work correctly... */
    decoder = g_object_new(MIRAGE_TYPE_CDTEXT_CODER, NULL);
    mirage_object_set_parent(MIRAGE_OBJECT(decoder), self);

    mirage_cdtext_decoder_init(decoder, data, len);

    for (gint i = 0; mirage_cdtext_decoder_get_block_info(decoder, i, NULL, NULL, NULL, NULL); i++) {
        succeeded = mirage_cdtext_decoder_get_data(decoder, i, (MirageCdTextDataCallback)set_cdtext_data, self);
        if (!succeeded) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SESSION_ERROR, "Failed to decode CD-TEXT data!");
            break;
        }
    }

    /* Free decoder */
    g_object_unref(decoder);

    return succeeded;
}

/**
 * mirage_session_get_cdtext_data:
 * @self: a #MirageSession
 * @data: (out callee-allocates) (transfer full) (array length=len): location to return buffer with encoded CD-TEXT data
 * @len: (out): location to return length of data in buffer
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Returns CD-TEXT data for session. It internally creates and uses #MirageCdTextCoder
 * object as an encoder to encode data from #MirageLanguage objects from both session and
 * its tracks. Buffer with encoded data is stored in @data; it should be freed with
 * g_free() when no longer needed.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_session_get_cdtext_data (MirageSession *self, guint8 **data, gint *len, GError **error)
{
    gint num_languages;
    gint num_tracks;
    gint buflen;
    guint8 *buffer;
    MirageCdTextCoder *encoder;

    /* Allocate space... technically, there could be 255 packs (each 18-byte long)
       per every language... and spec says we support 8 of those. So we play safe
       here; get number of languages and go for max number of packs */
    num_languages = mirage_session_get_number_of_languages(self);
    num_tracks = mirage_session_get_number_of_tracks(self);

    buflen = num_languages*255*18 /* Size of CD-TEXT pack */;
    buffer = g_malloc0(buflen);

    /* Set up encoder */
    encoder = g_object_new(MIRAGE_TYPE_CDTEXT_CODER, NULL);
    mirage_cdtext_encoder_init(encoder, buffer, buflen);
    mirage_object_set_parent(MIRAGE_OBJECT(encoder), self);

    /* Supported pack types */
    gint pack_types[] = {
        MIRAGE_LANGUAGE_PACK_TITLE,
        MIRAGE_LANGUAGE_PACK_PERFORMER,
        MIRAGE_LANGUAGE_PACK_SONGWRITER,
        MIRAGE_LANGUAGE_PACK_COMPOSER,
        MIRAGE_LANGUAGE_PACK_ARRANGER,
        MIRAGE_LANGUAGE_PACK_MESSAGE,
        MIRAGE_LANGUAGE_PACK_DISC_ID,
        MIRAGE_LANGUAGE_PACK_GENRE,
        MIRAGE_LANGUAGE_PACK_TOC,
        MIRAGE_LANGUAGE_PACK_TOC2,
        MIRAGE_LANGUAGE_PACK_RES_8A,
        MIRAGE_LANGUAGE_PACK_RES_8B,
        MIRAGE_LANGUAGE_PACK_RES_8C,
        MIRAGE_LANGUAGE_PACK_CLOSED_INFO,
        MIRAGE_LANGUAGE_PACK_UPC_ISRC,
        /*MIRAGE_LANGUAGE_PACK_SIZE*/
    };

    /* Add all languages' data to encoder */
    for (gint i = 0; i < num_languages; i++) {
        MirageLanguage* session_language;
        gint code;

        session_language = mirage_session_get_language_by_index(self, i, error);
        if (!session_language) {
            g_object_unref(encoder);
            g_free(buffer);
            return FALSE;
        }

        code = mirage_language_get_code(session_language);
        mirage_cdtext_encoder_set_block_info(encoder, i, code, 0, 0, NULL);

        /* Pack all supported pack types */
        for (gint j = 0; j < G_N_ELEMENTS(pack_types); j++) {
            gint pack_type = pack_types[j];

            const guint8 *session_language_data = NULL;
            gint session_language_len = 0;

            if (mirage_language_get_pack_data(session_language, pack_type, &session_language_data, &session_language_len, NULL)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_SESSION, "%s: adding pack for session; pack type: %02Xh; pack len: %i; pack data: <%s>\n", __debug__, pack_type, session_language_len, session_language_data);
                mirage_cdtext_encoder_add_data(encoder, code, pack_type, 0, session_language_data, session_language_len);
            }

            /* Now get and pack the same data for the all tracks */
            for (gint k = 0; k < num_tracks; k++) {
                MirageTrack *track;
                gint number;

                MirageLanguage *track_language;
                const guint8 *track_language_data;
                gint track_language_len;

                track = mirage_session_get_track_by_index(self, k, NULL);
                number = mirage_track_layout_get_track_number(track);

                track_language = mirage_track_get_language_by_code(track, code, NULL);
                if (!track_language) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Failed to get language with code %i on track %i!\n", __debug__, code, number);
                    g_object_unref(track);
                    continue;
                }

                if (mirage_language_get_pack_data(track_language, pack_type, &track_language_data, &track_language_len, NULL)) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_SESSION, "%s: adding pack for track %i; pack type: %02Xh; pack len: %i; pack data: <%s>\n", __debug__, number, pack_type, track_language_len, track_language_data);
                    mirage_cdtext_encoder_add_data(encoder, code, pack_type, number, track_language_data, track_language_len);
                }

                g_object_unref(track_language);
                g_object_unref(track);
            }

        }

        g_object_unref(session_language);
    }

    /* Encode */
    mirage_cdtext_encoder_encode(encoder, data, len);

    /* Free encoder */
    g_object_unref(encoder);

    return TRUE; /* TODO: Function needs error checking. */
}


/**
 * mirage_session_get_prev:
 * @self: a #MirageSession
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves session that is placed before @self in disc layout.
 *
 * Returns: (transfer full): a #MirageSession on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
MirageSession *mirage_session_get_prev (MirageSession *self, GError **error)
{
    MirageDisc *disc;
    MirageSession *session;

    /* Get parent disc */
    disc = mirage_object_get_parent(MIRAGE_OBJECT(self));
    if (!disc) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SESSION_ERROR, "Session is not in disc layout!");
        return FALSE;
    }

    session = mirage_disc_get_session_before(disc, self, error);
    g_object_unref(disc);

    return session;
}

/**
 * mirage_session_get_next:
 * @self: a #MirageSession
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves session that is placed after @self in disc layout.
 *
 * Returns: (transfer full): a #MirageSession on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
MirageSession *mirage_session_get_next (MirageSession *self, GError **error)
{
    MirageDisc *disc;
    MirageSession *session;

    /* Get parent disc */
    disc = mirage_object_get_parent(MIRAGE_OBJECT(self));
    if (!disc) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SESSION_ERROR, "Session is not in disc layout!");
        return FALSE;
    }

    session = mirage_disc_get_session_before(disc, self, error);
    g_object_unref(disc);

    return session;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE(MirageSession, mirage_session, MIRAGE_TYPE_OBJECT);


static void mirage_session_init (MirageSession *self)
{
    MirageTrack *track;

    self->priv = MIRAGE_SESSION_GET_PRIVATE(self);

    self->priv->mcn = NULL;
    self->priv->mcn_fixed = FALSE;
    self->priv->mcn_scan_complete = TRUE;

    self->priv->session_type = MIRAGE_SESSION_CD_ROM;

    self->priv->tracks_list = NULL;
    self->priv->languages_list = NULL;

    self->priv->session_number = 1;
    self->priv->first_track = 1;

    /* Create lead-in */
    track = g_object_new(MIRAGE_TYPE_TRACK, NULL);
    mirage_session_add_track_by_number(self, MIRAGE_TRACK_LEADIN, track, NULL);
    g_object_unref(track);

    /* Create lead-out */
    track = g_object_new(MIRAGE_TYPE_TRACK, NULL);
    mirage_session_add_track_by_number(self, MIRAGE_TRACK_LEADOUT, track, NULL);
    g_object_unref(track);
}

static void mirage_session_dispose (GObject *gobject)
{
    MirageSession *self = MIRAGE_SESSION(gobject);

    /* Unref tracks */
    for (GList *entry = self->priv->tracks_list; entry; entry = entry->next) {
        if (entry->data) {
            MirageTrack *track = entry->data;
            /* Disconnect signal handler and unref */
            g_signal_handlers_disconnect_by_func(track, mirage_session_track_layout_changed_handler, self);
            g_object_unref(track);

            entry->data = NULL;
        }
    }

    /* Unref languages */
    for (GList *entry = self->priv->languages_list; entry; entry = entry->next) {
        if (entry->data) {
            MirageLanguage *language = entry->data;
            g_object_unref(language);

            entry->data = NULL;
        }
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_session_parent_class)->dispose(gobject);
}

static void mirage_session_finalize (GObject *gobject)
{
    MirageSession *self = MIRAGE_SESSION(gobject);

    g_free(self->priv->mcn);

    g_list_free(self->priv->tracks_list);
    g_list_free(self->priv->languages_list);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_session_parent_class)->finalize(gobject);
}

static void mirage_session_class_init (MirageSessionClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = mirage_session_dispose;
    gobject_class->finalize = mirage_session_finalize;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageSessionPrivate));

    /* Signals */
    /**
     * MirageSession::layout-changed:
     * @session: a #MirageSession
     *
     * Emitted when a layout of #MirageSession changed in a way that causes a bottom-up change.
     */
    g_signal_new("layout-changed", G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, NULL);
}
