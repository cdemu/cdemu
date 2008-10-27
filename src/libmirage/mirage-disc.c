/*
 *  libMirage: Disc object
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


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_DISC_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_DISC, MIRAGE_DiscPrivate))

typedef struct {
    gchar **filenames;
    
    gint medium_type;
    
    gchar *mcn;
    gboolean mcn_encoded; /* Is MCN encoded in one of track's fragment's subchannel? */
    
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
} MIRAGE_DiscPrivate;


/******************************************************************************\
 *                              Private functions                             *
\******************************************************************************/
/* Forward declarations */
static gboolean __mirage_disc_commit_topdown_change (MIRAGE_Disc *self, GError **error);
static gboolean __mirage_disc_commit_bottomup_change (MIRAGE_Disc *self, GError **error);
static void __session_modified_handler (GObject *session, MIRAGE_Disc *self);
static gboolean __remove_session_from_disc (MIRAGE_Disc *self, GObject *session, GError **error);
static gboolean __generate_disc_structure (MIRAGE_Disc *self, gint layer, gint type, guint8 **data, gint *len, GError **error);
static gint __sort_sessions_by_number (GObject *session1, GObject *session2);
static void __free_disc_structure_data (gpointer data);

static gboolean __mirage_disc_check_for_encoded_mcn (MIRAGE_Disc *self, GError **error);


static gboolean __mirage_disc_commit_topdown_change (MIRAGE_Disc *self, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    GList *entry = NULL;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_CHAIN, "%s: start\n", __func__);
    
    /* Rearrange sessions: set numbers, set first tracks, set start sectors */
    gint cur_session_address = _priv->start_sector;
    gint cur_session_number = _priv->first_session;
    gint cur_session_ftrack = _priv->first_track;
    
    G_LIST_FOR_EACH(entry, _priv->sessions_list) {
        GObject *session = entry->data;
        
        /* Set session's number */
        mirage_session_layout_set_session_number(MIRAGE_SESSION(session), cur_session_number, NULL);
        cur_session_number++;
        
        /* Set session's first track */
        mirage_session_layout_set_first_track(MIRAGE_SESSION(session), cur_session_ftrack, NULL);
        gint session_tracks = 0;
        mirage_session_get_number_of_tracks(MIRAGE_SESSION(session), &session_tracks, NULL);
        cur_session_ftrack += session_tracks;
        
        /* Set session's start address */
        mirage_session_layout_set_start_sector(MIRAGE_SESSION(session), cur_session_address, NULL);
        gint session_length = 0;
        mirage_session_layout_get_length(MIRAGE_SESSION(session), &session_length, NULL);
        cur_session_address += session_length;
    }
        
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_CHAIN, "%s: end\n", __func__);
    
    return TRUE;
}

static gboolean __mirage_disc_commit_bottomup_change (MIRAGE_Disc *self, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    GList *entry = NULL;
        
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_CHAIN, "%s: start\n", __func__);
    
    /* Calculate disc length and number of tracks */
    _priv->length = 0; /* Reset; it'll be recalculated */
    _priv->tracks_number = 0; /* Reset; it'll be recalculated */

    G_LIST_FOR_EACH(entry, _priv->sessions_list) {
        GObject *session = entry->data;
        
        /* Disc length */
        gint session_length = 0;
        mirage_session_layout_get_length(MIRAGE_SESSION(session), &session_length, NULL);
        _priv->length += session_length;
        
        /* Number of all tracks */
        gint session_tracks = 0;
        mirage_session_get_number_of_tracks(MIRAGE_SESSION(session), &session_tracks, NULL);
        _priv->tracks_number += session_tracks;
    }
    
    /* Bottom-up change = eventual change in fragments, so MCN could've changed... */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_CHAIN, "%s: checking for MCN change\n", __func__);
    __mirage_disc_check_for_encoded_mcn(self, NULL);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_CHAIN, "%s: emitting signal\n", __func__);
    
    /* Signal disc change */
    g_signal_emit_by_name(MIRAGE_OBJECT(self), "object-modified", NULL);
    /* Disc is where we complete the arc by committing top-down change */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_CHAIN, "%s: completing arc by committing top-down change\n", __func__);
    __mirage_disc_commit_topdown_change(self, NULL);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_CHAIN, "%s: end\n", __func__);
    
    return TRUE;
}

static void __session_modified_handler (GObject *session, MIRAGE_Disc *self) {
    gint number_of_tracks = 0;
    
    MIRAGE_DEBUG(self, (MIRAGE_DEBUG_DISC|MIRAGE_DEBUG_CHAIN), "%s: start\n", __func__);

    /* If session has been emptied, remove it (it'll do bottom-up change automatically);
       otherwise, signal bottom-up change */
    mirage_session_get_number_of_tracks(MIRAGE_SESSION(session), &number_of_tracks, NULL);
    if (number_of_tracks == 0) {
        __remove_session_from_disc(self, session, NULL);
    } else {
        __mirage_disc_commit_bottomup_change(self, NULL);
    }
    
    MIRAGE_DEBUG(self, (MIRAGE_DEBUG_DISC|MIRAGE_DEBUG_CHAIN), "%s: end\n", __func__);
    
    return;
}

static gboolean __remove_session_from_disc (MIRAGE_Disc *self, GObject *session, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_DISC, "%s: start\n", __func__);

    /* Disconnect signal handler (find it by handler function and user data) */
    g_signal_handlers_disconnect_by_func(MIRAGE_OBJECT(session), __session_modified_handler, self);
    
    /* Remove session from list and unref it */
    _priv->sessions_list = g_list_remove(_priv->sessions_list, session);
    g_object_unref(G_OBJECT(session));
    
    /* Bottom-up change */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_DISC, "%s: commiting bottom-up change\n", __func__);
    __mirage_disc_commit_bottomup_change(self, NULL);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_DISC, "%s: end\n", __func__);
    
    return TRUE;
}

static gboolean __generate_disc_structure (MIRAGE_Disc *self, gint layer, gint type, guint8 **data, gint *len, GError **error) {
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_DISC, "%s: start (layer: %d, type: 0x%X)\n", __func__, layer, type);

    switch (type) {
        case 0x0000: {
            MIRAGE_DiscStruct_PhysInfo *phys_info = g_new0(MIRAGE_DiscStruct_PhysInfo, 1);
            
            gint disc_length = 0;
            mirage_disc_layout_get_length(self, &disc_length, NULL);
            
            phys_info->book_type = 0x00; /* DVD-ROM */
            phys_info->part_ver = 0x05; /* Let's say we comply with v.5 of DVD-ROM book */
            phys_info->disc_size = 0x00; /* 120mm disc */
            phys_info->max_rate = 0x0F; /* Not specified */
            phys_info->num_layers = 0x00; /* 0x00: 1 layer */
            phys_info->track_path = 0; /* Parallell track path */
            phys_info->layer_type = 1; /* Layer contains embossed data */
            phys_info->linear_density = 0; /* 0.267 um/bit */
            phys_info->track_density = 0; /* 0.74 um/track */
            /* The following three fields are 24-bit... */
            phys_info->data_start = GUINT32_FROM_BE(0x30000) >> 8; /* DVD-ROM */
            phys_info->data_end = GUINT32_FROM_BE(0x30000+disc_length) >> 8; /* FIXME: It seems lead-in (out?) length should be subtracted here (241-244 sectors...) */
            phys_info->layer0_end = GUINT32_FROM_BE(0x00) >>8; /* We don't contain multiple layers, but we don't use OTP, so we might get away with this */
            phys_info->bca = 0;
            
            *data = (guint8 *)phys_info;
            *len  = sizeof(MIRAGE_DiscStruct_PhysInfo);
            
            return TRUE;
        }
        case 0x0001: {
            MIRAGE_DiscStruct_Copyright *copy_info = g_new0(MIRAGE_DiscStruct_Copyright, 1);
            
            copy_info->copy_protection = 0x00; /* None */
            copy_info->region_info = 0x00; /* None */
            
            *data = (guint8 *)copy_info;
            *len  = sizeof(MIRAGE_DiscStruct_Copyright);
            
            return TRUE;
        }
        case 0x0004: {
            MIRAGE_DiscStruct_Manufacture *manu_info = g_new0(MIRAGE_DiscStruct_Manufacture, 1);
            
            /* Leave it empty */
            
            *data = (guint8 *)manu_info;
            *len  = sizeof(MIRAGE_DiscStruct_Manufacture);
            
            return TRUE;
        }
    }
    
    return FALSE;
}


static gint __sort_sessions_by_number (GObject *session1, GObject *session2) {
    gint number1 = 0;
    gint number2 = 0;
    
    mirage_session_layout_get_session_number(MIRAGE_SESSION(session1), &number1, NULL);
    mirage_session_layout_get_session_number(MIRAGE_SESSION(session2), &number2, NULL);
    
    if (number1 < number2) {
        return -1;
    } else if (number1 > number2) {
        return 1;
    } else {
        return 0;
    }
}

static void __free_disc_structure_data (gpointer data) {
    GValueArray *array = data;
    
    /* Free data */
    data = g_value_get_pointer(g_value_array_get_nth(array, 1));
    g_free(data);
    
    /* Free array */
    g_value_array_free(array);
    
    return;
}

static gboolean __mirage_disc_check_for_encoded_mcn (MIRAGE_Disc *self, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    GObject *track = NULL;
    gint start_address = 0;
    gint num_tracks = 0;
    gint i;
    
    /* Go over all tracks, and find the first one with fragment that contains
       subchannel... */
    mirage_disc_get_number_of_tracks(self, &num_tracks, NULL);
    for (i = 0; i < num_tracks; i++) {
        if (mirage_disc_get_track_by_index(self, i, &track, NULL)) {
            GObject *fragment = NULL;
            if (mirage_track_find_fragment_with_subchannel(MIRAGE_TRACK(track), &fragment, NULL)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_DISC, "%s: track %i contains subchannel\n", __func__, i);
                mirage_fragment_get_address(MIRAGE_FRAGMENT(fragment), &start_address, NULL);
                g_object_unref(fragment);
                break;
            } else {
                /* Unref the track we just checked */
                g_object_unref(track);
                track = NULL;
            }
        }
    }
    
    if (track) {
        gint cur_address = 0;
        gint end_address = start_address + 100;
        
        _priv->mcn_encoded = TRUE; /* It is, even if it may not be present... */
        /* Reset MCN */
        g_free(_priv->mcn);
        _priv->mcn = NULL;
        
        /* According to INF8090, MCN, if present, must be encoded in at least 
           one sector in 100 consequtive sectors. So we read first hundred 
           sectors' subchannel, and extract MCN if we find it. */
        for (cur_address = start_address; cur_address < end_address; cur_address++) {
            guint8 tmp_buf[16];
            
            if (!mirage_track_read_sector(MIRAGE_TRACK(track), cur_address, FALSE, 0, MIRAGE_SUBCHANNEL_PQ, tmp_buf, NULL, error)) {
                g_object_unref(track);
                return FALSE;
            }
            
            if ((tmp_buf[0] & 0x0F) == 0x02) {
                /* Mode-2 Q found */
                gchar tmp_mcn[13];
                
                mirage_helper_subchannel_q_decode_mcn(&tmp_buf[1], tmp_mcn);
                
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: found MCN: <%s>\n", __func__, tmp_mcn);
                
                /* Set MCN */
                _priv->mcn = g_strndup(tmp_mcn, 13);
            }
        }
        
        g_object_unref(track);
    }
    
    return TRUE;
}


/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
/**
 * mirage_disc_set_medium_type:
 * @self: a #MIRAGE_Disc
 * @medium_type: medium type
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets medium type. @medium_type must be one of #MIRAGE_MediumTypes.
 * </para>
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_set_medium_type (MIRAGE_Disc *self, gint medium_type, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    /* Set medium type */
    _priv->medium_type = medium_type;
    return TRUE;
}

/**
 * mirage_disc_get_medium_type:
 * @self: a #MIRAGE_Disc
 * @medium_type: location to store medium type.
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves medium type.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_get_medium_type (MIRAGE_Disc *self, gint *medium_type, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(medium_type);
    /* Return medium type */
    *medium_type = _priv->medium_type;
    return TRUE;
}


/**
 * mirage_disc_set_filenames:
 * @self: a #MIRAGE_Disc
 * @filenames: filenames
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets image filename(s).
 * </para>
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_set_filenames (MIRAGE_Disc *self, gchar **filenames, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(filenames);
    /* Free old filenames */
    g_strfreev(_priv->filenames);
    /* Set filenames */
    _priv->filenames = g_strdupv(filenames);
    return TRUE;
}

/**
 * mirage_disc_get_filenames:
 * @self: a #MIRAGE_Disc
 * @filenames: location to store image filename(s)
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves image filename(s). 
 * </para>
 *
 * <para>
 * A copy of filenames array is stored in @filenames; it should be freed with 
 * g_strfreev() when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_get_filenames (MIRAGE_Disc *self, gchar ***filenames, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(filenames);
    /* Check if filenames are set */
    if (!_priv->filenames) {
        mirage_error(MIRAGE_E_DATANOTSET, error);
        return FALSE;
    }
    /* Return filenames */
    *filenames = g_strdupv(_priv->filenames);
    return TRUE;
}


/**
 * mirage_disc_set_mcn:
 * @self: a #MIRAGE_Disc
 * @mcn: MCN
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets MCN (Media Catalogue Number).
 * </para>
 *
 * <para>
 * Because MCN is stored in subchannel data, this function fails if any of disc's
 * tracks contains fragments with subchannel data provided. In that case error is 
 * set to %MIRAGE_E_DATAFIXED.
 * </para>
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_set_mcn (MIRAGE_Disc *self, gchar *mcn, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    
    MIRAGE_CHECK_ARG(mcn);

    /* MCN can be set only if none of the tracks have fragments that contain 
       subchannel; this is because MCN is encoded in the subchannel, and cannot 
       be altered... */
    
    if (_priv->mcn_encoded) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_DISC, "%s: MCN is already encoded in subchannel!\n", __func__);
        mirage_error(MIRAGE_E_DATAFIXED, error);
        return FALSE;
    } else {
        g_free(_priv->mcn);
        _priv->mcn = g_strndup(mcn, 13);
    }
    
    return TRUE;
}

/**
 * mirage_disc_get_mcn:
 * @self: a #MIRAGE_Disc
 * @mcn: location to store MCN, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves MCN.
 * </para>
 *
 * <para>
 * A copy of MCN string is stored in @mcn; it should be freed with g_free() 
 * when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_get_mcn (MIRAGE_Disc *self, gchar **mcn, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    
    /* Check if MCN is set */
    if (!(_priv->mcn && _priv->mcn[0])) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_DISC, "%s: MCN not set!\n", __func__);
        mirage_error(MIRAGE_E_DATANOTSET, error);
        return FALSE;
    }
    
    /* Return MCN */
    if (mcn) {
        *mcn = g_strndup(_priv->mcn, 13);
    }
    
    return TRUE;
}


/**
 * mirage_disc_layout_set_first_session:
 * @self: a #MIRAGE_Disc
 * @first_session: first session number
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets first session number to @first_session. This is a number that is
 * assigned to the first session in the disc layout.
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
gboolean mirage_disc_layout_set_first_session (MIRAGE_Disc *self, gint first_session, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    /* Set first session */
    _priv->first_session = first_session;
    /* Top-down change */
    __mirage_disc_commit_topdown_change(self, NULL);
    return TRUE;
}

/**
 * mirage_disc_layout_get_first_session:
 * @self: a #MIRAGE_Disc
 * @first_session: location to store first session number
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves session number of the first session in the disc layout.
 * </para>
 * 
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_layout_get_first_session (MIRAGE_Disc *self, gint *first_session, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(first_session);
    /* Return first session */
    *first_session = _priv->first_session;
    return TRUE;
}

/**
 * mirage_disc_layout_set_first_track:
 * @self: a #MIRAGE_Disc
 * @first_track: first track number
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets first track number to @first_track. This is a number that is
 * assigned to the first track in the disc layout.
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
gboolean mirage_disc_layout_set_first_track (MIRAGE_Disc *self, gint first_track, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    /* Set first track */
    _priv->first_track = first_track;
    /* Top-down change */
    __mirage_disc_commit_topdown_change(self, NULL);
    return TRUE;
}

/**
 * mirage_disc_layout_get_first_track:
 * @self: a #MIRAGE_Disc
 * @first_track: location to store first track number
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track number of the first track in the disc layout.
 * </para>
 * 
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_layout_get_first_track (MIRAGE_Disc *self, gint *first_track, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(first_track);
    /* Return first track */
    *first_track = _priv->first_track;
    return TRUE;
}

/**
 * mirage_disc_layout_set_start_sector:
 * @self: a #MIRAGE_Disc
 * @start_sector: start sector
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets start sector of the disc layout to @start_sector. This is a sector at which
 * the first session (and consequently first track) in the disc layout will start.
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
gboolean mirage_disc_layout_set_start_sector (MIRAGE_Disc *self, gint start_sector, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    /* Set start sector */
    _priv->start_sector = start_sector;
    /* Top-down change */
    __mirage_disc_commit_topdown_change(self, NULL);
    return TRUE;
}

/**
 * mirage_disc_layout_get_start_sector:
 * @self: a #MIRAGE_Disc
 * @start_sector: location to store start sector
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves start sector of the disc layout.
 * </para>
 * 
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_layout_get_start_sector (MIRAGE_Disc *self, gint *start_sector, GError **error)  {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(start_sector);
    /* Return start sector */
    *start_sector = _priv->start_sector;
    return TRUE;
}

/**
 * mirage_disc_layout_get_length:
 * @self: a #MIRAGE_Disc
 * @length: location to store disc layout length
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves length of the disc layout. The returned length is given in sectors.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_layout_get_length (MIRAGE_Disc *self, gint *length, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(length);
    /* Return length */
    *length = _priv->length;
    return TRUE;
}


/**
 * mirage_disc_get_number_of_sessions:
 * @self: a #MIRAGE_Disc
 * @number_of_sessions: location to store number of sessions
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves number of sessions in the disc layout.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_get_number_of_sessions (MIRAGE_Disc *self, gint *number_of_sessions, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(number_of_sessions);
    /* Return number of sessions */
    *number_of_sessions = g_list_length(_priv->sessions_list); /* Length of list */
    return TRUE;
}

/**
 * mirage_disc_add_session_by_index:
 * @self: a #MIRAGE_Disc
 * @index: index at which session should be added
 * @session: pointer to #MIRAGE_Session, %NULL pointer or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Adds session to disc layout.
 * </para>
 *
 * <para>
 * @index is the index at which session is added. Negative index denotes 
 * index going backwards (i.e. -1 adds session at the end, -2 adds session
 * second-to-last, etc.). If index, either negative or positive, is too big, 
 * session is respectively added at the beginning or at the end of the layout.
 * </para>
 *
 * <para>
 * If @session contains pointer to existing #MIRAGE_Session object, the object
 * is added to disc layout. Otherwise, a new #MIRAGE_Session object is created. 
 * If @session contains a %NULL pointer, a reference to newly created object is stored
 * in it; it should be released with g_object_unref() when no longer needed. If @session
 * is %NULL, no reference is returned.
 * </para>
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_add_session_by_index (MIRAGE_Disc *self, gint index, GObject **session, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    GObject *new_session = NULL;
    gint num_sessions = 0;
    
    /* First session, last session... allow negative indexes to go from behind */
    mirage_disc_get_number_of_sessions(self, &num_sessions, NULL);
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
    
    /* If there's session provided, use it; else create new session */
    if (session && *session) {
        new_session = *session;
        /* If session is not MIRAGE_Session... */
        if (!MIRAGE_IS_SESSION(new_session)) {
            mirage_error(MIRAGE_E_INVALIDOBJTYPE, error);
            return FALSE;
        }
        g_object_ref(new_session);
    } else {
        new_session = g_object_new(MIRAGE_TYPE_SESSION, NULL);
    }

    /* We don't set session number here, because layout recalculation will do it for us */
    /* Set parent */
    mirage_object_set_parent(MIRAGE_OBJECT(new_session), G_OBJECT(self), NULL);
    /* Attach child */
    mirage_object_attach_child(MIRAGE_OBJECT(self), new_session, NULL);
    
    /* Insert session into sessions list */
    _priv->sessions_list = g_list_insert(_priv->sessions_list, new_session, index);
    
    /* Connect session modified signal */
    g_signal_connect(MIRAGE_OBJECT(new_session), "object-modified", (GCallback)__session_modified_handler, self);
    
    /* Bottom-up change */
    __mirage_disc_commit_bottomup_change(self, NULL);
    
    /* Return session to user if she wants it */
    if (session && (*session == NULL)) {
        g_object_ref(new_session);
        *session = new_session;
    }
    
    return TRUE;
}

/**
 * mirage_disc_add_session_by_number:
 * @self: a #MIRAGE_Disc
 * @number: session number for the added session
 * @session: pointer to #MIRAGE_Session, %NULL pointer or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Adds session to disc layout.
 * </para>
 *
 * <para>
 * @number is session number that should be assigned to added session. It determines
 * session's position in the layout. If session with that number already exists in 
 * the layout, the function fails.
 * </para>
 *
 * <para>
 * If @session contains pointer to existing #MIRAGE_Session object, the object
 * is added to disc layout. Otherwise, a new #MIRAGE_Session object is created. 
 * If @session contains a %NULL pointer, a reference to newly created object is stored
 * in it; it should be released with g_object_unref() when no longer needed. If @session
 * is %NULL, no reference is returned.
 * </para>
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_add_session_by_number (MIRAGE_Disc *self, gint number, GObject **session, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    GObject *new_session = NULL;
        
    /* Check if session with that number already exists */
    if (mirage_disc_get_session_by_number(self, number, NULL, NULL)) {
        mirage_error(MIRAGE_E_SESSIONEXISTS, error);
        return FALSE;
    }
    
    /* If there's session provided, use it; else create new one */
    if (session && *session) {
        new_session = *session;
        /* If session is not MIRAGE_Session... */
        if (!MIRAGE_IS_SESSION(new_session)) {
            mirage_error(MIRAGE_E_INVALIDOBJTYPE, error);
            return FALSE;
        }
        g_object_ref(new_session);
    } else {
        new_session = g_object_new(MIRAGE_TYPE_SESSION, NULL);
    }
    
    /* Set session number */
    mirage_session_layout_set_session_number(MIRAGE_SESSION(new_session), number, NULL);
    /* Set parent */
    mirage_object_set_parent(MIRAGE_OBJECT(new_session), G_OBJECT(self), NULL);
    /* Attach child */
    mirage_object_attach_child(MIRAGE_OBJECT(self), new_session, NULL);
    
    /* Insert session into sessions list */
    _priv->sessions_list = g_list_insert_sorted(_priv->sessions_list, new_session, (GCompareFunc)__sort_sessions_by_number);
    
    /* Connect session modified signal */
    g_signal_connect(MIRAGE_OBJECT(new_session), "object-modified", (GCallback)__session_modified_handler, self);
        
    /* Bottom-up change */
    __mirage_disc_commit_bottomup_change(self, NULL);
    
    /* Return session to user if she wants it */
    if (session && (*session == NULL)) {
        g_object_ref(new_session);
        *session = new_session;
    }
    
    return TRUE;
}

/**
 * mirage_disc_remove_session_by_index:
 * @self: a #MIRAGE_Disc
 * @index: index of session to be removed
 * @error: location to store error, or %NULL
 *
 * <para>
 * Removes session from disc layout.
 * </para>
 *
 * <para>
 * @index is the index of the session to be removed. This function calls 
 * mirage_disc_get_session_by_index() so @index behavior is determined by that 
 * function.
 * </para>
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_remove_session_by_index (MIRAGE_Disc *self, gint index, GError **error) {
    /*MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);*/
    GObject *session = NULL;
    gboolean succeeded = FALSE;
    
    /* Find session by index */
    if (!mirage_disc_get_session_by_index(self, index, &session, error)) {
        return FALSE;
    }
    
    /* Remove session from list */
    succeeded = __remove_session_from_disc(self, session, error);
    g_object_unref(session); /* This one's from get */
    
    return succeeded;
}

/**
 * mirage_disc_remove_session_by_number:
 * @self: a #MIRAGE_Disc
 * @number: session number of session to be removed
 * @error: location to store error, or %NULL
 *
 * <para>
 * Removes session from disc layout.
 * </para>
 *
 * <para>
 * @number is session number of the session to be removed.
 * </para>
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_remove_session_by_number (MIRAGE_Disc *self, gint number, GError **error) {
    /*MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);*/
    GObject *session = NULL;
    gboolean succeeded = FALSE;
        
    /* Find session by number */
    if (!mirage_disc_get_session_by_number(self, number, &session, error)) {
        return FALSE;
    }
    
    /* Remove track from list */
    succeeded = __remove_session_from_disc(self, session, error);
    g_object_unref(session); /* This one's from get */
    
    return succeeded;
}

/**
 * mirage_disc_remove_session_by_object:
 * @self: a #MIRAGE_Disc
 * @session: session object to be removed
 * @error: location to store error, or %NULL
 *
 * <para>
 * Removes session from disc layout.
 * </para>
 *
 * <para>
 * @session is a #MIRAGE_Session object to be removed.
 * </para>
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_remove_session_by_object (MIRAGE_Disc *self, GObject *session, GError **error) {
    MIRAGE_CHECK_ARG(session);
    return __remove_session_from_disc(self, session, error);
}

/**
 * mirage_disc_get_session_by_index:
 * @self: a #MIRAGE_Disc
 * @index: index of session to be retrieved
 * @session: location to store session, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves session by index. If @index is negative, sessions from the end of 
 * layout are retrieved (e.g. -1 is for last session, -2 for second-to-last 
 * session, etc.). If @index is out of range, regardless of the sign, the 
 * function fails.
 * </para>
 *
 * <para>
 * A reference to session is stored in @session; it should be released with 
 * g_object_unref() when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_get_session_by_index (MIRAGE_Disc *self, gint index, GObject **session, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    GObject *ret_session = NULL;
    gint num_sessions = 0;
    
    /* First session, last session... allow negative indexes to go from behind */
    mirage_disc_get_number_of_sessions(self, &num_sessions, NULL);
    if (index < -num_sessions || index >= num_sessions) {
        mirage_error(MIRAGE_E_INDEXOUTOFRANGE, error);
        return FALSE;
    } else if (index < 0) {
        index += num_sessions; 
    }
    
    /* Get index-th item from list... */
    ret_session = g_list_nth_data(_priv->sessions_list, index);
    
    if (ret_session) {
        /* Return session to user if she wants it */
        if (session) {
            g_object_ref(ret_session);
            *session = ret_session;
        }
        return TRUE;
    }
    
    mirage_error(MIRAGE_E_SESSIONNOTFOUND, error);
    return FALSE;
}

/**
 * mirage_disc_get_session_by_number:
 * @self: a #MIRAGE_Disc
 * @number: number of session to be retrieved
 * @session: location to store session, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves session by session number.
 * </para>
 *
 * <para>
 * A reference to session is stored in @session; it should be released with 
 * g_object_unref() when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_get_session_by_number (MIRAGE_Disc *self, gint session_number, GObject **session, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    GObject *ret_session = NULL;
    GList *entry = NULL;
    
    /* Go over all sessions */
    G_LIST_FOR_EACH(entry, _priv->sessions_list) {        
        gint cur_number = 0;
        
        ret_session = entry->data;
        
        mirage_session_layout_get_session_number(MIRAGE_SESSION(ret_session), &cur_number, NULL);
        
        /* Break the loop if number matches */
        if (session_number == cur_number) {
            break;
        } else {
            ret_session = NULL;
        }
    }
    
    /* If we didn't find anything... */
    if (!ret_session) {
        mirage_error(MIRAGE_E_SESSIONNOTFOUND, error);
        return FALSE;
    }
    
    /* Return session to user if she wants it */
    if (session) {
        g_object_ref(ret_session);
        *session = ret_session;
    }
    
    return TRUE;
}

/**
 * mirage_disc_get_session_by_address:
 * @self: a #MIRAGE_Disc
 * @address: address belonging to session to be retrieved
 * @session: location to store session, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves session by address. @address must be valid (disc-relative) sector 
 * address that is part of the session to be retrieved (i.e. lying between session's 
 * start and end sector).
 * </para>
 *
 * <para>
 * A reference to session is stored in @session; it should be released with 
 * g_object_unref() when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_get_session_by_address (MIRAGE_Disc *self, gint address, GObject **session, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    GObject *ret_session = NULL;
    GList *entry = NULL;
    
    if ((address < _priv->start_sector) || (address >= _priv->start_sector + _priv->length)) {
        mirage_error(MIRAGE_E_SECTOROUTOFRANGE, error);
        return FALSE;
    }
    
    /* Go over all sessions */
    G_LIST_FOR_EACH(entry, _priv->sessions_list) {        
        gint start_sector = 0;
        gint length = 0;
        
        ret_session = entry->data;
        
        mirage_session_layout_get_start_sector(MIRAGE_SESSION(ret_session), &start_sector, NULL);
        mirage_session_layout_get_length(MIRAGE_SESSION(ret_session), &length, NULL);
        
        /* Break the loop if address lies within session boundaries */
        if (address >= start_sector && address < start_sector + length) {
            break;
        } else {
            ret_session = NULL;
        }
    }
    
    /* If we didn't find anything... */
    if (!ret_session) {
        mirage_error(MIRAGE_E_SESSIONNOTFOUND, error);
        return FALSE;
    }
    
    /* Return session to user if she wants it */
    if (session) {
        g_object_ref(ret_session);
        *session = ret_session;
    }
    
    return TRUE;
}

/**
 * mirage_disc_get_session_by_track:
 * @self: a #MIRAGE_Disc
 * @track: number of track belonging to session to be retrieved
 * @session: location to store session, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves session by track number. @track must be valid track number of track
 * that is part of the session.
 * </para>
 *
 * <para>
 * A reference to session is stored in @session; it should be released with 
 * g_object_unref() when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_get_session_by_track (MIRAGE_Disc *self, gint track_number, GObject **session, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    GObject *ret_session = NULL;
    GList *entry = NULL;
    
    /* Go over all sessions */
    G_LIST_FOR_EACH(entry, _priv->sessions_list) {        
        gint first_track = 0;
        gint num_tracks = 0;
        
        ret_session = entry->data;
        
        mirage_session_layout_get_first_track(MIRAGE_SESSION(ret_session), &first_track, NULL);
        mirage_session_get_number_of_tracks(MIRAGE_SESSION(ret_session), &num_tracks, NULL);
        
        /* Break the loop if track with that number is part of the session */
        if (track_number >= first_track && track_number < first_track + num_tracks) {
            break;
        } else {
            ret_session = NULL;
        }
    }
    
    /* If we didn't find anything... */
    if (!ret_session) {
        mirage_error(MIRAGE_E_SESSIONNOTFOUND, error);
        return FALSE;
    }
    
    /* Return session to user if she wants it */
    if (session) {
        g_object_ref(ret_session);
        *session = ret_session;
    }
    
    return TRUE;
}

/**
 * mirage_disc_for_each_session:
 * @self: a #MIRAGE_Disc
 * @func: callback function
 * @user_data: data to be passed to callback function
 * @error: location to store error, or %NULL
 *
 * <para>
 * Iterates over sessions list, calling @func for each session in the layout.
 * </para>
 *
 * <para>
 * If @func returns %FALSE, the function immediately returns %FALSE and @error 
 * is set to %MIRAGE_E_ITERCANCELLED.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_for_each_session (MIRAGE_Disc *self, MIRAGE_CallbackFunction func, gpointer user_data, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    GList *entry = NULL;
    
    MIRAGE_CHECK_ARG(func);

    G_LIST_FOR_EACH(entry, _priv->sessions_list) {
        gboolean succeeded = (*func) (MIRAGE_SESSION(entry->data), user_data);
        if (!succeeded) {
            mirage_error(MIRAGE_E_ITERCANCELLED, error);
            return FALSE;
        }
    }
    
    return TRUE;
}

/**
 * mirage_disc_get_session_before:
 * @self: a #MIRAGE_Disc
 * @cur_session: a session
 * @prev_session: location to store session, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves session that comes before @cur_session.
 * </para>
 *
 * <para>
 * A reference to session is stored in @prev_session; it should be released with 
 * g_object_unref() when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_get_session_before (MIRAGE_Disc *self, GObject *cur_session, GObject **prev_session, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    gint index = 0;
    
    MIRAGE_CHECK_ARG(cur_session);

    /* Get index of given session in the list */
    index = g_list_index(_priv->sessions_list, cur_session);
    if (index == -1) {
        mirage_error(MIRAGE_E_NOTINLAYOUT, error);
        return FALSE;
    }
    
    /* Now check if we didn't pass the first session (index = 0) and return previous one */
    if (index > 0) {
        return mirage_disc_get_session_by_index(self, index - 1, prev_session, error);
    }
    
    mirage_error(MIRAGE_E_SESSIONNOTFOUND, error);
    return FALSE;
}

/**
 * mirage_disc_get_session_after:
 * @self: a #MIRAGE_Disc
 * @cur_session: a session
 * @next_session: location to store session, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves session that comes after @cur_session.
 * </para>
 *
 * <para>
 * A reference to session is stored in @next_session; it should be released with 
 * g_object_unref() when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_get_session_after (MIRAGE_Disc *self, GObject *cur_session, GObject **next_session, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    gint num_sessions = 0;
    gint index = 0;
    
    MIRAGE_CHECK_ARG(cur_session);
    
    /* Get index of given session in the list */
    index = g_list_index(_priv->sessions_list, cur_session);
    if (index == -1) {
        mirage_error(MIRAGE_E_NOTINLAYOUT, error);
        return FALSE;
    }
    
    /* Now check if we didn't pass the last session (index = num_sessions - 1) and return previous one */
    mirage_disc_get_number_of_sessions(self, &num_sessions, NULL);
    if (index < num_sessions - 1) {
        return mirage_disc_get_session_by_index(self, index + 1, next_session, error);
    }
    
    mirage_error(MIRAGE_E_SESSIONNOTFOUND, error);
    return FALSE;
}


/**
 * mirage_disc_get_number_of_tracks:
 * @self: a #MIRAGE_Disc
 * @number_of_tracks: location to store number of tracks
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves number of tracks in the disc layout.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_get_number_of_tracks (MIRAGE_Disc *self, gint *number_of_tracks, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(number_of_tracks);
    /* Return number of tracks */
    *number_of_tracks = _priv->tracks_number;    
    return TRUE;
}

/**
 * mirage_disc_add_track_by_index:
 * @self: a #MIRAGE_Disc
 * @index: index at which track should be added
 * @track: pointer to #MIRAGE_Track, %NULL pointer or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Adds track to disc layout.
 * </para>
 *
 * <para>
 * @index is the index at which track is added. The function attempts to find 
 * appropriate session by iterating over sessions list and verifying index ranges, 
 * then adds the track using mirage_session_add_track_by_index(). Negative 
 * @index denotes index going backwards (i.e. -1 adds track at the end of last 
 * session, etc.). If @index, either negative or positive, is too big, track is 
 * respectively added  at the beginning of the first or at the end of the last 
 * session in the layout.
 * </para>
 *
 * <para>
 * If disc layout is empty (i.e. contains no sessions), then session is created.
 * </para>
 *
 * <para>
 * The rest of behavior is same as of mirage_session_add_track_by_index().
 * </para>
 *
 * <note>
 * Causes bottom-up change.
 * </note> 
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_add_track_by_index (MIRAGE_Disc *self, gint index, GObject **track, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    GList *entry = NULL;
    gint num_sessions = 0;
    gint num_tracks = 0;
    gint count = 0;
    
    /* If disc layout is empty (if there are no sessions), we should create
       a session... and then track will be added to this one */
    mirage_disc_get_number_of_sessions(self, &num_sessions, NULL);
    if (!num_sessions) {
        mirage_disc_add_session_by_index(self, 0, NULL, NULL);
    }
    
    /* First track, last track... allow negative indexes to go from behind */
    mirage_disc_get_number_of_tracks(self, &num_tracks, NULL);
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
    G_LIST_FOR_EACH(entry, _priv->sessions_list) {
        GObject *cur_session = entry->data;
        
        mirage_session_get_number_of_tracks(MIRAGE_SESSION(cur_session), &num_tracks, NULL);
                
        if (index >= count && index <= count + num_tracks) {
            /* We got the session */
            return mirage_session_add_track_by_index(MIRAGE_SESSION(cur_session), index - count, track, error);
        }
        
        count += num_tracks;
    }
    
    mirage_error(MIRAGE_E_SESSIONNOTFOUND, error);
    return FALSE;
}

/**
 * mirage_disc_add_track_by_number:
 * @self: a #MIRAGE_Disc
 * @number: track number for the added track
 * @track: pointer to #MIRAGE_Track, %NULL pointer or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Adds track to disc layout.
 * </para>
 *
 * <para>
 * @number is track number that should be assigned to added track. It determines
 * track's position in the layout. The function attempts to find appropriate session
 * using mirage_disc_get_session_by_track(), then adds the track using
 * mirage_session_add_track_by_number().
 * </para>
 *
 * <para>
 * If disc layout is empty (i.e. contains no sessions), then session is created.
 * If @number is greater than last track's number, the track is added at the end
 * of last session.
 * </para>
 *
 * <para>
 * The rest of behavior is same as of mirage_session_add_track_by_number().
 * </para>
 *
 * <note>
 * Causes bottom-up change.
 * </note> 
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_add_track_by_number (MIRAGE_Disc *self, gint number, GObject **track, GError **error) {
    GObject *session = NULL;
    GObject *last_track = NULL;
    gboolean succeeded = FALSE;
    gint num_sessions = 0;
    gint last_number = 0;
    
    mirage_disc_get_number_of_sessions(self, &num_sessions, NULL);
    if (mirage_disc_get_track_by_index(self, -1, &last_track, NULL)) {
        mirage_track_layout_get_track_number(MIRAGE_TRACK(last_track), &last_number, NULL);
        g_object_unref(last_track);
    }
    
    if (!num_sessions) {
        /* If disc layout is empty (if there are no sessions), we should create
           a session... and then track will be added to this one */
        succeeded = mirage_disc_add_session_by_index(self, 0, &session, error);
    } else if (number > last_number) {
        /* If track number surpasses the number of last track on disc, then it
           means we need to add the track into last session */
        succeeded = mirage_disc_get_session_by_index(self, -1, &session, error);
    } else {
        /* Try to get the session by track number */
        succeeded = mirage_disc_get_session_by_track(self, number, &session, error);
    }
    if (!succeeded) {
        return succeeded;
    }
    
    /* If session was found, try to add track */
    succeeded = mirage_session_add_track_by_number(MIRAGE_SESSION(session), number, track, error);
    g_object_unref(session);
    return succeeded;
}

/**
 * mirage_disc_remove_track_by_index:
 * @self: a #MIRAGE_Disc
 * @index: index of track to be removed
 * @error: location to store error, or %NULL
 *
 * <para>
 * Removes track from disc layout.
 * </para>
 *
 * <para>
 * @index is the index of the track to be removed. This function calls 
 * mirage_disc_get_track_by_index() so @index behavior is determined by that 
 * function.
 * </para>
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_remove_track_by_index (MIRAGE_Disc *self, gint index, GError **error) {
    GObject *session = NULL;
    GObject *track = NULL;
    gboolean succeeded = TRUE;
    
    /* Get track directly */
    if (!mirage_disc_get_track_by_index(self, index, &track, error)) {
        return FALSE;
    }
    /* Get track's parent */
    if (!mirage_object_get_parent(MIRAGE_OBJECT(track), &session, error)) {
        g_object_unref(track);
        return FALSE;
    }
    /* Remove track from parent */
    succeeded = mirage_session_remove_track_by_object(MIRAGE_SESSION(session), track, error);
    
    g_object_unref(track);
    g_object_unref(session);
    
    return succeeded;
}

/**
 * mirage_disc_remove_track_by_number:
 * @self: a #MIRAGE_Disc
 * @number: track number of track to be removed
 * @error: location to store error, or %NULL
 *
 * <para>
 * Removes track from disc layout.
 * </para>
 *
 * <para>
 * @number is track number of the track to be removed. This function calls 
 * mirage_disc_get_track_by_number() so @number behavior is determined by that 
 * function.
 * </para>
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_remove_track_by_number (MIRAGE_Disc *self, gint number, GError **error) {
    GObject *session = NULL;
    GObject *track = NULL;
    gboolean succeeded = TRUE;
        
    /* Protect against removing lead-in and lead-out */
    if (number == MIRAGE_TRACK_LEADIN || number == MIRAGE_TRACK_LEADOUT) {
        mirage_error(MIRAGE_E_INVALIDARG, error);
        return FALSE;
    }
    
    /* Get track directly */
    if (!mirage_disc_get_track_by_number(self, number, &track, error)) {
        return FALSE;
    }
    /* Get track's parent */
    if (!mirage_object_get_parent(MIRAGE_OBJECT(track), &session, error)) {
        g_object_unref(track);
        return FALSE;
    }
    /* Remove track from parent */
    succeeded = mirage_session_remove_track_by_object(MIRAGE_SESSION(session), track, error);
    
    g_object_unref(track);
    g_object_unref(session);
    
    return succeeded;
}

/**
 * mirage_disc_get_track_by_index:
 * @self: a #MIRAGE_Disc
 * @index: index of track to be retrieved
 * @track: location to store track, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track by index. The function attempts to find appropriate session 
 * by iterating over sessions list and verifying index ranges, then retrieves 
 * the track using mirage_session_get_track_by_index(). If @index is negative, 
 * tracks from the end of layout are retrieved (e.g. -1 is for last track, -2 
 * for second-to-last track, etc.). If @index is out of range, regardless of 
 * the sign, the function fails.
 * </para>
 *
 * <para>
 * The rest of behavior is same as of mirage_session_get_track_by_index().
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_get_track_by_index (MIRAGE_Disc *self, gint index, GObject **track, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    GList *entry = NULL;
    gint num_tracks = 0;
    gint count = 0;
    
    /* First track, last track... allow negative indexes to go from behind */
    mirage_disc_get_number_of_tracks(self, &num_tracks, NULL);
    if (index < -num_tracks || index >= num_tracks) {
        mirage_error(MIRAGE_E_INDEXOUTOFRANGE, error);
        return FALSE;
    } else if (index < 0) {
        index += num_tracks; 
    }
    
    /* Loop over the sessions */
    G_LIST_FOR_EACH(entry, _priv->sessions_list) {
        GObject *cur_session = entry->data;
        
        mirage_session_get_number_of_tracks(MIRAGE_SESSION(cur_session), &num_tracks, NULL);
        
        if (index >= count && index < count + num_tracks) {
            /* We got the session */
            return mirage_session_get_track_by_index(MIRAGE_SESSION(cur_session), index - count, track, error);
        }
        
        count += num_tracks;
    }
    
    mirage_error(MIRAGE_E_TRACKNOTFOUND, error);
    return FALSE;
}

/**
 * mirage_disc_get_track_by_number:
 * @self: a #MIRAGE_Disc
 * @number: track number of track to be retrieved
 * @track: location to store track, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track by track number. The function attempts to find appropriate session 
 * using mirage_disc_get_session_by_track(), then retrieves the track using
 * mirage_session_get_track_by_number().
 * </para>
 *
 * <para>
 * The rest of behavior is same as of mirage_session_get_track_by_number().
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_get_track_by_number (MIRAGE_Disc *self, gint number, GObject **track, GError **error) {
    GObject *session = NULL;
    gboolean succeeded = TRUE;    

    /* We get session by track */
    if (!mirage_disc_get_session_by_track(self, number, &session, error)) {
        return FALSE;
    }
    
    /* And now we get the track */
    succeeded = mirage_session_get_track_by_number(MIRAGE_SESSION(session), number, track, error);
    g_object_unref(session);
    
    return succeeded;
}

/**
 * mirage_disc_get_track_by_address:
 * @self: a #MIRAGE_Disc
 * @address: address belonging to track to be retrieved
 * @track: location to store track, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track by address. @address must be valid (disc-relative) sector 
 * address that is part of the track to be retrieved (i.e. lying between track's 
 * start and end sector).
 * </para>
 *
 * <para>
 * The function attempts to find appropriate session using 
 * mirage_disc_get_session_by_address(), then retrieves the track using
 * mirage_session_get_track_by_address().
 * </para>
 *
 * <para>
 * The rest of behavior is same as of mirage_session_get_track_by_address().
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_get_track_by_address (MIRAGE_Disc *self, gint address, GObject **track, GError **error) {
    gboolean succeeded = FALSE;    
    GObject *session = NULL;
    
    /* We get session by sector */
    if (!mirage_disc_get_session_by_address(self, address, &session, error)) {
        return FALSE;
    }
    
    /* And now we get the track */
    succeeded = mirage_session_get_track_by_address(MIRAGE_SESSION(session), address, track, error);
    g_object_unref(session);
    
    return succeeded;
}


/**
 * mirage_disc_set_disc_structure:
 * @self: a #MIRAGE_Disc
 * @layer: disc layer
 * @type: disc structure type
 * @data: disc structure data to be set
 * @len: length of disc structure data
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets disc structure of type @type to layer @layer to disc. @data is buffer
 * containing disc structure data and @len is data length.
 * </para>
 *
 * <note>
 * Disc structures are valid only for DVD and BD discs; therefore, if disc type
 * is not set to %MIRAGE_MEDIUM_DVD or %MIRAGE_MEDIUM_BD prior to calling this
 * function, the function will fail and error will be set to %MIRAGE_E_INVALIDMEDIUM.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_set_disc_structure (MIRAGE_Disc *self, gint layer, gint type, guint8 *data, gint len, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    GValueArray *array = NULL;
    gint key = ((layer & 0x0000FFFF) << 16) | (type & 0x0000FFFF);
    
    if (_priv->medium_type != MIRAGE_MEDIUM_DVD && _priv->medium_type != MIRAGE_MEDIUM_BD) {
        mirage_error(MIRAGE_E_INVALIDMEDIUM, error);
        return FALSE;
    }
    
    /* We need to copy the data, and pack it together with its length... guess
       a value array is one of the ways to go... */
    array = g_value_array_new(2);
    
    array = g_value_array_append(array, NULL);
    g_value_init(g_value_array_get_nth(array, 0), G_TYPE_INT);
    g_value_set_int(g_value_array_get_nth(array, 0), len);

    array = g_value_array_append(array, NULL);
    g_value_init(g_value_array_get_nth(array, 1), G_TYPE_POINTER);
    g_value_set_pointer(g_value_array_get_nth(array, 1), g_memdup(data, len));
        
    g_hash_table_insert(_priv->disc_structures, GINT_TO_POINTER(key), array);
    
    return TRUE;
}

/**
 * mirage_disc_get_disc_structure:
 * @self: a #MIRAGE_Disc
 * @layer: disc layer
 * @type: disc structure type
 * @data: location to store buffer containing disc structure data, or %NULL
 * @len: location to store data length, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves disc structure of type @type from layer @layer. The buffer containing 
 * copy of disc structure is stored to @data; it should be freed with g_free() 
 * when no longer needed.
 * </para>
 *
 * <note>
 * Disc structures are valid only for DVD and BD discs; therefore, if disc type
 * is not set to %MIRAGE_MEDIUM_DVD or %MIRAGE_MEDIUM_BD prior to calling this
 * function, the function will fail and error will be set to %MIRAGE_E_INVALIDMEDIUM.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_get_disc_structure (MIRAGE_Disc *self, gint layer, gint type, guint8 **data, gint *len, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    gint key = ((layer & 0x0000FFFF) << 16) | (type & 0x0000FFFF);
    GValueArray *array = NULL;
    guint8 *tmp_data = NULL;
    gint tmp_len = 0;
    
    if (_priv->medium_type != MIRAGE_MEDIUM_DVD && _priv->medium_type != MIRAGE_MEDIUM_BD) {
        mirage_error(MIRAGE_E_INVALIDMEDIUM, error);
        return FALSE;
    }
    
    array = g_hash_table_lookup(_priv->disc_structures, GINT_TO_POINTER(key));
    
    if (!array) {
        /* Structure needs to be fabricated (if appropriate) */
        if (!__generate_disc_structure(self, layer, type, &tmp_data, &tmp_len, NULL)) {
            mirage_error(MIRAGE_E_DATANOTSET, error);
            return FALSE;
        }
    } else {
        /* Structure was provided by image */
        tmp_len = g_value_get_int(g_value_array_get_nth(array, 0));
        tmp_data = g_value_get_pointer(g_value_array_get_nth(array, 1));
    }
    
    if (data) {
        /* Return data to user if she wants it */
        *data = g_memdup(tmp_data, tmp_len);
    }
    if (len) {
        *len = tmp_len;
    }
    
    return TRUE;
}


/**
 * mirage_disc_get_sector:
 * @self: a #MIRAGE_Disc
 * @address: sector address
 * @sector: location to store sector
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves sector object representing sector at sector address @address.
 * </para>
 *
 * <para>
 * This function attempts to retrieve appropriate track using 
 * mirage_disc_get_track_by_address(),
 * then retrieves sector object using mirage_track_get_sector().
 * </para>
 * 
 * <para>
 * The rest of behavior is same as of mirage_disc_get_sector().
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_get_sector (MIRAGE_Disc *self, gint address, GObject **sector, GError **error) {
    /*MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);*/
    gboolean succeeded = FALSE;
    GObject *track = NULL;
    
    /* Fetch the right track */
    if (!mirage_disc_get_track_by_address(self, address, &track, error)) {
        return FALSE;
    }
        
    /* Get the sector */
    succeeded = mirage_track_get_sector(MIRAGE_TRACK(track), address, TRUE, sector, error);
    /* Unref track */
    g_object_unref(track);
    
    return succeeded;
}

/**
 * mirage_disc_read_sector:
 * @self: a #MIRAGE_Disc
 * @address: sector address
 * @main_sel: main channel selection flags
 * @subc_sel: subchannel selection flags
 * @ret_buf: buffer to write data into, or %NULL
 * @ret_len: location to store written data length, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Reads sector data from sector at address @address. The function attempts to 
 * retrieve appropriate track using mirage_disc_get_track_by_address(),
 * then reads sector data using mirage_track_read_sector().
 * </para>
 *
 * <para>
 * The rest of behavior is same as of mirage_track_read_sector().
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_read_sector (MIRAGE_Disc *self, gint address, guint8 main_sel, guint8 subc_sel, guint8 *ret_buf, gint *ret_len, GError **error) {
    /*MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);*/
    gboolean succeeded = FALSE;
    GObject *track = NULL;
    
    /* Fetch the right track */
    if (!mirage_disc_get_track_by_address(self, address, &track, error)) {
        return FALSE;
    }
    /* Read sector */
    succeeded = mirage_track_read_sector(MIRAGE_TRACK(track), address, TRUE, main_sel, subc_sel, ret_buf, ret_len, error);
    /* Unref track */
    g_object_unref(track);
    
    return succeeded;
}


/**
 * mirage_disc_set_dpm_data:
 * @self: a #MIRAGE_Disc
 * @start: DPM start sector
 * @resolution: DPM data resolution
 * @num_entries: number of DPM entries
 * @data: buffer containing DPM data
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets the DPM data for disc. If @num_entries is not positive, DPM data is reset.
 * @start is the address at which DPM data begins, @resolution is resolution of
 * DPM data and @num_entries is the number of DPM entries in buffer pointed to by
 * @data.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_set_dpm_data (MIRAGE_Disc *self, gint start, gint resolution, gint num_entries, guint32 *data, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    
    /* Free old DPM data */
    g_free(_priv->dpm_data);
    _priv->dpm_data = NULL;
    
    /* Set new DPM data */
    _priv->dpm_start = start;
    _priv->dpm_resolution = resolution;
    _priv->dpm_num_entries = num_entries;
    /* Allocate and copy data only if number of entries is positive (otherwise 
       the data is simply reset) */
    if (_priv->dpm_num_entries > 0) {
        _priv->dpm_data = g_new0(guint32, _priv->dpm_num_entries);
        memcpy(_priv->dpm_data, data, sizeof(guint32)*_priv->dpm_num_entries);
    }
    
    return TRUE;
}

/**
 * mirage_disc_get_dpm_data:
 * @self: a #MIRAGE_Disc
 * @start: location to store DPM start sector, or %NULL
 * @resolution: location to store DPM data resolution, or %NULL
 * @num_entries: location to store number of DPM entries, or %NULL
 * @data: location to store pointer to buffer containing DPM data, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves DPM data for disc. The pointer to object's private buffer containing
 * DPM data entries is stored in @data; as such it should not be freed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_get_dpm_data (MIRAGE_Disc *self, gint *start, gint *resolution, gint *num_entries, guint32 **data, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    
    /* Make sure DPM data has been set */
    if (!_priv->dpm_num_entries) {
        mirage_error(MIRAGE_E_DATANOTSET, error);
        return FALSE;
    }
    
    if (start) *start = _priv->dpm_start;
    if (resolution) *resolution = _priv->dpm_resolution;
    if (num_entries) *num_entries = _priv->dpm_num_entries;
    if (data) *data = _priv->dpm_data;
    
    return TRUE;
}

/**
 * mirage_disc_get_dpm_data_for_sector:
 * @self: a #MIRAGE_Disc
 * @address: address of sector to retrieve DPM data for
 * @angle: location to store sector angle, or %NULL
 * @density: location to store sector density, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves DPM data for sector at address @address. Two pieces of data can be
 * retrieved; first one is sector angle, expressed in rotations (i.e. 0.25 would
 * mean 1/4 of rotation or 90˚ and 1.0 means one full rotation or 360˚), and the
 * other one is sector density at given address, expressed in degrees per sector).
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_get_dpm_data_for_sector (MIRAGE_Disc *self, gint address, gdouble *angle, gdouble *density, GError **error) {
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    gint rel_address = 0;
    gint idx_bottom = 0;
    
    gdouble tmp_angle = 0;
    gdouble tmp_density = 0;
    
    if (!_priv->dpm_num_entries) {
        mirage_error(MIRAGE_E_DATANOTSET, error);
        return FALSE;
    }
    
    /* We'll operate with address relative to DPM data start sector */
    rel_address = address - _priv->dpm_start;
    
    /* Check if relative address is out of range (account for possibility of 
       sectors lying behind last DPM entry) */
    if (rel_address < 0 || rel_address >= (_priv->dpm_num_entries+1)*_priv->dpm_resolution) {
        mirage_error(MIRAGE_E_INVALIDARG, error);
        return FALSE;
    }
    
    /* Calculate index of DPM data entry belonging to the requested address */
    idx_bottom = rel_address/_priv->dpm_resolution;
            
    /* Three possibilities; in all three cases we calculate tmp_density as the
       difference between top and bottom angle, converted to rotations and 
       divided by resolution. Because our DPM data entries don't contain entry
       for address 0, but start with 1*dpm_resolution instead, we'll have to
       readjust bottom index... (actual entry index is bottom index minus 1) */
    if (idx_bottom == 0) {
        /* If bottom index is 0, we have address between 0 and 1*dpm_resolution;
           this means bottom angle is 0 and top angle is first DPM entry (with
           index 0, which equals idx_bottom). */
        tmp_density = _priv->dpm_data[idx_bottom];
    } else if (idx_bottom == _priv->dpm_num_entries) {
        /* Special case; we allow addresses past last DPM entry's address, but
           only as long as they don't get past the address that would belong to 
           next DPM entry. This is because resolution is not a factor of disc
           length and therefore some sectors might remain past last DPM entry.
           In this case, we use angles from previous interval. */
        tmp_density = (_priv->dpm_data[idx_bottom-1] - _priv->dpm_data[idx_bottom-2]);
    } else {
        /* Regular case; top angle minus bottom angle, where we need to decrease
           idx_bottom by one to account for index difference as described above */
        tmp_density = (_priv->dpm_data[idx_bottom] - _priv->dpm_data[idx_bottom-1]);
    }
    tmp_density /= 256.0; /* Convert hex degrees into rotations */
    tmp_density /= _priv->dpm_resolution; /* Rotations per sector */
    
    if (angle) {
        tmp_angle = (rel_address - idx_bottom*_priv->dpm_resolution)*tmp_density; /* Angle difference */
        /* Add base angle, but only if it's not 0 (which is the case when 
           idx_bottom is 0) */
        if (idx_bottom > 0) {
            tmp_angle += _priv->dpm_data[idx_bottom-1]/256.0; /* Add bottom angle */
        }
        
        *angle = tmp_angle;
    }
    
    if (density) {
        tmp_density *= 360; /* Degrees per sector */
        
        *density = tmp_density;
    }
        
    return TRUE;
}


/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static MIRAGE_ObjectClass *parent_class = NULL;

static void __mirage_disc_instance_init (GTypeInstance *instance, gpointer g_class) {
    MIRAGE_Disc *self = MIRAGE_DISC(instance);
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    
    /* Default layout values */
    _priv->start_sector = 0;
    _priv->first_session = 1;
    _priv->first_track  = 1;
    
    /* Create disc structures hash table */
    _priv->disc_structures = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, __free_disc_structure_data);
    
    return;
}

static void __mirage_disc_finalize (GObject *obj) {
    MIRAGE_Disc *self = MIRAGE_DISC(obj);
    MIRAGE_DiscPrivate *_priv = MIRAGE_DISC_GET_PRIVATE(self);
    GList *entry = NULL;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s:\n", __func__);
    
    /* Free list of sessions */
    G_LIST_FOR_EACH(entry, _priv->sessions_list) {
        if (entry->data) {
            GObject *session = entry->data;
            /* Disconnect signal handler and unref */
            g_signal_handlers_disconnect_by_func(MIRAGE_OBJECT(session), __session_modified_handler, self);
            g_object_unref(session);
        } else {
            MIRAGE_DEBUG(obj, MIRAGE_DEBUG_WARNING, "%s: session object = NULL!\n", __func__);
        }
    }
    g_list_free(_priv->sessions_list);
    
    g_strfreev(_priv->filenames);
    g_free(_priv->mcn);
    
    g_free(_priv->dpm_data);
    
    g_hash_table_destroy(_priv->disc_structures);
    
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __func__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __mirage_disc_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_DiscClass *klass = MIRAGE_DISC_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_DiscPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_disc_finalize;
        
    return;
}

GType mirage_disc_get_type (void) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_DiscClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_disc_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Disc),
            0,      /* n_preallocs */
            __mirage_disc_instance_init    /* instance_init */
        };
        
        type = g_type_register_static(MIRAGE_TYPE_OBJECT, "MIRAGE_Disc", &info, 0);
    }
    
    return type;
}
