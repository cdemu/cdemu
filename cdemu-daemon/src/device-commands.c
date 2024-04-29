/*
 *  CDEmu daemon: device - packet commands
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

#include "cdemu.h"
#include "device-private.h"

#define __debug__ "MMC-3"


/**********************************************************************\
 *                               Helpers                              *
\**********************************************************************/
static gint map_expected_sector_type (gint type)
{
    switch (type) {
        case 0: return 0; /* All types */
        case 1: return MIRAGE_SECTOR_AUDIO; /* CD-DA */
        case 2: return MIRAGE_SECTOR_MODE1; /* Mode 1 */
        case 3: return MIRAGE_SECTOR_MODE2; /* Mode 2 Formless */
        case 4: return MIRAGE_SECTOR_MODE2_FORM1; /* Mode 2 Form 1 */
        case 5: return MIRAGE_SECTOR_MODE2_FORM2; /* Mode 2 Form 2 */
        default: return -1;
    }
}

static gint map_session_type (MirageSessionType type)
{
    switch (type) {
        case MIRAGE_SESSION_CDDA:
        case MIRAGE_SESSION_CDROM: return 0;
        case MIRAGE_SESSION_CDI: return 0x10;
        case MIRAGE_SESSION_CDROM_XA: return 0x20;
    }

    return 0;
}

static gint read_sector_data (MirageSector *sector, MirageDisc *disc, gint address, guint8 mcsb_byte, gint subchannel, guint8 *buffer, GError **error)
{
    guint8 *ptr = buffer;
    gint read_length = 0;

    const guint8 *tmp_buf;
    gint tmp_len;

    /* If sector is provided, use it... */
    if (sector) {
        g_object_ref(sector);
    } else {
        /* ... otherwise, obtain it */
        sector = mirage_disc_get_sector(disc, address, error);
        if (!sector) {
            return -1;
        }
    }

    /* Main channel selection byte */
    if (mcsb_byte) {
        struct READ_CD_MSCB *mcsb = (struct READ_CD_MSCB *)&mcsb_byte;

        /* Sync */
        if (mcsb->sync) {
            mirage_sector_get_sync(sector, &tmp_buf, &tmp_len, NULL);
            memcpy(ptr, tmp_buf, tmp_len);
            ptr += tmp_len;
            read_length += tmp_len;
        }

        /* Header */
        if (mcsb->header) {
            mirage_sector_get_header(sector, &tmp_buf, &tmp_len, NULL);
            memcpy(ptr, tmp_buf, tmp_len);
            ptr += tmp_len;
            read_length += tmp_len;
        }

        /* Subheader */
        if (mcsb->subheader) {
            mirage_sector_get_subheader(sector, &tmp_buf, &tmp_len, NULL);
            memcpy(ptr, tmp_buf, tmp_len);
            ptr += tmp_len;
            read_length += tmp_len;
        }

        /* Data */
        if (mcsb->data) {
            mirage_sector_get_data(sector, &tmp_buf, &tmp_len, NULL);
            memcpy(ptr, tmp_buf, tmp_len);
            ptr += tmp_len;
            read_length += tmp_len;
        }

        /* EDC/ECC */
        if (mcsb->edc_ecc) {
            mirage_sector_get_edc_ecc(sector, &tmp_buf, &tmp_len, NULL);
            memcpy(ptr, tmp_buf, tmp_len);
            ptr += tmp_len;
            read_length += tmp_len;
        }

        /* C2 error bits: fill with zeros */
        switch (mcsb->c2_error) {
            case 0x01: {
                /* C2 error block data */
                tmp_len = 294;
                break;
            }
            case 0x02: {
                /* C2 and block error bits */
                tmp_len = 296;
                break;
            }
            default: {
                tmp_len = 0;
                break;
            }
        }
        memset(ptr, 0, tmp_len);
        ptr += tmp_len;
        read_length += tmp_len;
    }

    /* Subchannel: we support only RAW and Q */
    switch (subchannel) {
        case 0x01: {
            mirage_sector_get_subchannel(sector, MIRAGE_SUBCHANNEL_PW, &tmp_buf, &tmp_len, NULL);
            break;
        }
        case 0x02: {
            mirage_sector_get_subchannel(sector, MIRAGE_SUBCHANNEL_Q, &tmp_buf, &tmp_len, NULL);
            break;
        }
        default: {
            tmp_buf = NULL;
            tmp_len = 0;
        }
    }
    memcpy(ptr, tmp_buf, tmp_len);
    ptr += tmp_len;
    read_length += tmp_len;

    /* Release sector */
    g_object_unref(sector);

    return read_length;
}


/**********************************************************************\
 *                     Packet command implementations                 *
\**********************************************************************/
/* CLOSE TRACK/SESSION */
static gboolean command_close_track_session (CdemuDevice *self, const guint8 *raw_cdb)
{
    struct CLOSE_TRACK_SESSION_CDB *cdb = (struct CLOSE_TRACK_SESSION_CDB *)raw_cdb;

    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: close function: %d, track/session: %d\n", __debug__, cdb->function, cdb->number);

    if (cdb->function == 1) {
        /* Track */
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: closing track %d\n", __debug__, cdb->number);

        if (!self->priv->recording) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: no recording mode set!\n", __debug__);
            cdemu_device_write_sense(self, ILLEGAL_REQUEST, COMMAND_SEQUENCE_ERROR);
            return FALSE;
        }

        return self->priv->recording->close_track(self);
    } else if (cdb->function == 2) {
        /* Session */
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: closing last session\n", __debug__);

        if (!self->priv->recording) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: no recording mode set!\n", __debug__);
            cdemu_device_write_sense(self, ILLEGAL_REQUEST, COMMAND_SEQUENCE_ERROR);
            return FALSE;
        }

        return self->priv->recording->close_session(self);
    } else if (cdb->function == 5 || cdb->function == 6) {
        /* Finalize disc/finalize disc with minimal radius (DVD+R) */
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: finalizing DVD+R disc... same as closing session\n", __debug__);

        if (!self->priv->recording) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: no recording mode set!\n", __debug__);
            cdemu_device_write_sense(self, ILLEGAL_REQUEST, COMMAND_SEQUENCE_ERROR);
            return FALSE;
        }

        return self->priv->recording->close_session(self);
    } else {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: unimplemented close function: %d\n", __debug__, cdb->function);
        cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }

    return TRUE;
}

/* GET CONFIGURATION*/
static gboolean command_get_configuration (CdemuDevice *self, const guint8 *raw_cdb)
{
    struct GET_CONFIGURATION_CDB *cdb = (struct GET_CONFIGURATION_CDB *)raw_cdb;
    struct GET_CONFIGURATION_Header *ret_header = (struct GET_CONFIGURATION_Header *)self->priv->buffer;
    self->priv->buffer_size = sizeof(struct GET_CONFIGURATION_Header);
    guint8 *ret_data = self->priv->buffer+self->priv->buffer_size;

    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: requesting features from 0x%X on, with RT flag 0x%X\n", __debug__, GUINT16_FROM_BE(cdb->sfn), cdb->rt);

    /* Go over *all* features, and copy them according to RT value */
    for (GList *entry = self->priv->features_list; entry; entry = entry->next) {
        struct FeatureGeneral *feature = entry->data;

        /* We want this feature copied if:
            a) RT is 0x00 and feature's code >= SFN
            b) RT is 0x01, feature's code >= SFN and feature has 'current' bit set
            c) RT is 0x02 and feature's code == SFN

           NOTE: because in case c) we break loop as soon as a feature is copied and
           because we have features sorted in ascending order, we can use comparison
           "feature's code >= SFN" in all cases.
        */
        if (GUINT16_FROM_BE(feature->code) >= GUINT16_FROM_BE(cdb->sfn)) {
            if ((cdb->rt == 0x00) ||
                (cdb->rt == 0x01 && feature->cur) ||
                (cdb->rt == 0x02 && GUINT16_FROM_BE(feature->code) == GUINT16_FROM_BE(cdb->sfn))) {

                CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: copying feature 0x%X\n", __debug__, GUINT16_FROM_BE(feature->code));

                /* Copy feature */
                memcpy(ret_data, feature, feature->length + 4);
                self->priv->buffer_size += feature->length + 4;
                ret_data += feature->length + 4;

                /* Break the loop if RT is 0x02 */
                if (cdb->rt == 0x02) {
                    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: got the feature we wanted (0x%X), breaking the loop\n", __debug__, GUINT16_FROM_BE(cdb->sfn));
                    break;
                }
            }
        }
    }

    /* Header */
    ret_header->length = GUINT32_TO_BE(self->priv->buffer_size - 4);
    ret_header->cur_profile = GUINT16_TO_BE(self->priv->current_profile);

    /* Write data */
    cdemu_device_write_buffer(self, GUINT16_FROM_BE(cdb->length));

    return TRUE;
}

/* GET EVENT/STATUS NOTIFICATION*/
static gboolean command_get_event_status_notification (CdemuDevice *self, const guint8 *raw_cdb)
{
    struct GET_EVENT_STATUS_NOTIFICATION_CDB *cdb = (struct GET_EVENT_STATUS_NOTIFICATION_CDB*)raw_cdb;
    struct GET_EVENT_STATUS_NOTIFICATION_Header *ret_header = (struct GET_EVENT_STATUS_NOTIFICATION_Header *)self->priv->buffer;
    self->priv->buffer_size = sizeof(struct GET_EVENT_STATUS_NOTIFICATION_Header);

    if (!cdb->immed) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: asynchronous type not supported yet!\n", __debug__);
        cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }

    /* Let's say we're empty... this will change later in code accordingly */
    ret_header->nea = 1;

    /* Signal which event classes we do support */
    ret_header->media = 1;

    /* Process event classes */
    if (cdb->media) {
        struct GET_EVENT_STATUS_NOTIFICATION_MediaEventDescriptor *ret_desc = (struct GET_EVENT_STATUS_NOTIFICATION_MediaEventDescriptor *)(self->priv->buffer+self->priv->buffer_size);
        self->priv->buffer_size += sizeof(struct GET_EVENT_STATUS_NOTIFICATION_MediaEventDescriptor);

        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: media event class\n", __debug__);

        ret_header->nea = 0;
        ret_header->not_class = 4; /* Media notification class */

        /* Report current media event and then reset it */
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: reporting media event 0x%X\n", __debug__, self->priv->media_event);
        ret_desc->event = self->priv->media_event;
        self->priv->media_event = MEDIA_EVENT_NOCHANGE;

        /* Media status */
        ret_desc->present = self->priv->loaded;
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: medium present: %d\n", __debug__, ret_desc->present);
    }

    /* Header */
    ret_header->length = GUINT16_TO_BE(self->priv->buffer_size - 2);

    /* Write data */
    cdemu_device_write_buffer(self, GUINT16_FROM_BE(cdb->length));

    return TRUE;
}

/* GET PERFORMANCE */
static void __copy_performance_descriptors (struct GET_PERFORMANCE_00_Header *header,
                                            struct GET_PERFORMANCE_00_Descriptor *descriptors,
                                            gint num_descriptors,
                                            gint max_descriptors)
{
    /* Copy the descriptors behind header */
    gint total_descriptors = MIN(num_descriptors, max_descriptors);
    if (total_descriptors) {
        memcpy(header + 1, descriptors, total_descriptors * sizeof(struct GET_PERFORMANCE_00_Descriptor));
    }

    /* Modify the header's data_length based on number of descriptors*/
    header->data_length = GUINT32_TO_BE(4 + total_descriptors*sizeof(struct GET_PERFORMANCE_00_Descriptor));
}

static gboolean __get_performance_00 (CdemuDevice *self, const struct GET_PERFORMANCE_CDB *cdb)
{
    struct GET_PERFORMANCE_00_Header *ret_header = (struct GET_PERFORMANCE_00_Header *)self->priv->buffer;
    self->priv->buffer_size = sizeof(struct GET_PERFORMANCE_00_Header);

    /* Initialize header */
    ret_header->data_length = GUINT32_TO_BE(4); /* No descriptors */
    ret_header->except = cdb->except;
    ret_header->write = cdb->write;

    /* Validate the tolerance field - must be 10b */
    if (cdb->tolerance != 2) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: tolerance field is not 10b!\n", __debug__);
        cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }


    if (cdb->except) {
        /* Return no descriptors for exception data */
        return TRUE;
    }

    if (cdb->write) {
        /* *** Writing performance descriptors *** */

        /* If no disc is loaded, return default */
        if (!self->priv->loaded) {
            struct GET_PERFORMANCE_00_Descriptor descriptors[] = {
                {
                    .start_lba = GUINT32_TO_BE(0),
                    .start_performance =  GUINT32_TO_BE(0x00005690),
                    .end_lba = GUINT32_TO_BE(0),
                    .end_performance = GUINT32_TO_BE(0x00005690)
                }
            };
            __copy_performance_descriptors(ret_header, descriptors, G_N_ELEMENTS(descriptors), cdb->descriptors);
            return TRUE;
        }

        /* If disc is not recordable, return no descriptors */
        if (!self->priv->recordable_disc) {
            return TRUE;
        }

        gint medium_type = mirage_disc_get_medium_type(self->priv->disc);
        switch (medium_type) {
            case MIRAGE_MEDIUM_CD: {
                /* CD-R */
                struct GET_PERFORMANCE_00_Descriptor descriptors[] = {
                    {
                        .start_lba = GUINT32_TO_BE(0),
                        .start_performance =  GUINT32_TO_BE(0x00000B4E),
                        .end_lba = GUINT32_TO_BE(0x0004B0BA),
                        .end_performance = GUINT32_TO_BE(0x00001B90)
                    }, {
                        .start_lba = GUINT32_TO_BE(0x0004B0BA),
                        .start_performance =  GUINT32_TO_BE(0x00001B90),
                        .end_lba = GUINT32_TO_BE(self->priv->medium_capacity),
                        .end_performance = GUINT32_TO_BE(0x00001B90)
                    }
                };
                __copy_performance_descriptors(ret_header, descriptors, G_N_ELEMENTS(descriptors), cdb->descriptors);
                return TRUE;
            }
            case MIRAGE_MEDIUM_DVD: {
                /* DVD+R */
                struct GET_PERFORMANCE_00_Descriptor descriptors[] = {
                    {
                        .start_lba = GUINT32_TO_BE(0),
                        .start_performance =  GUINT32_TO_BE(0x000015A4),
                        .end_lba = GUINT32_TO_BE(self->priv->medium_capacity),
                        .end_performance = GUINT32_TO_BE(0x000015A4)
                    }
                };
                __copy_performance_descriptors(ret_header, descriptors, G_N_ELEMENTS(descriptors), cdb->descriptors);
                return TRUE;
            }
            case MIRAGE_MEDIUM_BD: {
                /* BD-R */
                struct GET_PERFORMANCE_00_Descriptor descriptors[] = {
                    {
                        .start_lba = GUINT32_TO_BE(0),
                        .start_performance =  GUINT32_TO_BE(0x0000231E),
                        .end_lba = GUINT32_TO_BE(self->priv->medium_capacity),
                        .end_performance = GUINT32_TO_BE(0x0000231E)
                    }
                };
                __copy_performance_descriptors(ret_header, descriptors, G_N_ELEMENTS(descriptors), cdb->descriptors);
                return TRUE;
            }
            default: {
                CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: writing performance descriptors are not implemented for medium type %d!\n", __debug__, medium_type);
                break;
            }
        }
    } else {
        /* *** Reading performance descriptors *** */

        /* If no disc is loaded, return default */
        if (!self->priv->loaded) {
            struct GET_PERFORMANCE_00_Descriptor descriptors[] = {
                {
                    .start_lba = GUINT32_TO_BE(0),
                    .start_performance =  GUINT32_TO_BE(0x00002383),
                    .end_lba = GUINT32_TO_BE(0),
                    .end_performance = GUINT32_TO_BE(0x00005690)
                }
            };
            __copy_performance_descriptors(ret_header, descriptors, G_N_ELEMENTS(descriptors), cdb->descriptors);
            return TRUE;
        }

        gint medium_type = mirage_disc_get_medium_type(self->priv->disc);
        gint disc_length = mirage_disc_layout_get_length(self->priv->disc);
        if (self->priv->recordable_disc) {
            disc_length = self->priv->medium_capacity;
        }

        switch (medium_type) {
            case MIRAGE_MEDIUM_CD: {
                /* CD-ROM */
                struct GET_PERFORMANCE_00_Descriptor descriptors[] = {
                    {
                        .start_lba = GUINT32_TO_BE(0),
                        .start_performance =  GUINT32_TO_BE(0x00000B4E),
                        .end_lba = GUINT32_TO_BE(disc_length),
                        .end_performance = GUINT32_TO_BE(0x00001B90)
                    }
                };
                __copy_performance_descriptors(ret_header, descriptors, G_N_ELEMENTS(descriptors), cdb->descriptors);
                return TRUE;
            }
            case MIRAGE_MEDIUM_DVD: {
                /* DVD-ROM */
                struct GET_PERFORMANCE_00_Descriptor descriptors[] = {
                    {
                        .start_lba = GUINT32_TO_BE(0),
                        .start_performance =  GUINT32_TO_BE(0x00001AA2),
                        .end_lba = GUINT32_TO_BE(disc_length),
                        .end_performance = GUINT32_TO_BE(0x000040EC)
                    }
                };
                __copy_performance_descriptors(ret_header, descriptors, G_N_ELEMENTS(descriptors), cdb->descriptors);
                return TRUE;
            }
            case MIRAGE_MEDIUM_BD: {
                /* BD-ROM */
                struct GET_PERFORMANCE_00_Descriptor descriptors[] = {
                    {
                        .start_lba = GUINT32_TO_BE(0),
                        .start_performance =  GUINT32_TO_BE(0x000039A0),
                        .end_lba = GUINT32_TO_BE(disc_length),
                        .end_performance = GUINT32_TO_BE(0x00008C78)
                    }
                };
                __copy_performance_descriptors(ret_header, descriptors, G_N_ELEMENTS(descriptors), cdb->descriptors);
                return TRUE;
            }
            default: {
                CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: reading performance descriptors are not implemented for medium type %d!\n", __debug__, medium_type);
                break;
            }
        }
    }

    return TRUE;
}


static gboolean command_get_performance (CdemuDevice *self, const guint8 *raw_cdb)
{
    struct GET_PERFORMANCE_CDB *cdb = (struct GET_PERFORMANCE_CDB *)raw_cdb;
    guint16 max_descriptors = GUINT16_FROM_BE(cdb->descriptors);

    switch (cdb->type) {
        case 0x00: {
            /* Performance */
            if (!__get_performance_00(self, cdb)) {
                return FALSE;
            }

            /* Modify buffer length based on header size */
            struct GET_PERFORMANCE_00_Header *ret_header = (struct GET_PERFORMANCE_00_Header *)self->priv->buffer;
            self->priv->buffer_size = 4 + GUINT32_FROM_BE(ret_header->data_length);

            break;
        }
        case 0x03: {
            /* Write Speed */
            struct GET_PERFORMANCE_03_Header *ret_header = (struct GET_PERFORMANCE_03_Header *)self->priv->buffer;
            self->priv->buffer_size = sizeof(struct GET_PERFORMANCE_03_Header);

            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: returning max %d write speed descriptors\n", __debug__, max_descriptors);

            /* Go over descriptors to count and copy them at the same time */
            gint num_descriptors = 0;
            for (GList *list_iter = g_list_first(self->priv->write_descriptors); list_iter; list_iter = g_list_next(list_iter)) {
                /* Make sure we don't return more descriptors than requested */
                if (num_descriptors < max_descriptors) {
                    /* Copy descriptor */
                    guint8 *desc_ptr = self->priv->buffer + self->priv->buffer_size;
                    memcpy(desc_ptr, list_iter->data, sizeof(struct GET_PERFORMANCE_03_Descriptor));
                    self->priv->buffer_size += sizeof(struct GET_PERFORMANCE_03_Descriptor);
                }

                num_descriptors++;
            }

            /* INF8090: "[Write Speed Data Length] is not modified when the
               maximum number of descriptors is insufficient to return all
               the write speed data available" => hence we return size of
               all descriptors */
            ret_header->data_length = GUINT32_TO_BE(4 + num_descriptors*sizeof(struct GET_PERFORMANCE_03_Descriptor));
            break;
        }
        default: {
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: unimplemented data type: %d\n", __debug__, cdb->type);
            cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
            return FALSE;
        }
    }

    /* Write data */
    cdemu_device_write_buffer(self, self->priv->buffer_size);
    return TRUE;
}

/* INQUIRY*/
static gboolean command_inquiry_vpd (CdemuDevice *self, const struct INQUIRY_CDB *cdb)
{
    switch (cdb->page_code) {
        case 0x00: {
            /* Supported VPD pages */
            struct INQUIRY_VPD_Header *ret_header = (struct INQUIRY_VPD_Header *)self->priv->buffer;
            self->priv->buffer_size = sizeof(struct INQUIRY_VPD_Header);
            ret_header->per_dev = 0x05; /* CD-ROM device */
            ret_header->page_code = 0; /* Page 0x00: Supported VPD pages */
            ret_header->page_length = 0;

            /* List the pages */
            gint num_pages = 0;
            guint8 *ret_list = (guint8 *)(ret_header + 1);

            ret_list[num_pages++] = 0x00; /* Supported VPD pages (mandatory) */
            ret_list[num_pages++] = 0x80; /* Unit serial number (optional) */
            ret_list[num_pages++] = 0x83; /* Device identification (mandatory) */

            /* Modify reported length */
            self->priv->buffer_size += num_pages;
            ret_header->page_length = num_pages;

            break;
        }
        case 0x80: {
            /* Serial number */
            struct INQUIRY_VPD_Header *ret_header = (struct INQUIRY_VPD_Header *)self->priv->buffer;
            self->priv->buffer_size = sizeof(struct INQUIRY_VPD_Header);
            ret_header->per_dev = 0x05; /* CD-ROM device */
            ret_header->page_code = 0x80; /* Page 0x80: Serial number */
            ret_header->page_length = 0;

            /* Serial number */
            /* NOTE: g_strlcpy() should add a trailing NULL to destination.
               This is not a problem, because a) it is not counted in the
               return value, and b) because we are writing directly to
               cache buffer, and hence this additional NULL is effectively
               discarded from the response. */
            gchar *ret_serial = (gchar *)(ret_header + 1);
            gint serial_len = g_strlcpy(ret_serial, self->priv->device_serial, self->priv->buffer_capacity - self->priv->buffer_size);

            /* Modify reported length */
            self->priv->buffer_size += serial_len;
            ret_header->page_length += serial_len;
            break;
        }
        case 0x83: {
            /* Device identification */
            struct INQUIRY_VPD_Header *ret_header = (struct INQUIRY_VPD_Header *)self->priv->buffer;
            self->priv->buffer_size = sizeof(struct INQUIRY_VPD_Header);
            ret_header->per_dev = 0x05; /* CD-ROM device */
            ret_header->page_code = 0x83; /* Page 0x80: Serial number */
            ret_header->page_length = 0;

            /* Identifier header */
            struct INQUIRY_VPD_IdentificationDescriptorHeader *id_header = (struct INQUIRY_VPD_IdentificationDescriptorHeader *)(self->priv->buffer + self->priv->buffer_size);
            self->priv->buffer_size += sizeof(struct INQUIRY_VPD_IdentificationDescriptorHeader);
            ret_header->page_length += sizeof(struct INQUIRY_VPD_IdentificationDescriptorHeader);

            id_header->protocol_id = 8; /* ATA/ATAPI */
            id_header->code_set = 2; /* ASCII */

            id_header->association = 0; /* Logical unit*/
            id_header->identifier_type = 1; /* T-10 vendor ID based */

            id_header->identifier_length = 0;

            /* T10 vendor identification (8-byte) */
            gchar *vendor_id = (gchar *)(self->priv->buffer + self->priv->buffer_size);

            /* Fill with spaces and copy the vendor string; the lenght
               of the latter is guaranteed not to exceed 8 characters
               (see comment in command_inquiry()) */
            memset(vendor_id, 32, 8);
            memcpy(vendor_id, self->priv->id_vendor_id, strlen(self->priv->id_vendor_id));

            self->priv->buffer_size += 8;
            ret_header->page_length += 8;
            id_header->identifier_length += 8;

            /* Vendor-specific identifier */
            /* SPC: "A recommended method of constructing a unique
               IDENTIFIER field is to concatenate the PRODUCT IDENTIFICATION
               field from the standard INQUIRY data and the PRODUCT SERIAL
               NUMBER field from the Unit Serial Number VPD page */
            gchar *id_string = (gchar *)(self->priv->buffer + self->priv->buffer_size);
            gint id_len = g_snprintf(id_string, self->priv->buffer_capacity - self->priv->buffer_size, "%s %s", self->priv->id_product_id, self->priv->device_serial);

            self->priv->buffer_size += id_len;
            ret_header->page_length += id_len;
            id_header->identifier_length += id_len;

            break;
        }
        default: {
            /* Unsupported; as stated in SPC, return CHECK CONDITION,
               ILLEGAL REQUEST and INVALID FIELD IN CDB */
            CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: VPD page %02Xh not implemented!\n", __debug__, cdb->page_code);
            cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
            return FALSE;
        }
    }

    /* Write data */
    cdemu_device_write_buffer(self, GUINT16_FROM_BE(cdb->length));

    return TRUE;
}

static gboolean command_inquiry (CdemuDevice *self, const guint8 *raw_cdb)
{
    struct INQUIRY_CDB *cdb = (struct INQUIRY_CDB *)raw_cdb;

    struct INQUIRY_Data *ret_data = (struct INQUIRY_Data *)self->priv->buffer;
    self->priv->buffer_size = sizeof(struct INQUIRY_Data);

    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: INQUIRY: EVPD=%d, PAGE CODE=02%Xh)\n", __debug__, cdb->evpd, cdb->page_code);

    /* Non-zero page code and zero EVPD is illegal as per SPC */
    if (!cdb->evpd && cdb->page_code) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: invalid field in CDB (EVPD=0 and non-zero PAGE CODE is illegal)\n", __debug__);
        cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }

    /* Vital product data */
    if (cdb->evpd) {
        return command_inquiry_vpd(self, cdb);
    }

    /* Values here are more or less what my DVD-ROM drive gives me
       (and in accord with INF8090) */
    ret_data->per_dev = 0x05; /* CD-ROM device */
    ret_data->rmb = 1; /* Removable medium */
    ret_data->version = 0x0; /* Should be 0 according to INF8090 */
    ret_data->atapi_version = 3; /* Should be 3 according to INF8090 */
    ret_data->response_fmt = 0x02; /* Should be 2 according to INF8090 */
    ret_data->length = sizeof(struct INQUIRY_Data) - 5;

    /* NOTE: the length of source strings is guaranteed to not exceed
       the corresponding buffer size due to limits imposed during
       storing procedure in cdemu_device_set_device_id()  */
    /* First, fill all bytes with whitespace, then copy the string
       without terminating NULL */
    memset(ret_data->vendor_id, 32, 8);
    memcpy(ret_data->vendor_id, self->priv->id_vendor_id, strlen(self->priv->id_vendor_id));

    memset(ret_data->product_id, 32, 16);
    memcpy(ret_data->product_id, self->priv->id_product_id, strlen(self->priv->id_product_id));

    memset(ret_data->product_rev, 32, 4);
    memcpy(ret_data->product_rev, self->priv->id_revision, strlen(self->priv->id_revision));

    memset(ret_data->vendor_spec1, 32, 20);
    memcpy(ret_data->vendor_spec1, self->priv->id_vendor_specific, strlen(self->priv->id_vendor_specific));

    ret_data->ver_desc1 = GUINT16_TO_BE(0x02A0); /* We'll try to pass as MMC-3 device */

    /* Write data */
    cdemu_device_write_buffer(self, GUINT16_FROM_BE(cdb->length));

    return TRUE;
}

/* MODE SELECT*/
static gboolean command_mode_select (CdemuDevice *self, const guint8 *raw_cdb)
{
    gint transfer_len = 0;
    /*gint sp;
    gint pf;*/

    /* MODE SELECT (6) vs MODE SELECT (10) */
    if (raw_cdb[0] == MODE_SELECT_6) {
        struct MODE_SELECT_6_CDB *cdb = (struct MODE_SELECT_6_CDB *)raw_cdb;
        /*sp = cdb->sp;
        pf = cdb->pf;*/
        transfer_len = cdb->length;
    } else {
        struct MODE_SELECT_10_CDB *cdb = (struct MODE_SELECT_10_CDB *)raw_cdb;
        /*sp = cdb->sp;
        pf = cdb->pf;*/
        transfer_len = GUINT16_FROM_BE(cdb->length);
    }

    /* Read the parameter list */
    cdemu_device_read_buffer(self, transfer_len);

    /* Dump the parameter list */
    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: received parameters:\n", __debug__);
    CDEMU_DEBUG_PRINT_BUFFER(self, DAEMON_DEBUG_MMC, __debug__, 16, self->priv->buffer, transfer_len);

    /* Try to decipher mode select data... MODE SENSE (6) vs MODE SENSE (10) */
    gint blkdesc_len = 0;
    gint offset = 0;
    if (raw_cdb[0] == MODE_SELECT_6) {
        struct MODE_SENSE_6_Header *header = (struct MODE_SENSE_6_Header *)self->priv->buffer;
        blkdesc_len = header->blkdesc_len;
        offset = sizeof(struct MODE_SENSE_6_Header) + blkdesc_len;
    } else if (raw_cdb[0] == MODE_SELECT_10) {
        struct MODE_SENSE_10_Header *header = (struct MODE_SENSE_10_Header *)self->priv->buffer;
        blkdesc_len = GUINT16_FROM_BE(header->blkdesc_len);
        offset = sizeof(struct MODE_SENSE_10_Header) + blkdesc_len;
    }

    /* Someday when I'm in good mood I might implement this */
    if (blkdesc_len) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: block descriptor provided... but ATAPI devices shouldn't support that\n", __debug__);
        cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_FIELD_IN_PARAMETER_LIST);
        return FALSE;
    }

    /* Take a peek at the byte following block descriptor data */
    gint page_size = transfer_len - offset;

    if (page_size) {
        gint page_code = ((struct ModePageGeneral *)(self->priv->buffer + offset))->code;
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: modifying mode page 0x%X (%d bytes)\n", __debug__, page_code, page_size);

        if (!cdemu_device_modify_mode_page(self, self->priv->buffer + offset, page_size)) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: failed to modify code page 0x%X!\n", __debug__, page_code);
            cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_FIELD_IN_PARAMETER_LIST);
            return FALSE;
        }
    }

    return TRUE;
}

/* MODE SENSE*/
static gboolean command_mode_sense (CdemuDevice *self, const guint8 *raw_cdb)
{
    gint page_code = 0;
    gint transfer_len = 0;
    gint pc = 0;

    /* MODE SENSE (6) vs MODE SENSE (10) */
    if (raw_cdb[0] == MODE_SENSE_6) {
        struct MODE_SENSE_6_CDB *cdb = (struct MODE_SENSE_6_CDB *)raw_cdb;

        pc = cdb->pc;
        page_code = cdb->page_code;
        transfer_len = cdb->length;

        self->priv->buffer_size = sizeof(struct MODE_SENSE_6_Header);
    } else {
        struct MODE_SENSE_10_CDB *cdb = (struct MODE_SENSE_10_CDB *)raw_cdb;

        pc = cdb->pc;
        page_code = cdb->page_code;
        transfer_len = GUINT16_FROM_BE(cdb->length);

        self->priv->buffer_size = sizeof(struct MODE_SENSE_10_Header);
    }

    guint8 *ret_data = self->priv->buffer+self->priv->buffer_size;

    /* We don't support saving mode pages */
    if (pc == 0x03) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: requested saved values; we don't support saving!\n", __debug__);
        cdemu_device_write_sense(self, ILLEGAL_REQUEST, SAVING_PARAMETERS_NOT_SUPPORTED);
        return FALSE;
    }

    /* Go over *all* pages, and if we want all pages, copy 'em all, otherwise
       copy just the one we've got request for and break the loop */
    gboolean page_found = FALSE;
    for (GList *entry = self->priv->mode_pages_list; entry; entry = entry->next) {
        struct ModePageEntry *page_entry = entry->data;
        struct ModePageGeneral *mode_page = page_entry->page_current;

        /* Check if we want this page copied */
        if (page_code == 0x3F || (page_code == mode_page->code)) {
            switch (pc) {
                case 0x00: {
                    /* Current values */
                    mode_page = page_entry->page_current;
                    break;
                }
                case 0x01: {
                    /* Changeable values */
                    mode_page = page_entry->page_mask;
                    break;
                }
                case 0x02: {
                    /* Default value */
                    mode_page = page_entry->page_default;
                    break;
                }
                default: {
                    CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: PC value is 0x%X and it shouldn't be!\n", __debug__, pc);
                    break;
                }
            }

            memcpy(ret_data, mode_page, mode_page->length + 2);
            self->priv->buffer_size += mode_page->length + 2;
            ret_data += mode_page->length + 2;

            if (page_code != 0x3F) {
                CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: got the page we wanted (0x%X), breaking the loop\n", __debug__, page_code);
                page_found = TRUE;
                break;
            }
        }
    }

    /* If we aren't returning all pages, check if page was found */
    if (page_code != 0x3F && !page_found) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: page 0x%X not found!\n", __debug__, page_code);
        cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }

    /* Header; MODE SENSE (6) vs MODE SENSE (10) */
    if (raw_cdb[0] == MODE_SENSE_6) {
        struct MODE_SENSE_6_Header *ret_header = (struct MODE_SENSE_6_Header *)self->priv->buffer;
        ret_header->length = self->priv->buffer_size - 2;
    } else if (raw_cdb[0] == MODE_SENSE_10) {
        struct MODE_SENSE_10_Header *ret_header = (struct MODE_SENSE_10_Header *)self->priv->buffer;
        ret_header->length = GUINT16_TO_BE(self->priv->buffer_size - 2);
    }

    /* Write data */
    cdemu_device_write_buffer(self, transfer_len);

    return TRUE;
}

/* PAUSE/RESUME*/
static gboolean command_pause_resume (CdemuDevice *self, const guint8 *raw_cdb)
{
    struct PAUSE_RESUME_CDB *cdb = (struct PAUSE_RESUME_CDB *)raw_cdb;
    gint audio_status = cdemu_audio_get_status(CDEMU_AUDIO(self->priv->audio_play));

    /* Resume */
    if (cdb->resume == 1) {
        /* MMC-3 says that if we request resume and operation can't be resumed,
           we return error (if we're already playing, it doesn't count as an
           error) */
        if ((audio_status != AUDIO_STATUS_PAUSED)
            && (audio_status != AUDIO_STATUS_PLAYING)) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: resume requested while in invalid state!\n", __debug__);
            cdemu_device_write_sense(self, ILLEGAL_REQUEST, COMMAND_SEQUENCE_ERROR);
            return FALSE;
        }

        /* Resume; we set status to playing, and fire the callback */
        if (audio_status != AUDIO_STATUS_PLAYING) {
            cdemu_audio_resume(CDEMU_AUDIO(self->priv->audio_play));
        }
    }


    if (cdb->resume == 0) {
        /* MMC-3 also says that we return error if pause is requested and the
           operation can't be paused (being already paused doesn't count) */
        if ((audio_status != AUDIO_STATUS_PAUSED)
            && (audio_status != AUDIO_STATUS_PLAYING)) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: pause requested while in invalid state!\n", __debug__);
            cdemu_device_write_sense(self, ILLEGAL_REQUEST, COMMAND_SEQUENCE_ERROR);
            return FALSE;
        }

       /* Pause; stop the playback and force status to AUDIO_STATUS_PAUSED */
        if (audio_status != AUDIO_STATUS_PAUSED) {
            cdemu_audio_pause(CDEMU_AUDIO(self->priv->audio_play));
        }
    }

    return TRUE;
}

/* PLAY AUDIO*/
static gboolean command_play_audio (CdemuDevice *self, const guint8 *raw_cdb)
{
    guint32 start_sector = 0;
    guint32 end_sector = 0;

    /* PLAY AUDIO (10) vs PLAY AUDIO (12) vs PLAY AUDIO MSF */
    if (raw_cdb[0] == PLAY_AUDIO_10) {
        struct PLAY_AUDIO_10_CDB *cdb = (struct PLAY_AUDIO_10_CDB *)raw_cdb;

        start_sector = GUINT32_FROM_BE(cdb->lba);
        end_sector = GUINT32_FROM_BE(cdb->lba) + GUINT16_FROM_BE(cdb->play_len);
    } else if (raw_cdb[0] == PLAY_AUDIO_12) {
        struct PLAY_AUDIO_12_CDB *cdb = (struct PLAY_AUDIO_12_CDB *)raw_cdb;

        start_sector = GUINT32_FROM_BE(cdb->lba);
        end_sector = GUINT32_FROM_BE(cdb->lba) + GUINT32_FROM_BE(cdb->play_len);
    } else {
        struct PLAY_AUDIO_MSF_CDB *cdb = (struct PLAY_AUDIO_MSF_CDB *)raw_cdb;

        start_sector = mirage_helper_msf2lba(cdb->start_m, cdb->start_s, cdb->start_f, TRUE);
        end_sector = mirage_helper_msf2lba(cdb->end_m, cdb->end_s, cdb->end_f, TRUE);
    }

     /* Check if we have medium loaded */
    if (!self->priv->loaded) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: medium not present\n", __debug__);
        cdemu_device_write_sense(self, NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }

    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: playing from sector 0x%X to sector 0x%X\n", __debug__, start_sector, end_sector);

    /* Play */
    if (!cdemu_audio_start(CDEMU_AUDIO(self->priv->audio_play), start_sector, end_sector, self->priv->disc)) {
        /* FIXME: write sense */
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: failed to start audio play!\n", __debug__);
        return FALSE;
    }

    return TRUE;
}

/* PREVENT/ALLOW MEDIUM REMOVAL*/
static gboolean command_prevent_allow_medium_removal (CdemuDevice *self, const guint8 *raw_cdb)
{
    struct PREVENT_ALLOW_MEDIUM_REMOVAL_CDB *cdb = (struct PREVENT_ALLOW_MEDIUM_REMOVAL_CDB*)raw_cdb;
    struct ModePage_0x2A *p_0x2A = cdemu_device_get_mode_page(self, 0x2A, MODE_PAGE_CURRENT);

    /* That's the locking, right? */
    if (cdb->prevent) {
        /* Lock */
        self->priv->locked = 1;
        /* Indicate in the mode page that we're locked */
        p_0x2A->lock_state = 1;
    } else {
        /* Unlock */
        self->priv->locked = 0;
        /* Indicate in the mode page that we're unlocked */
        p_0x2A->lock_state = 0;
    }

    return TRUE;
}

/* READ (10) and READ (12)*/
static gboolean command_read (CdemuDevice *self, const guint8 *raw_cdb)
{
    gint start_address; /* MUST be signed because it may be negative! */
    gint num_sectors;

    struct ModePage_0x01 *p_0x01 = cdemu_device_get_mode_page(self, 0x01, MODE_PAGE_CURRENT);

    /* READ 10 vs READ 12 */
    if (raw_cdb[0] == READ_10) {
        struct READ_10_CDB *cdb = (struct READ_10_CDB *)raw_cdb;
        start_address = GUINT32_FROM_BE(cdb->lba);
        num_sectors  = GUINT16_FROM_BE(cdb->length);
    } else {
        struct READ_12_CDB *cdb = (struct READ_12_CDB *)raw_cdb;
        start_address = GUINT32_FROM_BE(cdb->lba);
        num_sectors  = GUINT32_FROM_BE(cdb->length);
    }

    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: read request; start sector: 0x%X, number of sectors: %d\n", __debug__, start_address, num_sectors);

    /* Check if we have medium loaded (because we use track later... >.<) */
    if (!self->priv->loaded) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: medium not present\n", __debug__);
        cdemu_device_write_sense(self, NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }
    MirageDisc *disc = self->priv->disc;

    /* Set up delay emulation */
    cdemu_device_delay_begin(self, start_address, num_sectors);

    /* Process each sector */
    for (gint address = start_address; address < start_address + num_sectors; address++) {
        GError *error = NULL;
        MirageSector *sector = mirage_disc_get_sector(disc, address, &error);
        if (!sector) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: failed to read sector: %s\n", __debug__, error->message);
            g_error_free(error);
            cdemu_device_write_sense_full(self, ILLEGAL_REQUEST, ILLEGAL_MODE_FOR_THIS_TRACK, 0, address);
            return FALSE;
        }

        cdemu_device_flush_buffer(self);

        /* Here we do the emulation of "bad sectors"... if we're dealing with
           a bad sector, then its EDC/ECC won't correspond to actual data. So
           we verify sector's EDC and in case DCR (Disable Corrections) bit in
           Mode Page 1 is not enabled, we report the read error. However, my
           tests indicate this should be done only for Mode 1 or Mode 2 Form 1
           sectors */
        if (self->priv->bad_sector_emulation && !p_0x01->dcr) {
            gint sector_type = mirage_sector_get_sector_type(sector);

            if ((sector_type == MIRAGE_SECTOR_MODE1 || sector_type == MIRAGE_SECTOR_MODE2_FORM1)
                && !mirage_sector_verify_lec(sector)) {
                CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: bad sector detected, triggering read error!\n", __debug__);
                g_object_unref(sector);
                cdemu_device_write_sense_full(self, MEDIUM_ERROR, UNRECOVERED_READ_ERROR, 0, address);
                return FALSE;
            }
        }

        /* READ 10/12 should support only sectors with 2048-byte user data */
        const guint8 *tmp_buf;
        gint tmp_len;

        mirage_sector_get_data(sector, &tmp_buf, &tmp_len, NULL);
        if (tmp_len != 2048) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: sector 0x%X does not have 2048-byte user data (%i)\n", __debug__, address, tmp_len);
            g_object_unref(sector);
            cdemu_device_write_sense_full(self, ILLEGAL_REQUEST, ILLEGAL_MODE_FOR_THIS_TRACK, 1, address);
            return FALSE;
        }

        memcpy(self->priv->buffer+self->priv->buffer_size, tmp_buf, tmp_len);
        self->priv->buffer_size += tmp_len;

        /* Needed for some other commands */
        self->priv->current_address = address;
        /* Free sector */
        g_object_unref(sector);
        /* Write sector */
        cdemu_device_write_buffer(self, self->priv->buffer_size);
    }

    /* Perform delay emulation */
    cdemu_device_delay_finalize(self);

    return TRUE;
}

/* READ BUFFER CAPACITY */
static gboolean command_read_buffer_capacity (CdemuDevice *self, const guint8 *raw_cdb)
{
    struct READ_BUFFER_CAPACITY_CDB *cdb = (struct READ_BUFFER_CAPACITY_CDB *)raw_cdb;
    struct READ_BUFFER_CAPACITY_Data *ret_data = (struct READ_BUFFER_CAPACITY_Data *)self->priv->buffer;
    self->priv->buffer_size = sizeof(struct READ_BUFFER_CAPACITY_Data);

    /* Medium must be present */
    if (!self->priv->loaded) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: medium not present\n", __debug__);
        cdemu_device_write_sense(self, NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }

    /* Buffer capacity data */
    gint buffer_capacity = cdemu_device_get_kernel_io_buffer_size(self);

    ret_data->data_length = GUINT16_TO_BE(self->priv->buffer_size - 2);
    ret_data->block = cdb->block;
    if (ret_data->block) {
        ret_data->length_of_buffer = 0x00000000; /* Reserved */
        ret_data->blank_length_of_buffer = GUINT32_TO_BE(buffer_capacity/2048); /* In blocks */
    } else {
        ret_data->length_of_buffer = GUINT32_TO_BE(buffer_capacity);
        ret_data->blank_length_of_buffer = GUINT32_TO_BE(buffer_capacity);
    }

    /* Write data */
    cdemu_device_write_buffer(self, self->priv->buffer_size);

    return TRUE;
}

/* READ CAPACITY*/
static gboolean command_read_capacity (CdemuDevice *self, const guint8 *raw_cdb G_GNUC_UNUSED)
{
    /*struct READ_CAPACITY_CDB *cdb = (struct READ_CAPACITY_CDB *)raw_cdb;*/
    struct READ_CAPACITY_Data *ret_data = (struct READ_CAPACITY_Data *)self->priv->buffer;
    self->priv->buffer_size = sizeof(struct READ_CAPACITY_Data);

    gint last_sector = 0;

    if (!self->priv->loaded) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: medium not present\n", __debug__);
        cdemu_device_write_sense(self, NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }

    /* Capacity: starting sector of last leadout - 1 */
    MirageSession *lsession;
    MirageTrack *leadout;

    lsession = mirage_disc_get_session_by_index(self->priv->disc, -1, NULL);
    if (lsession) {
        leadout = mirage_session_get_track_by_number(lsession, MIRAGE_TRACK_LEADOUT, NULL);
        last_sector = mirage_track_layout_get_start_sector(leadout);

        g_object_unref(leadout);
        g_object_unref(lsession);

        last_sector -= 1;
    }

    ret_data->lba = GUINT32_TO_BE(last_sector);
    ret_data->block_size = GUINT32_TO_BE(2048);

    /* Write data */
    cdemu_device_write_buffer(self, self->priv->buffer_size);

    return TRUE;
}

/* READ CD and READ CD MSF*/
static gboolean command_read_cd (CdemuDevice *self, const guint8 *raw_cdb)
{
    gint start_address; /* MUST be signed because it may be negative! */
    gint num_sectors;
    gint exp_sect_type;
    gint subchannel_mode;

    struct ModePage_0x01 *p_0x01 = cdemu_device_get_mode_page(self, 0x01, MODE_PAGE_CURRENT);

    /* READ CD vs READ CD MSF */
    if (raw_cdb[0] == READ_CD) {
        struct READ_CD_CDB *cdb = (struct READ_CD_CDB *)raw_cdb;

        start_address = GUINT32_FROM_BE(cdb->lba);
        num_sectors = GUINT24_FROM_BE(cdb->length);

        exp_sect_type = map_expected_sector_type(cdb->sect_type);
        subchannel_mode = cdb->subchan;
    } else {
        struct READ_CD_MSF_CDB *cdb = (struct READ_CD_MSF_CDB *)raw_cdb;
        gint32 end_address = 0;

        start_address = mirage_helper_msf2lba(cdb->start_m, cdb->start_s, cdb->start_f, TRUE);
        end_address = mirage_helper_msf2lba(cdb->end_m, cdb->end_s, cdb->end_f, TRUE);
        num_sectors = end_address - start_address;

        exp_sect_type = map_expected_sector_type(cdb->sect_type);
        subchannel_mode = cdb->subchan;
    }

    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: READ CD:\n-> Address: 0x%08X\n-> Length: %i\n-> Expected sector (in libMirage type): 0x%X\n-> MCSB: 0x%X\n-> SubChannel: 0x%X\n",
        __debug__, start_address, num_sectors, exp_sect_type, raw_cdb[9], subchannel_mode);


    /* Check if we have medium loaded */
    if (!self->priv->loaded) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: medium not present\n", __debug__);
        cdemu_device_write_sense(self, NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }

    /* Not supported for DVD-ROMs, right? */
    if (self->priv->current_profile == PROFILE_DVDROM) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: READ CD not supported on DVD Media!\n", __debug__);
        cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }

    /* Verify the requested subchannel mode ('readcd:18' uses READ CD with transfer
       length 0x00 to determine which subchannel modes are supported; without this
       clause, call with R-W subchannel passes, causing app choke on it later (when
       there's transfer length > 0x00 and thus subchannel is verified */
    if (subchannel_mode == 0x04) {
        /* invalid subchannel requested (don't support R-W yet) */
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: R-W subchannel reading not supported yet\n", __debug__);
        cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }


    MirageDisc* disc = self->priv->disc;
    MirageSector *first_sector;
    GError *error = NULL;
    gint prev_sector_type G_GNUC_UNUSED;

    /* Read first sector to determine its type */
    first_sector = mirage_disc_get_sector(disc, start_address, &error);
    if (!first_sector) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: failed to get start sector: %s\n", __debug__, error->message);
        g_error_free(error);
        cdemu_device_write_sense_full(self, ILLEGAL_REQUEST, ILLEGAL_MODE_FOR_THIS_TRACK, 0, start_address);
        return FALSE;
    }
    prev_sector_type = mirage_sector_get_sector_type(first_sector);
    g_object_unref(first_sector);

    /* Set up delay emulation */
    cdemu_device_delay_begin(self, start_address, num_sectors);

    /* Process each sector */
    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: start sector: 0x%X (%i); start + num: 0x%X (%i)\n", __debug__, start_address, start_address, start_address+num_sectors, start_address+num_sectors);
    for (gint address = start_address; address < start_address + num_sectors; address++) {
        MirageSector *sector;

        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: reading sector 0x%X (%i)\n", __debug__, address, address);

        sector = mirage_disc_get_sector(disc, address, &error);
        if (!sector) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: failed to get sector: %s!\n", __debug__, error->message);
            g_error_free(error);
            cdemu_device_write_sense_full(self, ILLEGAL_REQUEST, ILLEGAL_MODE_FOR_THIS_TRACK, 0, address);
            return FALSE;
        }

        cdemu_device_flush_buffer(self);

        /* Expected sector stuff check... basically, if we have CDB->ExpectedSectorType
           set, we compare its translated value with our sector type, period. However, if
           it's 0, then "The Logical Unit shall always terminate a command at the sector
           where a transition between CD-ROM and CD-DA data occurs." */
        gint sector_type = mirage_sector_get_sector_type(sector);

        /* Break if current sector type doesn't match expected one*/
        if (exp_sect_type && (sector_type != exp_sect_type)) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: expected sector type mismatch (expecting %i, got %i)!\n", __debug__, exp_sect_type, sector_type);
            g_object_unref(sector);
            cdemu_device_write_sense_full(self, ILLEGAL_REQUEST, ILLEGAL_MODE_FOR_THIS_TRACK, 1, address);
            return FALSE;
        }

        if (FALSE) {
            /* Break if mode (sector type) has changed */
            /* NOTE: if we're going to be doing this, we need to account for the
               fact that Mode 2 Form 1 and Mode 2 Form 2 can alternate... */
            if (prev_sector_type != sector_type) {
                CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: previous sector type (%i) different from current one (%i)!\n", __debug__, prev_sector_type, sector_type);
                g_object_unref(sector);
                cdemu_device_write_sense_full(self, ILLEGAL_REQUEST, ILLEGAL_MODE_FOR_THIS_TRACK, 0, address);
                return FALSE;
            }
        }

        /* Here we do the emulation of "bad sectors"... if we're dealing with
           a bad sector, then its EDC/ECC won't correspond to actual data. So
           we verify sector's EDC and in case DCR (Disable Corrections) bit in
           Mode Page 1 is not enabled, we report the read error. However, my
           tests indicate this should be done only for Mode 1 or Mode 2 Form 1
           sectors */
        if (self->priv->bad_sector_emulation && !p_0x01->dcr) {
            if ((sector_type == MIRAGE_SECTOR_MODE1 || sector_type == MIRAGE_SECTOR_MODE2_FORM1)
                && !mirage_sector_verify_lec(sector)) {
                CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: bad sector detected, triggering read error!\n", __debug__);
                g_object_unref(sector);
                cdemu_device_write_sense_full(self, MEDIUM_ERROR, UNRECOVERED_READ_ERROR, 0, address);
                return FALSE;
            }
        }

        /* We read data here. NOTE: we do not verify MCSB for illegal combinations */
        gint read_length = read_sector_data(sector, NULL, 0, raw_cdb[9], subchannel_mode, self->priv->buffer+self->priv->buffer_size, &error);
        if (read_length == -1) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: failed to read sector 0x%X: %s\n", __debug__, address, error->message);
            g_error_free(error);
            g_object_unref(sector);
            cdemu_device_write_sense_full(self, ILLEGAL_REQUEST, ILLEGAL_MODE_FOR_THIS_TRACK, 0, address);
            return FALSE;
        }

        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: read length: 0x%X, buffer size: 0x%X\n", __debug__, read_length, self->priv->buffer_size);
        self->priv->buffer_size += read_length;

        /* Previous sector type */
        prev_sector_type = sector_type;
        /* Needed for some other commands */
        self->priv->current_address = address;
        /* Free sector */
        g_object_unref(sector);
        /* Write sector */
        cdemu_device_write_buffer(self, self->priv->buffer_size);
    }

    /* Perform delay emulation */
    cdemu_device_delay_finalize(self);

    return TRUE;
}

/* READ DISC INFORMATION*/
static gboolean command_read_disc_information (CdemuDevice *self, const guint8 *raw_cdb)
{
    struct READ_DISC_INFORMATION_CDB *cdb = (struct READ_DISC_INFORMATION_CDB *)raw_cdb;

    /* Check if we have medium loaded */
    if (!self->priv->loaded) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: medium not present\n", __debug__);
        cdemu_device_write_sense(self, NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }

    switch (cdb->type) {
        case 0x000: {
            struct READ_DISC_INFORMATION_Data *ret_data = (struct READ_DISC_INFORMATION_Data *)self->priv->buffer;
            self->priv->buffer_size = sizeof(struct READ_DISC_INFORMATION_Data);

            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: standard disc information\n", __debug__);

            gint first_track_on_disc = 1; /* Always report 1 */
            gint first_track_in_last_session = 1;
            gint last_track_in_last_session = 1; /* Always report 1 or more */
            gint num_sessions;
            gint last_session_leadin = -1;
            gint disc_type = 0xFF;

            gint disc_status;
            gint last_session_status;

            /* Initialize some of the variables */
            num_sessions = mirage_disc_get_number_of_sessions(self->priv->disc);
            num_sessions = num_sessions + !self->priv->disc_closed; /* Unless disc is closed, there's always an additional incomplete/empty session */

            /* First session */
            MirageSession *session;

            session = mirage_disc_get_session_by_index(self->priv->disc, 0, NULL);
            if (session) {
                /* Disc type is determined from first session, as per INF8090 */
                disc_type = map_session_type(mirage_session_get_session_type(session));
                g_object_unref(session);
            } else if (self->priv->recordable_disc && self->priv->open_session) {
                /* If the only session on disc is incomplete, get disc type from it */
                disc_type = map_session_type(mirage_session_get_session_type(self->priv->open_session));
            }

            /* Last session */
            if (self->priv->recordable_disc && !self->priv->disc_closed) {
                /* Recordable, non-closed disc */
                if (!self->priv->open_session) {
                    last_session_status = 0x00; /* Empty */
                } else {
                    last_session_status = 0x01; /* Incomplete */

                    /* Return for further data extraction */
                    session = g_object_ref(self->priv->open_session);
                }
            } else {
                session = mirage_disc_get_session_by_index(self->priv->disc, -1, NULL);
                last_session_status = 0x03; /* Complete */
            }

            if (session) {
                MirageTrack *track;

                /* First track in last session */
                track = mirage_session_get_track_by_index(session, 0, NULL);
                if (track) {
                    first_track_in_last_session = mirage_track_layout_get_track_number(track);
                    g_object_unref(track);
                }

                /* Last track in last session */
                track = mirage_session_get_track_by_index(session, -1, NULL);
                if (track) {
                    last_track_in_last_session = mirage_track_layout_get_track_number(track);
                    g_object_unref(track);
                }

                /* Lead-in of last session */
                if (mirage_session_layout_get_session_number(session) != 1) {
                    track = mirage_session_get_track_by_number(session, MIRAGE_TRACK_LEADIN, NULL);
                    if (track) {
                        last_session_leadin = mirage_track_layout_get_start_sector(track);
                        g_object_unref(track);
                    }
                } else {
                    /* For first session, we have a fixed, emulated, lead-in */
                    last_session_leadin = -1; /* FIXME: set real address here, and modify MSF conversion to handle it! */
                }

                g_object_unref(session);
            }


            /* Disc status */
            if (self->priv->recordable_disc) {
                if (self->priv->disc_closed) {
                    disc_status = 0x02; /* Complete */
                } else {
                    if (!mirage_disc_get_number_of_sessions(self->priv->disc) && !self->priv->open_session) {
                        /* No sessions on disc, and no open session empty */
                        disc_status = 0x00; /* Empty */
                    } else {
                        /* At least one finished session, or an open session */
                        disc_status = 0x01; /* Incomplete disc */
                    }
                }
            } else {
                disc_status = 0x02; /* Complete */
            }


            /* Write gathered information */
            ret_data->length = GUINT16_TO_BE(self->priv->buffer_size - 2);

            ret_data->erasable = self->priv->rewritable_disc;
            ret_data->disc_status = disc_status;
            ret_data->lsession_state = last_session_status;

            ret_data->ftrack_disc = first_track_on_disc;

            ret_data->sessions0 = (num_sessions & 0xFF00) >> 8;
            ret_data->ftrack_lsession0 = (first_track_in_last_session & 0xFF00) >> 8;
            ret_data->ltrack_lsession0 = (last_track_in_last_session & 0xFF00) >> 8;

            ret_data->disc_type = disc_type;

            ret_data->sessions1 = num_sessions & 0xFF;
            ret_data->ftrack_lsession1 = first_track_in_last_session & 0xFF;
            ret_data->ltrack_lsession1 = last_track_in_last_session & 0xFF;

            if (!self->priv->recordable_disc || self->priv->disc_closed) {
                ret_data->lsession_leadin = 0xFFFFFFFF;
            } else {
                guint8 *msf_ptr = (guint8 *)&ret_data->lsession_leadin;
                if (last_session_leadin == -1) {
                    last_session_leadin = self->priv->medium_leadin;
                }
                mirage_helper_lba2msf(last_session_leadin, TRUE, &msf_ptr[1], &msf_ptr[2], &msf_ptr[3]);
            }

            if (!self->priv->recordable_disc || self->priv->disc_closed) {
                ret_data->last_leadout = 0xFFFFFFFF; /* Not applicable since we're not a writer */
            } else {
                guint8 *msf_ptr = (guint8 *)&ret_data->last_leadout;
                mirage_helper_lba2msf(self->priv->medium_capacity - 2, FALSE, &msf_ptr[1], &msf_ptr[2], &msf_ptr[3]);
            }

            break;
        }
        default: {
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: data type 0x%X not supported!\n", __debug__, cdb->type);
            cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
            return FALSE;
        }
    }

    /* Write data */
    cdemu_device_write_buffer(self, self->priv->buffer_size);

    return TRUE;
}

/* READ DISC STRUCTURE*/
static gboolean command_read_disc_structure (CdemuDevice *self, const guint8 *raw_cdb)
{
    struct READ_DISC_STRUCTURE_CDB *cdb = (struct READ_DISC_STRUCTURE_CDB *)raw_cdb;
    struct READ_DISC_STRUCTURE_Header *head = (struct READ_DISC_STRUCTURE_Header *)self->priv->buffer;
    self->priv->buffer_size = sizeof(struct READ_DISC_STRUCTURE_Header);

    /* Check if we have medium loaded */
    if (!self->priv->loaded) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: medium not present\n", __debug__);
        cdemu_device_write_sense(self, NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }

    /* Validate media type */
    if (cdb->media_type == 0x00) {
        /* DVD and HD DVD types */
        if (self->priv->current_profile != PROFILE_DVDROM && self->priv->current_profile != PROFILE_DVDPLUSR) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: requested to read DVD structure from non-DVD medium!\n", __debug__);
            cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
            return FALSE;
        }
    } else if (cdb->media_type == 0x01) {
        /* BluRay */
        if (self->priv->current_profile != PROFILE_BDROM && self->priv->current_profile != PROFILE_BDR_SRM) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: requested to read BD structure from non-BD medium!\n", __debug__);
            cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
            return FALSE;
        }
    } else {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: invalid medium type code!\n", __debug__);
        cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }

    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: requested structure: 0x%X; layer: %d\n", __debug__, cdb->format, cdb->layer);

    /* Attempt to obtain the structure from the image */
    const guint8 *struct_data = NULL;
    gint struct_len = 0;

    if (mirage_disc_get_disc_structure(self->priv->disc, cdb->layer, cdb->format, &struct_data, &struct_len, NULL)) {
        memcpy(self->priv->buffer+sizeof(struct READ_DISC_STRUCTURE_Header), struct_data, struct_len);
        self->priv->buffer_size += struct_len;
    } else {
        /* Structure not found in the image; try to fabricate it */
        guint8 *tmp_data = NULL;
        gint tmp_len = 0;

        if (!cdemu_device_generate_disc_structure(self, cdb->layer, cdb->format, &tmp_data, &tmp_len)) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: structure %Xh in layer %Xh not provided by image and cannot be fabricated!\n", __debug__, cdb->format, cdb->layer);
            cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
            return FALSE;
        }

        memcpy(self->priv->buffer+sizeof(struct READ_DISC_STRUCTURE_Header), tmp_data, tmp_len);
        self->priv->buffer_size += tmp_len;

        g_free(tmp_data);
    }

    /* Header */
    head->length = GUINT16_TO_BE(self->priv->buffer_size - 2);

    /* Write data */
    cdemu_device_write_buffer(self, GUINT16_FROM_BE(cdb->length));

    return TRUE;
}

/* READ SUB-CHANNEL*/
static gboolean command_read_subchannel (CdemuDevice *self, const guint8 *raw_cdb)
{
    struct READ_SUBCHANNEL_CDB *cdb = (struct READ_SUBCHANNEL_CDB *)raw_cdb;
    struct READ_SUBCHANNEL_Header *ret_header = (struct READ_SUBCHANNEL_Header *)self->priv->buffer;
    self->priv->buffer_size = sizeof(struct READ_SUBCHANNEL_Header);

    MirageDisc *disc = self->priv->disc;

    /* Check if we have medium loaded */
    if (!self->priv->loaded) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: medium not present\n", __debug__);
        cdemu_device_write_sense(self, NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }

    if (cdb->subq) {
        /* I think the subchannel data should be read from sectors, the way real
           devices do it... if Mode-1 is requested, read subchannel from current
           sector, and if it's Mode-2 or Mode-3, interpolate Mode-1 data for it.
           To find Mode-2 or Mode-3, we need to loop over 100 sectors, and return
           data if we find it. Note that even though libMirage's get functions
           should do that for us, we'll do it manually, because we need the
           information about sector where data was found. */
        switch (cdb->param_list) {
            case 0x01: {
                /* Current position */
                struct READ_SUBCHANNEL_Data1 *ret_data = (struct READ_SUBCHANNEL_Data1 *)(self->priv->buffer+self->priv->buffer_size);
                self->priv->buffer_size += sizeof(struct READ_SUBCHANNEL_Data1);

                gint current_address = self->priv->current_address;

                CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: current position (sector 0x%X)\n", __debug__, current_address);
                ret_data->fmt_code = 0x01;

                /* Read current sector's Q subchannel */
                guint8 tmp_buf[16];
                if (read_sector_data(NULL, disc, current_address, 0x00 /* MCSB: empty */, 0x02 /* Subchannel: Q */, tmp_buf, NULL) != 16) {
                    CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to read subchannel of sector 0x%X!\n", __debug__, current_address);
                }

                /* Copy ADR/CTL, track number and index */
                ret_data->adr = tmp_buf[0] & 0x0F;
                ret_data->ctl = (tmp_buf[0] & 0xF0) >> 4;
                ret_data->track = mirage_helper_bcd2hex(tmp_buf[1]);
                ret_data->index = mirage_helper_bcd2hex(tmp_buf[2]);

                /* Now the address; if it happens that we got Mode-2 or Mode-3 Q
                   here, we need to interpolate address from adjacent sector. The
                   universal way to go here is: we take MSF address, convert it from
                   BCD to HEX, then transform it in LBA and apply correction (in case of
                   interpolation), and then convert it back to MSF if required.
                   It's ugly, but safe. */
                /* NOTE: It would seem that Alchohol 120% virtual drive returns BCD
                   data when read using READ CD, and HEX when read via READ SUBCHANNEL.
                   We do the same, because at least 'grip' on linux seems to rely on
                   data returned by READ SUBCHANNEL being HEX... (and it seems MMC3
                   requires READ CD to return BCD data) */
                gint correction = 0;
                while ((tmp_buf[0] & 0x0F) != 0x01) {
                    correction++;
                    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: got a sector that's not Mode-1 Q; taking next one (0x%X)!\n", __debug__, current_address+correction);

                    /* Read from next sector */
                    if (read_sector_data(NULL, disc, current_address+correction, 0x00 /* MCSB: empty */, 0x02 /* Subchannel: Q */, tmp_buf, NULL) != 16) {
                        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: failed to read subchannel of sector 0x%X!\n", __debug__, current_address+correction);
                        break;
                    }
                }

                /* In Q-subchannel, first MSF is relative, second absolute... in
                   data we return, it's the other way around */
                guint8 rmin = mirage_helper_bcd2hex(tmp_buf[3]);
                guint8 rsec = mirage_helper_bcd2hex(tmp_buf[4]);
                guint8 rframe = mirage_helper_bcd2hex(tmp_buf[5]);

                guint8 amin = mirage_helper_bcd2hex(tmp_buf[7]);
                guint8 asec = mirage_helper_bcd2hex(tmp_buf[8]);
                guint8 aframe = mirage_helper_bcd2hex(tmp_buf[9]);

                if (correction != 0) {
                    gint relative_address = mirage_helper_msf2lba(rmin, rsec, rframe, FALSE) - correction;
                    gint absolute_address = mirage_helper_msf2lba(amin, asec, aframe, TRUE) - correction;
                    mirage_helper_lba2msf(relative_address, FALSE, &rmin, &rsec, &rframe);
                    mirage_helper_lba2msf(absolute_address, TRUE, &amin, &asec, &aframe);
                }

                /* MSF vs. LBA */
                if (cdb->time) {
                    guint8 *msf_ptr = (guint8 *)&ret_data->abs_addr;
                    msf_ptr[1] = amin;
                    msf_ptr[2] = asec;
                    msf_ptr[3] = aframe;

                    msf_ptr = (guint8 *)&ret_data->rel_addr;
                    msf_ptr[1] = rmin;
                    msf_ptr[2] = rsec;
                    msf_ptr[3] = rframe;
                } else {
                    ret_data->abs_addr = GUINT32_TO_BE(mirage_helper_msf2lba(amin, asec, aframe, TRUE));
                    ret_data->rel_addr = GUINT32_TO_BE(mirage_helper_msf2lba(rmin, rsec, rframe, FALSE));
                }

                break;
            }
            case 0x02: {
                /* MCN */
                struct READ_SUBCHANNEL_Data2 *ret_data = (struct READ_SUBCHANNEL_Data2 *)(self->priv->buffer+self->priv->buffer_size);
                self->priv->buffer_size += sizeof(struct READ_SUBCHANNEL_Data2);

                CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: MCN/UPC/EAN\n", __debug__);
                ret_data->fmt_code = 0x02;

                /* Go over first 100 sectors; if MCN is present, it should be there */
                for (gint sector = 0; sector < 100; sector++) {
                    guint8 tmp_buf[16];

                    if (read_sector_data(NULL, disc, sector, 0x00 /* MSCB: empty */, 0x02 /* Subchannel: Q */, tmp_buf, NULL) != 16) {
                        CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to read subchannel of sector 0x%X!\n", __debug__, sector);
                        continue;
                    }

                    if ((tmp_buf[0] & 0x0F) == 0x02) {
                        /* Mode-2 Q found */
                        mirage_helper_subchannel_q_decode_mcn(&tmp_buf[1], (gchar *)ret_data->mcn);
                        ret_data->mcval = 1;
                        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: found MCN in subchannel of sector 0x%X: <%.13s>\n", __debug__, sector, ret_data->mcn);
                        break;
                    }
                }

                break;
            }
            case 0x03: {
                /* ISRC */
                struct READ_SUBCHANNEL_Data3 *ret_data = (struct READ_SUBCHANNEL_Data3 *)(self->priv->buffer+self->priv->buffer_size);
                self->priv->buffer_size += sizeof(struct READ_SUBCHANNEL_Data3);

                CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: ISRC\n", __debug__);
                ret_data->fmt_code = 0x03;

                MirageTrack *track = mirage_disc_get_track_by_number(disc, cdb->track, NULL);
                if (!track) {
                    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: failed to get track %i!\n", __debug__, cdb->track);
                    cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
                    return FALSE;
                }

                /* Go over first 100 sectors; if ISRC is present, it should be there */
                for (gint address = 0; address < 100; address++) {
                    guint8 tmp_buf[16];
                    MirageSector *sector;

                    /* Get sector */
                    sector = mirage_track_get_sector(track, address, FALSE, NULL);
                    if (!sector) {
                        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: failed to get sector 0x%X\n", __debug__, address);
                        continue;
                    }

                    /* Read sector */
                    if (read_sector_data(sector, NULL, 0, 0x00 /* MCSB: empty*/, 0x02 /* Subchannel: Q */, tmp_buf, NULL) != 16) {
                        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: failed to read subchannel of sector 0x%X\n", __debug__, address);
                        continue;
                    }

                    g_object_unref(sector);

                    if ((tmp_buf[0] & 0x0F) == 0x03) {
                        /* Mode-3 Q found */
                        /* Copy ADR/CTL and track number */
                        ret_data->adr = tmp_buf[0] & 0x0F;
                        ret_data->ctl = (tmp_buf[0] & 0xF0) >> 4;
                        ret_data->track = tmp_buf[1];
                        /* Copy ISRC */
                        mirage_helper_subchannel_q_decode_isrc(&tmp_buf[1], (gchar *)ret_data->isrc);
                        ret_data->tcval = 1;
                        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: found ISRC in subchannel of sector 0x%X: <%.12s>\n", __debug__, address, ret_data->isrc);
                        break;
                    }
                }

                g_object_unref(track);

                break;
            }
            default: {
                CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: unknown!\n", __debug__);
                break;
            }
        }
    }

    /* Header */
    ret_header->audio_status = cdemu_audio_get_status(CDEMU_AUDIO(self->priv->audio_play)); /* Audio status */
    ret_header->length = GUINT16_TO_BE(self->priv->buffer_size - 4);

    /* Write data */
    cdemu_device_write_buffer(self, GUINT16_FROM_BE(cdb->length));

    return TRUE;
}

/* READ TOC/PMA/ATIP*/
static gboolean command_read_toc_pma_atip (CdemuDevice *self, const guint8 *raw_cdb)
{
    struct READ_TOC_PMA_ATIP_CDB *cdb = (struct READ_TOC_PMA_ATIP_CDB *)raw_cdb;

    if (!self->priv->loaded) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: medium not present\n", __debug__);
        cdemu_device_write_sense(self, NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }

    /* MMC: No fabrication for DVD media is defined for forms other than 000b and 001b. */
    if ((self->priv->current_profile == PROFILE_DVDROM) && !((cdb->format == 0x00) || (cdb->format == 0x01))) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: invalid format type (0x%X) for DVD-ROM image!\n", __debug__, cdb->format);
        cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }

    MirageDisc *disc = self->priv->disc;

    /* Alcohol 120% was being a PITA claiming I was feeding it 'empty disc'...
       upon checking INF-8020, it turns out what MMC-3 specifies as control byte
       is actually used... so we do compatibility mapping here */
    if (cdb->format == 0) {
        if (cdb->control == 0x40) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: compliance to INF-8020 obviously expected... playing along\n", __debug__);
            cdb->format = 0x01;
        }
        if (cdb->control == 0x80) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: compliance to INF-8020 obviously expected... playing along\n", __debug__);
            cdb->format = 0x02;
        }
    }

    switch (cdb->format) {
        case 0x00: {
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: formatted TOC\n", __debug__);
            /* Formatted TOC */
            struct READ_TOC_PMA_ATIP_0_Header *ret_header = (struct READ_TOC_PMA_ATIP_0_Header *)self->priv->buffer;
            self->priv->buffer_size = sizeof(struct READ_TOC_PMA_ATIP_0_Header);
            struct READ_TOC_PMA_ATIP_0_Descriptor *ret_desc = (struct READ_TOC_PMA_ATIP_0_Descriptor *)(self->priv->buffer+self->priv->buffer_size);

            MirageTrack *cur_track;

            /* "For multi-session discs, this command returns the TOC data for
               all sessions and for Track number AAh only the Lead-out area of
               the last complete session." (MMC-3) */

            /* All tracks but lead-out */
            if (cdb->number != 0xAA) {
                gint num_tracks;

                /* If track number exceeds last track number, return error */
                cur_track = mirage_disc_get_track_by_index(disc, -1, NULL);
                if (!cur_track) {
                    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: no track found on disc!\n", __debug__);
                    cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
                    return FALSE;
                }

                num_tracks = mirage_track_layout_get_track_number(cur_track);
                g_object_unref(cur_track);
                if (cdb->number > num_tracks) {
                    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: starting track number (%i) exceeds last track number (%i)!\n", __debug__, cdb->number, num_tracks);
                    cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
                    return FALSE;
                }

                num_tracks = mirage_disc_get_number_of_tracks(disc);

                for (gint i = 0; i < num_tracks; i++) {
                    cur_track = mirage_disc_get_track_by_index(disc, i, NULL);
                    if (!cur_track) {
                        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: failed to get track with index %i (whole disc)!\n", __debug__, i);
                        break;
                    }

                    gint track_number = mirage_track_layout_get_track_number(cur_track);

                    if (track_number >= cdb->number) {
                        /* Track data */
                        gint start_sector = mirage_track_layout_get_start_sector(cur_track);
                        gint track_start = mirage_track_get_track_start(cur_track);

                        ret_desc->adr = mirage_track_get_adr(cur_track);
                        ret_desc->ctl = mirage_track_get_ctl(cur_track);
                        ret_desc->number = track_number;

                        /* (H)MSF vs. LBA */
                        start_sector += track_start;

                        if (cdb->time) {
                            guint8 *msf_ptr = (guint8 *)&ret_desc->lba;
                            mirage_helper_lba2msf(start_sector, TRUE, &msf_ptr[1], &msf_ptr[2], &msf_ptr[3]);
                        } else {
                            ret_desc->lba = GUINT32_TO_BE(start_sector);
                        }

                        self->priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_0_Descriptor);
                        ret_desc++;    /* next descriptor */
                    }

                    g_object_unref(cur_track);
                }
            }

            /* Lead-Out (of the last session): */
            MirageSession *lsession = mirage_disc_get_session_by_index(disc, -1, NULL);
            if (!lsession) {
                CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: no session found on disc!\n", __debug__);
                cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
                return FALSE;
            }

            cur_track = mirage_session_get_track_by_number(lsession, MIRAGE_TRACK_LEADOUT, NULL);

            ret_desc->adr = 0x01;
            ret_desc->ctl = 0x00;
            ret_desc->number = 0xAA;

            /* MSF vs. LBA */
            gint start_sector = mirage_track_layout_get_start_sector(cur_track);
            gint track_start = mirage_track_get_track_start(cur_track);

            start_sector += track_start;

            if (cdb->time) {
                guint8 *msf_ptr = (guint8 *)&ret_desc->lba;
                mirage_helper_lba2msf(start_sector, TRUE, &msf_ptr[1], &msf_ptr[2], &msf_ptr[3]);
            } else {
                ret_desc->lba = GUINT32_TO_BE(start_sector);
            }
            self->priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_0_Descriptor);

            g_object_unref(cur_track);

            /* Header */
            gint ltrack;
            cur_track = mirage_session_get_track_by_index(lsession, -1, NULL);
            ltrack = mirage_track_layout_get_track_number(cur_track);

            g_object_unref(cur_track);
            g_object_unref(lsession);

            ret_header->length = GUINT16_TO_BE(self->priv->buffer_size - 2);
            ret_header->ftrack = 0x01;
            ret_header->ltrack = ltrack;

            break;
        }
        case 0x01: {
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: multisession information\n", __debug__);
            /* Multi-session info */
            struct READ_TOC_PMA_ATIP_1_Data *ret_data = (struct READ_TOC_PMA_ATIP_1_Data *)self->priv->buffer;
            self->priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_1_Data);

            MirageSession *lsession;
            MirageTrack *ftrack;

            lsession = mirage_disc_get_session_by_index(disc, -1, NULL);
            if (!lsession) {
                CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: no session found on disc!\n", __debug__);
                cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
                return FALSE;
            }

            /* Header */
            ret_data->length = GUINT16_TO_BE(self->priv->buffer_size - 2);
            ret_data->fsession = 0x01;
            ret_data->lsession = mirage_session_layout_get_session_number(lsession);

            /* Track data: first track in last session */
            ftrack = mirage_session_get_track_by_index(lsession, 0, NULL);

            ret_data->adr = mirage_track_get_adr(ftrack);
            ret_data->ctl = mirage_track_get_ctl(ftrack);
            ret_data->ftrack = mirage_track_layout_get_track_number(ftrack);

            /* (H)MSF vs. LBA */
            gint start_sector = mirage_track_layout_get_start_sector(ftrack);
            gint track_start = mirage_track_get_track_start(ftrack);

            start_sector += track_start;

            if (cdb->time) {
                guint8 *msf_ptr = (guint8 *)&ret_data->lba;
                mirage_helper_lba2msf(start_sector, TRUE, &msf_ptr[1], &msf_ptr[2], &msf_ptr[3]);
            } else {
                ret_data->lba = GUINT32_TO_BE(start_sector);
            }

            g_object_unref(ftrack);
            g_object_unref(lsession);

            break;
        }
        case 0x02: {
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: raw TOC\n", __debug__);
            /* Raw TOC */
            struct READ_TOC_PMA_ATIP_2_Header *ret_header = (struct READ_TOC_PMA_ATIP_2_Header *)self->priv->buffer;
            self->priv->buffer_size = sizeof(struct READ_TOC_PMA_ATIP_2_Header);
            struct READ_TOC_PMA_ATIP_2_Descriptor *ret_desc = (struct READ_TOC_PMA_ATIP_2_Descriptor *)(self->priv->buffer+self->priv->buffer_size);

            /* For each session with number above the requested one... */
            gint num_sessions = mirage_disc_get_number_of_sessions(disc);
            for (gint i = 0; i < num_sessions; i++) {
                MirageSession *cur_session;
                gint session_number;

                cur_session = mirage_disc_get_session_by_index(disc, i, NULL);
                if (!cur_session) {
                    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: failed to get session by index %i!\n", __debug__, i);
                    break;
                }

                session_number = mirage_session_layout_get_session_number(cur_session);
                /* Return session's data only if its number is greater than or equal to requested one */
                if (session_number >= cdb->number) {
                    MirageTrack *cur_track;

                    /* 1. TOC descriptor: about first track in the session */
                    cur_track = mirage_session_get_track_by_index(cur_session, 0, NULL);

                    ret_desc->session = session_number;
                    ret_desc->adr = mirage_track_get_adr(cur_track);
                    ret_desc->ctl = mirage_track_get_ctl(cur_track);
                    ret_desc->point = 0xA0;
                    ret_desc->pmin = mirage_track_layout_get_track_number(cur_track);
                    ret_desc->psec = map_session_type(mirage_session_get_session_type(cur_session));

                    self->priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_2_Descriptor);
                    ret_desc++;

                    g_object_unref(cur_track);

                    /* 2. TOC descriptor: about last track in last session */
                    cur_track = mirage_session_get_track_by_index(cur_session, -1, NULL);

                    ret_desc->session = session_number;
                    ret_desc->adr = mirage_track_get_adr(cur_track);
                    ret_desc->ctl = mirage_track_get_ctl(cur_track);
                    ret_desc->point = 0xA1;
                    ret_desc->pmin = mirage_track_layout_get_track_number(cur_track);

                    self->priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_2_Descriptor);
                    ret_desc++;

                    g_object_unref(cur_track);

                    /* 3. TOC descriptor: about lead-out */
                    gint leadout_start;

                    cur_track = mirage_session_get_track_by_number(cur_session, MIRAGE_TRACK_LEADOUT, NULL);

                    leadout_start = mirage_track_layout_get_start_sector(cur_track);

                    ret_desc->session = session_number;
                    ret_desc->adr = 0x01;
                    ret_desc->ctl = 0x00;
                    ret_desc->point = 0xA2;

                    mirage_helper_lba2msf(leadout_start, TRUE, &ret_desc->pmin, &ret_desc->psec, &ret_desc->pframe);

                    self->priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_2_Descriptor);
                    ret_desc++;

                    g_object_unref(cur_track);

                    /* And now one TOC descriptor per track */
                    gint num_tracks = mirage_session_get_number_of_tracks(cur_session);

                    for (gint j = 0; j < num_tracks; j++) {
                        cur_track = mirage_session_get_track_by_index(cur_session, j, NULL);
                        if (!cur_track) {
                            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: failed to get track with index %i in session %i!\n", __debug__, j, session_number);
                            break;
                        }

                        ret_desc->session = session_number;
                        ret_desc->adr = mirage_track_get_adr(cur_track);
                        ret_desc->ctl = mirage_track_get_ctl(cur_track);
                        ret_desc->point = mirage_track_layout_get_track_number(cur_track);

                        gint cur_start = mirage_track_layout_get_start_sector(cur_track);
                        gint cur_track_start = mirage_track_get_track_start(cur_track);

                        cur_start += cur_track_start;

                        mirage_helper_lba2msf(cur_start, TRUE, &ret_desc->pmin, &ret_desc->psec, &ret_desc->pframe);

                        self->priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_2_Descriptor);
                        ret_desc++;

                        g_object_unref(cur_track);
                    }

                    /* If we're dealing with multisession disc, it'd probably be
                       a good idea to come up with B0 descriptors... */
                    if (num_sessions > 1) {
                        gint leadout_length;
                        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: multisession disc; cooking up a B0 descriptor for session %i!\n", __debug__, session_number);

                        leadout_length = mirage_session_get_leadout_length(cur_session);

                        ret_desc->session = session_number;
                        ret_desc->adr = 0x05;
                        ret_desc->ctl = 0x00;
                        ret_desc->point = 0xB0;

                        /* If this is last session, we set MSF to 0xFF, indicating
                           disc is closed */
                        if (session_number < num_sessions) {
                            mirage_helper_lba2msf(leadout_start + leadout_length, TRUE, &ret_desc->min, &ret_desc->sec, &ret_desc->frame);
                        } else {
                            ret_desc->min = 0xFF;
                            ret_desc->sec = 0xFF;
                            ret_desc->frame = 0xFF;
                        }

                        /* If this is first session, we'll need to provide C0 as well */
                        if (session_number == 1) {
                            ret_desc->zero = 2; /* Number of Mode-5 entries; we provide B0 and C0 */
                        } else {
                            ret_desc->zero = 1; /* Number of Mode-5 entries; we provide B0 only */
                        }

                        /* FIXME: have disc provide it's max capacity (currently
                           emulating 80 minute disc) */
                        ret_desc->pmin = 0x4F;
                        ret_desc->psec = 0x3B;
                        ret_desc->pframe = 0x47;

                        self->priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_2_Descriptor);
                        ret_desc++;

                        /* Add up C0 for session 1 */
                        if (session_number == 1) {
                            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: multisession disc; cooking up a C0 descriptor for session %i!\n", __debug__, session_number);

                            ret_desc->session = session_number;
                            ret_desc->adr = 0x05;
                            ret_desc->ctl = 0x00;
                            ret_desc->point = 0xC0;

                            ret_desc->min = 0x00;
                            ret_desc->sec = 0x00;
                            ret_desc->frame = 0x00;

                            /* No idea what these are supposed to be... MMC-3 and INF8090 say it's a lead-in of first session,
                               but it doesn't look like it; on internet, I came across a doc which claims that if min/sec/frame
                               are not set to 0x00, and pmin/psec/pframe to the following pattern, the disc is CD-R/RW. */
                            ret_desc->pmin = 0x95;
                            ret_desc->psec = 0x00;
                            ret_desc->pframe = 0x00;

                            self->priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_2_Descriptor);
                            ret_desc++;
                        }
                    }

                    /* FIXME: Should we provide C0 and C1 as well? */

                }
                g_object_unref(cur_session);
            }

            /* Header */
            MirageSession *lsession = mirage_disc_get_session_by_index(disc, -1, NULL);
            if (!lsession) {
                CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: no session found on disc!\n", __debug__);
                cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
                return FALSE;
            }

            ret_header->length = GUINT16_TO_BE(self->priv->buffer_size - 2);
            ret_header->fsession = 0x01;
            ret_header->lsession = mirage_session_layout_get_session_number(lsession);

            g_object_unref(lsession);

            break;
        }
        case 0x04: {
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: ATIP\n", __debug__);
            /* ATIP */
            struct READ_TOC_PMA_ATIP_4_Header *ret_header = (struct READ_TOC_PMA_ATIP_4_Header *)self->priv->buffer;
            self->priv->buffer_size = sizeof(struct READ_TOC_PMA_ATIP_4_Header);

            /* ATIP data is available only for recordable/rewritable media */
            if (self->priv->recordable_disc) {
                struct READ_TOC_PMA_ATIP_4_Descriptor *ret_descriptor = (struct READ_TOC_PMA_ATIP_4_Descriptor *)(self->priv->buffer + sizeof(struct READ_TOC_PMA_ATIP_4_Header));
                self->priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_4_Descriptor);

                ret_descriptor->one1 = 1;
                ret_descriptor->itwp = 0x4; /* Copied from real CD-R */

                ret_descriptor->one2 = 1;
                ret_descriptor->disc_type = self->priv->rewritable_disc ? 1 : 0; /* CD-R: 0, CD-RW: 1 */
                ret_descriptor->disc_subtype = 0x3; /* Copied from real CD-R */

                /* Leadin start; copied from real CD-R */
                ret_descriptor->leadin_start_m = 0x61;
                ret_descriptor->leadin_start_s = 0x22;
                ret_descriptor->leadin_start_f = 0x17;

                /* Last possible leadout; corresponds to capacity (on
                   my drive, capacity minus two) */
                mirage_helper_lba2msf(self->priv->medium_capacity - 2, FALSE, &ret_descriptor->last_leadout_m, &ret_descriptor->last_leadout_s, &ret_descriptor->last_leadout_f);
            }

            /* Header */
            ret_header->length = GUINT16_TO_BE(self->priv->buffer_size - 2);

            break;
        }
        case 0x05: {
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: CD-Text\n", __debug__);
            /* CD-TEXT */
            struct READ_TOC_PMA_ATIP_5_Header *ret_header = (struct READ_TOC_PMA_ATIP_5_Header *)self->priv->buffer;
            self->priv->buffer_size = sizeof(struct READ_TOC_PMA_ATIP_5_Header);

            guint8 *tmp_data = NULL;
            gint tmp_len  = 0;
            MirageSession *session;

            /* FIXME: for the time being, return data for first session */
            session = mirage_disc_get_session_by_index(self->priv->disc, 0, NULL);
            if (!session) {
                CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: no session found on disc!\n", __debug__);
                cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
                return FALSE;
            }

            if (!mirage_session_get_cdtext_data(session, &tmp_data, &tmp_len, NULL)) {
                CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: failed to get CD-TEXT data!\n", __debug__);
            }
            g_object_unref(session);

            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: length of CD-TEXT data: 0x%X\n", __debug__, tmp_len);

            memcpy(self->priv->buffer+sizeof(struct READ_TOC_PMA_ATIP_5_Header), tmp_data, tmp_len);
            g_free(tmp_data);
            self->priv->buffer_size += tmp_len;

            /* Header */
            ret_header->length = GUINT16_TO_BE(self->priv->buffer_size - 2);

            break;
        }
        default: {
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: format %X not supported yet\n", __debug__, cdb->format);
            cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
            return FALSE;
        }
    }

    /* Write data */
    cdemu_device_write_buffer(self, GUINT16_FROM_BE(cdb->length));

    return TRUE;
}

/* READ TRACK INFORMATION*/
static gboolean command_read_track_information (CdemuDevice *self, const guint8 *raw_cdb)
{
    struct READ_TRACK_INFORMATION_CDB *cdb = (struct READ_TRACK_INFORMATION_CDB *)raw_cdb;
    struct READ_TRACK_INFORMATION_Data *ret_data = (struct READ_TRACK_INFORMATION_Data *)self->priv->buffer;
    self->priv->buffer_size = sizeof(struct READ_TRACK_INFORMATION_Data);

    MirageDisc *disc = self->priv->disc;
    MirageTrack *track = NULL;

    gboolean return_empty_track = FALSE;
    gboolean return_disc_leadin = FALSE;

    if (!self->priv->loaded) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: medium not present\n", __debug__);
        cdemu_device_write_sense(self, NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }

    gint number = GUINT32_FROM_BE(cdb->number);

    if (cdb->type == 0x00) {
        /* LBA */
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: requested track containing sector 0x%X\n", __debug__, number);
        track = mirage_disc_get_track_by_address(disc, number, NULL);
        if (!track && self->priv->recordable_disc) {
            return_empty_track = TRUE;
        }
    } else if (cdb->type == 0x01) {
        /* Track number */
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: requested track %d (%Xh)\n", __debug__, number, number);
        if (number >= 1 && number <= mirage_disc_get_number_of_tracks(disc)) {
            /* Existing track */
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: requested existing track\n", __debug__);
            track = mirage_disc_get_track_by_number(disc, number, NULL);
        } else if (number == mirage_disc_get_number_of_tracks(disc) + 1) {
            /* Empty track (N+1); valid only on recordable disc */
            if (self->priv->recordable_disc) {
                CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: will return entry for empty track\n", __debug__);
                return_empty_track = TRUE;
            } else {
                CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: empty track cannot be requested on non-recordable disc; will bail out!\n", __debug__);
            }
        } else if (number == 0x00) {
            /* Disc lead-in; valid only on recordable disc */
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: requested disc lead-in\n", __debug__);
            if (self->priv->recordable_disc) {
                return_disc_leadin = TRUE;
            } else {
                CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: disc lead-in cannot be requested on non-recordable disc; will bail out!\n", __debug__);
            }
        } else if (number == 0xFF) {
            /* Invisible/incomplete track; valid only on recordable disc */
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: requested invisible/incomplete track\n", __debug__);
            if (self->priv->recordable_disc) {
                return_empty_track = TRUE;
            } else {
                CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: invisible/incomplete track cannot be requested on non-recordable disc; will bail out!\n", __debug__);
            }
        }
    } else if (cdb->type == 0x02) {
        /* Session number */
        MirageSession *session;
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: requested first track in session %i\n", __debug__, number);

        session = mirage_disc_get_session_by_number(disc, number, NULL);
        if (session) {
            track = mirage_session_get_track_by_index(session, 0, NULL);
            g_object_unref(session);
        }
    }

    gint track_number;
    gint session_number;
    gint track_mode;
    gint data_mode;
    guint32 start_sector = 0x000000;
    guint32 next_writable_address = 0x000000;
    guint32 free_blocks = 0x000000;
    guint32 fixed_packet_size = 0x000000;
    guint32 length = 0x000000;
    guint32 last_recorded_address = 0x000000;
    gboolean nwa_valid = FALSE;
    gboolean blank_track = FALSE;

    /* Check if track was found */
    if (track) {
        /* If track was found, get its information */
        track_number = mirage_track_layout_get_track_number(track);
        session_number = mirage_track_layout_get_session_number(track);

        track_mode = mirage_track_get_ctl(track);

        switch (mirage_track_get_sector_type(track)) {
            case MIRAGE_SECTOR_AUDIO:
            case MIRAGE_SECTOR_MODE1: {
                data_mode = 0x01;
                break;
            }
            case MIRAGE_SECTOR_MODE2:
            case MIRAGE_SECTOR_MODE2_FORM1:
            case MIRAGE_SECTOR_MODE2_FORM2:
            case MIRAGE_SECTOR_MODE2_MIXED: {
                data_mode = 0x02;
                break;
            }
            default: {
                data_mode = 0x0F;
            }
        }

        start_sector = mirage_track_layout_get_start_sector(track);
        length = mirage_track_layout_get_length(track);

        g_object_unref(track);
    } else if (return_empty_track) {
        /* Return information for next empty track */
        track_number = mirage_disc_get_number_of_tracks(disc) + 1;
        session_number = mirage_disc_get_number_of_sessions(disc) + 1;
        data_mode = 0x01;
        track_mode = 0x07;

        free_blocks = self->priv->medium_capacity - 150;
        length = self->priv->medium_capacity - 150;

        blank_track = TRUE;

        if (self->priv->recording) {
            nwa_valid = TRUE;
            next_writable_address = self->priv->recording->get_next_writable_address(self);
        }
    } else if (return_disc_leadin) {
        /* Return information for disc lead-in */
        track_number = 0;
        session_number = 0;
        track_mode = 0x00;
        data_mode = 0x00;
        start_sector = self->priv->medium_leadin;
    } else {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: couldn't find track!\n", __debug__);
        cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }

    /* Write the obtained data */
    ret_data->length = GUINT16_TO_BE(self->priv->buffer_size - 2);
    ret_data->track_number0 = track_number >> 8;
    ret_data->session_number0 = session_number >> 8;

    ret_data->track_mode = track_mode;

    ret_data->blank = blank_track;
    ret_data->data_mode = data_mode;

    ret_data->nwa_v = nwa_valid;

    ret_data->start_address = GUINT32_TO_BE(start_sector);
    ret_data->next_writable_address = GUINT32_TO_BE(next_writable_address);
    ret_data->free_blocks = GUINT32_TO_BE(free_blocks);
    ret_data->fixed_packet_size = GUINT32_TO_BE(fixed_packet_size);
    ret_data->track_size = GUINT32_TO_BE(length);
    ret_data->last_recorded_address = GUINT32_TO_BE(last_recorded_address);

    ret_data->track_number1 = track_number & 0xFF;
    ret_data->session_number1 = session_number & 0xFF;

    /* Write data */
    cdemu_device_write_buffer(self, GUINT16_FROM_BE(cdb->length));

    return TRUE;
}

/* REPORT KEY*/
static gboolean command_report_key (CdemuDevice *self, const guint8 *raw_cdb)
{
    struct REPORT_KEY_CDB *cdb = (struct REPORT_KEY_CDB *)raw_cdb;

    if (cdb->key_format == 0x08) {
        /* RPC */
        struct REPORT_KEY_8_Data *data = (struct REPORT_KEY_8_Data *)self->priv->buffer;
        self->priv->buffer_size = sizeof(struct REPORT_KEY_8_Data);

        data->type_code = 0; /* No region setting */
        data->vendor_resets = 4;
        data->user_changes = 5;
        data->region_mask = 0xFF; /* It's what all my drives return... */
        data->rpc_scheme = 1; /* It's what all my drives return... */

        data->length = GUINT16_TO_BE(self->priv->buffer_size - 2);
    } else {
        if (self->priv->current_profile != PROFILE_DVDROM) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: not supported with non-DVD media!\n", __debug__);
            cdemu_device_write_sense(self, ILLEGAL_REQUEST, CANNOT_READ_MEDIUM_INCOMPATIBLE_FORMAT);
            return FALSE;
        }

        /* We don't support these yet */
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: FIXME: not implemented yet!\n", __debug__);
        cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }

    /* Write data */
    cdemu_device_write_buffer(self, GUINT16_FROM_BE(cdb->length));

    return TRUE;
}

/* REQUEST SENSE*/
static gboolean command_request_sense (CdemuDevice *self, const guint8 *raw_cdb)
{
    struct REQUEST_SENSE_CDB *cdb = (struct REQUEST_SENSE_CDB *)raw_cdb;
    struct REQUEST_SENSE_SenseFixed *sense = (struct REQUEST_SENSE_SenseFixed *)self->priv->buffer;
    self->priv->buffer_size = sizeof(struct REQUEST_SENSE_SenseFixed);

    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: returning sense data\n", __debug__);

    /* REQUEST SENSE is used for retrieving deferred errors; right now, we
       don't support reporting those (actually, we don't generate them, either),
       so we return empty sense here */
    sense->res_code = 0x70; /* Current error */
    sense->valid = 0;
    sense->length = 0x0A; /* Additional sense length */

    /* MMC-3: the status of the play operation may be determined by issuing a
       REQUEST SENSE command. The sense key is set to NO SENSE, the ASC is set
       to NO ADDITIONAL SENSE DATA and the audio status is reported in
       the additional sense code qualifier field. */
    sense->sense_key = NO_SENSE;
    sense->asc = NO_ADDITIONAL_SENSE_INFORMATION;
    sense->ascq = cdemu_audio_get_status(CDEMU_AUDIO(self->priv->audio_play));

    /* Write data */
    cdemu_device_write_buffer(self, cdb->length);

    return TRUE;
}

/* RESERVE TRACK */
static gboolean command_reserve_track (CdemuDevice *self, const guint8 *raw_cdb)
{
    struct RESERVE_TRACK_CDB *cdb = (struct RESERVE_TRACK_CDB *)raw_cdb;
    guint track_length;

    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: reserve track: ARSV: %d RMZ: %d\n", __debug__, cdb->arsv, cdb->rmz);

    /* Make sure we have recording mode set */
    if (!self->priv->recording) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: RESERVE TRACK called without recording mode set\n", __debug__);
        cdemu_device_write_sense(self, ILLEGAL_REQUEST, COMMAND_SEQUENCE_ERROR);
        return FALSE;
    }

    /* Make sure set recording mode implements reserve track */
    if (!self->priv->recording->reserve_track) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: RESERVE TRACK called in recoding mode that does not implement it (yet?)\n", __debug__);
        cdemu_device_write_sense(self, ILLEGAL_REQUEST, COMMAND_SEQUENCE_ERROR);
        return FALSE;
    }

    if (!cdb->arsv) {
        /* Reservation size format */
        struct RESERVE_TRACK_SizeParameter *param = (struct RESERVE_TRACK_SizeParameter *)cdb->parameter;
        guint32 reservation_size = GUINT32_FROM_BE(param->reservation_size);
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: reservation size format: %d (%X)\n", __debug__, reservation_size, reservation_size);
        track_length = reservation_size;
    } else {
        /* Reservation LBA format */
        struct RESERVE_TRACK_AddressParameter *param = (struct RESERVE_TRACK_AddressParameter *)cdb->parameter;
        guint32 reservation_lba = GUINT32_FROM_BE(param->reservation_lba);
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: reservation LBA format: %d (%X)\n", __debug__, reservation_lba, reservation_lba);

        /* Is this correct? */
        track_length = reservation_lba - self->priv->recording->get_next_writable_address(self);
    }

    return self->priv->recording->reserve_track(self, track_length);
}


/* SEEK (10)*/
static gboolean command_seek (CdemuDevice *self, const guint8 *raw_cdb G_GNUC_UNUSED)
{
    /*struct SET_CD_SPEED_CDB *cdb = (struct SET_CD_SPEED_CDB *)raw_cdb;*/
    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: nothing to do here yet...\n", __debug__);
    return TRUE;
}

/* SEND CUE SHEET */
static gboolean command_send_cue_sheet (CdemuDevice *self, const guint8 *raw_cdb)
{
    struct SEND_CUE_SHEET_CDB *cdb = (struct SEND_CUE_SHEET_CDB *)raw_cdb;
    struct ModePage_0x05 *p_0x05 = cdemu_device_get_mode_page(self, 0x05, MODE_PAGE_CURRENT);
    gint cue_sheet_size = GUINT24_FROM_BE(cdb->cue_sheet_size);

    /* Verify that we are in SAO/DAO mode */
    if (p_0x05->write_type != 2) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: CUE sheet sent when write type is not set to Session-at-Once!\n", __debug__);
        cdemu_device_write_sense(self, ILLEGAL_REQUEST, COMMAND_SEQUENCE_ERROR);
        return FALSE;
    }

    /* Read CUE sheet */
    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: reading CUE sheet (%d bytes)\n", __debug__, cue_sheet_size);
    cdemu_device_read_buffer(self, cue_sheet_size);

    /* Dump CUE sheet */
    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: received CUE sheet:\n", __debug__);
    CDEMU_DEBUG_PRINT_BUFFER(self, DAEMON_DEBUG_MMC, __debug__, 8, self->priv->buffer, cue_sheet_size);

    /* Parse CUE sheet */
    if (!cdemu_device_sao_recording_parse_cue_sheet(self, self->priv->buffer, cue_sheet_size)) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: failed to parse CUE sheet!\n", __debug__);
        return FALSE;
    }

    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: successfully parsed CUE sheet!\n", __debug__);

    return TRUE;
}

/* SET CD SPEED*/
static gboolean command_set_cd_speed (CdemuDevice *self, const guint8 *raw_cdb)
{
    struct SET_CD_SPEED_CDB *cdb = (struct SET_CD_SPEED_CDB *)raw_cdb;
    struct ModePage_0x2A *p_0x2A = cdemu_device_get_mode_page(self, 0x2A, MODE_PAGE_CURRENT);

    /* Set the value to mode page and do nothing else at the moment...
       Note that we don't have to convert from BE neither for comparison (because
       it's 0xFFFF and unsigned short) nor when setting value (because it's BE in
       mode page anyway) */
    if (cdb->read_speed == 0xFFFF) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: setting read speed to max\n", __debug__);
        p_0x2A->cur_read_speed = p_0x2A->max_read_speed;
    } else {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: setting read speed to %i kB/s\n", __debug__, GUINT16_FROM_BE(cdb->read_speed));
        p_0x2A->cur_read_speed = cdb->read_speed;
    }

    if (cdb->write_speed == 0xFFFF) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: setting read write to max\n", __debug__);
        p_0x2A->cur_write_speed = p_0x2A->cur_wspeed = p_0x2A->max_write_speed;
    } else {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: setting read write to %i kB/s\n", __debug__, GUINT16_FROM_BE(cdb->write_speed));
        p_0x2A->cur_write_speed = p_0x2A->cur_wspeed = cdb->write_speed;
    }

    return TRUE;
}

/* SET STREAMING */
static gboolean command_set_streaming (CdemuDevice *self, const guint8 *raw_cdb)
{
    struct SET_STREAMING_CDB *cdb = (struct SET_STREAMING_CDB *)raw_cdb;
    guint16 length = GUINT16_FROM_BE(cdb->length);

    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: performance descriptor type %d, length: %d\n", __debug__, cdb->type, length);

    /* Read descriptors */
    cdemu_device_read_buffer(self, length);

    /* Dump descriptors */
    CDEMU_DEBUG_PRINT_BUFFER(self, DAEMON_DEBUG_MMC, __debug__, 16, self->priv->buffer, length);

    return TRUE;
}

/* START/STOP UNIT */
static gboolean command_start_stop_unit (CdemuDevice *self, const guint8 *raw_cdb)
{
    struct START_STOP_UNIT_CDB *cdb = (struct START_STOP_UNIT_CDB *)raw_cdb;

    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: lo_ej: %d; start: %d\n", __debug__, cdb->lo_ej, cdb->start);

    if (cdb->lo_ej) {
        if (!cdb->start) {

            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: unloading disc...\n", __debug__);
            if (!cdemu_device_unload_disc_private(self, NULL)) {
                CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: failed to unload disc\n", __debug__);
                cdemu_device_write_sense(self, NOT_READY, MEDIUM_REMOVAL_PREVENTED);
                return FALSE;
            } else {
                CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: successfully unloaded disc\n", __debug__);
            }
        }
    }

    return TRUE;
}

/* SYNCHRONIZE CACHE */
static gboolean command_synchronize_cache (CdemuDevice *self, const guint8 *raw_cdb)
{
    struct SYNCHRONIZE_CACHE_CDB *cdb = (struct SYNCHRONIZE_CACHE_CDB *)raw_cdb;
    guint32 lba = GUINT32_FROM_BE(cdb->lba);
    guint16 blocks = GUINT16_FROM_BE(cdb->blocks);

    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: request for cache sync from LBA %Xh (%d), num blocks: %d\n", __debug__, lba, lba, blocks);

    /* Use recording structure */
    if (!self->priv->recording) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: no recording mode set!\n", __debug__);
        cdemu_device_write_sense(self, ILLEGAL_REQUEST, COMMAND_SEQUENCE_ERROR);
        return FALSE;
    }

    return self->priv->recording->close_track(self);
}


/* TEST UNIT READY*/
static gboolean command_test_unit_ready (CdemuDevice *self, const guint8 *raw_cdb G_GNUC_UNUSED)
{
    /*struct TEST_UNIT_READY_CDB *cdb = (struct TEST_UNIT_READY_CDB *)raw_cdb;*/

    /* Check if we have medium loaded */
    if (!self->priv->loaded) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: medium not present\n", __debug__);
        cdemu_device_write_sense(self, NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }

    /* SCSI requires us to report UNIT ATTENTION with NOT READY TO READY CHANGE,
       MEDIUM MAY HAVE CHANGED whenever medium changes... this is required for
       linux SCSI layer to set medium block size properly upon disc insertion */
    if (self->priv->media_event == MEDIA_EVENT_NEW_MEDIA) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: reporting media changed\n", __debug__);
        self->priv->media_event = MEDIA_EVENT_NOCHANGE;
        cdemu_device_write_sense(self, UNIT_ATTENTION, NOT_READY_TO_READY_CHANGE_MEDIUM_MAY_HAVE_CHANGED);
        return FALSE;
    }

    return TRUE;
}

/* WRITE (10) */
static gboolean command_write (CdemuDevice *self, const guint8 *raw_cdb)
{
    gint start_address; /* MUST be signed because it may be negative! */
    gint num_sectors;
    gboolean succeeded = TRUE;

    /* WRITE 10 vs WRITE 12 */
    if (raw_cdb[0] == WRITE_10) {
        struct WRITE_10_CDB *cdb = (struct WRITE_10_CDB *)raw_cdb;
        start_address = GUINT32_FROM_BE(cdb->lba);
        num_sectors  = GUINT16_FROM_BE(cdb->length);
    } else {
        struct WRITE_12_CDB *cdb = (struct WRITE_12_CDB *)raw_cdb;
        start_address = GUINT32_FROM_BE(cdb->lba);
        num_sectors  = GUINT32_FROM_BE(cdb->length);
    }

    /* Write */
    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: write request: start sector: 0x%X (%d), number of sectors: %d\n", __debug__, start_address, start_address, num_sectors);

    if (!self->priv->recording) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: no recording mode set!\n", __debug__);
        cdemu_device_write_sense(self, ILLEGAL_REQUEST, COMMAND_SEQUENCE_ERROR);
        return FALSE;
    }

    succeeded = self->priv->recording->write_sectors(self, start_address, num_sectors);

    return succeeded;
}


/**********************************************************************\
 *                      Packet command switch                         *
\**********************************************************************/
gint cdemu_device_execute_command (CdemuDevice *self, const guint8 *cdb)
{
    SenseStatus status = CHECK_CONDITION;

    /* Flush buffer */
    cdemu_device_flush_buffer(self);

    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "\n");
    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", __debug__,
        cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5],
        cdb[6], cdb[7], cdb[8], cdb[9], cdb[10], cdb[11]);

    /* Packet command table */
    static struct {
        PacketCommand cmd;
        gchar *debug_name;
        gboolean (*implementation)(CdemuDevice *, const guint8 *);
        gboolean interrupt_audio_play;
    } packet_commands[] = {
        { CLOSE_TRACK_SESSION,
          "CLOSE TRACK/SESSION",
          command_close_track_session,
          TRUE },
        { GET_EVENT_STATUS_NOTIFICATION,
          "GET EVENT/STATUS NOTIFICATION",
          command_get_event_status_notification,
          FALSE },
        { GET_CONFIGURATION,
          "GET CONFIGURATION",
          command_get_configuration,
          FALSE, },
        { GET_PERFORMANCE,
          "GET PERFORMANCE",
          command_get_performance,
          FALSE, },
        { INQUIRY,
          "INQUIRY",
          command_inquiry,
          FALSE },
        { MODE_SELECT_6,
          "MODE SELECT (6)",
          command_mode_select,
          FALSE },
        { MODE_SELECT_10,
          "MODE SELECT (10)",
          command_mode_select,
          FALSE },
        { MODE_SENSE_6,
          "MODE SENSE (6)",
          command_mode_sense,
          FALSE },
        { MODE_SENSE_10,
          "MODE SENSE (10)",
          command_mode_sense,
          FALSE },
        { PAUSE_RESUME,
          "PAUSE/RESUME",
          command_pause_resume,
          FALSE /* Well, it does... but in it's own, unique way :P */ },
        { PLAY_AUDIO_10,
          "PLAY AUDIO (10)",
          command_play_audio,
          TRUE },
        { PLAY_AUDIO_12,
          "PLAY AUDIO (12)",
          command_play_audio,
          TRUE },
        { PLAY_AUDIO_MSF,
          "PLAY AUDIO MSF",
          command_play_audio,
          TRUE },
        { PREVENT_ALLOW_MEDIUM_REMOVAL,
          "PREVENT/ALLOW MEDIUM REMOVAL",
          command_prevent_allow_medium_removal,
          FALSE },
        { READ_10,
          "READ (10)",
          command_read,
          TRUE },
        { READ_12,
          "READ (12)",
          command_read,
          TRUE },
        { READ_BUFFER_CAPACITY,
          "READ BUFFER CAPACITY",
          command_read_buffer_capacity,
          FALSE },
        { READ_CAPACITY,
          "READ CAPACITY",
          command_read_capacity,
          FALSE },
        { READ_CD,
          "READ CD",
          command_read_cd,
          FALSE },
        { READ_CD_MSF,
          "READ CD MSF",
          command_read_cd,
          FALSE },
        { READ_DISC_INFORMATION,
          "READ DISC INFORMATION",
          command_read_disc_information,
          TRUE },
        { READ_DISC_STRUCTURE,
          "READ DISC STRUCTURE",
          command_read_disc_structure,
          TRUE },
        { READ_TOC_PMA_ATIP,
          "READ TOC/PMA/ATIP",
          command_read_toc_pma_atip,
          FALSE },
        { READ_TRACK_INFORMATION,
          "READ TRACK INFORMATION",
          command_read_track_information,
          TRUE },
        { READ_SUBCHANNEL,
          "READ SUBCHANNEL",
          command_read_subchannel,
          FALSE },
        { REPORT_KEY,
          "REPORT KEY",
          command_report_key,
          TRUE },
        { REQUEST_SENSE,
          "REQUEST SENSE",
          command_request_sense,
          FALSE },
        { RESERVE_TRACK,
          "RESERVE TRACK",
          command_reserve_track,
          TRUE },
        { SEEK_10,
          "SEEK (10)",
          command_seek,
          TRUE },
        { SEND_CUE_SHEET,
          "SEND CUE SHEET",
          command_send_cue_sheet,
          TRUE },
        { SET_CD_SPEED,
          "SET CD SPEED",
          command_set_cd_speed,
          TRUE },
        { SET_STREAMING,
          "SET STREAMING",
          command_set_streaming,
          TRUE },
        { START_STOP_UNIT,
          "START/STOP UNIT",
          command_start_stop_unit,
          TRUE },
        { SYNCHRONIZE_CACHE,
          "SYNCHRONIZE CACHE",
          command_synchronize_cache,
          FALSE },
        { TEST_UNIT_READY,
          "TEST UNIT READY",
          command_test_unit_ready,
          FALSE },
        { WRITE_10,
          "WRITE (10)",
          command_write,
          TRUE },
        { WRITE_12,
          "WRITE (12)",
          command_write,
          TRUE },
    };

    /* Find the command and execute its implementation handler */
    for (guint i = 0; i < G_N_ELEMENTS(packet_commands); i++) {
        if (packet_commands[i].cmd == cdb[0]) {
            gboolean succeeded = FALSE;

            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: command: %s\n", __debug__, packet_commands[i].debug_name);

            /* Lock */
            g_mutex_lock(self->priv->device_mutex);

            /* FIXME: If there is deferred error sense available, return CHECK CONDITION
               with that sense. We do not execute requested command. */

            /* Stop audio play if command interrupts it */
            if (packet_commands[i].interrupt_audio_play) {
                gint audio_status = cdemu_audio_get_status(CDEMU_AUDIO(self->priv->audio_play));
                if (audio_status == AUDIO_STATUS_PLAYING || audio_status == AUDIO_STATUS_PAUSED) {
                    cdemu_audio_stop(CDEMU_AUDIO(self->priv->audio_play));
                }
            }
            /* Execute the command */
            succeeded = packet_commands[i].implementation(self, cdb);
            status = (succeeded) ? GOOD : CHECK_CONDITION;

            /* Unlock */
            g_mutex_unlock(self->priv->device_mutex);

            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: command completed with status %d\n", __debug__, status);

            return status;
        }
    }

    /* Command not found */
    CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: packet command %02Xh not implemented yet!\n", __debug__, cdb[0]);
    cdemu_device_write_sense(self, ILLEGAL_REQUEST, INVALID_COMMAND_OPERATION_CODE);

    return status;
}
