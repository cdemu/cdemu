/*
 *  libMirage: Sector object
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
 * SECTION: mirage-sector
 * @title: MirageSector
 * @short_description: Object representing a sector.
 * @see_also: #MirageTrack
 * @include: mirage-sector.h
 *
 * #MirageSector object represents a sector. It provides access to the
 * sector data, generating it if needed.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"

#define __debug__ "Sector"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_SECTOR_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_SECTOR, MirageSectorPrivate))

struct _MirageSectorPrivate
{
    MirageSectorType type;
    gint address; /* Absolute address of sector */

    gint real_data; /* Which parts of sector data were provided by image */
    gint valid_data; /* Which parts of sector data are valid (either provided by image or generated) */
    guint8 sector_data[2352]; /* Buffer for sector data */
    guint8 subchan_pw[96]; /* Buffer for interleaved PW subchannel */
    guint8 subchan_q[16]; /* Buffer for deinterleaved Q subchannel */
};


/**********************************************************************\
 *                          Private functions                         *
\**********************************************************************/
static gboolean mirage_sector_get_sync_offset_and_length (MirageSector *self, gint *offset, gint *length, GError **error)
{
    /* Sync is supported by all non-audio sectors */
    switch (self->priv->type) {
        case MIRAGE_SECTOR_MODE0:
        case MIRAGE_SECTOR_MODE1:
        case MIRAGE_SECTOR_MODE2:
        case MIRAGE_SECTOR_MODE2_FORM1:
        case MIRAGE_SECTOR_MODE2_FORM2: {
            *offset = 0;
            *length = 12;
            return TRUE;
        }
        default: {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SECTOR_ERROR, "Sync pattern not available for sector type %d!", self->priv->type);
            return FALSE;
        }
    }
}

static gboolean mirage_sector_get_header_offset_and_length (MirageSector *self, gint *offset, gint *length, GError **error)
{
    /* Header is supported by all non-audio sectors */
    switch (self->priv->type) {
        case MIRAGE_SECTOR_MODE0:
        case MIRAGE_SECTOR_MODE1:
        case MIRAGE_SECTOR_MODE2:
        case MIRAGE_SECTOR_MODE2_FORM1:
        case MIRAGE_SECTOR_MODE2_FORM2: {
            *offset = 12;
            *length = 4;
            return TRUE;
        }
        default: {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SECTOR_ERROR, "Header not available for sector type %d!", self->priv->type);
            return FALSE;
        }
    }
}

static gboolean mirage_sector_get_subheader_offset_and_length (MirageSector *self, gint *offset, gint *length, GError **error)
{
    /* Subheader is supported by formed Mode 2 sectors */
    switch (self->priv->type) {
        case MIRAGE_SECTOR_MODE2_FORM1:
        case MIRAGE_SECTOR_MODE2_FORM2: {
            *offset = 16;
            *length = 8;
            return TRUE;
        }
        default: {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SECTOR_ERROR, "Subheader not available for sector type %d!", self->priv->type);
            return FALSE;
        }
    }
}

static gboolean mirage_sector_get_data_offset_and_length (MirageSector *self, gint *offset, gint *length, GError **error)
{
    /* Data is supported by all sectors */
    switch (self->priv->type) {
        case MIRAGE_SECTOR_AUDIO: {
            *offset = 0;
            *length = 2352;
            return TRUE;
        }
        case MIRAGE_SECTOR_MODE0: {
            *offset = 16;
            *length = 2336;
            return TRUE;
        }
        case MIRAGE_SECTOR_MODE1: {
            *offset = 16;
            *length = 2048;
            return TRUE;
        }
        case MIRAGE_SECTOR_MODE2: {
            *offset = 16;
            *length = 2336;
            return TRUE;
        }
        case MIRAGE_SECTOR_MODE2_FORM1: {
            *offset = 24;
            *length = 2048;
            return TRUE;
        }
        case MIRAGE_SECTOR_MODE2_FORM2: {
            *offset = 24;
            *length = 2324;
            return TRUE;
        }
        default: {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SECTOR_ERROR, "Data not available for sector type %d!", self->priv->type);
            return FALSE;
        }
    }
}

static gboolean mirage_sector_get_edc_ecc_offset_and_length (MirageSector *self, gint *offset, gint *length, GError **error)
{
    /* EDC/ECC is supported by Mode 1 and formed Mode 2 sectors */
    switch (self->priv->type) {
        case MIRAGE_SECTOR_MODE1: {
            *offset = 2064;
            *length = 288;
            return TRUE;
        }
        case MIRAGE_SECTOR_MODE2_FORM1: {
            *offset = 2072;
            *length = 280;
            return TRUE;
        }
        case MIRAGE_SECTOR_MODE2_FORM2: {
            *offset = 2348;
            *length = 4;
            return TRUE;
        }
        default: {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SECTOR_ERROR, "EDC/ECC not available for sector type %d!", self->priv->type);
            return FALSE;
        }
    }
}


static gboolean mirage_sector_get_info_for_feed_or_extract (MirageSector *self, gint main_data_length, gint *data_offset, gint *real_data, GError **error)
{
    *data_offset = 0;
    *real_data = 0;

    switch (self->priv->type) {
        case MIRAGE_SECTOR_AUDIO: {
            /* Audio sector structure: data (2352) */
            switch (main_data_length) {
                case 0: {
                    /* Nothing; pregap */
                    break;
                }
                case 2352: {
                    /* Audio data */
                    *data_offset = 0; /* Offset: 0 */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_DATA;

                    /* We mark the rest as valid as well, so that we don't need
                       additional checks in fake data generation code */
                    *real_data |= MIRAGE_VALID_SYNC;
                    *real_data |= MIRAGE_VALID_HEADER;
                    *real_data |= MIRAGE_VALID_SUBHEADER;
                    *real_data |= MIRAGE_VALID_EDC_ECC;
                    break;
                }
                default: {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unhandled sector size %d for Audio sector!\n", __debug__, main_data_length);
                    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SECTOR_ERROR, "Unhandled sector size %d for Audio sector!", main_data_length);
                    return FALSE;
                }
            }
            break;
        }
        case MIRAGE_SECTOR_MODE0: {
            /* Mode 0 sector structue:
                sync (12) + header (4) + data (2336) */
            switch (main_data_length) {
                case 0: {
                    /* Nothing; pregap */
                    break;
                }
                case 2336: {
                    /* Data only */
                    *data_offset = 12 + 4; /* Offset: sync + header */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_DATA;

                    break;
                }
                case 2340: {
                    /* Data + header */
                    *data_offset = 12; /* Offset: sync */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_HEADER;
                    *real_data |= MIRAGE_VALID_DATA;

                    break;
                }
                case 2352: {
                    /* Sync + header + data */
                    *data_offset = 0; /* Offset: 0 */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_SYNC;
                    *real_data |= MIRAGE_VALID_HEADER;
                    *real_data |= MIRAGE_VALID_DATA;

                    break;
                }
                default: {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unhandled sector size %d for Mode 0 sector!\n", __debug__, main_data_length);
                    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SECTOR_ERROR, "Unhandled sector size %d for Mode 0 sector!", main_data_length);
                    return FALSE;
                }
            }

            break;
        }
        case MIRAGE_SECTOR_MODE1: {
            /* Mode 1 sector structue:
                sync (12) + header (4) + data (2048) + EDC/ECC (288) */
            switch (main_data_length) {
                case 0: {
                    /* Nothing; pregap */
                    break;
                }
                case 2048: {
                    /* Data only */
                    *data_offset = 12 + 4; /* Offset: sync + header */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_DATA;

                    break;
                }
                case 2052: {
                    /* Header + data */
                    *data_offset = 12; /* Offset: sync */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_HEADER;
                    *real_data |= MIRAGE_VALID_DATA;

                    break;
                }
                case 2064: {
                    /* Sync + header + data */
                    *data_offset = 0; /* Offset: 0 */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_SYNC;
                    *real_data |= MIRAGE_VALID_HEADER;
                    *real_data |= MIRAGE_VALID_DATA;

                    break;
                }
                case 2336: {
                    /* Data + EDC/ECC */
                    *data_offset = 12 + 4; /* Offset: sync + header */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_DATA;
                    *real_data |= MIRAGE_VALID_EDC_ECC;

                    break;
                }
                case 2340: {
                    /* Header + data + EDC/ECC */
                    *data_offset = 12; /* Offset: sync */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_HEADER;
                    *real_data |= MIRAGE_VALID_DATA;
                    *real_data |= MIRAGE_VALID_EDC_ECC;

                    break;
                }
                case 2352: {
                    /* Sync + header + data + EDC/ECC */
                    *data_offset = 0; /* Offset: 0 */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_SYNC;
                    *real_data |= MIRAGE_VALID_HEADER;
                    *real_data |= MIRAGE_VALID_DATA;
                    *real_data |= MIRAGE_VALID_EDC_ECC;

                    break;
                }
                default: {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unhandled sector size %d for Mode 1 sector!\n", __debug__, main_data_length);
                    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SECTOR_ERROR, "Unhandled sector size %d for Mode 1 sector!", main_data_length);
                    return FALSE;
                }
            }

            break;
        }
        case MIRAGE_SECTOR_MODE2: {
            /* Mode 2 formless sector structue:
                sync (12) + header (4) + data (2336) */
            switch (main_data_length) {
                case 0: {
                    /* Nothing; pregap */
                    break;
                }
                case 2336: {
                    /* Data only */
                    *data_offset = 12 + 4; /* Offset: sync + header */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_DATA;

                    break;
                }
                case 2340: {
                    /* Header + data */
                    *data_offset = 12; /* Offset: sync */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_HEADER;
                    *real_data |= MIRAGE_VALID_DATA;

                    break;
                }
                case 2352: {
                    /* Sync + header + data */
                    *data_offset = 0; /* Offset: 0 */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_SYNC;
                    *real_data |= MIRAGE_VALID_HEADER;
                    *real_data |= MIRAGE_VALID_DATA;

                    break;
                }
                default: {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unhandled sector size %d for Mode 2 Formless sector!\n", __debug__, main_data_length);
                    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SECTOR_ERROR, "Unhandled sector size %d for Mode 2 Formless sector!", main_data_length);
                    return FALSE;
                }
            }

            break;
        }
        case MIRAGE_SECTOR_MODE2_FORM1: {
            /* Mode 2 Form 1 sector structue:
                sync (12) + header (4) + subheader (8) + data (2048) + EDC/ECC (280) */
            switch (main_data_length) {
                case 0: {
                    /* Nothing; pregap */
                    break;
                }
                case 2048: {
                    /* Data only */
                    *data_offset = 12 + 4 + 8; /* Offset: sync + header + subheader */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_DATA;

                    break;
                }
                case 2056: {
                    /* Subheader + data */
                    *data_offset = 12 + 4; /* Offset: sync + header */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_SUBHEADER;
                    *real_data |= MIRAGE_VALID_DATA;

                    break;
                }
                case 2060: {
                    /* Header + subheader + data */
                    *data_offset = 12; /* Offset: sync */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_HEADER;
                    *real_data |= MIRAGE_VALID_SUBHEADER;
                    *real_data |= MIRAGE_VALID_DATA;

                    break;
                }
                case 2072: {
                    /* Sync + header + subheader + data */
                    *data_offset = 0; /* Offset: 0 */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_SYNC;
                    *real_data |= MIRAGE_VALID_HEADER;
                    *real_data |= MIRAGE_VALID_SUBHEADER;
                    *real_data |= MIRAGE_VALID_DATA;

                    break;
                }
                case 2328: {
                    /* Data + EDC/ECC */
                    *data_offset = 12 + 4 + 8; /* Offset: sync + header + subheader */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_DATA;
                    *real_data |= MIRAGE_VALID_EDC_ECC;

                    break;
                }
                case 2336: {
                    /* Subheader + data + EDC/ECC */
                    *data_offset = 12 + 4; /* Offset: sync + header */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_SUBHEADER;
                    *real_data |= MIRAGE_VALID_DATA;
                    *real_data |= MIRAGE_VALID_EDC_ECC;

                    break;
                }
                case 2340: {
                    /* Header + subheader + data + EDC/ECC */
                    *data_offset = 12; /* Offset: sync */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_HEADER;
                    *real_data |= MIRAGE_VALID_SUBHEADER;
                    *real_data |= MIRAGE_VALID_DATA;
                    *real_data |= MIRAGE_VALID_EDC_ECC;

                    break;
                }
                case 2352: {
                    /* Sync + header + subheader + data + EDC/ECC */
                    *data_offset = 0; /* Offset: 0 */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_SYNC;
                    *real_data |= MIRAGE_VALID_HEADER;
                    *real_data |= MIRAGE_VALID_SUBHEADER;
                    *real_data |= MIRAGE_VALID_DATA;
                    *real_data |= MIRAGE_VALID_EDC_ECC;

                    break;
                }
                default: {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unhandled sector size %d for Mode 2 Form 1 sector!\n", __debug__, main_data_length);
                    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SECTOR_ERROR, "Unhandled sector size %d for Mode 2 Form 1 sector!", main_data_length);
                    return FALSE;
                }
            }

            break;
        }
        case MIRAGE_SECTOR_MODE2_FORM2: {
            /* Mode 2 Form 2 sector structue:
                sync (12) + header (4) + subheader (8) + data (2324) + EDC/ECC (4) */
            switch (main_data_length) {
                case 0: {
                    /* Nothing; pregap */
                    break;
                }
                case 2324: {
                    /* Data only */
                    *data_offset = 12 + 4 + 8; /* Offset: sync + header + subheader */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_DATA;

                    break;
                }
                case 2332: {
                    /* Subheader + data */
                    *data_offset = 12 + 4; /* Offset: sync + header */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_SUBHEADER;
                    *real_data |= MIRAGE_VALID_DATA;

                    break;
                }
#if 0
                /* This one yields same size as subheader + data + EDC/ECC,
                   which is actually used, while this one most likely isn't. */
                case 2336: {
                    /* Header + subheader + data */
                    *data_offset = 12; /* Offset: sync */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_HEADER;
                    *real_data |= MIRAGE_VALID_SUBHEADER;
                    *real_data |= MIRAGE_VALID_DATA;

                    break;
                }
#endif
                case 2348: {
                    /* Sync + header + subheader + data */
                    *data_offset = 0; /* Offset: 0 */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_SYNC;
                    *real_data |= MIRAGE_VALID_HEADER;
                    *real_data |= MIRAGE_VALID_SUBHEADER;
                    *real_data |= MIRAGE_VALID_DATA;

                    break;
                }
                case 2328: {
                    /* Data + EDC/ECC */
                    *data_offset = 12 + 4 + 8; /* Offset: sync + header + subheader */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_DATA;
                    *real_data |= MIRAGE_VALID_EDC_ECC;

                    break;
                }
                case 2336: {
                    /* Subheader + data + EDC/ECC */
                    *data_offset = 12 + 4; /* Offset: sync + header */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_SUBHEADER;
                    *real_data |= MIRAGE_VALID_DATA;
                    *real_data |= MIRAGE_VALID_EDC_ECC;

                    break;
                }
                case 2340: {
                    /* Header + subheader + data + EDC/ECC */
                    *data_offset = 12; /* Offset: sync */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_HEADER;
                    *real_data |= MIRAGE_VALID_SUBHEADER;
                    *real_data |= MIRAGE_VALID_DATA;
                    *real_data |= MIRAGE_VALID_EDC_ECC;

                    break;
                }
                case 2352: {
                    /* Sync + header + subheader + data + EDC/ECC */
                    *data_offset = 0; /* Offset: 0 */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_SYNC;
                    *real_data |= MIRAGE_VALID_HEADER;
                    *real_data |= MIRAGE_VALID_SUBHEADER;
                    *real_data |= MIRAGE_VALID_DATA;
                    *real_data |= MIRAGE_VALID_EDC_ECC;

                    break;
                }
                default: {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unhandled sector size %d for Mode 2 Form 2 sector!\n", __debug__, main_data_length);
                    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SECTOR_ERROR, "Unhandled sector size %d for Mode 2 Form 2 sector!", main_data_length);
                    return FALSE;
                }
            }

            break;
        }
        case MIRAGE_SECTOR_MODE2_MIXED: {
            /* Mode 2 Mixed consists of both Mode 2 Form 1 and Mode 2 Form 2
               sectors, which are distinguished by subheader. In addition to
               having to provide subheader data, both Form 1 and Form 2 sectors
               must be of same size; this is true only if at least subheader,
               data and EDC/ECC are provided */
            switch (main_data_length) {
                case 0: {
                    /* Nothing; pregap */
                    break;
                }
                case 2332:
                    /* This one's a special case; it is same as 2336, except
                       that last four bytes (for Form 2 sectors, that's optional
                       EDC, and for Form 1 sectors, it's last four bytes of ECC)
                       are omitted. Therefore, we need to re-generate
                       the EDC/ECC code */

                    /* Subheader + data (+ EDC/ECC) */
                    *data_offset = 12 + 4; /* Offset: sync + header */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_SUBHEADER;
                    *real_data |= MIRAGE_VALID_DATA;

                    break;
                case 2336: {
                    /* Subheader + data + EDC/ECC */
                    *data_offset = 12 + 4; /* Offset: sync + header */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_SUBHEADER;
                    *real_data |= MIRAGE_VALID_DATA;
                    *real_data |= MIRAGE_VALID_EDC_ECC;

                    break;
                }
                case 2340: {
                    /* Header + subheader + data + EDC/ECC */
                    *data_offset = 12 + 4; /* Offset: sync */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_HEADER;
                    *real_data |= MIRAGE_VALID_SUBHEADER;
                    *real_data |= MIRAGE_VALID_DATA;
                    *real_data |= MIRAGE_VALID_EDC_ECC;

                    break;
                }
                case 2352: {
                    /* Sync + header + subheader + data + EDC/ECC */
                    *data_offset = 0; /* Offset: 0 */

                    /* Valid */
                    *real_data |= MIRAGE_VALID_SYNC;
                    *real_data |= MIRAGE_VALID_HEADER;
                    *real_data |= MIRAGE_VALID_SUBHEADER;
                    *real_data |= MIRAGE_VALID_DATA;
                    *real_data |= MIRAGE_VALID_EDC_ECC;

                    break;
                }
                default: {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unhandled sector size %d for Mode 2 Mixed sector!\n", __debug__, main_data_length);
                    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SECTOR_ERROR, "Unhandled sector size %d for Mode 2 Mixed sector!", main_data_length);
                    return FALSE;
                }
            }

            break;
        }
        default: {
            /* Raw sector type; nothing to do */
            break;
        }
    }

    return TRUE;
}


static void mirage_sector_generate_subchannel (MirageSector *self);

static void mirage_sector_generate_sync (MirageSector *self)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: generating sync\n", __debug__);

    switch (self->priv->type) {
        case MIRAGE_SECTOR_MODE0:
        case MIRAGE_SECTOR_MODE1:
        case MIRAGE_SECTOR_MODE2:
        case MIRAGE_SECTOR_MODE2_FORM1:
        case MIRAGE_SECTOR_MODE2_FORM2: {
            memcpy(self->priv->sector_data, mirage_pattern_sync, 12);
            break;
        }
        default: {
            return;
        }
    }

    self->priv->valid_data |= MIRAGE_VALID_SYNC;
}

static void mirage_sector_generate_header (MirageSector *self)
{
    guint8 *header = self->priv->sector_data+12;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: generating header\n", __debug__);

    /* Set mode */
    switch (self->priv->type) {
        case MIRAGE_SECTOR_MODE0: {
            header[3] = 0; /* Mode = 0 */
            break;
        }
        case MIRAGE_SECTOR_MODE1: {
            header[3] = 1; /* Mode = 1 */
            break;
        }
        case MIRAGE_SECTOR_MODE2:
        case MIRAGE_SECTOR_MODE2_FORM1:
        case MIRAGE_SECTOR_MODE2_FORM2: {
            header[3] = 2; /* Mode = 2 */
            break;
        }
        default: {
            return;
        }
    }

    /* Address */
    mirage_helper_lba2msf(self->priv->address, TRUE, &header[0], &header[1], &header[2]);
    header[0] = mirage_helper_hex2bcd(header[0]);
    header[1] = mirage_helper_hex2bcd(header[1]);
    header[2] = mirage_helper_hex2bcd(header[2]);

    self->priv->valid_data |= MIRAGE_VALID_HEADER;
}

static void mirage_sector_generate_subheader (MirageSector *self)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: generating subheader\n", __debug__);

    switch (self->priv->type) {
        case MIRAGE_SECTOR_MODE2_FORM1: {
            guint8 *subheader = self->priv->sector_data+16;
            subheader[2] |= (0 << 5); /* Form 1 */
            subheader[5] = subheader[2];
            break;
        }
        case MIRAGE_SECTOR_MODE2_FORM2: {
            guint8 *subheader = self->priv->sector_data+16;
            subheader[2] |= (1 << 5); /* Form 2 */
            subheader[5] = subheader[2];
            break;
        }
        default: {
            return;
        }
    }

    self->priv->valid_data |= MIRAGE_VALID_SUBHEADER;
}

static void mirage_sector_generate_data (MirageSector *self)
{
    gint offset = 0;
    gint length = 0;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: \"generating\" (clearing) user data\n", __debug__);

    mirage_sector_get_data_offset_and_length(self, &offset, &length, NULL);
    memset(self->priv->sector_data + offset, 0, length);

    self->priv->valid_data |= MIRAGE_VALID_DATA;
}

static void mirage_sector_generate_edc_ecc (MirageSector *self)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: generating EDC/ECC\n", __debug__);

    /* If user data is not set, make sure we have cleared it! */
    if (!(self->priv->valid_data & MIRAGE_VALID_DATA)) {
        mirage_sector_generate_data(self);
    }

    switch (self->priv->type) {
        case MIRAGE_SECTOR_MODE1: {
            /* EDC/ECC are generated over sync, header and data in Mode 1 sectors...
               so make sure we have those */
            if (!(self->priv->valid_data & MIRAGE_VALID_SYNC)) {
                mirage_sector_generate_sync(self);
            }
            if (!(self->priv->valid_data & MIRAGE_VALID_HEADER)) {
                mirage_sector_generate_header(self);
            }

            /* Generate EDC */
            mirage_helper_sector_edc_ecc_compute_edc_block(self->priv->sector_data+0x00, 0x810, self->priv->sector_data+0x810);
            /* Generate ECC P/Q codes */
            mirage_helper_sector_edc_ecc_compute_ecc_block(self->priv->sector_data+0xC, 86, 24, 2, 86, self->priv->sector_data+0x81C); /* P */
            mirage_helper_sector_edc_ecc_compute_ecc_block(self->priv->sector_data+0xC, 52, 43, 86, 88, self->priv->sector_data+0x8C8); /* Q */

            break;
        }
        case MIRAGE_SECTOR_MODE2_FORM1: {
            guint8 tmp_header[4];
            /* Zero the header, because it is not supposed to be included in the
               calculation; copy, calculate, then copy back */
            memcpy(tmp_header, self->priv->sector_data+12, 4);
            memset(self->priv->sector_data+12, 0, 4);
            /* Generate EDC */
            mirage_helper_sector_edc_ecc_compute_edc_block(self->priv->sector_data+0x10, 0x808, self->priv->sector_data+0x818);
            /* Generate ECC P/Q codes */
            mirage_helper_sector_edc_ecc_compute_ecc_block(self->priv->sector_data+0xC, 86, 24, 2, 86, self->priv->sector_data+0x81C); /* P */
            mirage_helper_sector_edc_ecc_compute_ecc_block(self->priv->sector_data+0xC, 52, 43, 86, 88, self->priv->sector_data+0x8C8); /* Q */
            /* Unzero */
            memcpy(self->priv->sector_data+12, tmp_header, 4);

            break;
        }
        case MIRAGE_SECTOR_MODE2_FORM2: {
            /* Compute EDC */
            mirage_helper_sector_edc_ecc_compute_edc_block(self->priv->sector_data+0x10, 0x91C, self->priv->sector_data+0x92C);

            break;
        }
        default: {
            return;
        }
    }

    self->priv->valid_data |= MIRAGE_VALID_EDC_ECC;
}


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
/**
 * mirage_sector_feed_data:
 * @self: a #MirageSector
 * @address: (in): absolute disc address the sector represents. Given in sectors.
 * @type: (in): track type (one of #MirageSectorType
 * @main_data: (in): main data buffer
 * @main_data_length: (in): length of data in main data buffer
 * @subchannel_format: (in): subchannel data format
 * @subchannel_data: (in) (allow-none): subchannel data buffer
 * @subchannel_data_length: (in): length of data in subchannel data buffer
 * @ignore_data_mask: (in): a mask of #MirageSectorValidData values, indicating which parts of main channel sector data, if any, should be ignored and regerated even though they are provided by the data feed
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Feeds data to sector. If @type is %MIRAGE_SECTOR_RAW or %MIRAGE_SECTOR_RAW_SCRAMBLED,
 * the real sector type is determined during feeding.
 *
 * <note>
 * Intended for internal use.
 * </note>
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_sector_feed_data (MirageSector *self, gint address, MirageSectorType type, const guint8 *main_data, guint main_data_length, MirageSectorSubchannelFormat subchannel_format, const guint8 *subchannel_data, guint subchannel_data_length, gint ignore_data_mask, GError **error)
{
    gint data_offset;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: feeding: address: %Xh (%d), type: %d, main channel data size: %d, subchannel data size: %d\n", __debug__, address, address, type, main_data_length, subchannel_data_length);

    /* Reset data validity flags */
    self->priv->real_data = self->priv->valid_data = 0;

    /* Store address and sector type */
    self->priv->address = address;
    self->priv->type = type;

    /* If type is raw or scrambled raw, we copy data first and determine
       sector type from it. Then we use the main switch to fill in the
       valid data flags for us, and avoid copying the data again. */
    if (type == MIRAGE_SECTOR_RAW || type == MIRAGE_SECTOR_RAW_SCRAMBLED) {
        if (main_data_length != 2352) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: raw sectors require 2352 bytes of data!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SECTOR_ERROR, "Raw sectors require 2352 bytes of data!");
            return FALSE;
        }

        /* Copy data from buffer */
        memcpy(self->priv->sector_data, main_data, main_data_length);

        /* We can easily recognise an audio sector by the lack of sync
           pattern at the beginning */
        if (!memcmp(self->priv->sector_data, mirage_pattern_sync, sizeof(mirage_pattern_sync))) {
            /* Data sector; unscramble if necessary */
            if (type == MIRAGE_SECTOR_RAW_SCRAMBLED) {
                mirage_sector_scramble(self);
            }

            /* Determine sector type */
            self->priv->type = mirage_helper_determine_sector_type(self->priv->sector_data);
        } else {
            /* Audio sector */
            self->priv->type = MIRAGE_SECTOR_AUDIO;
        }
    }

    /* Now, determine offset and real data flags based on type and main channel data length */
    if (!mirage_sector_get_info_for_feed_or_extract(self, main_data_length, &data_offset, &self->priv->real_data, error)) {
        return FALSE;
    }

    /* Copy data (skip if initial sector type was raw) */
    if (type != MIRAGE_SECTOR_RAW && type != MIRAGE_SECTOR_RAW_SCRAMBLED) {
        memcpy(self->priv->sector_data+data_offset, main_data, main_data_length);
    }

    /* Now, if we had Mode 2 Mixed, we can determine whether we have
       Mode 2 Form 1 or Mode 2 Form 2 */
    if (self->priv->type == MIRAGE_SECTOR_MODE2_MIXED) {
        /* Check the subheader... */
        if (self->priv->sector_data[18] & 0x20) {
            self->priv->type = MIRAGE_SECTOR_MODE2_FORM2;
        } else {
            self->priv->type = MIRAGE_SECTOR_MODE2_FORM1;
        }
    }

    /* Mask the real_data with ignore_data_mask, to force re-generation
       of masked parts of sector data */
    self->priv->real_data &= ~ignore_data_mask;

    /* At this point, real_data field indicates which parts of sector
       data were provided by the image file; make a copy of this field
       in valid_data field, which will be modified when the missing data
       is generated */
    self->priv->valid_data = self->priv->real_data;

    /* Subchannel; use sector's function to set it */
    if (subchannel_data_length && subchannel_data) {
        if (!mirage_sector_set_subchannel(self, subchannel_format, subchannel_data, subchannel_data_length, error)) {
            return FALSE;
        }
    }

    return TRUE;
}

gboolean mirage_sector_extract_data (MirageSector *self, const guint8 **main_data, guint main_data_length, MirageSectorSubchannelFormat subchannel_format, const guint8 **subchannel_data, guint subchannel_data_length, GError **error)
{
    gint data_offset, required_data;
    gint actual_subchannel_length;

    /* Determine offset and real data flags based on type and main channel data length */
    if (!mirage_sector_get_info_for_feed_or_extract(self, main_data_length, &data_offset, &required_data, error)) {
        return FALSE;
    }

    /* Ensure that all required data is available */
    if (required_data & MIRAGE_VALID_SYNC) {
        mirage_sector_generate_sync(self);
    }
    if (required_data & MIRAGE_VALID_HEADER) {
        mirage_sector_generate_header(self);
    }
    if (required_data & MIRAGE_VALID_SUBHEADER) {
        mirage_sector_generate_subheader(self);
    }
    if (required_data & MIRAGE_VALID_DATA) {
        mirage_sector_generate_data(self);
    }
    if (required_data & MIRAGE_VALID_EDC_ECC) {
        mirage_sector_generate_edc_ecc(self);
    }

    /* Return pointer to main data buffer */
    *main_data = self->priv->sector_data + data_offset;

    /* Subchannel */
    if (!mirage_sector_get_subchannel(self, subchannel_format, subchannel_data, &actual_subchannel_length, error)) {
        return FALSE;
    }

    if (actual_subchannel_length != subchannel_data_length) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: requested subchannel size (%d) and actual subchannel size (%d) mismatch!\n", __debug__, subchannel_data_length, actual_subchannel_length);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SECTOR_ERROR, "Requested subchannel size (%d) and actual subchannel size (%d) mismatch!\n", subchannel_data_length, actual_subchannel_length);
        return FALSE;
    }

    return TRUE;
}


/**
 * mirage_sector_get_sector_type:
 * @self: a #MirageSector
 *
 * Retrieves sector type (track mode); one of #MirageTrackModes.
 *
 * Returns: sector type (track mode)
 */
MirageSectorType mirage_sector_get_sector_type (MirageSector *self)
{
    /* Return sector type */
    return self->priv->type;
}

/**
 * mirage_sector_get_address:
 * @self: a #MirageSector
 *
 * Retrieves absolute disc address of the sector.
 *
 * Returns: sector address
 */
gint mirage_sector_get_address (MirageSector *self)
{
    /* Return sector address */
    return self->priv->address;
}


/**
 * mirage_sector_get_sync:
 * @self: a #MirageSector
 * @ret_buf: (out) (transfer none) (allow-none) (array length=ret_len): location to store pointer to buffer containing sync pattern, or %NULL
 * @ret_len: (out) (allow-none): location to store length of sync pattern, or %NULL. Length is given in bytes.
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves sector's sync pattern. The pointer to appropriate location in
 * sector's data buffer is stored into @ret_buf; therefore, the buffer should not
 * be modified.
 *
 * If sync pattern is not provided by image file(s), it is generated.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_sector_get_sync (MirageSector *self, const guint8 **ret_buf, gint *ret_len, GError **error)
{
    gboolean succeeded;
    gint offset = 0, length = 0;

    /* Generate sync if it's not provided; generation routine takes care of
       incompatible sector types */
    if (!(self->priv->valid_data & MIRAGE_VALID_SYNC)) {
        mirage_sector_generate_sync(self);
    }

    /* Get offset and length */
    succeeded = mirage_sector_get_sync_offset_and_length(self, &offset, &length, error);

    /* Return the requested data */
    if (ret_buf) {
        *ret_buf = succeeded ? self->priv->sector_data + offset : NULL;
    }
    if (ret_len) {
        *ret_len = succeeded ? length : 0;
    }

    return succeeded;
}

gboolean mirage_sector_set_sync (MirageSector *self, const guint8 *buf, gint len, GError **error)
{
    gint offset, expected_length;

    /* Get offset and length */
    if (!mirage_sector_get_sync_offset_and_length(self, &offset, &expected_length, error)) {
        return FALSE;
    }

    /* Validate length */
    if (len != expected_length) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SECTOR_ERROR, "Expected %d bytes for sync pattern!", expected_length);
        return FALSE;
    }

    /* Copy */
    memcpy(self->priv->sector_data + offset, buf, len);

    /* Mark as both real and valid */
    self->priv->real_data |= MIRAGE_VALID_SYNC;
    self->priv->valid_data |= MIRAGE_VALID_SYNC;

    return TRUE;
}


/**
 * mirage_sector_get_header:
 * @self: a #MirageSector
 * @ret_buf: (out) (transfer none) (allow-none) (array length=ret_len): location to store pointer to buffer containing header, or %NULL
 * @ret_len: (out) (allow-none): location to store length of header, or %NULL. Length is given in bytes.
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves sector's header. The pointer to appropriate location in
 * sector's data buffer is stored into @ret_buf; therefore, the buffer should not
 * be modified.
 *
 * If header is not provided by image file(s), it is generated.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_sector_get_header (MirageSector *self, const guint8 **ret_buf, gint *ret_len, GError **error)
{
    gboolean succeeded;
    gint offset = 0, length = 0;

    /* Generate header if it's not provided; generation routine takes care of
       incompatible sector types */
    if (!(self->priv->valid_data & MIRAGE_VALID_HEADER)) {
        mirage_sector_generate_header(self);
    }

    /* Get offset and length */
    succeeded = mirage_sector_get_header_offset_and_length(self, &offset, &length, error);

    /* Return the requested data */
    if (ret_buf) {
        *ret_buf = succeeded ? self->priv->sector_data + offset : NULL;
    }
    if (ret_len) {
        *ret_len = succeeded ? length : 0;
    }

    return succeeded;
}

gboolean mirage_sector_set_header (MirageSector *self, const guint8 *buf, gint len, GError **error)
{
    gint offset, expected_length;

    /* Get offset and length */
    if (!mirage_sector_get_header_offset_and_length(self, &offset, &expected_length, error)) {
        return FALSE;
    }

    /* Validate length */
    if (len != expected_length) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SECTOR_ERROR, "Expected %d bytes for header!", expected_length);
        return FALSE;
    }

    /* Copy */
    memcpy(self->priv->sector_data + offset, buf, len);

    /* Mark as both real and valid */
    self->priv->real_data |= MIRAGE_VALID_HEADER;
    self->priv->valid_data |= MIRAGE_VALID_HEADER;

    return TRUE;
}


/**
 * mirage_sector_get_subheader:
 * @self: a #MirageSector
 * @ret_buf: (out) (transfer none) (allow-none) (array length=ret_len): location to store pointer to buffer containing subheader, or %NULL
 * @ret_len: (out) (allow-none): location to store length of subheader, or %NULL. Length is given in bytes.
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves sector's subheader. The pointer to appropriate location in
 * sector's data buffer is stored into @ret_buf;  therefore, the buffer should not
 * be modified.
 *
 * If subheader is not provided by image file(s), it is generated.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_sector_get_subheader (MirageSector *self, const guint8 **ret_buf, gint *ret_len, GError **error)
{
    gboolean succeeded;
    gint offset = 0, length = 0;

    /* Generate subheader if it's not provided; generation routine takes care of
       incompatible sector types */
    if (!(self->priv->valid_data & MIRAGE_VALID_SUBHEADER)) {
        mirage_sector_generate_subheader(self);
    }

    /* Get offset and length */
    succeeded = mirage_sector_get_subheader_offset_and_length(self, &offset, &length, error);

    /* Return the requested data */
    if (ret_buf) {
        *ret_buf = succeeded ? self->priv->sector_data + offset : NULL;
    }
    if (ret_len) {
        *ret_len = succeeded ? length : 0;
    }

    return succeeded;
}

gboolean mirage_sector_set_subheader (MirageSector *self, const guint8 *buf, gint len, GError **error)
{
    gint offset, expected_length;

    /* Get offset and length */
    if (!mirage_sector_get_subheader_offset_and_length(self, &offset, &expected_length, error)) {
        return FALSE;
    }

    /* Validate length */
    if (len == expected_length) {
        /* Copy */
        memcpy(self->priv->sector_data + offset, buf, len);
    } else if (expected_length == 8 && len == 4) {
        /* This is valid as well; make two copies */
        memcpy(self->priv->sector_data + offset, buf, len);
        memcpy(self->priv->sector_data + offset + 4, buf, len);
    } else {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SECTOR_ERROR, "Expected %d bytes for subheader!", expected_length);
        return FALSE;
    }

    /* Mark as both real and valid */
    self->priv->real_data |= MIRAGE_VALID_SUBHEADER;
    self->priv->valid_data |= MIRAGE_VALID_SUBHEADER;

    return TRUE;
}


/**
 * mirage_sector_get_data:
 * @self: a #MirageSector
 * @ret_buf: (out) (transfer none) (allow-none) (array length=ret_len): location to store pointer to buffer containing user data, or %NULL
 * @ret_len: (out) (allow-none): location to store length of user data, or %NULL. Length is given in bytes.
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves sector's user data. The pointer to appropriate location in
 * sector's data buffer is stored into @ret_buf;  therefore, the buffer should not
 * be modified.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_sector_get_data (MirageSector *self, const guint8 **ret_buf, gint *ret_len, GError **error)
{
    gboolean succeeded;
    gint offset = 0, length = 0;

    /* If user data is not available, make sure that it is blank */
    if (!(self->priv->valid_data & MIRAGE_VALID_DATA)) {
        mirage_sector_generate_data(self);
    }

    /* Get offset and length */
    succeeded = mirage_sector_get_data_offset_and_length(self, &offset, &length, error);

    /* Return the requested data */
    if (ret_buf) {
        *ret_buf = succeeded ? self->priv->sector_data + offset : NULL;
    }
    if (ret_len) {
        *ret_len = succeeded ? length : 0;
    }

    return succeeded;
}

gboolean mirage_sector_set_data (MirageSector *self, const guint8 *buf, gint len, GError **error)
{
    gint offset, expected_length;

    /* Get offset and length */
    if (!mirage_sector_get_data_offset_and_length(self, &offset, &expected_length, error)) {
        return FALSE;
    }

    /* Validate length */
    if (len != expected_length) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SECTOR_ERROR, "Expected %d bytes for data!", expected_length);
        return FALSE;
    }

    /* Copy */
    memcpy(self->priv->sector_data + offset, buf, len);

    /* Mark as both real and valid */
    self->priv->real_data |= MIRAGE_VALID_DATA;
    self->priv->valid_data |= MIRAGE_VALID_DATA;

    return TRUE;
}

/**
 * mirage_sector_get_edc_ecc:
 * @self: a #MirageSector
 * @ret_buf: (out) (transfer none) (allow-none) (array length=ret_len): location to store pointer to buffer containing EDC/ECC data, or %NULL
 * @ret_len: (out) (allow-none): location to store length of EDC/ECC data, or %NULL. Length is given in bytes.
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves sector's EDC/ECC data. The pointer to appropriate location in
 * sector's data buffer is stored into @ret_buf;  therefore, the buffer should not
 * be modified.
 *
 * If EDC/ECC data is not provided by image file(s), it is generated.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_sector_get_edc_ecc (MirageSector *self, const guint8 **ret_buf, gint *ret_len, GError **error)
{
    gboolean succeeded;
    gint offset = 0, length = 0;

    /* Generate EDC/ECC if it's not provided; generation routine takes care of
       incompatible sector types */
    if (!(self->priv->valid_data & MIRAGE_VALID_EDC_ECC)) {
        mirage_sector_generate_edc_ecc(self);
    }

    /* Get offset and length */
    succeeded = mirage_sector_get_edc_ecc_offset_and_length(self, &offset, &length, error);

    /* Return the requested data */
    if (ret_buf) {
        *ret_buf = succeeded ? self->priv->sector_data + offset : NULL;
    }
    if (ret_len) {
        *ret_len = succeeded ? length : 0;
    }

    return succeeded;
}

gboolean mirage_sector_set_edc_ecc (MirageSector *self, const guint8 *buf, gint len, GError **error)
{
    gint offset, expected_length;

    /* Get offset and length */
    if (!mirage_sector_get_edc_ecc_offset_and_length(self, &offset, &expected_length, error)) {
        return FALSE;
    }

    /* Validate length */
    if (len != expected_length) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SECTOR_ERROR, "Expected %d bytes for EDC/ECC!", expected_length);
        return FALSE;
    }

    /* Copy */
    memcpy(self->priv->sector_data + offset, buf, len);

    /* Mark as both real and valid */
    self->priv->real_data |= MIRAGE_VALID_EDC_ECC;
    self->priv->valid_data |= MIRAGE_VALID_EDC_ECC;

    return TRUE;
}

/**
 * mirage_sector_get_subchannel:
 * @self: a #MirageSector
 * @format: (in): subchannel format
 * @ret_buf: (out) (transfer none) (allow-none) (array length=ret_len): location to store pointer to buffer containing subchannel, or %NULL
 * @ret_len: (out) (allow-none): location to store length of subchannel data, or %NULL. Length is given in bytes.
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves sector's subchannel. @format must be one of #MirageSectorSubchannelFormat.
 * The pointer to appropriate location in sector's data buffer is stored into
 * @ret_buf;  therefore, the buffer should not be modified.
 *
 * If subchannel is not provided by image file(s), it is generated.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_sector_get_subchannel (MirageSector *self, MirageSectorSubchannelFormat format, const guint8 **ret_buf, gint *ret_len, GError **error)
{
    /* Generate subchannel if it's not provided */
    if (!(self->priv->valid_data & MIRAGE_VALID_SUBCHAN)) {
        mirage_sector_generate_subchannel(self);
    }

    switch (format) {
        case MIRAGE_SUBCHANNEL_NONE: {
            /* No subchannel */
            if (ret_buf) {
                *ret_buf = NULL;
            }
            if (ret_len) {
                *ret_len = 0;
            }
            break;
        }
        case MIRAGE_SUBCHANNEL_PW: {
            /* Interleaved PW subchannel */
            if (ret_buf) {
                *ret_buf = self->priv->subchan_pw;
            }
            if (ret_len) {
                *ret_len = 96;
            }
            break;
        }
        case MIRAGE_SUBCHANNEL_Q: {
            /* De-interleaved Q subchannel */
            memset(self->priv->subchan_q, 0, sizeof(self->priv->subchan_q));
            mirage_helper_subchannel_deinterleave(SUBCHANNEL_Q, self->priv->subchan_pw, self->priv->subchan_q);
            if (ret_buf) {
                *ret_buf = self->priv->subchan_q;
            }
            if (ret_len) {
                *ret_len = 16;
            }
            break;
        }
        case MIRAGE_SUBCHANNEL_RW: {
            /* Cooked RW subchannel contains P and Q subchannel data
               interleaved in bits 7 and 6, and bits 5-0 contain R-W
               data packets. For now, we pretend there is no data in
               R-W, and therefore we can return raw PW subchannel here */
            if (ret_buf) {
                *ret_buf = self->priv->subchan_pw;
            }
            if (ret_len) {
                *ret_len = 96;
            }
            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: subchannel format %d not supported yet!\n", __debug__, format);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SECTOR_ERROR, "Subchannel format %d not supported yet!", format);
            return FALSE;
        }
    }

    return TRUE;
}

gboolean mirage_sector_set_subchannel (MirageSector *self, MirageSectorSubchannelFormat format, const guint8 *buf, gint len, GError **error)
{
    switch (format) {
        case MIRAGE_SUBCHANNEL_PW: {
            /* Interleaved PW subchannel */
            if (len != 96) {
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SECTOR_ERROR, "Expected 96 bytes for PW subchannel!");
                return FALSE;
            }
            /* Copy */
            memcpy(self->priv->subchan_pw, buf, len);
            break;
        }
        case MIRAGE_SUBCHANNEL_Q: {
            /* De-interleaved Q subchannel */
            if (len != 16) {
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SECTOR_ERROR, "Expected 16 bytes for Q subchannel!");
                return FALSE;
            }
            /* Interleave Q (first 12 bytes) */
            memset(self->priv->subchan_pw, 0, sizeof(self->priv->subchan_pw));
            mirage_helper_subchannel_interleave(SUBCHANNEL_Q, buf, self->priv->subchan_pw);

            /* Byte 15 actually has P state in its most-significant bit
               (but only if sectors are fed from recording code; when
               reading, P bit is not returned) */
            if (buf[15]) {
                const guint8 p_subchannel[12] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
                mirage_helper_subchannel_interleave(SUBCHANNEL_P, p_subchannel, self->priv->subchan_pw);
            }

            break;
        }
        case MIRAGE_SUBCHANNEL_RW: {
            /* Cooked RW subchannel */
            if (len != 96) {
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SECTOR_ERROR, "Expected 96 bytes for RW subchannel!");
                return FALSE;
            }

            /* Cooked RW subchannel contains P and Q subchannel data
               interleaved in bits 7 and 6, and bits 5-0 contain R-W
               data packets. For now, we are interested only in P-Q data,
               which we could deinterleave out of RW buffer and interleave
               it back into our PW buffer; but if its more efficient if
               we copy it directly */
            for (gint i = 0; i < 96; i++) {
                self->priv->subchan_pw[i] = buf[i] & 0xC0;
            }
            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: subchannel format %d not supported yet!\n", __debug__, format);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_SECTOR_ERROR, "Subchannel format %d not supported yet!", format);
            return FALSE;
        }
    }

    /* Mark as both real and valid */
    self->priv->real_data |= MIRAGE_VALID_SUBCHAN;
    self->priv->valid_data |= MIRAGE_VALID_SUBCHAN;

    return TRUE;
}


/**
 * mirage_sector_verify_lec:
 * @self: a #MirageSector
 *
 * Verifies the sector data in terms of L-EC error detection/correction.
 * Data sectors (Mode 1, Mode 2 Form 1 and Mode 2 Form 2) contain error
 * detection/error correction codes which is part of so called layered
 * error correction. This function calculates the EDC for sector data and
 * compares it with EDC provided by the image file.
 *
 * As a result of comparison, the sectors with intentionally faulty EDC
 * (and possibly ECC, though ECC is not verified) can be discovered.
 *
 * This function requires EDC/ECC data to be provided by the image. If it
 * is not provided, it would be generated by #MirageSector on first access
 * via mirage_sector_get_edc_ecc() using the same algorithm as the one used
 * by this function. Therefore, in case of EDC/ECC data missing, the verification
 * automatically succeeds.
 *
 * Returns: %TRUE if sector passes verification (i.e. no L-EC errors are detected) otherwise %FALSE
 */
gboolean mirage_sector_verify_lec (MirageSector *self)
{
    gboolean valid = TRUE;

    /* Validation is possible only if EDC/ECC data was provided by the
       image file; if it is missing, it is generated by same algorithm
       as used in verification. */
    if (self->priv->real_data & MIRAGE_VALID_EDC_ECC) {
        /* I believe calculating EDC suffices for this test; ECC should
           not really be needed, since we won't be doing any corrections */
        guint8 computed_edc[4];

        switch (self->priv->type) {
            case MIRAGE_SECTOR_MODE1: {
                /* We assume sync and header data are available, which is probably
                   a reasonable assumption at this point... */
                mirage_helper_sector_edc_ecc_compute_edc_block(self->priv->sector_data+0x00, 0x810, computed_edc);
                valid = !memcmp(computed_edc, self->priv->sector_data+0x810, 4);
                break;
            }
            case MIRAGE_SECTOR_MODE2_FORM1: {
                /* We assume subheader data is available... */
                mirage_helper_sector_edc_ecc_compute_edc_block(self->priv->sector_data+0x10, 0x808, computed_edc);
                valid = !memcmp(computed_edc, self->priv->sector_data+0x818, 4);
                break;
            }
            case MIRAGE_SECTOR_MODE2_FORM2: {
                /* We assume subheader data is available... */
                mirage_helper_sector_edc_ecc_compute_edc_block(self->priv->sector_data+0x10, 0x91C, computed_edc);
                valid = !memcmp(computed_edc, self->priv->sector_data+0x92C, 4);
                break;
            }
            default: {
                break; /* Succeed for other sector types */
            }
        }
    }

    return valid;
}


/**
 * mirage_sector_verify_subchannel_crc:
 * @self: a #MirageSector
 *
 * Verifies the Q subchannel's CRC for the sector.
 *
 * As a result of comparison, the sectors with intentionally faulty Q subchannel
 * can be discovered.
 *
 * This function requires subchannel data to be provided by the image. If it
 * is not provided, it would be generated by #MirageSector on first access
 * via mirage_sector_get_subchannel() using the same algorithm as the one used
 * by this function. Therefore, in case of subchannel data missing, the verification
 * automatically succeeds.
 *
 * Returns: %TRUE if sector's Q subchannel CRC passes verification otherwise %FALSE
 */
gboolean mirage_sector_verify_subchannel_crc (MirageSector *self)
{
    gboolean valid = TRUE;

    /* Validation is possible only if subchannel data was provided by the
       image file; if it is missing, it is generated by same algorithm
       as used in verification. */
    if (self->priv->real_data & MIRAGE_VALID_SUBCHAN) {
        guint16 computed_crc;
        const guint8 *buf;
        gint buflen;

        /* Get Q subchannel */
        mirage_sector_get_subchannel(self, MIRAGE_SUBCHANNEL_Q, &buf, &buflen, NULL);

        /* Compute CRC */
        computed_crc = mirage_helper_subchannel_q_calculate_crc(&buf[0]);

        /* Compare */
        valid = computed_crc == ((buf[10] << 8) | buf[11]);
    }

    return valid;
}


/**
 * mirage_sector_scramble:
 * @self: a #MirageSector
 *
 * Scrambles 2340 bytes of sector data after sync pattern, using scrambler
 * from ECMA-130 Annex B. Running this function on already-scrambled
 * sector results in unscrambling.
 */
void mirage_sector_scramble (MirageSector *self)
{
    /* Make sure scrambler LUT is initialized */
    if (!ecma_130_scrambler_lut) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: cannot scramble sector - scrambler LUT not initialized!\n", __debug__);
        return;
    }

    for (gint i = 0; i < 2340; i++) {
        *(self->priv->sector_data + 12 + i) ^= ecma_130_scrambler_lut[i];
    }
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE(MirageSector, mirage_sector, MIRAGE_TYPE_OBJECT);


static void mirage_sector_init (MirageSector *self)
{
    self->priv = MIRAGE_SECTOR_GET_PRIVATE(self);

    /* Un-initialize sector type */
    self->priv->type = 0xDEADBEEF;
}

static void mirage_sector_class_init (MirageSectorClass *klass)
{
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageSectorPrivate));
}


/**********************************************************************\
 *                        Subchannel generation                       *
\**********************************************************************/
static gint subchannel_generate_p (MirageSector *self, guint8 *buf)
{
    MirageTrack *track;
    gint track_start;
    gint relative_address;

    /* Get sector's parent track */
    track = mirage_object_get_parent(MIRAGE_OBJECT(self));
    if (!track) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get sector's parent!\n", __debug__);
        return 12;
    }

    relative_address = self->priv->address - mirage_track_layout_get_start_sector(track);
    track_start = mirage_track_get_track_start(track);

    /* P subchannel being 0xFF indicates we're in the pregap */
    if (relative_address < track_start) {
        memset(buf, 0xFF, 12);
    } else {
        memset(buf, 0, 12);
    }

    /* Release sector's parent track */
    g_object_unref(track);

    return 12;
}

static gint subchannel_generate_q (MirageSector *self, guint8 *buf)
{
    MirageTrack *track;

    gint mode_switch;
    gint relative_address, absolute_address;
    guint16 crc;

    /* Get sector's parent track */
    track = mirage_object_get_parent(MIRAGE_OBJECT(self));
    if (!track) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get sector's parent!\n", __debug__);
        return 12;
    }

    absolute_address = self->priv->address;
    relative_address = self->priv->address - mirage_track_layout_get_start_sector(track);

    /* We support Mode-1, Mode-2 and Mode-3 Q; according to INF8090 and MMC-3,
       "if used, they shall exist in at least one out of 100 consecutive sectors".
       So we put MCN in every 25th sector and ISRC in every 50th sector */

    /* Track number, index, absolute and relative track adresses are converted
       from HEX to BCD */

    switch (relative_address % 100) {
        case 25: {
            /* MCN is to be returned; check if we actually have it */
            MirageSession *session;

            session = mirage_object_get_parent(MIRAGE_OBJECT(track));

            if (!mirage_session_get_mcn(session)) {
                mode_switch = 0x01;
            } else {
                mode_switch = 0x02;
            }

            g_object_unref(session);

            break;
        }
        case 50: {
            /* ISRC is to be returned; verify that this is an audio track and
               that it actually has ISRC set */
            gint mode = mirage_sector_get_sector_type(self);

            if (mode != MIRAGE_SECTOR_AUDIO) {
                mode_switch = 0x01;
            } else if (!mirage_track_get_isrc(track)) {
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

    switch (mode_switch) {
        case 0x01: {
            /* Mode-1: Current position */
            gint ctl, track_number;
            gint track_start;
            MirageIndex *index;

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: generating Mode-1 Q: Position\n", __debug__);

            ctl = mirage_track_get_ctl(track);
            track_number = mirage_track_layout_get_track_number(track);

            track_start = mirage_track_get_track_start(track);

            buf[0] = (ctl << 0x04) | 0x01; /* Mode-1 Q */
            buf[1] = mirage_helper_hex2bcd(track_number); /* Track number */

            /* Index: try getting index object by address; if it's not found, we
               check if sector lies before track start... */
            index = mirage_track_get_index_by_address(track, relative_address, NULL);
            if (index) {
                gint index_number = mirage_index_get_number(index);
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: relative address 0x%X belongs to index with number: %d\n", __debug__, relative_address, index_number);
                buf[2] = index_number;
                g_object_unref(index);
            } else {
                /* No index... check if address is in a pregap (strictly less than track start) */
                if (relative_address < track_start) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: relative address 0x%X is part of a pregap\n", __debug__, relative_address);
                    buf[2] = 0;
                } else {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: relative address 0x%X belongs to index 1\n", __debug__, relative_address);
                    buf[2] = 1;
                }
            }
            buf[2] = mirage_helper_hex2bcd(buf[2]);

            /* Relative M/S/F; when converting, we do not add 2 seconds */
            mirage_helper_lba2msf(ABS(relative_address - track_start), FALSE, &buf[3], &buf[4], &buf[5]);
            buf[3] = mirage_helper_hex2bcd(buf[3]);
            buf[4] = mirage_helper_hex2bcd(buf[4]);
            buf[5] = mirage_helper_hex2bcd(buf[5]);
            buf[6] = 0; /* Zero */
            /* Absolute M/S/F */
            mirage_helper_lba2msf(absolute_address, TRUE, &buf[7], &buf[8], &buf[9]);
            buf[7] = mirage_helper_hex2bcd(buf[7]);
            buf[8] = mirage_helper_hex2bcd(buf[8]);
            buf[9] = mirage_helper_hex2bcd(buf[9]);
            break;
        }
        case 0x02: {
            /* Mode-2: MCN */
            gint ctl;

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: generating Mode-2 Q: MCN\n", __debug__);

            ctl = mirage_track_get_ctl(track);
            buf[0] = (ctl << 0x04) | 0x02; /* Mode-2 Q */

            /* MCN */
            MirageSession *session;

            const gchar *mcn;

            session = mirage_object_get_parent(MIRAGE_OBJECT(track));

            mcn = mirage_session_get_mcn(session);

            g_object_unref(session);

            mirage_helper_subchannel_q_encode_mcn(&buf[1], mcn);
            buf[8] = 0; /* zero */
            /* AFRAME */
            mirage_helper_lba2msf(absolute_address, TRUE, NULL, NULL, &buf[9]);
            buf[9] = mirage_helper_hex2bcd(buf[9]);
            break;
        }
        case 0x03: {
            /* Mode-3: ISRC */
            gint ctl;

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: generating Mode-3 Q: ISRC\n", __debug__);

            ctl = mirage_track_get_ctl(track);
            buf[0] = (ctl << 0x04) | 0x03; /* Mode-3 Q */

            /* ISRC*/
            const gchar *isrc = mirage_track_get_isrc(track);
            mirage_helper_subchannel_q_encode_isrc(&buf[1], isrc);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_SECTOR, "%s: ISRC string: %s bytes: %02X %02X %02X %02X %02X %02X %02X %02X\n", __debug__, isrc, buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], buf[8]);
            /* AFRAME */
            mirage_helper_lba2msf(absolute_address, TRUE, NULL, NULL, &buf[9]);
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

static gint subchannel_generate_r (MirageSector *self G_GNUC_UNUSED, guint8 *buf)
{
    memset(buf, 0, 12);
    return 12;
}

static gint subchannel_generate_s (MirageSector *self G_GNUC_UNUSED, guint8 *buf)
{
    memset(buf, 0, 12);
    return 12;
}

static gint subchannel_generate_t (MirageSector *self G_GNUC_UNUSED, guint8 *buf)
{
    memset(buf, 0, 12);
    return 12;
}

static gint subchannel_generate_u (MirageSector *self G_GNUC_UNUSED, guint8 *buf)
{
    memset(buf, 0, 12);
    return 12;
}

static gint subchannel_generate_v (MirageSector *self G_GNUC_UNUSED, guint8 *buf)
{
    memset(buf, 0, 12);
    return 12;
}

static gint subchannel_generate_w (MirageSector *self G_GNUC_UNUSED, guint8 *buf)
{
    memset(buf, 0, 12);
    return 12;
}

static void mirage_sector_generate_subchannel (MirageSector *self)
{
    guint8 tmp_buf[12];

    /* Generate subchannel: only P/Q can be generated at the moment
       (other subchannels are set to 0) */

    memset(self->priv->subchan_pw, 0, sizeof(self->priv->subchan_pw));

    /* Read P subchannel into temporary buffer, then interleave it */
    subchannel_generate_p(self, tmp_buf);
    mirage_helper_subchannel_interleave(SUBCHANNEL_P, tmp_buf, self->priv->subchan_pw);
    /* Read Q subchannel into temporary buffer, then interleave it */
    subchannel_generate_q(self, tmp_buf);
    mirage_helper_subchannel_interleave(SUBCHANNEL_Q, tmp_buf, self->priv->subchan_pw);
    /* Read R subchannel into temporary buffer, then interleave it */
    subchannel_generate_r(self, tmp_buf);
    mirage_helper_subchannel_interleave(SUBCHANNEL_R, tmp_buf, self->priv->subchan_pw);
    /* Read S subchannel into temporary buffer, then interleave it */
    subchannel_generate_s(self, tmp_buf);
    mirage_helper_subchannel_interleave(SUBCHANNEL_S, tmp_buf, self->priv->subchan_pw);
    /* Read T subchannel into temporary buffer, then interleave it */
    subchannel_generate_t(self, tmp_buf);
    mirage_helper_subchannel_interleave(SUBCHANNEL_T, tmp_buf, self->priv->subchan_pw);
    /* Read U subchannel into temporary buffer, then interleave it */
    subchannel_generate_v(self, tmp_buf);
    mirage_helper_subchannel_interleave(SUBCHANNEL_U, tmp_buf, self->priv->subchan_pw);
    /* Read V subchannel into temporary buffer, then interleave it */
    subchannel_generate_u(self, tmp_buf);
    mirage_helper_subchannel_interleave(SUBCHANNEL_V, tmp_buf, self->priv->subchan_pw);
    /* Read W subchannel into temporary buffer, then interleave it */
    subchannel_generate_w(self, tmp_buf);
    mirage_helper_subchannel_interleave(SUBCHANNEL_W, tmp_buf, self->priv->subchan_pw);
}
