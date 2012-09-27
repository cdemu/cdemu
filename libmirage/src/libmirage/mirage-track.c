/*
 *  libMirage: Track object
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

#define __debug__ "Track"


/**********************************************************************\
 *                         Private structure                          *
\**********************************************************************/
#define MIRAGE_TRACK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_TRACK, MirageTrackPrivate))

struct _MirageTrackPrivate
{
    /* Layout settings */
    gint track_number; /* Track number */
    gint start_sector; /* Start sector (where pregap starts)... disc/session-relative address */
    gint length;       /* Length of track (sum of fragments' length) */

    gint track_start; /* Track start sector (where index changes to 1)... track-relative address */

    /* Track mode and flags */
    gint flags; /* Track flags */
    MirageTrackModes mode;  /* Track mode */

    gchar *isrc; /* ISRC */
    gboolean isrc_encoded; /* Is ISRC encoded in one of track's fragment's subchannel? */

    /* List of index changes (indexes > 1) */
    GList *indices_list;

    /* List of data fragments */
    GList *fragments_list;

    /* CD-Text list */
    GList *languages_list;
};


/**********************************************************************\
 *                          Private functions                         *
\**********************************************************************/
static gboolean mirage_track_check_for_encoded_isrc (MirageTrack *self)
{
    /* Check if we have fragment with subchannel */
    GObject *fragment = mirage_track_find_fragment_with_subchannel(self, NULL);

    if (fragment) {
        gint start_address, end_address;

        /* According to INF8090, ISRC, if present, must be encoded in at least
           one sector in 100 consequtive sectors. So we read first hundred
           sectors' subchannel, and extract ISRC if we find it. */
        start_address = mirage_fragment_get_address(MIRAGE_FRAGMENT(fragment));
        end_address = start_address + 100;

        g_object_unref(fragment);

        self->priv->isrc_encoded = TRUE; /* It is, even if it may not be present... */
        /* Reset ISRC */
        g_free(self->priv->isrc);
        self->priv->isrc = NULL;

        for (gint address = start_address; address < end_address; address++) {
            GObject *sector;
            const guint8 *buf;
            gint buflen;

            /* Get sector */
            sector = mirage_track_get_sector(self, address, FALSE, NULL);
            if (!sector) {
                continue;
            }

            /* Get PQ subchannel */
            if (!mirage_sector_get_subchannel(MIRAGE_SECTOR(sector), MIRAGE_SUBCHANNEL_PQ, &buf, &buflen, NULL)) {
                g_object_unref(sector);
                continue;
            }

            if ((buf[0] & 0x0F) == 0x03) {
                /* Mode-3 Q found */
                gchar tmp_isrc[12];

                mirage_helper_subchannel_q_decode_isrc(&buf[1], tmp_isrc);
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: found ISRC: <%s>\n", __debug__, tmp_isrc);

                /* Set ISRC */
                self->priv->isrc = g_strndup(tmp_isrc, 12);
            }

            g_object_unref(sector);
        }
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: no fragments with subchannel found\n", __debug__);
        self->priv->isrc_encoded = FALSE;
    }

    return TRUE;
}


static void mirage_track_rearrange_indices (MirageTrack *self)
{
    GList *entry;

    /* Rearrange indices: set their numbers */
    /* Indices numbers start with 2 (0 and 1 are controlled via
       get/set_track_start... and while we're at it, if index lies before
       track start, remove it (it most likely means track start got changed
       after indices were added) */
    gint cur_index = 2;
    g_assert(self->priv->indices_list != NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: rearranging indices (%d indices found)...\n", __debug__, g_list_length(self->priv->indices_list));
    G_LIST_FOR_EACH(entry, self->priv->indices_list) {
        GObject *index = entry->data;
        gint address = mirage_index_get_address(MIRAGE_INDEX(index));

        if (address <= self->priv->track_start) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: found an index that's set before track's start... removing\n", __debug__);
            entry = entry->next; /* Because we'll remove the entry */
            mirage_track_remove_index_by_object(self, index);
            continue;
        }
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: setting index number to: %d\n", __debug__, cur_index);
        mirage_index_set_number(MIRAGE_INDEX(index), cur_index++);
    }
}

static void mirage_track_commit_topdown_change (MirageTrack *self)
{
    GList *entry;

    /* No need to rearrange indices, because they don't have anything to do with
       the global layout */

    /* Rearrange fragments: set start sectors */
    gint cur_fragment_address = 0;
    G_LIST_FOR_EACH(entry, self->priv->fragments_list) {
        GObject *fragment = entry->data;

        /* Set fragment's start address */
        mirage_fragment_set_address(MIRAGE_FRAGMENT(fragment), cur_fragment_address);
        cur_fragment_address += mirage_fragment_get_length(MIRAGE_FRAGMENT(fragment));
    }
}

static void mirage_track_commit_bottomup_change (MirageTrack *self)
{
    GList *entry;
    GObject *session;

    /* Calculate track length */
    self->priv->length = 0; /* Reset; it'll be recalculated */

    G_LIST_FOR_EACH(entry, self->priv->fragments_list) {
        GObject *fragment = entry->data;
        self->priv->length += mirage_fragment_get_length(MIRAGE_FRAGMENT(fragment));
    }

    /* Bottom-up change = change in fragments, so ISRC could've changed... */
    mirage_track_check_for_encoded_isrc(self);

    /* Signal track change */
    g_signal_emit_by_name(MIRAGE_OBJECT(self), "object-modified", NULL);

    /* If we don't have parent, we should complete the arc by committing top-down change */
    session = mirage_object_get_parent(MIRAGE_OBJECT(self));
    if (!session) {
        mirage_track_commit_topdown_change(self);
    } else {
        g_object_unref(session);
    }
}

static void mirage_track_fragment_modified_handler (GObject *fragment G_GNUC_UNUSED, MirageTrack *self)
{
    /* Bottom-up change */
    mirage_track_commit_bottomup_change(self);
}


static void mirage_track_remove_fragment (MirageTrack *self, GObject *fragment)
{
    /* Disconnect signal handler (find it by handler function and user data) */
    g_signal_handlers_disconnect_by_func(MIRAGE_OBJECT(fragment), mirage_track_fragment_modified_handler, self);

    /* Remove fragment from list and unref it */
    self->priv->fragments_list = g_list_remove(self->priv->fragments_list, fragment);
    g_object_unref(fragment);

    /* Bottom-up change */
    mirage_track_commit_bottomup_change(self);
}

static void mirage_track_remove_index (MirageTrack *self, GObject *index)
{
    /* Remove it from list and unref it */
    self->priv->indices_list = g_list_remove(self->priv->indices_list, index);
    g_object_unref(index);

    /* Rearrange indices; note that indices do *not* trigger a bottom-up change */
    mirage_track_rearrange_indices(self);
}

static void mirage_track_remove_language (MirageTrack *self, GObject *language)
{
    /* Remove it from list and unref it */
    self->priv->languages_list = g_list_remove(self->priv->languages_list, language);
    g_object_unref(language);
}


static gint sort_indices_by_address (GObject *index1, GObject *index2)
{
    gint address1 = mirage_index_get_address(MIRAGE_INDEX(index1));
    gint address2 = mirage_index_get_address(MIRAGE_INDEX(index2));

    if (address1 < address2) {
        return -1;
    } else if (address1 > address2) {
        return 1;
    } else {
        return 0;
    }
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


/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
/**
 * mirage_track_set_flags:
 * @self: a #MirageTrack
 * @flags: (in): track flags
 *
 * <para>
 * Sets track flags. @flags must be a combination of #MirageTrackFlags.
 * </para>
 **/
void mirage_track_set_flags (MirageTrack *self, gint flags)
{
    /* Set flags */
    self->priv->flags = flags;
}

/**
 * mirage_track_get_flags:
 * @self: a #MirageTrack
 *
 * <para>
 * Retrieves track flags.
 * </para>
 *
 * Returns: track flags
 **/
gint mirage_track_get_flags (MirageTrack *self)
{
    /* Return flags */
    return self->priv->flags;
}


/**
 * mirage_track_set_mode:
 * @self: a #MirageTrack
 * @mode: (in): track mode
 *
 * <para>
 * Sets track mode. @mode must be one of #MirageTrackModes.
 * </para>
 **/
void mirage_track_set_mode (MirageTrack *self, MirageTrackModes mode)
{
    /* Set mode */
    self->priv->mode = mode;
}

/**
 * mirage_track_get_mode:
 * @self: a #MirageTrack
 *
 * <para>
 * Retrieves track mode.
 * </para>
 *
 * Returns: track mode
 **/
MirageTrackModes mirage_track_get_mode (MirageTrack *self)
{
    /* Return mode */
    return self->priv->mode;
}


/**
 * mirage_track_get_adr:
 * @self: a #MirageTrack
 *
 * <para>
 * Retrieves track's ADR.
 * </para>
 *
 * <note>
 * At the moment, ADR is always returned as 1.
 * </note>
 *
 * Returns: ADR value
 **/
gint mirage_track_get_adr (MirageTrack *self G_GNUC_UNUSED)
{
    /* Return adr; always 1 */
    return 1;
}


/**
 * mirage_track_set_ctl:
 * @self: a #MirageTrack
 * @ctl: (in): track's CTL
 *
 * <para>
 * Sets track's CTL; the function translates CTL into track flags and sets them
 * using mirage_track_set_flags(). Track mode set with CTL is ignored.
 * </para>
 **/
void mirage_track_set_ctl (MirageTrack *self, gint ctl)
{
    gint flags = 0;

    /* We ignore track mode (data type) here */

    /* Flags */
    if (ctl & 0x01) flags |= MIRAGE_TRACK_FLAG_PREEMPHASIS;
    if (ctl & 0x02) flags |= MIRAGE_TRACK_FLAG_COPYPERMITTED;
    if (ctl & 0x08) flags |= MIRAGE_TRACK_FLAG_FOURCHANNEL;

    return mirage_track_set_flags(self, flags);
}

/**
 * mirage_track_get_ctl:
 * @self: a #MirageTrack
 *
 * <para>
 * Retrieves track's CTL. CTL is calculated on basis of track mode and track
 * flags.
 * </para>
 *
 * Returns: CTL value
 **/
gint mirage_track_get_ctl (MirageTrack *self)
{
    /* Return ctl */
    gint ctl = 0;

    /* If data (= non-audio) track, ctl = 0x4 */
    gint mode = mirage_track_get_mode(self);
    if (mode != MIRAGE_MODE_AUDIO) {
        ctl |= 0x4;
    }

    /* Flags */
    gint flags = mirage_track_get_flags(self);
    if (flags & MIRAGE_TRACK_FLAG_FOURCHANNEL) ctl |= 0x8;
    if (flags & MIRAGE_TRACK_FLAG_COPYPERMITTED) ctl |= 0x2;
    if (flags & MIRAGE_TRACK_FLAG_PREEMPHASIS) ctl |= 0x1;

    return ctl;
}


/**
 * mirage_track_set_isrc:
 * @self: a #MirageTrack
 * @isrc: (in): ISRC
 *
 * <para>
 * Sets MCN.
 * </para>
 *
 * <para>
 * Because ISRC is stored in subchannel data, this function silently
 * fails if track contains fragments with subchannel data provided.
 * </para>
 **/
void mirage_track_set_isrc (MirageTrack *self, const gchar *isrc)
{
    /* ISRC is encoded in track's subchannel. This means that if subchannel is
       provided by one of track's fragments (and therefore won't be generated by
       libMirage), ISRC shouldn't be settable... */

    if (self->priv->isrc_encoded) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: ISRC is already encoded in subchannel!\n", __debug__);
    } else {
        g_free(self->priv->isrc);
        self->priv->isrc = g_strndup(isrc, 12);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: set ISRC to <%.12s>\n", __debug__, self->priv->isrc);
    }
}

/**
 * mirage_track_get_isrc:
 * @self: a #MirageTrack
 *
 * <para>
 * Retrieves ISRC.
 * </para>
 *
 * Returns: (transfer none): pointer to ISRC string, or %NULL. The string
 * belongs to the object and should not be modified.
 **/
const gchar *mirage_track_get_isrc (MirageTrack *self)
{
    /* Return ISRC */
    return self->priv->isrc;
}


/**
 * mirage_track_get_sector:
 * @self: a #MirageTrack
 * @address: (in): sector address
 * @abs: (in): absolute address
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves a sector. @address is sector address for which a #MirageSector
 * object representing sector should be returned. @abs specifies whether @address
 * is absolute or relative; if %TRUE, @address is absolute (i.e. relative to start
 * of the disc), if %FALSE, it is relative (i.e. relative to start of the track).
 * </para>
 *
 * <para>
 * A reference to sector is stored in @sector; it
 * </para>
 *
 * Returns: sector object on success, %NULL on failure. The sector object
 * should be released with g_object_unref() when no longer needed.
 **/
GObject *mirage_track_get_sector (MirageTrack *self, gint address, gboolean abs, GError **error)
{
    GObject *sector = NULL;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: getting sector for address 0x%X (%d); absolute: %i\n", __debug__, address, address, abs);

    /* If sector address is absolute, we need to subtract track's start sector,
       since sector feeding code assumes relative address */
    if (abs) {
        address -= mirage_track_layout_get_start_sector(self);
    }

    /* Sector must lie within track boundaries... */
    if (address >= 0 && address < self->priv->length) {
        /* Create sector object */
        sector = g_object_new(MIRAGE_TYPE_SECTOR, NULL);
        /* While sector isn't strictly a child, we still need to attach it for debug context */
        mirage_object_attach_child(MIRAGE_OBJECT(self), sector);
        /* Feed data to sector */
        if (!mirage_sector_feed_data(MIRAGE_SECTOR(sector), address, G_OBJECT(self), error)) {
            g_object_unref(sector);
            return NULL;
        }
    } else {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, "Sector address out of range!");
        return NULL;
    }

    return sector;
}


/**
 * mirage_track_layout_get_session_number:
 * @self: a #MirageTrack
 *
 * <para>
 * Retrieves track's session number. If track is not part of disc layout, 0
 * is returned.
 * </para>
 *
 * Returns: session number
 **/
gint mirage_track_layout_get_session_number (MirageTrack *self)
{
    /* Get parent session... if it's not found, return 0 */
    GObject *session = mirage_object_get_parent(MIRAGE_OBJECT(self));
    gint number = -0;

    if (session) {
        number = mirage_session_layout_get_session_number(MIRAGE_SESSION(session));
        g_object_unref(session);
    }

    return number;
}


/**
 * mirage_track_layout_set_track_number:
 * @self: a #MirageTrack
 * @track_number: (in): track number
 *
 * <para>
 * Set track's track number.
 * </para>
 *
 * <note>
 * Intended for internal use only.
 * </note>
 **/
void mirage_track_layout_set_track_number (MirageTrack *self, gint track_number)
{
    /* Set track number */
    self->priv->track_number = track_number;
}

/**
 * mirage_track_layout_get_track_number:
 * @self: a #MirageTrack
 *
 * <para>
 * Retrieves track's track number.
 * </para>
 *
 * Returns: track number
 **/
gint mirage_track_layout_get_track_number (MirageTrack *self)
{
    /* Return track number */
    return self->priv->track_number;
}

/**
 * mirage_track_layout_set_start_sector:
 * @self: a #MirageTrack
 * @start_sector: (in): start sector
 *
 * <para>
 * Sets track's start sector.
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
void mirage_track_layout_set_start_sector (MirageTrack *self, gint start_sector)
{
    /* Set start sector */
    self->priv->start_sector = start_sector;
    /* Top-down change */
    mirage_track_commit_topdown_change(self);
}

/**
 * mirage_track_layout_get_start_sector:
 * @self: a #MirageTrack
 *
 * <para>
 * Retrieves track's start sector.
 * </para>
 *
 * Returns: start sector
 **/
gint mirage_track_layout_get_start_sector (MirageTrack *self)
{
    /* Return start sector */
    return self->priv->start_sector;
}

/**
 * mirage_track_layout_get_length:
 * @self: a #MirageTrack
 *
 * <para>
 * Retrieves track's length. The returned length is given in sectors.
 * </para>
 *
 * Returns: track length
 **/
gint mirage_track_layout_get_length (MirageTrack *self)
{
    /* Return track's real length */
    return self->priv->length;
}


/**
 * mirage_track_get_number_of_fragments:
 * @self: a #MirageTrack
 *
 * <para>
 * Retrieves number of fragments making up the track.
 * </para>
 *
 * Returns: number of fragments
 **/
gint mirage_track_get_number_of_fragments (MirageTrack *self)
{
    /* Return number of fragments */
    return g_list_length(self->priv->fragments_list); /* Length of list */
}

/**
 * mirage_track_add_fragment:
 * @self: a #MirageTrack
 * @index: (in): index at which fragment should be added
 * @fragment: (in): a #MirageFragment to be added
 *
 * <para>
 * Adds a fragment implementation to track. @index is index at which fragment
 * should be added. Negative index denotes index going backwards (i.e. -1 adds
 * fragment at the end, -2 adds fragment second-to-last, etc.). If index, either
 * negative or positive, is too big, fragment is respectively added at the
 * beginning or at the end of the track.
 * </para>
 *
 * <note>
 * Currently, unlike in most libMirage's *_add_* functions, @fragment argument cannot be %NULL.
 * This is because specific fragment implementation is required and therefore must be
 * provided by the caller.
 * </note>
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 **/
void mirage_track_add_fragment (MirageTrack *self, gint index, GObject *fragment)
{
    gint num_fragments;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: (index: %i, fragment: %p)\n", __debug__, index, fragment);

    /* First fragment, last fragment... allow negative indexes to go from behind */
    num_fragments = mirage_track_get_number_of_fragments(self);
    if (index < -num_fragments) {
        /* If negative index is too big, return the first fragment */
        index = 0;
    }
    if (index > num_fragments) {
        /* If positive index is too big, return the last fragment */
        index = num_fragments;
    }
    if (index < 0) {
        index += num_fragments + 1;
    }

    /* Add reference */
    g_object_ref(fragment);

    /* We don't set fragment's start address here, because layout recalculation will do it for us */
    /* Set parent */
    mirage_object_set_parent(MIRAGE_OBJECT(fragment), G_OBJECT(self));
    /* Attach child */
    mirage_object_attach_child(MIRAGE_OBJECT(self), fragment);

    /* Insert fragment into fragment list */
    self->priv->fragments_list = g_list_insert(self->priv->fragments_list, fragment, index);

    /* Connect fragment modified signal */
    g_signal_connect(MIRAGE_OBJECT(fragment), "object-modified", (GCallback)mirage_track_fragment_modified_handler, self);

    /* Bottom-up change */
    mirage_track_commit_bottomup_change(self);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: end\n", __debug__);
}

/**
 * mirage_track_remove_fragment_by_index:
 * @self: a #MirageTrack
 * @index: (in): index of fragment to be removed.
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Removes fragment from track.
 * </para>
 *
 * <para>
 * @index is the index of the fragment to be removed. This function calls
 * mirage_track_get_fragment_by_index() so @index behavior is determined by that
 * function.
 * </para>
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_remove_fragment_by_index (MirageTrack *self, gint index, GError **error)
{
    /* Find fragment by index */
    GObject *fragment = mirage_track_get_fragment_by_index(self, index, error);
    if (!fragment) {
        return FALSE;
    }

    /* Remove fragment from list */
    mirage_track_remove_fragment(self, fragment);
    g_object_unref(fragment); /* This one's from get */

    return TRUE;
}

/**
 * mirage_track_remove_fragment_by_object:
 * @self: a #MirageTrack
 * @fragment: (in): fragment object to be removed
 *
 * <para>
 * Removes fragment from track.
 * </para>
 *
 * <para>
 * @fragment is a #MirageFragment object to be removed.
 * </para>
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 **/
void mirage_track_remove_fragment_by_object (MirageTrack *self, GObject *fragment)
{
    mirage_track_remove_fragment(self, fragment);
}

/**
 * mirage_track_get_fragment_by_index:
 * @self: a #MirageTrack
 * @index: (in): index of fragment to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves fragment by index. If @index is negative, fragments from the end of
 * track are retrieved (e.g. -1 is for last track, -2 for second-to-last
 * track, etc.). If @index is out of range, regardless of the sign, the
 * function fails.
 * </para>
 *
 * Returns: a #MirageFragment on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_track_get_fragment_by_index (MirageTrack *self, gint index, GError **error)
{
    GObject *fragment;
    gint num_fragments;

    /* First fragment, last fragment... allow negative indexes to go from behind */
    num_fragments = mirage_track_get_number_of_fragments(self);
    if (index < -num_fragments || index >= num_fragments) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, "Fragment index %d out of range!", index);
        return FALSE;
    } else if (index < 0) {
        index += num_fragments;
    }

    /* Get index-th item from list... */
    fragment = g_list_nth_data(self->priv->fragments_list, index);

    if (!fragment) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, "Fragment with index %d not found!", index);
        return FALSE;
    }

    g_object_ref(fragment);
    return fragment;
}

/**
 * mirage_track_get_fragment_by_address:
 * @self: a #MirageTrack
 * @address: (in): address belonging to fragment to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves fragment by address. @address must be valid (track-relative) sector
 * address that is part of the fragment to be retrieved (i.e. lying between fragment's
 * start and end address).
 * </para>
 *
 * Returns: a #MirageFragment on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_track_get_fragment_by_address (MirageTrack *self, gint address, GError **error)
{
    GObject *fragment;
    GList *entry;

    /* Go over all fragments */
    fragment = NULL;
    G_LIST_FOR_EACH(entry, self->priv->fragments_list) {
        gint cur_address;
        gint cur_length;

        fragment = entry->data;

        cur_address = mirage_fragment_get_address(MIRAGE_FRAGMENT(fragment));
        cur_length = mirage_fragment_get_length(MIRAGE_FRAGMENT(fragment));

        /* Break the loop if address lies within fragment boundaries */
        if (address >= cur_address && address < cur_address + cur_length) {
            break;
        } else {
            fragment = NULL;
        }
    }

    /* If we didn't find anything... */
    if (!fragment) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, "Fragment with address %d not found!", address);
        return FALSE;
    }

    g_object_ref(fragment);
    return fragment;
}

/**
 * mirage_track_for_each_fragment:
 * @self: a #MirageTrack
 * @func: (in) (scope call): callback function
 * @user_data: (in) (closure): data to be passed to callback function
 *
 * <para>
 * Iterates over fragments list, calling @func for each fragment in the layout.
 * </para>
 *
 * <para>
 * If @func returns %FALSE, the function immediately returns %FALSE.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_for_each_fragment (MirageTrack *self, MirageCallbackFunction func, gpointer user_data)
{
    GList *entry;

    G_LIST_FOR_EACH(entry, self->priv->fragments_list) {
        gboolean succeeded = (*func) ((entry->data), user_data);
        if (!succeeded) {
            return FALSE;
        }
    }

    return TRUE;
}


/**
 * mirage_track_find_fragment_with_subchannel:
 * @self: a #MirageTrack
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves first fragment that contains subchannel data. A reference to fragment
 * is stored in @fragment; it should be released with g_object_unref() when no
 * longer needed.
 * </para>
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: a #MirageFragment on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_track_find_fragment_with_subchannel (MirageTrack *self, GError **error)
{
    GObject *fragment;
    GList *entry;

    /* Go over all fragments */
    fragment = NULL;
    G_LIST_FOR_EACH(entry, self->priv->fragments_list) {
        gint subchan_sectsize = 0;
        fragment = entry->data;

        mirage_fragment_read_subchannel_data(MIRAGE_FRAGMENT(fragment), 0, NULL, &subchan_sectsize, NULL);
        if (subchan_sectsize) {
            break;
        } else {
            fragment = NULL;
        }
    }

    /* If we didn't find anything... */
    if (!fragment) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, "No fragment with subchannel found!");
        return FALSE;
    }

    g_object_ref(fragment);
    return fragment;
}


/**
 * mirage_track_set_track_start:
 * @self: a #MirageTrack
 * @track_start: (in): track start address
 *
 * <para>
 * Sets track start address. @track_start is a track-relative address at which track's
 * pregap ends and track "logically" starts (i.e. where index changes from 00 to 01). Note that
 * this is not the same as start address that is set by mirage_track_layout_set_start_sector();
 * that one sets the address at which track "physically" starts (i.e. where index 00 starts).
 * </para>
 **/
void mirage_track_set_track_start (MirageTrack *self, gint track_start)
{
    /* Set track start */
    self->priv->track_start = track_start;
}

/**
 * mirage_track_get_track_start:
 * @self: a #MirageTrack
 *
 * <para>
 * Retrieves track start address. This is track-relative address at which pregap
 * ends and track "logically" starts (i.e. where index changes from 00 to 01).
 * </para>
 *
 * Returns: track start address
 **/
gint mirage_track_get_track_start (MirageTrack *self)
{
    /* Return track start */
    return self->priv->track_start;
}


/**
 * mirage_track_get_number_of_indices:
 * @self: a #MirageTrack
 *
 * <para>
 * Retrieves number of indices the track contains. Note that this includes
 * only indices greater than 01.
 * </para>
 *
 * Returns: number of indices
 **/
gint mirage_track_get_number_of_indices (MirageTrack *self)
{
    /* Return number of indices */
    return g_list_length(self->priv->indices_list); /* Length of list */
}

/**
 * mirage_track_add_index:
 * @self: a #MirageTrack
 * @address: (in): address at which the index is to be added
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Adds index to track.
 * </para>
 *
 * <para>
 * @address is track-relative address at which index should be added. As it determines
 * position of the index, it also determines the number index will be assigned.
 * </para>
 *
 * <para>
 * If address falls before index 01 (i.e. if it's less than address that was set
 * using mirage_track_set_track_start()), the function fails.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_add_index (MirageTrack *self, gint address, GError **error)
{
    GObject *index;
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: address: 0x%X\n", __debug__, address);

    /* Make sure we're not trying to put index before track start (which has index 1) */
    if (address < self->priv->track_start) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, "Invalid index start address (%d); before track start!", address);
        return FALSE;
    }

    /* Increment reference counter */
    index = g_object_new(MIRAGE_TYPE_INDEX, NULL);
    /* Set index address */
    mirage_index_set_address(MIRAGE_INDEX(index), address);
    /* Set parent */
    mirage_object_set_parent(MIRAGE_OBJECT(index), G_OBJECT(self));
    /* Attach child */
    mirage_object_attach_child(MIRAGE_OBJECT(self), index);

    /* Insert index into indices list */
    self->priv->indices_list = g_list_insert_sorted(self->priv->indices_list, index, (GCompareFunc)sort_indices_by_address);

    /* Rearrange indices; note that indices do *not* trigger a bottom-up change */
    mirage_track_rearrange_indices(self);

    return TRUE;
}

/**
 * mirage_track_remove_index_by_number:
 * @self: a #MirageTrack
 * @number: (in): index number of index to be removed
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Removes index from track. This causes index numbers of remaining indices to be readjusted.
 * </para>
 *
 * <para>
 * @number is index number of index to be removed. It must be greater or equal than 2.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_remove_index_by_number (MirageTrack *self, gint number, GError **error)
{
    /* Find index by number */
    GObject *index = mirage_track_get_index_by_number(self, number, error);
    if (!index) {
        return FALSE;
    }

    /* Remove index from list */
    mirage_track_remove_index(self, index);
    g_object_unref(index); /* This one's from get */

    return TRUE;
}

/**
 * mirage_track_remove_index_by_object:
 * @self: a #MirageTrack
 * @index: (in): index object to be removed
 *
 * <para>
 * Removes index from track.This causes index numbers of remaining indices to be readjusted.
 * </para>
 *
 * <para>
 * @index is a #MirageIndex object to be removed.
 * </para>
 **/
void mirage_track_remove_index_by_object (MirageTrack *self, GObject *index)
{
    mirage_track_remove_index(self, index);
}


/**
 * mirage_track_get_index_by_number:
 * @self: a #MirageTrack
 * @number: (in): index number of index to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves index by index number. If @number is negative, indices from the end of
 * track are retrieved (e.g. -1 is for index, -2 for second-to-last index, etc.).
 * If @number is out of range, regardless of the sign, the function fails.
 * </para>
 *
 * Returns: a #MirageIndex on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_track_get_index_by_number (MirageTrack *self, gint number, GError **error)
{
    GObject *index;
    gint num_indices;

    /* First index, last index... allow negative numbers to go from behind */
    num_indices = mirage_track_get_number_of_indices(self);
    if (number < -num_indices || number >= num_indices) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, "Index number %d out of range!", number);
        return NULL;
    } else if (number < 0) {
        number += num_indices;
    }

    /* Get index-th item from list... */
    index = g_list_nth_data(self->priv->indices_list, number);

    if (!index) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, "Index with number %d not found!", number);
        return NULL;
    }

    g_object_ref(index);
    return index;
}

/**
 * mirage_track_get_index_by_address:
 * @self: a #MirageTrack
 * @address: (in): address belonging to index to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves index by address. @address must be valid (track-relative) sector
 * address that is part of the index to be retrieved (i.e. lying between index's
 * start and end sector).
 * </para>
 *
 * Returns: a #MirageIndex on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_track_get_index_by_address (MirageTrack *self, gint address, GError **error)
{
    GObject *index;
    GList *entry;

    /* Go over all indices */
    index = NULL;
    G_LIST_FOR_EACH(entry, self->priv->indices_list) {
        GObject *cur_index = entry->data;

        /* We return the last index whose address doesn't surpass requested address */
        if (mirage_index_get_address(MIRAGE_INDEX(cur_index)) <= address) {
            index = cur_index;
        } else {
            break;
        }
    }

    /* If we didn't find anything... */
    if (!index) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, "Index with address %d not found!", address);
        return FALSE;
    }

    g_object_ref(index);
    return index;
}

/**
 * mirage_track_for_each_index:
 * @self: a #MirageTrack
 * @func: (in) (scope call): callback function
 * @user_data: (in) (closure): user data to be passed to callback function
 *
 * <para>
 * Iterates over indices list, calling @func for each index.
 * </para>
 *
 * <para>
 * If @func returns %FALSE, the function immediately returns %FALSE.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_for_each_index (MirageTrack *self, MirageCallbackFunction func, gpointer user_data)
{
    GList *entry;

    G_LIST_FOR_EACH(entry, self->priv->indices_list) {
        gboolean succeeded = (*func) (MIRAGE_INDEX(entry->data), user_data);
        if (!succeeded) {
            return FALSE;
        }
    }

    return TRUE;
}



/**
 * mirage_track_get_number_of_languages:
 * @self: a #MirageTrack
 *
 * <para>
 * Retrieves number of languages the track contains.
 * </para>
 *
 * Returns: number of languages
 **/
gint mirage_track_get_number_of_languages (MirageTrack *self)
{
    /* Return number of languages */
    return g_list_length(self->priv->languages_list); /* Length of list */
}

/**
 * mirage_track_add_language:
 * @self: a #MirageTrack
 * @langcode: (in): language code for the added language
 * @language: (in) (transfer full) (allow-none): a #MirageLanguage to be added
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Adds language to track.
 * </para>
 *
 * <para>
 * @langcode is language code that should be assigned to added language. If
 * language with that code is already present in the track, the function fails.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_add_language (MirageTrack *self, gint langcode, GObject *language, GError **error)
{
    GObject *tmp_language;

    /* Check if language already exists */
    tmp_language = mirage_track_get_language_by_code(self, langcode, NULL);
    if (tmp_language) {
        g_object_unref(tmp_language);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, "Language with language code %d already exists!", langcode);
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

    /* Insert language to language list */
    self->priv->languages_list = g_list_insert_sorted(self->priv->languages_list, language, (GCompareFunc)sort_languages_by_code);

    return TRUE;
}

/**
 * mirage_track_remove_language_by_index:
 * @self: a #MirageTrack
 * @index: (in): index of language to be removed
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Removes language from track.
 * </para>
 *
 * <para>
 * @index is the index of the language to be removed. This function calls
 * mirage_track_get_language_by_index() so @index behavior is determined by that
 * function.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_remove_language_by_index (MirageTrack *self, gint index, GError **error)
{
    /* Find track by index */
    GObject *language = mirage_track_get_language_by_index(self, index, error);
    if (!language) {
        return FALSE;
    }

    /* Remove track from list */
    mirage_track_remove_language(self, language);
    g_object_unref(language); /* This one's from get */

    return TRUE;
}

/**
 * mirage_track_remove_language_by_code:
 * @self: a #MirageTrack
 * @langcode: (in): language code of language to be removed
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Removes language from track.
 * </para>
 *
 * <para>
 * @langcode is language code the language to be removed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_remove_language_by_code (MirageTrack *self, gint langcode, GError **error)
{
    /* Find language by code */
    GObject *language = mirage_track_get_language_by_code(self, langcode, error);
    if (!language) {
        return FALSE;
    }

    /* Remove track from list */
    mirage_track_remove_language(self, language);
    g_object_unref(language); /* This one's from get */

    return TRUE;
}

/**
 * mirage_track_remove_language_by_object:
 * @self: a #MirageTrack
 * @language: (in): language object to be removed
 *
 * <para>
 * Removes language from track.
 * </para>
 *
 * <para>
 * @language is a #MirageLanguage object to be removed.
 * </para>
 **/
void mirage_track_remove_language_by_object (MirageTrack *self, GObject *language)
{
    mirage_track_remove_language(self, language);
}

/**
 * mirage_track_get_language_by_index:
 * @self: a #MirageTrack
 * @index: (in): index of language to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves language by index. If @index is negative, languages from the end of
 * track are retrieved (e.g. -1 is for last language, -2 for second-to-last
 * language, etc.). If @index is out of range, regardless of the sign, the
 * function fails.
 * </para>
 *
 * Returns: a #MirageLanguage on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_track_get_language_by_index (MirageTrack *self, gint index, GError **error)
{
    GObject *language;
    gint num_languages;

    /* First language, last language... allow negative indexes to go from behind */
    num_languages = mirage_track_get_number_of_languages(self);
    if (index < -num_languages || index >= num_languages) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, "Language index %d out of range!", index);
        return NULL;
    } else if (index < 0) {
        index += num_languages;
    }

    /* Get index-th item from list... */
    language = g_list_nth_data(self->priv->languages_list, index);

    if (!language) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, "Language with index %d not found!", index);
        return NULL;
    }

    g_object_ref(language);
    return language;
}

/**
 * mirage_track_get_language_by_code:
 * @self: a #MirageTrack
 * @langcode: (in): language code of language to be retrieved
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves language by language code.
 * </para>
 *
 * Returns: a #MirageLanguage on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_track_get_language_by_code (MirageTrack *self, gint langcode, GError **error)
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
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, "Language with language code %d not found!", langcode);
        return NULL;
    }

    g_object_ref(language);
    return language;
}

/**
 * mirage_track_for_each_language:
 * @self: a #MirageTrack
 * @func: (in) (scope call): callback function
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
gboolean mirage_track_for_each_language (MirageTrack *self, MirageCallbackFunction func, gpointer user_data)
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


/**
 * mirage_track_get_prev:
 * @self: a #MirageTrack
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves track that is placed before @self in session layout.
 * </para>
 *
 * Returns: a #MirageTrack on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_track_get_prev (MirageTrack *self, GError **error)
{
    GObject *session;
    GObject *track;

    /* Get parent session */
    session = mirage_object_get_parent(MIRAGE_OBJECT(self));
    if (!session) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, "Track is not in session layout!");
        return NULL;
    }

    track = mirage_session_get_track_before(MIRAGE_SESSION(session), G_OBJECT(self), error);
    g_object_unref(session);

    return track;
}

/**
 * mirage_track_get_next:
 * @self: a #MirageTrack
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves track that is placed after @self in session layout
 * </para>
 *
 * Returns: a #MirageTrack on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GObject *mirage_track_get_next (MirageTrack *self, GError **error)
{
    GObject *session;
    GObject *track;

    /* Get parent session */
    session = mirage_object_get_parent(MIRAGE_OBJECT(self));
    if (!session) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, "Track is not in session layout!");
        return NULL;
    }

    track = mirage_session_get_track_after(MIRAGE_SESSION(session), G_OBJECT(self), error);
    g_object_unref(session);

    return track;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE(MirageTrack, mirage_track, MIRAGE_TYPE_OBJECT);


static void mirage_track_init (MirageTrack *self)
{
    self->priv = MIRAGE_TRACK_GET_PRIVATE(self);

    self->priv->fragments_list = NULL;
    self->priv->indices_list = NULL;
    self->priv->languages_list = NULL;

    g_free(self->priv->isrc);

    self->priv->track_number = 1;
}

static void mirage_track_dispose (GObject *gobject)
{
    MirageTrack *self = MIRAGE_TRACK(gobject);
    GList *entry;

    /* Unref fragments */
    G_LIST_FOR_EACH(entry, self->priv->fragments_list) {
        if (entry->data) {
            GObject *fragment = entry->data;
            /* Disconnect signal handler and unref */
            g_signal_handlers_disconnect_by_func(MIRAGE_OBJECT(fragment), mirage_track_fragment_modified_handler, self);
            g_object_unref(fragment);

            entry->data = NULL;
        }
    }

    /* Unref indices */
    G_LIST_FOR_EACH(entry, self->priv->indices_list) {
        if (entry->data) {
            GObject *index = entry->data;
            g_object_unref(index);

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
    return G_OBJECT_CLASS(mirage_track_parent_class)->dispose(gobject);
}

static void mirage_track_finalize (GObject *gobject)
{
    MirageTrack *self = MIRAGE_TRACK(gobject);

    g_list_free(self->priv->fragments_list);
    g_list_free(self->priv->indices_list);
    g_list_free(self->priv->languages_list);

    g_free(self->priv->isrc);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_track_parent_class)->finalize(gobject);
}

static void mirage_track_class_init (MirageTrackClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = mirage_track_dispose;
    gobject_class->finalize = mirage_track_finalize;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageTrackPrivate));
}
