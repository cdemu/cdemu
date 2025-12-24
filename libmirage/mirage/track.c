/*
 *  libMirage: track
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
 * SECTION: mirage-track
 * @title: MirageTrack
 * @short_description: Object representing a track.
 * @see_also: #MirageDisc, #MirageSession, #MirageLanguage, #MirageSector
 * @include: mirage-track.h
 *
 * #MirageTrack object represents a track in the disc layout. It provides
 * functions for manipulating track data and layout; setting track type
 * and flags, adding and removing fragments, indices and languages,
 * read/write access to sectors, etc.
 */

#include "mirage/config.h"
#include "mirage/mirage.h"

#include <glib/gi18n-lib.h>

#define __debug__ "Track"


/**********************************************************************\
 *                         Private structure                          *
\**********************************************************************/
struct _MirageTrackPrivate
{
    /* Layout settings */
    gint track_number; /* Track number */
    gint start_sector; /* Start sector (where pregap starts)... disc/session-relative address */
    gint length;       /* Length of track (sum of fragments' length) */

    gint track_start; /* Track start sector (where index changes to 1)... track-relative address */

    /* Track mode and flags */
    gint flags; /* Track flags */
    MirageSectorType sector_type;  /* Type of sectors that comprise track */

    gchar *isrc; /* ISRC */
    gboolean isrc_fixed; /* Is ISRC fixed due to one of track's fragments having subchannel? */
    gboolean isrc_scan_complete; /* Have we performed scan for ISRC in track's fragments' subchannel? */

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
static gchar *mirage_track_scan_for_isrc (MirageTrack *self)
{
    MirageFragment *fragment = mirage_track_find_fragment_with_subchannel(self, NULL);
    gchar *isrc = NULL;

    if (!fragment) {
        return isrc;
    }

    /* According to INF8090, ISRC, if present, must be encoded in at least
       one sector in 100 consequtive sectors. So we read first hundred
       sectors' subchannel, and extract ISRC if we find it. */
    gint start_address = mirage_fragment_get_address(fragment);

    g_object_unref(fragment);

    for (gint address = start_address; address < start_address + 100; address++) {
        MirageSector *sector;
        const guint8 *buf;
        gint buflen;

        /* Get sector */
        sector = mirage_track_get_sector(self, address, FALSE, NULL);
        if (!sector) {
            break;
        }

        /* Get Q subchannel */
        if (!mirage_sector_get_subchannel(sector, MIRAGE_SUBCHANNEL_Q, &buf, &buflen, NULL)) {
            g_object_unref(sector);
            break;
        }

        if ((buf[0] & 0x0F) == 0x03) {
            /* Mode-3 Q found */
            gchar tmp_isrc[12];

            mirage_helper_subchannel_q_decode_isrc(&buf[1], tmp_isrc);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: found ISRC: <%s>\n", __debug__, tmp_isrc);

            /* Set ISRC */
            isrc = g_strndup(tmp_isrc, 12);
        }

        g_object_unref(sector);

        if (isrc) {
            break;
        }
    }

    return isrc;
}


static void mirage_track_rearrange_indices (MirageTrack *self)
{
    /* Rearrange indices: set their numbers */
    /* Indices numbers start with 2 (0 and 1 are controlled via
       get/set_track_start... and while we're at it, if index lies before
       track start, remove it (it most likely means track start got changed
       after indices were added) */
    gint cur_index = 2;
    g_assert(self->priv->indices_list != NULL);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: rearranging indices (%d indices found)...\n", __debug__, g_list_length(self->priv->indices_list));

    for (GList *entry = self->priv->indices_list; entry; entry = entry->next) {
        MirageIndex *index = entry->data;
        gint address = mirage_index_get_address(index);

        if (address <= self->priv->track_start) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: found an index that's set before track's start... removing\n", __debug__);
            entry = entry->next; /* Because we'll remove the entry */
            mirage_track_remove_index_by_object(self, index);
            continue;
        }
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: setting index number to: %d\n", __debug__, cur_index);
        mirage_index_set_number(index, cur_index++);
    }
}

static void mirage_track_commit_topdown_change (MirageTrack *self)
{
    /* No need to rearrange indices, because they don't have anything to do with
       the global layout */

    /* Rearrange fragments: set start sectors */
    gint cur_fragment_address = 0;

    for (GList *entry = self->priv->fragments_list; entry; entry = entry->next) {
        MirageFragment *fragment = entry->data;

        /* Set fragment's start address */
        mirage_fragment_set_address(fragment, cur_fragment_address);
        cur_fragment_address += mirage_fragment_get_length(fragment);
    }
}

static void mirage_track_commit_bottomup_change (MirageTrack *self)
{
    MirageSession *session;

    /* Calculate track length */
    self->priv->length = 0; /* Reset; it'll be recalculated */

    for (GList *entry = self->priv->fragments_list; entry; entry = entry->next) {
        MirageFragment *fragment = entry->data;
        self->priv->length += mirage_fragment_get_length(fragment);
    }

    /* Bottom-up change = change in fragments, so ISRC could've changed... */
    MirageFragment *fragment = mirage_track_find_fragment_with_subchannel(self, NULL);
    if (fragment) {
        self->priv->isrc_fixed = TRUE;
        self->priv->isrc_scan_complete = FALSE; /* Will trigger scan in mirage_track_get_isrc() */
        g_object_unref(fragment);
    } else {
        self->priv->isrc_fixed = FALSE;
    }

    /* Signal track change */
    g_signal_emit_by_name(self, "layout-changed", NULL);

    /* If we don't have parent, we should complete the arc by committing top-down change */
    session = mirage_object_get_parent(MIRAGE_OBJECT(self));
    if (!session) {
        mirage_track_commit_topdown_change(self);
    } else {
        g_object_unref(session);
    }
}

static void mirage_track_fragment_layout_changed_handler (MirageTrack *self, MirageFragment *fragment G_GNUC_UNUSED)
{
    /* Bottom-up change */
    mirage_track_commit_bottomup_change(self);
}


static void mirage_track_remove_fragment (MirageTrack *self, MirageFragment *fragment)
{
    /* Disconnect signal handler (find it by handler function and user data) */
    g_signal_handlers_disconnect_by_func(fragment, mirage_track_fragment_layout_changed_handler, self);

    /* Remove fragment from list and unref it */
    self->priv->fragments_list = g_list_remove(self->priv->fragments_list, fragment);
    g_object_unref(fragment);

    /* Bottom-up change */
    mirage_track_commit_bottomup_change(self);
}

static void mirage_track_remove_index (MirageTrack *self, MirageIndex *index)
{
    /* Remove it from list and unref it */
    self->priv->indices_list = g_list_remove(self->priv->indices_list, index);
    g_object_unref(index);

    /* Rearrange indices; note that indices do *not* trigger a bottom-up change */
    mirage_track_rearrange_indices(self);
}

static void mirage_track_remove_language (MirageTrack *self, MirageLanguage *language)
{
    /* Remove it from list and unref it */
    self->priv->languages_list = g_list_remove(self->priv->languages_list, language);
    g_object_unref(language);
}


static gint sort_indices_by_address (MirageIndex *index1, MirageIndex *index2)
{
    gint address1 = mirage_index_get_address(index1);
    gint address2 = mirage_index_get_address(index2);

    if (address1 < address2) {
        return -1;
    } else if (address1 > address2) {
        return 1;
    } else {
        return 0;
    }
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


/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
/**
 * mirage_track_set_flags:
 * @self: a #MirageTrack
 * @flags: (in): track flags
 *
 * Sets track flags. @flags must be a combination of #MirageTrackFlags.
 */
void mirage_track_set_flags (MirageTrack *self, gint flags)
{
    /* Set flags */
    self->priv->flags = flags;
}

/**
 * mirage_track_get_flags:
 * @self: a #MirageTrack
 *
 * Retrieves track flags.
 *
 * Returns: track flags
 */
gint mirage_track_get_flags (MirageTrack *self)
{
    /* Return flags */
    return self->priv->flags;
}


/**
 * mirage_track_set_sector_type:
 * @self: a #MirageTrack
 * @sector_type: (in): type of sectors comprising the track
 *
 * Sets sector type. @mode must be one of #MirageSectorType.
 */
void mirage_track_set_sector_type (MirageTrack *self, MirageSectorType sector_type)
{
    /* Set sector type */
    self->priv->sector_type = sector_type;
}

/**
 * mirage_track_get_sector_type:
 * @self: a #MirageTrack
 *
 * Retrieves type of sectors comprising the track.
 *
 * Returns: sector type
 */
MirageSectorType mirage_track_get_sector_type (MirageTrack *self)
{
    /* Return sector type */
    return self->priv->sector_type;
}


/**
 * mirage_track_get_adr:
 * @self: a #MirageTrack
 *
 * Retrieves track's ADR.
 *
 * <note>
 * At the moment, ADR is always returned as 1.
 * </note>
 *
 * Returns: ADR value
 */
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
 * Sets track's CTL; the function translates CTL into track flags and sets them
 * using mirage_track_set_flags(). Track mode set with CTL is ignored.
 */
void mirage_track_set_ctl (MirageTrack *self, gint ctl)
{
    gint flags = 0;

    /* We ignore track mode (data type) here */

    /* Flags */
    if (ctl & 0x01) flags |= MIRAGE_TRACK_FLAG_PREEMPHASIS;
    if (ctl & 0x02) flags |= MIRAGE_TRACK_FLAG_COPYPERMITTED;
    if (ctl & 0x08) flags |= MIRAGE_TRACK_FLAG_FOURCHANNEL;

    mirage_track_set_flags(self, flags);
}

/**
 * mirage_track_get_ctl:
 * @self: a #MirageTrack
 *
 * Retrieves track's CTL. CTL is calculated on basis of track mode and track
 * flags.
 *
 * Returns: CTL value
 */
gint mirage_track_get_ctl (MirageTrack *self)
{
    /* Return ctl */
    gint ctl = 0;

    /* If data (= non-audio) track, ctl = 0x4 */
    gint mode = mirage_track_get_sector_type(self);
    if (mode != MIRAGE_SECTOR_AUDIO) {
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
 * Sets MCN.
 *
 * Because ISRC is stored in subchannel data, this function silently
 * fails if track contains fragments with subchannel data provided.
 */
void mirage_track_set_isrc (MirageTrack *self, const gchar *isrc)
{
    /* ISRC is encoded in track's subchannel. This means that if subchannel is
       provided by one of track's fragments (and therefore won't be generated by
       libMirage), ISRC shouldn't be settable... */

    if (self->priv->isrc_fixed) {
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
 * Retrieves ISRC.
 *
 * Returns: (transfer none): pointer to ISRC string, or %NULL. The string
 * belongs to the object and should not be modified.
 */
const gchar *mirage_track_get_isrc (MirageTrack *self)
{
    if (self->priv->isrc_fixed && !self->priv->isrc_scan_complete) {
        g_free(self->priv->isrc);
        self->priv->isrc = mirage_track_scan_for_isrc(self);

        self->priv->isrc_scan_complete = TRUE;
    }

    /* Return ISRC */
    return self->priv->isrc;
}


/**
 * mirage_track_get_sector:
 * @self: a #MirageTrack
 * @address: (in): sector address
 * @abs: (in): absolute address
 * @error: (out) (optional): location to store error, or %NULL
 *
 * Retrieves a sector. @address is sector address for which a #MirageSector
 * object representing sector should be returned. @abs specifies whether @address
 * is absolute or relative; if %TRUE, @address is absolute (i.e. relative to start
 * of the disc), if %FALSE, it is relative (i.e. relative to start of the track).
 *
 * A reference to sector is stored in @sector; it should be released with
 * g_object_unref() when no longer needed.
 *
 * Returns: (transfer full): sector object on success, %NULL on failure. The sector object
 * should be released with g_object_unref() when no longer needed.
 */
MirageSector *mirage_track_get_sector (MirageTrack *self, gint address, gboolean abs, GError **error)
{
    MirageSector *sector;
    MirageFragment *fragment;
    GError *local_error = NULL;
    gint absolute_address, relative_address;
    gint fragment_start;
    guint8 *main_buffer, *subchannel_buffer;
    gint main_length, subchannel_length;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: getting sector for address 0x%X (%d); absolute: %i\n", __debug__, address, address, abs);

    /* We need both disc-absolute and track-relative address */
    if (abs) {
        absolute_address = address;
        relative_address = address - mirage_track_layout_get_start_sector(self);
    } else {
        relative_address = address;
        absolute_address = address + mirage_track_layout_get_start_sector(self);
    }

    /* Sector must lie within track boundaries... */
    if (relative_address < 0 || relative_address >= self->priv->length) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, Q_("Sector address out of range!"));
        return NULL;
    }

    /* Get data fragment to feed from */
    fragment = mirage_track_get_fragment_by_address(self, relative_address, &local_error);
    if (!fragment) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, Q_("Failed to get fragment to feed sector: %s"), local_error->message);
        g_error_free(local_error);
        return NULL;
    }

    /* Fragments work with fragment-relative addresses, so get fragment's start address */
    fragment_start = mirage_fragment_get_address(fragment);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: got fragment %p for track-relative address 0x%X; fragment relative address: 0x%X\n", __debug__, fragment, address, address - fragment_start);

    /* Main channel data */
    if (!mirage_fragment_read_main_data(fragment, relative_address - fragment_start, &main_buffer, &main_length, &local_error)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, Q_("Failed read main channel data: %s"), local_error->message);
        g_error_free(local_error);
        g_object_unref(fragment);
        return NULL;
    }

    /* Subchannel data */
    if (!mirage_fragment_read_subchannel_data(fragment, relative_address - fragment_start, &subchannel_buffer, &subchannel_length, &local_error)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, Q_("Failed to read subchannel data: %s"), local_error->message);
        g_error_free(local_error);
        g_object_unref(fragment);
        g_free(main_buffer);
        return NULL;
    }

    /* Create sector object */
    sector = g_object_new(MIRAGE_TYPE_SECTOR, NULL);
    mirage_object_set_parent(MIRAGE_OBJECT(sector), self);

    /* Feed data to sector; fragment's reading code guarantees that
       subchannel format is PW96 */
    if (!mirage_sector_feed_data(sector, absolute_address, self->priv->sector_type, main_buffer, main_length, MIRAGE_SUBCHANNEL_PW, subchannel_buffer, subchannel_length, 0, &local_error)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, Q_("Failed to feed data: %s"), local_error->message);
        g_error_free(local_error);

        g_object_unref(sector);
        sector = NULL;
    }

    /* Cleanup */
    g_free(main_buffer);
    g_free(subchannel_buffer);
    g_object_unref(fragment);

    return sector;
}


/**
 * mirage_track_put_sector:
 * @self: a #MirageTrack
 * @sector: (in): a #MirageSector representing sector to be written
 * @error: (out) (optional): location to store error, or %NULL
 *
 * Writes the @sector to track. The address at which sector is written
 * is retrieved from sector's property; for this function to succeed,
 * the address must either fall within track's layout (i.e., the track's
 * fragment(s) must have sufficient length "reserved" to accept sector),
 * or, alternatively, the sector address is allowed to equal track's
 * current lenght plus one. In the latter case, the track's length is
 * incremented when the sector is written (i.e., the corresponding track's
 * fragment is extended before data is written to it).
 *
 * Returns: %TRUE on success, %FALSE on failure.
 */
gboolean mirage_track_put_sector (MirageTrack *self, MirageSector *sector, GError **error)
{
    gint relative_address = mirage_sector_get_address(sector) - mirage_track_layout_get_start_sector(self);
    MirageFragment *fragment;
    gint fragment_start;
    GError *local_error = NULL;
    const guint8 *main_buffer, *subchannel_buffer;
    gint main_length, subchannel_length;

    /* Note that we check only if relative_address is greater than track's
       length. This accounts for the case when sector's address is one
       more than track's length, in which case we will append the sector
       to track */
    if (relative_address < 0 || relative_address > self->priv->length) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, Q_("Sector address out of range!"));
        return FALSE;
    }

    /* We allow appending of sector to the track only if relative address
       matches track's length, and if there is no track following this one */
    if (relative_address == self->priv->length) {
        MirageTrack *next_track = mirage_track_get_next(self, NULL);
        if (next_track) {
            g_object_unref(next_track);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, Q_("Cannot append sector to track that is not last in the layout!"));
            return FALSE;
        }
    }

    /* Get fragment */
    if (relative_address == self->priv->length) {
        fragment = mirage_track_get_fragment_by_index(self, -1, &local_error);
        if (!fragment) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, Q_("Failed to get last fragment to append sector: %s"), local_error->message);
            g_error_free(local_error);
            return FALSE;
        }
        /* Extend fragment so that we can write into it */
        mirage_fragment_set_length(fragment, mirage_fragment_get_length(fragment) + 1);
    } else {
        fragment = mirage_track_get_fragment_by_address(self, relative_address, &local_error);
        if (!fragment) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, Q_("Failed to get fragment to write sector: %s"), local_error->message);
            g_error_free(local_error);
            return FALSE;
        }
    }

    /* Fragments work with fragment-relative addresses, so get fragment's start address */
    fragment_start = mirage_fragment_get_address(fragment);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: got fragment %p for track-relative address 0x%X; fragment relative address: 0x%X\n", __debug__, fragment, relative_address, relative_address - fragment_start);

    /* Get expected main channel data size from the fragment; if fragment
       expects subchannel, we always feed 96-byte raw interleaved PW */
    main_length = mirage_fragment_main_data_get_size(fragment);
    subchannel_length = mirage_fragment_subchannel_data_get_size(fragment);

    /* Extract data from sector */
    if (!mirage_sector_extract_data(sector, &main_buffer, main_length, subchannel_length ? MIRAGE_SUBCHANNEL_PW : MIRAGE_SUBCHANNEL_NONE, &subchannel_buffer, subchannel_length ? 96 : 0, &local_error)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, Q_("Failed to extract data from sector: %s"), local_error->message);
        g_error_free(local_error);
        g_object_unref(fragment);
        return FALSE;
    }

    /* Write main channel data */
    if (!mirage_fragment_write_main_data(fragment, relative_address - fragment_start, main_buffer, main_length, &local_error)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, Q_("Failed write main channel data: %s"), local_error->message);
        g_error_free(local_error);
        g_object_unref(fragment);
        return FALSE;
    }

    /* Write subchannel data */
    if (subchannel_length) {
        if (!mirage_fragment_write_subchannel_data(fragment, relative_address - fragment_start, subchannel_buffer, subchannel_length, &local_error)) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, Q_("Failed to write subchannel data: %s"), local_error->message);
            g_error_free(local_error);
            g_object_unref(fragment);
            return FALSE;
        }
    }

    g_object_unref(fragment);

    return TRUE;
}

/**
 * mirage_track_layout_get_session_number:
 * @self: a #MirageTrack
 *
 * Retrieves track's session number. If track is not part of disc layout, 0
 * is returned.
 *
 * Returns: session number
 */
gint mirage_track_layout_get_session_number (MirageTrack *self)
{
    /* Get parent session... if it's not found, return 0 */
    MirageSession *session = mirage_object_get_parent(MIRAGE_OBJECT(self));
    gint number = -0;

    if (session) {
        number = mirage_session_layout_get_session_number(session);
        g_object_unref(session);
    }

    return number;
}


/**
 * mirage_track_layout_set_track_number:
 * @self: a #MirageTrack
 * @track_number: (in): track number
 *
 * Set track's track number.
 *
 * <note>
 * Intended for internal use only.
 * </note>
 */
void mirage_track_layout_set_track_number (MirageTrack *self, gint track_number)
{
    /* Set track number */
    self->priv->track_number = track_number;
}

/**
 * mirage_track_layout_get_track_number:
 * @self: a #MirageTrack
 *
 * Retrieves track's track number.
 *
 * Returns: track number
 */
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
 * Sets track's start sector.
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * <note>
 * Causes top-down change.
 * </note>
 */
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
 * Retrieves track's start sector.
 *
 * Returns: start sector
 */
gint mirage_track_layout_get_start_sector (MirageTrack *self)
{
    /* Return start sector */
    return self->priv->start_sector;
}

/**
 * mirage_track_layout_get_length:
 * @self: a #MirageTrack
 *
 * Retrieves track's length. The returned length is given in sectors.
 *
 * Returns: track length
 */
gint mirage_track_layout_get_length (MirageTrack *self)
{
    /* Return track's real length */
    return self->priv->length;
}


/**
 * mirage_track_layout_contains_address:
 * @self: a #MirageTrack
 * @address: address to be checked
 *
 * Checks whether the track contains the given address or not.
 *
 * Returns: %TRUE if @address falls inside track, %FALSE if it does not
 */
gboolean mirage_track_layout_contains_address (MirageTrack *self, gint address)
{
    return address >= self->priv->start_sector && address < self->priv->start_sector + self->priv->length;
}


/**
 * mirage_track_get_number_of_fragments:
 * @self: a #MirageTrack
 *
 * Retrieves number of fragments making up the track.
 *
 * Returns: number of fragments
 */
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
 * Adds a fragment implementation to track. @index is index at which fragment
 * should be added. Negative index denotes index going backwards (i.e. -1 adds
 * fragment at the end, -2 adds fragment second-to-last, etc.). If index, either
 * negative or positive, is too big, fragment is respectively added at the
 * beginning or at the end of the track.
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
 */
void mirage_track_add_fragment (MirageTrack *self, gint index, MirageFragment *fragment)
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
    mirage_object_set_parent(MIRAGE_OBJECT(fragment), self);

    /* Insert fragment into fragment list */
    self->priv->fragments_list = g_list_insert(self->priv->fragments_list, fragment, index);

    /* Connect fragment modified signal */
    g_signal_connect_swapped(fragment, "layout-changed", G_CALLBACK(mirage_track_fragment_layout_changed_handler), self);

    /* Bottom-up change */
    mirage_track_commit_bottomup_change(self);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: end\n", __debug__);
}

/**
 * mirage_track_remove_fragment_by_index:
 * @self: a #MirageTrack
 * @index: (in): index of fragment to be removed.
 * @error: (out) (optional): location to store error, or %NULL
 *
 * Removes fragment from track.
 *
 * @index is the index of the fragment to be removed. This function calls
 * mirage_track_get_fragment_by_index() so @index behavior is determined by that
 * function.
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_track_remove_fragment_by_index (MirageTrack *self, gint index, GError **error)
{
    /* Find fragment by index */
    MirageFragment *fragment = mirage_track_get_fragment_by_index(self, index, error);
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
 * Removes fragment from track.
 *
 * @fragment is a #MirageFragment object to be removed.
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 */
void mirage_track_remove_fragment_by_object (MirageTrack *self, MirageFragment *fragment)
{
    mirage_track_remove_fragment(self, fragment);
}

/**
 * mirage_track_get_fragment_by_index:
 * @self: a #MirageTrack
 * @index: (in): index of fragment to be retrieved
 * @error: (out) (optional): location to store error, or %NULL
 *
 * Retrieves fragment by index. If @index is negative, fragments from the end of
 * track are retrieved (e.g. -1 is for last track, -2 for second-to-last
 * track, etc.). If @index is out of range, regardless of the sign, the
 * function fails.
 *
 * Returns: (transfer full): a #MirageFragment on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
MirageFragment *mirage_track_get_fragment_by_index (MirageTrack *self, gint index, GError **error)
{
    MirageFragment *fragment;
    gint num_fragments;

    /* First fragment, last fragment... allow negative indexes to go from behind */
    num_fragments = mirage_track_get_number_of_fragments(self);
    if (index < -num_fragments || index >= num_fragments) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, Q_("Fragment index %d out of range!"), index);
        return FALSE;
    } else if (index < 0) {
        index += num_fragments;
    }

    /* Get index-th item from list... */
    fragment = g_list_nth_data(self->priv->fragments_list, index);

    if (!fragment) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, Q_("Fragment with index %d not found!"), index);
        return FALSE;
    }

    return g_object_ref(fragment);
}

/**
 * mirage_track_get_fragment_by_address:
 * @self: a #MirageTrack
 * @address: (in): address belonging to fragment to be retrieved
 * @error: (out) (optional): location to store error, or %NULL
 *
 * Retrieves fragment by address. @address must be valid (track-relative) sector
 * address that is part of the fragment to be retrieved (i.e. lying between fragment's
 * start and end address).
 *
 * Returns: (transfer full): a #MirageFragment on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
MirageFragment *mirage_track_get_fragment_by_address (MirageTrack *self, gint address, GError **error)
{
    MirageFragment *fragment = NULL;

    /* Go over all fragments */
    for (GList *entry = self->priv->fragments_list; entry; entry = entry->next) {
        fragment = entry->data;

        /* Break the loop if address lies within fragment boundaries */
        if (mirage_fragment_contains_address(fragment, address)) {
            break;
        } else {
            fragment = NULL;
        }
    }

    /* If we didn't find anything... */
    if (!fragment) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, Q_("Fragment with address %d not found!"), address);
        return FALSE;
    }

    return g_object_ref(fragment);
}

/**
 * mirage_track_enumerate_fragments:
 * @self: a #MirageTrack
 * @func: (in) (scope call) (closure user_data): callback function
 * @user_data: (in) (nullable): data to be passed to callback function
 *
 * Iterates over fragments list, calling @func for each fragment in the layout.
 *
 * If @func returns %FALSE, the function immediately returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_track_enumerate_fragments (MirageTrack *self, MirageEnumFragmentCallback func, gpointer user_data)
{
    for (GList *entry = self->priv->fragments_list; entry; entry = entry->next) {
        gboolean succeeded = (*func)(entry->data, user_data);
        if (!succeeded) {
            return FALSE;
        }
    }

    return TRUE;
}


/**
 * mirage_track_find_fragment_with_subchannel:
 * @self: a #MirageTrack
 * @error: (out) (optional): location to store error, or %NULL
 *
 * Retrieves first fragment that contains subchannel data. A reference to fragment
 * is stored in @fragment; it should be released with g_object_unref() when no
 * longer needed.
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: (transfer full): a #MirageFragment on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
MirageFragment *mirage_track_find_fragment_with_subchannel (MirageTrack *self, GError **error)
{
    MirageFragment *fragment = NULL;

    /* Go over all fragments */
    for (GList *entry = self->priv->fragments_list; entry; entry = entry->next) {
        fragment = entry->data;

        if (mirage_fragment_subchannel_data_get_size(fragment) && !mirage_fragment_is_writable(fragment)) {
            break;
        } else {
            fragment = NULL;
        }
    }

    /* If we didn't find anything... */
    if (!fragment) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, Q_("No fragment with subchannel found!"));
        return FALSE;
    }

    return g_object_ref(fragment);
}


/**
 * mirage_track_set_track_start:
 * @self: a #MirageTrack
 * @track_start: (in): track start address
 *
 * Sets track start address. @track_start is a track-relative address at which track's
 * pregap ends and track "logically" starts (i.e. where index changes from 00 to 01). Note that
 * this is not the same as start address that is set by mirage_track_layout_set_start_sector();
 * that one sets the address at which track "physically" starts (i.e. where index 00 starts).
 */
void mirage_track_set_track_start (MirageTrack *self, gint track_start)
{
    /* Set track start */
    self->priv->track_start = track_start;
}

/**
 * mirage_track_get_track_start:
 * @self: a #MirageTrack
 *
 * Retrieves track start address. This is track-relative address at which pregap
 * ends and track "logically" starts (i.e. where index changes from 00 to 01).
 *
 * Returns: track start address
 */
gint mirage_track_get_track_start (MirageTrack *self)
{
    /* Return track start */
    return self->priv->track_start;
}


/**
 * mirage_track_get_number_of_indices:
 * @self: a #MirageTrack
 *
 * Retrieves number of indices the track contains. Note that this includes
 * only indices greater than 01.
 *
 * Returns: number of indices
 */
gint mirage_track_get_number_of_indices (MirageTrack *self)
{
    /* Return number of indices */
    return g_list_length(self->priv->indices_list); /* Length of list */
}

/**
 * mirage_track_add_index:
 * @self: a #MirageTrack
 * @address: (in): address at which the index is to be added
 * @error: (out) (optional): location to store error, or %NULL
 *
 * Adds index to track.
 *
 * @address is track-relative address at which index should be added. As it determines
 * position of the index, it also determines the number index will be assigned.
 *
 * If address falls before index 01 (i.e. if it's less than address that was set
 * using mirage_track_set_track_start()), the function fails.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_track_add_index (MirageTrack *self, gint address, GError **error)
{
    MirageIndex *index;
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: address: 0x%X\n", __debug__, address);

    /* Make sure we're not trying to put index before track start (which has index 1) */
    if (address < self->priv->track_start) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, Q_("Invalid index start address (%d); before track start!"), address);
        return FALSE;
    }

    /* Increment reference counter */
    index = g_object_new(MIRAGE_TYPE_INDEX, NULL);
    /* Set index address */
    mirage_index_set_address(index, address);
    /* Set parent */
    mirage_object_set_parent(MIRAGE_OBJECT(index), self);

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
 * @error: (out) (optional): location to store error, or %NULL
 *
 * Removes index from track. This causes index numbers of remaining indices to be readjusted.
 *
 * @number is index number of index to be removed. It must be greater or equal than 2.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_track_remove_index_by_number (MirageTrack *self, gint number, GError **error)
{
    /* Find index by number */
    MirageIndex *index = mirage_track_get_index_by_number(self, number, error);
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
 * Removes index from track.This causes index numbers of remaining indices to be readjusted.
 *
 * @index is a #MirageIndex object to be removed.
 */
void mirage_track_remove_index_by_object (MirageTrack *self, MirageIndex *index)
{
    mirage_track_remove_index(self, index);
}


/**
 * mirage_track_get_index_by_number:
 * @self: a #MirageTrack
 * @number: (in): index number of index to be retrieved
 * @error: (out) (optional): location to store error, or %NULL
 *
 * Retrieves index by index number. If @number is negative, indices from the end of
 * track are retrieved (e.g. -1 is for index, -2 for second-to-last index, etc.).
 * If @number is out of range, regardless of the sign, the function fails.
 *
 * Returns: (transfer full): a #MirageIndex on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
MirageIndex *mirage_track_get_index_by_number (MirageTrack *self, gint number, GError **error)
{
    MirageIndex *index;
    gint num_indices;

    /* First index, last index... allow negative numbers to go from behind */
    num_indices = mirage_track_get_number_of_indices(self);
    if (number < -num_indices || number >= num_indices) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, Q_("Index number %d out of range!"), number);
        return NULL;
    } else if (number < 0) {
        number += num_indices;
    }

    /* Get index-th item from list... */
    index = g_list_nth_data(self->priv->indices_list, number);

    if (!index) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, Q_("Index with number %d not found!"), number);
        return NULL;
    }

    return g_object_ref(index);
}

/**
 * mirage_track_get_index_by_address:
 * @self: a #MirageTrack
 * @address: (in): address belonging to index to be retrieved
 * @error: (out) (optional): location to store error, or %NULL
 *
 * Retrieves index by address. @address must be valid (track-relative) sector
 * address that is part of the index to be retrieved (i.e. lying between index's
 * start and end sector).
 *
 * Returns: (transfer full): a #MirageIndex on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
MirageIndex *mirage_track_get_index_by_address (MirageTrack *self, gint address, GError **error)
{
    MirageIndex *index = NULL;

    /* Go over all indices */
    for (GList *entry = self->priv->indices_list; entry; entry = entry->next) {
        MirageIndex *cur_index = entry->data;

        /* We return the last index whose address doesn't surpass requested address */
        if (mirage_index_get_address(cur_index) <= address) {
            index = cur_index;
        } else {
            break;
        }
    }

    /* If we didn't find anything... */
    if (!index) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, Q_("Index with address %d not found!"), address);
        return FALSE;
    }

    return g_object_ref(index);
}

/**
 * mirage_track_enumerate_indices:
 * @self: a #MirageTrack
 * @func: (in) (scope call) (closure user_data): callback function
 * @user_data: (in) (nullable): user data to be passed to callback function
 *
 * Iterates over indices list, calling @func for each index.
 *
 * If @func returns %FALSE, the function immediately returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_track_enumerate_indices (MirageTrack *self, MirageEnumIndexCallback func, gpointer user_data)
{
    for (GList *entry = self->priv->indices_list; entry; entry = entry->next) {
        gboolean succeeded = (*func)(entry->data, user_data);
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
 * Retrieves number of languages the track contains.
 *
 * Returns: number of languages
 */
gint mirage_track_get_number_of_languages (MirageTrack *self)
{
    /* Return number of languages */
    return g_list_length(self->priv->languages_list); /* Length of list */
}

/**
 * mirage_track_add_language:
 * @self: a #MirageTrack
 * @code: (in): language code for the added language
 * @language: (in) (transfer full): a #MirageLanguage to be added
 * @error: (out) (optional): location to store error, or %NULL
 *
 * Adds language to track.
 *
 * @code is language code that should be assigned to added language. If
 * language with that code is already present in the track, the function fails.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_track_add_language (MirageTrack *self, gint code, MirageLanguage *language, GError **error)
{
    MirageLanguage *tmp_language;

    /* Check if language already exists */
    tmp_language = mirage_track_get_language_by_code(self, code, NULL);
    if (tmp_language) {
        g_object_unref(tmp_language);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, Q_("Language with language code %d already exists!"), code);
        return FALSE;
    }

    /* Increment reference counter */
    g_object_ref(language);
    /* Set language code */
    mirage_language_set_code(language, code);
    /* Set parent */
    mirage_object_set_parent(MIRAGE_OBJECT(language), self);

    /* Insert language to language list */
    self->priv->languages_list = g_list_insert_sorted(self->priv->languages_list, language, (GCompareFunc)sort_languages_by_code);

    return TRUE;
}

/**
 * mirage_track_remove_language_by_index:
 * @self: a #MirageTrack
 * @index: (in): index of language to be removed
 * @error: (out) (optional): location to store error, or %NULL
 *
 * Removes language from track.
 *
 * @index is the index of the language to be removed. This function calls
 * mirage_track_get_language_by_index() so @index behavior is determined by that
 * function.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_track_remove_language_by_index (MirageTrack *self, gint index, GError **error)
{
    /* Find track by index */
    MirageLanguage *language = mirage_track_get_language_by_index(self, index, error);
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
 * @code: (in): language code of language to be removed
 * @error: (out) (optional): location to store error, or %NULL
 *
 * Removes language from track.
 *
 * @code is language code the language to be removed.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_track_remove_language_by_code (MirageTrack *self, gint code, GError **error)
{
    /* Find language by code */
    MirageLanguage *language = mirage_track_get_language_by_code(self, code, error);
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
 * Removes language from track.
 *
 * @language is a #MirageLanguage object to be removed.
 */
void mirage_track_remove_language_by_object (MirageTrack *self, MirageLanguage *language)
{
    mirage_track_remove_language(self, language);
}

/**
 * mirage_track_get_language_by_index:
 * @self: a #MirageTrack
 * @index: (in): index of language to be retrieved
 * @error: (out) (optional): location to store error, or %NULL
 *
 * Retrieves language by index. If @index is negative, languages from the end of
 * track are retrieved (e.g. -1 is for last language, -2 for second-to-last
 * language, etc.). If @index is out of range, regardless of the sign, the
 * function fails.
 *
 * Returns: (transfer full): a #MirageLanguage on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
MirageLanguage *mirage_track_get_language_by_index (MirageTrack *self, gint index, GError **error)
{
    MirageLanguage *language;
    gint num_languages;

    /* First language, last language... allow negative indexes to go from behind */
    num_languages = mirage_track_get_number_of_languages(self);
    if (index < -num_languages || index >= num_languages) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, Q_("Language index %d out of range!"), index);
        return NULL;
    } else if (index < 0) {
        index += num_languages;
    }

    /* Get index-th item from list... */
    language = g_list_nth_data(self->priv->languages_list, index);

    if (!language) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, Q_("Language with index %d not found!"), index);
        return NULL;
    }

    return g_object_ref(language);
}

/**
 * mirage_track_get_language_by_code:
 * @self: a #MirageTrack
 * @code: (in): language code of language to be retrieved
 * @error: (out) (optional): location to store error, or %NULL
 *
 * Retrieves language by language code.
 *
 * Returns: (transfer full): a #MirageLanguage on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
MirageLanguage *mirage_track_get_language_by_code (MirageTrack *self, gint code, GError **error)
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
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, Q_("Language with language code %d not found!"), code);
        return NULL;
    }

    return g_object_ref(language);
}

/**
 * mirage_track_enumerate_languages:
 * @self: a #MirageTrack
 * @func: (in) (scope call) (closure user_data): callback function
 * @user_data: (in) (nullable): data to be passed to callback function
 *
 * Iterates over languages list, calling @func for each language.
 *
 * If @func returns %FALSE, the function immediately returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_track_enumerate_languages (MirageTrack *self, MirageEnumLanguageCallback func, gpointer user_data)
{
    for (GList *entry = self->priv->languages_list; entry; entry = entry->next) {
        gboolean succeeded = (*func)(entry->data, user_data);
        if (!succeeded) {
            return FALSE;
        }
    }

    return TRUE;
}


/**
 * mirage_track_get_prev:
 * @self: a #MirageTrack
 * @error: (out) (optional): location to store error, or %NULL
 *
 * Retrieves track that is placed before @self in session layout.
 *
 * Returns: (transfer full): a #MirageTrack on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
MirageTrack *mirage_track_get_prev (MirageTrack *self, GError **error)
{
    MirageSession *session;
    MirageTrack *track;

    /* Get parent session */
    session = mirage_object_get_parent(MIRAGE_OBJECT(self));
    if (!session) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, Q_("Track is not in session layout!"));
        return NULL;
    }

    track = mirage_session_get_track_before(session, self, error);
    g_object_unref(session);

    return track;
}

/**
 * mirage_track_get_next:
 * @self: a #MirageTrack
 * @error: (out) (optional): location to store error, or %NULL
 *
 * Retrieves track that is placed after @self in session layout
 *
 * Returns: (transfer full): a #MirageTrack on success, %NULL on failure.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
MirageTrack *mirage_track_get_next (MirageTrack *self, GError **error)
{
    MirageSession *session;
    MirageTrack *track;

    /* Get parent session */
    session = mirage_object_get_parent(MIRAGE_OBJECT(self));
    if (!session) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_TRACK_ERROR, Q_("Track is not in session layout!"));
        return NULL;
    }

    track = mirage_session_get_track_after(session, self, error);
    g_object_unref(session);

    return track;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE_WITH_PRIVATE(MirageTrack, mirage_track, MIRAGE_TYPE_OBJECT)


static void mirage_track_init (MirageTrack *self)
{
    self->priv = mirage_track_get_instance_private(self);

    self->priv->fragments_list = NULL;
    self->priv->indices_list = NULL;
    self->priv->languages_list = NULL;

    self->priv->isrc = NULL;
    self->priv->isrc_fixed = FALSE;
    self->priv->isrc_scan_complete = TRUE;

    self->priv->track_number = 1;
}

static void mirage_track_dispose (GObject *gobject)
{
    MirageTrack *self = MIRAGE_TRACK(gobject);

    /* Unref fragments */
    for (GList *entry = self->priv->fragments_list; entry; entry = entry->next) {
        if (entry->data) {
            MirageFragment *fragment = entry->data;
            /* Disconnect signal handler and unref */
            g_signal_handlers_disconnect_by_func(fragment, mirage_track_fragment_layout_changed_handler, self);
            g_object_unref(fragment);

            entry->data = NULL;
        }
    }

    /* Unref indices */
    for (GList *entry = self->priv->indices_list; entry; entry = entry->next) {
        if (entry->data) {
            MirageIndex *index = entry->data;
            g_object_unref(index);

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
    G_OBJECT_CLASS(mirage_track_parent_class)->dispose(gobject);
}

static void mirage_track_finalize (GObject *gobject)
{
    MirageTrack *self = MIRAGE_TRACK(gobject);

    g_list_free(self->priv->fragments_list);
    g_list_free(self->priv->indices_list);
    g_list_free(self->priv->languages_list);

    g_free(self->priv->isrc);

    /* Chain up to the parent class */
    G_OBJECT_CLASS(mirage_track_parent_class)->finalize(gobject);
}

static void mirage_track_class_init (MirageTrackClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = mirage_track_dispose;
    gobject_class->finalize = mirage_track_finalize;

    /* Signals */
    /**
     * MirageTrack::layout-changed:
     * @track: a #MirageTrack
     *
     * Emitted when a layout of #MirageTrack changed in a way that causes a bottom-up change.
     */
    g_signal_new("layout-changed", G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, NULL);
}
