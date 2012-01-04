 /*
 *  CDEmuD: Device object - Userspace <-> Kernel bridge
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


/* Mode pages: we initialize three instances of each supported page; one to
   store current values, one to store default values, and one to store mask
   which indicates which values can be changed. We pack the pointers into
   GValue array for easier access (if they were an array we'd have to calculate
   offsets for generic struct access all the time) */


/**********************************************************************\
 *                    Mode page declaration macros                    *
\**********************************************************************/
#define MODE_PAGE_DEFINITION_START(CODE) \
    if (1) { \
        GValueArray *array = g_value_array_new(3); \
        struct ModePage_##CODE *page = g_new0(struct ModePage_##CODE, 1); \
        struct ModePage_##CODE *mask = g_new0(struct ModePage_##CODE, 1); \
        /* Prepare the array */ \
        array = g_value_array_append(array, NULL); \
        array = g_value_array_append(array, NULL); \
        array = g_value_array_append(array, NULL); \
        g_value_init(g_value_array_get_nth(array, 0), G_TYPE_POINTER); \
        g_value_init(g_value_array_get_nth(array, 1), G_TYPE_POINTER); \
        g_value_init(g_value_array_get_nth(array, 2), G_TYPE_POINTER); \
        /* Initialize page and mask */ \
        page->code = mask->code = CODE; \
        page->length = mask->length = sizeof(struct ModePage_##CODE) - 2;

#define MODE_PAGE_DEFINITION_END() \
        /* Pack pointers into value array */ \
        g_value_set_pointer(g_value_array_get_nth(array, MODE_PAGE_CURRENT), page); \
        g_value_set_pointer(g_value_array_get_nth(array, MODE_PAGE_DEFAULT), g_memdup(page, sizeof(*page))); \
        g_value_set_pointer(g_value_array_get_nth(array, MODE_PAGE_MASK), mask); \
        /* Insert into list */ \
        self->priv->mode_pages_list = g_list_insert_sorted(self->priv->mode_pages_list, array, (GCompareFunc)compare_mode_pages); \
    }


static gint compare_mode_pages (GValueArray *mode_page1_ptr, GValueArray *mode_page2_ptr)
{
    struct ModePage_GENERAL *mode_page1 = g_value_get_pointer(g_value_array_get_nth(mode_page1_ptr, 0));
    struct ModePage_GENERAL *mode_page2 = g_value_get_pointer(g_value_array_get_nth(mode_page2_ptr, 0));

    if (mode_page1->code < mode_page2->code) {
        return -1;
    } else if (mode_page1->code > mode_page2->code) {
        return 1;
    } else {
        return 0;
    }
}

static gint find_mode_page (GValueArray *mode_page_ptr, gconstpointer code_ptr)
{
    struct ModePage_GENERAL *mode_page = g_value_get_pointer(g_value_array_get_nth(mode_page_ptr, 0));
    gint code = GPOINTER_TO_INT(code_ptr);

    if (mode_page->code < code) {
        return -1;
    } else if (mode_page->code > code) {
        return 1;
    } else {
        return 0;
    }
}


/**********************************************************************\
 *                    Mode pages public API                           *
\**********************************************************************/
gpointer cdemud_device_get_mode_page (CDEMUD_Device *self, gint page, gint type)
{
    GList *entry = NULL;
    entry = g_list_find_custom(self->priv->mode_pages_list, GINT_TO_POINTER(page), (GCompareFunc)find_mode_page);

    if (entry) {
        GValueArray *array = entry->data;
        return g_value_get_pointer(g_value_array_get_nth(array, type));
    }

    return NULL;
}


void cdemud_device_mode_pages_init (CDEMUD_Device *self)
{
    /*** Mode page 0x01: Read/Write Error Recovery Parameters Mode Page ***/
    /* IMPLEMENTATION NOTE: read retry is set to 1, because we're a virtual
       device and as such don't need to do any retries (it's the value that
       Alchohol 120% virtual device reports as well). We allow it to be changed,
       though, since it makes no difference. We do allow DCR bit to be changed,
       too, because according to INF8020 it affects the way subchannel is read */
    MODE_PAGE_DEFINITION_START(0x01)
    page->read_retry = 0x01;
    mask->dcr = 1;
    mask->read_retry = 0xFF;
    MODE_PAGE_DEFINITION_END()

    /*** Mode Page 0x0D: CD Device Parameters Mode Page ****/
    /* IMPLEMENTATION NOTE: this one is marked as reserved in ATAPI, but all
       my drives return it anyway. We just set seconds per minutes and frames
       per second values, with no option of changing anything */
    MODE_PAGE_DEFINITION_START(0x0D)
    page->spm = GUINT16_TO_BE(60);
    page->fps = GUINT16_TO_BE(75);
    MODE_PAGE_DEFINITION_END()

    /*** Mode Page 0x0E: CD Audio Control Mode Page ***/
    /* IMPLEMENTATION NOTE: IMMED bit is set to 1 in accord with ATAPI, and SOTC
       to 0. There is an obsolete field that is set to 75 according to ATAPI,
       and two unmuted audio ports (1 and 2). We don't support changing of IMMED,
       but SOTC can be changed, and so can all port-related fields */
    MODE_PAGE_DEFINITION_START(0x0E)
    page->immed = 0;
    page->__dummy4__[4] = 75;
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
    MODE_PAGE_DEFINITION_END()

    /*** Mode Page 0x1A: Power Condition Mode Page ***/
    /* IMPLEMENTATION NOTE: No values for timers are set, though they can be */
    MODE_PAGE_DEFINITION_START(0x1A)
    mask->idle  = 1;
    mask->stdby = 1;
    mask->idle_timer  = 0xFFFFFFFF;
    mask->stdby_timer = 0xFFFFFFFF;
    MODE_PAGE_DEFINITION_END()

    /*** Mode Page 0x2A: CD/DVD Capabilities and Mechanical Status Mode Page ***/
    /* IMPLEMENTATION NOTE: We claim to do things we can (more or less), and nothing
       can be changed, just like INF8090 says */
    MODE_PAGE_DEFINITION_START(0x2A)

    page->dvdr_read = 1;
    page->dvdrom_read = 1;
    page->method2 = 1;
    page->cdrw_read = 1;
    page->cdr_read = 1;

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
    MODE_PAGE_DEFINITION_END()

    return;
};

void cdemud_device_mode_pages_cleanup (CDEMUD_Device *self)
{
    GList *entry;
    G_LIST_FOR_EACH(entry, self->priv->mode_pages_list) {
        if (entry->data) {
            GValueArray *array = entry->data;

            g_free(g_value_get_pointer(g_value_array_get_nth(array, 0)));
            g_free(g_value_get_pointer(g_value_array_get_nth(array, 1)));
            g_free(g_value_get_pointer(g_value_array_get_nth(array, 2)));

            g_value_array_free(array);
        }
    }
    g_list_free(self->priv->mode_pages_list);
}

