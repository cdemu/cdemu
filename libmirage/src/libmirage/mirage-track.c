/*
 *  libMirage: Track object
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
#define MIRAGE_TRACK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_TRACK, MIRAGE_TrackPrivate))

typedef struct {    
    /* Layout settings */
    gint track_number; /* Track number */
    gint start_sector; /* Start sector (where pregap starts)... disc/session-relative address */
    gint length;       /* Length of track (sum of fragments' length) */
    
    gint track_start; /* Track start sector (where index changes to 1)... track-relative address */
    
    /* Track mode and flags */
    gint flags; /* Track flags */
    gint mode;  /* Track mode */
    
    gchar *isrc; /* ISRC */
    gboolean isrc_encoded; /* Is ISRC encoded in one of track's fragment's subchannel? */
    
    /* List of index changes (indexes > 1) */
    GList *indices_list;
    
    /* List of data fragments */
    GList *fragments_list; 
    
    /* CD-Text list */
    GList *languages_list;
} MIRAGE_TrackPrivate;


/******************************************************************************\
 *                              Private functions                             *
\******************************************************************************/
/* Forward declarations */
static gboolean __mirage_track_commit_topdown_change (MIRAGE_Track *self, GError **error);
static gboolean __mirage_track_commit_bottomup_change (MIRAGE_Track *self, GError **error);

static void __fragment_modified_handler (GObject *fragment, MIRAGE_Track *self);

static gboolean __remove_fragment_from_track (MIRAGE_Track *self, GObject *fragment, GError **error);
static gboolean __remove_index_from_track (MIRAGE_Track *self, GObject *index, GError **error);
static gboolean __remove_language_from_track (MIRAGE_Track *self, GObject *language, GError **error);

static gint __sort_indices_by_address (GObject *index1, GObject *index2);
static gint __sort_languages_by_code (GObject *language1, GObject *language2);

static gboolean __mirage_track_check_for_encoded_isrc (MIRAGE_Track *self, GError **error);


static gboolean __mirage_track_rearrange_indices (MIRAGE_Track *self, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    GList *entry = NULL;
    
    /* Rearrange indices: set their numbers */
    /* Indices numbers start with 2 (0 and 1 are controlled via 
       get/set_track_start... and while we're at it, if index lies before
       track start, remove it (it most likely means track start got changed
       after indices were added) */
    gint cur_index = 2;
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: rearranging indices (%d indices found)...\n", __func__, g_list_length(_priv->indices_list));
    G_LIST_FOR_EACH(entry, _priv->indices_list) {
        GObject *index = entry->data;
        gint address = 0;
        mirage_index_get_address(MIRAGE_INDEX(index), &address, NULL);
        if (address <= _priv->track_start) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: found an index that's set before track's start... removing\n", __func__);
            entry = entry->next; /* Because we'll remove the entry */
            mirage_track_remove_index_by_object(self, index, NULL);
            continue;
        }
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: setting index number to: %d\n", __func__, cur_index);
        mirage_index_set_number(MIRAGE_INDEX(index), cur_index++, NULL);
    }
    
    return TRUE;
}

static gboolean __mirage_track_commit_topdown_change (MIRAGE_Track *self, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    GList *entry = NULL;
    
    /* No need to rearrange indices, because they don't have anything to do with
       the global layout */
    
    /* Rearrange fragments: set start sectors */
    gint cur_fragment_address = 0;
    G_LIST_FOR_EACH(entry, _priv->fragments_list) {
        GObject *fragment = entry->data;
        
        /* Set fragment's start address */
        mirage_fragment_set_address(MIRAGE_FRAGMENT(fragment), cur_fragment_address, NULL);
        gint fragment_length = 0;
        mirage_fragment_get_length(MIRAGE_FRAGMENT(fragment), &fragment_length, NULL);
        cur_fragment_address += fragment_length;
    }
        
    return TRUE;
}

static gboolean __mirage_track_commit_bottomup_change (MIRAGE_Track *self, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    GList *entry = NULL;
        
    /* Calculate track length */
    _priv->length = 0; /* Reset; it'll be recalculated */

    G_LIST_FOR_EACH(entry, _priv->fragments_list) {
        GObject *fragment = entry->data;
        gint fragment_length = 0;
        mirage_fragment_get_length(MIRAGE_FRAGMENT(fragment), &fragment_length, NULL);
        _priv->length += fragment_length;
    }
    
    /* Bottom-up change = change in fragments, so ISRC could've changed... */
    __mirage_track_check_for_encoded_isrc(self, NULL);
    
    /* Signal track change */
    g_signal_emit_by_name(MIRAGE_OBJECT(self), "object-modified", NULL);
    /* If we don't have parent, we should complete the arc by committing top-down change */
    if (!mirage_object_get_parent(MIRAGE_OBJECT(self), NULL, NULL)) {
        __mirage_track_commit_topdown_change(self, NULL);
    }
    
    return TRUE;
}

static void __fragment_modified_handler (GObject *fragment, MIRAGE_Track *self) {   
    /* Bottom-up change */
    __mirage_track_commit_bottomup_change(self, NULL);
    return;
}

static gboolean __remove_fragment_from_track (MIRAGE_Track *self, GObject *fragment, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
                
    /* Disconnect signal handler (find it by handler function and user data) */
    g_signal_handlers_disconnect_by_func(MIRAGE_OBJECT(fragment), __fragment_modified_handler, self);
    
    /* Remove fragment from list and unref it */
    _priv->fragments_list = g_list_remove(_priv->fragments_list, fragment);
    g_object_unref(fragment);
    
    /* Bottom-up change */
    __mirage_track_commit_bottomup_change(self, NULL);
    
    return TRUE;
}

static gboolean __remove_index_from_track (MIRAGE_Track *self, GObject *index, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
            
    /* Remove it from list and unref it */
    _priv->indices_list = g_list_remove(_priv->indices_list, index);
    g_object_unref(index);
    
    /* Rearrange indices; note that indices do *not* trigger a bottom-up change */
    __mirage_track_rearrange_indices(self, NULL);
    
    return TRUE;
}

static gboolean __remove_language_from_track (MIRAGE_Track *self, GObject *language, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
            
    /* Remove it from list and unref it */
    _priv->languages_list = g_list_remove(_priv->languages_list, language);
    g_object_unref(language);
    
    return TRUE;
}

static gint __sort_indices_by_address (GObject *index1, GObject *index2) {           
    gint address1 = 0;
    gint address2 = 0;
        
    mirage_index_get_address(MIRAGE_INDEX(index1), &address1, NULL);
    mirage_index_get_address(MIRAGE_INDEX(index2), &address2, NULL);
    
    if (address1 < address2) {
        return -1;
    } else if (address1 > address2) {
        return 1;
    } else {
        return 0;
    }
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

static gboolean __mirage_track_check_for_encoded_isrc (MIRAGE_Track *self, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    GObject *fragment = NULL;
    
    /* Check if we have fragment with subchannel */
    if (mirage_track_find_fragment_with_subchannel(self, &fragment, NULL)) {
        gint sector = 0;
        gint start = 0, end = 0;
        
        /* According to INF8090, ISRC, if present, must be encoded in at least 
           one sector in 100 consequtive sectors. So we read first hundred 
           sectors' subchannel, and extract ISRC if we find it. */
        mirage_fragment_get_address(MIRAGE_FRAGMENT(fragment), &start, NULL);
        end = start + 100;
        
        g_object_unref(fragment);
        
        _priv->isrc_encoded = TRUE; /* It is, even if it may not be present... */
        /* Reset ISRC */
        g_free(_priv->isrc);
        _priv->isrc = NULL;
    
        for (sector = start; sector < end; sector++) {
            guint8 tmp_buf[16];
                        
            if (!mirage_track_read_sector(self, sector, FALSE, 0, MIRAGE_SUBCHANNEL_PQ, tmp_buf, NULL, error)) {
                return FALSE;
            }
            
            if ((tmp_buf[0] & 0x0F) == 0x03) {
                /* Mode-3 Q found */
                gchar tmp_isrc[12];
                
                mirage_helper_subchannel_q_decode_isrc(&tmp_buf[1], tmp_isrc);
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: found ISRC: <%s>\n", __func__, tmp_isrc);
                
                /* Set ISRC */
                _priv->isrc = g_strndup(tmp_isrc, 12);
            }
        }
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: no fragments with subchannel found\n", __func__);
        _priv->isrc_encoded = FALSE;
    }
    
    return TRUE;
}


/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
/**
 * mirage_track_set_flags:
 * @self: a #MIRAGE_Track
 * @flags: track flags
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets track flags. @flags must be a combination of #MIRAGE_TrackFlags.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_set_flags (MIRAGE_Track *self, gint flags, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    /* Set flags */
    _priv->flags = flags;
    return TRUE;
}

/**
 * mirage_track_get_flags:
 * @self: a #MIRAGE_Track
 * @flags: location to store track flags
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track flags.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_get_flags (MIRAGE_Track *self, gint *flags, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(flags);
    /* Return flags */
    *flags = _priv->flags;
    return TRUE;
}


/**
 * mirage_track_set_mode:
 * @self: a #MIRAGE_Track
 * @mode: track mode
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets track mode. @mode must be one of #MIRAGE_TrackModes.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_set_mode (MIRAGE_Track *self, gint mode, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    /* Set mode */
    _priv->mode = mode;
    return TRUE;
}

/**
 * mirage_track_get_mode:
 * @self: a #MIRAGE_Track
 * @mode: location to store track mode
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track mode.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_get_mode (MIRAGE_Track *self, gint *mode, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(mode);
    /* Return mode */
    *mode = _priv->mode;
    return TRUE;
}


/**
 * mirage_track_get_adr:
 * @self: a #MIRAGE_Track
 * @adr: location to store ADR
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track's ADR.
 * </para>
 *
 * <note>
 * At the moment, ADR is always returned as 1.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_get_adr (MIRAGE_Track *self, gint *adr, GError **error) {
    MIRAGE_CHECK_ARG(adr);
    /* Return adr; always 1 */
    *adr = 1;
    return TRUE;
}


/**
 * mirage_track_set_ctl:
 * @self: a #MIRAGE_Track
 * @ctl: track's CTL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets track's CTL; the function translates CTL into track flags and sets them
 * using mirage_track_set_flags(). Track mode set with CTL is ignored.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_set_ctl (MIRAGE_Track *self, gint ctl, GError **error) {
    gint flags = 0;
    
    /* We ignore track mode (data type) here */
    
    /* Flags */
    if (ctl & 0x01) flags |= MIRAGE_TRACKF_PREEMPHASIS;
    if (ctl & 0x02) flags |= MIRAGE_TRACKF_COPYPERMITTED;
    if (ctl & 0x08) flags |= MIRAGE_TRACKF_FOURCHANNEL;
    
    mirage_track_set_flags(self, flags, NULL);

    return TRUE;
}

/**
 * mirage_track_get_ctl:
 * @self: a #MIRAGE_Track
 * @ctl: location to store track's CTL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track's CTL. CTL is calculated on basis of track mode and track
 * flags.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_get_ctl (MIRAGE_Track *self, gint *ctl, GError **error) {
    MIRAGE_CHECK_ARG(ctl);
    
    /* Return ctl */
    *ctl = 0;
    
    /* If data (= non-audio) track, ctl = 0x4 */
    gint mode = 0;
    mirage_track_get_mode(self, &mode, NULL);
    if (mode != MIRAGE_MODE_AUDIO) {
        *ctl |= 0x4;
    }
    
    /* Flags */
    gint flags = 0;
    mirage_track_get_flags(self, &flags, NULL);
    if (flags & MIRAGE_TRACKF_FOURCHANNEL) *ctl |= 0x8;
    if (flags & MIRAGE_TRACKF_COPYPERMITTED) *ctl |= 0x2;
    if (flags & MIRAGE_TRACKF_PREEMPHASIS) *ctl |= 0x1;
    
    return TRUE;
}


/**
 * mirage_track_set_isrc:
 * @self: a #MIRAGE_Track
 * @isrc: ISRC
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets ISRC.
 * </para>
 *
 * <para>
 * Because ISRC is stored in subchannel data, this function fails if track
 * contains fragments with subchannel data provided. In that case error is 
 * set to %MIRAGE_E_DATAFIXED.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_set_isrc (MIRAGE_Track *self, gchar *isrc, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);

    MIRAGE_CHECK_ARG(isrc);
    
    /* ISRC is encoded in track's subchannel. This means that if subchannel is
       provided by one of track's fragments (and therefore won't be generated by 
       libMirage), ISRC shouldn't be settable... */
    
    if (_priv->isrc_encoded) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: ISRC is already encoded in subchannel!\n", __func__);
        mirage_error(MIRAGE_E_DATAFIXED, error);
        return FALSE;
    } else {
        g_free(_priv->isrc);
        _priv->isrc = g_strndup(isrc, 12);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: set ISRC to <%.12s>\n", __func__, _priv->isrc);
    }

    return TRUE;
}

/**
 * mirage_track_get_isrc:
 * @self: a #MIRAGE_Track
 * @isrc: location to store ISRC, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves ISRC.
 * </para>
 *
 * <para>
 * A copy of ISRC string is stored in @isrc; it should be freed with g_free() 
 * when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_get_isrc (MIRAGE_Track *self, gchar **isrc, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    
    /* Check if ISRC is set */
    if (!(_priv->isrc && _priv->isrc[0])) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: ISRC not set!\n", __func__);
        mirage_error(MIRAGE_E_DATANOTSET, error);
        return FALSE;
    }
    
    /* Return ISRC */
    if (isrc) {
        *isrc = g_strndup(_priv->isrc, 12);
    }
    
    return TRUE;
}


/**
 * mirage_track_get_sector:
 * @self: a #MIRAGE_Track
 * @address: sector address
 * @abs: absolute address
 * @sector: location to store sector
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves a sector. @address is sector address for which a #MIRAGE_Sector
 * object representing sector should be returned. @abs specifies whether @address
 * is absolute or relative; if %TRUE, @address is absolute (i.e. relative to start
 * of the disc), if %FALSE, it is relative (i.e. relative to start of the track).
 * </para>
 *
 * <para>
 * A reference to sector is stored in @sector; it should be released with 
 * g_object_unref() when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_get_sector (MIRAGE_Track *self, gint address, gboolean abs, GObject **sector, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    GObject *ret_sector = NULL;
    
    MIRAGE_CHECK_ARG(sector);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: getting sector for address 0x%X (%d); absolute: %i\n", __func__, address, address, abs);
    
    /* If sector address is absolute, we need to subtract track's start sector,
       since sector feeding code assumes relative address */
    if (abs) {
        gint start = 0;
        if (!mirage_track_layout_get_start_sector(self, &start, error)) {
            return FALSE;
        }
        address -= start;
    }
        
    /* Sector must lie within track boundaries... */
    if (address >= 0 && address < _priv->length) {
        /* Create sector object */
        ret_sector = g_object_new(MIRAGE_TYPE_SECTOR, NULL);
        /* While sector isn't strictly a child, we still need to attach it for debug context */
        mirage_object_attach_child(MIRAGE_OBJECT(self), ret_sector, NULL);
        /* Feed data to sector */
        if (!mirage_sector_feed_data(MIRAGE_SECTOR(ret_sector), address, G_OBJECT(self), error)) {
            g_object_unref(ret_sector);
            return FALSE;
        }
    } else {
        mirage_error(MIRAGE_E_SECTOROUTOFRANGE, error);
        return FALSE;
    }
    
    *sector = ret_sector;
    return TRUE;
}

/**
 * mirage_track_read_sector:
 * @self: a #MIRAGE_Track
 * @address: sector address
 * @abs: absolute address
 * @main_sel: main channel selection flags
 * @subc_sel: subchannel selection flags
 * @ret_buf: buffer to write data into, or %NULL
 * @ret_len: location to store written data length, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Reads data from sector at address @address. It internally acquires a 
 * #MIRAGE_Sector object by passing @address and @abs to mirage_track_get_sector();
 * behavior of those two arguments is determined by that function. 
 * </para>
 *
 * <para>
 * If sector object is successfully acquired, its data is read using 
 * mirage_sector_get_sync(), mirage_sector_get_header(),
 * mirage_sector_get_data(), mirage_sector_get_edc_ecc() and mirage_sector_get_subchannel().
 * in accord with main channel and subchannel selection flags. @main_sel can
 * be a combination of #MIRAGE_SectorMCSB and @subc_sel must be one of 
 * #MIRAGE_SectorSubchannelType. 
 * </para>
 *
 * <para>
 * Data is written into buffer provided in @ret_buf, and written data length 
 * is stored into @ret_len.
 * </para>
 *
 * <note>
 * If any of data fields specified in @main_sel are inappropriate for the given
 * track's mode, they are silently ignored (for example, if subheaders were 
 * requested in Mode 1 track).
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_read_sector (MIRAGE_Track *self, gint address, gboolean abs, guint8 main_sel, guint8 subc_sel, guint8 *ret_buf, gint *ret_len, GError **error) {
    GObject *cur_sector = NULL;
        
    if (!mirage_track_get_sector(self, address, abs, &cur_sector, error)) {
        return FALSE;
    }
    
    gint len = 0;
    guint8 *ptr = ret_buf;
    
    /* Read whatever was requested in main channel selection byte... request must
       be appropriate for sector we're dealing with... (FIXME: checking?) */
    /* Sync */
    if (main_sel & MIRAGE_MCSB_SYNC) {
        guint8 *tmp_buf = NULL;
        gint tmp_len = 0;
        
        mirage_sector_get_sync(MIRAGE_SECTOR(cur_sector), &tmp_buf, &tmp_len, NULL);
        if (ret_buf) {
            memcpy(ptr, tmp_buf, tmp_len);
            ptr += tmp_len;
        }
        len += tmp_len;
    }
    /* Header */
    if (main_sel & MIRAGE_MCSB_HEADER) {
        guint8 *tmp_buf = NULL;
        gint tmp_len = 0;
                    
        mirage_sector_get_header(MIRAGE_SECTOR(cur_sector), &tmp_buf, &tmp_len, NULL);
        if (ret_buf) {
            memcpy(ptr, tmp_buf, tmp_len);
            ptr += tmp_len;
        }
        len += tmp_len;
    }                
    /* Sub-Header */
    if (main_sel & MIRAGE_MCSB_SUBHEADER) {
        guint8 *tmp_buf = NULL;
        gint tmp_len = 0;
                    
        mirage_sector_get_subheader(MIRAGE_SECTOR(cur_sector), &tmp_buf, &tmp_len, NULL);
        if (ret_buf) {
            memcpy(ptr, tmp_buf, tmp_len);
            ptr += tmp_len;
        }
        len += tmp_len;
    }    
    /* User Data */
    if (main_sel & MIRAGE_MCSB_DATA) {
        guint8 *tmp_buf = NULL;
        gint tmp_len = 0;
                    
        mirage_sector_get_data(MIRAGE_SECTOR(cur_sector), &tmp_buf, &tmp_len, NULL);
        if (ret_buf) {
            memcpy(ptr, tmp_buf, tmp_len);
            ptr += tmp_len;
        }
        len += tmp_len;
    }
    /* EDC/ECC */
    if (main_sel & MIRAGE_MCSB_EDC_ECC) {
        guint8 *tmp_buf = NULL;
        gint tmp_len = 0;
                
        mirage_sector_get_edc_ecc(MIRAGE_SECTOR(cur_sector), &tmp_buf, &tmp_len, NULL);
        if (ret_buf) {
            memcpy(ptr, tmp_buf, tmp_len);
            ptr += tmp_len;
        }
        len += tmp_len;
    }

    /* "read" C2 Error: nothing to copy, just set the offset and all ;) */
    if (main_sel & MIRAGE_MCSB_C2_1) {
        if (ret_buf) {
            ptr += 294;
        }
        len += 294;
    } else if (main_sel & MIRAGE_MCSB_C2_2) {
        if (ret_buf) {
            ptr += 296;
        }
        len += 296;
    }
    
    /* Subchannel */
    switch (subc_sel) {
        case MIRAGE_SUBCHANNEL_PW: {
            /* RAW P-W */
            guint8 *tmp_buf = NULL;
            gint tmp_len = 0;
                    
            mirage_sector_get_subchannel(MIRAGE_SECTOR(cur_sector), MIRAGE_SUBCHANNEL_PW, &tmp_buf, &tmp_len, NULL);
            
            if (ret_buf) {
                memcpy(ptr, tmp_buf, tmp_len);
                ptr += tmp_len;
            }
            len += tmp_len;
            break;
        }
        case MIRAGE_SUBCHANNEL_PQ: {
            /* Q */
            guint8 *tmp_buf = NULL;
            gint tmp_len = 0;
                    
            mirage_sector_get_subchannel(MIRAGE_SECTOR(cur_sector), MIRAGE_SUBCHANNEL_PQ, &tmp_buf, &tmp_len, NULL);
            if (ret_buf) {
                memcpy(ptr, tmp_buf, tmp_len);
                ptr += tmp_len;
            }
            len += tmp_len;
            break;
        }
    }
    
    g_object_unref(cur_sector);
    
    if (ret_len) {
        *ret_len = len;
    }
        
    return TRUE;
}


/**
 * mirage_track_layout_get_session_number:
 * @self: a #MIRAGE_Track
 * @session_number: location to store session number
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track's session number. If track is not part of disc layout, 0 
 * is returned.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_layout_get_session_number (MIRAGE_Track *self, gint *session_number, GError **error) {
    /*MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);*/
    MIRAGE_CHECK_ARG(session_number);
    GObject *session = NULL;
    
    /* Get parent session... if it's not found, return 0 */
    if (mirage_object_get_parent(MIRAGE_OBJECT(self), &session, NULL)) {
        mirage_session_layout_get_session_number(MIRAGE_SESSION(session), session_number, error);
        g_object_unref(session);
    } else {
        *session_number = 0;
    }
    
    return TRUE;
}


/**
 * mirage_track_layout_set_track_number:
 * @self: a #MIRAGE_Track
 * @track_number: track number
 * @error: location to store error, or %NULL
 *
 * <para>
 * Set track's track number.
 * </para>
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_layout_set_track_number (MIRAGE_Track *self, gint track_number, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    /* Set track number */
    _priv->track_number = track_number;
    return TRUE;
}

/**
 * mirage_track_layout_get_track_number:
 * @self: a #MIRAGE_Track
 * @track_number: location to store track number
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track's track number.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_layout_get_track_number (MIRAGE_Track *self, gint *track_number, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(track_number);
    /* Return track number */
    *track_number = _priv->track_number;
    return TRUE;
}

/**
 * mirage_track_layout_set_start_sector:
 * @self: a #MIRAGE_Track
 * @start_sector: start sector
 * @error: location to store error, or %NULL
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
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_layout_set_start_sector (MIRAGE_Track *self, gint start_sector, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    /* Set start sector */
    _priv->start_sector = start_sector;
    /* Top-down change */
    __mirage_track_commit_topdown_change(self, NULL);
    return TRUE;
}

/**
 * mirage_track_layout_get_start_sector:
 * @self: a #MIRAGE_Track
 * @start_sector: location to store start sector
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track's start sector.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_layout_get_start_sector (MIRAGE_Track *self, gint *start_sector, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(start_sector);
    /* Return start sector */
    *start_sector = _priv->start_sector;
    return TRUE;
}

/**
 * mirage_track_layout_get_length:
 * @self: a #MIRAGE_Track
 * @length: location to store track length
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track's length.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_layout_get_length (MIRAGE_Track *self, gint *length, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(length);
    
    /* Return track's real length */
    *length = _priv->length;
    
    return TRUE;
}


/**
 * mirage_track_get_number_of_fragments:
 * @self: a #MIRAGE_Track
 * @number_of_fragments: location to store number of fragments
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves number of fragments making up the track.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_get_number_of_fragments (MIRAGE_Track *self, gint *number_of_fragments, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(number_of_fragments);
    /* Return number of fragments */
    *number_of_fragments = g_list_length(_priv->fragments_list); /* Length of list */
    return TRUE;
}

/**
 * mirage_track_add_fragment:
 * @self: a #MIRAGE_Track
 * @index: index at which fragment should be added
 * @fragment: pointer to #MIRAGE_Fragment implementation
 * @error: location to store error, or %NULL
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
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_add_fragment (MIRAGE_Track *self, gint index, GObject **fragment, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    GObject *new_fragment = NULL;
    gint num_fragments = 0;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: (index: %i, fragment: %p->%p)\n", __func__, index, fragment, fragment ? *fragment : NULL);
    
    /* First fragment, last fragment... allow negative indexes to go from behind */
    mirage_track_get_number_of_fragments(self, &num_fragments, NULL);
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

#if 0
    /* If there's fragment provided, use it; else create new one */
    if (fragment && *fragment) {
        new_fragment = *fragment;
        /* If fragment is not MIRAGE_Fragment... */
        if (!MIRAGE_IS_FRAGMENT(new_fragment)) {
            mirage_error(MIRAGE_E_INVALIDOBJTYPE, error);
            return FALSE;
        }
        g_object_ref(new_fragment);
    } else {
        new_fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL);
    }
#else
    /* Fragment *MUST* be provided in the current implementation (because
       subtype needs to be manually determined... hopefully that will change
       one day */
    new_fragment = *fragment;
    if (!MIRAGE_IS_FRAGMENT(new_fragment)) {
        mirage_error(MIRAGE_E_INVALIDOBJTYPE, error);
        return FALSE;
    }
    g_object_ref(new_fragment);
#endif
    
    /* We don't set fragment's start address here, because layout recalculation will do it for us */
    /* Set parent */
    mirage_object_set_parent(MIRAGE_OBJECT(new_fragment), G_OBJECT(self), NULL);
    /* Attach child */
    mirage_object_attach_child(MIRAGE_OBJECT(self), new_fragment, NULL);
    
    /* Insert fragment into fragment list */
    _priv->fragments_list = g_list_insert(_priv->fragments_list, new_fragment, index);

    /* Connect fragment modified signal */
    g_signal_connect(MIRAGE_OBJECT(new_fragment), "object-modified", (GCallback)__fragment_modified_handler, self);
    
    /* Bottom-up change */
    __mirage_track_commit_bottomup_change(self, NULL);
        
    /* Return fragment to user if she wants it */
    if (fragment && (*fragment == NULL)) {
        g_object_ref(new_fragment);
        *fragment = new_fragment;
    }
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: end\n", __func__);
    
    return TRUE;
}
    
/**
 * mirage_track_remove_fragment_by_index:
 * @self: a #MIRAGE_Track
 * @index: index of fragment to be removed.
 * @error: location to store error, or %NULL
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
gboolean mirage_track_remove_fragment_by_index (MIRAGE_Track *self, gint index, GError **error) {
    /*MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);*/
    GObject *fragment = NULL;
    gboolean succeeded = FALSE;
    
    /* Find fragment by index */
    if (!mirage_track_get_fragment_by_index(self, index, &fragment, error)) {
        return FALSE;
    }
    
    /* Remove fragment from list */
    succeeded = __remove_fragment_from_track(self, fragment, error);
    g_object_unref(fragment); /* This one's from get */
    
    return succeeded;
}

/**
 * mirage_track_remove_fragment_by_object:
 * @self: a #MIRAGE_Track
 * @fragment: fragment object to be removed
 * @error: location to store error, or %NULL
 *
 * <para>
 * Removes fragment from track.
 * </para>
 *
 * <para>
 * @fragment is a #MIRAGE_Fragment object to be removed.
 * </para>
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_remove_fragment_by_object (MIRAGE_Track *self, GObject *fragment, GError **error) {
    MIRAGE_CHECK_ARG(fragment);
    return __remove_fragment_from_track(self, fragment, error);
}

/**
 * mirage_track_get_fragment_by_index:
 * @self: a #MIRAGE_Track
 * @index: index of fragment to be retrieved
 * @fragment: location to store fragment, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves fragment by index. If @index is negative, fragments from the end of 
 * track are retrieved (e.g. -1 is for last track, -2 for second-to-last 
 * track, etc.). If @index is out of range, regardless of the sign, the 
 * function fails.
 * </para>
 *
 * <para>
 * A reference to fragment is stored in @fragment; it should be released with 
 * g_object_unref() when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_get_fragment_by_index (MIRAGE_Track *self, gint index, GObject **fragment, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    GObject *ret_fragment = NULL;
    gint num_fragments = 0;
    
    /* First fragment, last fragment... allow negative indexes to go from behind */
    mirage_track_get_number_of_fragments(self, &num_fragments, NULL);
    if (index < -num_fragments || index >= num_fragments) {
        mirage_error(MIRAGE_E_INDEXOUTOFRANGE, error);
        return FALSE;
    } else if (index < 0) {
        index += num_fragments; 
    }
    
    /* Get index-th item from list... */
    ret_fragment = g_list_nth_data(_priv->fragments_list, index);
    
    if (ret_fragment) {
        /* Return fragment to user if she wants it */
        if (fragment) {
            g_object_ref(ret_fragment);
            *fragment = ret_fragment;
        }
        return TRUE;
    }
    
    mirage_error(MIRAGE_E_FRAGMENTNOTFOUND, error);
    return FALSE;
}

/**
 * mirage_track_get_fragment_by_address:
 * @self: a #MIRAGE_Track
 * @address: address belonging to fragment to be retrieved
 * @fragment: location to store fragment, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves fragment by address. @address must be valid (track-relative) sector 
 * address that is part of the fragment to be retrieved (i.e. lying between fragment's 
 * start and end address).
 * </para>
 *
 * <para>
 * A reference to fragment is stored in @fragment; it should be released with 
 * g_object_unref() when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_get_fragment_by_address (MIRAGE_Track *self, gint address, GObject **fragment, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    GObject *ret_fragment = NULL;
    GList *entry = NULL;
    
    /* Go over all fragments */
    G_LIST_FOR_EACH(entry, _priv->fragments_list) {        
        gint cur_address = 0;
        gint cur_length  = 0;
        
        ret_fragment = entry->data;
        
        mirage_fragment_get_address(MIRAGE_FRAGMENT(ret_fragment), &cur_address, NULL);
        mirage_fragment_get_length(MIRAGE_FRAGMENT(ret_fragment), &cur_length, NULL);
        
        /* Break the loop if address lies within fragment boundaries */
        if (address >= cur_address && address < cur_address + cur_length) {
            break;
        } else {
            ret_fragment = NULL;
        }
    }
    
    /* If we didn't find anything... */
    if (!ret_fragment) {
        mirage_error(MIRAGE_E_FRAGMENTNOTFOUND, error);
        return FALSE;
    }
    
    /* Return fragment to user if she wants it */
    if (fragment) {
        g_object_ref(ret_fragment);
        *fragment = ret_fragment;
    }
    
    return TRUE;
}

/**
 * mirage_track_for_each_fragment:
 * @self: a #MIRAGE_Track
 * @func: callback function
 * @user_data: data to be passed to callback function
 * @error: location to store error, or %NULL
 *
 * <para>
 * Iterates over fragments list, calling @func for each fragment in the layout.
 * </para>
 *
 * <para>
 * If @func returns %FALSE, the function immediately returns %FALSE and @error 
 * is set to %MIRAGE_E_ITERCANCELLED.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_for_each_fragment (MIRAGE_Track *self, MIRAGE_CallbackFunction func, gpointer user_data, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    GList *entry = NULL;
    
    MIRAGE_CHECK_ARG(func);
    
    G_LIST_FOR_EACH(entry, _priv->fragments_list) {
        gboolean succeeded = (*func) ((entry->data), user_data);
        if (!succeeded) {
            mirage_error(MIRAGE_E_ITERCANCELLED, error);
            return FALSE;
        }
    }
    
    return TRUE;
}


/**
 * mirage_track_find_fragment_with_subchannel:
 * @self: a #MIRAGE_Track
 * @fragment: location to store fragment, or %NULL
 * @error: location to store error, or %NULL
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
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_find_fragment_with_subchannel (MIRAGE_Track *self, GObject **fragment, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    GObject *ret_fragment = NULL;
    GList *entry = NULL;
    
    /* Go over all fragments */
    G_LIST_FOR_EACH(entry, _priv->fragments_list) {        
        gint subchan_sectsize = 0;
        ret_fragment = entry->data;
        
        mirage_fragment_read_subchannel_data(MIRAGE_FRAGMENT(ret_fragment), 0, NULL, &subchan_sectsize, NULL);
        if (subchan_sectsize) {
            break;
        } else {
            ret_fragment = NULL;
        }
    }
    
    /* If we didn't find anything... */
    if (!ret_fragment) {
        mirage_error(MIRAGE_E_FRAGMENTNOTFOUND, error);
        return FALSE;
    }
    
    /* Return fragment to user if she wants it */
    if (fragment) {
        g_object_ref(ret_fragment);
        *fragment = ret_fragment;
    }
    
    return TRUE;
}


/**
 * mirage_track_set_track_start:
 * @self: a #MIRAGE_Track
 * @track_start: track start address
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets track start address. @track_start is a track-relative address at which track's
 * pregap ends and track "logically" starts (i.e. where index changes from 00 to 01). Note that
 * this is not the same as start address that is set by mirage_track_layout_set_start_sector();
 * that one sets the address at which track "physically" starts (i.e. where index 00 starts).
 * </para>
 * 
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_set_track_start (MIRAGE_Track *self, gint track_start, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    /* Set track start */
    _priv->track_start = track_start;
    
    return TRUE;
}

/**
 * mirage_track_get_track_start:
 * @self: a #MIRAGE_Track
 * @track_start: location to store track start address
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track start address. This is track-relative address at which pregap
 * ends and track "logically" starts (i.e. where index changes from 00 to 01).
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_get_track_start (MIRAGE_Track *self, gint *track_start, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(track_start);
    /* Return track start */
    *track_start = _priv->track_start;
    return TRUE;
}


/**
 * mirage_track_get_number_of_indices:
 * @self: a #MIRAGE_Track
 * @number_of_indices: location to store number of indices
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves number of indices the track contains. Note that this includes
 * only indices greater than 01.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_get_number_of_indices (MIRAGE_Track *self, gint *number_of_indices, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(number_of_indices);
    /* Return number of indices */
    *number_of_indices = g_list_length(_priv->indices_list); /* Length of list */
    return TRUE;
}

/**
 * mirage_track_add_index:
 * @self: a #MIRAGE_Track
 * @address: address at which the index is to be added
 * @index: pointer to #MIRAGE_Index, %NULL pointer or %NULL
 * @error: location to store error, or %NULL
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
 * If @index contains pointer to existing #MIRAGE_Index object, the object
 * is added to track. Otherwise, a new #MIRAGE_Index object is created. 
 * If @index contains a %NULL pointer, a reference to newly created object is stored
 * in it; it should be released with g_object_unref() when no longer needed. If @index
 * is %NULL, no reference is returned.
 * </para>
 *
 * <para>
 * If address falls before index 01 (i.e. if it's less than address that was set
 * using mirage_track_set_track_start()), the function fails.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_add_index (MIRAGE_Track *self, gint address, GObject **index, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    GObject *new_index = NULL;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_TRACK, "%s: address: 0x%X\n", __func__, address);
    
    /* Make sure we're not trying to put index before track start (which has index 1) */
    if (address < _priv->track_start) {
        mirage_error(MIRAGE_E_INVALIDARG, error);
        return FALSE;
    }
    
    /* If there's index provided, use it; else create new one */
    if (index && *index) {
        new_index = *index;
        /* If index is not MIRAGE_Index... */
        if (!MIRAGE_IS_INDEX(new_index)) {
            mirage_error(MIRAGE_E_INVALIDOBJTYPE, error);
            return FALSE;
        }
        g_object_ref(new_index);
    } else {
        new_index = g_object_new(MIRAGE_TYPE_INDEX, NULL);
    }
    
    /* Set index address */
    mirage_index_set_address(MIRAGE_INDEX(new_index), address, NULL);
    /* Set parent */
    mirage_object_set_parent(MIRAGE_OBJECT(new_index), G_OBJECT(self), NULL);
    /* Attach child */
    mirage_object_attach_child(MIRAGE_OBJECT(self), new_index, NULL);
    
    /* Insert index into indices list */
    _priv->indices_list = g_list_insert_sorted(_priv->indices_list, new_index, (GCompareFunc)__sort_indices_by_address);
    
    /* Rearrange indices; note that indices do *not* trigger a bottom-up change */
    __mirage_track_rearrange_indices(self, NULL);
    
    /* Return index to user if she wants it */
    if (index && (*index == NULL)) {
        g_object_ref(new_index);
        *index = new_index;
    }
    
    return TRUE;
}

/**
 * mirage_track_remove_index_by_number:
 * @self: a #MIRAGE_Track
 * @number: index number of index to be removed
 * @error: location to store error, or %NULL
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
gboolean mirage_track_remove_index_by_number (MIRAGE_Track *self, gint number, GError **error) {
    /*MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);*/
    GObject *index = NULL;
    gboolean succeeded = FALSE;
    
    /* Find index by number */
    if (!mirage_track_get_index_by_number(self, number, &index, error)) {
        return FALSE;
    }
    
    /* Remove index from list */
    succeeded = __remove_index_from_track(self, index, error);
    g_object_unref(index); /* This one's from get */
    
    return succeeded;
}

/**
 * mirage_track_remove_index_by_object:
 * @self: a #MIRAGE_Track
 * @index: index object to be removed
 * @error: location to store error, or %NULL
 *
 * <para>
 * Removes index from track.This causes index numbers of remaining indices to be readjusted.
 * </para>
 * 
 * <para>
 * @index is a #MIRAGE_Index object to be removed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_remove_index_by_object (MIRAGE_Track *self, GObject *index, GError **error) {
    MIRAGE_CHECK_ARG(index);
    return __remove_index_from_track(self, index, error);
}

/**
 * mirage_track_get_index_by_number:
 * @self: a #MIRAGE_Track
 * @number: index number of index to be retrieved
 * @index: location to store index, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves index by index number. If @number is negative, indices from the end of 
 * track are retrieved (e.g. -1 is for index, -2 for second-to-last index, etc.). 
 * If @number is out of range, regardless of the sign, the function fails.
 * </para>
 *
 * <para>
 * A reference to index is stored in @index; it should be released with 
 * g_object_unref() when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_get_index_by_number (MIRAGE_Track *self, gint number, GObject **index, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    GObject *ret_index = NULL;
    gint num_indices = 0;
    
    /* First index, last index... allow negative numbers to go from behind */
    mirage_track_get_number_of_indices(self, &num_indices, NULL);
    if (number < -num_indices || number >= num_indices) {
        mirage_error(MIRAGE_E_INDEXOUTOFRANGE, error);
        return FALSE;
    } else if (number < 0) {
        number += num_indices; 
    }
    
    /* Get index-th item from list... */
    ret_index = g_list_nth_data(_priv->indices_list, number);
    
    if (ret_index) {
        /* Return index to user if she wants it */
        if (index) {
            g_object_ref(ret_index);
            *index = ret_index;
        }
        return TRUE;
    }
    
    mirage_error(MIRAGE_E_INDEXNOTFOUND, error);
    return FALSE;
}

/**
 * mirage_track_get_index_by_address:
 * @self: a #MIRAGE_Track
 * @address: address belonging to index to be retrieved
 * @index: location to store index, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves index by address. @address must be valid (track-relative) sector 
 * address that is part of the index to be retrieved (i.e. lying between index's 
 * start and end sector).
 * </para>
 *
 * <para>
 * A reference to index is stored in @index; it should be released with 
 * g_object_unref() when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_get_index_by_address (MIRAGE_Track *self, gint address, GObject **index, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    GObject *ret_index = NULL;
    GList *entry = NULL;
    
    /* Go over all indices */
    G_LIST_FOR_EACH(entry, _priv->indices_list) {
        GObject *cur_index = NULL;
        gint cur_address = 0;
        
        cur_index = entry->data;
        
        mirage_index_get_address(MIRAGE_INDEX(cur_index), &cur_address, NULL);
        
        /* We return the last index whose address doesn't surpass requested address */
        if (cur_address <= address) {
            ret_index = cur_index;
        } else {
            break;
        }
    }
    
    /* If we didn't find anything... */
    if (!ret_index) {
        mirage_error(MIRAGE_E_INDEXNOTFOUND, error);
        return FALSE;
    }
    
    /* Return index to user if she wants it */
    if (index) {
        g_object_ref(ret_index);
        *index = ret_index;
    }
    
    return TRUE;
}

/**
 * mirage_track_for_each_index:
 * @self: a #MIRAGE_Track
 * @func: callback function
 * @user_data: user data to be passed to callback function
 * @error: location to store error, or %NULL
 *
 * <para>
 * Iterates over indices list, calling @func for each index.
 * </para>
 *
 * <para>
 * If @func returns %FALSE, the function immediately returns %FALSE and @error 
 * is set to %MIRAGE_E_ITERCANCELLED.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_for_each_index (MIRAGE_Track *self, MIRAGE_CallbackFunction func, gpointer user_data, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    GList *entry = NULL;
    
    MIRAGE_CHECK_ARG(func);
    
    G_LIST_FOR_EACH(entry, _priv->indices_list) {
        gboolean succeeded = (*func) (MIRAGE_INDEX(entry->data), user_data);
        if (!succeeded) {
            mirage_error(MIRAGE_E_ITERCANCELLED, error);
            return FALSE;
        }
    }
    
    return TRUE;
}



/**
 * mirage_track_get_number_of_languages:
 * @self: a #MIRAGE_Track
 * @number_of_languages: location to store number of languages
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves number of languages the track contains.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_get_number_of_languages (MIRAGE_Track *self, gint *number_of_languages, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(number_of_languages);
    /* Return number of languages */
    *number_of_languages = g_list_length(_priv->languages_list); /* Length of list */
    return TRUE;
}

/**
 * mirage_track_add_language:
 * @self: a #MIRAGE_Track
 * @langcode: language code for the added language
 * @language: pointer to #MIRAGE_Language, %NULL pointer or %NULL
 * @error: location to store error, or %NULL
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
gboolean mirage_track_add_language (MIRAGE_Track *self, gint langcode, GObject **language, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    GObject *new_language = NULL;
    
    /* Check if language already exists */
    if (mirage_track_get_language_by_code(self, langcode, NULL, NULL)) {
        mirage_error(MIRAGE_E_LANGEXISTS, error);
        return FALSE;
    }
    
    /* If there's language provided, use it; else create new one */
    if (language && *language) {
        new_language = *language;
        /* If language is not MIRAGE_Language... */
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
    
    /* Insert language to language list */
    _priv->languages_list = g_list_insert_sorted(_priv->languages_list, new_language, (GCompareFunc)__sort_languages_by_code);
    
    /* Return language to user if she wants it */
    if (language && (*language == NULL)) {
        g_object_ref(new_language);
        *language = new_language;
    }
    
    return TRUE;
}

/**
 * mirage_track_remove_language_by_index:
 * @self: a #MIRAGE_Track
 * @index: index of language to be removed
 * @error: location to store error, or %NULL
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
gboolean mirage_track_remove_language_by_index (MIRAGE_Track *self, gint index, GError **error) {
    /*MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);*/
    GObject *language = NULL;
    gboolean succeeded = FALSE;
    
    /* Find track by index */
    if (!mirage_track_get_language_by_index(self, index, &language, error)) {
        return FALSE;
    }
    
    /* Remove track from list */
    succeeded = __remove_language_from_track(self, language, error);
    g_object_unref(language); /* This one's from get */
    
    return succeeded;
}

/**
 * mirage_track_remove_language_by_code:
 * @self: a #MIRAGE_Track
 * @langcode: language code of language to be removed
 * @error: location to store error, or %NULL
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
gboolean mirage_track_remove_language_by_code (MIRAGE_Track *self, gint langcode, GError **error) {
    /*MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);*/
    GObject *language = NULL;
    gboolean succeeded = FALSE;
    
    /* Find session in layout */
    if (!mirage_track_get_language_by_code(self, langcode, &language, error)) {
        return FALSE;
    }
        
    /* Remove track from list */
    succeeded = __remove_language_from_track(self, language, error);
    g_object_unref(language); /* This one's from get */
    
    return succeeded;
}

/**
 * mirage_track_remove_language_by_object:
 * @self: a #MIRAGE_Track
 * @language: language object to be removed
 * @error: location to store error, or %NULL
 *
 * <para>
 * Removes language from track.
 * </para>
 *
 * <para>
 * @language is a #MIRAGE_Language object to be removed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_remove_language_by_object (MIRAGE_Track *self, GObject *language, GError **error) {
    MIRAGE_CHECK_ARG(language);
    return __remove_language_from_track(self, language, error);
}

/**
 * mirage_track_get_language_by_index:
 * @self: a #MIRAGE_Track
 * @index: index of language to be retrieved
 * @language: location to store language, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves language by index. If @index is negative, languages from the end of 
 * track are retrieved (e.g. -1 is for last language, -2 for second-to-last 
 * language, etc.). If @index is out of range, regardless of the sign, the 
 * function fails.
 * </para>
 *
 * <para>
 * A reference to language is stored in @language; it should be released with 
 * g_object_unref() when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_get_language_by_index (MIRAGE_Track *self, gint index, GObject **language, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    GObject *ret_language = NULL;
    gint num_languages = 0;
    
    /* First language, last language... allow negative indexes to go from behind */
    mirage_track_get_number_of_languages(self, &num_languages, NULL);
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
 * mirage_track_get_language_by_code:
 * @self: a #MIRAGE_Track
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
gboolean mirage_track_get_language_by_code (MIRAGE_Track *self, gint langcode, GObject **language, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
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
 * mirage_track_for_each_language:
 * @self: a #MIRAGE_Track
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
gboolean mirage_track_for_each_language (MIRAGE_Track *self, MIRAGE_CallbackFunction func, gpointer user_data, GError **error) {
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
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


/**
 * mirage_track_get_prev:
 * @self: a #MIRAGE_Track
 * @prev_track: location to store previous track, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track that is placed before @self in session layout. A reference 
 * to track is stored in @prev_track; it should be released with g_object_unref() 
 * when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_get_prev (MIRAGE_Track *self, GObject **prev_track, GError **error) {
    GObject *session = NULL;
    gboolean succeeded = TRUE;
    
    /* Get parent session */
    if (!mirage_object_get_parent(MIRAGE_OBJECT(self), &session, error)) {
        return FALSE;
    }
    
    succeeded = mirage_session_get_track_before(MIRAGE_SESSION(session), G_OBJECT(self), prev_track, error);
    g_object_unref(session);
    
    return succeeded;
}

/**
 * mirage_track_get_next:
 * @self: a #MIRAGE_Track
 * @next_track: location to store next track, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves track that is placed after @self in session layout. A reference 
 * to track is stored in @next_track; it should be released with g_object_unref() 
 * when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_track_get_next (MIRAGE_Track *self, GObject **next_track, GError **error) {
    GObject *session = NULL;
    gboolean succeeded = TRUE;
    
    /* Get parent session */
    if (!mirage_object_get_parent(MIRAGE_OBJECT(self), &session, error)) {
        return FALSE;
    }
    
    succeeded = mirage_session_get_track_after(MIRAGE_SESSION(session), G_OBJECT(self), next_track, error);
    g_object_unref(session);
    
    return succeeded;
}


/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static GObjectClass *parent_class = NULL;

static void __mirage_track_instance_init (GTypeInstance *instance, gpointer g_class) {
    MIRAGE_Track *self = MIRAGE_TRACK(instance);
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    
    _priv->track_number = 1;
    
    return;
}


static void __mirage_track_finalize (GObject *obj) {
    MIRAGE_Track *self = MIRAGE_TRACK(obj);
    MIRAGE_TrackPrivate *_priv = MIRAGE_TRACK_GET_PRIVATE(self);
    GList *entry = NULL;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s:\n", __func__);
    
    /* Free list of fragments */
    G_LIST_FOR_EACH(entry, _priv->fragments_list) {        
        if (entry->data) {
            GObject *fragment = entry->data;
            /* Disconnect signal handler and unref */
            g_signal_handlers_disconnect_by_func(MIRAGE_OBJECT(fragment), __fragment_modified_handler, self);
            g_object_unref(fragment);
        }
    }
    g_list_free(_priv->fragments_list);
    
    /* Free list of indices */
    G_LIST_FOR_EACH(entry, _priv->indices_list) {
        if (entry->data) {
            GObject *index = entry->data;
            g_object_unref(index);
        }
    }
    g_list_free(_priv->indices_list);
    
    /* Free list of languages */
    G_LIST_FOR_EACH(entry, _priv->languages_list) {
        if (entry->data) {
            GObject *language = entry->data;            
            g_object_unref(language);
        }
    }
    g_list_free(_priv->languages_list);
    
    g_free(_priv->isrc);
        
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __func__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __mirage_track_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_TrackClass *klass = MIRAGE_TRACK_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_TrackPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_track_finalize;

    return;
}

GType mirage_track_get_type (void) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_TrackClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_track_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Track),
            0,      /* n_preallocs */
            __mirage_track_instance_init    /* instance_init */
        };
        
        type = g_type_register_static(MIRAGE_TYPE_OBJECT, "MIRAGE_Track", &info, 0);
    }
    
    return type;
}
