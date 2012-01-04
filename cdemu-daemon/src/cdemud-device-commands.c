/*
 *  CDEmuD: Device object - packet commands
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

#include "cdemud.h"
#include "cdemud-device-private.h"

#define __debug__ "MMC-3"


/**********************************************************************\
 *                               Helpers                              *
\**********************************************************************/
static gint map_expected_sector_type (gint type)
{
    switch (type) {
        case 0: return 0; /* All types */
        case 1: return MIRAGE_MODE_AUDIO; /* CD-DA */
        case 2: return MIRAGE_MODE_MODE1; /* Mode 1 */
        case 3: return MIRAGE_MODE_MODE2; /* Mode 2 Formless */
        case 4: return MIRAGE_MODE_MODE2_FORM1; /* Mode 2 Form 1 */
        case 5: return MIRAGE_MODE_MODE2_FORM2; /* Mode 2 Form 2 */
        default: return -1;
    }
}

static gint map_mcsb (guint8 *byte9, gint mode_code)
{
    guint8 mode = 0;
    guint8 cur_mode = (*byte9) & 0xF8;
    gint i;

    /* The matrix (TM) */
    static gint matrix[32][6] = {
        {0x00,  0x00, 0x00, 0x00, 0x00, 0x00},
        {0x08,  0x10, 0x08, 0x10, 0x10, 0x10},
        {0x10,  0x10, 0x10, 0x10, 0x10, 0x10},
        {0x18,  0x10, 0x18, 0x10, 0x18, 0x18},
        {0x20,  0x10, 0x20, 0x20, 0x20, 0x20},
        {0x28,  0x10,   -1,   -1,   -1,   -1},
        {0x30,  0x10, 0x30, 0x30,   -1,   -1},
        {0x38,  0x10, 0x38, 0x30,   -1,   -1},
        {0x40,  0x10, 0x00, 0x00, 0x40, 0x40},
        {0x48,  0x10,   -1,   -1,   -1,   -1},
        {0x50,  0x10, 0x10, 0x10, 0x50, 0x50},
        {0x58,  0x10, 0x18, 0x10, 0x58, 0x58},
        {0x60,  0x10, 0x20, 0x20, 0x60, 0x60},
        {0x68,  0x10,   -1,   -1,   -1,   -1},
        {0x70,  0x10, 0x30, 0x30, 0x70, 0x70},
        {0x78,  0x10, 0x38, 0x38, 0x78, 0x78},
        {0x80,  0x10, 0x80, 0x80, 0x80, 0x80},
        {0x88,  0x10,   -1,   -1,   -1,   -1},
        {0x90,  0x10,   -1,   -1,   -1,   -1},
        {0x98,  0x10,   -1,   -1,   -1,   -1},
        {0xA0,  0x10, 0xA0, 0xA0, 0xA0, 0xA0},
        {0xA8,  0x10,   -1,   -1,   -1,   -1},
        {0xB0,  0x10, 0xB0, 0xB0,   -1,   -1},
        {0xB8,  0x10, 0xB8, 0xB0,   -1,   -1},
        {0xC0,  0x10,   -1,   -1,   -1,   -1},
        {0xC8,  0x10,   -1,   -1,   -1,   -1},
        {0xD0,  0x10,   -1,   -1,   -1,   -1},
        {0xD8,  0x10,   -1,   -1,   -1,   -1},
        {0xE0,  0x10, 0xA0, 0xA0, 0xE0, 0xE0},
        {0xE8,  0x10,   -1,   -1,   -1,   -1},
        {0xF0,  0x10, 0xB0, 0xB0, 0xF0, 0xF0},
        {0xF8,  0x10, 0xB8, 0xB8, 0xF8, 0xF8}
    };

    /* First, let's decode mode_code :)
        - CD-DA = 1
        - Mode 1 = 2
        - Mode 2 Formless = 3
        - Mode 2 Form 1 = 4
        - Mode 2 Form 2 = 5
        (so that values are actually offsets in matrix) */
    switch (mode_code) {
        case MIRAGE_MODE_AUDIO: mode = 1; break;
        case MIRAGE_MODE_MODE1: mode = 2; break;
        case MIRAGE_MODE_MODE2: mode = 3; break;
        case MIRAGE_MODE_MODE2_FORM1: mode = 4; break;
        case MIRAGE_MODE_MODE2_FORM2: mode = 5; break;
        return -1;
    }

    for (i = 0; i < 32; i++) {
        if (cur_mode == matrix[i][0]) {
            /* Clear current MCSB */
            cur_mode &= 0x07;
            /* Apply new MCSB */
            cur_mode |= matrix[i][mode];
            /* Write */
            *byte9 = cur_mode;
            return 0;
        }
    }

    return -1;
}


/**********************************************************************\
 *                     Packet command implementations                 *
\**********************************************************************/
/* GET CONFIGURATION*/
static gboolean command_get_configuration (CDEMUD_Device *self, guint8 *raw_cdb)
{
    struct GET_CONFIGURATION_CDB *cdb = (struct GET_CONFIGURATION_CDB*)raw_cdb;
    struct GET_CONFIGURATION_Header *ret_header = (struct GET_CONFIGURATION_Header *)self->priv->buffer;
    self->priv->buffer_size = sizeof(struct GET_CONFIGURATION_Header);
    guint8 *ret_data = self->priv->buffer+self->priv->buffer_size;

    CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: requesting features from 0x%X on, with RT flag 0x%X\n", __debug__, GUINT16_FROM_BE(cdb->sfn), cdb->rt);

    /* Go over *all* features, and copy them according to RT value */
    GList *entry = NULL;
    G_LIST_FOR_EACH(entry, self->priv->features_list) {
        struct Feature_GENERAL *feature = entry->data;

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

                CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: copying feature 0x%X\n", __debug__, GUINT16_FROM_BE(feature->code));

                /* Copy feature */
                memcpy(ret_data, feature, feature->length + 4);
                self->priv->buffer_size += feature->length + 4;
                ret_data += feature->length + 4;

                /* Break the loop if RT is 0x02 */
                if (cdb->rt == 0x02) {
                    CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: got the feature we wanted (0x%X), breaking the loop\n", __debug__, GUINT16_FROM_BE(cdb->sfn));
                    break;
                }
            }
        }
    }

    /* Header */
    ret_header->length = GUINT32_TO_BE(self->priv->buffer_size - 4);
    ret_header->cur_profile = GUINT16_TO_BE(self->priv->current_profile);

    /* Write data */
    cdemud_device_write_buffer(self, GUINT16_FROM_BE(cdb->length));

    return TRUE;
}

/* GET EVENT/STATUS NOTIFICATION*/
static gboolean command_get_event_status_notification (CDEMUD_Device *self, guint8 *raw_cdb)
{
    struct GET_EVENT_STATUS_NOTIFICATION_CDB *cdb = (struct GET_EVENT_STATUS_NOTIFICATION_CDB*)raw_cdb;
    struct GET_EVENT_STATUS_NOTIFICATION_Header *ret_header = (struct GET_EVENT_STATUS_NOTIFICATION_Header *)self->priv->buffer;
    self->priv->buffer_size = sizeof(struct GET_EVENT_STATUS_NOTIFICATION_Header);

    if (!cdb->immed) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: asynchronous type not supported yet!\n", __debug__);
        cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
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

        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: media event class\n", __debug__);

        ret_header->nea = 0;
        ret_header->not_class = 4; /* Media notification class */

        /* Report current media event and then reset it */
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: reporting media event 0x%X\n", __debug__, self->priv->media_event);
        ret_desc->event = self->priv->media_event;
        self->priv->media_event = MEDIA_EVENT_NOCHANGE;

        /* Media status */
        ret_desc->present = self->priv->loaded;
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: medium present: %d\n", __debug__, ret_desc->present);
    }

    /* Header */
    ret_header->length = GUINT16_TO_BE(self->priv->buffer_size - 2);

    /* Write data */
    cdemud_device_write_buffer(self, GUINT16_FROM_BE(cdb->length));

    return TRUE;
}

/* INQUIRY*/
static gboolean command_inquiry (CDEMUD_Device *self, guint8 *raw_cdb)
{
    struct INQUIRY_CDB *cdb = (struct INQUIRY_CDB *)raw_cdb;

    struct INQUIRY_Data *ret_data = (struct INQUIRY_Data *)self->priv->buffer;
    self->priv->buffer_size = sizeof(struct INQUIRY_Data);

    if (cdb->evpd || cdb->page_code) {
        /* We don't support either; so as stated in SPC, return CHECK CONDITION,
           ILLEGAL REQUEST and INVALID FIELD IN CDB */
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: invalid field in CDB\n", __debug__);
        cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }

    /* Values here are more or less what my DVD-ROM drive gives me
       (and in accord with INF8090) */
    ret_data->per_dev = 0x05; /* CD-ROM device */
    ret_data->rmb = 1; /* Removable medium */
    ret_data->version = 0x0; /* Should be 0 according to INF8090 */
    ret_data->atapi_version = 3; /* Should be 3 according to INF8090 */
    ret_data->response_fmt = 0x02; /* Should be 2 according to INF8090 */
    ret_data->length = sizeof(struct INQUIRY_Data) - 5;
    strncpy(ret_data->vendor_id, self->priv->id_vendor_id, 8);
    strncpy(ret_data->product_id, self->priv->id_product_id, 16);
    strncpy(ret_data->product_rev, self->priv->id_revision, 4);
    strncpy(ret_data->vendor_spec1, self->priv->id_vendor_specific, 20);

    ret_data->ver_desc1 = GUINT16_TO_BE(0x02A0); /* We'll try to pass as MMC-3 device */

    /* Write data */
    cdemud_device_write_buffer(self, GUINT16_FROM_BE(cdb->length));

    return TRUE;
}

/* MODE SELECT*/
static gboolean command_mode_select (CDEMUD_Device *self, guint8 *raw_cdb)
{
    gint transfer_len = 0;
    /*gint sp;
    gint pf;*/

    /* MODE SELECT (6) vs MODE SELECT (10) */
    if (raw_cdb[0] == PC_MODE_SELECT_6) {
        struct MODE_SELECT_6_CDB *cdb = (struct MODE_SELECT_6_CDB *)raw_cdb;
        /*sp = cdb->sp;
        pf = cdb->pf;*/
        transfer_len = cdb->length;
    } else if (raw_cdb[0] == PC_MODE_SELECT_10) {
        struct MODE_SELECT_10_CDB *cdb = (struct MODE_SELECT_10_CDB *)raw_cdb;
        /*sp = cdb->sp;
        pf = cdb->pf;*/
        transfer_len = GUINT16_FROM_BE(cdb->length);
    } else {
        /* Because bad things happen to good people... :/ */
        CDEMUD_DEBUG(self, DAEMON_DEBUG_ERROR, "%s: someone called this function when they shouldn't have :/...\n", __debug__);
        cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }

    /* Read the parameter list */
    cdemud_device_read_buffer(self, transfer_len);

    /*if (CDEMUD_DEBUG_ON(self, DAEMON_DEBUG_DEV_PC_DUMP)) {
        gint i;
        g_print(">>> MODE SELECT DATA <<<\n");
        for (i = 0; i < transfer_len; i++) {
            g_print("0x%02X ", self->priv->buffer[i]);
            if (i % 8 == 7) {
                g_print("\n");
            }
        }
        g_print("\n");
    }*/

    /* Try to decipher mode select data... MODE SENSE (6) vs MODE SENSE (10) */
    gint blkdesc_len = 0;
    gint offset = 0;
    if (raw_cdb[0] == PC_MODE_SELECT_6) {
        struct MODE_SENSE_6_Header *header = (struct MODE_SENSE_6_Header *)self->priv->buffer;
        blkdesc_len = header->blkdesc_len;
        offset = sizeof(struct MODE_SENSE_6_Header) + blkdesc_len;
    } else if (raw_cdb[0] == PC_MODE_SELECT_10) {
        struct MODE_SENSE_10_Header *header = (struct MODE_SENSE_10_Header *)self->priv->buffer;
        blkdesc_len = GUINT16_FROM_BE(header->blkdesc_len);
        offset = sizeof(struct MODE_SENSE_10_Header) + blkdesc_len;
    }

    /* Someday when I'm in good mood I might implement this */
    if (blkdesc_len) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: block descriptor provided... but ATAPI devices shouldn't support that\n", __debug__);
        cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_PARAMETER_LIST);
        return FALSE;
    }

    /* Take a peek at the byte following block descriptor data */
    gint page_size = transfer_len - offset;

    if (page_size) {
        struct ModePage_GENERAL *mode_page_new  = (struct ModePage_GENERAL *)(self->priv->buffer+offset);
        struct ModePage_GENERAL *mode_page_mask = NULL;
        struct ModePage_GENERAL *mode_page_cur  = NULL;

        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: mode page 0x%X\n", __debug__, mode_page_new->code);

        /* Get pointer to current data */
        mode_page_cur = cdemud_device_get_mode_page(self, mode_page_new->code, MODE_PAGE_CURRENT);
        if (!mode_page_cur) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: we don't have mode page 0x%X\n", __debug__, mode_page_new->code);
            cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_PARAMETER_LIST);
            return FALSE;
        }

        /* Some length checking */
        if (page_size - 2 != mode_page_cur->length) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: declared page size doesn't match length of data we were given!\n", __debug__);
            cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_PARAMETER_LIST);
            return FALSE;
        }

        /* Some more length checking */
        if (mode_page_new->length != mode_page_cur->length) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: invalid page size!\n", __debug__);
            cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_PARAMETER_LIST);
            return FALSE;
        }

        /* Now we need to check if only values that can be changed are set */
        mode_page_mask = cdemud_device_get_mode_page(self, mode_page_new->code, MODE_PAGE_MASK);
        guint8 *raw_data_new  = ((guint8 *)mode_page_new) + 2;
        guint8 *raw_data_mask = ((guint8 *)mode_page_mask) + 2;
        gint i;

        for (i = 1; i < mode_page_new->length; i++) {
            /* Compare every byte against the mask (except first byte) */
            if (raw_data_new[i] & ~raw_data_mask[i]) {
                CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: invalid value set on byte %i!\n", __debug__, i);
                cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_PARAMETER_LIST);
                return FALSE;
            }
        }

        /* And finally, copy the page */
        memcpy(mode_page_cur, mode_page_new, mode_page_new->length + 2);
    }

    return TRUE;
}

/* MODE SENSE*/
static gboolean command_mode_sense (CDEMUD_Device *self, guint8 *raw_cdb)
{
    gint page_code = 0;
    gint transfer_len = 0;
    gint pc = 0;

    /* MODE SENSE (6) vs MODE SENSE (10) */
    if (raw_cdb[0] == PC_MODE_SENSE_6) {
        struct MODE_SENSE_6_CDB *cdb = (struct MODE_SENSE_6_CDB *)raw_cdb;

        pc = cdb->pc;
        page_code = cdb->page_code;
        transfer_len = cdb->length;

        self->priv->buffer_size = sizeof(struct MODE_SENSE_6_Header);
    } else if (raw_cdb[0] == PC_MODE_SENSE_10) {
        struct MODE_SENSE_10_CDB *cdb = (struct MODE_SENSE_10_CDB *)raw_cdb;

        pc = cdb->pc;
        page_code = cdb->page_code;
        transfer_len = GUINT16_FROM_BE(cdb->length);

        self->priv->buffer_size = sizeof(struct MODE_SENSE_10_Header);
    } else {
        /* Because bad things happen to good people... :/ */
        CDEMUD_DEBUG(self, DAEMON_DEBUG_ERROR, "%s: someone called this function when they shouldn't have :/...\n", __debug__);
        cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }
    guint8 *ret_data = self->priv->buffer+self->priv->buffer_size;

    /* We don't support saving mode pages */
    if (pc == 0x03) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: requested saved values; we don't support saving!\n", __debug__);
        cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, SAVING_PARAMETERS_NOT_SUPPORTED);
        return FALSE;
    }

    /* Go over *all* pages, and if we want all pages, copy 'em all, otherwise
       copy just the one we've got request for and break the loop */
    GList *entry = NULL;
    gboolean page_found = FALSE;
    G_LIST_FOR_EACH(entry, self->priv->mode_pages_list) {
        struct ModePage_GENERAL *mode_page = g_value_get_pointer(g_value_array_get_nth((GValueArray *)entry->data, 0));

        /* Check if we want this page copied */
        if (page_code == 0x3F || (page_code == mode_page->code)) {
            switch (pc) {
                case 0x00: {
                    /* Current values */
                    mode_page = g_value_get_pointer(g_value_array_get_nth((GValueArray *)entry->data, MODE_PAGE_CURRENT));
                    break;
                }
                case 0x01: {
                    /* Changeable values */
                    mode_page = g_value_get_pointer(g_value_array_get_nth((GValueArray *)entry->data, MODE_PAGE_MASK));
                    break;
                }
                case 0x02: {
                    /* Default value */
                    mode_page = g_value_get_pointer(g_value_array_get_nth((GValueArray *)entry->data, MODE_PAGE_DEFAULT));
                    break;
                }
                default: {
                    CDEMUD_DEBUG(self, DAEMON_DEBUG_ERROR, "%s: PC value is 0x%X and it shouldn't be!\n", __debug__, pc);
                    break;
                }
            }

            memcpy(ret_data, mode_page, mode_page->length + 2);
            self->priv->buffer_size += mode_page->length + 2;
            ret_data += mode_page->length + 2;

            if (page_code != 0x3F) {
                CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: got the page we wanted (0x%X), breaking the loop\n", __debug__, page_code);
                page_found = TRUE;
                break;
            }
        }
    }

    /* If we aren't returning all pages, check if page was found */
    if (page_code != 0x3F && !page_found) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: page 0x%X not found!\n", __debug__, page_code);
        cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }

    /* Header; MODE SENSE (6) vs MODE SENSE (10) */
    if (raw_cdb[0] == PC_MODE_SENSE_6) {
        struct MODE_SENSE_6_Header *ret_header = (struct MODE_SENSE_6_Header *)self->priv->buffer;
        ret_header->length = self->priv->buffer_size - 2;
    } else if (raw_cdb[0] == PC_MODE_SENSE_10) {
        struct MODE_SENSE_10_Header *ret_header = (struct MODE_SENSE_10_Header *)self->priv->buffer;
        ret_header->length = GUINT16_TO_BE(self->priv->buffer_size - 2);
    }

    /* Write data */
    cdemud_device_write_buffer(self, transfer_len);

    return TRUE;
}

/* PAUSE/RESUME*/
static gboolean command_pause_resume (CDEMUD_Device *self, guint8 *raw_cdb)
{
    struct PAUSE_RESUME_CDB *cdb = (struct PAUSE_RESUME_CDB *)raw_cdb;
    gint audio_status = 0;

    cdemud_audio_get_status(CDEMUD_AUDIO(self->priv->audio_play), &audio_status, NULL);

    /* Resume */
    if (cdb->resume == 1) {
        /* MMC-3 says that if we request resume and operation can't be resumed,
           we return error (if we're already playing, it doesn't count as an
           error) */
        if ((audio_status != AUDIO_STATUS_PAUSED)
            && (audio_status != AUDIO_STATUS_PLAYING)) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: resume requested while in invalid state!\n", __debug__);
            cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, COMMAND_SEQUENCE_ERROR);
            return FALSE;
        }

        /* Resume; we set status to playing, and fire the callback */
        if (audio_status != AUDIO_STATUS_PLAYING) {
            cdemud_audio_resume(CDEMUD_AUDIO(self->priv->audio_play), NULL);
        }
    }


    if (cdb->resume == 0) {
        /* MMC-3 also says that we return error if pause is requested and the
           operation can't be paused (being already paused doesn't count) */
        if ((audio_status != AUDIO_STATUS_PAUSED)
            && (audio_status != AUDIO_STATUS_PLAYING)) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: pause requested while in invalid state!\n", __debug__);
            cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, COMMAND_SEQUENCE_ERROR);
            return FALSE;
        }

       /* Pause; stop the playback and force status to AUDIO_STATUS_PAUSED */
        if (audio_status != AUDIO_STATUS_PAUSED) {
            cdemud_audio_pause(CDEMUD_AUDIO(self->priv->audio_play), NULL);
        }
    }

    return TRUE;
}

/* PLAY AUDIO*/
static gboolean command_play_audio (CDEMUD_Device *self, guint8 *raw_cdb)
{
    guint32 start_sector = 0;
    guint32 end_sector = 0;

    /* PLAY AUDIO (10) vs PLAY AUDIO (12) vs PLAY AUDIO MSF */
    if (raw_cdb[0] == PC_PLAY_AUDIO_10) {
        struct PLAY_AUDIO_10_CDB *cdb = (struct PLAY_AUDIO_10_CDB *)raw_cdb;

        start_sector = GUINT32_FROM_BE(cdb->lba);
        end_sector = GUINT32_FROM_BE(cdb->lba) + GUINT16_FROM_BE(cdb->play_len);
    } else if (raw_cdb[0] == PC_PLAY_AUDIO_12) {
        struct PLAY_AUDIO_12_CDB *cdb = (struct PLAY_AUDIO_12_CDB *)raw_cdb;

        start_sector = GUINT32_FROM_BE(cdb->lba);
        end_sector = GUINT32_FROM_BE(cdb->lba) + GUINT32_FROM_BE(cdb->play_len);
    } else if (raw_cdb[0] == PC_PLAY_AUDIO_MSF) {
        struct PLAY_AUDIO_MSF_CDB *cdb = (struct PLAY_AUDIO_MSF_CDB *)raw_cdb;

        start_sector = mirage_helper_msf2lba(cdb->start_m, cdb->start_s, cdb->start_f, TRUE);
        end_sector = mirage_helper_msf2lba(cdb->end_m, cdb->end_s, cdb->end_f, TRUE);
    } else {
        /* Because bad things happen to good people... :/ */
        CDEMUD_DEBUG(self, DAEMON_DEBUG_ERROR, "%s: someone called this function when they shouldn't have :/...\n", __debug__);
        cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }

     /* Check if we have medium loaded */
    if (!self->priv->loaded) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: medium not present\n", __debug__);
        cdemud_device_write_sense(self, SK_NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }

    CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: playing from sector 0x%X to sector 0x%X\n", __debug__, start_sector, end_sector);

    /* Play */
    if (!cdemud_audio_start(CDEMUD_AUDIO(self->priv->audio_play), start_sector, end_sector, self->priv->disc, NULL)) {
        /* FIXME: write sense */
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: failed to start audio play!\n", __debug__);
        return FALSE;
    }

    return TRUE;
}

/* PREVENT/ALLOW MEDIUM REMOVAL*/
static gboolean command_prevent_allow_medium_removal (CDEMUD_Device *self, guint8 *raw_cdb)
{
    struct PREVENT_ALLOW_MEDIUM_REMOVAL_CDB *cdb = (struct PREVENT_ALLOW_MEDIUM_REMOVAL_CDB*)raw_cdb;
    struct ModePage_0x2A *p_0x2A = cdemud_device_get_mode_page(self, 0x2A, MODE_PAGE_CURRENT);

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
static gboolean command_read (CDEMUD_Device *self, guint8 *raw_cdb)
{
    gint start_sector; /* MUST be signed because it may be negative! */
    gint num_sectors;

    struct ModePage_0x01 *p_0x01 = cdemud_device_get_mode_page(self, 0x01, MODE_PAGE_CURRENT);

    /* READ 10 vs READ 12 */
    if (raw_cdb[0] == PC_READ_10) {
        struct READ_10_CDB *cdb = (struct READ_10_CDB *)raw_cdb;
        start_sector = GUINT32_FROM_BE(cdb->lba);
        num_sectors  = GUINT16_FROM_BE(cdb->length);
    } else if (raw_cdb[0] == PC_READ_12) {
        struct READ_12_CDB *cdb = (struct READ_12_CDB *)raw_cdb;
        start_sector = GUINT32_FROM_BE(cdb->lba);
        num_sectors  = GUINT32_FROM_BE(cdb->length);
    } else {
        /* Because bad things happen to good people... :/ */
        CDEMUD_DEBUG(self, DAEMON_DEBUG_ERROR, "%s: someone called this function when they shouldn't have :/...\n", __debug__);
        cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }

    CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: read request; start sector: 0x%X, number of sectors: %d\n", __debug__, start_sector, num_sectors);

    /* Check if we have medium loaded (because we use track later... >.<) */
    if (!self->priv->loaded) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: medium not present\n", __debug__);
        cdemud_device_write_sense(self, SK_NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }
    MIRAGE_Disc *disc = MIRAGE_DISC(self->priv->disc);

    gint sector;

    /* Set up delay emulation */
    cdemud_device_delay_begin(self, start_sector, num_sectors);

    /* Process each sector */
    for (sector = start_sector; sector < start_sector + num_sectors; sector++) {
        GObject *cur_sector = NULL;
        mirage_disc_get_sector(disc, sector, &cur_sector, NULL);
        if (!cur_sector) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: invalid sector!\n", __debug__);
            cdemud_device_write_sense_full(self, SK_ILLEGAL_REQUEST, ILLEGAL_MODE_FOR_THIS_TRACK, 0, sector);
            return FALSE;
        }

        cdemud_device_flush_buffer(self);

        /* Here we do the emulation of "bad sectors"... if we're dealing with
           a bad sector, then its EDC/ECC won't correspond to actual data. So
           we verify sector's EDC and in case DCR (Disable Corrections) bit in
           Mode Page 1 is not enabled, we report the read error. However, my
           tests indicate this should be done only for Mode 1 or Mode 2 Form 1
           sectors */
        if (!p_0x01->dcr) {
            gint sector_type;
            mirage_sector_get_sector_type(MIRAGE_SECTOR(cur_sector), &sector_type, NULL);

            if ((sector_type == MIRAGE_MODE_MODE1 || sector_type == MIRAGE_MODE_MODE2_FORM1)
                && !mirage_sector_verify_lec(MIRAGE_SECTOR(cur_sector))) {
                CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: bad sector detected, triggering read error!\n", __debug__);
                g_object_unref(cur_sector);
                cdemud_device_write_sense_full(self, SK_MEDIUM_ERROR, UNRECOVERED_READ_ERROR, 0, sector);
                return FALSE;
            }
        }

        /* READ 10/12 should support only sectors with 2048-byte user data */
        gint tmp_len = 0;
        const guint8 *tmp_buf = NULL;
        guint8 *cache_ptr = self->priv->buffer+self->priv->buffer_size;

        mirage_sector_get_data(MIRAGE_SECTOR(cur_sector), &tmp_buf, &tmp_len, NULL);
        if (tmp_len != 2048) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: sector 0x%X does not have 2048-byte user data (%i)\n", __debug__, sector, tmp_len);
            g_object_unref(cur_sector);
            cdemud_device_write_sense_full(self, SK_ILLEGAL_REQUEST, ILLEGAL_MODE_FOR_THIS_TRACK, 1, sector);
            return FALSE;
        }

        memcpy(cache_ptr, tmp_buf, tmp_len);
        self->priv->buffer_size += tmp_len;

        /* Needed for some other commands */
        self->priv->current_sector = sector;
        /* Free sector */
        g_object_unref(cur_sector);
        /* Write sector */
        cdemud_device_write_buffer(self, self->priv->buffer_size);
    }

    /* Perform delay emulation */
    cdemud_device_delay_finalize(self);

    return TRUE;
}

/* READ CAPACITY*/
static gboolean command_read_capacity (CDEMUD_Device *self, guint8 *raw_cdb G_GNUC_UNUSED)
{
    /*struct READ_CAPACITY_CDB *cdb = (struct READ_CAPACITY_CDB *)raw_cdb;*/
    struct READ_CAPACITY_Data *ret_data = (struct READ_CAPACITY_Data *)self->priv->buffer;
    self->priv->buffer_size = sizeof(struct READ_CAPACITY_Data);

    gint last_sector = 0;

    if (!self->priv->loaded) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: medium not present\n", __debug__);
        cdemud_device_write_sense(self, SK_NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }

    /* Capacity: starting sector of last leadout - 1 */
    GObject *lsession = NULL;
    GObject *leadout  = NULL;

    mirage_disc_get_session_by_index(MIRAGE_DISC(self->priv->disc), -1, &lsession, NULL);
    mirage_session_get_track_by_number(MIRAGE_SESSION(lsession), MIRAGE_TRACK_LEADOUT, &leadout, NULL);
    mirage_track_layout_get_start_sector(MIRAGE_TRACK(leadout), &last_sector, NULL);

    g_object_unref(leadout);
    g_object_unref(lsession);

    last_sector -= 1;

    ret_data->lba = GUINT32_TO_BE(last_sector);
    ret_data->block_size = GUINT32_TO_BE(2048);

    /* Write data */
    cdemud_device_write_buffer(self, self->priv->buffer_size);

    return TRUE;
}

/* READ CD and READ CD MSF*/
static gboolean command_read_cd (CDEMUD_Device *self, guint8 *raw_cdb)
{
    gint start_sector; /* MUST be signed because it may be negative! */
    gint num_sectors;
    gint exp_sect_type;
    gint subchan_mode;

    struct ModePage_0x01 *p_0x01 = cdemud_device_get_mode_page(self, 0x01, MODE_PAGE_CURRENT);

    /* READ CD vs READ CD MSF */
    if (raw_cdb[0] == PC_READ_CD) {
        struct READ_CD_CDB *cdb = (struct READ_CD_CDB *)raw_cdb;

        start_sector = GUINT32_FROM_BE(cdb->lba);
        num_sectors = GUINT24_FROM_BE(cdb->length);

        exp_sect_type = map_expected_sector_type(cdb->sect_type);
        subchan_mode = cdb->subchan;
    } else if (raw_cdb[0] == PC_READ_CD_MSF) {
        struct READ_CD_MSF_CDB *cdb = (struct READ_CD_MSF_CDB *)raw_cdb;
        gint32 end_sector = 0;

        start_sector = mirage_helper_msf2lba(cdb->start_m, cdb->start_s, cdb->start_f, TRUE);
        end_sector = mirage_helper_msf2lba(cdb->end_m, cdb->end_s, cdb->end_f, TRUE);
        num_sectors = end_sector - start_sector;

        exp_sect_type = map_expected_sector_type(cdb->sect_type);
        subchan_mode = cdb->subchan;
    } else {
        /* Because bad things happen to good people... :/ */
        CDEMUD_DEBUG(self, DAEMON_DEBUG_ERROR, "%s: someone called this function when they shouldn't have :/...\n", __debug__);
        cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }

    CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: READ CD:\n-> Address: 0x%08X\n-> Length: %i\n-> Expected sector (in libMirage type): 0x%X\n-> MCSB: 0x%X\n-> SubChannel: 0x%X\n",
        __debug__, start_sector, num_sectors, exp_sect_type, raw_cdb[9], subchan_mode);


    /* Check if we have medium loaded */
    if (!self->priv->loaded) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: medium not present\n", __debug__);
        cdemud_device_write_sense(self, SK_NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }

    /* Not supported for DVD-ROMs, right? */
    if (self->priv->current_profile == PROFILE_DVDROM) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: READ CD not supported on DVD Media!\n", __debug__);
        cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }

    /* Verify the requested subchannel mode ('readcd:18' uses READ CD with transfer
       length 0x00 to determine which subchannel modes are supported; without this
       clause, call with R-W subchannel passes, causing app choke on it later (when
       there's transfer length > 0x00 and thus subchannel is verified */
    if (subchan_mode == 0x04) {
        /* invalid subchannel requested (don't support R-W yet) */
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: R-W subchannel reading not supported yet\n", __debug__);
        cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }


    MIRAGE_Disc* disc = MIRAGE_DISC(self->priv->disc);
    GObject* first_sector = NULL;
    gint prev_sector_type = 0;

    gint sector = start_sector;

    /* Read first sector to determine its type */
    mirage_disc_get_sector(disc, start_sector, &first_sector, NULL);
    if (!first_sector) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: invalid starting sector!\n", __debug__);
        cdemud_device_write_sense_full(self, SK_ILLEGAL_REQUEST, ILLEGAL_MODE_FOR_THIS_TRACK, 0, sector);
        return FALSE;
    }
    mirage_sector_get_sector_type(MIRAGE_SECTOR(first_sector), &prev_sector_type, NULL);
    g_object_unref(first_sector);

    /* Set up delay emulation */
    cdemud_device_delay_begin(self, start_sector, num_sectors);

    /* Process each sector */
    CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: start sector: 0x%X (%i); start + num: 0x%X (%i)\n", __debug__, start_sector, start_sector, start_sector+num_sectors, start_sector+num_sectors);
    for (sector = start_sector; sector < start_sector + num_sectors; sector++) {
        GObject *cur_sector = NULL;

        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: reading sector 0x%X (%i)\n", __debug__, sector, sector);

        mirage_disc_get_sector(disc, sector, &cur_sector, NULL);
        if (!cur_sector) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: invalid sector!\n", __debug__);
            cdemud_device_write_sense_full(self, SK_ILLEGAL_REQUEST, ILLEGAL_MODE_FOR_THIS_TRACK, 0, sector);
            return FALSE;
        }

        cdemud_device_flush_buffer(self);

        /* Expected sector stuff check... basically, if we have CDB->ExpectedSectorType
           set, we compare its translated value with our sector type, period. However, if
           it's 0, then "The Logical Unit shall always terminate a command at the sector
           where a transition between CD-ROM and CD-DA data occurs." */
        gint cur_sector_type = 0;
        mirage_sector_get_sector_type(MIRAGE_SECTOR(cur_sector), &cur_sector_type, NULL);

        /* Break if current sector type doesn't match expected one*/
        if (exp_sect_type && (cur_sector_type != exp_sect_type)) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: expected sector type mismatch (expecting %i, got %i)!\n", __debug__, exp_sect_type, cur_sector_type);
            g_object_unref(cur_sector);
            cdemud_device_write_sense_full(self, SK_ILLEGAL_REQUEST, ILLEGAL_MODE_FOR_THIS_TRACK, 1, sector);
            return FALSE;
        }

#if 0
        /* Break if mode (sector type) has changed */
        /* NOTE: if we're going to be doing this, we need to account for the
           fact that Mode 2 Form 1 and Mode 2 Form 2 can alternate... */
        if (prev_sector_type != cur_sector_type) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: previous sector type (%i) different from current one (%i)!\n", __debug__, prev_sector_type, cur_sector_type);
            g_object_unref(cur_sector);
            cdemud_device_write_sense_full(self, SK_ILLEGAL_REQUEST, ILLEGAL_MODE_FOR_THIS_TRACK, 0, sector);
            return FALSE;
        }
#endif

        /* Here we do the emulation of "bad sectors"... if we're dealing with
           a bad sector, then its EDC/ECC won't correspond to actual data. So
           we verify sector's EDC and in case DCR (Disable Corrections) bit in
           Mode Page 1 is not enabled, we report the read error. However, my
           tests indicate this should be done only for Mode 1 or Mode 2 Form 1
           sectors */
        if (!p_0x01->dcr) {
            if ((cur_sector_type == MIRAGE_MODE_MODE1 || cur_sector_type == MIRAGE_MODE_MODE2_FORM1)
                && !mirage_sector_verify_lec(MIRAGE_SECTOR(cur_sector))) {
                CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: bad sector detected, triggering read error!\n", __debug__);
                g_object_unref(cur_sector);
                cdemud_device_write_sense_full(self, SK_MEDIUM_ERROR, UNRECOVERED_READ_ERROR, 0, sector);
                return FALSE;
            }
        }

        /* Map the MCSB: operation performed on raw Byte9 */
        if (map_mcsb(&raw_cdb[9], cur_sector_type) == -1) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: invalid MCSB: %X\n", __debug__, raw_cdb[9]);
            g_object_unref(cur_sector);
            cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
            return FALSE;
        }

        /* We read data here... we use reading method provided by libmirage, instead
           of copying all sector's data ourselves... yeah, it's a waste of resources
           to have sector created twice this way, but I hate to duplicate code...
           FIXME: find a better way to do it? */
        guint8 *ptr = self->priv->buffer+self->priv->buffer_size;
        gint read_length = 0;

        GError *read_err = NULL;
        if (!mirage_disc_read_sector(disc, sector, raw_cdb[9], subchan_mode, ptr, &read_length, &read_err)) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: failed to read sector 0x%X: %s\n", __debug__, sector, read_err->message);
            g_object_unref(cur_sector);
            cdemud_device_write_sense_full(self, SK_ILLEGAL_REQUEST, ILLEGAL_MODE_FOR_THIS_TRACK, 0, sector);
            return FALSE;
        }

        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: read length: 0x%X, buffer size: 0x%X\n", __debug__, read_length, self->priv->buffer_size);
        self->priv->buffer_size += read_length;

        /* Previous sector type */
        prev_sector_type = cur_sector_type;
        /* Needed for some other commands */
        self->priv->current_sector = sector;
        /* Free sector */
        g_object_unref(cur_sector);
        /* Write sector */
        cdemud_device_write_buffer(self, self->priv->buffer_size);
    }

    /* Perform delay emulation */
    cdemud_device_delay_finalize(self);

    return TRUE;
}

/* READ DISC INFORMATION*/
static gboolean command_read_disc_information (CDEMUD_Device *self, guint8 *raw_cdb)
{
    struct READ_DISC_INFORMATION_CDB *cdb = (struct READ_DISC_INFORMATION_CDB *)raw_cdb;

    /* Check if we have medium loaded */
    if (!self->priv->loaded) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: medium not present\n", __debug__);
        cdemud_device_write_sense(self, SK_NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }

    switch (cdb->type) {
        case 0x000: {
            struct READ_DISC_INFORMATION_Data *ret_data = (struct READ_DISC_INFORMATION_Data *)self->priv->buffer;
            self->priv->buffer_size = sizeof(struct READ_DISC_INFORMATION_Data);

            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: standard disc information\n", __debug__);

            ret_data->length = GUINT16_TO_BE(self->priv->buffer_size - 2);
            ret_data->lsession_state = 0x03; /* complete */
            ret_data->disc_status = 0x02; /* complete */
            ret_data->ftrack_disc = 0x01;

            MIRAGE_Disc *disc = MIRAGE_DISC(self->priv->disc);
            gint num_sessions = 0;
            mirage_disc_get_number_of_sessions(disc, &num_sessions, NULL);
            ret_data->sessions0 = (num_sessions & 0xFF00) >> 8;
            ret_data->sessions1 = num_sessions & 0xFF;

            /* First track in last session */
            GObject *lsession = NULL;
            GObject *ftrack = NULL;
            gint ftrack_lsession = 0;

            mirage_disc_get_session_by_index(disc, -1, &lsession, NULL);
            mirage_session_get_track_by_index(MIRAGE_SESSION(lsession), 0, &ftrack, NULL);
            mirage_track_layout_get_track_number(MIRAGE_TRACK(ftrack), &ftrack_lsession, NULL);
            g_object_unref(ftrack);

            ret_data->ftrack_lsession0 = (ftrack_lsession & 0xFF00) >> 8;
            ret_data->ftrack_lsession1 = ftrack_lsession & 0xFF;

            /* Last track in last session */
            GObject *ltrack = NULL;
            GObject *leadin = NULL;
            gint ltrack_lsession = 0;
            gint lsession_leadin = 0;

            mirage_session_get_track_by_index(MIRAGE_SESSION(lsession), -1, &ltrack, NULL);
            mirage_track_layout_get_track_number(MIRAGE_TRACK(ltrack), &ltrack_lsession, NULL);
            g_object_unref(ltrack);

            mirage_session_get_track_by_number(MIRAGE_SESSION(lsession), MIRAGE_TRACK_LEADIN, &leadin, NULL);
            mirage_track_layout_get_start_sector(MIRAGE_TRACK(leadin), &lsession_leadin, NULL);
            g_object_unref(leadin);

            g_object_unref(lsession);

            ret_data->ltrack_lsession0 = (ltrack_lsession & 0xFF00) >> 8;
            ret_data->ltrack_lsession1 = ltrack_lsession & 0xFF;


            /* Disc type; determined from first session, as per INF8090 */
            gint disc_type = 0;
            GObject *fsession = NULL;
            mirage_disc_get_session_by_index(disc, 0, &fsession, NULL);
            mirage_session_get_session_type(MIRAGE_SESSION(fsession), &disc_type, NULL);
            g_object_unref(fsession);

            ret_data->disc_type = disc_type;

            /* Last session lead-in address (MSF) */
            guint8 *msf_ptr = (guint8 *)&ret_data->lsession_leadin;
            mirage_helper_lba2msf(lsession_leadin, TRUE, &msf_ptr[1], &msf_ptr[2], &msf_ptr[3]);

            ret_data->last_leadout = 0xFFFFFFFF; /* Not applicable since we're not a writer */

            break;
        }
        default: {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: data type 0x%X not supported!\n", __debug__, cdb->type);
            cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
            return FALSE;
        }
    }

    /* Write data */
    cdemud_device_write_buffer(self, self->priv->buffer_size);

    return TRUE;
}

/* READ DVD STRUCTURE*/
static gboolean command_read_dvd_structure (CDEMUD_Device *self, guint8 *raw_cdb)
{
    struct READ_DVD_STRUCTURE_CDB *cdb = (struct READ_DVD_STRUCTURE_CDB *)raw_cdb;
    struct READ_DVD_STRUCTURE_Header *head = (struct READ_DVD_STRUCTURE_Header *)self->priv->buffer;
    self->priv->buffer_size = sizeof(struct READ_DVD_STRUCTURE_Header);

    /* Check if we have medium loaded */
    if (!self->priv->loaded) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: medium not present\n", __debug__);
        cdemud_device_write_sense(self, SK_NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }

    if (self->priv->current_profile != PROFILE_DVDROM) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: READ DVD STRUCTURE is supported only with DVD media\n", __debug__);
        cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, CANNOT_READ_MEDIUM_INCOMPATIBLE_FORMAT);
        return FALSE;
    }

    /* Try to get the structure */
    const guint8 *tmp_data = NULL;
    gint tmp_len = 0;

    CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: requested structure: 0x%X; layer: %d\n", __debug__, cdb->format, cdb->layer);
    if (!mirage_disc_get_disc_structure(MIRAGE_DISC(self->priv->disc), cdb->layer, cdb->format, &tmp_data, &tmp_len, NULL)) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: structure not present on disc!\n", __debug__);
        cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }

    memcpy(self->priv->buffer+sizeof(struct READ_DVD_STRUCTURE_Header), tmp_data, tmp_len);
    self->priv->buffer_size += tmp_len;

    /* Header */
    head->length = GUINT16_TO_BE(self->priv->buffer_size - 2);

    /* Write data */
    cdemud_device_write_buffer(self, GUINT16_FROM_BE(cdb->length));

    return TRUE;
}

/* READ SUB-CHANNEL*/
static gboolean command_read_subchannel (CDEMUD_Device *self, guint8 *raw_cdb)
{
    struct READ_SUBCHANNEL_CDB *cdb = (struct READ_SUBCHANNEL_CDB *)raw_cdb;
    struct READ_SUBCHANNEL_Header *ret_header = (struct READ_SUBCHANNEL_Header *)self->priv->buffer;
    self->priv->buffer_size = sizeof(struct READ_SUBCHANNEL_Header);

    MIRAGE_Disc *disc = MIRAGE_DISC(self->priv->disc);

    /* Check if we have medium loaded */
    if (!self->priv->loaded) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: medium not present\n", __debug__);
        cdemud_device_write_sense(self, SK_NOT_READY, MEDIUM_NOT_PRESENT);
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

                gint current_position = self->priv->current_sector;

                CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: current position (sector 0x%X)\n", __debug__, current_position);
                ret_data->fmt_code = 0x01;

                /* Read current sector's PQ subchannel */
                guint8 tmp_buf[16];
                if (!mirage_disc_read_sector(MIRAGE_DISC(self->priv->disc), current_position, 0x00, MIRAGE_SUBCHANNEL_PQ, tmp_buf, NULL, NULL)) {
                    CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to read subchannel of sector 0x%X!\n", __debug__, current_position);
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
                gint correction = 1;
                while ((tmp_buf[0] & 0x0F) != 0x01) {
                    CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: got a sector that's not Mode-1 Q; taking next one (0x%X)!\n", __debug__, current_position+correction);

                    /* Read from next sector */
                    if (!mirage_disc_read_sector(MIRAGE_DISC(self->priv->disc), current_position+correction, 0x00, MIRAGE_SUBCHANNEL_PQ, tmp_buf, NULL, NULL)) {
                        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: failed to read subchannel of sector 0x%X!\n", __debug__, current_position+correction);
                        break;
                    }

                    correction++;
                }

                /* In Q-subchannel, first MSF is relative, second absolute... in
                   data we return, it's the other way around */
                gint relative_address = mirage_helper_msf2lba(mirage_helper_bcd2hex(tmp_buf[3]), mirage_helper_bcd2hex(tmp_buf[4]), mirage_helper_bcd2hex(tmp_buf[5]), FALSE) - correction;
                gint absolute_address = mirage_helper_msf2lba(mirage_helper_bcd2hex(tmp_buf[7]), mirage_helper_bcd2hex(tmp_buf[8]), mirage_helper_bcd2hex(tmp_buf[9]), TRUE) - correction;

                /* MSF vs. LBA */
                if (cdb->time) {
                    guint8 *msf_ptr = (guint8 *)&ret_data->abs_addr;

                    mirage_helper_lba2msf(absolute_address, TRUE, &msf_ptr[1], &msf_ptr[2], &msf_ptr[3]);

                    msf_ptr = (guint8 *)&ret_data->rel_addr;
                    mirage_helper_lba2msf(relative_address, FALSE, &msf_ptr[1], &msf_ptr[2], &msf_ptr[3]);
                } else {
                    ret_data->abs_addr = GUINT32_TO_BE(absolute_address);
                    ret_data->rel_addr = GUINT32_TO_BE(relative_address);
                }

                break;
            }
            case 0x02: {
                /* MCN */
                struct READ_SUBCHANNEL_Data2 *ret_data = (struct READ_SUBCHANNEL_Data2 *)(self->priv->buffer+self->priv->buffer_size);
                self->priv->buffer_size += sizeof(struct READ_SUBCHANNEL_Data2);

                CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: MCN/UPC/EAN\n", __debug__);
                ret_data->fmt_code = 0x02;

                /* Go over first 100 sectors; if MCN is present, it should be there */
                gint sector = 0;
                for (sector = 0; sector < 100; sector++) {
                    guint8 tmp_buf[16];

                    if (!mirage_disc_read_sector(MIRAGE_DISC(self->priv->disc), sector, 0, MIRAGE_SUBCHANNEL_PQ, tmp_buf, NULL, NULL)) {
                        CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to read subchannel of sector 0x%X!\n", __debug__, sector);
                        continue;
                    }

                    if ((tmp_buf[0] & 0x0F) == 0x02) {
                        /* Mode-2 Q found */
                        mirage_helper_subchannel_q_decode_mcn(&tmp_buf[1], (gchar *)ret_data->mcn);
                        ret_data->mcval = 1;
                        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: found MCN in subchannel of sector 0x%X: <%.13s>\n", __debug__, sector, ret_data->mcn);
                        break;
                    }
                }

                break;
            }
            case 0x03: {
                /* ISRC */
                struct READ_SUBCHANNEL_Data3 *ret_data = (struct READ_SUBCHANNEL_Data3 *)(self->priv->buffer+self->priv->buffer_size);
                self->priv->buffer_size += sizeof(struct READ_SUBCHANNEL_Data3);

                CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: ISRC\n", __debug__);
                ret_data->fmt_code = 0x03;

                GObject *track = NULL;
                if (!mirage_disc_get_track_by_number(disc, cdb->track, &track, NULL)) {
                    CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: failed to get track %i!\n", __debug__, cdb->track);
                    cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
                    return FALSE;
                }

                /* Go over first 100 sectors; if ISRC is present, it should be there */
                gint sector = 0;
                for (sector = 0; sector < 100; sector++) {
                    guint8 tmp_buf[16];

                    if (!mirage_track_read_sector(MIRAGE_TRACK(track), sector, FALSE, 0, MIRAGE_SUBCHANNEL_PQ, tmp_buf, NULL, NULL)) {
                        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: failed to read subchannel of sector 0x%X\n", __debug__, sector);
                        continue;
                    }

                    if ((tmp_buf[0] & 0x0F) == 0x03) {
                        /* Mode-3 Q found */
                        /* Copy ADR/CTL and track number */
                        ret_data->adr = tmp_buf[0] & 0x0F;
                        ret_data->ctl = (tmp_buf[0] & 0xF0) >> 4;
                        ret_data->track = tmp_buf[1];
                        /* Copy ISRC */
                        mirage_helper_subchannel_q_decode_isrc(&tmp_buf[1], (gchar *)ret_data->isrc);
                        ret_data->tcval = 1;
                        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: found ISRC in subchannel of sector 0x%X: <%.12s>\n", __debug__, sector, ret_data->isrc);
                        break;
                    }
                }

                g_object_unref(track);

                break;
            }
            default: {
                CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: unknown!\n", __debug__);
                break;
            }
        }
    }

    /* Header */
    gint audio_status = 0;
    cdemud_audio_get_status(CDEMUD_AUDIO(self->priv->audio_play), &audio_status, NULL);
    ret_header->audio_status = audio_status; /* Audio status */
    ret_header->length = GUINT32_TO_BE(self->priv->buffer_size - 4);

    /* Write data */
    cdemud_device_write_buffer(self, GUINT16_FROM_BE(cdb->length));

    return TRUE;
}

/* READ TOC/PMA/ATIP*/
static gboolean command_read_toc_pma_atip (CDEMUD_Device *self, guint8 *raw_cdb)
{
    struct READ_TOC_PMA_ATIP_CDB *cdb = (struct READ_TOC_PMA_ATIP_CDB *)raw_cdb;

    if (!self->priv->loaded) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: medium not present\n", __debug__);
        cdemud_device_write_sense(self, SK_NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }

    /* MMC: No fabrication for DVD media is defined for forms other than 000b and 001b. */
    if ((self->priv->current_profile == PROFILE_DVDROM) && !((cdb->format == 0x00) || (cdb->format == 0x01))) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: invalid format type (0x%X) for DVD-ROM image!\n", __debug__, cdb->format);
        cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }

    MIRAGE_Disc *disc = MIRAGE_DISC(self->priv->disc);

    /* Alcohol 120% was being a PITA claiming I was feeding it 'empty disc'...
       upon checking INF-8020, it turns out what MMC-3 specifies as control byte
       is actually used... so we do compatibility mapping here */
    if (cdb->format == 0) {
        if (cdb->control == 0x40) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: compliance to INF-8020 obviously expected... playing along\n", __debug__);
            cdb->format = 0x01;
        }
        if (cdb->control == 0x80) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: compliance to INF-8020 obviously expected... playing along\n", __debug__);
            cdb->format = 0x02;
        }
    }

    switch (cdb->format) {
        case 0x00: {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: formatted TOC\n", __debug__);
            /* Formatted TOC */
            struct READ_TOC_PMA_ATIP_0000_Header *ret_header = (struct READ_TOC_PMA_ATIP_0000_Header *)self->priv->buffer;
            self->priv->buffer_size = sizeof(struct READ_TOC_PMA_ATIP_0000_Header);
            struct READ_TOC_PMA_ATIP_0000_Descriptor *ret_desc = (struct READ_TOC_PMA_ATIP_0000_Descriptor *)(self->priv->buffer+self->priv->buffer_size);

            GObject *cur_track = NULL;

            /* "For multi-session discs, this command returns the TOC data for
               all sessions and for Track number AAh only the Lead-out area of
               the last complete session." (MMC-3) */

            /* All tracks but lead-out */
            if (cdb->number != 0xAA) {
                gint nr_tracks = 0;
                gint i;

                /* If track number exceeds last track number, return error */
                mirage_disc_get_track_by_index(disc, -1, &cur_track, NULL);
                mirage_track_layout_get_track_number(MIRAGE_TRACK(cur_track), &nr_tracks, NULL);
                g_object_unref(cur_track);
                if (cdb->number > nr_tracks) {
                    CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: starting track number (%i) exceeds last track number (%i)!\n", __debug__, cdb->number, nr_tracks);
                    cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
                    return FALSE;
                }


                mirage_disc_get_number_of_tracks(disc, &nr_tracks, NULL);

                for (i = 0; i < nr_tracks; i++) {
                    if (!mirage_disc_get_track_by_index(disc, i, &cur_track, NULL)) {
                        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: failed to get track with index %i (whole disc)!\n", __debug__, i);
                        break;
                    }

                    gint track_nr = 0;
                    mirage_track_layout_get_track_number(MIRAGE_TRACK(cur_track), &track_nr, NULL);

                    if (track_nr >= cdb->number) {
                        /* Track data */
                        gint adr, ctl;
                        gint start_sector = 0;
                        gint track_start = 0;

                        mirage_track_get_adr(MIRAGE_TRACK(cur_track), &adr, NULL);
                        mirage_track_get_ctl(MIRAGE_TRACK(cur_track), &ctl, NULL);

                        ret_desc->adr = adr;
                        ret_desc->ctl = ctl;
                        ret_desc->number = track_nr;

                        /* (H)MSF vs. LBA */
                        mirage_track_layout_get_start_sector(MIRAGE_TRACK(cur_track), &start_sector, NULL);
                        mirage_track_get_track_start(MIRAGE_TRACK(cur_track), &track_start, NULL);
                        start_sector += track_start;

                        if (cdb->time) {
                            guint8 *msf_ptr = (guint8 *)&ret_desc->lba;
                            mirage_helper_lba2msf(start_sector, TRUE, &msf_ptr[1], &msf_ptr[2], &msf_ptr[3]);
                        } else {
                            ret_desc->lba = GUINT32_TO_BE(start_sector);
                        }

                        self->priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_0000_Descriptor);
                        ret_desc++;    /* next descriptor */
                    }

                    g_object_unref(cur_track);
                }
            }

            /* Lead-Out (of the last session): */
            GObject *lsession = NULL;
            mirage_disc_get_session_by_index(disc, -1, &lsession, NULL);
            mirage_session_get_track_by_number(MIRAGE_SESSION(lsession), MIRAGE_TRACK_LEADOUT, &cur_track, NULL);

            ret_desc->adr = 0x01;
            ret_desc->ctl = 0x00;
            ret_desc->number = 0xAA;

            /* MSF vs. LBA */
            gint start_sector = 0;
            gint track_start = 0;
            mirage_track_layout_get_start_sector(MIRAGE_TRACK(cur_track), &start_sector, NULL);
            mirage_track_get_track_start(MIRAGE_TRACK(cur_track), &track_start, NULL);

            start_sector += track_start;

            if (cdb->time) {
                guint8 *msf_ptr = (guint8 *)&ret_desc->lba;
                mirage_helper_lba2msf(start_sector, TRUE, &msf_ptr[1], &msf_ptr[2], &msf_ptr[3]);
            } else {
                ret_desc->lba = GUINT32_TO_BE(start_sector);
            }
            self->priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_0000_Descriptor);

            g_object_unref(cur_track);

            /* Header */
            gint ltrack = 0;
            mirage_session_get_track_by_index(MIRAGE_SESSION(lsession), -1, &cur_track, NULL);
            mirage_track_layout_get_track_number(MIRAGE_TRACK(cur_track), &ltrack, NULL);

            g_object_unref(cur_track);
            g_object_unref(lsession);

            ret_header->length = GUINT16_TO_BE(self->priv->buffer_size - 2);
            ret_header->ftrack = 0x01;
            ret_header->ltrack = ltrack;

            break;
        }
        case 0x01: {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: multisession information\n", __debug__);
            /* Multi-session info */
            struct READ_TOC_PMA_ATIP_0001_Data *ret_data = (struct READ_TOC_PMA_ATIP_0001_Data *)self->priv->buffer;
            self->priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_0001_Data);

            GObject *lsession = NULL;
            GObject *ftrack = NULL;

            mirage_disc_get_session_by_index(disc, -1, &lsession, NULL);

            /* Header */
            ret_data->length = GUINT16_TO_BE(self->priv->buffer_size - 2);
            ret_data->fsession = 0x01;
            gint lsession_nr = 0;
            mirage_session_layout_get_session_number(MIRAGE_SESSION(lsession), &lsession_nr, NULL);
            ret_data->lsession = lsession_nr;

            /* Track data: first track in last session */
            mirage_session_get_track_by_index(MIRAGE_SESSION(lsession), 0, &ftrack, NULL);

            gint adr, ctl, number;
            mirage_track_get_adr(MIRAGE_TRACK(ftrack), &adr, NULL);
            mirage_track_get_ctl(MIRAGE_TRACK(ftrack), &ctl, NULL);
            mirage_track_layout_get_track_number(MIRAGE_TRACK(ftrack), &number, NULL);

            ret_data->adr = adr;
            ret_data->ctl = ctl;
            ret_data->ftrack = number;

            /* (H)MSF vs. LBA */
            gint start_sector = 0;
            gint track_start = 0;
            mirage_track_layout_get_start_sector(MIRAGE_TRACK(ftrack), &start_sector, NULL);
            mirage_track_get_track_start(MIRAGE_TRACK(ftrack), &track_start, NULL);

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
            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: raw TOC\n", __debug__);
            /* Raw TOC */
            struct READ_TOC_PMA_ATIP_0010_Header *ret_header = (struct READ_TOC_PMA_ATIP_0010_Header *)self->priv->buffer;
            self->priv->buffer_size = sizeof(struct READ_TOC_PMA_ATIP_0010_Header);
            struct READ_TOC_PMA_ATIP_0010_Descriptor *ret_desc = (struct READ_TOC_PMA_ATIP_0010_Descriptor *)(self->priv->buffer+self->priv->buffer_size);

            gint i,j;

            /* For each session with number above the requested one... */
            gint nr_sessions = 0;
            mirage_disc_get_number_of_sessions(disc, &nr_sessions, NULL);
            for (i = 0; i < nr_sessions; i++) {
                GObject *cur_session = NULL;
                gint session_nr = 0;

                if (!mirage_disc_get_session_by_index(disc, i, &cur_session, NULL)) {
                    CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: failed to get session by index %i!\n", __debug__, i);
                    break;
                }

                mirage_session_layout_get_session_number(MIRAGE_SESSION(cur_session), &session_nr, NULL);
                /* Return session's data only if its number is greater than or equal to requested one */
                if (session_nr >= cdb->number) {
                    gint adr, ctl, track_nr, session_type;
                    GObject *cur_track = NULL;

                    /* 1. TOC descriptor: about first track in the session */
                    mirage_session_get_track_by_index(MIRAGE_SESSION(cur_session), 0, &cur_track, NULL);
                    mirage_session_get_session_type(MIRAGE_SESSION(cur_session), &session_type, NULL);
                    mirage_track_get_adr(MIRAGE_TRACK(cur_track), &adr, NULL);
                    mirage_track_get_ctl(MIRAGE_TRACK(cur_track), &ctl, NULL);
                    mirage_track_layout_get_track_number(MIRAGE_TRACK(cur_track), &track_nr, NULL);

                    g_object_unref(cur_track);

                    ret_desc->session = session_nr;
                    ret_desc->adr = adr;
                    ret_desc->ctl = ctl;
                    ret_desc->point = 0xA0;
                    ret_desc->pmin = track_nr;
                    ret_desc->psec = session_type;

                    self->priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_0010_Descriptor);
                    ret_desc++;

                    /* 2. TOC descriptor: about last track in last session */
                    mirage_session_get_track_by_index(MIRAGE_SESSION(cur_session), -1, &cur_track, NULL);
                    mirage_track_get_adr(MIRAGE_TRACK(cur_track), &adr, NULL);
                    mirage_track_get_ctl(MIRAGE_TRACK(cur_track), &ctl, NULL);
                    mirage_track_layout_get_track_number(MIRAGE_TRACK(cur_track), &track_nr, NULL);

                    g_object_unref(cur_track);

                    ret_desc->session = session_nr;
                    ret_desc->adr = adr;
                    ret_desc->ctl = ctl;
                    ret_desc->point = 0xA1;
                    ret_desc->pmin = track_nr;

                    self->priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_0010_Descriptor);
                    ret_desc++;

                    /* 3. TOC descriptor: about lead-out */
                    gint leadout_start = 0;
                    mirage_session_get_track_by_number(MIRAGE_SESSION(cur_session), MIRAGE_TRACK_LEADOUT, &cur_track, NULL);
                    mirage_track_layout_get_start_sector(MIRAGE_TRACK(cur_track), &leadout_start, NULL);

                    g_object_unref(cur_track);

                    ret_desc->session = session_nr;
                    ret_desc->adr = 0x01;
                    ret_desc->ctl = 0x00;
                    ret_desc->point = 0xA2;

                    mirage_helper_lba2msf(leadout_start, TRUE, &ret_desc->pmin, &ret_desc->psec, &ret_desc->pframe);

                    self->priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_0010_Descriptor);
                    ret_desc++;

                    /* And now one TOC descriptor per track */
                    gint nr_tracks = 0;
                    mirage_session_get_number_of_tracks(MIRAGE_SESSION(cur_session), &nr_tracks, NULL);

                    for (j = 0; j < nr_tracks; j++) {
                        if (!mirage_session_get_track_by_index(MIRAGE_SESSION(cur_session), j, &cur_track, NULL)) {
                            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: failed to get track with index %i in session %i!\n", __debug__, j, session_nr);
                            break;
                        }

                        mirage_track_get_adr(MIRAGE_TRACK(cur_track), &adr, NULL);
                        mirage_track_get_ctl(MIRAGE_TRACK(cur_track), &ctl, NULL);
                        mirage_track_layout_get_track_number(MIRAGE_TRACK(cur_track), &track_nr, NULL);

                        ret_desc->session = session_nr;
                        ret_desc->adr = adr;
                        ret_desc->ctl = ctl;
                        ret_desc->point = track_nr;

                        gint cur_start, cur_track_start;
                        mirage_track_layout_get_start_sector(MIRAGE_TRACK(cur_track), &cur_start, NULL);
                        mirage_track_get_track_start(MIRAGE_TRACK(cur_track), &cur_track_start, NULL);
                        cur_start += cur_track_start;

                        mirage_helper_lba2msf(cur_start, TRUE, &ret_desc->pmin, &ret_desc->psec, &ret_desc->pframe);

                        self->priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_0010_Descriptor);
                        ret_desc++;
                        g_object_unref(cur_track);
                    }

                    /* If we're dealing with multisession disc, it'd probably be
                       a good idea to come up with B0 descriptors... */
                    if (nr_sessions > 1) {
                        gint leadout_length = 0;
                        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: multisession disc; cooking up a B0 descriptor for session %i!\n", __debug__, session_nr);

                        mirage_session_get_leadout_length(MIRAGE_SESSION(cur_session), &leadout_length, NULL);

                        ret_desc->session = session_nr;
                        ret_desc->adr = 0x05;
                        ret_desc->ctl = 0x00;
                        ret_desc->point = 0xB0;

                        /* If this is last session, we set MSF to 0xFF, indicating
                           disc is closed */
                        if (session_nr < nr_sessions) {
                            mirage_helper_lba2msf(leadout_start + leadout_length, TRUE, &ret_desc->min, &ret_desc->sec, &ret_desc->frame);
                        } else {
                            ret_desc->min = 0xFF;
                            ret_desc->sec = 0xFF;
                            ret_desc->frame = 0xFF;
                        }

                        /* If this is first session, we'll need to provide C0 as well */
                        if (session_nr == 1) {
                            ret_desc->zero = 2; /* Number of Mode-5 entries; we provide B0 and C0 */
                        } else {
                            ret_desc->zero = 1; /* Number of Mode-5 entries; we provide B0 only */
                        }

                        /* FIXME: have disc provide it's max capacity (currently
                           emulating 80 minute disc) */
                        ret_desc->pmin = 0x4F;
                        ret_desc->psec = 0x3B;
                        ret_desc->pframe = 0x47;

                        self->priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_0010_Descriptor);
                        ret_desc++;

                        /* Add up C0 for session 1 */
                        if (session_nr == 1) {
                            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: multisession disc; cooking up a C0 descriptor for session %i!\n", __debug__, session_nr);

                            ret_desc->session = session_nr;
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

                            self->priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_0010_Descriptor);
                            ret_desc++;
                        }
                    }

                    /* FIXME: Should we provide C0 and C1 as well? */

                }
                g_object_unref(cur_session);
            }

            /* Header */
            GObject *lsession = NULL;
            gint lsession_nr = 0;
            mirage_disc_get_session_by_index(disc, -1, &lsession, NULL);
            mirage_session_layout_get_session_number(MIRAGE_SESSION(lsession), &lsession_nr, NULL);
            g_object_unref(lsession);

            ret_header->length = GUINT16_TO_BE(self->priv->buffer_size - 2);
            ret_header->fsession = 0x01;
            ret_header->lsession = lsession_nr;

            break;
        }
        case 0x04: {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: ATIP\n", __debug__);
            /* ATIP */
            struct READ_TOC_PMA_ATIP_0100_Header *ret_header = (struct READ_TOC_PMA_ATIP_0100_Header *)self->priv->buffer;
            self->priv->buffer_size = sizeof(struct READ_TOC_PMA_ATIP_0100_Header);

            /* Header */
            ret_header->length = GUINT16_TO_BE(self->priv->buffer_size - 2);

            break;
        }
        case 0x05: {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: CD-Text\n", __debug__);
            /* CD-TEXT */
            struct READ_TOC_PMA_ATIP_0101_Header *ret_header = (struct READ_TOC_PMA_ATIP_0101_Header *)self->priv->buffer;
            self->priv->buffer_size = sizeof(struct READ_TOC_PMA_ATIP_0101_Header);

            guint8 *tmp_data = NULL;
            gint tmp_len  = 0;
            GObject *session = NULL;

            /* FIXME: for the time being, return data for first session */
            mirage_disc_get_session_by_index(MIRAGE_DISC(self->priv->disc), 0, &session, NULL);
            if (!mirage_session_get_cdtext_data(MIRAGE_SESSION(session), &tmp_data, &tmp_len, NULL)) {
                CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: failed to get CD-TEXT data!\n", __debug__);
            }
            g_object_unref(session);

            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: length of CD-TEXT data: 0x%X\n", __debug__, tmp_len);

            memcpy(self->priv->buffer+sizeof(struct READ_TOC_PMA_ATIP_0101_Header), tmp_data, tmp_len);
            g_free(tmp_data);
            self->priv->buffer_size += tmp_len;

            /* Header */
            ret_header->length = GUINT16_TO_BE(self->priv->buffer_size - 2);

            break;
        }
        default: {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: format %X not suppoted yet\n", __debug__, cdb->format);
            cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
            return FALSE;
        }
    }

    /* Write data */
    cdemud_device_write_buffer(self, GUINT16_FROM_BE(cdb->length));

    return TRUE;
}

/* READ TRACK INFORMATION*/
static gboolean command_read_track_information (CDEMUD_Device *self, guint8 *raw_cdb)
{
    struct READ_TRACK_INFORMATION_CDB *cdb = (struct READ_TRACK_INFORMATION_CDB *)raw_cdb;
    struct READ_TRACK_INFORMATION_Data *ret_data = (struct READ_TRACK_INFORMATION_Data *)self->priv->buffer;
    self->priv->buffer_size = sizeof(struct READ_TRACK_INFORMATION_Data);

    MIRAGE_Disc *disc = MIRAGE_DISC(self->priv->disc);
    GObject *track = NULL;

    if (!self->priv->loaded) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: medium not present\n", __debug__);
        cdemud_device_write_sense(self, SK_NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }

    gint number = GUINT32_FROM_BE(cdb->number);

    switch (cdb->type) {
        case 0x00: {
            /* LBA */
            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: requested track containing sector 0x%X\n", __debug__, number);
            mirage_disc_get_track_by_address(disc, number, &track, NULL);
            break;
        }
        case 0x01: {
            /* Lead-in/Track/Invisible track */
            switch (number) {
                case 0x00: {
                    /* Lead-in: not supported */
                    CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: requested lead-in; not supported!\n", __debug__);
                    cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
                    return FALSE;
                }
                case 0xFF: {
                    /* Invisible/Incomplete track: not supported */
                    CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: requested invisible/incomplete track; not supported!\n", __debug__);
                    cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
                    return FALSE;
                }
                default: {
                    /* Track number */
                    CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: requested track %i\n", __debug__, number);
                    mirage_disc_get_track_by_number(disc, number, &track, NULL);
                    break;
                }
            }
            break;
        }
        case 0x02: {
            /* Session number */
            GObject *session = NULL;
            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: requested first track in session %i\n", __debug__, number);
            mirage_disc_get_session_by_number(disc, number, &session, NULL);
            mirage_session_get_track_by_index(MIRAGE_SESSION(session), 0, &track, NULL);
            g_object_unref(session);
            break;
        }
    }

    /* Check if track was found */
    if (!track) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: couldn't find track!\n", __debug__);
        cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }

    gint track_number = 0;
    gint session_number = 0;
    gint ctl = 0;
    gint mode = 0;
    gint start_sector = 0;
    gint length = 0;

    mirage_track_layout_get_track_number(MIRAGE_TRACK(track), &track_number, NULL);
    mirage_track_layout_get_session_number(MIRAGE_TRACK(track), &session_number, NULL);
    mirage_track_get_ctl(MIRAGE_TRACK(track), &ctl, NULL);
    mirage_track_get_mode(MIRAGE_TRACK(track), &mode, NULL);
    mirage_track_layout_get_start_sector(MIRAGE_TRACK(track), &start_sector, NULL);
    mirage_track_layout_get_length(MIRAGE_TRACK(track), &length, NULL);
    g_object_unref(track);

    ret_data->length = GUINT16_TO_BE(self->priv->buffer_size - 2);
    ret_data->track_number0 = track_number >> 8;
    ret_data->track_number1 = track_number & 0xFF;
    ret_data->session_number0 = session_number >> 8;
    ret_data->session_number1 = session_number & 0xFF;
    ret_data->track_mode = ctl;

    switch (mode) {
        case MIRAGE_MODE_AUDIO:
        case MIRAGE_MODE_MODE1: {
            ret_data->data_mode = 0x01;
            break;
        }
        case MIRAGE_MODE_MODE2:
        case MIRAGE_MODE_MODE2_FORM1:
        case MIRAGE_MODE_MODE2_FORM2:
        case MIRAGE_MODE_MODE2_MIXED: {
            ret_data->data_mode = 0x02;
            break;
        }
        default:
            ret_data->data_mode = 0x0F;
    }

    ret_data->start_address = GUINT32_TO_BE(start_sector);
    ret_data->track_size = GUINT32_TO_BE(length);

    /* Write data */
    cdemud_device_write_buffer(self, GUINT16_FROM_BE(cdb->length));

    return TRUE;
}

/* REPORT KEY*/
static gboolean command_report_key (CDEMUD_Device *self, guint8 *raw_cdb)
{
    struct REPORT_KEY_CDB *cdb = (struct REPORT_KEY_CDB *)raw_cdb;

    if (cdb->key_format == 0x08) {
        /* RPC */
        struct REPORT_KEY_001000_Data *data = (struct REPORT_KEY_001000_Data *)self->priv->buffer;
        self->priv->buffer_size = sizeof(struct REPORT_KEY_001000_Data);

        data->type_code = 0; /* No region setting */
        data->vendor_resets = 4;
        data->user_changes = 5;
        data->region_mask = 0xFF; /* It's what all my drives return... */
        data->rpc_scheme = 1; /* It's what all my drives return... */

        data->length = GUINT16_TO_BE(self->priv->buffer_size - 2);
    } else {
        if (self->priv->current_profile != PROFILE_DVDROM) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: not supported with non-DVD media!\n", __debug__);
            cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, CANNOT_READ_MEDIUM_INCOMPATIBLE_FORMAT);
            return FALSE;
        }

        /* We don't support these yet */
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: FIXME: not implemented yet!\n", __debug__);
        cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }

    /* Write data */
    cdemud_device_write_buffer(self, GUINT16_FROM_BE(cdb->length));

    return TRUE;
}

/* REQUEST SENSE*/
static gboolean command_request_sense (CDEMUD_Device *self, guint8 *raw_cdb)
{
    struct REQUEST_SENSE_CDB *cdb = (struct REQUEST_SENSE_CDB *)raw_cdb;
    struct REQUEST_SENSE_SenseFixed *sense = (struct REQUEST_SENSE_SenseFixed *)self->priv->buffer;
    self->priv->buffer_size = sizeof(struct REQUEST_SENSE_SenseFixed);

    CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: returning sense data\n", __debug__);

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
    gint audio_status = 0;
    cdemud_audio_get_status(CDEMUD_AUDIO(self->priv->audio_play), &audio_status, NULL);

    sense->sense_key = SK_NO_SENSE;
    sense->asc = NO_ADDITIONAL_SENSE_INFORMATION;
    sense->ascq = audio_status;

    /* Write data */
    cdemud_device_write_buffer(self, cdb->length);

    return TRUE;
}

/* SEEK (10)*/
static gboolean command_seek (CDEMUD_Device *self, guint8 *raw_cdb G_GNUC_UNUSED)
{
    /*struct SET_CD_SPEED_CDB *cdb = (struct SET_CD_SPEED_CDB *)raw_cdb;*/
    CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: nothing to do here yet...\n", __debug__);
    return TRUE;
}

/* SET CD SPEED*/
static gboolean command_set_cd_speed (CDEMUD_Device *self, guint8 *raw_cdb)
{
    struct SET_CD_SPEED_CDB *cdb = (struct SET_CD_SPEED_CDB *)raw_cdb;
    struct ModePage_0x2A *p_0x2A = cdemud_device_get_mode_page(self, 0x2A, MODE_PAGE_CURRENT);

    /* Set the value to mode page and do nothing else at the moment...
       Note that we don't have to convert from BE neither for comparison (because
       it's 0xFFFF and unsigned short) nor when setting value (because it's BE in
       mode page anyway) */
    if (cdb->read_speed == 0xFFFF) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: setting read speed to max\n", __debug__);
        p_0x2A->cur_read_speed = p_0x2A->max_read_speed;
    } else {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: setting read speed to %i kB/s\n", __debug__, GUINT16_FROM_BE(cdb->read_speed));
        p_0x2A->cur_read_speed = cdb->read_speed;
    }

    return TRUE;
}


/* START/STOP UNIT */
static gboolean command_start_stop_unit (CDEMUD_Device *self, guint8 *raw_cdb)
{
    struct START_STOP_UNIT_CDB *cdb = (struct START_STOP_UNIT_CDB *)raw_cdb;

    CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: lo_ej: %d; start: %d\n", __debug__, cdb->lo_ej, cdb->start);

    if (cdb->lo_ej) {
        if (!cdb->start) {

            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: unloading disc...\n", __debug__);
            if (!cdemud_device_unload_disc_private(self, FALSE, NULL)) {
                CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: failed to unload disc\n", __debug__);
                cdemud_device_write_sense(self, SK_NOT_READY, MEDIUM_REMOVAL_PREVENTED);
                return FALSE;
            } else {
                CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: successfully unloaded disc\n", __debug__);
            }
        }
    }

    return TRUE;
}


/* TEST UNIT READY*/
static gboolean command_test_unit_ready (CDEMUD_Device *self, guint8 *raw_cdb G_GNUC_UNUSED)
{
    /*struct TEST_UNIT_READY_CDB *cdb = (struct TEST_UNIT_READY_CDB *)raw_cdb;*/

    /* Check if we have medium loaded */
    if (!self->priv->loaded) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: medium not present\n", __debug__);
        cdemud_device_write_sense(self, SK_NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }

    /* SCSI requires us to report UNIT ATTENTION with NOT READY TO READY CHANGE,
       MEDIUM MAY HAVE CHANGED whenever medium changes... this is required for
       linux SCSI layer to set medium block size properly upon disc insertion */
    if (self->priv->media_event == MEDIA_EVENT_NEW_MEDIA) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: reporting media changed\n", __debug__);
        self->priv->media_event = MEDIA_EVENT_NOCHANGE;
        cdemud_device_write_sense(self, SK_UNIT_ATTENTION, NOT_READY_TO_READY_CHANGE_MEDIUM_MAY_HAVE_CHANGED);
        return FALSE;
    }

    return TRUE;
}


/**********************************************************************\
 *                      Packet command switch                         *
\**********************************************************************/
gint cdemud_device_execute_command (CDEMUD_Device *self, CDEMUD_Command *cmd)
{
    gint status = STATUS_CHECK_CONDITION;

    self->priv->cmd = cmd;
    self->priv->cur_len = 0;

    /* Flush buffer */
    cdemud_device_flush_buffer(self);

    CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "\n");
    CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", __debug__,
        cmd->cdb[0], cmd->cdb[1], cmd->cdb[2], cmd->cdb[3], cmd->cdb[4], cmd->cdb[5],
        cmd->cdb[6], cmd->cdb[7], cmd->cdb[8], cmd->cdb[9], cmd->cdb[10], cmd->cdb[11]);

    /* Packet command table */
    gint i;
    static struct {
        guint8 cmd;
        gchar *debug_name;
        gboolean (*implementation)(CDEMUD_Device *, guint8 *);
        gboolean disturbs_audio_play;
    } packet_commands[] = {
        { PC_GET_EVENT_STATUS_NOTIFICATION,
          "GET EVENT/STATUS NOTIFICATION",
          command_get_event_status_notification,
          FALSE },
        { PC_GET_CONFIGURATION,
          "GET CONFIGURATION",
          command_get_configuration,
          TRUE, },
        { PC_INQUIRY,
          "INQUIRY",
          command_inquiry,
          FALSE },
        { PC_MODE_SELECT_6,
          "MODE SELECT (6)",
          command_mode_select,
          TRUE },
        { PC_MODE_SELECT_10,
          "MODE SELECT (10)",
          command_mode_select,
          TRUE },
        { PC_MODE_SENSE_6,
          "MODE SENSE (6)",
          command_mode_sense,
          TRUE },
        { PC_MODE_SENSE_10,
          "MODE SENSE (10)",
          command_mode_sense,
          TRUE },
        { PC_PAUSE_RESUME,
          "PAUSE/RESUME",
          command_pause_resume,
          FALSE /* Well, it does... but in it's own, unique way :P */ },
        { PC_PLAY_AUDIO_10,
          "PLAY AUDIO (10)",
          command_play_audio,
          TRUE },
        { PC_PLAY_AUDIO_12,
          "PLAY AUDIO (12)",
          command_play_audio,
          TRUE },
        { PC_PLAY_AUDIO_MSF,
          "PLAY AUDIO MSF",
          command_play_audio,
          TRUE },
        { PC_PREVENT_ALLOW_MEDIUM_REMOVAL,
          "PREVENT/ALLOW MEDIUM REMOVAL",
          command_prevent_allow_medium_removal,
          TRUE },
        { PC_READ_10,
          "READ (10)",
          command_read,
          TRUE },
        { PC_READ_12,
          "READ (12)",
          command_read,
          TRUE },
        { PC_READ_CAPACITY,
          "READ CAPACITY",
          command_read_capacity,
          FALSE },
        { PC_READ_CD,
          "READ CD",
          command_read_cd,
          TRUE },
        { PC_READ_CD_MSF,
          "READ CD MSF",
          command_read_cd,
          TRUE },
        { PC_READ_DISC_INFORMATION,
          "READ DISC INFORMATION",
          command_read_disc_information,
          TRUE },
        { PC_READ_DVD_STRUCTURE,
          "READ DISC STRUCTURE",
          command_read_dvd_structure,
          TRUE },
        { PC_READ_TOC_PMA_ATIP,
          "READ TOC/PMA/ATIP",
          command_read_toc_pma_atip,
          TRUE },
        { PC_READ_TRACK_INFORMATION,
          "READ TRACK INFORMATION",
          command_read_track_information,
          TRUE },
        { PC_READ_SUBCHANNEL,
          "READ SUBCHANNEL",
          command_read_subchannel,
          FALSE },
        { PC_REPORT_KEY,
          "REPORT KEY",
          command_report_key,
          TRUE },
        { PC_REQUEST_SENSE,
          "REQUEST SENSE",
          command_request_sense,
          FALSE },
        { PC_SEEK_10,
          "SEEK (10)",
          command_seek,
          FALSE },
        { PC_SET_CD_SPEED,
          "SET CD SPEED",
          command_set_cd_speed,
          TRUE },
        { PC_START_STOP_UNIT,
          "START/STOP UNIT",
          command_start_stop_unit,
          TRUE },
        { PC_TEST_UNIT_READY,
          "TEST UNIT READY",
          command_test_unit_ready,
          FALSE },
    };

    /* Find the command and execute its implementation handler */
    for (i = 0; i < G_N_ELEMENTS(packet_commands); i++) {
        if (packet_commands[i].cmd == cmd->cdb[0]) {
            gboolean succeeded = FALSE;

            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: command: %s\n", __debug__, packet_commands[i].debug_name);

            /* Lock */
            g_mutex_lock(self->priv->device_mutex);

            /* FIXME: If there is deferred error sense available, return CHECK CONDITION
               with that sense. We do not execute requested command. */

            /* Stop audio play if command disturbs it */
            if (packet_commands[i].disturbs_audio_play) {
                gint status = 0;
                cdemud_audio_get_status(CDEMUD_AUDIO(self->priv->audio_play), &status, NULL);
                if (status == AUDIO_STATUS_PLAYING || status == AUDIO_STATUS_PAUSED) {
                    cdemud_audio_stop(CDEMUD_AUDIO(self->priv->audio_play), NULL);
                }
            }
            /* Execute the command */
            succeeded = packet_commands[i].implementation(self, cmd->cdb);
            status = (succeeded) ? STATUS_GOOD : STATUS_CHECK_CONDITION;
            self->priv->cmd->out_len = self->priv->cur_len;

            /* Unlock */
            g_mutex_unlock(self->priv->device_mutex);

            CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: command completed with status %d\n", __debug__, status);

            return status;
        }
    }

    /* Command not found */
    CDEMUD_DEBUG(self, DAEMON_DEBUG_MMC, "%s: packet command %02Xh not implemented yet!\n", __debug__, cmd->cdb[0]);
    cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_COMMAND_OPERATION_CODE);
    self->priv->cmd->out_len = self->priv->cur_len;

    return status;
}
