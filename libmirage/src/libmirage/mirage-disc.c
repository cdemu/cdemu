/*
 *  libMirage: Disc object
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

#define __debug__ "Disc"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_DISC_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_DISC, MIRAGE_DiscPrivate))

struct _MIRAGE_DiscPrivate
{
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

    /* User-supplied properties */
    gboolean dvd_report_css; /* Whether to report that DVD image is CSS-encrypted or not */
};


/**********************************************************************\
 *                          Private functions                         *
\**********************************************************************/
static void mirage_disc_remove_session (MIRAGE_Disc *self, GObject *session);


static gboolean mirage_disc_check_for_encoded_mcn (MIRAGE_Disc *self)
{
    GObject *track = NULL;
    gint start_address = 0;
    gint num_tracks = mirage_disc_get_number_of_tracks(self);
    gint i;

    /* Go over all tracks, and find the first one with fragment that contains
       subchannel... */
    for (i = 0; i < num_tracks; i++) {
        track = mirage_disc_get_track_by_index(self, i, NULL);
        if (track) {
            GObject *fragment = mirage_track_find_fragment_with_subchannel(MIRAGE_TRACK(track), NULL);
            if (fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_DISC, "%s: track %i contains subchannel\n", __debug__, i);
                start_address = mirage_fragment_get_address(MIRAGE_FRAGMENT(fragment));
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
        gint cur_address;
        gint end_address = start_address + 100;

        self->priv->mcn_encoded = TRUE; /* It is, even if it may not be present... */
        /* Reset MCN */
        g_free(self->priv->mcn);
        self->priv->mcn = NULL;

        /* According to INF8090, MCN, if present, must be encoded in at least
           one sector in 100 consequtive sectors. So we read first hundred
           sectors' subchannel, and extract MCN if we find it. */
        for (cur_address = start_address; cur_address < end_address; cur_address++) {
            guint8 tmp_buf[16];

            if (!mirage_track_read_sector(MIRAGE_TRACK(track), cur_address, FALSE, 0, MIRAGE_SUBCHANNEL_PQ, tmp_buf, NULL, NULL)) {
                g_object_unref(track);
                return FALSE;
            }

            if ((tmp_buf[0] & 0x0F) == 0x02) {
                /* Mode-2 Q found */
                gchar tmp_mcn[13];

                mirage_helper_subchannel_q_decode_mcn(&tmp_buf[1], tmp_mcn);

                MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: found MCN: <%s>\n", __debug__, tmp_mcn);

                /* Set MCN */
                self->priv->mcn = g_strndup(tmp_mcn, 13);
            }
        }

        g_object_unref(track);
    }

    return TRUE;
}


static void mirage_disc_commit_topdown_change (MIRAGE_Disc *self)
{
    GList *entry;

    /* Rearrange sessions: set numbers, set first tracks, set start sectors */
    gint cur_session_address = self->priv->start_sector;
    gint cur_session_number = self->priv->first_session;
    gint cur_session_ftrack = self->priv->first_track;

    G_LIST_FOR_EACH(entry, self->priv->sessions_list) {
        GObject *session = entry->data;

        /* Set session's number */
        mirage_session_layout_set_session_number(MIRAGE_SESSION(session), cur_session_number);
        cur_session_number++;

        /* Set session's first track */
        mirage_session_layout_set_first_track(MIRAGE_SESSION(session), cur_session_ftrack);
        cur_session_ftrack += mirage_session_get_number_of_tracks(MIRAGE_SESSION(session));

        /* Set session's start address */
        mirage_session_layout_set_start_sector(MIRAGE_SESSION(session), cur_session_address);
        cur_session_address += mirage_session_layout_get_length(MIRAGE_SESSION(session));
    }
}

static void mirage_disc_commit_bottomup_change (MIRAGE_Disc *self)
{
    GList *entry;

    /* Calculate disc length and number of tracks */
    self->priv->length = 0; /* Reset; it'll be recalculated */
    self->priv->tracks_number = 0; /* Reset; it'll be recalculated */

    G_LIST_FOR_EACH(entry, self->priv->sessions_list) {
        GObject *session = entry->data;

        /* Disc length */
        self->priv->length += mirage_session_layout_get_length(MIRAGE_SESSION(session));

        /* Number of all tracks */
        self->priv->tracks_number += mirage_session_get_number_of_tracks(MIRAGE_SESSION(session));
    }

    /* Bottom-up change = eventual change in fragments, so MCN could've changed... */
    mirage_disc_check_for_encoded_mcn(self);

    /* Signal disc change */
    g_signal_emit_by_name(MIRAGE_OBJECT(self), "object-modified", NULL);
    /* Disc is where we complete the arc by committing top-down change */
    mirage_disc_commit_topdown_change(self);
}

static void mirage_disc_session_modified_handler (GObject *session, MIRAGE_Disc *self)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_DISC, "%s: start\n", __debug__);

    /* If session has been emptied, remove it (it'll do bottom-up change automatically);
       otherwise, signal bottom-up change */
    if (!mirage_session_get_number_of_tracks(MIRAGE_SESSION(session))) {
        mirage_disc_remove_session(self, session);
    } else {
        mirage_disc_commit_bottomup_change(self);
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_DISC, "%s: end\n", __debug__);
}

static void mirage_disc_remove_session (MIRAGE_Disc *self, GObject *session)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_DISC, "%s: start\n", __debug__);

    /* Disconnect signal handler (find it by handler function and user data) */
    g_signal_handlers_disconnect_by_func(MIRAGE_OBJECT(session), mirage_disc_session_modified_handler, self);

    /* Remove session from list and unref it */
    self->priv->sessions_list = g_list_remove(self->priv->sessions_list, session);
    g_object_unref(G_OBJECT(session));

    /* Bottom-up change */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_DISC, "%s: commiting bottom-up change\n", __debug__);
    mirage_disc_commit_bottomup_change(self);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_DISC, "%s: end\n", __debug__);
}


static gboolean mirage_disc_generate_disc_structure (MIRAGE_Disc *self, gint layer, gint type, guint8 **data, gint *len)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_DISC, "%s: start (layer: %d, type: 0x%X)\n", __debug__, layer, type);

    switch (type) {
        case 0x0000: {
            MIRAGE_DiscStruct_PhysInfo *phys_info = g_new0(MIRAGE_DiscStruct_PhysInfo, 1);

            gint disc_length = mirage_disc_layout_get_length(self);

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

            if (self->priv->dvd_report_css) {
                copy_info->copy_protection = 0x01; /* CSS/CPPM */
                copy_info->region_info = 0x00; /* Playable in all regions */
            } else {
                copy_info->copy_protection = 0x00;/* None */
                copy_info->region_info = 0x00; /* N/A */
            }

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


static gint sort_sessions_by_number (GObject *session1, GObject *session2)
{
    gint number1 = mirage_session_layout_get_session_number(MIRAGE_SESSION(session1));
    gint number2 = mirage_session_layout_get_session_number(MIRAGE_SESSION(session2));

    if (number1 < number2) {
        return -1;
    } else if (number1 > number2) {
        return 1;
    } else {
        return 0;
    }
}

static void free_disc_structure_data (GValueArray *array)
{
    /* Free data */
    gpointer data = g_value_get_pointer(g_value_array_get_nth(array, 1));
    g_free(data);

    /* Free array */
    g_value_array_free(array);
}


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
/**
 * mirage_disc_set_medium_type:
 * @self: a #MIRAGE_Disc
 * @medium_type: (in): medium type
 *
 * <para>
 * Sets medium type. @medium_type must be one of #MIRAGE_MediumTypes.
 * </para>
 *
 * <note>
 * Intended for internal use only.
 * </note>
 **/
void mirage_disc_set_medium_type (MIRAGE_Disc *self, gint medium_type)
{
    /* Set medium type */
    self->priv->medium_type = medium_type;
}

/**
 * mirage_disc_get_medium_type:
 * @self: a #MIRAGE_Disc
 *
 * <para>
 * Retrieves medium type.
 * </para>
 *
 * Returns: medium type
 **/
gint mirage_disc_get_medium_type (MIRAGE_Disc *self)
{
    /* Return medium type */
    return self->priv->medium_type;
}


/**
 * mirage_disc_set_filenames:
 * @self: a #MIRAGE_Disc
 * @filenames: (in) (array zero-terminated=1): %NULL-terminated array of filenames
 *
 * <para>
 * Sets image filename(s).
 * </para>
 *
 * <note>
 * Intended for internal use only, in image parser implementations.
 * </note>
 **/
void mirage_disc_set_filenames (MIRAGE_Disc *self, gchar **filenames)
{
    /* Free old filenames */
    g_strfreev(self->priv->filenames);
    /* Set filenames */
    self->priv->filenames = g_strdupv(filenames);
}

/**
 * mirage_disc_set_filename:
 * @self: a #MIRAGE_Disc
 * @filename: (in): filename
 *
 * <para>
 * Sets image filename. The functionality is similar to mirage_disc_set_filenames(),
 * except that only one filename is set. It is intended to be used in parsers which
 * support only single-file images.
 * </para>
 *
 * <note>
 * Intended for internal use only, in image parser implementations.
 * </note>
 **/
void mirage_disc_set_filename (MIRAGE_Disc *self, const gchar *filename)
{
    /* Free old filenames */
    g_strfreev(self->priv->filenames);
    /* Set filenames */
    self->priv->filenames = g_new0(gchar*, 2);
    self->priv->filenames[0] = g_strdup(filename);
}

/**
 * mirage_disc_get_filenames:
 * @self: a #MIRAGE_Disc
 *
 * <para>
 * Retrieves image filename(s).
 * </para>
 *
 * Returns: (transfer none) (array zero-terminated=1): pointer to %NULL-terminated
 * array of filenames. The array belongs to the object and should not be modified.
 **/
gchar **mirage_disc_get_filenames (MIRAGE_Disc *self)
{
    /* Return filenames */
    return self->priv->filenames;
}


/**
 * mirage_disc_set_mcn:
 * @self: a #MIRAGE_Disc
 * @mcn: (in): MCN
 *
 * <para>
 * Sets MCN (Media Catalogue Number).
 * </para>
 *
 * <para>
 * Because MCN is stored in subchannel data, this function silently
 * fails if any of disc's tracks contains fragments with subchannel
 * data provided.
 * </para>
 *
 * <note>
 * Intended for internal use only.
 * </note>
 **/
void mirage_disc_set_mcn (MIRAGE_Disc *self, const gchar *mcn)
{
    /* MCN can be set only if none of the tracks have fragments that contain
       subchannel; this is because MCN is encoded in the subchannel, and cannot
       be altered... */
    if (self->priv->mcn_encoded) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_DISC, "%s: MCN is already encoded in subchannel!\n", __debug__);
    } else {
        g_free(self->priv->mcn);
        self->priv->mcn = g_strndup(mcn, 13);
    }
}

/**
 * mirage_disc_get_mcn:
 * @self: a #MIRAGE_Disc
 *
 * <para>
 * Retrieves MCN.
 * </para>
 *
 * Returns: (transfer none): pointer to MCN string, or %NULL. The string
 * belongs to the object and should not be modified.
 **/
const gchar *mirage_disc_get_mcn (MIRAGE_Disc *self)
{
    /* Return pointer to MCN */
    return self->priv->mcn;
}


/**
 * mirage_disc_layout_set_first_session:
 * @self: a #MIRAGE_Disc
 * @first_session: (in): first session number
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
 **/
void mirage_disc_layout_set_first_session (MIRAGE_Disc *self, gint first_session)
{
    /* Set first session */
    self->priv->first_session = first_session;
    /* Top-down change */
    mirage_disc_commit_topdown_change(self);
}

/**
 * mirage_disc_layout_get_first_session:
 * @self: a #MIRAGE_Disc
 *
 * <para>
 * Retrieves session number of the first session in the disc layout.
 * </para>
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: first session number
 **/
gint mirage_disc_layout_get_first_session (MIRAGE_Disc *self)
{
    /* Return first session */
    return self->priv->first_session;
}

/**
 * mirage_disc_layout_set_first_track:
 * @self: a #MIRAGE_Disc
 * @first_track: (in): first track number
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
 **/
void mirage_disc_layout_set_first_track (MIRAGE_Disc *self, gint first_track)
{
    /* Set first track */
    self->priv->first_track = first_track;
    /* Top-down change */
    mirage_disc_commit_topdown_change(self);
}

/**
 * mirage_disc_layout_get_first_track:
 * @self: a #MIRAGE_Disc
 *
 * <para>
 * Retrieves track number of the first track in the disc layout.
 * </para>
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: first track number
 **/
gint mirage_disc_layout_get_first_track (MIRAGE_Disc *self)
{
    /* Return first track */
    return self->priv->first_track;
}

/**
 * mirage_disc_layout_set_start_sector:
 * @self: a #MIRAGE_Disc
 * @start_sector: (in): start sector
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
 **/
void mirage_disc_layout_set_start_sector (MIRAGE_Disc *self, gint start_sector)
{
    /* Set start sector */
    self->priv->start_sector = start_sector;
    /* Top-down change */
    mirage_disc_commit_topdown_change(self);
}

/**
 * mirage_disc_layout_get_start_sector:
 * @self: a #MIRAGE_Disc
 *
 * <para>
 * Retrieves start sector of the disc layout.
 * </para>
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: start sector
 **/
gint mirage_disc_layout_get_start_sector (MIRAGE_Disc *self)
{
    /* Return start sector */
    return self->priv->start_sector;
}

/**
 * mirage_disc_layout_get_length:
 * @self: a #MIRAGE_Disc
 *
 * <para>
 * Retrieves length of the disc layout. The returned length is given in sectors.
 * </para>
 *
 * Returns: disc layout length
 **/
gint mirage_disc_layout_get_length (MIRAGE_Disc *self)
{
    /* Return length */
    return self->priv->length;
}


/**
 * mirage_disc_get_number_of_sessions:
 * @self: a #MIRAGE_Disc
 *
 * <para>
 * Retrieves number of sessions in the disc layout.
 * </para>
 *
 * Returns: number of sessions
 **/
gboolean mirage_disc_get_number_of_sessions (MIRAGE_Disc *self)
{
    /* Return number of sessions */
    return g_list_length(self->priv->sessions_list); /* Length of list */
}

/**
 * mirage_disc_add_session_by_index:
 * @self: a #MIRAGE_Disc
 * @index: (in): index at which session should be added
 * @session: (in) (transfer full): a #MIRAGE_Session to be added
 *
 * <para>
 * Adds session to disc layout.
 * </para>
 *
 * <para>
 * @index is the index at which session is added. Negative index denotes
 * index going backwards (i.e. -1 adds session at the end, -2 adds session
 * second-to-last, etc.). If index, either negative or positive, is too big,
 * session is added at the beginning or at the end of the layout, respectively.
 * </para>
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 **/
void mirage_disc_add_session_by_index (MIRAGE_Disc *self, gint index, GObject *session)
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
    mirage_object_set_parent(MIRAGE_OBJECT(session), G_OBJECT(self));
    /* Attach child */
    mirage_object_attach_child(MIRAGE_OBJECT(self), session);

    /* Insert session into sessions list */
    self->priv->sessions_list = g_list_insert(self->priv->sessions_list, session, index);

    /* Connect session modified signal */
    g_signal_connect(MIRAGE_OBJECT(session), "object-modified", (GCallback)mirage_disc_session_modified_handler, self);

    /* Bottom-up change */
    mirage_disc_commit_bottomup_change(self);
}

/**
 * mirage_disc_add_session_by_number:
 * @self: a #MIRAGE_Disc
 * @number: (in): session number for the added session
 * @session: (in) (transfer full): a #MIRAGE_Session to be added
 * @error: (out) (allow-none): location to store error, or %NULL
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
 * <note>
 * Causes bottom-up change.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_add_session_by_number (MIRAGE_Disc *self, gint number, GObject *session, GError **error)
{
    GObject *tmp_session;

    /* Check if session with that number already exists */
    tmp_session = mirage_disc_get_session_by_number(self, number, NULL);
    if (tmp_session) {
        g_object_unref(tmp_session);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, "Session with number %d already exists!", number);
        return FALSE;
    }

    /* Increment reference counter */
    g_object_ref(session);
    /* Set session number */
    mirage_session_layout_set_session_number(MIRAGE_SESSION(session), number);
    /* Set parent */
    mirage_object_set_parent(MIRAGE_OBJECT(session), G_OBJECT(self));
    /* Attach child */
    mirage_object_attach_child(MIRAGE_OBJECT(self), session);

    /* Insert session into sessions list */
    self->priv->sessions_list = g_list_insert_sorted(self->priv->sessions_list, session, (GCompareFunc)sort_sessions_by_number);

    /* Connect session modified signal */
    g_signal_connect(MIRAGE_OBJECT(session), "object-modified", (GCallback)mirage_disc_session_modified_handler, self);

    /* Bottom-up change */
    mirage_disc_commit_bottomup_change(self);

    return TRUE;
}

/**
 * mirage_disc_remove_session_by_index:
 * @self: a #MIRAGE_Disc
 * @index: (in): index of session to be removed
 * @error: (out) (allow-none): location to store error, or %NULL
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
gboolean mirage_disc_remove_session_by_index (MIRAGE_Disc *self, gint index, GError **error)
{
    /* Find session by index */
    GObject *session = mirage_disc_get_session_by_index(self, index, error);
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
 * @self: a #MIRAGE_Disc
 * @number: (in): session number of session to be removed
 * @error: (out) (allow-none): location to store error, or %NULL
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
gboolean mirage_disc_remove_session_by_number (MIRAGE_Disc *self, gint number, GError **error)
{
    /* Find session by number */
    GObject *session = mirage_disc_get_session_by_number(self, number, error);
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
 * @self: a #MIRAGE_Disc
 * @session: (in): session object to be removed
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
 **/
void mirage_disc_remove_session_by_object (MIRAGE_Disc *self, GObject *session)
{
    mirage_disc_remove_session(self, session);
}


/**
 * mirage_disc_get_session_by_index:
 * @self: a #MIRAGE_Disc
 * @index: (in): index of session to be retrieved
 * @error: (out) (allow-none):location to store error, or %NULL
 *
 * <para>
 * Retrieves session by index. If @index is negative, sessions from the end of
 * layout are retrieved (e.g. -1 is for last session, -2 for second-to-last
 * session, etc.). If @index is out of range, regardless of the sign, the
 * function fails.
 * </para>
 *
 * Returns: (transfer full): a #MIRAGE_Session on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_disc_get_session_by_index (MIRAGE_Disc *self, gint index, GError **error)
{
    GObject *session;
    gint num_sessions;

    /* First session, last session... allow negative indexes to go from behind */
    num_sessions = mirage_disc_get_number_of_sessions(self);
    if (index < -num_sessions || index >= num_sessions) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, "Session index %d out of range!", index);
        return NULL;
    } else if (index < 0) {
        index += num_sessions;
    }

    /* Get index-th item from list... */
    session = g_list_nth_data(self->priv->sessions_list, index);

    if (!session) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, "Session with index %d not found!", index);
        return NULL;
    }

    g_object_ref(session);
    return session;
}

/**
 * mirage_disc_get_session_by_number:
 * @self: a #MIRAGE_Disc
 * @number: (in): number of session to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves session by session number.
 * </para>
 *
 * Returns: (transfer full): a #MIRAGE_Session on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_disc_get_session_by_number (MIRAGE_Disc *self, gint session_number, GError **error)
{
    GObject *session;
    GList *entry ;

    /* Go over all sessions */
    session = NULL;
    G_LIST_FOR_EACH(entry, self->priv->sessions_list) {
        session = entry->data;

        /* Break the loop if number matches */
        if (session_number == mirage_session_layout_get_session_number(MIRAGE_SESSION(session))) {
            break;
        } else {
            session = NULL;
        }
    }

    /* If we didn't find anything... */
    if (!session) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, "Session with number %d not found!", session_number);
        return FALSE;
    }

    g_object_ref(session);
    return session;
}

/**
 * mirage_disc_get_session_by_address:
 * @self: a #MIRAGE_Disc
 * @address: (in) address belonging to session to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves session by address. @address must be valid (disc-relative) sector
 * address that is part of the session to be retrieved (i.e. lying between session's
 * start and end sector).
 * </para>
 *
 * Returns: (transfer full): a #MIRAGE_Session on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_disc_get_session_by_address (MIRAGE_Disc *self, gint address, GError **error)
{
    GObject *session;
    GList *entry;

    if ((address < self->priv->start_sector) || (address >= self->priv->start_sector + self->priv->length)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, "Session address %d (0x%X) out of range!", address, address);
        return FALSE;
    }

    /* Go over all sessions */
    session = NULL;
    G_LIST_FOR_EACH(entry, self->priv->sessions_list) {
        gint start_sector;
        gint length;

        session = entry->data;

        start_sector = mirage_session_layout_get_start_sector(MIRAGE_SESSION(session));
        length = mirage_session_layout_get_length(MIRAGE_SESSION(session));

        /* Break the loop if address lies within session boundaries */
        if (address >= start_sector && address < start_sector + length) {
            break;
        } else {
            session = NULL;
        }
    }

    /* If we didn't find anything... */
    if (!session) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, "Session containing address %d not found!", address);
        return FALSE;
    }

    g_object_ref(session);
    return session;
}

/**
 * mirage_disc_get_session_by_track:
 * @self: a #MIRAGE_Disc
 * @track: (in): number of track belonging to session to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves session by track number. @track must be valid track number of track
 * that is part of the session.
 * </para>
 *
 * Returns: (transfer full): a #MIRAGE_Session on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_disc_get_session_by_track (MIRAGE_Disc *self, gint track_number, GError **error)
{
    GObject *session;
    GList *entry;

    /* Go over all sessions */
    session = NULL;
    G_LIST_FOR_EACH(entry, self->priv->sessions_list) {
        gint first_track;
        gint num_tracks;

        session = entry->data;

        first_track = mirage_session_layout_get_first_track(MIRAGE_SESSION(session));
        num_tracks = mirage_session_get_number_of_tracks(MIRAGE_SESSION(session));

        /* Break the loop if track with that number is part of the session */
        if (track_number >= first_track && track_number < first_track + num_tracks) {
            break;
        } else {
            session = NULL;
        }
    }

    /* If we didn't find anything... */
    if (!session) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, "Session with track %d not found!", track_number);
        return FALSE;
    }

    g_object_ref(session);
    return session;
}

/**
 * mirage_disc_for_each_session:
 * @self: a #MIRAGE_Disc
 * @func: (in) (closure closure): callback function
 * @user_data: (in) (closure): data to be passed to callback function
 *
 * <para>
 * Iterates over sessions list, calling @func for each session in the layout.
 * </para>
 *
 * <para>
 * If @func returns %FALSE, the function immediately returns %FALSE.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_for_each_session (MIRAGE_Disc *self, MIRAGE_CallbackFunction func, gpointer user_data)
{
    GList *entry;

    G_LIST_FOR_EACH(entry, self->priv->sessions_list) {
        gboolean succeeded = (*func) (MIRAGE_SESSION(entry->data), user_data);
        if (!succeeded) {
            return FALSE;
        }
    }

    return TRUE;
}

/**
 * mirage_disc_get_session_before:
 * @self: a #MIRAGE_Disc
 * @session: (in): a session
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves session that comes before @session.
 * </para>
 *
 * Returns: (transfer full): a #MIRAGE_Session on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_disc_get_session_before (MIRAGE_Disc *self, GObject *session, GError **error)
{
    gint index;

    /* Get index of given session in the list */
    index = g_list_index(self->priv->sessions_list, session);
    if (index == -1) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, "Session %p is not in disc layout!", session);
        return NULL;
    }

    /* Now check if we didn't pass the first session (index = 0) and return previous one */
    if (index > 0) {
        return mirage_disc_get_session_by_index(self, index - 1, error);
    }

    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, "Session before session %p not found!", session);
    return NULL;
}

/**
 * mirage_disc_get_session_after:
 * @self: a #MIRAGE_Disc
 * @session: (in): a session
 *
 * <para>
 * Retrieves session that comes after @session.
 * </para>
 *
 * Returns: (transfer full): a #MIRAGE_Session on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_disc_get_session_after (MIRAGE_Disc *self, GObject *session, GError **error)
{
    gint num_sessions, index;

    /* Get index of given session in the list */
    index = g_list_index(self->priv->sessions_list, session);
    if (index == -1) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, "Session %p is not in disc layout!", session);
        return FALSE;
    }

    /* Now check if we didn't pass the last session (index = num_sessions - 1) and return previous one */
    num_sessions = mirage_disc_get_number_of_sessions(self);
    if (index < num_sessions - 1) {
        return mirage_disc_get_session_by_index(self, index + 1, error);
    }

    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, "Session after session %p not found!", session);
    return FALSE;
}


/**
 * mirage_disc_get_number_of_tracks:
 * @self: a #MIRAGE_Disc
 *
 * <para>
 * Retrieves number of tracks in the disc layout.
 * </para>
 *
 * Returns: number of tracks
 **/
gint mirage_disc_get_number_of_tracks (MIRAGE_Disc *self)
{
    /* Return number of tracks */
    return self->priv->tracks_number;
}

/**
 * mirage_disc_add_track_by_index:
 * @self: a #MIRAGE_Disc
 * @index: (in): index at which track should be added
 * @track: (in) (transfer full): a #MIRAGE_Track to be added
 * @error: (out) (allow-none): location to store error, or %NULL
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
gboolean mirage_disc_add_track_by_index (MIRAGE_Disc *self, gint index, GObject *track, GError **error)
{
    GList *entry;
    gint num_tracks;
    gint count;

    /* If disc layout is empty (if there are no sessions), we should create
       a session... and then track will be added to this one */
    if (!mirage_disc_get_number_of_sessions(self)) {
        mirage_disc_add_session_by_index(self, 0, NULL);
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
    G_LIST_FOR_EACH(entry, self->priv->sessions_list) {
        GObject *session = entry->data;

        num_tracks = mirage_session_get_number_of_tracks(MIRAGE_SESSION(session));

        if (index >= count && index <= count + num_tracks) {
            /* We got the session */
            mirage_session_add_track_by_index(MIRAGE_SESSION(session), index - count, track);
            return TRUE;
        }

        count += num_tracks;
    }

    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, "Session not found!");
    return FALSE;
}

/**
 * mirage_disc_add_track_by_number:
 * @self: a #MIRAGE_Disc
 * @number: (in): track number for the added track
 * @track: (in) (transfer full): a #MIRAGE_Track to be added
 * @error: (out) (allow-none): location to store error, or %NULL
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
gboolean mirage_disc_add_track_by_number (MIRAGE_Disc *self, gint number, GObject *track, GError **error)
{
    GObject *session;
    GObject *last_track;
    gboolean succeeded;
    gint last_number;

    /* Get number of last track */
    last_track = mirage_disc_get_track_by_index(self, -1, NULL);
    if (last_track) {
        last_number = mirage_track_layout_get_track_number(MIRAGE_TRACK(last_track));
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
    succeeded = mirage_session_add_track_by_number(MIRAGE_SESSION(session), number, track, error);

    g_object_unref(session);

    return succeeded;
}

/**
 * mirage_disc_remove_track_by_index:
 * @self: a #MIRAGE_Disc
 * @index: (in): index of track to be removed
 * @error: (out) (allow-none): location to store error, or %NULL
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
gboolean mirage_disc_remove_track_by_index (MIRAGE_Disc *self, gint index, GError **error)
{
    GObject *session;
    GObject *track;

    /* Get track directly */
    track = mirage_disc_get_track_by_index(self, index, error);
    if (!track) {
        return FALSE;
    }
    /* Get track's parent */
    session = mirage_object_get_parent(MIRAGE_OBJECT(track));
    if (!session) {
        g_object_unref(track);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, "Track has no parent!");
        return FALSE;
    }
    /* Remove track from parent */
    mirage_session_remove_track_by_object(MIRAGE_SESSION(session), track);

    g_object_unref(track);
    g_object_unref(session);

    return TRUE;
}

/**
 * mirage_disc_remove_track_by_number:
 * @self: a #MIRAGE_Disc
 * @number: (in): track number of track to be removed
 * @error: (out) (allow-none): location to store error, or %NULL
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
gboolean mirage_disc_remove_track_by_number (MIRAGE_Disc *self, gint number, GError **error)
{
    GObject *session;
    GObject *track;

    /* Protect against removing lead-in and lead-out */
    if (number == MIRAGE_TRACK_LEADIN || number == MIRAGE_TRACK_LEADOUT) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, "Invalid track number %d!", number);
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
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, "Track has no parent!");
        return FALSE;
    }
    /* Remove track from parent */
    mirage_session_remove_track_by_object(MIRAGE_SESSION(session), track);

    g_object_unref(track);
    g_object_unref(session);

    return TRUE;
}

/**
 * mirage_disc_get_track_by_index:
 * @self: a #MIRAGE_Disc
 * @index: (in): index of track to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
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
 * Returns: (transfer full): a #MIRAGE_Track on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_disc_get_track_by_index (MIRAGE_Disc *self, gint index, GError **error)
{
    GList *entry;
    gint num_tracks;
    gint count;

    /* First track, last track... allow negative indexes to go from behind */
    num_tracks = mirage_disc_get_number_of_tracks(self);
    if (index < -num_tracks || index >= num_tracks) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, "Track index %d out of range!", index);
        return NULL;
    } else if (index < 0) {
        index += num_tracks;
    }

    /* Loop over the sessions */
    count = 0;
    G_LIST_FOR_EACH(entry, self->priv->sessions_list) {
        GObject *session = entry->data;

        num_tracks = mirage_session_get_number_of_tracks(MIRAGE_SESSION(session));

        if (index >= count && index < count + num_tracks) {
            /* We got the session */
            return mirage_session_get_track_by_index(MIRAGE_SESSION(session), index - count, error);
        }

        count += num_tracks;
    }

    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, "Track with index %d not found!", index);
    return NULL;
}

/**
 * mirage_disc_get_track_by_number:
 * @self: a #MIRAGE_Disc
 * @number: (in): track number of track to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
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
 * Returns: (transfer full): a #MIRAGE_Track on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_disc_get_track_by_number (MIRAGE_Disc *self, gint number, GError **error)
{
    GObject *session;
    GObject *track;

    /* We get session by track */
    session = mirage_disc_get_session_by_track(self, number, error);
    if (!session) {
        return NULL;
    }

    /* And now we get the track */
    track = mirage_session_get_track_by_number(MIRAGE_SESSION(session), number, error);
    g_object_unref(session);

    return track;
}

/**
 * mirage_disc_get_track_by_address:
 * @self: a #MIRAGE_Disc
 * @address: (in): address belonging to track to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
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
 * Returns: (transfer full): a #MIRAGE_Track on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_disc_get_track_by_address (MIRAGE_Disc *self, gint address, GError **error)
{
    GObject *session;
    GObject *track;

    /* We get session by sector */
    session = mirage_disc_get_session_by_address(self, address, error);
    if (!session) {
        return FALSE;
    }

    /* And now we get the track */
    track = mirage_session_get_track_by_address(MIRAGE_SESSION(session), address, error);
    g_object_unref(session);

    return track;
}


/**
 * mirage_disc_set_disc_structure:
 * @self: a #MIRAGE_Disc
 * @layer: (in): disc layer
 * @type: (in): disc structure type
 * @data: (in): disc structure data to be set
 * @len: (in): length of disc structure data
 *
 * <para>
 * Sets disc structure of type @type to layer @layer to disc. @data is buffer
 * containing disc structure data and @len is data length.
 * </para>
 *
 * <note>
 * Disc structures are valid only for DVD and BD discs. This function
 * silently fails on invalid disc types.
 * </note>
 **/
void mirage_disc_set_disc_structure (MIRAGE_Disc *self, gint layer, gint type, const guint8 *data, gint len)
{
    GValueArray *array;
    gint key = ((layer & 0x0000FFFF) << 16) | (type & 0x0000FFFF);

    if (self->priv->medium_type != MIRAGE_MEDIUM_DVD && self->priv->medium_type != MIRAGE_MEDIUM_BD) {
        return;
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

    g_hash_table_insert(self->priv->disc_structures, GINT_TO_POINTER(key), array);
}

/**
 * mirage_disc_get_disc_structure:
 * @self: a #MIRAGE_Disc
 * @layer: (in): disc layer
 * @type: (in): disc structure type
 * @data: (out) (transfer none) (allow-none): location to store buffer containing disc structure data, or %NULL
 * @len: (out) (allow-none): location to store data length, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves disc structure of type @type from layer @layer. The pointer to buffer
 * containing the disc structure is stored in @data; the buffer belongs to the
 * object and therefore should not be modified.
 * </para>
 *
 * <note>
 * Disc structures are valid only for DVD and BD discs; therefore, if disc type
 * is not set to %MIRAGE_MEDIUM_DVD or %MIRAGE_MEDIUM_BD prior to calling this
 * function, the function will fail.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_disc_get_disc_structure (MIRAGE_Disc *self, gint layer, gint type, const guint8 **data, gint *len, GError **error)
{
    gint key = ((layer & 0x0000FFFF) << 16) | (type & 0x0000FFFF);
    GValueArray *array;
    guint8 *tmp_data;
    gint tmp_len;

    if (self->priv->medium_type != MIRAGE_MEDIUM_DVD && self->priv->medium_type != MIRAGE_MEDIUM_BD) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, "Invalid medium type!");
        return FALSE;
    }

    array = g_hash_table_lookup(self->priv->disc_structures, GINT_TO_POINTER(key));

    if (!array) {
        /* Structure needs to be fabricated (if appropriate) */
        if (!mirage_disc_generate_disc_structure(self, layer, type, &tmp_data, &tmp_len)) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, "Failed to generate fake disc structure data!");
            return FALSE;
        }
    } else {
        /* Structure was provided by image */
        tmp_len = g_value_get_int(g_value_array_get_nth(array, 0));
        tmp_data = g_value_get_pointer(g_value_array_get_nth(array, 1));
    }

    if (data) {
        /* Return data to user if she wants it */
        *data = tmp_data;
    }
    if (len) {
        *len = tmp_len;
    }

    return TRUE;
}


/**
 * mirage_disc_get_sector:
 * @self: a #MIRAGE_Disc
 * @address: (in): sector address
 * @error: (out) (allow-none): location to store error, or %NULL
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
 * Returns: (transfer full): sector object on success, %NULL on failure
 **/
GObject *mirage_disc_get_sector (MIRAGE_Disc *self, gint address, GError **error)
{
    GObject *track, *sector;

    /* Fetch the right track */
    track = mirage_disc_get_track_by_address(self, address, error);
    if (!track) {
        return FALSE;
    }

    /* Get the sector */
    sector = mirage_track_get_sector(MIRAGE_TRACK(track), address, TRUE, error);
    /* Unref track */
    g_object_unref(track);

    return sector;
}

/**
 * mirage_disc_read_sector:
 * @self: a #MIRAGE_Disc
 * @address: (in): sector address
 * @main_sel: (in): main channel selection flags
 * @subc_sel: (in): subchannel selection flags
 * @ret_buf: (out caller-allocates) (allow-none): buffer to write data into, or %NULL
 * @ret_len: (out) (allow-none): location to store written data length, or %NULL
 * @error: (out) (allow-none): location to store error, or %NULL
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
gboolean mirage_disc_read_sector (MIRAGE_Disc *self, gint address, guint8 main_sel, guint8 subc_sel, guint8 *ret_buf, gint *ret_len, GError **error)
{
    gboolean succeeded;
    GObject *track;

    /* Fetch the right track */
    track = mirage_disc_get_track_by_address(self, address, error);
    if (!track) {
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
 * @start: (in): DPM start sector
 * @resolution: (in): DPM data resolution
 * @num_entries: (in): number of DPM entries
 * @data: buffer (in): containing DPM data
 *
 * <para>
 * Sets the DPM data for disc. If @num_entries is not positive, DPM data is reset.
 * @start is the address at which DPM data begins, @resolution is resolution of
 * DPM data and @num_entries is the number of DPM entries in buffer pointed to by
 * @data.
 * </para>
 **/
void mirage_disc_set_dpm_data (MIRAGE_Disc *self, gint start, gint resolution, gint num_entries, const guint32 *data)
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
 * @self: a #MIRAGE_Disc
 * @start: (out) (allow-none): location to store DPM start sector, or %NULL
 * @resolution: (out) (allow-none): location to store DPM data resolution, or %NULL
 * @num_entries: (out) (allow-none): location to store number of DPM entries, or %NULL
 * @data: (out) (allow-none): location to store pointer to buffer containing DPM data, or %NULL
 *
 * <para>
 * Retrieves DPM data for disc. The pointer to buffer containing DPM data entries
 * is stored in @data; the buffer belongs to object and therefore should not be
 * modified.
 * </para>
 **/
void mirage_disc_get_dpm_data (MIRAGE_Disc *self, gint *start, gint *resolution, gint *num_entries, const guint32 **data)
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
 * @self: a #MIRAGE_Disc
 * @address: (in): address of sector to retrieve DPM data for
 * @angle: (out) (allow-none): location to store sector angle, or %NULL
 * @density: (out) (allow-none): location to store sector density, or %NULL
 * @error: (out) (allow-none): location to store error, or %NULL
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
gboolean mirage_disc_get_dpm_data_for_sector (MIRAGE_Disc *self, gint address, gdouble *angle, gdouble *density, GError **error)
{
    gint rel_address;
    gint idx_bottom;

    gdouble tmp_angle, tmp_density;

    if (!self->priv->dpm_num_entries) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, "DPM data not available!");
        return FALSE;
    }

    /* We'll operate with address relative to DPM data start sector */
    rel_address = address - self->priv->dpm_start;

    /* Check if relative address is out of range (account for possibility of
       sectors lying behind last DPM entry) */
    if (rel_address < 0 || rel_address >= (self->priv->dpm_num_entries+1)*self->priv->dpm_resolution) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DISC_ERROR, "Sector addreess %d out of range!", address);
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
G_DEFINE_TYPE(MIRAGE_Disc, mirage_disc, MIRAGE_TYPE_OBJECT);


static void mirage_disc_init (MIRAGE_Disc *self)
{
    self->priv = MIRAGE_DISC_GET_PRIVATE(self);

    self->priv->sessions_list = NULL;

    self->priv->filenames = NULL;
    self->priv->mcn = NULL;

    self->priv->dpm_data = NULL;

    /* Default layout values */
    self->priv->start_sector = 0;
    self->priv->first_session = 1;
    self->priv->first_track  = 1;

    /* Create disc structures hash table */
    self->priv->disc_structures = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)free_disc_structure_data);

    /* Default values for user-supplied properties */
    self->priv->dvd_report_css = FALSE;
}

static void mirage_disc_dispose (GObject *gobject)
{
    MIRAGE_Disc *self = MIRAGE_DISC(gobject);
    GList *entry;

    /* Unref sessions */
    G_LIST_FOR_EACH(entry, self->priv->sessions_list) {
        if (entry->data) {
            GObject *session = entry->data;
            /* Disconnect signal handler and unref */
            g_signal_handlers_disconnect_by_func(MIRAGE_OBJECT(session), mirage_disc_session_modified_handler, self);
            g_object_unref(session);

            entry->data = NULL;
        }
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_disc_parent_class)->dispose(gobject);
}

static void mirage_disc_finalize (GObject *gobject)
{
    MIRAGE_Disc *self = MIRAGE_DISC(gobject);

    g_list_free(self->priv->sessions_list);

    g_strfreev(self->priv->filenames);
    g_free(self->priv->mcn);

    g_free(self->priv->dpm_data);

    g_hash_table_destroy(self->priv->disc_structures);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_disc_parent_class)->finalize(gobject);
}

static void mirage_disc_set_property (GObject *gobject, guint property_id, const GValue *value, GParamSpec *pspec)
{
    MIRAGE_Disc *self = MIRAGE_DISC(gobject);

    switch (property_id) {
        case PROP_MIRAGE_DISC_DVD_REPORT_CSS: {
            self->priv->dvd_report_css = g_value_get_boolean(value);
            return;
        }
    }

    return G_OBJECT_CLASS(mirage_disc_parent_class)->set_property(gobject, property_id, value, pspec);
}

static void mirage_disc_get_property (GObject *gobject, guint property_id, GValue *value, GParamSpec *pspec)
{
    MIRAGE_Disc *self = MIRAGE_DISC(gobject);

    switch (property_id) {
        case PROP_MIRAGE_DISC_DVD_REPORT_CSS: {
            g_value_set_boolean(value, self->priv->dvd_report_css);
            return;
        }
    }

    return G_OBJECT_CLASS(mirage_disc_parent_class)->get_property(gobject, property_id, value, pspec);
}

static void mirage_disc_class_init (MIRAGE_DiscClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = mirage_disc_dispose;
    gobject_class->finalize = mirage_disc_finalize;
    gobject_class->get_property = mirage_disc_get_property;
    gobject_class->set_property = mirage_disc_set_property;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_DiscPrivate));

    /* Property: PROP_MIRAGE_DISC_DVD_REPORT_CSS */
    /**
    * MIRAGE_Disc:dvd-report-css:
    *
    * Flag controlling whether the generated Disc Structure 0x01 should
    * report that DVD is CSS-encrypted or not. Relevant only for DVD images;
    * and as it controls the generation of fake Disc Structures, it affects
    * only DVD images that do not provide Disc Structure information.
    **/
    GParamSpec *pspec = g_param_spec_boolean("dvd-report-css", "DVD Report CSS flag", "Set/Get DVD Report CSS flag", FALSE, G_PARAM_READWRITE);
    g_object_class_install_property(gobject_class, PROP_MIRAGE_DISC_DVD_REPORT_CSS, pspec);
}
