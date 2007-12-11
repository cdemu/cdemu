/*
 *  libMirage: Sector object
 *  Copyright (C) 2006-2007 Rok Mandeljc
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
#define MIRAGE_SECTOR_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_SECTOR, MIRAGE_SectorPrivate))

typedef struct {    
    gint type;
    gint address;
    
    gint valid_data;          /* Which parts of sector data are valid */
    guint8 sector_data[2352]; /* Buffer for sector data */
    guint8 subchan_pw[96];    /* Buffer for interleaved PW subchannel */
    guint8 subchan_pq[16];    /* Buffer for deinterleaved PQ subchannel */
} MIRAGE_SectorPrivate;


/******************************************************************************\
 *                              Private functions                             *
\******************************************************************************/
static void __mirage_sector_generate_subchannel (MIRAGE_Sector *self);

static void __mirage_sector_generate_sync (MIRAGE_Sector *self) {
    MIRAGE_SectorPrivate *_priv = MIRAGE_SECTOR_GET_PRIVATE(self);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: generating sync\n", __func__);

    switch (_priv->type) {
        case MIRAGE_MODE_MODE0:
        case MIRAGE_MODE_MODE1:
        case MIRAGE_MODE_MODE2:
        case MIRAGE_MODE_MODE2_FORM1:
        case MIRAGE_MODE_MODE2_FORM2: {
            guint8 sync[12] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
            memcpy(_priv->sector_data, sync, 12);
            break;
        }
        default: {
            return;
        }
    }
    
    _priv->valid_data |= MIRAGE_VALID_SYNC;
    return;
}

static void __mirage_sector_generate_header (MIRAGE_Sector *self) {
    MIRAGE_SectorPrivate *_priv = MIRAGE_SECTOR_GET_PRIVATE(self);

    guint8 *head = _priv->sector_data+12;
    gint start_sector = 0;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: generating header\n", __func__);
    
    /* We need to convert track-relative address into disc-relative one */
    GObject *track = NULL;
    
    if (!mirage_object_get_parent(MIRAGE_OBJECT(self), &track, NULL)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get sector's parent!\n", __func__);
        return;
    }
    mirage_track_layout_get_start_sector(MIRAGE_TRACK(track), &start_sector, NULL);
    g_object_unref(track);
    
    /* Address */
    mirage_helper_lba2msf(_priv->address + start_sector, TRUE, &head[0], &head[1], &head[2]);
    head[0] = mirage_helper_hex2bcd(head[0]);
    head[1] = mirage_helper_hex2bcd(head[1]);
    head[2] = mirage_helper_hex2bcd(head[2]);
    _priv->valid_data |= MIRAGE_VALID_HEADER;
    
    /* Set mode */
    switch (_priv->type) {
        case MIRAGE_MODE_MODE0: {
            head[3] = 0; /* Mode = 0 */
            break;
        }
        case MIRAGE_MODE_MODE1: {
            head[3] = 1; /* Mode = 1 */
            break;
        }
        case MIRAGE_MODE_MODE2:
        case MIRAGE_MODE_MODE2_FORM1:
        case MIRAGE_MODE_MODE2_FORM2: {
            head[3] = 2; /* Mode = 2 */
            break;
        }
        default: {
            return;
        }
    }
    
    return;
}

static void __mirage_sector_generate_subheader (MIRAGE_Sector *self) {
    MIRAGE_SectorPrivate *_priv = MIRAGE_SECTOR_GET_PRIVATE(self);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: generating subheader\n", __func__);
    
    switch (_priv->type) {
        case MIRAGE_MODE_MODE2_FORM1: {
            guint8 *subhead = _priv->sector_data+16;
            subhead[2] |= (0 << 5); /* Form 1 */
            subhead[5] = subhead[2];
            break;            
        }
        case MIRAGE_MODE_MODE2_FORM2: {
            guint8 *subhead = _priv->sector_data+16;
            subhead[2] |= (1 << 5); /* Form 2 */
            subhead[5] = subhead[2];
            break;
        }
        default: {
            return;
        }
    }
    
    _priv->valid_data |= MIRAGE_VALID_SUBHEADER;
    
    return;
}

static void __mirage_sector_generate_edc_ecc (MIRAGE_Sector *self) {
    MIRAGE_SectorPrivate *_priv = MIRAGE_SECTOR_GET_PRIVATE(self);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: generating EDC/ECC\n", __func__);
    
    switch (_priv->type) {
        case MIRAGE_MODE_MODE1: {
            /* EDC/ECC are generated over sync, header and data in Mode 1 sectors... 
               so make sure we have those */
            if (!(_priv->valid_data & MIRAGE_VALID_SYNC)) {
                __mirage_sector_generate_sync(self);
            }
            if (!(_priv->valid_data & MIRAGE_VALID_HEADER)) {
                __mirage_sector_generate_header(self);
            }
            
            /* Generate EDC */
            mirage_helper_sector_edc_ecc_compute_edc_block(_priv->sector_data+0x00, 0x810, _priv->sector_data+0x810);
            /* Generate ECC P/Q codes */
            mirage_helper_sector_edc_ecc_compute_ecc_block(_priv->sector_data+0xC, 86, 24, 2, 86, _priv->sector_data+0x81C); /* P */
            mirage_helper_sector_edc_ecc_compute_ecc_block(_priv->sector_data+0xC, 52, 43, 86, 88, _priv->sector_data+0x8C8); /* Q */
            
            break;
        }
        case MIRAGE_MODE_MODE2_FORM1: {
            guint8 tmp_header[4];
            /* Zero the header, because it is not supposed to be included in the 
               calculation; copy, calculate, then copy back */
            memcpy(tmp_header, _priv->sector_data+12, CD_HEAD_SIZE);
            memset(_priv->sector_data+12, 0, CD_HEAD_SIZE);
            /* Generate EDC */
            mirage_helper_sector_edc_ecc_compute_edc_block(_priv->sector_data+0x10, 0x808, _priv->sector_data+0x818);
            /* Generate ECC P/Q codes */
            mirage_helper_sector_edc_ecc_compute_ecc_block(_priv->sector_data+0xC, 86, 24, 2, 86, _priv->sector_data+0x81C); /* P */
            mirage_helper_sector_edc_ecc_compute_ecc_block(_priv->sector_data+0xC, 52, 43, 86, 88, _priv->sector_data+0x8C8); /* Q */
            /* Unzero */
            memcpy(_priv->sector_data+12, tmp_header, CD_HEAD_SIZE);
            
            break;            
        }
        case MIRAGE_MODE_MODE2_FORM2: {
            /* Compute EDC */
            mirage_helper_sector_edc_ecc_compute_edc_block(_priv->sector_data+0x10, 0x91C, _priv->sector_data+0x92C);
            
            break;
        }
        default: {
            return;
        }
    }
    
    _priv->valid_data |= MIRAGE_VALID_EDC_ECC;
    
    return;
}



/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
/**
 * mirage_sector_feed_data:
 * @self: a #MIRAGE_Sector
 * @address: address the sector represents
 * @track: track the sector belongs to
 * @error: location to store error, or %NULL
 *
 * <para>
 * Feeds data to sector. It finds appropriate fragment to feed from, reads data
 * into sector object and sets data validity flags.
 * </para>
 *
 * <note>
 * Intended for internal use.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_sector_feed_data (MIRAGE_Sector *self, gint address, GObject *track, GError **error) {
    MIRAGE_SectorPrivate *_priv = MIRAGE_SECTOR_GET_PRIVATE(self);

    GObject *data_fragment = NULL;
    
    gint mode = 0;
    gint sectsize = 0;
    gint data_offset = 0;
    gint fragment_start = 0;
    
    /* Get track mode */
    mirage_track_get_mode(MIRAGE_TRACK(track), &mode, NULL);
    /* Set track as sector's parent */
    mirage_object_set_parent(MIRAGE_OBJECT(self), track, NULL);
    /* Store sector's address */
    _priv->address = address;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: feeding data for sector 0x%X\n", __func__, _priv->address);
    
    /* Get data fragment to feed from */
    if (!mirage_track_get_fragment_by_address(MIRAGE_TRACK(track), address, &data_fragment, error)) {
        return FALSE;
    }
    
    /* Fragments work with fragment-relative addresses */
    mirage_fragment_get_address(MIRAGE_FRAGMENT(data_fragment), &fragment_start, NULL);
    address -= fragment_start;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: got fragment for track-relative address 0x%X... %p\n", __func__, address, data_fragment);

    /* *** Main channel data ***/
    
    /* Get sector size by performing 'empty' read */
    if (!mirage_fragment_read_main_data(MIRAGE_FRAGMENT(data_fragment), address, NULL, &sectsize, error)) {
        g_object_unref(data_fragment);
        return FALSE;
    }
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: main channel sector size: %d\n", __func__, sectsize);

    /* Now, calculate offset and valid data based on mode and sector size */
    switch (mode) {
        case MIRAGE_MODE_AUDIO: {
            /* Data offset: 0, because it should always be 2352 */
            
            /* Valid data: Data should always be valid */
            _priv->valid_data |= MIRAGE_VALID_DATA;
            
            break;
        }
        case MIRAGE_MODE_MODE0: {
            /* Data offset */
            if (sectsize == 2336) {
                data_offset = CD_SYNC_SIZE + CD_HEAD_SIZE;
            }
            
            /* Set valid data */
            if (sectsize >= 2336) {
                _priv->valid_data |= MIRAGE_VALID_DATA;
            }
            if (sectsize >= 2352) {
                _priv->valid_data |= MIRAGE_VALID_SYNC;
                _priv->valid_data |= MIRAGE_VALID_HEADER;
            }
            
            break;
        }
        case MIRAGE_MODE_MODE1: {
            /* Data offset */
            if (sectsize == 2048) {
                data_offset = CD_SYNC_SIZE + CD_HEAD_SIZE;
            }
            
            /* Valid data */
            if (sectsize >= 2048) {
                _priv->valid_data |= MIRAGE_VALID_DATA;
            }
            if (sectsize >= 2352) {
                _priv->valid_data |= MIRAGE_VALID_SYNC;
                _priv->valid_data |= MIRAGE_VALID_HEADER;
                _priv->valid_data |= MIRAGE_VALID_EDC_ECC;
            }
            
            break;
        }
        case MIRAGE_MODE_MODE2: {
            /* Data offset */
            if (sectsize == 2336) {
                data_offset = CD_SYNC_SIZE + CD_HEAD_SIZE;
            }
            
            /* Valid data */
            if (sectsize >= 2336) {
                _priv->valid_data |= MIRAGE_VALID_DATA;
            }
            if (sectsize >= 2352) {
                _priv->valid_data |= MIRAGE_VALID_SYNC;
                _priv->valid_data |= MIRAGE_VALID_HEADER;
            }
            
            break;
        }
        case MIRAGE_MODE_MODE2_FORM1: {
            /* Data offset */
            if (sectsize == 2048) {
                data_offset = CD_SYNC_SIZE + CD_HEAD_SIZE + CD_SUBHEAD_SIZE;
            } else if (sectsize == 2336) {
                data_offset = CD_SYNC_SIZE + CD_HEAD_SIZE;
            }
            
            /* Valid data */
            if (sectsize >= 2048) {
                _priv->valid_data |= MIRAGE_VALID_DATA;
            }
            if (sectsize >= 2336) {
                _priv->valid_data |= MIRAGE_VALID_SUBHEADER;
                _priv->valid_data |= MIRAGE_VALID_EDC_ECC;
            }
            if (sectsize >= 2352) {
                _priv->valid_data |= MIRAGE_VALID_SYNC;
                _priv->valid_data |= MIRAGE_VALID_HEADER;
            }
    
            break;
        }
        case MIRAGE_MODE_MODE2_FORM2: {
            /* Data offset */
            if (sectsize == 2324) {
                data_offset = CD_SYNC_SIZE + CD_HEAD_SIZE + CD_SUBHEAD_SIZE;
            } else if (sectsize == 2336) {
                data_offset = CD_SYNC_SIZE + CD_HEAD_SIZE;
            }
            
            /* Valid data */
            if (sectsize >= 2324) {
                _priv->valid_data |= MIRAGE_VALID_DATA;
            }
            if (sectsize >= 2336) {
                _priv->valid_data |= MIRAGE_VALID_SUBHEADER;
                _priv->valid_data |= MIRAGE_VALID_EDC_ECC;
            }
            if (sectsize >= 2352) {
                _priv->valid_data |= MIRAGE_VALID_SYNC;
                _priv->valid_data |= MIRAGE_VALID_HEADER;
            }
            
            break;
        }
        case MIRAGE_MODE_MODE2_MIXED: {
            /* Data offset */
            if (sectsize == 2336) {
                data_offset = CD_SYNC_SIZE + CD_HEAD_SIZE;
            }
            
            /* Valid data */
            if (sectsize >= 2336) {
                _priv->valid_data |= MIRAGE_VALID_DATA;
                _priv->valid_data |= MIRAGE_VALID_SUBHEADER;
                _priv->valid_data |= MIRAGE_VALID_EDC_ECC;
            }
            if (sectsize >= 2352) {
                _priv->valid_data |= MIRAGE_VALID_SYNC;
                _priv->valid_data |= MIRAGE_VALID_HEADER;
            }
            
            break;
        }
    }
    
    /* Read */
    if (!mirage_fragment_read_main_data(MIRAGE_FRAGMENT(data_fragment), address, _priv->sector_data+data_offset, &sectsize, error)) {
        g_object_unref(data_fragment);
        return FALSE;
    }
    
    /* Now, if we had Mode 2 Mixed, we can determine whether we have 
       Mode 2 Form 1 or Mode 2 Form 2 */
    if (mode == MIRAGE_MODE_MODE2_MIXED) {
        /* Check the subheader... */
        if (_priv->sector_data[18] & 0x20) {
            mode = MIRAGE_MODE_MODE2_FORM2;
        } else {
            mode = MIRAGE_MODE_MODE2_FORM1;
        }
    }
    /* Set sector type */
    _priv->type = mode;
    
    /* *** Subchannel *** */
    /* Read subchannel... fragment should *always* return us 96-byte interleaved
       PW subchannel (or nothing) */
    if (!mirage_fragment_read_subchannel_data(MIRAGE_FRAGMENT(data_fragment), address, _priv->subchan_pw, &sectsize, error)) {
        g_object_unref(data_fragment);
        return FALSE;
    }
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: subchannel sector size: %d\n", __func__, sectsize);

    if (sectsize) {
        _priv->valid_data |= MIRAGE_VALID_SUBCHAN;
    }
    
    g_object_unref(data_fragment);

    return TRUE;
}


/**
 * mirage_sector_get_sector_type:
 * @self: a #MIRAGE_Sector
 * @type: location to store sector type
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves sector type (track mode); one of #MIRAGE_TrackModes.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_sector_get_sector_type (MIRAGE_Sector *self, gint *type, GError **error) {
    MIRAGE_SectorPrivate *_priv = MIRAGE_SECTOR_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(type);
    /* Return sector type */
    *type = _priv->type;
    return TRUE;
}

/**
 * mirage_sector_get_sync:
 * @self: a #MIRAGE_Sector
 * @ret_buf: location to store pointer to buffer containing sync pattern, or %NULL
 * @ret_len: location to store length of sync pattern, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves sector's sync pattern. The pointer to appropriate location in
 * sector's data buffer is stored into @ret_buf; as such, it should not be freed.
 * </para>
 *
 * <para>
 * If sync pattern is not provided by image file(s), it is generated.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_sector_get_sync (MIRAGE_Sector *self, guint8 **ret_buf, gint *ret_len, GError **error) {
    MIRAGE_SectorPrivate *_priv = MIRAGE_SECTOR_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    guint8 *buf = NULL;
    gint len = 0;
    
    /* Generate sync if it's not provided; generation routine takes care of 
       incompatible sector types */
    if (!(_priv->valid_data & MIRAGE_VALID_SYNC)) {
        __mirage_sector_generate_sync(self);
    }
    
    /* Sync is supported by all non-audio sectors */    
    switch (_priv->type) {
        case MIRAGE_MODE_MODE0:
        case MIRAGE_MODE_MODE1:
        case MIRAGE_MODE_MODE2:
        case MIRAGE_MODE_MODE2_FORM1:
        case MIRAGE_MODE_MODE2_FORM2: {
            buf = _priv->sector_data;
            len = 12;
            break;
        }
        default: {
            mirage_error(MIRAGE_E_SECTORTYPE, error);
            succeeded = FALSE;
            break;
        }
    }
    
    /* Return the requested data */
    if (ret_buf) {
        *ret_buf = buf;
    }
    if (ret_len) {
        *ret_len = len;
    }
    
    return succeeded;
}

/**
 * mirage_sector_get_header:
 * @self: a #MIRAGE_Sector
 * @ret_buf: location to store pointer to buffer containing header, or %NULL
 * @ret_len: location to store length of header, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves sector's header. The pointer to appropriate location in
 * sector's data buffer is stored into @ret_buf; as such, it should not be freed.
 * </para>
 *
 * <para>
 * If header is not provided by image file(s), it is generated.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_sector_get_header (MIRAGE_Sector *self, guint8 **ret_buf, gint *ret_len, GError **error) {
    MIRAGE_SectorPrivate *_priv = MIRAGE_SECTOR_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    guint8 *buf = NULL;
    gint len = 0;
    
    /* Generate header if it's not provided; generation routine takes care of 
       incompatible sector types */
    if (!(_priv->valid_data & MIRAGE_VALID_HEADER)) {
        __mirage_sector_generate_header(self);
    }
    
    /* Header is supported by all non-audio sectors */    
    switch (_priv->type) {
        case MIRAGE_MODE_MODE0:
        case MIRAGE_MODE_MODE1:
        case MIRAGE_MODE_MODE2:
        case MIRAGE_MODE_MODE2_FORM1:
        case MIRAGE_MODE_MODE2_FORM2: {
            buf = _priv->sector_data+12;
            len = 4;
            break;
        }
        default: {
            mirage_error(MIRAGE_E_SECTORTYPE, error);
            succeeded = FALSE;
            break;
        }
    }
    
    /* Return the requested data */
    if (ret_buf) {
        *ret_buf = buf;
    }
    if (ret_len) {
        *ret_len = len;
    }
    
    return succeeded;
}

/**
 * mirage_sector_get_subheader:
 * @self: a #MIRAGE_Sector
 * @ret_buf: location to store pointer to buffer containing subheader, or %NULL
 * @ret_len: location to store length of subheader, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves sector's subheader. The pointer to appropriate location in
 * sector's data buffer is stored into @ret_buf; as such, it should not be freed.
 * </para>
 *
 * <para>
 * If subheader is not provided by image file(s), it is generated.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_sector_get_subheader (MIRAGE_Sector *self, guint8 **ret_buf, gint *ret_len, GError **error) {
    MIRAGE_SectorPrivate *_priv = MIRAGE_SECTOR_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    guint8 *buf = NULL;
    gint len = 0;
    
    /* Generate subheader if it's not provided; generation routine takes care of 
       incompatible sector types */
    if (!(_priv->valid_data & MIRAGE_VALID_SUBHEADER)) {
        __mirage_sector_generate_subheader(self);
    }
    
    /* Subheader is supported by formed Mode 2 sectors */
    switch (_priv->type) {
        case MIRAGE_MODE_MODE2_FORM1:
        case MIRAGE_MODE_MODE2_FORM2: {
            buf = _priv->sector_data+16;
            len = 8;
            break;
        }
        default: {
            mirage_error(MIRAGE_E_SECTORTYPE, error);
            succeeded = FALSE;
            break;
        }
    }
    
    /* Return the requested data */
    if (ret_buf) {
        *ret_buf = buf;
    }
    if (ret_len) {
        *ret_len = len;
    }
    
    return succeeded;
}

/**
 * mirage_sector_get_data:
 * @self: a #MIRAGE_Sector
 * @ret_buf: location to store pointer to buffer containing user data, or %NULL
 * @ret_len: location to store length of user data, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves sector's user data. The pointer to appropriate location in
 * sector's data buffer is stored into @ret_buf; as such, it should not be freed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_sector_get_data (MIRAGE_Sector *self, guint8 **ret_buf, gint *ret_len, GError **error) {
    MIRAGE_SectorPrivate *_priv = MIRAGE_SECTOR_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    guint8 *buf = NULL;
    gint len = 0;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: sector type: %d\n", __func__, _priv->type);
    
    /* Data is supported by all sectors */
    switch (_priv->type) {
        case MIRAGE_MODE_AUDIO: {
            buf = _priv->sector_data;
            len = 2352;
            break;
        }
        case MIRAGE_MODE_MODE0: {
            buf = _priv->sector_data+16;
            len = 2336;
            break;
        }
        case MIRAGE_MODE_MODE1: {
            buf = _priv->sector_data+16;
            len = 2048;
            break;
        }
        case MIRAGE_MODE_MODE2: {
            buf = _priv->sector_data+16;
            len = 2336;
            break;
        }
        case MIRAGE_MODE_MODE2_FORM1: {
            buf = _priv->sector_data+24;
            len = 2048;
            break;
        }
        case MIRAGE_MODE_MODE2_FORM2: {
            buf = _priv->sector_data+24;
            len = 2324;
            break;
        }
        default: {
            mirage_error(MIRAGE_E_SECTORTYPE, error);
            succeeded = FALSE;
            break;
        }
    }
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: offset: %d length: %d\n", __func__, buf - _priv->sector_data, len);

    /* Return the requested data */
    if (ret_buf) {
        *ret_buf = buf;
    }
    if (ret_len) {
        *ret_len = len;
    }
    
    return succeeded;
}

/**
 * mirage_sector_get_edc_ecc:
 * @self: a #MIRAGE_Sector
 * @ret_buf: location to store pointer to buffer containing EDC/ECC data, or %NULL
 * @ret_len: location to store length of EDC/ECC data, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves sector's EDC/ECC data. The pointer to appropriate location in
 * sector's data buffer is stored into @ret_buf; as such, it should not be freed.
 * </para>
 *
 * <para>
 * If EDC/ECC data is not provided by image file(s), it is generated.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_sector_get_edc_ecc (MIRAGE_Sector *self, guint8 **ret_buf, gint *ret_len, GError **error) {
    MIRAGE_SectorPrivate *_priv = MIRAGE_SECTOR_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    guint8 *buf = NULL;
    gint len = 0;
    
    /* Generate EDC/ECC if it's not provided; generation routine takes care of 
       incompatible sector types */
    if (!(_priv->valid_data & MIRAGE_VALID_EDC_ECC)) {
        __mirage_sector_generate_edc_ecc(self);
    }
    
    /* EDC/ECC is supported by Mode 1 and formed Mode 2 sectors */    
    switch (_priv->type) {
        case MIRAGE_MODE_MODE1: {
            buf = _priv->sector_data+2064;
            len = 288;
            break;
        }
        case MIRAGE_MODE_MODE2_FORM1: {
            buf = _priv->sector_data+2072;
            len = 280;
            break;
        }
        case MIRAGE_MODE_MODE2_FORM2: {
            buf = _priv->sector_data+2324;
            len = 4;
            break;
        }
        default: {
            mirage_error(MIRAGE_E_SECTORTYPE, error);
            succeeded = FALSE;
            break;
        }
    }
    
    /* Return the requested data */
    if (ret_buf) {
        *ret_buf = buf;
    }
    if (ret_len) {
        *ret_len = len;
    }
    
    return succeeded;
}

/**
 * mirage_sector_get_subchannel:
 * @self: a #MIRAGE_Sector
 * @format: subchannel format
 * @ret_buf: location to store pointer to buffer containing subchannel, or %NULL
 * @ret_len: location to store length of subchannel data, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves sector's subchannel. @type must be one of #MIRAGE_Sector_SubchannelFormat.
 * The pointer to appropriate location in sector's data buffer is stored into 
 * @ret_buf; as such, it should not be freed.
 * </para>
 *
 * <para>
 * If subchannel is not provided by image file(s), it is generated.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_sector_get_subchannel (MIRAGE_Sector *self, gint format, guint8 **ret_buf, gint *ret_len, GError **error) {
    MIRAGE_SectorPrivate *_priv = MIRAGE_SECTOR_GET_PRIVATE(self);
    
    /* Generate subchannel if it's not provided */
    if (!(_priv->valid_data & MIRAGE_VALID_SUBCHAN)) {
        __mirage_sector_generate_subchannel(self);
    }
    
    switch (format) {
        case MIRAGE_SUBCHANNEL_PW: {
            /* Interleaved PW subchannel */
            if (ret_buf) {
                *ret_buf = _priv->subchan_pw;
            }
            if (ret_len) {
                *ret_len = 96;
            }
            break;
        }
        case MIRAGE_SUBCHANNEL_PQ: {
            /* De-interleaved PQ subchannel */
            mirage_helper_subchannel_deinterleave(SUBCHANNEL_Q, _priv->subchan_pw, _priv->subchan_pq);
            if (ret_buf) {
                *ret_buf = _priv->subchan_pq;
            }
            if (ret_len) {
                *ret_len = 16;
            }
            break;
        }
        case MIRAGE_SUBCHANNEL_RW: {
            /* FIXME: Cooked RW subchannel; can't do yet */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: cooked RW subchannel not supported yet!\n", __func__);
            mirage_error(MIRAGE_E_NOTIMPL, error);
            return FALSE;
        }
    }
    
    return TRUE;
}


/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static MIRAGE_ObjectClass *parent_class = NULL;

static void __mirage_sector_instance_init (GTypeInstance *instance, gpointer g_class) {
    MIRAGE_Sector *self = MIRAGE_SECTOR(instance);
    MIRAGE_SectorPrivate *_priv = MIRAGE_SECTOR_GET_PRIVATE(self);
    
    /* Un-initialize sector type */
    _priv->type = 0xDEADBEEF;
    
    return;
}

static void __mirage_sector_class_init (gpointer g_class, gpointer g_class_data) {
    MIRAGE_SectorClass *klass = MIRAGE_SECTOR_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_SectorPrivate));
    
    return;
}

GType mirage_sector_get_type (void) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_SectorClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_sector_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Sector),
            0,      /* n_preallocs */
            __mirage_sector_instance_init    /* instance_init */
        };
        
        type = g_type_register_static(MIRAGE_TYPE_OBJECT, "MIRAGE_Sector", &info, 0);
    }
    
    return type;
}

/******************************************************************************\
 *                            Subchannel generation                           *
\******************************************************************************/
static gint __subchannel_generate_p (MIRAGE_Sector *self, guint8 *buf) {
    MIRAGE_SectorPrivate *_priv = MIRAGE_SECTOR_GET_PRIVATE(self);
    gint address = _priv->address;
    
    /* P subchannel being 1 indicates we're in the pregap */
    if (address < 0) {
        memset(buf, 1, 12);
    } else {
        memset(buf, 0, 12);        
    }
    return 12;
}

static gint __subchannel_generate_q (MIRAGE_Sector *self, guint8 *buf) {
    MIRAGE_SectorPrivate *_priv = MIRAGE_SECTOR_GET_PRIVATE(self);
    gint address = _priv->address;

    GObject *track = NULL;
    
    gint mode_switch = 0;
    gint start_sector = 0;
    guint16 crc = 0;
    
    /* Get sector's parent track */
    if (!mirage_object_get_parent(MIRAGE_OBJECT(self), &track, NULL)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get sector's parent!\n", __func__);
        return 12;
    }
    
    /* We support Mode-1, Mode-2 and Mode-3 Q; according to INF8090 and MMC-3,
       "if used, they shall exist in at least one out of 100 consecutive sectors".
       So we put MCN in every 25th sector and ISRC in every 50th sector */
    
    /* Track number, index, absolute and relative track adresses are converted
       from HEX to BCD */
    
    switch (address % 100) {
        case 25: {
            /* MCN is to be returned; check if we actually have it */
            GObject *session = NULL;
            GObject *disc = NULL;
            
            mirage_object_get_parent(MIRAGE_OBJECT(track), &session, NULL);
            mirage_object_get_parent(MIRAGE_OBJECT(session), &disc, NULL);
            
            if (!mirage_disc_get_mcn(MIRAGE_DISC(disc), NULL, NULL)) {
                mode_switch = 0x01;
            } else {
                mode_switch = 0x02;
            }
            
            g_object_unref(disc);
            g_object_unref(session);
            
            break;
        }
        case 50: {
            /* ISRC is to be returned; verify that this is an audio track and 
               that it actually has ISRC set */
            gint mode = 0;
            
            mirage_sector_get_sector_type(self, &mode, NULL);
            if (mode != MIRAGE_MODE_AUDIO) {
                mode_switch = 0x01;
            } else if (!mirage_track_get_isrc(MIRAGE_TRACK(track), NULL, NULL)) {
                mode_switch = 0x01;
            } else {
                mode_switch = 0x03;
            }
            
            break;
        }
        default: {
            /* Current position */
            mode_switch = 1;
            break;
        }
    }
    
    mirage_track_layout_get_start_sector(MIRAGE_TRACK(track), &start_sector, NULL);
    
    switch (mode_switch) {
        case 0x01: {
            /* Mode-1: Current position */
            gint ctl = 0, track_number = 0;
            gint track_start = 0;
            GObject *index = NULL;
            
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: generating Mode-1 Q: Position\n", __func__);
                        
            mirage_track_get_ctl(MIRAGE_TRACK(track), &ctl, NULL);
            mirage_track_layout_get_track_number(MIRAGE_TRACK(track), &track_number, NULL);
            
            mirage_track_get_track_start(MIRAGE_TRACK(track), &track_start, NULL);
            
            buf[0] = (ctl << 0x04) | 0x01; /* Mode-1 Q */
            buf[1] = mirage_helper_hex2bcd(track_number); /* Track number */
            
            /* Index: try getting index object by address; if it's not found, we
               check if sector lies before track start... */
            if (mirage_track_get_index_by_address(MIRAGE_TRACK(track), address, &index, NULL)) {
                gint index_number = 0;
                mirage_index_get_number(MIRAGE_INDEX(index), &index_number, NULL);
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: address 0x%X belongs to index with number: %d\n", __func__, address, index_number);
                buf[2] = index_number;
                g_object_unref(index);
            } else {
                /* No index... check if address is in a pregap (strictly less than track start) */
                if (address < track_start) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: address 0x%X is part of pregap\n", __func__, address);
                    buf[2] = 0;
                } else {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: address 0x%X belongs to index 1\n", __func__, address);
                    buf[2] = 1;
                }
            }
            buf[2] = mirage_helper_hex2bcd(buf[2]);
            
            /* Relative M/S/F */
            mirage_helper_lba2msf(ABS(address - track_start), FALSE /* Don't add 2 sec here (because it's relative address)! */, &buf[3], &buf[4], &buf[5]);                
            buf[3] = mirage_helper_hex2bcd(buf[3]);
            buf[4] = mirage_helper_hex2bcd(buf[4]);
            buf[5] = mirage_helper_hex2bcd(buf[5]);
            buf[6] = 0; /* Zero */
            /* Absolute M/S/F */
            mirage_helper_lba2msf(address + start_sector, TRUE, &buf[7], &buf[8], &buf[9]);
            buf[7] = mirage_helper_hex2bcd(buf[7]);
            buf[8] = mirage_helper_hex2bcd(buf[8]);
            buf[9] = mirage_helper_hex2bcd(buf[9]);
            break;
        }
        case 0x02: {
            /* Mode-2: MCN */
            gint ctl = 0;
            
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: generating Mode-2 Q: MCN\n", __func__);

            mirage_track_get_ctl(MIRAGE_TRACK(track), &ctl, NULL);
            buf[0] = (ctl << 0x04) | 0x02; /* Mode-2 Q */
            
            /* MCN */
            GObject *session = NULL;
            GObject *disc = NULL;

            gchar *mcn = NULL;
            
            mirage_object_get_parent(MIRAGE_OBJECT(track), &session, NULL);
            mirage_object_get_parent(MIRAGE_OBJECT(session), &disc, NULL);
            
            mirage_disc_get_mcn(MIRAGE_DISC(disc), &mcn, NULL);
            
            g_object_unref(disc);
            g_object_unref(session);
            
            mirage_helper_subchannel_q_encode_mcn(&buf[1], mcn);
            g_free(mcn);
            buf[8] = 0; /* zero */
            /* AFRAME */
            mirage_helper_lba2msf(address + start_sector, TRUE, NULL, NULL, &buf[9]);
            buf[9] = mirage_helper_hex2bcd(buf[9]);
            break;
        }
        case 0x03: {
            /* Mode-3: ISRC */
            gint ctl = 0;
            
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: generating Mode-3 Q: ISRC\n", __func__);

            mirage_track_get_ctl(MIRAGE_TRACK(track), &ctl, NULL);
            buf[0] = (ctl << 0x04) | 0x03; /* Mode-3 Q */
            
            /* ISRC*/
            gchar *isrc = NULL;
            mirage_track_get_isrc(MIRAGE_TRACK(track), &isrc, NULL);
            mirage_helper_subchannel_q_encode_isrc(&buf[1], isrc);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: ISRC string: %s bytes: %02X %02X %02X %02X %02X %02X %02X %02X\n", __func__, isrc, buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], buf[8]);
            g_free(isrc);
            /* AFRAME */
            mirage_helper_lba2msf(address + start_sector, TRUE, NULL, NULL, &buf[9]);
            buf[9] = mirage_helper_hex2bcd(buf[9]);
            break;
        }
    }
    
    /* CRC */
    crc = mirage_helper_subchannel_q_calculate_crc(&buf[0]);
    buf[10] = (crc & 0xFF00) >> 0x08;
    buf[11] = (crc & 0x00FF) >> 0x00;
    
    /* Release sector's parent track */
    g_object_unref(track);
    
    return 12;
}

static gint __subchannel_generate_r (MIRAGE_Sector *self, guint8 *buf) {
    memset(buf, 0, 12);
    return 12;
}

static gint __subchannel_generate_s (MIRAGE_Sector *self, guint8 *buf) {
    memset(buf, 0, 12);
    return 12;
}

static gint __subchannel_generate_t (MIRAGE_Sector *self, guint8 *buf) {
    memset(buf, 0, 12);
    return 12;
}

static gint __subchannel_generate_u (MIRAGE_Sector *self, guint8 *buf) {
    memset(buf, 0, 12);
    return 12;
}

static gint __subchannel_generate_v (MIRAGE_Sector *self, guint8 *buf) {
    memset(buf, 0, 12);
    return 12;
}

static gint __subchannel_generate_w (MIRAGE_Sector *self, guint8 *buf) {
    memset(buf, 0, 12);
    return 12;
}

static void __mirage_sector_generate_subchannel (MIRAGE_Sector *self) {
    MIRAGE_SectorPrivate *_priv = MIRAGE_SECTOR_GET_PRIVATE(self);
    
    guint8 tmp_buf[12] = {0};
    /* Generate subchannel: only P/Q can be generated at the moment 
       (other subchannels are set to 0) */
    
    /* Read P subchannel into temporary buffer, then interleave it */
    __subchannel_generate_p(self, tmp_buf);
    mirage_helper_subchannel_interleave(SUBCHANNEL_P, tmp_buf, _priv->subchan_pw);
    /* Read Q subchannel into temporary buffer, then interleave it */
    __subchannel_generate_q(self, tmp_buf);
    mirage_helper_subchannel_interleave(SUBCHANNEL_Q, tmp_buf, _priv->subchan_pw);
    /* Read R subchannel into temporary buffer, then interleave it */
    __subchannel_generate_r(self, tmp_buf);
    mirage_helper_subchannel_interleave(SUBCHANNEL_R, tmp_buf, _priv->subchan_pw);
    /* Read S subchannel into temporary buffer, then interleave it */
    __subchannel_generate_s(self, tmp_buf);
    mirage_helper_subchannel_interleave(SUBCHANNEL_S, tmp_buf, _priv->subchan_pw);
    /* Read T subchannel into temporary buffer, then interleave it */
    __subchannel_generate_t(self, tmp_buf);
    mirage_helper_subchannel_interleave(SUBCHANNEL_T, tmp_buf, _priv->subchan_pw);
    /* Read U subchannel into temporary buffer, then interleave it */
    __subchannel_generate_v(self, tmp_buf);
    mirage_helper_subchannel_interleave(SUBCHANNEL_U, tmp_buf, _priv->subchan_pw);
    /* Read V subchannel into temporary buffer, then interleave it */
    __subchannel_generate_u(self, tmp_buf);
    mirage_helper_subchannel_interleave(SUBCHANNEL_V, tmp_buf, _priv->subchan_pw);
    /* Read W subchannel into temporary buffer, then interleave it */
    __subchannel_generate_w(self, tmp_buf);
    mirage_helper_subchannel_interleave(SUBCHANNEL_W, tmp_buf, _priv->subchan_pw);
    
    return;
}
