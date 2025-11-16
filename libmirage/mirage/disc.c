/*
 *  libMirage: disc
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

/**
 * SECTION: mirage-disc
 * @title: MirageDisc
 * @short_description: Object representing an optical disc.
 * @see_also: #MirageSession, #MirageTrack, #MirageObject, #MirageParser, #MirageWriter, #MirageContext
 * @include: mirage-disc.h
 *
 * #MirageDisc object is a top-level object in the disc layout
 * representation, representing the actual disc.
 *
 * It provides functions for manipulating the disc layout; adding and
 * removing sessions and tracks, manipulating medium type, and convenience
 * functions for accessing sectors on the disc.
 *
 * Typically, a #MirageDisc is obtained as a result of loading an image
 * using #MirageContext and its mirage_context_load_image() function.
 */

#include "mirage/config.h"
#include "mirage/mirage.h"

#include <glib/gi18n-lib.h>

#define __debug__ "Disc"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
struct _MirageDiscPrivate
{
    gchar **filenames;

    MirageMediumType medium_type;

    /* Layout settings */
    gint start_sector;  /* Start sector */
    gint first_session; /* Number of the first session on disc */
    gint first_track;   /* Number of the first track on disc */
    gint length;

    GHashTable *disc_structures;

    gint tracks_number;

    /* Session list */
    GList *sessions_list;

    /* DPM */
    gint dpm_start;
    gint dpm_resolution;
    gint dpm_num_entries;
    guint32 *dpm_data;
};


/**********************************************************************\
 *                          Private functions                         *
\**********************************************************************/
static void mirage_disc_remove_session (MirageDisc *self, MirageSession *session);

static void mirage_disc_commit_topdown_change (MirageDisc *self)
{
    /* Rearrange sessions: set numbers, set first tracks, set start sectors */
    gint cur_session_address = self->priv->start_sector;
    gint cur_session_number = self->priv->first_session;
    gint cur_session_ftrack = self->priv->first_track;

    for (GList *entry = self->priv->sessions_list; entry; entry = entry->next) {
        MirageSession *session = entry->data;

        /* Set session's number */
        mirage_session_layout_set_session_number(session, cur_session_number);
        cur_session_number++;

        /* Set session's first track */
        mirage_session_layout_set_first_track(session, cur_session_ftrack);
        cur_session_ftrack += mirage_session_get_number_of_tracks(session);

        /* Set session's start address */
        mirage_session_layout_set_start_sector(session, cur_session_address);
        cur_session_address += mirage_session_layout_get_length(session);
    }
}

static void mirage_disc_commit_bottomup_change (MirageDisc *self)
{
    /* Calculate disc length and number of tracks */
    self->priv->length = 0; /* Reset; it'll be recalculated */
    self->priv->tracks_number = 0; /* Reset; it'll be recalculated */

    for (GList *entry = self->priv->sessions_list; entry; entry = entry->next) {
        MirageSession *session = entry->data;

        /* Disc length */
        self->priv->length += mirage_session_layout_get_length(session);

        /* Number of all tracks */
        self->priv->tracks_number += mirage_session_get_number_of_tracks(session);
    }

    /* Signal disc change */
    g_signal_emit_by_name(self, "layout-changed", NULL);
    /* Disc is where we complete the arc by committing top-down change */
    mirage_disc_commit_topdown_change(self);
}

static void mirage_disc_session_layout_changed_handler (MirageDisc *self, MirageSession *session)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_DISC, "%s: start\n", __debug__);

    /* If session has been emptied, remove it (it'll do bottom-up change automatically);
       otherwise, signal bottom-up change */
    if (!mirage_session_get_number_of_tracks(session)) {
        mirage_disc_remove_session(self, session);
    } else {
        mirage_disc_commit_bottomup_change(self);
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_DISC, "%s: end\n", __debug__);
}

static void mirage_disc_remove_session (MirageDisc *self, MirageSession *session)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_DISC, "%s: start\n", __debug__);

    /* Disconnect signal handler (find it by handler function and user data) */
    g_signal_handlers_disconnect_by_func(session, mirage_disc_session_layout_changed_handler, self);

    /* Remove session from list and unref it */
    self->priv->sessions_list = g_list_remove(self->priv->sessions_list, session);
    g_object_unref(session);

    /* Bottom-up change */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_DISC, "%s: committing bottom-up change\n", __debug__);
    mirage_disc_commit_bottomup_change(self);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_DISC, "%s: end\n", __debug__);
}

static gint sort_sessions_by_number (MirageSession *session1, MirageSession *session2)
{
    gint number1 = mirage_session_layout_get_session_number(session1);
    gint number2 = mirage_session_layout_get_session_number(session2);

    if (number1 < number2) {
        return -1;
    } else if (number1 > number2) {
        return 1;
    } else {
        return 0;
    }
}


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
/**
 * mirage_disc_set_medium_type:
 * @self: a #MirageDisc
 * @medium_type: (in): medium type
 *
 * Sets medium type. @medium_type must be one of #MirageMediumType.
 *
 * <note>
 * Intended for internal use only.
 * </note>
 */
void mirage_disc_set_medium_type (MirageDisc *self, MirageMediumType medium_type)
{
    /* Set medium type */
    self->priv->medium_type = medium_type;
}

/**
 * mirage_disc_get_medium_type:
 * @self: a #MirageDisc
 *
 * Retrieves medium type.
 *
 * Returns: medium type
 */
MirageMediumType mirage_disc_get_medium_type (MirageDisc *self)
{
    /* Return medium type */
    return self->priv->medium_type;
}


/**
 * mirage_disc_set_filenames:
 * @self: a #MirageDisc
 * @filenames: (in) (array zero-terminated=1): %NULL-terminated array of filenames
 *
 * Sets image filename(s).
 *
 * <note>
 * Intended for internal use only, in image parser implementations.
 * </note>
 */
void mirage_disc_set_filenames (MirageDisc *self, gchar **filenames)
{
    /* Free old filenames */
    g_strfreev(self->priv->filenames);
    /* Set filenames */
    self->priv->filenames = g_strdupv(filenames);
}

/**
 * mirage_disc_set_filename:
 * @self: a #MirageDisc
 * @filename: (in): filename
 *
 * Sets image filename. The functionality is similar to mirage_disc_set_filenames(),
 * except that only one filename is set. It is intended to be used in parsers which
 * support only single-file images.
 *
 * <note>
 * Intended for internal use only, in image parser implementations.
 * </note>
 */
void mirage_disc_set_filename (MirageDisc *self, const gchar *filename)
{
    /* Free old filenames */
    g_strfreev(self->priv->filenames);
    /* Set filenames */
    self->priv->filenames = g_new0(gchar *, 2);
    self->priv->filenames[0] = g_strdup(filename);
}

/**
 * mirage_disc_get_filenames:
 * @self: a #MirageDisc
 *
 * Retrieves image filename(s).
 *
 * Returns: (transfer none) (array zero-terminated=1): pointer to %NULL-terminated
 * array of filenames. The array belongs to the object and should not be modified.
 */
gchar **mirage_disc_get_filenames (MirageDisc *self)
{
    /* Return filenames */
    return self->priv->filenames;
}


/**
 * mirage_disc_layout_set_first_session:
 * @self: a #MirageDisc
 * @first_session: (in): first session number
 *
 * Sets first session number to @first_session. This is a number that is
 * assigned to the first session in the disc layout.
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * <note>
 * Causes top-down change.
 * </note>
 */
void mirage_disc_layout_set_first_session (MirageDisc *self, gint first_session)
{
    /* Set first session */
    self->priv->first_session = first_session;
    /* Top-down change */
    mirage_disc_commit_topdown_change(self);
}

/**
 * mirage_disc_layout_get_first_session:
 * @self: a #MirageDisc
 *
 * Retrieves session number of the first session in the disc layout.
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: first session number
 */
gint mirage_disc_layout_get_first_session (MirageDisc *self)
{
    /* Return first session */
    return self->priv->first_session;
}

/**
 * mirage_disc_layout_set_first_track:
 * @self: a #MirageDisc
 * @first_track: (in): first track number
 *
 * Sets first track number to @first_track. This is a number that is
 * assigned to the first track in the disc layout.
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * <note>
 * Causes top-down change.
 * </note>
 */
void mirage_disc_layout_set_first_track (MirageDisc *self, gint first_track)
{
    /* Set first track */
    self->priv->first_track = first_track;
    /* Top-down change */
    mirage_disc_commit_topdown_change(self);
}

/**
 * mirage_disc_layout_get_first_track:
 * @self: a #MirageDisc
 *
 * Retrieves track number of the first track in the disc layout.
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: first track number
 */
gint mirage_disc_layout_get_first_track (MirageDisc *self)
{
    /* Return first track */
    return self->priv->first_track;
}

/**
 * mirage_disc_layout_set_start_sector:
 * @self: a #MirageDisc
 * @start_sector: (in): start sector
 *
 * Sets start sector of the disc layout to @start_sector. This is a sector at which
 * the first session (and consequently first track) in the disc layout will start.
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * <note>
 * Causes top-down change.
 * </note>
 */
void mirage_disc_layout_set_start_sector (MirageDisc *self, gint start_sector)
{
    /* Set start sector */
    self->priv->start_sector = start_sector;
    /* Top-down change */
    mirage_disc_commit_topdown_change(self);
}

/**
 * mirage_disc_layout_get_start_sector:
 * @self: a #MirageDisc
 *
 * Retrieves start sector of the disc layout.
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: start sector
 */
gint mirage_disc_layout_get_start_sector (MirageDisc *self)
{
    /* Return start sector */
    return self->priv->start_sector;
}

/**
 * mirage_disc_layout_get_length:
 * @self: a #MirageDisc
 *
 * Retrieves length of the disc layout. The returned length is given in sectors.
 *
 * Returns: disc layout length
 */
gint mirage_disc_layout_get_length (MirageDisc *self)
{
    /* Return length */
    return self->priv->length;
}


/**
 * mirage_disc_layout_contains_address:
 * @self: a #MirageDisc
 * @address: address to be checked
 *
 * Checks whether the disc contains the given address or not.
 *
 * Returns: %TRUE if @address falls inside disc, %FALSE if it does not
 */
gboolean mirage_disc_layout_contains_address (MirageDisc *self, gint address)
{
    return address >= self->priv->start_sector && address < self->priv->start_sector + self->priv->length;
}


/**
 * mirage_disc_get_number_of_sessions:
 * @self: a #MirageDisc
 *
 * Retrieves number of sessions in the disc layout.
 *
 * Returns: number of sessions
 */
gboolean mirage_disc_get_number_of_sessions (MirageDisc *self)
{
    /* Return number of sessions */
    return g_list_length(self->priv->sessions_list); /* Length of list */
}

/**
 * mirage_disc_add_session_by_index:
 * @self: a #MirageDisc
 * @index: (in): index at which session should be added
 * @session: (in) (transfer full): a #MirageSession to be added
 *
 * Adds session to disc layout.
 *
 * @index is the index at which session is added. Negative index denotes
 * index going backwards (i.e. -1 adds session at the end, -2 adds session
 * second-to-last, etc.). If index, either negative or positive, is too big,
 * session is added at the beginning or at the end of the layout, respectively.
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 */
void mirage_disc_add_session_by_index (MirageDisc *self, gint index, MirageSession *session)
{
    gint num_sessions;

    /* First session, last session... allow negative indexes to go from behind */
    num_sessions = mirage_disc_get_number_of_sessions(self);
    if (index < -num_sessions) {
        /* If negative index is too big, put it at the beginning */
        index = 0;
    }
    if (index > num_sessions) {
        /* If positive index is too big, put it at the end */
        index = num_sessions;
    }
    if (index < 0) {
        index += num_sessions + 1;
    }

    /* We don't set session number here, because layout recalculation will do it for us */

    /* Increment reference counter */
    g_object_ref(session);
    /* Set parent */
    mirage_object_set_parent(MIRAGE_OBJECT(session), self);

    /* Insert session into sessions list */
    self->priv->sessions_list = g_list_insert(self->priv->sessions_list, session, index);

    /* Connect session modified signal */
    g_signal_connect_swapped(session, "layout-changed", (GCallback)mirage_disc_session_layout_changed_handler, self);

    /* Bottom-up change */
    mirage_disc_commit_bottomup_change(self);
}

/**
 * mirage_disc_add_session_by_number:
 * @self: a #MirageDisc
 * @number: (in): session number for the added session
 * @session: (in) (transfer full): a #MirageSession to be added
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Adds session to disc layout.
 *
 * @number is session number that should be assigned to added session. It determines
 * session's position in the layout. If session with that number already exists in
 * the layout, the function fails.
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_disc_add_session_by_number (MirageDisc *self, gint number, MirageSession *session, GError **error)
{
    MirageSession *tmp_session;

    /* Check if session with that number already exists */
    tmp_session = mirage_disc_get_session_by_number(self, number, NULL);
    if (tmp_session) {
        g_object_unref(tmp_session);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, Q_("Session with number %d already exists!"), number);
        return FALSE;
    }

    /* Increment reference counter */
    g_object_ref(session);
    /* Set session number */
    mirage_session_layout_set_session_number(session, number);
    /* Set parent */
    mirage_object_set_parent(MIRAGE_OBJECT(session), self);

    /* Insert session into sessions list */
    self->priv->sessions_list = g_list_insert_sorted(self->priv->sessions_list, session, (GCompareFunc)sort_sessions_by_number);

    /* Connect session modified signal */
    g_signal_connect_swapped(session, "layout-changed", (GCallback)mirage_disc_session_layout_changed_handler, self);

    /* Bottom-up change */
    mirage_disc_commit_bottomup_change(self);

    return TRUE;
}

/**
 * mirage_disc_remove_session_by_index:
 * @self: a #MirageDisc
 * @index: (in): index of session to be removed
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Removes session from disc layout.
 *
 * @index is the index of the session to be removed. This function calls
 * mirage_disc_get_session_by_index() so @index behavior is determined by that
 * function.
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_disc_remove_session_by_index (MirageDisc *self, gint index, GError **error)
{
    /* Find session by index */
    MirageSession *session = mirage_disc_get_session_by_index(self, index, error);
    if (!session) {
        return FALSE;
    }

    /* Remove session from list */
    mirage_disc_remove_session(self, session);
    g_object_unref(session); /* This one's from get */

    return TRUE;
}

/**
 * mirage_disc_remove_session_by_number:
 * @self: a #MirageDisc
 * @number: (in): session number of session to be removed
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Removes session from disc layout.
 *
 * @number is session number of the session to be removed.
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_disc_remove_session_by_number (MirageDisc *self, gint number, GError **error)
{
    /* Find session by number */
    MirageSession *session = mirage_disc_get_session_by_number(self, number, error);
    if (!session) {
        return FALSE;
    }

    /* Remove track from list */
    mirage_disc_remove_session(self, session);
    g_object_unref(session); /* This one's from get */

    return TRUE;
}

/**
 * mirage_disc_remove_session_by_object:
 * @self: a #MirageDisc
 * @session: (in): session object to be removed
 *
 * Removes session from disc layout.
 *
 * @session is a #MirageSession object to be removed.
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 */
void mirage_disc_remove_session_by_object (MirageDisc *self, MirageSession *session)
{
    mirage_disc_remove_session(self, session);
}


/**
 * mirage_disc_get_session_by_index:
 * @self: a #MirageDisc
 * @index: (in): index of session to be retrieved
 * @error: (out) (allow-none):location to store error, or %NULL
 *
 * Retrieves session by index. If @index is negative, sessions from the end of
 * layout are retrieved (e.g. -1 is for last session, -2 for second-to-last
 * session, etc.). If @index is out of range, regardless of the sign, the
 * function fails.
 *
 * Returns: (transfer full): a #MirageSession on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
MirageSession *mirage_disc_get_session_by_index (MirageDisc *self, gint index, GError **error)
{
    MirageSession *session;
    gint num_sessions;

    /* First session, last session... allow negative indexes to go from behind */
    num_sessions = mirage_disc_get_number_of_sessions(self);
    if (index < -num_sessions || index >= num_sessions) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, Q_("Session index %d out of range!"), index);
        return NULL;
    } else if (index < 0) {
        index += num_sessions;
    }

    /* Get index-th item from list... */
    session = g_list_nth_data(self->priv->sessions_list, index);

    if (!session) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, Q_("Session with index %d not found!"), index);
        return NULL;
    }

    return g_object_ref(session);
}

/**
 * mirage_disc_get_session_by_number:
 * @self: a #MirageDisc
 * @number: (in): number of session to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves session by session number.
 *
 * Returns: (transfer full): a #MirageSession on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
MirageSession *mirage_disc_get_session_by_number (MirageDisc *self, gint session_number, GError **error)
{
    MirageSession *session = NULL;

    /* Go over all sessions */
    for (GList *entry = self->priv->sessions_list; entry; entry = entry->next) {
        session = entry->data;

        /* Break the loop if number matches */
        if (session_number == mirage_session_layout_get_session_number(session)) {
            break;
        } else {
            session = NULL;
        }
    }

    /* If we didn't find anything... */
    if (!session) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, Q_("Session with number %d not found!"), session_number);
        return FALSE;
    }

    return g_object_ref(session);
}

/**
 * mirage_disc_get_session_by_address:
 * @self: a #MirageDisc
 * @address: (in): address belonging to session to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves session by address. @address must be valid (disc-relative) sector
 * address that is part of the session to be retrieved (i.e. lying between session's
 * start and end sector).
 *
 * Returns: (transfer full): a #MirageSession on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
MirageSession *mirage_disc_get_session_by_address (MirageDisc *self, gint address, GError **error)
{
    MirageSession *session = NULL;

    if (!mirage_disc_layout_contains_address(self, address)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, Q_("Session address %d (0x%X) out of range!"), address, address);
        return FALSE;
    }

    /* Go over all sessions */
    for (GList *entry = self->priv->sessions_list; entry; entry = entry->next) {
        session = entry->data;

        /* Break the loop if address lies within session boundaries */
        if (mirage_session_layout_contains_address(session, address)) {
            break;
        } else {
            session = NULL;
        }
    }

    /* If we didn't find anything... */
    if (!session) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, Q_("Session containing address %d not found!"), address);
        return FALSE;
    }

    return g_object_ref(session);
}

/**
 * mirage_disc_get_session_by_track:
 * @self: a #MirageDisc
 * @track: (in): number of track belonging to session to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves session by track number. @track must be valid track number of track
 * that is part of the session.
 *
 * Returns: (transfer full): a #MirageSession on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
MirageSession *mirage_disc_get_session_by_track (MirageDisc *self, gint track_number, GError **error)
{
    MirageSession *session = NULL;

    /* Go over all sessions */
    for (GList *entry = self->priv->sessions_list; entry; entry = entry->next) {
        gint first_track;
        gint num_tracks;

        session = entry->data;

        first_track = mirage_session_layout_get_first_track(session);
        num_tracks = mirage_session_get_number_of_tracks(session);

        /* Break the loop if track with that number is part of the session */
        if (track_number >= first_track && track_number < first_track + num_tracks) {
            break;
        } else {
            session = NULL;
        }
    }

    /* If we didn't find anything... */
    if (!session) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, Q_("Session with track %d not found!"), track_number);
        return FALSE;
    }

    return g_object_ref(session);
}

/**
 * mirage_disc_enumerate_sessions:
 * @self: a #MirageDisc
 * @func: (in) (scope call) (closure user_data): callback function
 * @user_data: (in) (allow-none): data to be passed to callback function
 *
 * Iterates over sessions list, calling @func for each session in the layout.
 *
 * If @func returns %FALSE, the function immediately returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_disc_enumerate_sessions (MirageDisc *self, MirageEnumSessionCallback func, gpointer user_data)
{
    for (GList *entry = self->priv->sessions_list; entry; entry = entry->next) {
        gboolean succeeded = (*func)(entry->data, user_data);
        if (!succeeded) {
            return FALSE;
        }
    }

    return TRUE;
}

/**
 * mirage_disc_get_session_before:
 * @self: a #MirageDisc
 * @session: (in): a session
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves session that comes before @session.
 *
 * Returns: (transfer full): a #MirageSession on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
MirageSession *mirage_disc_get_session_before (MirageDisc *self, MirageSession *session, GError **error)
{
    gint index;

    /* Get index of given session in the list */
    index = g_list_index(self->priv->sessions_list, session);
    if (index == -1) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, Q_("Session %p is not in disc layout!"), session);
        return NULL;
    }

    /* Now check if we didn't pass the first session (index = 0) and return previous one */
    if (index > 0) {
        return mirage_disc_get_session_by_index(self, index - 1, error);
    }

    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, Q_("Session before session %p not found!"), session);
    return NULL;
}

/**
 * mirage_disc_get_session_after:
 * @self: a #MirageDisc
 * @session: (in): a session
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves session that comes after @session.
 *
 * Returns: (transfer full): a #MirageSession on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
MirageSession *mirage_disc_get_session_after (MirageDisc *self, MirageSession *session, GError **error)
{
    gint num_sessions, index;

    /* Get index of given session in the list */
    index = g_list_index(self->priv->sessions_list, session);
    if (index == -1) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, Q_("Session %p is not in disc layout!"), session);
        return NULL;
    }

    /* Now check if we didn't pass the last session (index = num_sessions - 1) and return previous one */
    num_sessions = mirage_disc_get_number_of_sessions(self);
    if (index < num_sessions - 1) {
        return mirage_disc_get_session_by_index(self, index + 1, error);
    }

    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, Q_("Session after session %p not found!"), session);
    return NULL;
}


/**
 * mirage_disc_get_number_of_tracks:
 * @self: a #MirageDisc
 *
 * Retrieves number of tracks in the disc layout.
 *
 * Returns: number of tracks
 */
gint mirage_disc_get_number_of_tracks (MirageDisc *self)
{
    /* Return number of tracks */
    return self->priv->tracks_number;
}

/**
 * mirage_disc_add_track_by_index:
 * @self: a #MirageDisc
 * @index: (in): index at which track should be added
 * @track: (in) (transfer full): a #MirageTrack to be added
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Adds track to disc layout.
 *
 * @index is the index at which track is added. The function attempts to find
 * appropriate session by iterating over sessions list and verifying index ranges,
 * then adds the track using mirage_session_add_track_by_index(). Negative
 * @index denotes index going backwards (i.e. -1 adds track at the end of last
 * session, etc.). If @index, either negative or positive, is too big, track is
 * respectively added  at the beginning of the first or at the end of the last
 * session in the layout.
 *
 * If disc layout is empty (i.e. contains no sessions), then session is created.
 *
 * The rest of behavior is same as of mirage_session_add_track_by_index().
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_disc_add_track_by_index (MirageDisc *self, gint index, MirageTrack *track, GError **error)
{
    gint num_tracks;
    gint count;

    /* If disc layout is empty (if there are no sessions), we should create
       a session... and then track will be added to this one */
    if (!mirage_disc_get_number_of_sessions(self)) {
        MirageSession *session = g_object_new(MIRAGE_TYPE_SESSION, NULL);
        mirage_disc_add_session_by_index(self, 0, session);
        g_object_unref(session);
    }

    /* First track, last track... allow negative indexes to go from behind */
    num_tracks = mirage_disc_get_number_of_tracks(self);
    if (index < -num_tracks) {
        /* If negative index is too big, return the first track */
        index = 0;
    }
    if (index > num_tracks) {
        /* If positive index is too big, return the last track */
        index = num_tracks;
    }
    if (index < 0) {
        index += num_tracks + 1;
    }

    /* Iterate over all the sessions and determine the one where track with
       desired index should be in */
    count = 0;
    for (GList *entry = self->priv->sessions_list; entry; entry = entry->next) {
        MirageSession *session = entry->data;

        num_tracks = mirage_session_get_number_of_tracks(session);

        if (index >= count && index <= count + num_tracks) {
            /* We got the session */
            mirage_session_add_track_by_index(session, index - count, track);
            return TRUE;
        }

        count += num_tracks;
    }

    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, Q_("Session not found!"));
    return FALSE;
}

/**
 * mirage_disc_add_track_by_number:
 * @self: a #MirageDisc
 * @number: (in): track number for the added track
 * @track: (in) (transfer full): a #MirageTrack to be added
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Adds track to disc layout.
 *
 * @number is track number that should be assigned to added track. It determines
 * track's position in the layout. The function attempts to find appropriate session
 * using mirage_disc_get_session_by_track(), then adds the track using
 * mirage_session_add_track_by_number().
 *
 * If disc layout is empty (i.e. contains no sessions), then session is created.
 * If @number is greater than last track's number, the track is added at the end
 * of last session.
 *
 * The rest of behavior is same as of mirage_session_add_track_by_number().
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_disc_add_track_by_number (MirageDisc *self, gint number, MirageTrack *track, GError **error)
{
    MirageSession *session;
    MirageTrack *last_track;
    gboolean succeeded;
    gint last_number;

    /* Get number of last track */
    last_track = mirage_disc_get_track_by_index(self, -1, NULL);
    if (last_track) {
        last_number = mirage_track_layout_get_track_number(last_track);
        g_object_unref(last_track);
    } else {
        last_number = 0;
    }

    if (!mirage_disc_get_number_of_sessions(self)) {
        /* If disc layout is empty (if there are no sessions), we should create
           a session... and then track will be added to this one */
        session = g_object_new(MIRAGE_TYPE_SESSION, NULL);
        mirage_disc_add_session_by_index(self, 0, session);
    } else if (number > last_number) {
        /* If track number surpasses the number of last track on disc, then it
           means we need to add the track into last session */
        session = mirage_disc_get_session_by_index(self, -1, error);
    } else {
        /* Try to get the session by track number */
        session = mirage_disc_get_session_by_track(self, number, error);
    }
    if (!session) {
        return FALSE;
    }

    /* If session was found, try to add track */
    succeeded = mirage_session_add_track_by_number(session, number, track, error);

    g_object_unref(session);

    return succeeded;
}

/**
 * mirage_disc_remove_track_by_index:
 * @self: a #MirageDisc
 * @index: (in): index of track to be removed
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Removes track from disc layout.
 *
 * @index is the index of the track to be removed. This function calls
 * mirage_disc_get_track_by_index() so @index behavior is determined by that
 * function.
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_disc_remove_track_by_index (MirageDisc *self, gint index, GError **error)
{
    MirageSession *session;
    MirageTrack *track;

    /* Get track directly */
    track = mirage_disc_get_track_by_index(self, index, error);
    if (!track) {
        return FALSE;
    }
    /* Get track's parent */
    session = mirage_object_get_parent(MIRAGE_OBJECT(track));
    if (!session) {
        g_object_unref(track);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, Q_("Track has no parent!"));
        return FALSE;
    }
    /* Remove track from parent */
    mirage_session_remove_track_by_object(session, track);

    g_object_unref(track);
    g_object_unref(session);

    return TRUE;
}

/**
 * mirage_disc_remove_track_by_number:
 * @self: a #MirageDisc
 * @number: (in): track number of track to be removed
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Removes track from disc layout.
 *
 * @number is track number of the track to be removed. This function calls
 * mirage_disc_get_track_by_number() so @number behavior is determined by that
 * function.
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_disc_remove_track_by_number (MirageDisc *self, gint number, GError **error)
{
    MirageSession *session;
    MirageTrack *track;

    /* Protect against removing lead-in and lead-out */
    if (number == MIRAGE_TRACK_LEADIN || number == MIRAGE_TRACK_LEADOUT) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, Q_("Invalid track number %d!"), number);
        return FALSE;
    }

    /* Get track directly */
    track = mirage_disc_get_track_by_number(self, number, error);
    if (!track) {
        return FALSE;
    }
    /* Get track's parent */
    session = mirage_object_get_parent(MIRAGE_OBJECT(track));
    if (!session) {
        g_object_unref(track);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, Q_("Track has no parent!"));
        return FALSE;
    }
    /* Remove track from parent */
    mirage_session_remove_track_by_object(session, track);

    g_object_unref(track);
    g_object_unref(session);

    return TRUE;
}

/**
 * mirage_disc_get_track_by_index:
 * @self: a #MirageDisc
 * @index: (in): index of track to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves track by index. The function attempts to find appropriate session
 * by iterating over sessions list and verifying index ranges, then retrieves
 * the track using mirage_session_get_track_by_index(). If @index is negative,
 * tracks from the end of layout are retrieved (e.g. -1 is for last track, -2
 * for second-to-last track, etc.). If @index is out of range, regardless of
 * the sign, the function fails.
 *
 * The rest of behavior is same as of mirage_session_get_track_by_index().
 *
 * Returns: (transfer full): a #MirageTrack on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
MirageTrack *mirage_disc_get_track_by_index (MirageDisc *self, gint index, GError **error)
{
    gint num_tracks;
    gint count;

    /* First track, last track... allow negative indexes to go from behind */
    num_tracks = mirage_disc_get_number_of_tracks(self);
    if (index < -num_tracks || index >= num_tracks) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, Q_("Track index %d out of range!"), index);
        return NULL;
    } else if (index < 0) {
        index += num_tracks;
    }

    /* Loop over the sessions */
    count = 0;
    for (GList *entry = self->priv->sessions_list; entry; entry = entry->next) {
        MirageSession *session = entry->data;

        num_tracks = mirage_session_get_number_of_tracks(session);

        if (index >= count && index < count + num_tracks) {
            /* We got the session */
            return mirage_session_get_track_by_index(session, index - count, error);
        }

        count += num_tracks;
    }

    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, Q_("Track with index %d not found!"), index);
    return NULL;
}

/**
 * mirage_disc_get_track_by_number:
 * @self: a #MirageDisc
 * @number: (in): track number of track to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves track by track number. The function attempts to find appropriate session
 * using mirage_disc_get_session_by_track(), then retrieves the track using
 * mirage_session_get_track_by_number().
 *
 * The rest of behavior is same as of mirage_session_get_track_by_number().
 *
 * Returns: (transfer full): a #MirageTrack on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
MirageTrack *mirage_disc_get_track_by_number (MirageDisc *self, gint number, GError **error)
{
    MirageSession *session;
    MirageTrack *track;

    /* We get session by track */
    session = mirage_disc_get_session_by_track(self, number, error);
    if (!session) {
        return NULL;
    }

    /* And now we get the track */
    track = mirage_session_get_track_by_number(session, number, error);
    g_object_unref(session);

    return track;
}

/**
 * mirage_disc_get_track_by_address:
 * @self: a #MirageDisc
 * @address: (in): address belonging to track to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves track by address. @address must be valid (disc-relative) sector
 * address that is part of the track to be retrieved (i.e. lying between track's
 * start and end sector).
 *
 * The function attempts to find appropriate session using
 * mirage_disc_get_session_by_address(), then retrieves the track using
 * mirage_session_get_track_by_address().
 *
 * The rest of behavior is same as of mirage_session_get_track_by_address().
 *
 * Returns: (transfer full): a #MirageTrack on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
MirageTrack *mirage_disc_get_track_by_address (MirageDisc *self, gint address, GError **error)
{
    MirageSession *session;
    MirageTrack *track;

    /* We get session by sector */
    session = mirage_disc_get_session_by_address(self, address, error);
    if (!session) {
        return FALSE;
    }

    /* And now we get the track */
    track = mirage_session_get_track_by_address(session, address, error);
    g_object_unref(session);

    return track;
}


/**
 * mirage_disc_set_disc_structure:
 * @self: a #MirageDisc
 * @layer: (in): disc layer
 * @type: (in): disc structure type
 * @data: (in) (array length=len): disc structure data to be set
 * @len: (in): length of disc structure data
 *
 * Sets disc structure of type @type to layer @layer to disc. @data is buffer
 * containing disc structure data and @len is data length.
 *
 * <note>
 * Disc structures are valid only for DVD and BD discs. This function
 * silently fails on invalid disc types.
 * </note>
 */
void mirage_disc_set_disc_structure (MirageDisc *self, gint layer, gint type, const guint8 *data, gint len)
{
    GByteArray *array;
    gint key = ((layer & 0x0000FFFF) << 16) | (type & 0x0000FFFF);

    if (self->priv->medium_type != MIRAGE_MEDIUM_DVD && self->priv->medium_type != MIRAGE_MEDIUM_BD) {
        return;
    }

    /* Store the data in a GByteArray (FIXME someday, we'll migrate
       this to GBytes, which requires GLib 2.32) */
    array = g_byte_array_new();
    array = g_byte_array_append(array, data, len);

    g_hash_table_insert(self->priv->disc_structures, GINT_TO_POINTER(key), array);
}

/**
 * mirage_disc_get_disc_structure:
 * @self: a #MirageDisc
 * @layer: (in): disc layer
 * @type: (in): disc structure type
 * @data: (out) (transfer none) (allow-none) (array length=len): location to store buffer containing disc structure data, or %NULL
 * @len: (out) (allow-none): location to store data length, or %NULL
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves disc structure of type @type from layer @layer. The pointer to buffer
 * containing the disc structure is stored in @data; the buffer belongs to the
 * object and therefore should not be modified.
 *
 * <note>
 * Disc structures are valid only for DVD and BD discs; therefore, if disc type
 * is not set to %MIRAGE_MEDIUM_DVD or %MIRAGE_MEDIUM_BD prior to calling this
 * function, the function will fail.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_disc_get_disc_structure (MirageDisc *self, gint layer, gint type, const guint8 **data, gint *len, GError **error)
{
    gint key = ((layer & 0x0000FFFF) << 16) | (type & 0x0000FFFF);
    GByteArray *array;

    if (self->priv->medium_type != MIRAGE_MEDIUM_DVD && self->priv->medium_type != MIRAGE_MEDIUM_BD) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, Q_("Invalid medium type!"));
        return FALSE;
    }

    if (layer < 0 || layer > 1) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, Q_("Invalid layer %d!"), layer);
        return FALSE;
    }

    array = g_hash_table_lookup(self->priv->disc_structures, GINT_TO_POINTER(key));

    if (!array) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, Q_("Disc structure data not provided!"));
        return FALSE;
    }

    if (data) {
        /* Return data to user if she wants it */
        *data = array->data;
    }
    if (len) {
        *len = array->len;
    }

    return TRUE;
}


/**
 * mirage_disc_get_sector:
 * @self: a #MirageDisc
 * @address: (in): sector address
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves sector object representing sector at sector address @address.
 *
 * This function attempts to retrieve appropriate track using
 * mirage_disc_get_track_by_address(),
 * then retrieves sector object using mirage_track_get_sector().
 *
 * Returns: (transfer full): sector object on success, %NULL on failure
 */
MirageSector *mirage_disc_get_sector (MirageDisc *self, gint address, GError **error)
{
    MirageTrack *track;
    MirageSector *sector;

    /* Fetch the right track */
    track = mirage_disc_get_track_by_address(self, address, error);
    if (!track) {
        return FALSE;
    }

    /* Get the sector */
    sector = mirage_track_get_sector(track, address, TRUE, error);
    /* Unref track */
    g_object_unref(track);

    return sector;
}

/**
 * mirage_disc_put_sector:
 * @self: a #MirageDisc
 * @sector: (in): a #MirageSector representing sector to be written
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Writes the @sector to disc.
 *
 * This function attempts to retrieve appropriate track using
 * mirage_disc_get_track_by_address(),
 * then writes sector object using mirage_track_put_sector(); therefore,
 * same restrictions regarding sector address apply as when putting sector
 * directly to track.
 *
 * Returns: %TRUE on success, %FALSE on failure.
 */
gboolean mirage_disc_put_sector (MirageDisc *self, MirageSector *sector, GError **error)
{
    MirageTrack *track;
    gint address = mirage_sector_get_address(sector);
    gboolean succeeded = TRUE;

    /* Fetch the right track */
    track = mirage_disc_get_track_by_address(self, address, NULL);
    if (!track) {
        /* We also allow data to be appended to the last track; for this,
           however, the sector's address is allowed to be one more than
           the last valid address of the last layout... */
        track = mirage_disc_get_track_by_address(self, address - 1, error);
        if (!track) {
            return FALSE;
        }
    }

    /* Put the sector in the track */
    succeeded = mirage_track_put_sector(track, sector, error);

    g_object_unref(track);

    return succeeded;
}


/**
 * mirage_disc_set_dpm_data:
 * @self: a #MirageDisc
 * @start: (in): DPM start sector
 * @resolution: (in): DPM data resolution
 * @num_entries: (in): number of DPM entries
 * @data: (in) (array length=num_entries): buffer containing DPM data
 *
 * Sets the DPM data for disc. If @num_entries is not positive, DPM data is reset.
 * @start is the address at which DPM data begins, @resolution is resolution of
 * DPM data and @num_entries is the number of DPM entries in buffer pointed to by
 * @data.
 */
void mirage_disc_set_dpm_data (MirageDisc *self, gint start, gint resolution, gint num_entries, const guint32 *data)
{
    /* Free old DPM data */
    g_free(self->priv->dpm_data);
    self->priv->dpm_data = NULL;

    /* Set new DPM data */
    self->priv->dpm_start = start;
    self->priv->dpm_resolution = resolution;
    self->priv->dpm_num_entries = num_entries;
    /* Allocate and copy data only if number of entries is positive (otherwise
       the data is simply reset) */
    if (self->priv->dpm_num_entries > 0) {
        self->priv->dpm_data = g_new0(guint32, self->priv->dpm_num_entries);
        memcpy(self->priv->dpm_data, data, sizeof(guint32)*self->priv->dpm_num_entries);
    }
}

/**
 * mirage_disc_get_dpm_data:
 * @self: a #MirageDisc
 * @start: (out) (allow-none): location to store DPM start sector, or %NULL
 * @resolution: (out) (allow-none): location to store DPM data resolution, or %NULL
 * @num_entries: (out) (allow-none): location to store number of DPM entries, or %NULL
 * @data: (out) (allow-none) (array length=num_entries) (transfer none): location to store pointer to buffer containing DPM data, or %NULL
 *
 * Retrieves DPM data for disc. The pointer to buffer containing DPM data entries
 * is stored in @data; the buffer belongs to object and therefore should not be
 * modified.
 */
void mirage_disc_get_dpm_data (MirageDisc *self, gint *start, gint *resolution, gint *num_entries, const guint32 **data)
{
    if (start) {
        *start = self->priv->dpm_start;
    }
    if (resolution) {
        *resolution = self->priv->dpm_resolution;
    }
    if (num_entries) {
        *num_entries = self->priv->dpm_num_entries;
    }
    if (data) {
        *data = self->priv->dpm_data;
    }
}

/**
 * mirage_disc_get_dpm_data_for_sector:
 * @self: a #MirageDisc
 * @address: (in): address of sector to retrieve DPM data for
 * @angle: (out) (allow-none): location to store sector angle, or %NULL
 * @density: (out) (allow-none): location to store sector density, or %NULL
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves DPM data for sector at address @address. Two pieces of data can be
 * retrieved; first one is sector angle, expressed in rotations (i.e. 0.25 would
 * mean 1/4 of rotation or 90˚ and 1.0 means one full rotation or 360˚), and the
 * other one is sector density at given address, expressed in degrees per sector).
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_disc_get_dpm_data_for_sector (MirageDisc *self, gint address, gdouble *angle, gdouble *density, GError **error)
{
    gint rel_address;
    gint idx_bottom;

    gdouble tmp_angle, tmp_density;

    if (!self->priv->dpm_num_entries) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, Q_("DPM data not available!"));
        return FALSE;
    }

    /* We'll operate with address relative to DPM data start sector */
    rel_address = address - self->priv->dpm_start;

    /* Check if relative address is out of range (account for possibility of
       sectors lying behind last DPM entry) */
    if (rel_address < 0 || rel_address >= (self->priv->dpm_num_entries+1)*self->priv->dpm_resolution) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, Q_("Sector address %d out of range!"), address);
        return FALSE;
    }

    /* Calculate index of DPM data entry belonging to the requested address */
    idx_bottom = rel_address/self->priv->dpm_resolution;

    /* Three possibilities; in all three cases we calculate tmp_density as the
       difference between top and bottom angle, converted to rotations and
       divided by resolution. Because our DPM data entries don't contain entry
       for address 0, but start with 1*dpm_resolution instead, we'll have to
       readjust bottom index... (actual entry index is bottom index minus 1) */
    if (idx_bottom == 0) {
        /* If bottom index is 0, we have address between 0 and 1*dpm_resolution;
           this means bottom angle is 0 and top angle is first DPM entry (with
           index 0, which equals idx_bottom). */
        tmp_density = self->priv->dpm_data[idx_bottom];
    } else if (idx_bottom == self->priv->dpm_num_entries) {
        /* Special case; we allow addresses past last DPM entry's address, but
           only as long as they don't get past the address that would belong to
           next DPM entry. This is because resolution is not a factor of disc
           length and therefore some sectors might remain past last DPM entry.
           In this case, we use angles from previous interval. */
        tmp_density = (self->priv->dpm_data[idx_bottom-1] - self->priv->dpm_data[idx_bottom-2]);
    } else {
        /* Regular case; top angle minus bottom angle, where we need to decrease
           idx_bottom by one to account for index difference as described above */
        tmp_density = (self->priv->dpm_data[idx_bottom] - self->priv->dpm_data[idx_bottom-1]);
    }
    tmp_density /= 256.0; /* Convert hex degrees into rotations */
    tmp_density /= self->priv->dpm_resolution; /* Rotations per sector */

    if (angle) {
        tmp_angle = (rel_address - idx_bottom*self->priv->dpm_resolution)*tmp_density; /* Angle difference */
        /* Add base angle, but only if it's not 0 (which is the case when
           idx_bottom is 0) */
        if (idx_bottom > 0) {
            tmp_angle += self->priv->dpm_data[idx_bottom-1]/256.0; /* Add bottom angle */
        }

        *angle = tmp_angle;
    }

    if (density) {
        tmp_density *= 360; /* Degrees per sector */

        *density = tmp_density;
    }

    return TRUE;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE_WITH_PRIVATE(MirageDisc, mirage_disc, MIRAGE_TYPE_OBJECT)


static void mirage_disc_init (MirageDisc *self)
{
    self->priv = mirage_disc_get_instance_private(self);

    self->priv->sessions_list = NULL;

    self->priv->filenames = NULL;

    self->priv->dpm_data = NULL;

    /* Default layout values */
    self->priv->start_sector = 0;
    self->priv->first_session = 1;
    self->priv->first_track  = 1;

    /* Create disc structures hash table */
    self->priv->disc_structures = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)g_byte_array_unref);
}

static void mirage_disc_dispose (GObject *gobject)
{
    MirageDisc *self = MIRAGE_DISC(gobject);

    /* Unref sessions */
    for (GList *entry = self->priv->sessions_list; entry; entry = entry->next) {
        if (entry->data) {
            MirageSession *session = entry->data;
            /* Disconnect signal handler and unref */
            g_signal_handlers_disconnect_by_func(session, mirage_disc_session_layout_changed_handler, self);
            g_object_unref(session);

            entry->data = NULL;
        }
    }

    /* Unref disc structure table */
    if (self->priv->disc_structures) {
        g_hash_table_unref(self->priv->disc_structures);
        self->priv->disc_structures = NULL;
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_disc_parent_class)->dispose(gobject);
}

static void mirage_disc_finalize (GObject *gobject)
{
    MirageDisc *self = MIRAGE_DISC(gobject);

    g_list_free(self->priv->sessions_list);

    g_strfreev(self->priv->filenames);

    g_free(self->priv->dpm_data);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_disc_parent_class)->finalize(gobject);
}

static void mirage_disc_class_init (MirageDiscClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = mirage_disc_dispose;
    gobject_class->finalize = mirage_disc_finalize;

    /* Signals */
    /**
     * MirageDisc::layout-changed:
     * @disc: a #MirageDisc
     *
     * Emitted when a layout of #MirageDisc changed in a way that causes a bottom-up change.
     */
    g_signal_new("layout-changed", G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, NULL);
}
