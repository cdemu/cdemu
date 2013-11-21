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

#define __debug__ "MMC-3"


/* Mode pages: we initialize three instances of each supported page; one to
   store current values, one to store default values, and one to store mask
   which indicates which values can be changed. We pack the pointers into
   GValue array for easier access (if they were an array we'd have to calculate
   offsets for generic struct access all the time) */


/**********************************************************************\
 *                    Mode page declaration helpers                   *
\**********************************************************************/


static inline gint compare_mode_pages (GArray *mode_page1_ptr, GArray *mode_page2_ptr)
{
    struct ModePageGeneral *mode_page1 = g_array_index(mode_page1_ptr, struct ModePageGeneral *, MODE_PAGE_DEFAULT);
    struct ModePageGeneral *mode_page2 = g_array_index(mode_page2_ptr, struct ModePageGeneral *, MODE_PAGE_DEFAULT);

    if (mode_page1->code < mode_page2->code) {
        return -1;
    } else if (mode_page1->code > mode_page2->code) {
        return 1;
    } else {
        return 0;
    }
}

static inline gint find_mode_page (GArray *mode_page_ptr, gconstpointer code_ptr)
{
    struct ModePageGeneral *mode_page = g_array_index(mode_page_ptr, struct ModePageGeneral *, MODE_PAGE_DEFAULT);
    gint code = GPOINTER_TO_INT(code_ptr);

    if (mode_page->code < code) {
        return -1;
    } else if (mode_page->code > code) {
        return 1;
    } else {
        return 0;
    }
}


static inline GArray *initialize_mode_page (gint code, gint size)
{
    GArray *array = g_array_sized_new(FALSE, TRUE, sizeof(struct ModePageGeneral *), 3);
    g_assert(array != NULL);

    struct ModePageGeneral *page_current = g_malloc0(size);
    struct ModePageGeneral *page_default = g_malloc0(size);
    struct ModePageGeneral *page_mask = g_malloc0(size);

    /* Initialize default page and mask; current page is initialized in
       append_mode_page() function */
    page_default->code   = page_mask->code   = code;
    page_default->length = page_mask->length = size - 2;

    /* Pack pointers into value array */
    g_array_append_val(array, page_current); /* MODE_PAGE_CURRENT */
    g_array_append_val(array, page_default); /* MODE_PAGE_DEFAULT */
    g_array_append_val(array, page_mask); /* MODE_PAGE_MASK */

    return array;
}

static inline GList *append_mode_page (GList *list, GArray *mode_page)
{
    struct ModePageGeneral *page_default = g_array_index(mode_page, struct ModePageGeneral *, MODE_PAGE_DEFAULT);
    struct ModePageGeneral *page_current = g_array_index(mode_page, struct ModePageGeneral *, MODE_PAGE_CURRENT);

    /* Make a copy of MODE_PAGE_DEFAULT to MODE_PAGE_CURRENT */
    memcpy(page_current, page_default, page_default->length + 2);

    /* Append mode page array to the list */
    return g_list_insert_sorted(list, mode_page, (GCompareFunc)compare_mode_pages);
}



/**********************************************************************\
 *                    Mode pages public API                           *
\**********************************************************************/
gpointer cdemu_device_get_mode_page (CdemuDevice *self, gint page, gint type)
{
    GList *entry = NULL;
    entry = g_list_find_custom(self->priv->mode_pages_list, GINT_TO_POINTER(page), (GCompareFunc)find_mode_page);

    if (entry) {
        GArray *array = entry->data;
        return g_array_index(array, gpointer, type);
    }

    return NULL;
}


void cdemu_device_mode_pages_init (CdemuDevice *self)
{
    GArray *mode_page;

    /*** Mode page 0x01: Read/Write Error Recovery Parameters Mode Page ***/
    /* IMPLEMENTATION NOTE: read retry is set to 1, because we're a virtual
       device and as such don't need to do any retries (it's the value that
       Alchohol 120% virtual device reports as well). We allow it to be changed,
       though, since it makes no difference. We do allow DCR bit to be changed,
       too, because according to INF8020 it affects the way subchannel is read */
    mode_page = initialize_mode_page(0x01, sizeof(struct ModePage_0x01));
    if (mode_page) {
        struct ModePage_0x01 *page = g_array_index(mode_page, struct ModePage_0x01 *, MODE_PAGE_DEFAULT);
        struct ModePage_0x01 *mask = g_array_index(mode_page, struct ModePage_0x01 *, MODE_PAGE_MASK);

        page->read_retry = 0x01;

        mask->dcr = 1;
        mask->read_retry = 0xFF;
    }
    self->priv->mode_pages_list = append_mode_page(self->priv->mode_pages_list, mode_page);

    
    /*** Mode page 0x05: Write Parameters Mode Page ***/
    /* IMPLEMENTATION NOTE: */
    mode_page = initialize_mode_page(0x05, sizeof(struct ModePage_0x05));
    if (mode_page) {
        struct ModePage_0x05 *page = g_array_index(mode_page, struct ModePage_0x05 *, MODE_PAGE_DEFAULT);
        struct ModePage_0x05 *mask = g_array_index(mode_page, struct ModePage_0x05 *, MODE_PAGE_MASK);
        
        page->bufe = 1; /* Buffer underrun protection; 0 by default as per MMC3 */
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
    mode_page = initialize_mode_page(0x0D, sizeof(struct ModePage_0x0D));
    if (mode_page) {
        struct ModePage_0x0D *page = g_array_index(mode_page, struct ModePage_0x0D *, MODE_PAGE_DEFAULT);

        page->spm = GUINT16_TO_BE(60);
        page->fps = GUINT16_TO_BE(75);
    }
    self->priv->mode_pages_list = append_mode_page(self->priv->mode_pages_list, mode_page);


    /*** Mode Page 0x0E: CD Audio Control Mode Page ***/
    /* IMPLEMENTATION NOTE: IMMED bit is set to 1 in accord with ATAPI, and SOTC
       to 0. There is an obsolete field that is set to 75 according to ATAPI,
       and two unmuted audio ports (1 and 2). We don't support changing of IMMED,
       but SOTC can be changed, and so can all port-related fields */
    mode_page = initialize_mode_page(0x0E, sizeof(struct ModePage_0x0E));
    if (mode_page) {
        struct ModePage_0x0E *page = g_array_index(mode_page, struct ModePage_0x0E *, MODE_PAGE_DEFAULT);
        struct ModePage_0x0E *mask = g_array_index(mode_page, struct ModePage_0x0E *, MODE_PAGE_MASK);

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
    mode_page = initialize_mode_page(0x1A, sizeof(struct ModePage_0x1A));
    if (mode_page) {
        struct ModePage_0x1A *mask = g_array_index(mode_page, struct ModePage_0x1A *, MODE_PAGE_MASK);

        mask->idle  = 1;
        mask->stdby = 1;
        mask->idle_timer  = 0xFFFFFFFF;
        mask->stdby_timer = 0xFFFFFFFF;
    }
    self->priv->mode_pages_list = append_mode_page(self->priv->mode_pages_list, mode_page);


    /*** Mode Page 0x2A: CD/DVD Capabilities and Mechanical Status Mode Page ***/
    /* IMPLEMENTATION NOTE: We claim to do things we can (more or less), and nothing
       can be changed, just like INF8090 says */
    mode_page = initialize_mode_page(0x2A, sizeof(struct ModePage_0x2A));
    if (mode_page) {
        struct ModePage_0x2A *page = g_array_index(mode_page, struct ModePage_0x2A *, MODE_PAGE_DEFAULT);

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

        page->max_read_speed = GUINT16_TO_BE(0x1B90); /* 40X */
        page->buf_size = GUINT16_TO_BE(0x0100); /* 256 kB */
        page->cur_read_speed = page->max_read_speed; /* Max by default */

        page->copy_man_rev = GUINT16_TO_BE(0x01);
    }
    self->priv->mode_pages_list = append_mode_page(self->priv->mode_pages_list, mode_page);
};

void cdemu_device_mode_pages_cleanup (CdemuDevice *self)
{
    for (GList *entry = self->priv->mode_pages_list; entry; entry = entry->next) {
        if (entry->data) {
            GArray *array = entry->data;

            g_free(g_array_index(array, gpointer, 0));
            g_free(g_array_index(array, gpointer, 1));
            g_free(g_array_index(array, gpointer, 2));

            g_array_free(array, TRUE);
        }
    }
    g_list_free(self->priv->mode_pages_list);
}

