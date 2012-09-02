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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"

#define __debug__ "Session"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_SESSION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_SESSION, MIRAGE_SessionPrivate))

struct _MIRAGE_SessionPrivate
{
    /* Layout settings */
    gint session_number; /* Session number */
    gint start_sector; /* Start sector */
    gint first_track; /* Number of the first track in session */
    gint length; /* Length of session (sum of tracks' length) */

    /* Session type */
    gint session_type;

    /* Tracks list */
    GList *tracks_list;

    /* CD-Text list */
    GList *languages_list;
} ;


/**********************************************************************\
 *                          Private functions                         *
\**********************************************************************/
static void mirage_session_commit_topdown_change (MIRAGE_Session *self)
{
    GList *entry;

    /* Rearrange tracks: set numbers, set start sectors */
    gint cur_track_address = self->priv->start_sector;
    gint cur_track_number  = self->priv->first_track;

    G_LIST_FOR_EACH(entry, self->priv->tracks_list) {
        GObject *track = entry->data;

        /* Set track's number */
        gint old_number = mirage_track_layout_get_track_number(MIRAGE_TRACK(track));
        if ((old_number != MIRAGE_TRACK_LEADIN) && (old_number != MIRAGE_TRACK_LEADOUT)) {
            mirage_track_layout_set_track_number(MIRAGE_TRACK(track), cur_track_number);
            cur_track_number++;
        }

        /* Set track's start address */
        mirage_track_layout_set_start_sector(MIRAGE_TRACK(track), cur_track_address);
        cur_track_address += mirage_track_layout_get_length(MIRAGE_TRACK(track));
    }
}

static void mirage_session_commit_bottomup_change (MIRAGE_Session *self)
{
    GList *entry;
    GObject *disc;

    /* Calculate session length */
    self->priv->length = 0; /* Reset; it'll be recalculated */

    G_LIST_FOR_EACH(entry, self->priv->tracks_list) {
        GObject *track = entry->data;
        self->priv->length += mirage_track_layout_get_length(MIRAGE_TRACK(track));
    }

    /* Signal session change */
    g_signal_emit_by_name(MIRAGE_OBJECT(self), "object-modified", NULL);
    /* If we don't have parent, we should complete the arc by committing top-down change */
    disc = mirage_object_get_parent(MIRAGE_OBJECT(self));
    if (!disc) {
        mirage_session_commit_topdown_change(self);
    } else {
        g_object_unref(disc);
    }
}

static void mirage_session_track_modified_handler (GObject *track G_GNUC_UNUSED, MIRAGE_Session *self)
{
    /* Bottom-up change */
    mirage_session_commit_bottomup_change(self);
}

static void mirage_session_remove_track (MIRAGE_Session *self, GObject *track)
{
    /* Disconnect signal handler (find it by handler function and user data) */
    g_signal_handlers_disconnect_by_func(MIRAGE_OBJECT(track), mirage_session_track_modified_handler, self);

    /* Remove track from list and unref it */
    self->priv->tracks_list = g_list_remove(self->priv->tracks_list, track);
    g_object_unref(track);

    /* Bottom-up change */
    mirage_session_commit_bottomup_change(self);
}

static void mirage_session_remove_language (MIRAGE_Session *self, GObject *language)
{
    /* Remove it from list and unref it */
    self->priv->languages_list = g_list_remove(self->priv->languages_list, language);
    g_object_unref(language);
}


static gint sort_languages_by_code (GObject *language1, GObject *language2)
{
    gint code1 = mirage_language_get_langcode(MIRAGE_LANGUAGE(language1));
    gint code2 = mirage_language_get_langcode(MIRAGE_LANGUAGE(language2));

    if (code1 < code2) {
        return -1;
    } else if (code1 > code2) {
        return 1;
    } else {
        return 0;
    }
}

static gint sort_tracks_by_number (GObject *track1, GObject *track2)
{
    gint number1 = mirage_track_layout_get_track_number(MIRAGE_TRACK(track1));
    gint number2 = mirage_track_layout_get_track_number(MIRAGE_TRACK(track2));

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
 * @self: a #MIRAGE_Session
 * @type: (in); session type
 *
 * <para>
 * Sets session type. @type must be one of #MIRAGE_SessionTypes.
 * </para>
 **/
void mirage_session_set_session_type (MIRAGE_Session *self, gint type)
{
    /* Set session type */
    self->priv->session_type =type;
}

/**
 * mirage_session_get_session_type:
 * @self: a #MIRAGE_Session
 *
 * <para>
 * Retrieves session type.
 * </para>
 *
 * Returns: session type
 **/
gint mirage_session_get_session_type (MIRAGE_Session *self)
{
    /* Return session number */
    return self->priv->session_type;
}


/**
 * mirage_session_layout_set_session_number:
 * @self: a #MIRAGE_Session
 * @number: (in): session number
 *
 * <para>
 * Sets sessions's session number.
 * </para>
 *
 * <note>
 * Intended for internal use only.
 * </note>
 **/
void mirage_session_layout_set_session_number (MIRAGE_Session *self, gint number)
{
    /* Set session number */
    self->priv->session_number = number;
}

/**
 * mirage_session_layout_get_session_number:
 * @self: a #MIRAGE_Session
 *
 * <para>
 * Retrieves sessions's session number.
 * </para>
 *
 * Returns: session number
 **/
gint mirage_session_layout_get_session_number (MIRAGE_Session *self)
{
    /* Return session number */
    return self->priv->session_number;
}

/**
 * mirage_session_layout_set_first_track:
 * @self: a #MIRAGE_Session
 * @first_track: (in): first track number
 *
 * <para>
 * Sets first track number to @first_track. This is a number that is
 * assigned to the first track in the session layout.
 * </para>
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * <note>
 * Causes top-down change.
 * </note>
 **/
void mirage_session_layout_set_first_track (MIRAGE_Session *self, gint first_track)
{
    /* Set first track */
    self->priv->first_track = first_track;
    /* Top-down change */
    mirage_session_commit_topdown_change(self);
}

/**
 * mirage_session_layout_get_first_track:
 * @self: a #MIRAGE_Session
 *
 * <para>
 * Retrieves track number of the first track in the session layout.
 * </para>
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: first track number
 **/
gint mirage_session_layout_get_first_track (MIRAGE_Session *self)
{
    /* Return first track */
    return self->priv->first_track;
}

/**
 * mirage_session_layout_set_start_sector:
 * @self: a #MIRAGE_Session
 * @start_sector: (in): start sector
 *
 * <para>
 * Sets start sector of the session layout to @start_sector. This is a sector at which
 * the first track in the session layout will start.
 * </para>
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * <note>
 * Causes top-down change.
 * </note>
 **/
void mirage_session_layout_set_start_sector (MIRAGE_Session *self, gint start_sector)
{
    /* Set start sector */
    self->priv->start_sector = start_sector;
    /* Top-down change */
    mirage_session_commit_topdown_change(self);
}

/**
 * mirage_session_layout_get_start_sector:
 * @self: a #MIRAGE_Session
 *
 * <para>
 * Retrieves start sector of the session layout.
 * </para>
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: start sector
 **/
gint mirage_session_layout_get_start_sector (MIRAGE_Session *self)
{
    /* Return start sector */
    return self->priv->start_sector;
}

/**
 * mirage_session_layout_get_length:
 * @self: a #MIRAGE_Session
 *
 * <para>
 * Retrieves length of the session layout. This means the length of
 * all tracks combined, including lead-in and lead-out tracks. The returned
 * length is given in sectors.
 * </para>
 *
 * Returns: session layout length
 **/
gint mirage_session_layout_get_length (MIRAGE_Session *self)
{
    /* Return length */
    return self->priv->length;
}


/**
 * mirage_session_set_leadout_length:
 * @self: a #MIRAGE_Session
 * @length: (in): leadout length
 *
 * <para>
 * Sets session's leadout length to @length. It does so by creating NULL fragment
 * and adding it to leadout. This function is internally used to properly handle
 * multi-session disc layouts. The length is given in sectors.
 * </para>
 *
 * <note>
 * Intended for internal use only.
 * </note>
 **/
void mirage_session_set_leadout_length (MIRAGE_Session *self, gint length)
{
    GObject *leadout;
    GObject *fragment;

    /* Get leadout - should never fail */
    leadout = mirage_session_get_track_by_number(self, MIRAGE_TRACK_LEADOUT, NULL);

    /* Now, check if leadout already has a fragment... if it has (and it should
       have only one, unless I screwed up somewhere), then we'll simply readjust
       its length. If not, we need to create it... The simplest way is to try to
       get the last fragment in track */
    fragment = mirage_track_get_fragment_by_index(MIRAGE_TRACK(leadout), -1, NULL);
    if (!fragment) {
        /* Create NULL fragment - should never fail */
        fragment = libmirage_create_fragment(MIRAGE_TYPE_FRAG_IFACE_NULL, NULL, G_OBJECT(self), NULL);
        mirage_track_add_fragment(MIRAGE_TRACK(leadout), 0, fragment);
    }

    /* Set fragment's new length */
    mirage_fragment_set_length(MIRAGE_FRAGMENT(fragment), length);

    /* Unref fragment */
    g_object_unref(fragment);

    /* Unref leadout */
    g_object_unref(leadout);
}

/**
 * mirage_session_get_leadout_length:
 * @self: a #MIRAGE_Session
 *
 * <para>
 * Retrieves session's leadout length. The returned length is given in sectors.
 * </para>
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: leadout length
 **/
gint mirage_session_get_leadout_length (MIRAGE_Session *self)
{
    GObject *leadout;
    gint length;

    /* Get leadout */
    leadout = mirage_session_get_track_by_number(self, MIRAGE_TRACK_LEADOUT, NULL);
    if (!leadout) {
        return -1;
    }
    /* Get leadout's length */
    length = mirage_track_layout_get_length(MIRAGE_TRACK(leadout));
    /* Unref leadout */
    g_object_unref(leadout);

    return length;
}


/**
 * mirage_session_get_number_of_tracks:
 * @self: a #MIRAGE_Session
 *
 * <para>
 * Retrieves number of tracks in the session layout.
 * </para>
 *
 * Returns: number of tracks
 **/
gint mirage_session_get_number_of_tracks (MIRAGE_Session *self)
{
    /* Return number of tracks */
    return g_list_length(self->priv->tracks_list) - 2; /* Length of list, without lead-in and lead-out */
}

/**
 * mirage_session_add_track_by_index:
 * @self: a #MIRAGE_Session
 * @index: (in): index at which track should be added
 * @track: (in) (transfer full): a #MIRAGE_Track to be added
 *
 * <para>
 * Adds track to session layout.
 * </para>
 *
 * <para>
 * @index is the index at which track is added. Negative index denotes
 * index going backwards (i.e. -1 adds track at the end, -2 adds track
 * second-to-last, etc.). If index, either negative or positive, is too big,
 * track is respectively added at the beginning or at the end of the layout.
 * </para>
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 **/
void mirage_session_add_track_by_index (MIRAGE_Session *self, gint index, GObject *track)
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
    mirage_object_set_parent(MIRAGE_OBJECT(track), G_OBJECT(self));
    /* Attach child */
    mirage_object_attach_child(MIRAGE_OBJECT(self), track);

    /* Insert track into track list... take into account that lead-in has index 0,
       thus all indexes should be increased by 1 */
    self->priv->tracks_list = g_list_insert(self->priv->tracks_list, track, index + 1);

    /* Connect track modified signal */
    g_signal_connect(MIRAGE_OBJECT(track), "object-modified", (GCallback)mirage_session_track_modified_handler, self);

    /* Bottom-up change */
    mirage_session_commit_bottomup_change(self);
}

/**
 * mirage_session_add_track_by_number:
 * @self: a #MIRAGE_Session
 * @number: (in): track number for the added track
 * @track: (in) (transfer full): a #MIRAGE_Track to be added
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Adds track to session layout.
 * </para>
 *
 * <para>
 * @number is track number that should be assigned to added track. It determines
 * track's position in the layout. If track with that number already exists in
 * the layout, the function fails.
 * </para>
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_add_track_by_number (MIRAGE_Session *self, gint number, GObject *track, GError **error)
{
    GObject *tmp_track;

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
    mirage_track_layout_set_track_number(MIRAGE_TRACK(track), number);
    /* Set parent */
    mirage_object_set_parent(MIRAGE_OBJECT(track), G_OBJECT(self));
    /* Attach child */
    mirage_object_attach_child(MIRAGE_OBJECT(self), track);

    /* Insert track into track list */
    self->priv->tracks_list = g_list_insert_sorted(self->priv->tracks_list, track, (GCompareFunc)sort_tracks_by_number);

    /* Connect track modified signal */
    g_signal_connect(MIRAGE_OBJECT(track), "object-modified", (GCallback)mirage_session_track_modified_handler, self);

    /* Bottom-up change */
    mirage_session_commit_bottomup_change(self);

    return TRUE;
}

/**
 * mirage_session_remove_track_by_index:
 * @self: a #MIRAGE_Session
 * @index: (in): index of track to be removed
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Removes track from session layout.
 * </para>
 *
 * <para>
 * @index is the index of the track to be removed. This function calls
 * mirage_session_get_track_by_index() so @index behavior is determined by that
 * function.
 * </para>
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_remove_track_by_index (MIRAGE_Session *self, gint index, GError **error)
{
    GObject *track;

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
 * @self: a #MIRAGE_Session
 * @number: (in): track number of track to be removed
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Removes track from session layout.
 * </para>
 *
 * <para>
 * @number is track number of the track to be removed.
 * </para>
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_remove_track_by_number (MIRAGE_Session *self, gint number, GError **error)
{
    GObject *track;

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
 * @self: a #MIRAGE_Session
 * @track: (in): track object to be removed
 *
 * <para>
 * Removes track from session layout.
 * </para>
 *
 * <para>
 * @track is a #MIRAGE_Track object to be removed.
 * </para>
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 **/
void mirage_session_remove_track_by_object (MIRAGE_Session *self, GObject *track)
{
    mirage_session_remove_track(self, track);
}

/**
 * mirage_session_get_track_by_index:
 * @self: a #MIRAGE_Session
 * @index: (in): index of track to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves track by index. If @index is negative, tracks from the end of
 * layout are retrieved (e.g. -1 is for last track, -2 for second-to-last
 * track, etc.). If @index is out of range, regardless of the sign, the
 * function fails.
 * </para>
 *
 * Returns: (transfer full): a #MIRAGE_Track on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_session_get_track_by_index (MIRAGE_Session *self, gint index, GError **error)
{
    GObject *track;
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

    g_object_ref(track);
    return track;
}

/**
 * mirage_session_get_track_by_number:
 * @self: a #MIRAGE_Session
 * @number: (in): number of track to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves track by track number.
 * </para>
 *
 * Returns: (transfer full): a #MIRAGE_Track on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_session_get_track_by_number (MIRAGE_Session *self, gint track_number, GError **error)
{
    GObject *track;
    GList *entry;

    /* Go over all tracks */
    track = NULL;
    G_LIST_FOR_EACH(entry, self->priv->tracks_list) {
        track = entry->data;

        /* Break the loop if number matches */
        if (track_number == mirage_track_layout_get_track_number(MIRAGE_TRACK(track))) {
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

    g_object_ref(track);
    return track;
}

/**
 * mirage_session_get_track_by_address:
 * @self: a #MIRAGE_Session
 * @address: (in): address belonging to track to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves track by address. @address must be valid (disc-relative) sector
 * address that is part of the track to be retrieved (i.e. lying between tracks's
 * start and end sector).
 * </para>
 *
 * Returns: (transfer full): a #MIRAGE_Track on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_session_get_track_by_address (MIRAGE_Session *self, gint address, GError **error)
{
    GObject *track;
    GList *entry;

    if ((address < self->priv->start_sector) || (address >= (self->priv->start_sector + self->priv->length))) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SESSION_ERROR, "Track address %d out of range!", address);
        return NULL;
    }

    /* Go over all tracks */
    track = NULL;
    G_LIST_FOR_EACH(entry, self->priv->tracks_list) {
        gint start_sector;
        gint length;

        track = entry->data;

        start_sector = mirage_track_layout_get_start_sector(MIRAGE_TRACK(track));
        length = mirage_track_layout_get_length(MIRAGE_TRACK(track));

        /* Break the loop if address lies within track boundaries */
        if (address >= start_sector && address < start_sector + length) {
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

    g_object_ref(track);
    return track;
}

/**
 * mirage_session_for_each_track:
 * @self: a #MIRAGE_Session
 * @func: (in) (closure closure): callback function
 * @user_data: (in) (closure): data to be passed to callback function
 *
 * <para>
 * Iterates over tracks list, calling @func for each track in the layout.
 * </para>
 *
 * <para>
 * If @func returns %FALSE, the function immediately returns %FALSE.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_for_each_track (MIRAGE_Session *self, MIRAGE_CallbackFunction func, gpointer user_data)
{
    GList *entry;

    G_LIST_FOR_EACH(entry, self->priv->tracks_list) {
        gboolean succeeded = (*func) (MIRAGE_TRACK(entry->data), user_data);
        if (!succeeded) {
            return FALSE;
        }
    }

    return TRUE;
}

/**
 * mirage_session_get_track_before:
 * @self: a #MIRAGE_Session
 * @track: (in): a track
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves track that comes before @track.
 * </para>
 *
 * Returns: (transfer full): a #MIRAGE_Track on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_session_get_track_before (MIRAGE_Session *self, GObject *track, GError **error)
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
 * @self: a #MIRAGE_Session
 * @track: (in): a track
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves track that comes after @track.
 * </para>
 *
 * Returns: (transfer full): a #MIRAGE_Track on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_session_get_track_after (MIRAGE_Session *self, GObject *track, GError **error)
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
 * @self: a #MIRAGE_Session
 *
 * <para>
 * Retrieves number of languages the session contains.
 * </para>
 *
 * Returns: number of languages
 **/
gint mirage_session_get_number_of_languages (MIRAGE_Session *self)
{
    /* Return number of languages */
    return g_list_length(self->priv->languages_list); /* Length of list */
}

/**
 * mirage_session_add_language:
 * @self: a #MIRAGE_Session
 * @langcode: (in): language code for the added language
 * @language: (in) (transfer full): a #MIRAGE_Language to be added
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Adds language to session.
 * </para>
 *
 * <para>
 * @langcode is language code that should be assigned to added language. If
 * language with that code is already present in the session, the function fails.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_add_language (MIRAGE_Session *self, gint langcode, GObject *language, GError **error)
{
    GObject *tmp_language;

    /* Check if language already exists */
    tmp_language = mirage_session_get_language_by_code(self, langcode, NULL);
    if (tmp_language) {
        g_object_unref(tmp_language);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SESSION_ERROR, "Language with language code %d already exists!", langcode);
        return FALSE;
    }

    /* Increment reference counter */
    g_object_ref(language);
    /* Set language code */
    mirage_language_set_langcode(MIRAGE_LANGUAGE(language), langcode);
    /* Set parent */
    mirage_object_set_parent(MIRAGE_OBJECT(language), G_OBJECT(self));
    /* Attach child */
    mirage_object_attach_child(MIRAGE_OBJECT(self), language);

    /* Insert language into language list */
    self->priv->languages_list = g_list_insert_sorted(self->priv->languages_list, language, (GCompareFunc)sort_languages_by_code);

    return TRUE;
}

/**
 * mirage_session_remove_language_by_index:
 * @self: a #MIRAGE_Session
 * @index: (in): index of language to be removed
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Removes language from session.
 * </para>
 *
 * <para>
 * @index is the index of the language to be removed. This function calls
 * mirage_session_get_language_by_index() so @index behavior is determined by that
 * function.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure

 **/
gboolean mirage_session_remove_language_by_index (MIRAGE_Session *self, gint index, GError **error)
{
    /* Find language by index */
    GObject *language = mirage_session_get_language_by_index(self, index, error);
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
 * @self: a #MIRAGE_Session
 * @langcode: (in): language code of language to be removed
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Removes language from session.
 * </para>
 *
 * <para>
 * @langcode is language code the language to be removed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_remove_language_by_code (MIRAGE_Session *self, gint langcode, GError **error)
{
    /* Find language by code */
    GObject *language = mirage_session_get_language_by_code(self, langcode, error);
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
 * @self: a #MIRAGE_Session
 * @language: (in): language object to be removed
 *
 * <para>
 * Removes language from session.
 * </para>
 *
 * <para>
 * @language is a #MIRAGE_Language object to be removed.
 * </para>
 **/
void mirage_session_remove_language_by_object (MIRAGE_Session *self, GObject *language)
{
    mirage_session_remove_language(self, language);
}

/**
 * mirage_session_get_language_by_index:
 * @self: a #MIRAGE_Session
 * @index: (in): index of language to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves language by index. If @index is negative, languages from the end of
 * session are retrieved (e.g. -1 is for last language, -2 for second-to-last
 * language, etc.). If @index is out of range, regardless of the sign, the
 * function fails.
 * </para>
 *
 * Returns: (transfer full): a #MIRAGE_Language on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_session_get_language_by_index (MIRAGE_Session *self, gint index, GError **error)
{
    GObject *language;
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

    g_object_ref(language);
    return language;
}

/**
 * mirage_session_get_language_by_code:
 * @self: a #MIRAGE_Session
 * @langcode: (in): language code of language to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves language by language code.
 * </para>
 *
 * Returns: (transfer full): a #MIRAGE_Language on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_session_get_language_by_code (MIRAGE_Session *self, gint langcode, GError **error)
{
    GObject *language;
    GList *entry;

    /* Go over all languages */
    language = NULL;
    G_LIST_FOR_EACH(entry, self->priv->languages_list) {
        language = entry->data;

        /* Break the loop if code matches */
        if (langcode == mirage_language_get_langcode(MIRAGE_LANGUAGE(language))) {
            break;
        } else {
            language = NULL;
        }
    }

    /* If we didn't find anything... */
    if (!language) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SESSION_ERROR, "Language with language code %d not found!", langcode);
        return FALSE;
    }

    g_object_ref(language);
    return language;
}

/**
 * mirage_session_for_each_language:
 * @self: a #MIRAGE_Session
 * @func: (in) (closure closure): callback function
 * @user_data: (in) (closure): data to be passed to callback function
 *
 * <para>
 * Iterates over languages list, calling @func for each language.
 * </para>
 *
 * <para>
 * If @func returns %FALSE, the function immediately returns %FALSE.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_for_each_language (MIRAGE_Session *self, MIRAGE_CallbackFunction func, gpointer user_data)
{
    GList *entry;

    G_LIST_FOR_EACH(entry, self->priv->languages_list) {
        gboolean succeeded = (*func) (MIRAGE_LANGUAGE(entry->data), user_data);
        if (!succeeded) {
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean set_cdtext_data (gint langcode, gint type, gint track_number, guint8 *data, gint len, MIRAGE_Session *self)
{
    gboolean succeeded;
    GObject *language;

    if (track_number == 0) {
        /* Session */
        language = mirage_session_get_language_by_code(self, langcode, NULL);
        if (!language) {
            /* If language does not exist, create it */
            language = g_object_new(MIRAGE_TYPE_LANGUAGE, NULL);
            if (!mirage_session_add_language(self, langcode, language, NULL)) {
                g_object_unref(language);
                return FALSE;
            }
        }
    } else {
        /* Track */
        GObject *track = mirage_session_get_track_by_number(self, track_number, NULL);
        if (!track) {
            return FALSE;
        }

        language = mirage_track_get_language_by_code(MIRAGE_TRACK(track), langcode, NULL);
        if (!language) {
            /* If language does not exist, create it */
            language = g_object_new(MIRAGE_TYPE_LANGUAGE, NULL);
            if (!mirage_track_add_language(MIRAGE_TRACK(track), langcode, language, NULL)) {
                g_object_unref(language);
                g_object_unref(track);
                return FALSE;
            }
        }

        g_object_unref(track);
    }

    succeeded = mirage_language_set_pack_data(MIRAGE_LANGUAGE(language), type, (gchar *)data, len, NULL);

    g_object_unref(language);

    return succeeded;
}

/**
 * mirage_session_set_cdtext_data:
 * @self: a #MIRAGE_Session
 * @data: (in): buffer containing encoded CD-TEXT data
 * @len: (in): length of data in buffer
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Sets CD-TEXT data for session. It internally creates and uses #MIRAGE_CDTextEncDec
 * object as a decoder to decode data in @data. Decoded data is stored in #MIRAGE_Language
 * objects in both session and its tracks. Therefore session must have same number of tracks
 * as the encoded CD-TEXT data.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_set_cdtext_data (MIRAGE_Session *self, guint8 *data, gint len, GError **error)
{
    GObject *decoder;
    gboolean succeeded = TRUE;
    gint i;

    /* Create decoder object and hope it'll do all the dirty work correctly... */
    decoder = g_object_new(MIRAGE_TYPE_CDTEXT_ENCDEC, NULL);
    mirage_object_attach_child(MIRAGE_OBJECT(self), decoder);

    mirage_cdtext_decoder_init(MIRAGE_CDTEXT_ENCDEC(decoder), data, len);

    for (i = 0; mirage_cdtext_decoder_get_block_info(MIRAGE_CDTEXT_ENCDEC(decoder), i, NULL, NULL, NULL, NULL); i++) {
        succeeded = mirage_cdtext_decoder_get_data(MIRAGE_CDTEXT_ENCDEC(decoder), i, (MIRAGE_CDTextDataCallback)set_cdtext_data, self);
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
 * @self: a #MIRAGE_Session
 * @data: (out) (transfer full): location to return buffer with encoded CD-TEXT data
 * @len: (out): location to return length of data in buffer
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Returns CD-TEXT data for session. It internally creates and uses #MIRAGE_CDTextEncDec
 * object as an encoder to encode data from #MIRAGE_Language objects from both session and
 * its tracks. Buffer with encoded data is stored in @data; it should be freed with
 * g_free() when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_get_cdtext_data (MIRAGE_Session *self, guint8 **data, gint *len, GError **error)
{
    gint num_languages;
    gint num_tracks;
    gint buflen;
    guint8 *buffer;
    GObject *encoder;
    gboolean succeeded;

    /* Allocate space... technically, there could be 255 packs (each 18-byte long)
       per every language... and spec says we support 8 of those. So we play safe
       here; get number of languages and go for max number of packs */
    num_languages = mirage_session_get_number_of_languages(self);
    num_tracks = mirage_session_get_number_of_tracks(self);

    buflen = num_languages*255*18 /* Size of CD-TEXT pack */;
    buffer = g_malloc0(buflen);

    /* Set up encoder */
    encoder = g_object_new(MIRAGE_TYPE_CDTEXT_ENCDEC, NULL);
    mirage_cdtext_encoder_init(MIRAGE_CDTEXT_ENCDEC(encoder), buffer, buflen);
    mirage_object_attach_child(MIRAGE_OBJECT(self), encoder);

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
    gint i, j, k;

    /* Add all languages' data to encoder */
    for (i = 0; i < num_languages; i++) {
        GObject* session_language;
        gint langcode;

        session_language = mirage_session_get_language_by_index(self, i, error);
        if (!session_language) {
            g_object_unref(encoder);
            g_free(buffer);
            return FALSE;
        }

        langcode = mirage_language_get_langcode(MIRAGE_LANGUAGE(session_language));
        mirage_cdtext_encoder_set_block_info(MIRAGE_CDTEXT_ENCDEC(encoder), i, langcode, 0, 0, NULL);

        /* Pack all supported pack types */
        for (j = 0; j < G_N_ELEMENTS(pack_types); j++) {
            gint pack_type = pack_types[j];

            const guint8 *session_language_data = NULL;
            gint session_language_len = 0;

            if (mirage_language_get_pack_data(MIRAGE_LANGUAGE(session_language), pack_type, (const gchar **)&session_language_data, &session_language_len, NULL)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_SESSION, "%s: adding pack for session; pack type: %02Xh; pack len: %i; pack data: <%s>\n", __debug__, pack_type, session_language_len, session_language_data);
                mirage_cdtext_encoder_add_data(MIRAGE_CDTEXT_ENCDEC(encoder), langcode, pack_type, 0, session_language_data, session_language_len);
            }

            /* Now get and pack the same data for the all tracks */
            for (k = 0; k < num_tracks; k++) {
                GObject *track;
                gint number;

                GObject *track_language;
                const guint8 *track_language_data;
                gint track_language_len;

                track = mirage_session_get_track_by_index(self, k, NULL);
                number = mirage_track_layout_get_track_number(MIRAGE_TRACK(track));

                track_language = mirage_track_get_language_by_code(MIRAGE_TRACK(track), langcode, NULL);
                if (!track_language) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Failed to get language with code %i on track %i!\n", __debug__, langcode, number);
                    g_object_unref(track);
                    continue;
                }

                if (mirage_language_get_pack_data(MIRAGE_LANGUAGE(track_language), pack_type, (const gchar **)&track_language_data, &track_language_len, NULL)) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_SESSION, "%s: adding pack for track %i; pack type: %02Xh; pack len: %i; pack data: <%s>\n", __debug__, number, pack_type, track_language_len, track_language_data);
                    mirage_cdtext_encoder_add_data(MIRAGE_CDTEXT_ENCDEC(encoder), langcode, pack_type, number, track_language_data, track_language_len);
                }

                g_object_unref(track_language);
                g_object_unref(track);
            }

        }

        g_object_unref(session_language);
    }

    /* Encode */
    mirage_cdtext_encoder_encode(MIRAGE_CDTEXT_ENCDEC(encoder), data, len);

    /* Free encoder */
    g_object_unref(encoder);

    return succeeded;
}


/**
 * mirage_session_get_prev:
 * @self: a #MIRAGE_Session
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves session that is placed before @self in disc layout.
 * </para>
 *
 * Returns: (transfer full): a #MIRAGE_Session on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_session_get_prev (MIRAGE_Session *self, GError **error)
{
    GObject *disc;
    GObject *session;

    /* Get parent disc */
    disc = mirage_object_get_parent(MIRAGE_OBJECT(self));
    if (!disc) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SESSION_ERROR, "Session is not in disc layout!");
        return FALSE;
    }

    session = mirage_disc_get_session_before(MIRAGE_DISC(disc), G_OBJECT(self), error);
    g_object_unref(disc);

    return session;
}

/**
 * mirage_session_get_next:
 * @self: a #MIRAGE_Session
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves session that is placed after @self in disc layout.
 * </para>
 *
 * Returns: (transfer full): a #MIRAGE_Session on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_session_get_next (MIRAGE_Session *self, GError **error)
{
    GObject *disc;
    GObject *session;

    /* Get parent disc */
    disc = mirage_object_get_parent(MIRAGE_OBJECT(self));
    if (!disc) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SESSION_ERROR, "Session is not in disc layout!");
        return FALSE;
    }

    session = mirage_disc_get_session_before(MIRAGE_DISC(disc), G_OBJECT(self), error);
    g_object_unref(disc);

    return session;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE(MIRAGE_Session, mirage_session, MIRAGE_TYPE_OBJECT);


static void mirage_session_init (MIRAGE_Session *self)
{
    self->priv = MIRAGE_SESSION_GET_PRIVATE(self);

    self->priv->tracks_list = NULL;
    self->priv->languages_list = NULL;

    self->priv->session_number = 1;
    self->priv->first_track = 1;

    /* Create lead-in */
    mirage_session_add_track_by_number(self, MIRAGE_TRACK_LEADIN, NULL, NULL);

    /* Create lead-out */
    mirage_session_add_track_by_number(self, MIRAGE_TRACK_LEADOUT, NULL, NULL);
}

static void mirage_session_dispose (GObject *gobject)
{
    MIRAGE_Session *self = MIRAGE_SESSION(gobject);
    GList *entry;

    /* Unref tracks */
    G_LIST_FOR_EACH(entry, self->priv->tracks_list) {
        if (entry->data) {
            GObject *track = entry->data;
            /* Disconnect signal handler and unref */
            g_signal_handlers_disconnect_by_func(MIRAGE_OBJECT(track), mirage_session_track_modified_handler, self);
            g_object_unref(track);

            entry->data = NULL;
        }
    }

    /* Unref languages */
    G_LIST_FOR_EACH(entry, self->priv->languages_list) {
        if (entry->data) {
            GObject *language = entry->data;
            g_object_unref(language);

            entry->data = NULL;
        }
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_session_parent_class)->dispose(gobject);
}

static void mirage_session_finalize (GObject *gobject)
{
    MIRAGE_Session *self = MIRAGE_SESSION(gobject);

    g_list_free(self->priv->tracks_list);
    g_list_free(self->priv->languages_list);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_session_parent_class)->finalize(gobject);
}

static void mirage_session_class_init (MIRAGE_SessionClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = mirage_session_dispose;
    gobject_class->finalize = mirage_session_finalize;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_SessionPrivate));
}
