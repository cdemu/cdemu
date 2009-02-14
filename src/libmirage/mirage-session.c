/*
 *  libMirage: Session object
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"

#define __debug__ "Session"


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_SESSION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_SESSION, MIRAGE_SessionPrivate))

typedef struct {    
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
} MIRAGE_SessionPrivate;


/******************************************************************************\
 *                              Private functions                             *
\******************************************************************************/
/* Forward declarations */
static gboolean __mirage_session_commit_topdown_change (MIRAGE_Session *self, GError **error);
static gboolean __mirage_session_commit_bottomup_change (MIRAGE_Session *self, GError **error);

static void __track_modified_handler (GObject *track, MIRAGE_Session *self);

static gboolean __remove_track_from_session (MIRAGE_Session *self, GObject *track, GError **error);
static gboolean __remove_language_from_session (MIRAGE_Session *self, GObject *language, GError **error);

static gint __sort_languages_by_code (GObject *language1, GObject *language2);
static gint __sort_tracks_by_number (GObject *track1, GObject *track2);


static gboolean __mirage_session_commit_topdown_change (MIRAGE_Session *self, GError **error) {
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
    GList *entry = NULL;
        
    /* Rearrange tracks: set numbers, set start sectors */
    gint cur_track_address = _priv->start_sector;
    gint cur_track_number  = _priv->first_track;
    
    G_LIST_FOR_EACH(entry, _priv->tracks_list) {
        GObject *track = entry->data;
        
        /* Set track's number */
        gint old_number = 0;
        mirage_track_layout_get_track_number(MIRAGE_TRACK(track), &old_number, NULL);
        if ((old_number != MIRAGE_TRACK_LEADIN) && (old_number != MIRAGE_TRACK_LEADOUT)) {
            mirage_track_layout_set_track_number(MIRAGE_TRACK(track), cur_track_number, NULL);
            cur_track_number++;
        }
        
        /* Set track's start address */
        mirage_track_layout_set_start_sector(MIRAGE_TRACK(track), cur_track_address, NULL);
        gint track_length = 0;
        mirage_track_layout_get_length(MIRAGE_TRACK(track), &track_length, NULL);
        cur_track_address += track_length;
    }
        
    return TRUE;
}

static gboolean __mirage_session_commit_bottomup_change (MIRAGE_Session *self, GError **error) {
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
    GList *entry = NULL;
        
    /* Calculate session length */
    _priv->length = 0; /* Reset; it'll be recalculated */

    G_LIST_FOR_EACH(entry, _priv->tracks_list) {
        GObject *track = entry->data;
        gint track_length = 0;
        mirage_track_layout_get_length(MIRAGE_TRACK(track), &track_length, NULL);
        _priv->length += track_length;
    }
    
    /* Signal session change */
    g_signal_emit_by_name(MIRAGE_OBJECT(self), "object-modified", NULL);
    /* If we don't have parent, we should complete the arc by committing top-down change */
    if (!mirage_object_get_parent(MIRAGE_OBJECT(self), NULL, NULL)) {
        __mirage_session_commit_topdown_change(self, NULL);
    }
    
    return TRUE;
}

static void __track_modified_handler (GObject *track, MIRAGE_Session *self) {    
    /* Bottom-up change */
    __mirage_session_commit_bottomup_change(self, NULL);
    return;
}

static gboolean __remove_track_from_session (MIRAGE_Session *self, GObject *track, GError **error) {
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
                
    /* Disconnect signal handler (find it by handler function and user data) */
    g_signal_handlers_disconnect_by_func(MIRAGE_OBJECT(track), __track_modified_handler, self);
    
    /* Remove track from list and unref it */
    _priv->tracks_list = g_list_remove(_priv->tracks_list, track);
    g_object_unref(track);
    
    /* Bottom-up change */
    __mirage_session_commit_bottomup_change(self, NULL);
    
    return TRUE;
}

static gboolean __remove_language_from_session (MIRAGE_Session *self, GObject *language, GError **error) {
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
            
    /* Remove it from list and unref it */
    _priv->languages_list = g_list_remove(_priv->languages_list, language);
    g_object_unref(language);
    
    return TRUE;
}

static gint __sort_languages_by_code (GObject *language1, GObject *language2) {   
    gint code1 = 0;
    gint code2 = 0;
    
    mirage_language_get_langcode(MIRAGE_LANGUAGE(language1), &code1, NULL);
    mirage_language_get_langcode(MIRAGE_LANGUAGE(language2), &code2, NULL);
    
    if (code1 < code2) {
        return -1;
    } else if (code1 > code2) {
        return 1;
    } else {
        return 0;
    }
}

static gint __sort_tracks_by_number (GObject *track1, GObject *track2) {          
    gint number1 = 0;
    gint number2 = 0;
        
    mirage_track_layout_get_track_number(MIRAGE_TRACK(track1), &number1, NULL);
    mirage_track_layout_get_track_number(MIRAGE_TRACK(track2), &number2, NULL);
    
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


/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
/**
 * mirage_session_set_session_type:
 * @self: a #MIRAGE_Session
 * @type: session type
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets session type. @type must be one of #MIRAGE_SessionTypes.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_set_session_type (MIRAGE_Session *self, gint type, GError **error) {
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
    /* Set session type */
    _priv->session_type =type;
    return TRUE;
}

/**
 * mirage_session_get_session_type:
 * @self: a #MIRAGE_Session
 * @type: location to store session type
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves session type.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_get_session_type (MIRAGE_Session *self, gint *type, GError **error) {
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(type);
    /* Return session number */
    *type = _priv->session_type;
    return TRUE;
}


/**
 * mirage_session_layout_set_session_number:
 * @self: a #MIRAGE_Session
 * @number: session number
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets sessions's session number.
 * </para>
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_layout_set_session_number (MIRAGE_Session *self, gint number, GError **error) {
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
    /* Set session number */
    _priv->session_number = number;
    return TRUE;
}

/**
 * mirage_session_layout_get_session_number:
 * @self: a #MIRAGE_Session
 * @number: location to store session number
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves sessions's session number.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_layout_get_session_number (MIRAGE_Session *self, gint *number, GError **error) {
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(number);
    /* Return session number */
    *number = _priv->session_number;
    return TRUE;
}

/**
 * mirage_session_layout_set_first_track:
 * @self: a #MIRAGE_Session
 * @first_track: first track number
 * @error: location to store error, or %NULL
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
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_layout_set_first_track (MIRAGE_Session *self, gint first_track, GError **error) {
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
    /* Set first track */
    _priv->first_track = first_track;
    /* Top-down change */
    __mirage_session_commit_topdown_change(self, NULL);
    return TRUE;
}

/**
 * mirage_session_layout_get_first_track:
 * @self: a #MIRAGE_Session
 * @first_track: location to store first track number
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track number of the first track in the session layout.
 * </para>
 * 
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_layout_get_first_track (MIRAGE_Session *self, gint *first_track, GError **error) {
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(first_track);
    /* Return first track */
    *first_track = _priv->first_track;
    return TRUE;
}

/**
 * mirage_session_layout_set_start_sector:
 * @self: a #MIRAGE_Session
 * @start_sector: start sector
 * @error: location to store error, or %NULL
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
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_layout_set_start_sector (MIRAGE_Session *self, gint start_sector, GError **error) {
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
    /* Set start sector */
    _priv->start_sector = start_sector;
    /* Top-down change */
    __mirage_session_commit_topdown_change(self, NULL);
    return TRUE;
}

/**
 * mirage_session_layout_get_start_sector:
 * @self: a #MIRAGE_Session
 * @start_sector: location to store start sector
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves start sector of the session layout.
 * </para>
 * 
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_layout_get_start_sector (MIRAGE_Session *self, gint *start_sector, GError **error) {
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(start_sector);
    /* Return start sector */
    *start_sector = _priv->start_sector;
    return TRUE;
}

/**
 * mirage_session_layout_get_length:
 * @self: a #MIRAGE_Session
 * @length: location to store session layout length
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves length of the session layout. This means the length of 
 * all tracks combined, including lead-in and lead-out tracks. The returned
 * length is given in sectors.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_layout_get_length (MIRAGE_Session *self, gint *length, GError **error) {
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(length);
    /* Return length */
    *length = _priv->length;
    return TRUE;
}


/**
 * mirage_session_set_leadout_length:
 * @self: a #MIRAGE_Session
 * @length: leadout length
 * @error: location to store error, or %NULL
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
 
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_set_leadout_length (MIRAGE_Session *self, gint length, GError **error) {
    GObject *leadout = NULL;
    gboolean succeeded = TRUE;
    
    /* Get leadout */
    if (!mirage_session_get_track_by_number(self, MIRAGE_TRACK_LEADOUT, &leadout, error)) {
        return FALSE;
    }
    
    /* Now, check if leadout already has a fragment... if it has (and it should
       have only one, unless I screwed up somewhere), then we'll simply readjust
       its length. If not, we need to create it... The simplest way is to try to 
       get the last fragment in track */
    GObject *null_fragment = NULL;
    
    if (!mirage_track_get_fragment_by_index(MIRAGE_TRACK(leadout), -1, &null_fragment, NULL)) {
        /* Create NULL fragment */
        null_fragment = libmirage_create_fragment(MIRAGE_TYPE_FINTERFACE_NULL, "NULL", error);
        
        if (!null_fragment) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to create NULL fragment\n", __debug__);
            g_object_unref(leadout);
            return FALSE;
        }
        mirage_track_add_fragment(MIRAGE_TRACK(leadout), 0, &null_fragment, NULL);
    }
    
    /* Set fragment's new length */
    succeeded = mirage_fragment_set_length(MIRAGE_FRAGMENT(null_fragment), length, error);
    
    /* Unref fragment */
    g_object_unref(null_fragment);
    
    /* Unref leadout */
    g_object_unref(leadout);
    
    return succeeded;
}

/**
 * mirage_session_get_leadout_length:
 * @self: a #MIRAGE_Session
 * @length: location to store leadout length
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves session's leadout length. The returned length is given in sectors.
 * </para>
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_get_leadout_length (MIRAGE_Session *self, gint *length, GError **error) {
    GObject *leadout = NULL;
    gboolean succeeded = TRUE;
    
    MIRAGE_CHECK_ARG(length);
    
    /* Get leadout */
    if (!mirage_session_get_track_by_number(self, MIRAGE_TRACK_LEADOUT, &leadout, error)) {
        return FALSE;
    }
    /* Get leadout's length */
    succeeded = mirage_track_layout_get_length(MIRAGE_TRACK(leadout), length, error);
    /* Unref leadout */
    g_object_unref(leadout);
    
    return succeeded;
}


/**
 * mirage_session_get_number_of_tracks:
 * @self: a #MIRAGE_Session
 * @number_of_tracks: location to store number of tracks
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves number of tracks in the session layout.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_get_number_of_tracks (MIRAGE_Session *self, gint *number_of_tracks, GError **error) {
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(number_of_tracks);
    /* Return number of tracks */
    *number_of_tracks = g_list_length(_priv->tracks_list) - 2; /* Length of list, without lead-in and lead-out */
    return TRUE;
}

/**
 * mirage_session_add_track_by_index:
 * @self: a #MIRAGE_Session
 * @index: index at which track should be added
 * @track: pointer to #MIRAGE_Track, %NULL pointer or %NULL
 * @error: location to store error, or %NULL
 
 * @index: index at which session should be added
 * @session: 
 * @error: location to store error, or %NULL
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
 * <para>
 * If @track contains pointer to existing #MIRAGE_Track object, the object
 * is added to session layout. Otherwise, a new #MIRAGE_Track object is created. 
 * If @track contains a %NULL pointer, a reference to newly created object is stored
 * in it; it should be released with g_object_unref() when no longer needed. If @track
 * is %NULL, no reference is returned.
 * </para>
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_add_track_by_index (MIRAGE_Session *self, gint index, GObject **track, GError **error) {
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
    GObject *new_track = NULL;
    gint num_tracks = 0;
    
    /* First track, last track... allow negative indexes to go from behind */
    mirage_session_get_number_of_tracks(self, &num_tracks, NULL);
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
    
    /* If there's track provided, use it; else create new track */
    if (track && *track) {
        new_track = *track;
        /* If track is not MIRAGE_Track... */
        if (!MIRAGE_IS_TRACK(new_track)) {
            mirage_error(MIRAGE_E_INVALIDOBJTYPE, error);
            return FALSE;
        }
        g_object_ref(new_track);
    } else {
        new_track = g_object_new(MIRAGE_TYPE_TRACK, NULL);
    }

    /* We don't set track number here, because layout recalculation will do it for us */
    /* Set parent */
    mirage_object_set_parent(MIRAGE_OBJECT(new_track), G_OBJECT(self), NULL);
    /* Attach child */
    mirage_object_attach_child(MIRAGE_OBJECT(self), new_track, NULL);
    
    /* Insert track into track list... take into account that lead-in has index 0,
       thus all indexes should be increased by 1 */
    _priv->tracks_list = g_list_insert(_priv->tracks_list, new_track, index + 1);
    
    /* Connect track modified signal */
    g_signal_connect(MIRAGE_OBJECT(new_track), "object-modified", (GCallback)__track_modified_handler, self);
    
    /* Bottom-up change */
    __mirage_session_commit_bottomup_change(self, NULL);
    
    /* Return track to user if she wants it */
    if (track && (*track == NULL)) {
        g_object_ref(new_track);
        *track = new_track;
    }
    
    return TRUE;
}

/**
 * mirage_session_add_track_by_number:
 * @self: a #MIRAGE_Session
 * @number: track number for the added track
 * @track: pointer to #MIRAGE_Track , %NULL pointer or %NULL
 * @error: location to store error, or %NULL
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
 * <para>
 * If @track contains pointer to existing #MIRAGE_Track object, the object
 * is added to session layout. Otherwise, a new #MIRAGE_Track object is created. 
 * If @track contains a %NULL pointer, a reference to newly created object is stored
 * in it; it should be released with g_object_unref() when no longer needed. If @track
 * is %NULL, no reference is returned.
 * </para>
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_add_track_by_number (MIRAGE_Session *self, gint number, GObject **track, GError **error) {
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
    GObject *new_track = NULL;
    
    /* Check if track with that number already exists */
    if (mirage_session_get_track_by_number(self, number, NULL, NULL)) {
        mirage_error(MIRAGE_E_TRACKEXISTS, error);
        return FALSE;
    }
    
    /* If there's track provided, use it; else create new one */
    if (track && *track) {
        new_track = *track;
        /* If track is not MIRAGE_Track... */
        if (!MIRAGE_IS_TRACK(new_track)) {
            mirage_error(MIRAGE_E_INVALIDOBJTYPE, error);
            return FALSE;
        }
        g_object_ref(new_track);
    } else {
        new_track = g_object_new(MIRAGE_TYPE_TRACK, NULL);
    }
    
    /* Set track number */
    mirage_track_layout_set_track_number(MIRAGE_TRACK(new_track), number, NULL);
    /* Set parent */
    mirage_object_set_parent(MIRAGE_OBJECT(new_track), G_OBJECT(self), NULL);
    /* Attach child */
    mirage_object_attach_child(MIRAGE_OBJECT(self), new_track, NULL);
    
    /* Insert track into track list */
    _priv->tracks_list = g_list_insert_sorted(_priv->tracks_list, new_track, (GCompareFunc)__sort_tracks_by_number);
    
    /* Connect track modified signal */
    g_signal_connect(MIRAGE_OBJECT(new_track), "object-modified", (GCallback)__track_modified_handler, self);
    
    /* Bottom-up change */
    __mirage_session_commit_bottomup_change(self, NULL);
    
    /* Return track to user if she wants it */
    if (track && (*track == NULL)) {
        g_object_ref(new_track);
        *track = new_track;
    }
    
    return TRUE;
}

/**
 * mirage_session_remove_track_by_index:
 * @self: a #MIRAGE_Session
 * @index: index of track to be removed
 * @error: location to store error, or %NULL
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
gboolean mirage_session_remove_track_by_index (MIRAGE_Session *self, gint index, GError **error) {
    /*MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);*/
    GObject *track = NULL;
    gboolean succeeded = FALSE;
    
    /* Find track by index */
    if (!mirage_session_get_track_by_index(self, index, &track, error)) {
        return FALSE;
    }
    
    /* Remove track from list */
    succeeded = __remove_track_from_session(self, track, error);
    g_object_unref(track); /* This one's from get */
    
    return succeeded;
}

/**
 * mirage_session_remove_track_by_number:
 * @self: a #MIRAGE_Session
 * @number: track number of track to be removed
 * @error: location to store error, or %NULL
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
gboolean mirage_session_remove_track_by_number (MIRAGE_Session *self, gint number, GError **error) {
    /*MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);*/
    GObject *track = NULL;
    gboolean succeeded = FALSE;
    
    /* You can't delete lead-in/lead-out */
    if (number == MIRAGE_TRACK_LEADIN || number == MIRAGE_TRACK_LEADOUT) {
        mirage_error(MIRAGE_E_INVALIDARG, error);
        return FALSE;
    }
        
    /* Find track in layout */
    if (!mirage_session_get_track_by_number(self, number, &track, error)) {
        return FALSE;
    }
    
    /* Remove track from list */
    succeeded = __remove_track_from_session(self, track, error);
    g_object_unref(track); /* This one's from get */
    
    return succeeded;
}

/**
 * mirage_session_remove_track_by_object:
 * @self: a #MIRAGE_Session
 * @track: track object to be removed
 * @error: location to store error, or %NULL
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
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_remove_track_by_object (MIRAGE_Session *self, GObject *track, GError **error) {
    MIRAGE_CHECK_ARG(track);
    return __remove_track_from_session(self, track, error);
}

/**
 * mirage_session_get_track_by_index:
 * @self: a #MIRAGE_Session
 * @index: index of track to be retrieved
 * @track: location to store track, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track by index. If @index is negative, tracks from the end of 
 * layout are retrieved (e.g. -1 is for last track, -2 for second-to-last 
 * track, etc.). If @index is out of range, regardless of the sign, the 
 * function fails.
 * </para>
 *
 * <para>
 * A reference to track is stored in @track; it should be released with 
 * g_object_unref() when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_get_track_by_index (MIRAGE_Session *self, gint index, GObject **track, GError **error) {
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
    GObject *ret_track = NULL;
    gint num_tracks = 0;
    
    /* First track, last track... allow negative indexes to go from behind */
    mirage_session_get_number_of_tracks(self, &num_tracks, NULL);
    if (index < -num_tracks || index >= num_tracks) {
        mirage_error(MIRAGE_E_INDEXOUTOFRANGE, error);
        return FALSE;
    } else if (index < 0) {
        index += num_tracks; 
    }
    
    /* Get index-th item from list... take into account the fact that index needs
       to be increased by one, because first track we'll get with following function
       will be the lead-in... */
    ret_track = g_list_nth_data(_priv->tracks_list, index + 1);
    
    if (ret_track) {
        /* Return track to user if she wants it */
        if (track) {
            g_object_ref(ret_track);
            *track = ret_track;
        }
        return TRUE;
    }
    
    mirage_error(MIRAGE_E_TRACKNOTFOUND, error);
    return FALSE;
}

/**
 * mirage_session_get_track_by_number:
 * @self: a #MIRAGE_Session
 * @number: number of track to be retrieved
 * @track: location to store track, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track by track number.
 * </para>
 *
 * <para>
 * A reference to track is stored in @track; it should be released with 
 * g_object_unref() when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_get_track_by_number (MIRAGE_Session *self, gint track_number, GObject **track, GError **error) {
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
    GObject *ret_track = NULL;
    GList *entry = NULL;
    
    /* Go over all tracks */
    G_LIST_FOR_EACH(entry, _priv->tracks_list) {        
        gint cur_number = 0;
        
        ret_track = entry->data;
        
        mirage_track_layout_get_track_number(MIRAGE_TRACK(ret_track), &cur_number, NULL);
        
        /* Break the loop if number matches */
        if (track_number == cur_number) {
            break;
        } else {
            ret_track = NULL;
        }
    }
    
    /* If we didn't find anything... */
    if (!ret_track) {
        mirage_error(MIRAGE_E_TRACKNOTFOUND, error);
        return FALSE;
    }
    
    /* Return track to user if she wants it */
    if (track) {
        g_object_ref(ret_track);
        *track = ret_track;
    }
    
    return TRUE;
}

/**
 * mirage_session_get_track_by_address:
 * @self: a #MIRAGE_Session
 * @address: address belonging to track to be retrieved
 * @track: location to store track, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track by address. @address must be valid (disc-relative) sector 
 * address that is part of the track to be retrieved (i.e. lying between tracks's 
 * start and end sector).
 * </para>
 *
 * <para>
 * A reference to track is stored in @track; it should be released with 
 * g_object_unref() when no longer needed.
 * </para>
 *
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_get_track_by_address (MIRAGE_Session *self, gint address, GObject **track, GError **error) {
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
    GObject *ret_track = NULL;
    GList *entry = NULL;
    
    if ((address < _priv->start_sector) || (address >= (_priv->start_sector + _priv->length))) {
        mirage_error(MIRAGE_E_SECTOROUTOFRANGE, error);
        return FALSE;
    }
    
    /* Go over all tracks */
    G_LIST_FOR_EACH(entry, _priv->tracks_list) {        
        gint start_sector = 0;
        gint length = 0;
        
        ret_track = entry->data;
        
        mirage_track_layout_get_start_sector(MIRAGE_TRACK(ret_track), &start_sector, NULL);
        mirage_track_layout_get_length(MIRAGE_TRACK(ret_track), &length, NULL);
        
        /* Break the loop if address lies within track boundaries */
        if (address >= start_sector && address < start_sector + length) {
            break;
        } else {
            ret_track = NULL;
        }
    }
    
    /* If we didn't find anything... */
    if (!ret_track) {
        mirage_error(MIRAGE_E_TRACKNOTFOUND, error);
        return FALSE;
    }
    
    /* Return track to user if she wants it */
    if (track) {
        g_object_ref(ret_track);
        *track = ret_track;
    }
    
    return TRUE;
}

/**
 * mirage_session_for_each_track:
 * @self: a #MIRAGE_Session
 * @func: callback function
 * @user_data: data to be passed to callback function
 * @error: location to store error, or %NULL
 *
 * <para>
 * Iterates over tracks list, calling @func for each track in the layout.
 * </para>
 *
 * <para>
 * If @func returns %FALSE, the function immediately returns %FALSE and @error 
 * is set to %MIRAGE_E_ITERCANCELLED.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_for_each_track (MIRAGE_Session *self, MIRAGE_CallbackFunction func, gpointer user_data, GError **error) {
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
    GList *entry = NULL;
    
    MIRAGE_CHECK_ARG(func);
    
    G_LIST_FOR_EACH(entry, _priv->tracks_list) {
        gboolean succeeded = (*func) (MIRAGE_TRACK(entry->data), user_data);
        if (!succeeded) {
            mirage_error(MIRAGE_E_ITERCANCELLED, error);
            return FALSE;
        }
    }
    
    return TRUE;
}

/**
 * mirage_session_get_track_before:
 * @self: a #MIRAGE_Session
 * @cur_track: a track
 * @prev_track: location to store track, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track that comes before @cur_track.
 * </para>
 *
 * <para>
 * A reference to track is stored in @prev_track; it should be released with 
 * g_object_unref() when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_get_track_before (MIRAGE_Session *self, GObject *cur_track, GObject **prev_track, GError **error) {
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
    gint index = 0;
    
    MIRAGE_CHECK_ARG(cur_track);
    
    /* Get index of given track in the list */
    index = g_list_index(_priv->tracks_list, cur_track);
    if (index == -1) {
        mirage_error(MIRAGE_E_NOTINLAYOUT, error);
        return FALSE;
    }
    index -= 1; /* Because lead-in has index 0... */
    
    /* Now check if we didn't pass the first track (index = 0) and return previous one */
    if (index > 0) {
        return mirage_session_get_track_by_index(self, index - 1, prev_track, error);
    }
    
    mirage_error(MIRAGE_E_TRACKNOTFOUND, error);
    return FALSE;
}

/**
 * mirage_session_get_track_after:
 * @self: a #MIRAGE_Session
 * @cur_track: a track
 * @next_track: location to store track, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track that comes after @cur_track.
 * </para>
 *
 * <para>
 * A reference to track is stored in @next_track; it should be released with 
 * g_object_unref() when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_get_track_after (MIRAGE_Session *self, GObject *cur_track, GObject **next_track, GError **error) {
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
    gint num_tracks = 0;
    gint index = 0;
    
    MIRAGE_CHECK_ARG(cur_track);

    /* Get index of given track in the list */
    index = g_list_index(_priv->tracks_list, cur_track);
    if (index == -1) {
        mirage_error(MIRAGE_E_NOTINLAYOUT, error);
        return FALSE;
    }
    index -= 1; /* Because lead-in has index 0... */
    
    /* Now check if we didn't pass the last track (index = num_tracks - 1) and return previous one */
    mirage_session_get_number_of_tracks(self, &num_tracks, NULL);
    if (index < num_tracks - 1) {
        return mirage_session_get_track_by_index(self, index + 1, next_track, error);
    }
    
    mirage_error(MIRAGE_E_TRACKNOTFOUND, error);
    return FALSE;
}


/**
 * mirage_session_get_number_of_languages:
 * @self: a #MIRAGE_Session
 * @number_of_languages: location to store number of languages
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves number of languages the session contains.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_get_number_of_languages (MIRAGE_Session *self, gint *number_of_languages, GError **error) {
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(number_of_languages);
    /* Return number of languages */
    *number_of_languages = g_list_length(_priv->languages_list); /* Length of list */
    return TRUE;
}

/**
 * mirage_session_add_language:
 * @self: a #MIRAGE_Session
 * @langcode: language code for the added language
 * @language: pointer to #MIRAGE_Language, %NULL pointer or %NULL
 * @error: location to store error, or %NULL
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
 * <para>
 * If @language contains pointer to existing #MIRAGE_Language object, the object
 * is added to track. Otherwise, a new #MIRAGE_Language object is created. 
 * If @language contains a %NULL pointer, a reference to newly created object is stored
 * in it; it should be released with g_object_unref() when no longer needed. If @language
 * is %NULL, no reference is returned.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_add_language (MIRAGE_Session *self, gint langcode, GObject **language, GError **error) {
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
    GObject *new_language = NULL;
    
    /* Check if language already exists */
    if (mirage_session_get_language_by_code(self, langcode, NULL, NULL)) {
        mirage_error(MIRAGE_E_LANGEXISTS, error);
        return FALSE;
    }
    
    /* If there's language provided, use it; else create new one */
    if (language && *language) {
        new_language = *language;
        /* If language is not MIRAGE_CDText... */
        if (!MIRAGE_IS_LANGUAGE(new_language)) {
            mirage_error(MIRAGE_E_INVALIDARG, error);
            return FALSE;
        }
        g_object_ref(new_language);
    } else {
        new_language = g_object_new(MIRAGE_TYPE_LANGUAGE, NULL);
    }
    
    /* Set language code */
    mirage_language_set_langcode(MIRAGE_LANGUAGE(new_language), langcode, NULL);
    /* Set parent */
    mirage_object_set_parent(MIRAGE_OBJECT(new_language), G_OBJECT(self), NULL);
    /* Attach child */
    mirage_object_attach_child(MIRAGE_OBJECT(self), new_language, NULL);
    
    /* Insert language into language list */
    _priv->languages_list = g_list_insert_sorted(_priv->languages_list, new_language, (GCompareFunc)__sort_languages_by_code);
    
    /* Return language to user if she wants it */
    if (language && (*language == NULL)) {
        g_object_ref(new_language);
        *language = new_language;
    }
    
    return TRUE;
}

/**
 * mirage_session_remove_language_by_index:
 * @self: a #MIRAGE_Session
 * @index: index of language to be removed
 * @error: location to store error, or %NULL
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
gboolean mirage_session_remove_language_by_index (MIRAGE_Session *self, gint index, GError **error) {
    /*MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);*/
    GObject *language = NULL;
    gboolean succeeded = FALSE;
    
    /* Find track by index */
    if (!mirage_session_get_language_by_index(self, index, &language, error)) {
        return FALSE;
    }
    
    /* Remove track from list */
    succeeded = __remove_language_from_session(self, language, error);
    g_object_unref(language); /* This one's from get */
    
    return succeeded;
}

/**
 * mirage_session_remove_language_by_code:
 * @self: a #MIRAGE_Session
 * @langcode: language code of language to be removed
 * @error: location to store error, or %NULL
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
gboolean mirage_session_remove_language_by_code (MIRAGE_Session *self, gint langcode, GError **error) {
    /*MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);*/
    GObject *language = NULL;
    gboolean succeeded = FALSE;
    
    /* Find session in layout */
    if (!mirage_session_get_language_by_code(self, langcode, &language, error)) {
        return FALSE;
    }
        
    /* Remove track from list */
    succeeded = __remove_language_from_session(self, language, error);
    g_object_unref(language); /* This one's from get */
    
    return succeeded;
}

/**
 * mirage_session_remove_language_by_object:
 * @self: a #MIRAGE_Session
 * @language: language object to be removed
 * @error: location to store error, or %NULL
 *
 * <para>
 * Removes language from session.
 * </para>
 *
 * <para>
 * @language is a #MIRAGE_Language object to be removed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_remove_language_by_object (MIRAGE_Session *self, GObject *language, GError **error) {
    MIRAGE_CHECK_ARG(language);
    return __remove_language_from_session(self, language, error);
}

/**
 * mirage_session_get_language_by_index:
 * @self: a #MIRAGE_Session
 * @index: index of language to be retrieved
 * @language: location to store language, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves language by index. If @index is negative, languages from the end of 
 * session are retrieved (e.g. -1 is for last language, -2 for second-to-last 
 * language, etc.). If @index is out of range, regardless of the sign, the 
 * function fails.
 * </para>
 *
 * <para>
 * A reference to language is stored in @language; it should be released with 
 * g_object_unref() when no longer needed.
 * </para>
 *
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_get_language_by_index (MIRAGE_Session *self, gint index, GObject **language, GError **error) {
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
    GObject *ret_language = NULL;
    gint num_languages = 0;
    
    /* First language, last language... allow negative indexes to go from behind */
    mirage_session_get_number_of_languages(self, &num_languages, NULL);
    if (index < -num_languages || index >= num_languages) {
        mirage_error(MIRAGE_E_INDEXOUTOFRANGE, error);
        return FALSE;
    } else if (index < 0) {
        index += num_languages; 
    }
    
    /* Get index-th item from list... */
    ret_language = g_list_nth_data(_priv->languages_list, index);
    
    if (ret_language) {
        /* Return language to user if she wants it */
        if (language) {
            g_object_ref(ret_language);
            *language = ret_language;
        }
        return TRUE;
    }
    
    mirage_error(MIRAGE_E_LANGNOTFOUND, error);
    return FALSE;
}

/**
 * mirage_session_get_language_by_code:
 * @self: a #MIRAGE_Session
 * @langcode: language code of language to be retrieved
 * @language: location to store language, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves language by language code.
 * </para>
 *
 * <para>
 * A reference to language is stored in @language; it should be released with 
 * g_object_unref() when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_get_language_by_code (MIRAGE_Session *self, gint langcode, GObject **language, GError **error) {
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
    GObject *ret_language = NULL;
    GList *entry = NULL;
    
    /* Go over all languages */
    G_LIST_FOR_EACH(entry, _priv->languages_list) {        
        gint cur_code = 0;
        
        ret_language = entry->data;
        
        mirage_language_get_langcode(MIRAGE_LANGUAGE(ret_language), &cur_code, NULL);
        
        /* Break the loop if code matches */
        if (langcode == cur_code) {
            break;
        } else {
            ret_language = NULL;
        }
    }
    
    /* If we didn't find anything... */
    if (!ret_language) {
        mirage_error(MIRAGE_E_LANGNOTFOUND, error);
        return FALSE;
    }
    
    /* Return language to user if she wants it */
    if (language) {
        g_object_ref(ret_language);
        *language = ret_language;
    }
    
    return TRUE;
}

/**
 * mirage_session_for_each_language:
 * @self: a #MIRAGE_Session
 * @func: callback function
 * @user_data: data to be passed to callback function
 * @error: location to store error, or %NULL
 *
 * <para>
 * Iterates over languages list, calling @func for each language.
 * </para>
 *
 * <para>
 * If @func returns %FALSE, the function immediately returns %FALSE and @error 
 * is set to %MIRAGE_E_ITERCANCELLED.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_for_each_language (MIRAGE_Session *self, MIRAGE_CallbackFunction func, gpointer user_data, GError **error) {
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
    GList *entry = NULL;
    
    MIRAGE_CHECK_ARG(func);
    
    G_LIST_FOR_EACH(entry, _priv->languages_list) {
        gboolean succeeded = (*func) (MIRAGE_LANGUAGE(entry->data), user_data);
        if (!succeeded) {
            mirage_error(MIRAGE_E_ITERCANCELLED, error);
            return FALSE;
        }
    }
    
    return TRUE;
}

static gboolean __set_cdtext_data (gint langcode, gint type, gint track, guint8 *data, gint len, MIRAGE_Session *self) {
    gboolean succeeded = FALSE;
    GObject *language = NULL;
    
    if (track == 0) {
        /* Session */
        if (!mirage_session_get_language_by_code(self, langcode, &language, NULL)) {
            /* If language does not exist, create it */
            if (!mirage_session_add_language(self, langcode, &language, NULL)) {
                return FALSE;
            }
        }
    } else {
        /* Track */
        GObject *cur_track = NULL;
        if (!mirage_session_get_track_by_number(self, track, &cur_track, NULL)) {
            return FALSE;
        }
        
        if (!mirage_track_get_language_by_code(MIRAGE_TRACK(cur_track), langcode, &language, NULL)) {
            /* If language does not exist, create it */
            if (!mirage_track_add_language(MIRAGE_TRACK(cur_track), langcode, &language, NULL)) {
                g_object_unref(cur_track);
                return FALSE;
            }
        }
        
        g_object_unref(cur_track);
    }
    
    succeeded = mirage_language_set_pack_data(MIRAGE_LANGUAGE(language), type, (gchar *)data, len, NULL);
    
    g_object_unref(language);
    
    return succeeded;
}

/**
 * mirage_session_set_cdtext_data:
 * @self: a #MIRAGE_Session
 * @data: buffer containing encoded CD-TEXT data
 * @len: length of data in buffer
 * @error: location to store error, or %NULL
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
gboolean mirage_session_set_cdtext_data (MIRAGE_Session *self, guint8 *data, gint len, GError **error) {
    GObject *decoder = NULL;
    gboolean succeeded = FALSE;
    
    MIRAGE_CHECK_ARG(data);
    
    /* Create decoder object and hope it'll do all the dirty work correctly... */
    decoder = g_object_new(MIRAGE_TYPE_CDTEXT_ENCDEC, NULL);
    mirage_object_attach_child(MIRAGE_OBJECT(self), decoder, NULL);

    succeeded = mirage_cdtext_decoder_init(MIRAGE_CDTEXT_ENCDEC(decoder), data, len, error);
    if (succeeded) {
        gint i = 0;
        /* Try all possible blocks... */
        while (mirage_cdtext_decoder_get_block_info(MIRAGE_CDTEXT_ENCDEC(decoder), i, NULL, NULL, NULL, NULL)) {
            mirage_cdtext_decoder_get_data(MIRAGE_CDTEXT_ENCDEC(decoder), i, (MIRAGE_CDTextDataCallback)__set_cdtext_data, self, error);
            i++;
        }
    }
    
    /* Free decoder */
    g_object_unref(decoder);
    
    return succeeded;
}

/**
 * mirage_session_get_cdtext_data:
 * @self: a #MIRAGE_Session
 * @data: location to return buffer with encoded CD-TEXT data
 * @len: location to return length of data in buffer
 * @error: location to store error, or %NULL
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
gboolean mirage_session_get_cdtext_data (MIRAGE_Session *self, guint8 **data, gint *len, GError **error) {
    gint num_languages = 0;
    gint num_tracks = 0;
    gint buflen = 0;
    guint8 *buffer = NULL;
    GObject *encoder = NULL;
    gboolean succeeded = TRUE;
    
    /* Allocate space... technically, there could be 255 packs (each 18-byte long) 
       per every language... and spec says we support 8 of those. So we play safe 
       here; get number of languages and go for max number of packs */
    mirage_session_get_number_of_languages(self, &num_languages, NULL);
    mirage_session_get_number_of_tracks(self, &num_tracks, NULL);
    
    buflen = num_languages*255*18 /* Size of CD-TEXT pack */;
    buffer = g_malloc0(buflen);
       
    /* Set up encoder */
    encoder = g_object_new(MIRAGE_TYPE_CDTEXT_ENCDEC, NULL);
    mirage_cdtext_encoder_init(MIRAGE_CDTEXT_ENCDEC(encoder), buffer, buflen, NULL);
    mirage_object_attach_child(MIRAGE_OBJECT(self), encoder, NULL);
    
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
        GObject* session_lang = NULL;
        gint langcode = 0;
        
        if (!mirage_session_get_language_by_index(self, i, &session_lang, error)) {
            g_object_unref(encoder);
            g_free(buffer);
            return FALSE;
        }
        
        mirage_language_get_langcode(MIRAGE_LANGUAGE(session_lang), &langcode, NULL);
        mirage_cdtext_encoder_set_block_info(MIRAGE_CDTEXT_ENCDEC(encoder), i, langcode, 0, 0, NULL);
        
        /* Pack all supported pack types */
        for (j = 0; j < G_N_ELEMENTS(pack_types); j++) {
            gint pack_type = pack_types[j];
            
            guint8 *session_lang_data = NULL;
            gint session_lang_len = 0;
            
            if (mirage_language_get_pack_data(MIRAGE_LANGUAGE(session_lang), pack_type, (gchar **)&session_lang_data, &session_lang_len, NULL)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_SESSION, "%s: adding pack for session; pack type: %02Xh; pack len: %i; pack data: <%s>\n", __debug__, pack_type, session_lang_len, session_lang_data);
                mirage_cdtext_encoder_add_data(MIRAGE_CDTEXT_ENCDEC(encoder), langcode, pack_type, 0, session_lang_data, session_lang_len, NULL);
            }
            
            g_free(session_lang_data);
            
            /* Now get and pack the same data for the all tracks */
            for (k = 0; k < num_tracks; k++) {
                GObject *track = NULL;
                gint number = 0;
                
                GObject *track_lang = NULL;
                guint8 *track_lang_data = NULL;
                gint track_lang_len = 0;
                
                mirage_session_get_track_by_index(self, k, &track, NULL);
                mirage_track_layout_get_track_number(MIRAGE_TRACK(track), &number, NULL);
                                
                if (!mirage_track_get_language_by_code(MIRAGE_TRACK(track), langcode, &track_lang, NULL)) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Failed to get language with code %i on track %i!\n", __debug__, langcode, number);
                    continue;
                }
                
                if (mirage_language_get_pack_data(MIRAGE_LANGUAGE(track_lang), pack_type, (gchar **)&track_lang_data, &track_lang_len, NULL)) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_SESSION, "%s: adding pack for track %i; pack type: %02Xh; pack len: %i; pack data: <%s>\n", __debug__, number, pack_type, track_lang_len, track_lang_data);
                    mirage_cdtext_encoder_add_data(MIRAGE_CDTEXT_ENCDEC(encoder), langcode, pack_type, number, track_lang_data, track_lang_len, NULL);
                }
                   
                g_object_unref(track_lang);
                g_object_unref(track);
            }
            
        }
        
        g_object_unref(session_lang);
    }

    /* Encode */
    succeeded = mirage_cdtext_encoder_encode(MIRAGE_CDTEXT_ENCDEC(encoder), data, len, error);    
    
    /* Free encoder */
    g_object_unref(encoder);
    
    return succeeded;
}


/**
 * mirage_session_get_prev:
 * @self: a #MIRAGE_Session
 * @prev_session: location to store previous session, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves session that is placed before @self in disc layout. A reference 
 * to session is stored in @prev_session; it should be released with g_object_unref() 
 * when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_get_prev (MIRAGE_Session *self, GObject **prev_session, GError **error) {
    GObject *disc = NULL;
    gboolean succeeded = TRUE;
    
    /* Get parent disc */
    if (!mirage_object_get_parent(MIRAGE_OBJECT(self), &disc, error)) {
        return FALSE;
    }
    
    succeeded = mirage_disc_get_session_before(MIRAGE_DISC(disc), G_OBJECT(self), prev_session, error);
    g_object_unref(disc);
    
    return succeeded;
}

/**
 * mirage_session_get_next:
 * @self: a #MIRAGE_Session
 * @next_session: location to store next session, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves session that is placed after @self in disc layout. A reference 
 * to session is stored in @next_session; it should be released with g_object_unref() 
 * when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_session_get_next (MIRAGE_Session *self, GObject **next_session, GError **error) {
    GObject *disc = NULL;
    gboolean succeeded = TRUE;
   
    /* Get parent disc */
    if (!mirage_object_get_parent(MIRAGE_OBJECT(self), &disc, error)) {
        return FALSE;
    }
    
    succeeded = mirage_disc_get_session_before(MIRAGE_DISC(disc), G_OBJECT(self), next_session, error);
    g_object_unref(disc);
    
    return succeeded;
}


/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static MIRAGE_ObjectClass *parent_class = NULL;

static void __mirage_session_instance_init (GTypeInstance *instance, gpointer g_class) {
    MIRAGE_Session *self = MIRAGE_SESSION(instance);
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
    
    _priv->session_number = 1;
    _priv->first_track = 1;
    
    /* Create lead-in */
    mirage_session_add_track_by_number(self, MIRAGE_TRACK_LEADIN, NULL, NULL);
    
    /* Create lead-out */
    mirage_session_add_track_by_number(self, MIRAGE_TRACK_LEADOUT, NULL, NULL);
    
    return;
}

static void __mirage_session_finalize (GObject *obj) {
    MIRAGE_Session *self = MIRAGE_SESSION(obj);
    MIRAGE_SessionPrivate *_priv = MIRAGE_SESSION_GET_PRIVATE(self);
    GList *entry = NULL;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: finalizing object\n", __debug__);

    /* Free list of tracks */
    G_LIST_FOR_EACH(entry, _priv->tracks_list) {
        if (entry->data) {
            GObject *track = entry->data;
            /* Disconnect signal handler and unref */
            g_signal_handlers_disconnect_by_func(MIRAGE_OBJECT(track), __track_modified_handler, self);
            g_object_unref(track);
        }
    }
    g_list_free(_priv->tracks_list);
    
    /* Free list of languages */
    G_LIST_FOR_EACH(entry, _priv->languages_list) {
        if (entry->data) {
            GObject *language = entry->data;
            g_object_unref(language);
        }
    }
    g_list_free(_priv->languages_list);
    
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __debug__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __mirage_session_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_SessionClass *klass = MIRAGE_SESSION_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_SessionPrivate));
    
    /* Initialize GObject members */
    class_gobject->finalize = __mirage_session_finalize;
    
    return;
}

GType mirage_session_get_type (void) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_SessionClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_session_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Session),
            0,      /* n_preallocs */
            __mirage_session_instance_init    /* instance_init */
        };
        
        type = g_type_register_static(MIRAGE_TYPE_OBJECT, "MIRAGE_Session", &info, 0);
    }
    
    return type;
}
