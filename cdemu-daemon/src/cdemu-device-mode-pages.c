 /*
 *  CDEmu daemon: Device object - Userspace <-> Kernel bridge
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

#include "cdemu.h"
#include "cdemu-device-private.h"

#define __debug__ "Mode page"


/* Mode pages: we initialize three instances of each supported page; one to
   store current values, one to store default values, and one to store mask
   which indicates which values can be changed. Plus, we optionally define
   a validator for validating mode page data set via MODE SELECT command */


/**********************************************************************\
 *                    Mode page declaration helpers                   *
\**********************************************************************/
static inline gint compare_mode_pages (struct ModePageEntry *entry1, struct ModePageEntry *entry2)
{
    struct ModePageGeneral *page1 = entry1->page_default;
    struct ModePageGeneral *page2 = entry2->page_default;

    if (page1->code < page2->code) {
        return -1;
    } else if (page1->code > page2->code) {
        return 1;
    } else {
        return 0;
    }
}

static inline gint find_mode_page (struct ModePageEntry *entry, gconstpointer code_ptr)
{
    struct ModePageGeneral *page = entry->page_default;
    gint code = GPOINTER_TO_INT(code_ptr);

    if (page->code < code) {
        return -1;
    } else if (page->code > code) {
        return 1;
    } else {
        return 0;
    }
}

static inline struct ModePageEntry *initialize_mode_page (gint code, gint size, gboolean (*validator) (CdemuDevice *, const guint8 *))
{
    struct ModePageEntry *entry = g_new0(struct ModePageEntry, 1);

    /* Allocate */
    entry->page_current = g_malloc0(size);
    entry->page_default = g_malloc0(size);
    entry->page_mask = g_malloc0(size);

    entry->validate_new_data = validator;

    /* Initialize default page and mask; current page is initialized in
       append_mode_page() function */
    struct ModePageGeneral *page_default = entry->page_default;
    struct ModePageGeneral *page_mask = entry->page_mask;

    page_default->code = page_mask->code = code;
    page_default->length = page_mask->length = size - 2;

    return entry;
}

static inline GList *append_mode_page (GList *list, struct ModePageEntry *entry)
{
    /* Make a copy of MODE_PAGE_DEFAULT to MODE_PAGE_CURRENT */
    memcpy(entry->page_current, entry->page_default, ((struct ModePageGeneral *)entry->page_default)->length + 2);

    /* Append mode page entry to the list */
    return g_list_insert_sorted(list, entry, (GCompareFunc)compare_mode_pages);
}


/**********************************************************************\
 *                        Mode page validators                        *
\**********************************************************************/
static gboolean mode_page_0x05_validator (CdemuDevice *self, const guint8 *new_data)
{
    const struct ModePage_0x05 *new_page = (const struct ModePage_0x05 *)new_data;
    const struct ModePage_0x05 *old_page = cdemu_device_get_mode_page(self, 0x05, MODE_PAGE_CURRENT);

    /* Change of write type */
    if (new_page->write_type != old_page->write_type) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: changing recording type from %d to %d!\n", __debug__, old_page->write_type, new_page->write_type);
        cdemu_device_recording_set_mode(self, new_page->write_type);
    }

    return TRUE;
}


/**********************************************************************\
 *                        Mode pages public API                       *
\**********************************************************************/
gpointer cdemu_device_get_mode_page (CdemuDevice *self, gint page, ModePageType type)
{
    GList *entry = g_list_find_custom(self->priv->mode_pages_list, GINT_TO_POINTER(page), (GCompareFunc)find_mode_page);
    if (!entry) {
        return NULL;
    }

    struct ModePageEntry *page_entry = entry->data;
    switch (type) {
        case MODE_PAGE_CURRENT: {
            return page_entry->page_current;
        }
        case MODE_PAGE_DEFAULT: {
            return page_entry->page_default;
        }
        case MODE_PAGE_MASK: {
            return page_entry->page_mask;
        }
    }

    return NULL;
}


void cdemu_device_mode_pages_init (CdemuDevice *self)
{
    struct ModePageEntry *mode_page;

    /*** Mode page 0x01: Read/Write Error Recovery Parameters Mode Page ***/
    /* IMPLEMENTATION NOTE: read retry is set to 1, because we're a virtual
       device and as such don't need to do any retries (it's the value that
       Alchohol 120% virtual device reports as well). We allow it to be changed,
       though, since it makes no difference. We do allow DCR bit to be changed,
       too, because according to INF8020 it affects the way subchannel is read */
    mode_page = initialize_mode_page(0x01, sizeof(struct ModePage_0x01), NULL);
    if (mode_page) {
        struct ModePage_0x01 *page = mode_page->page_default;
        struct ModePage_0x01 *mask = mode_page->page_mask;

        page->read_retry = 0x01;

        mask->dcr = 1;
        mask->read_retry = 0xFF;
    }
    self->priv->mode_pages_list = append_mode_page(self->priv->mode_pages_list, mode_page);


    /*** Mode page 0x05: Write Parameters Mode Page ***/
    /* IMPLEMENTATION NOTE: */
    mode_page = initialize_mode_page(0x05, sizeof(struct ModePage_0x05), &mode_page_0x05_validator);
    if (mode_page) {
        struct ModePage_0x05 *page = mode_page->page_default;
        struct ModePage_0x05 *mask = mode_page->page_mask;

        page->bufe = 0; /* Buffer underrun protection; 0 by default as per MMC3 */
        mask->bufe = 1;

        page->ls_v = 1; /* Link size is valid */
        mask->ls_v = 1;

        page->test_write = 1; /* Off by default */
        mask->test_write = 1;

        page->write_type = 0x00; /* Packet/incremental */
        mask->write_type = 0x0F;

        page->multisession = 0x00; /* No multi-session */
        mask->multisession = 0x03;

        page->fp = 0; /* Variable packet by default */
        mask->fp = 1;

        page->copy = 0; /* Not a higher generation copy by default */
        mask->copy = 1;

        page->track_mode = 5;
        mask->track_mode = 0x0F;

        page->data_block_type = 8; /* Mode1 by default */
        mask->data_block_type = 0x0F;

        page->link_size = 7;
        mask->link_size = 0xFF;

        page->initiator_application_code = 0;
        mask->initiator_application_code = 0x3F;

        page->session_format = 0x00;
        mask->session_format = 0xFF;

        page->packet_size = GUINT32_TO_BE(16);
        mask->packet_size = 0xFFFFFFFF;

        page->audio_pause_length = GUINT16_TO_BE(150);
        mask->audio_pause_length = 0xFFFF;

        memset(mask->mcn, 0xFF, sizeof(mask->mcn));
        memset(mask->isrc, 0xFF, sizeof(mask->isrc));
        memset(mask->subheader, 0xFF, sizeof(mask->subheader));
    }
    self->priv->mode_pages_list = append_mode_page(self->priv->mode_pages_list, mode_page);


    /*** Mode Page 0x0D: CD Device Parameters Mode Page ****/
    /* IMPLEMENTATION NOTE: this one is marked as reserved in ATAPI, but all
       my drives return it anyway. We just set seconds per minutes and frames
       per second values, with no option of changing anything */
    mode_page = initialize_mode_page(0x0D, sizeof(struct ModePage_0x0D), NULL);
    if (mode_page) {
        struct ModePage_0x0D *page = mode_page->page_default;

        page->spm = GUINT16_TO_BE(60);
        page->fps = GUINT16_TO_BE(75);
    }
    self->priv->mode_pages_list = append_mode_page(self->priv->mode_pages_list, mode_page);


    /*** Mode Page 0x0E: CD Audio Control Mode Page ***/
    /* IMPLEMENTATION NOTE: IMMED bit is set to 1 in accord with ATAPI, and SOTC
       to 0. There is an obsolete field that is set to 75 according to ATAPI,
       and two unmuted audio ports (1 and 2). We don't support changing of IMMED,
       but SOTC can be changed, and so can all port-related fields */
    mode_page = initialize_mode_page(0x0E, sizeof(struct ModePage_0x0E), NULL);
    if (mode_page) {
        struct ModePage_0x0E *page = mode_page->page_default;
        struct ModePage_0x0E *mask = mode_page->page_mask;

        page->immed = 0;
        page->obsolete1 = 75;
        page->obsolete2 = 75;
        page->port0csel = 1;
        page->port0vol = 0xFF;
        page->port1csel = 2;
        page->port1vol = 0xFF;

        mask->sotc = 1;
        mask->port0csel = 0xF;
        mask->port0vol  = 0xFF;
        mask->port1csel = 0xF;
        mask->port1vol  = 0xFF;
        mask->port2csel = 0xF;
        mask->port2vol  = 0xFF;
        mask->port3csel = 0xF;
        mask->port3vol  = 0xFF;
    }
    self->priv->mode_pages_list = append_mode_page(self->priv->mode_pages_list, mode_page);


    /*** Mode Page 0x1A: Power Condition Mode Page ***/
    /* IMPLEMENTATION NOTE: No values for timers are set, though they can be */
    mode_page = initialize_mode_page(0x1A, sizeof(struct ModePage_0x1A), NULL);
    if (mode_page) {
        struct ModePage_0x1A *mask = mode_page->page_mask;

        mask->idle  = 1;
        mask->stdby = 1;
        mask->idle_timer  = 0xFFFFFFFF;
        mask->stdby_timer = 0xFFFFFFFF;
    }
    self->priv->mode_pages_list = append_mode_page(self->priv->mode_pages_list, mode_page);


    /*** Mode Page 0x2A: CD/DVD Capabilities and Mechanical Status Mode Page ***/
    /* IMPLEMENTATION NOTE: We claim to do things we can (more or less), and nothing
       can be changed, just like INF8090 says. We also have 6 Write Speed Performance
       Descriptors, which are appended at the end of the page */
    mode_page = initialize_mode_page(0x2A, sizeof(struct ModePage_0x2A), NULL);
    if (mode_page) {
        struct ModePage_0x2A *page = mode_page->page_default;

        page->dvdr_read = 1;
        page->dvdrom_read = 1;
        page->method2 = 1;
        page->cdrw_read = 1;
        page->cdr_read = 1;

        page->dvdram_write = 1;
        page->dvdr_write = 1;
        page->test_write = 1;
        page->cdrw_write = 1;
        page->cdr_write = 1;

        page->multisession = 1;
        page->mode2_form2 = 1;
        page->mode2_form1 = 1;
        page->audio_play = 1;

        page->read_barcode = 1;
        page->upc = 1;
        page->isrc = 1;
        page->c2pointers = 1;
        page->rw_deinterleaved = 1;
        page->rw_supported = 1;
        page->cdda_acc_stream = 1;
        page->cdda_cmds = 1;

        page->load_mech = 0x01; /* Tray */
        page->eject = 1;
        page->lock = 1;

        page->rw_in_leadin = 1;
        page->sep_mute = 1;
        page->sep_vol_lvls = 1;

        page->vol_lvls = GUINT16_TO_BE(0x0100);

        page->max_read_speed = GUINT16_TO_BE(0x2113); /* 56x */
        page->buf_size = GUINT16_TO_BE(0x0100); /* 256 kB */
        page->cur_read_speed = page->max_read_speed; /* Max by default */

        page->max_write_speed = GUINT16_TO_BE(0x2113); /* 56x */
        page->cur_write_speed = page->max_write_speed; /* Max by default */

        page->copy_man_rev = GUINT16_TO_BE(0x01);

        page->rot_ctl_sel = 1; /* Rotation control selected */
        page->cur_wspeed = page->cur_write_speed; /* Copy */

        page->num_wsp_descriptors = GUINT16_TO_BE(0); /* NOTE: write speed performance descriptors are initialized separately! */
    }
    self->priv->mode_pages_list = append_mode_page(self->priv->mode_pages_list, mode_page);

    if (1) {
        /* A hack; we need to resize the "current" Mode Page 0x2A to accept
           write speed performance descriptors */
        mode_page->page_current = g_realloc(mode_page->page_current, sizeof(struct ModePage_0x2A) + 6*sizeof(struct ModePage_0x2A_WriteSpeedPerformanceDescriptor));

        /* Modify page size */
        struct ModePage_0x2A *page = mode_page->page_current;
        page->length += 6*sizeof(struct ModePage_0x2A_WriteSpeedPerformanceDescriptor);

        page->num_wsp_descriptors = GUINT16_TO_BE(6);

        /* NOTE: the actual write speed performance descriptors are initialized when profile changes! */
        memset((page+1), 0, 6*sizeof(struct ModePage_0x2A_WriteSpeedPerformanceDescriptor));
    }
};

void cdemu_device_mode_pages_cleanup (CdemuDevice *self)
{
    for (GList *entry = self->priv->mode_pages_list; entry; entry = entry->next) {
        if (entry->data) {
            struct ModePageEntry *mode_page = entry->data;

            g_free(mode_page->page_current);
            g_free(mode_page->page_default);
            g_free(mode_page->page_mask);

            g_free(mode_page);
        }
    }
    g_list_free(self->priv->mode_pages_list);
}

gboolean cdemu_device_modify_mode_page (CdemuDevice *self, const guint8 *new_data, gint page_size)
{
    struct ModePageGeneral *page_new = (struct ModePageGeneral *)(new_data);

    /* Get page's entry */
    GList *raw_entry = g_list_find_custom(self->priv->mode_pages_list, GINT_TO_POINTER(page_new->code), (GCompareFunc)find_mode_page);
    if (!raw_entry) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: we don't have mode page 0x%X\n", __debug__, page_new->code);
        return FALSE;
    }

    /* Validate page size */
    struct ModePageEntry *page_entry = raw_entry->data;
    struct ModePageGeneral *page_current = page_entry->page_current;

    if (page_size - 2 != page_current->length) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: declared page size doesn't match length of data we were given!\n", __debug__);
        return FALSE;
    }

    if (page_new->length != page_current->length) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: invalid page size!\n", __debug__);
        return FALSE;
    }

    /* Make sure that only masked values are changed */
    const guint8 *raw_ptr_new = new_data;
    const guint8 *raw_ptr_current = page_entry->page_current;
    const guint8 *raw_ptr_mask = page_entry->page_mask;

    raw_ptr_new += 2; /* Skip code and length */
    raw_ptr_current += 2;
    raw_ptr_mask += 2;

    for (gint i = 0; i < page_new->length; i++) {
        /* Compare every byte against the mask */
        if ((raw_ptr_current[i] ^ raw_ptr_new[i]) & ~raw_ptr_mask[i]) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: attempting to change unchangeable value in byte %i!\n", __debug__, i);
            return FALSE;
        }
    }

    /* Use validator function, if provided */
    if (page_entry->validate_new_data && !page_entry->validate_new_data(self, new_data)) {
        return FALSE;
    }

    /* And finally, copy the page */
    memcpy(page_current, page_new, page_new->length + 2);

    return TRUE;
}
