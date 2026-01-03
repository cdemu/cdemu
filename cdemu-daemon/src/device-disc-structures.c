/*
 *  CDEmu daemon: device - fabrication of disc structures
 *  Copyright (C) 2018 Rok Mandeljc
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

#include "cdemu.h"
#include "device-private.h"


/**********************************************************************\
 *                          Disc structures                           *
\**********************************************************************/
#pragma pack(1)

struct DISC_STRUCTURE_ListEntry
{
    guint8 format_code;

#if G_BYTE_ORDER == G_BIG_ENDIAN
    guint8 sds : 1;
    guint8 rds : 1;
    guint8 reserved1 : 6;
#else
    guint8 reserved1 : 6;
    guint8 rds : 1;
    guint8 sds : 1;
#endif

    guint16 structure_length;
};

struct DVD_STRUCTURE_PhysicalFormat
{
#if G_BYTE_ORDER == G_BIG_ENDIAN
    guint8 book_type : 4;
    guint8 part_ver : 4;
#else
    guint8 part_ver : 4;
    guint8 book_type : 4;
#endif

#if G_BYTE_ORDER == G_BIG_ENDIAN
    guint8 disc_size : 4;
    guint8 max_rate : 4;
#else
    guint8 max_rate : 4;
    guint8 disc_size : 4;
#endif

#if G_BYTE_ORDER == G_BIG_ENDIAN
    guint8 reserved1 : 1;
    guint8 num_layers : 2;
    guint8 track_path : 1;
    guint8 layer_type : 4;
#else
    guint8 layer_type : 4;
    guint8 track_path : 1;
    guint8 num_layers : 2;
    guint8 reserved1 : 1;
#endif

#if G_BYTE_ORDER == G_BIG_ENDIAN
    guint8 linear_density : 4;
    guint8 track_density : 4;
#else
    guint8 track_density : 4;
    guint8 linear_density : 4;
#endif

#if G_BYTE_ORDER == G_BIG_ENDIAN
    guint32 reserved2 : 8;
    guint32 data_start : 24;
#else
    guint32 data_start : 24;
    guint32 reserved2 : 8;
#endif

#if G_BYTE_ORDER == G_BIG_ENDIAN
    guint32 reserved3 : 8;
    guint32 data_end : 24;
#else
    guint32 data_end : 24;
    guint32 reserved3 : 8;
#endif

#if G_BYTE_ORDER == G_BIG_ENDIAN
    guint32 reserved4 : 8;
    guint32 layer0_end : 24;
#else
    guint32 layer0_end : 24;
    guint32 reserved4 : 8;
#endif

#if G_BYTE_ORDER == G_BIG_ENDIAN
    guint8 bca : 1;
    guint8 reserved5 : 7;
#else
    guint8 reserved5 : 7;
    guint8 bca : 1;
#endif

    guint8 media_specific[2031];
};

struct DVD_STRUCTURE_Copyright
{
    guint8 copy_protection;
    guint8 region_info;
    guint8 reserved1;
    guint8 reserved2;
};

#pragma pack()


/**********************************************************************\
 *                              DVD disc                              *
\**********************************************************************/
static gboolean cdemu_device_generate_dvd_structure (CdemuDevice *self, gint layer G_GNUC_UNUSED, gint format, guint8 **structure_buffer, gint *structure_length)
{
    /* Clear the outputs, just in case */
    *structure_buffer = NULL;
    *structure_length = 0;

    switch (format) {
        case 0x00: {
            /* 00h: Physical Format Information */
            struct DVD_STRUCTURE_PhysicalFormat *info = g_new0(struct DVD_STRUCTURE_PhysicalFormat, 1);

            gint disc_length = mirage_disc_layout_get_length(self->priv->disc);
            gint num_layers = (disc_length >= 2295104) ? 2 : 1; /* Attempt to detect dual-layer disc */

            if (self->priv->current_profile == PROFILE_DVDROM) {
                /* DVD-ROM */
                info->book_type = 0x00; /* DVD-ROM */
                info->part_ver = 0x01;
                info->disc_size = 0x00; /* 120mm disc */
                info->max_rate = 0x02; /* 10.08 Mbps */
                info->num_layers = (num_layers == 2); /* 0x00: 1 layer, 0x01: 2 layers */
                info->track_path = 0; /* Parallell track path */
                info->layer_type = 1 << 0; /* Layer contains embossed data (bit 0) */
                info->linear_density = 0; /* 0.267 um/bit */
                info->track_density = 0; /* 0.74 um/track */

                /* The following three fields are 24-bit... */
                info->data_start = GUINT32_FROM_BE(0x30000) >> 8; /* DVD-ROM, DVD-R/-RW, DVD+RW */
                info->data_end = GUINT32_FROM_BE(0x30000+disc_length) >> 8; /* FIXME: It seems lead-in (out?) length should be subtracted here (241-244 sectors...) */
                info->layer0_end = GUINT32_FROM_BE(0x00) >>8; /* We do not use Opposite track path, so leave at 0 */
                info->bca = 0;
            } else {
                /* DVD+R */
                info->book_type = 0x0A; /* DVD+R */
                info->part_ver = 0x02;
                info->disc_size = 0x00; /* 120mm disc */
                info->max_rate = 0x0F; /* Max rate unspecified */
                info->num_layers = 0; /* Single layer */
                info->track_path = 0; /* Parallel track path */
                info->layer_type = 1 << 1; /* Layer contains recordable area (bit 1) */
                info->linear_density = 0; /* 0.267 um/bit */
                info->track_density = 0; /* 0.74 um/track */

                /* The following three fields are 24-bit... */
                info->data_start = GUINT32_FROM_BE(0x30000) >> 8; /* DVD-ROM, DVD-R/-RW, DVD+RW */
                info->data_end = GUINT32_FROM_BE(0x260500) >> 8; /* Max capacity? */
                info->layer0_end = GUINT32_FROM_BE(0x00) >>8; /* We do not use Opposite track path, so leave at 0 */
                info->bca = 0;
            }

            *structure_buffer = (guint8 *)info;
            *structure_length = sizeof(struct DVD_STRUCTURE_PhysicalFormat);

            return TRUE;
        }

        case 0x01: {
            /* 01h: DVD Copyright Information */
            struct DVD_STRUCTURE_Copyright *info = g_new0(struct DVD_STRUCTURE_Copyright, 1);

            if (self->priv->dvd_report_css) {
                info->copy_protection = 0x01; /* CSS/CPPM */
                info->region_info = 0x00; /* Playable in all regions */
            } else {
                info->copy_protection = 0x00;/* None */
                info->region_info = 0x00; /* N/A */
            }

            *structure_buffer = (guint8 *)info;
            *structure_length = sizeof(struct DVD_STRUCTURE_Copyright);

            return TRUE;
        }
        case 0x04: {
            /* Manufacturing data: return 2048 bytes, all zero */
            *structure_buffer = g_malloc0(2048);
            *structure_length = 2048;

            return TRUE;
        }
        case 0xFF: {
            /* List of all supported disc structures; this appears to be
             * a capability list of the device rather than the list of
             * present structures on the disc. So we return a hard-coded
             * list based on what my physical drive returns. */

            /* Note: the lengths that are not mandated by the MMC are
             * taken from what my physical drive returns. */
            const struct DISC_STRUCTURE_ListEntry entries[] = {
                {.format_code = 0x00, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 2048)},
                {.format_code = 0x01, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 4)},
                {.format_code = 0x02, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 2048)},
                {.format_code = 0x03, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 188)},
                {.format_code = 0x04, .sds = 1, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 2048)},
                {.format_code = 0x05, .sds = 1, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 4)},
                {.format_code = 0x06, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 20)},
                {.format_code = 0x07, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 24576)},
                {.format_code = 0x08, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 2048)},
                {.format_code = 0x09, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 4)},
                {.format_code = 0x0A, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 12)},
                {.format_code = 0x0B, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 4)},
                {.format_code = 0x0C, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 30720)},
                {.format_code = 0x0D, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 32772)},
                {.format_code = 0x0E, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 64)},
                {.format_code = 0x0F, .sds = 1, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 18)},
                {.format_code = 0x10, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 2048)},
                {.format_code = 0x11, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 256)},
                {.format_code = 0x20, .sds = 1, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 8)},
                {.format_code = 0x21, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 8)},
                {.format_code = 0x22, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 8)},
                {.format_code = 0x23, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 8)},
                {.format_code = 0x24, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 8)},
                {.format_code = 0x30, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 32768)},
                {.format_code = 0x82, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 32)},
                {.format_code = 0x86, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 24576)},
                {.format_code = 0xC0, .sds = 1, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 4)},
                {.format_code = 0xFF, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(4 + 112)},
            };

            *structure_buffer = g_malloc(sizeof(entries));
            memcpy(*structure_buffer, entries, sizeof(entries));
            *structure_length = sizeof(entries);

            return TRUE;
        }
    }

    return FALSE;
}


/**********************************************************************\
 *                            Blu-ray disc                            *
\**********************************************************************/
static gboolean cdemu_device_generate_bluray_structure (CdemuDevice *self G_GNUC_UNUSED, gint layer G_GNUC_UNUSED, gint format G_GNUC_UNUSED, guint8 **structure_buffer, gint *structure_length)
{
    switch (format) {
        case 0x00: {
            /* Disc Information (DI) */

            /* The disc information structure consists of one or more
             * disc information (DI) units and emergency brake (EB) data
             * units. The structure of the former is only roughly
             * outlined in the MMC-5, while details are in BluRay specs
             * that are not publicly available.
             *
             * Therefore, for now, we return only zeroed data buffer.*/
            *structure_buffer = g_malloc0(4096);
            *structure_length = 4096;

            return TRUE;
        }
        case 0xFF: {
            /* For some reason, my physical drive reports length of 0 for all ...*/
            const struct DISC_STRUCTURE_ListEntry entries[] = {
                {.format_code = 0x00, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(0)},
                {.format_code = 0x03, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(0)},
                {.format_code = 0x08, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(0)},
                {.format_code = 0x09, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(0)},
                {.format_code = 0x0A, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(0)},
                {.format_code = 0x0F, .sds = 1, .rds = 0, .structure_length = GUINT16_TO_BE(0)},
                {.format_code = 0x12, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(0)},
                {.format_code = 0x30, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(0)},
                {.format_code = 0x80, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(0)},
                {.format_code = 0x81, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(0)},
                {.format_code = 0x82, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(0)},
                {.format_code = 0x84, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(0)},
                {.format_code = 0xC0, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(0)},
                {.format_code = 0xFF, .sds = 0, .rds = 1, .structure_length = GUINT16_TO_BE(0)},
            };

            *structure_buffer = g_malloc(sizeof(entries));
            memcpy(*structure_buffer, entries, sizeof(entries));
            *structure_length = sizeof(entries);

            return TRUE;
        }
    }

    *structure_buffer = NULL;
    *structure_length = 0;

    return FALSE;
}


// *********************************************************************
// *                              Dispatch                             *
// *********************************************************************
gboolean cdemu_device_generate_disc_structure (CdemuDevice *self, gint layer, gint format, guint8 **structure_buffer, gint *structure_length)
{
    switch (self->priv->current_profile) {
        case PROFILE_DVDROM:
        case PROFILE_DVDPLUSR: {
            return cdemu_device_generate_dvd_structure(self, layer, format, structure_buffer, structure_length);
        }
        case PROFILE_BDROM:
        case PROFILE_BDR_SRM: {
            return cdemu_device_generate_bluray_structure(self, layer, format, structure_buffer, structure_length);
        }
        default: {
            *structure_buffer = NULL;
            *structure_length = 0;
            return FALSE;
        }
    }
}
