/*
 *  CDEmuD: Device object
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

#include "cdemud.h"
#include "cdemud-marshallers.h"


/* Don't ask... it just happens that some fields are of 3-byte size... >.< */
#define GUINT24_FROM_BE(x) (GUINT32_FROM_BE(x) >> 8)
#define GUINT24_TO_BE(x)   (GUINT32_TO_BE(x) >> 8)
    
/* List of audio backends */
static const CDEMUD_AudioBackend audio_backends[] = {
#ifdef ALSA_BACKEND
    { "alsa", cdemud_audio_alsa_get_type },
#endif
    /* Last entry as it's also our fallback */
    { "null", cdemud_audio_null_get_type },
};

/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define CDEMUD_DEVICE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), CDEMUD_TYPE_DEVICE, CDEMUD_DevicePrivate))

typedef struct {   
    gboolean initialized;
    
    /* Device stuff */
    gint number;
    gchar *device_name;
    
    /* Device mutex */
    GStaticMutex device_mutex;
    
    /* Command */
    CDEMUD_Command *cmd;
    guint cur_len;
    
    /* Buffer/"cache" */
    guint8 *buffer;
    gint buffer_size;
    
    /* Audio play */
    GObject *audio_play;
    
    /* Mirage */
    GObject *mirage;
    
    /* Disc */
    gboolean loaded;
    GObject *disc;
    GObject *disc_debug; /* Debug context for disc */
    
    /* Locked flag */
    gboolean locked;
    /* Media changed flag */
    gboolean media_changed;
            
    /* Last accessed sector */
    gint current_sector;
    
    /* Mode pages */
    GList *mode_pages_list;
    
    /* Current device profile */
    gint current_profile;
    /* Features */
    GList *features_list;
} CDEMUD_DevicePrivate;


/******************************************************************************\
 *                                  Helpers                                   *
\******************************************************************************/
static gint __helper_map_expected_sector_type (gint type) {
    switch (type) {
        case 0: {
            /* All types */
            return 0;
        }
        case 1: {
            /* CD-DA */
            return MIRAGE_MODE_AUDIO;
        }
        case 2: {
            /* Mode 1 */
            return MIRAGE_MODE_MODE1;
        }
        case 3: {
            /* Mode 2 Formless */
            return MIRAGE_MODE_MODE2;
        }
        case 4: {
            /* Mode 2 Form 1 */
            return MIRAGE_MODE_MODE2_FORM1;
        }
        case 5: {
            /* Mode 2 Form 2 */
            return MIRAGE_MODE_MODE2_FORM2;
        }
        default: {
            return -1;
        }
    }
        
}

static gint __helper_map_mcsb (guint8 *byte9, gint mode_code) {
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


/******************************************************************************\
 *                    Kernel <-> Userspace bridge functions                   *
\******************************************************************************/
static void __cdemud_device_write_buffer (CDEMUD_Device *self, guint32 length) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    guint32 len;

    len = MIN(_priv->buffer_size, length);
    if (_priv->cur_len + len > _priv->cmd->out_len) {
        len = _priv->cmd->out_len - _priv->cur_len;
    }

    memcpy(_priv->cmd->out + _priv->cur_len, _priv->buffer, len);
    _priv->cur_len += len;
    return;
}

static void __cdemud_device_read_buffer (CDEMUD_Device *self, guint32 length) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    guint32 len;

    len = MIN(_priv->cmd->in_len, length);
    memcpy(_priv->buffer, _priv->cmd->in, len);
    _priv->buffer_size = len;
    return;
}

static void __cdemud_device_write_sense_full (CDEMUD_Device *self, guint8 sense_key, guint16 asc_ascq, gint ili, guint32 command_info) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    
    /* Initialize sense */
    struct REQUEST_SENSE_SenseFixed sense;
    
    memset(&sense, 0, sizeof(struct REQUEST_SENSE_SenseFixed));
    sense.res_code = 0x70; /* Current error */
    sense.valid = 0;
    sense.length = 0x0A; /* Additional sense length */
    
    /* Sense key and ASC/ASCQ */
    sense.sense_key = sense_key;
    sense.asc = (asc_ascq & 0xFF00) >> 8; /* ASC */
    sense.ascq = (asc_ascq & 0x00FF) >> 0; /* ASCQ */
    /* ILI bit */
    sense.ili = ili;
    /* Command information */
    sense.cmd_info[0] = (command_info & 0xFF000000) >> 24;
    sense.cmd_info[1] = (command_info & 0x00FF0000) >> 16;
    sense.cmd_info[2] = (command_info & 0x0000FF00) >>  8;
    sense.cmd_info[3] = (command_info & 0x000000FF) >>  0;
    
    memcpy(_priv->cmd->out, &sense, sizeof(struct REQUEST_SENSE_SenseFixed));
    _priv->cur_len = sizeof(struct REQUEST_SENSE_SenseFixed);
    return;
}

#define __cdemud_device_write_sense(self, sense_key, asc_ascq) __cdemud_device_write_sense_full(self, sense_key, asc_ascq, 0, 0x0000)


static void __cdemud_device_flush_buffer (CDEMUD_Device *self) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);

    memset(_priv->buffer, 0, _priv->buffer_size);
    _priv->buffer_size = 0;
}


/******************************************************************************\
 *                             Mode pages handling                            *
\******************************************************************************/
/* Mode pages: we initialize three instances of each supported page; one to 
   store current values, one to store default values, and one to store mask 
   which indicates which values can be changed. We pack the pointers into 
   GValue array for easier access (if they were an array we'd have to calculate 
   offsets for generic struct access all the time) */

enum {
    MODE_PAGE_CURRENT = 0,
    MODE_PAGE_DEFAULT = 1,
    MODE_PAGE_MASK    = 2
};

/* Because a decent portion of setting up a mode page involves same code, we
   do it via three macros. I know, it's a black magic, and it obfuscates code,
   but it actually looks nicer and makes it easier to implement a page... */
#define MODE_PAGE_DEFINITION_START(CODE)                                        \
    if (1) {                                                                    \
        GValueArray *array = g_value_array_new(3);                              \
        struct ModePage_##CODE *page = g_new0(struct ModePage_##CODE, 1);       \
        struct ModePage_##CODE *mask = g_new0(struct ModePage_##CODE, 1);       \
                                                                                \
        /* Prepare the array */                                                 \
        array = g_value_array_append(array, NULL);                              \
        array = g_value_array_append(array, NULL);                              \
        array = g_value_array_append(array, NULL);                              \
        g_value_init(g_value_array_get_nth(array, 0), G_TYPE_POINTER);          \
        g_value_init(g_value_array_get_nth(array, 1), G_TYPE_POINTER);          \
        g_value_init(g_value_array_get_nth(array, 2), G_TYPE_POINTER);          \
                                                                                \
        /* Initialize page and mask */                                          \
        page->code = mask->code = CODE;                                         \
        page->length = mask->length = sizeof(struct ModePage_##CODE) - 2;

#define MODE_PAGE_DEFINITION_END()                                              \
        /* Pack pointers into value array */                                    \
        g_value_set_pointer(g_value_array_get_nth(array, MODE_PAGE_CURRENT), page); \
        g_value_set_pointer(g_value_array_get_nth(array, MODE_PAGE_DEFAULT), g_memdup(page, sizeof(*page))); \
        g_value_set_pointer(g_value_array_get_nth(array, MODE_PAGE_MASK), mask); \
                                                                                \
        /* Insert into list */                                                  \
        _priv->mode_pages_list = g_list_insert_sorted(_priv->mode_pages_list, array, __compare_mode_pages); \
    }

static gint __compare_mode_pages (gconstpointer mode_page1_ptr, gconstpointer mode_page2_ptr) {
    struct ModePage_GENERAL *mode_page1 = g_value_get_pointer(g_value_array_get_nth((GValueArray *)mode_page1_ptr, 0));
    struct ModePage_GENERAL *mode_page2 = g_value_get_pointer(g_value_array_get_nth((GValueArray *)mode_page2_ptr, 0));
    
    if (mode_page1->code < mode_page2->code) {
        return -1;
    } else if (mode_page1->code > mode_page2->code) {
        return 1;
    } else {
        return 0;
    }
}

static gint __find_mode_page (gconstpointer mode_page_ptr, gconstpointer code_ptr) {
    struct ModePage_GENERAL *mode_page = g_value_get_pointer(g_value_array_get_nth((GValueArray *)mode_page_ptr, 0));
    gint code = GPOINTER_TO_INT(code_ptr);
    
    if (mode_page->code < code) {
        return -1;
    } else if (mode_page->code > code) {
        return 1;
    } else {
        return 0;
    }
}

static gpointer __cdemud_device_get_mode_page (CDEMUD_Device *self, gint page, gint type) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    GList *entry = NULL;
    entry = g_list_find_custom(_priv->mode_pages_list, GINT_TO_POINTER(page), __find_mode_page);
    
    if (entry) {
        GValueArray *array = entry->data;
        return g_value_get_pointer(g_value_array_get_nth(array, type));
    }
    
    return NULL;
}

static void __cdemud_device_init_mode_pages (CDEMUD_Device *self) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    
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


/******************************************************************************\
 *                                Features handling                           *
\******************************************************************************/
/* Since macro magic seemed to work nice for mode page declarations, we'll do 
   the same with features; it's less complicated here, because we need to store
   only one instance of the feature data, which we put directly in the list */
#define FEATURE_DEFINITION_START(CODE)                                          \
    if (1) {                                                                    \
        struct Feature_##CODE *feature = g_new0(struct Feature_##CODE, 1);      \
                                                                                \
        /* Initialize feature */                                                \
        feature->code = GUINT16_TO_BE(CODE);                                    \
        feature->length = sizeof(struct Feature_##CODE) - 4;

#define FEATURE_DEFINITION_END()                                                \
        /* Insert into list */                                                  \
        _priv->features_list = g_list_insert_sorted(_priv->features_list, feature, __compare_features); \
    }

static gint __compare_features (gconstpointer feature1_ptr, gconstpointer feature2_ptr) {
    struct Feature_GENERAL *feature1 = (struct Feature_GENERAL *)feature1_ptr;
    struct Feature_GENERAL *feature2 = (struct Feature_GENERAL *)feature2_ptr;
    
    gint code1 = GUINT16_FROM_BE(feature1->code);
    gint code2 = GUINT16_FROM_BE(feature2->code);
    
    if (code1 < code2) {
        return -1;
    } else if (code1 > code2) {
        return 1;
    } else {
        return 0;
    }
}

static gint __find_feature (gconstpointer feature_ptr, gconstpointer code_ptr) {
    struct Feature_GENERAL *feature = (struct Feature_GENERAL *)feature_ptr;
    gint feat_code = GUINT16_FROM_BE(feature->code);
    gint code = GPOINTER_TO_INT(code_ptr);

    if (feat_code < code) {
        return -1;
    } else if (feat_code > code) {
        return 1;
    } else {
        return 0;
    }
}

static gpointer __cdemud_device_get_feature (CDEMUD_Device *self, gint feature) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    GList *entry = NULL;
    entry = g_list_find_custom(_priv->features_list, GINT_TO_POINTER(feature), __find_feature);
    
    if (entry) {
        return entry->data;
    }
    
    return NULL;
}

static void __cdemud_device_init_features (CDEMUD_Device *self) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    
    /* Feature 0x0000: Profile List */
    /* IMPLEMENTATION NOTE: persistent; we support two profiles; CD-ROM and 
       DVD-ROM. Version is left at 0x00, as per INF8090 */
    FEATURE_DEFINITION_START(0x0000)
    feature->per = 1;
    
    feature->profiles[0].profile = GUINT16_TO_BE(PROFILE_CDROM);
    feature->profiles[1].profile = GUINT16_TO_BE(PROFILE_DVDROM);
    
    FEATURE_DEFINITION_END()
    
    
    /* Feature 0x0001: Core Feature */
    /* IMPLEMENTATION NOTE: persistent; INF8090 requires us to set version to
       0x02. We emulate ATAPI device, thus interface is set to ATAPI. We don't
       support everything that's required for INQ2 bit (namely, vital product
       information) and we don't support device busy event class */
    FEATURE_DEFINITION_START(0x0001)
    feature->per = 1;
    feature->ver = 0x02;
    
    feature->interface = GUINT32_TO_BE(0x02); /* ATAPI */
    FEATURE_DEFINITION_END()
    
    
    /* Feature 0x0002: Morphing Feature */
    /* IMPLEMENTATION NOTE: persistent; version set to 0x01 as per INF8090. Both
       Async and OCEvent bits are left at 0. */
    FEATURE_DEFINITION_START(0x0002)
    feature->per = 1;
    feature->ver = 0x01;
    FEATURE_DEFINITION_END()


    /* Feature 0x0003: Removable Medium Feature */
    /* IMPLEMENTATION NOTE: persistent; version left at 0x00 as there's none
       specified in INF8090. Mechanism is set to 'Tray' (0x001), and we support
       both eject and lock. Prevent jumper is not present. */
    FEATURE_DEFINITION_START(0x0003)
    feature->per = 1;
        
    feature->mechanism = 0x001;
    feature->eject = 1;
    feature->lock = 1;
    FEATURE_DEFINITION_END()
    
    
    /* Feature 0x0010: Random Readable Feature */
    /* IMPLEMENTATION NOTE: non-persistent; version left at 0x00. Block size is
       2048 bytes and we set blocking to 1 (as recommended for most CD-ROMs...
       it's non-essential, really). Read-write error recovery page is present */
    FEATURE_DEFINITION_START(0x0010)    
    feature->block_size = GUINT32_TO_BE(2048);
    feature->blocking = GUINT16_TO_BE(1);
    feature->pp = 1;
    FEATURE_DEFINITION_END()
    
    
    /* Feature 0x001D: Multi-read Feature */
    /* IMPLEMENTATION NOTE: non-persistent; version left at 0x00. No other content. */
    FEATURE_DEFINITION_START(0x001D)
    FEATURE_DEFINITION_END()
    
    
    /* Feature 0x001E: CD Read Feature */
    /* IMPLEMENTATION NOTE: non-persistent; version set to 0x02 as per INF8090.
       Both C2Flags and CDText bits are set, while DAP is not supported. */
    FEATURE_DEFINITION_START(0x001E)
    feature->ver = 0x02;
    
    feature->c2flags = 1;
    feature->cdtext = 1;
    FEATURE_DEFINITION_END()

    
    /* Feature 0x001F: DVD Read Feature */
    /* IMPLEMENTATION NOTE: non-persistent; version set to 0x01 as per INF8090.
       We claim we conform to DVD Multi Specification Version 1.1 and that we
       support dual-layer DVD-R */
    FEATURE_DEFINITION_START(0x001F)
    feature->ver = 0x01;
    
    feature->multi110 = 1;
    feature->dualr = 1;
    FEATURE_DEFINITION_END()

    
    /* Feature 0x0100: Power Management Feature */
    /* IMPLEMENTATION NOTE: persistent; version left at 0x00. No other content. */
    FEATURE_DEFINITION_START(0x0100)
    feature->per = 1;
    FEATURE_DEFINITION_END()

    
    /* Feature 0x0103: CD External Audio Play Feature */    
    /* IMPLEMENTATION NOTE: non-persistent; version left at 0x00. Separate volume
       and separate channel mute are supported, and so is scan. Volume levels is
       set to 0x100. */
    FEATURE_DEFINITION_START(0x0103)       
    feature->scm = 1;
    feature->sv = 1;
    feature->scan = 1;
    feature->vol_lvls = GUINT16_TO_BE(0x0100);
    FEATURE_DEFINITION_END()

    
    /* Feature 0x0106: DVD CSS Feature */
    /* IMPLEMENTATION NOTE: non-persistent; version left at 0x00. CSS version is
       set to 0x01 as per INF8090. */
    FEATURE_DEFINITION_START(0x0106)    
    feature->css_ver = 0x01;
    FEATURE_DEFINITION_END()

    
    /* Feature 0x0107: Real Time Streaming Feature */
    /* IMPLEMENTATION NOTE: non-persistent; version is set to 0x03 as per INF8090.
       We claim we support READ BUFFER CAPACITY and we do support SET CD SPEED. We
       don't support mode page 0x2A with write speed performance descriptors and
       we don't support the rest of write-related functions. */
    FEATURE_DEFINITION_START(0x0107)
    feature->ver = 0x03;
    
    feature->rbcb = 1;
    feature->scs = 1;
    FEATURE_DEFINITION_END()

    return;
}


/******************************************************************************\
 *                                 Profile handling                           *
\******************************************************************************/
/* Here we define profiles and features associated with them. When we set a 
   certain profile, features get their 'current' bit reset (unless 'persistent'
   bit is set), and then if they are associated with the profile, they are set
   again */
static guint32 Features_PROFILE_CDROM[] = {
    0x0010, /* Random Readable Feature */
    0x001D, /* Multi-read Feature */
    0x001E, /* CD Read Feature */
    0x0103, /* CD External Audio Play Feature */
    0x0107, /* Real Time Streaming Feature */
};

static guint32 Features_PROFILE_DVDROM[] = {
    0x0010, /* Random Readable Feature */
    0x001F, /* DVD Read Feature */
    0x0106, /* DVD CSS Feature */
    0x0107, /* Real Time Streaming Feature */
};

static void __cdemud_device_set_current_features (CDEMUD_Device *self, guint32 *feats, gint feats_len) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    
    /* Go over the features list and reset 'current' bits of features that
       don't have 'persistent' bit set */
    GList *entry = NULL;
    G_LIST_FOR_EACH(entry, _priv->features_list) {
        struct Feature_GENERAL *feature = entry->data;
        
        if (!feature->per) {
            feature->cur = 0;
        } else {
            feature->cur = 1;
        }
    };
    
    /* Now go over list of input features and set their 'current' bits */
    gint i;
    for (i = 0; i < feats_len; i++) {
        struct Feature_GENERAL *feature = __cdemud_device_get_feature(self, feats[i]);
        if (feature) {
            feature->cur = 1;
        } else {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: feature 0x%X not found; shouldn't happen!\n", __func__, feats[i]);
        }
    }
    
    return;
}

static void __cdemud_device_set_profile (CDEMUD_Device *self, gint profile) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    
    /* Set current profile */
    _priv->current_profile = profile;
    
    /* Features */
    switch (profile) {
        case PROFILE_NONE: {
            /* Current features */
            __cdemud_device_set_current_features(self, NULL, 0);
            /* Profiles */
            struct Feature_0x0000 *f_0x0000 = __cdemud_device_get_feature(self, 0x0000);
            f_0x0000->profiles[0].cur = 0;
            f_0x0000->profiles[1].cur = 0;
            break;
        }
        case PROFILE_CDROM: {
            /* Current features */
            __cdemud_device_set_current_features(self, Features_PROFILE_CDROM, G_N_ELEMENTS(Features_PROFILE_CDROM));
            /* Profiles */
            struct Feature_0x0000 *f_0x0000 = __cdemud_device_get_feature(self, 0x0000);
            f_0x0000->profiles[0].cur = 1;
            f_0x0000->profiles[1].cur = 0;
            break;
        }
        case PROFILE_DVDROM: {
            /* Current features */
            __cdemud_device_set_current_features(self, Features_PROFILE_DVDROM, G_N_ELEMENTS(Features_PROFILE_DVDROM));
           /* Profiles */
            struct Feature_0x0000 *f_0x0000 = __cdemud_device_get_feature(self, 0x0000);
            f_0x0000->profiles[0].cur = 0;
            f_0x0000->profiles[1].cur = 1;
            break;
        }
    }
    
    return;
}


/******************************************************************************\
 *           Packet commands and packet command switch implementation         *
\******************************************************************************/
/* GET CONFIGURATION implementation */
static gboolean __cdemud_device_pc_get_configuration (CDEMUD_Device *self, guint8 *raw_cdb) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    struct GET_CONFIGURATION_CDB *cdb = (struct GET_CONFIGURATION_CDB*)raw_cdb;
    struct GET_CONFIGURATION_Header *ret_header = (struct GET_CONFIGURATION_Header *)_priv->buffer;
    _priv->buffer_size = sizeof(struct GET_CONFIGURATION_Header);
    guint8 *ret_data = _priv->buffer+_priv->buffer_size;

    CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: requesting features from 0x%X on, with RT flag 0x%X\n", __func__, GUINT16_FROM_BE(cdb->sfn), cdb->rt);
    
    /* Go over *all* features, and copy them according to RT value */
    GList *entry = NULL;
    G_LIST_FOR_EACH(entry, _priv->features_list) {
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
                
                CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: copying feature 0x%X\n", __func__, GUINT16_FROM_BE(feature->code));
                
                /* Copy feature */
                memcpy(ret_data, feature, feature->length + 4);
                _priv->buffer_size += feature->length + 4;
                ret_data += feature->length + 4;
                
                /* Break the loop if RT is 0x02 */
                if (cdb->rt == 0x02) {
                    CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: got the feature we wanted (0x%X), breaking the loop\n", __func__, GUINT16_FROM_BE(cdb->sfn));
                    break;
                }
            }
        }
    }

    /* Header */
    ret_header->length = GUINT32_TO_BE(_priv->buffer_size - 4);
    ret_header->cur_profile = GUINT16_TO_BE(_priv->current_profile);
    
    /* Write data */
    __cdemud_device_write_buffer(self, GUINT16_FROM_BE(cdb->length));
    
    return TRUE;
}


/* GET EVENT/STATUS NOTIFICATION implementation */
static gboolean __cdemud_device_pc_get_event_status_notification (CDEMUD_Device *self, guint8 *raw_cdb) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    struct GET_EVENT_STATUS_NOTIFICATION_CDB *cdb = (struct GET_EVENT_STATUS_NOTIFICATION_CDB*)raw_cdb;
    struct GET_EVENT_STATUS_NOTIFICATION_Header *ret_header = (struct GET_EVENT_STATUS_NOTIFICATION_Header *)_priv->buffer;
    _priv->buffer_size = sizeof(struct GET_EVENT_STATUS_NOTIFICATION_Header);
    
    if (!cdb->immed) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_FIXME, "%s: asynchronous type not supported yet!\n", __func__);
        __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }
    
    /* Let's say we're empty... this will change later in code accordingly */
    ret_header->nea = 1;
    
    /* Signal which event classes we do support */
    ret_header->media = 1;
    
    /* Process event classes */
    if (cdb->media) {
        struct GET_EVENT_STATUS_NOTIFICATION_MediaEventDescriptor *ret_desc = (struct GET_EVENT_STATUS_NOTIFICATION_MediaEventDescriptor *)(_priv->buffer+_priv->buffer_size);
        _priv->buffer_size += sizeof(struct GET_EVENT_STATUS_NOTIFICATION_MediaEventDescriptor);
        
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: media event class\n", __func__);
        
        ret_header->nea = 0;
        ret_header->not_class = 4; /* Media notification class */
        
        /* Media changed? */
        if (_priv->media_changed) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: medium has changed; reporting\n", __func__);
            ret_desc->event = 4; /* Media Changed */
            _priv->media_changed = FALSE;
        }
        
        /* Media status */
        ret_desc->present = _priv->loaded;
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: medium present: %d\n", __func__, ret_desc->present);
    }
    
    /* Header */
    ret_header->length = GUINT16_TO_BE(_priv->buffer_size - 2);
    
    /* Write data */
    __cdemud_device_write_buffer(self, GUINT16_FROM_BE(cdb->length));
    
    return TRUE;
}


/* INQUIRY implementation */
static gboolean __cdemud_device_pc_inquiry (CDEMUD_Device *self, guint8 *raw_cdb) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    struct INQUIRY_CDB *cdb = (struct INQUIRY_CDB *)raw_cdb;
        
    struct INQUIRY_Data *ret_data = (struct INQUIRY_Data *)_priv->buffer;
    _priv->buffer_size = sizeof(struct INQUIRY_Data);
    
    if (cdb->evpd || cdb->page_code) {
        /* We don't support either; so as stated in SPC, return CHECK CONDITION,
           ILLEGAL REQUEST and INVALID FIELD IN CDB */
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: invalid field in CDB\n", __func__);
        __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
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
    sprintf((gchar *)ret_data->vendor_id, "CDEmu   ");
    sprintf((gchar *)ret_data->product_id, "Virt. CD/DVD-ROM");
    sprintf((gchar *)ret_data->product_rev, "0.01");
    sprintf((gchar *)ret_data->vendor_spec1, "http:\\\\cdemu.sf.net");
    
    ret_data->ver_desc1 = GUINT16_TO_BE(0x02A0); /* We'll try to pass as MMC-3 device */
    
    /* Write data */
    __cdemud_device_write_buffer(self, GUINT16_FROM_BE(cdb->length));

    return TRUE;
}

/* MODE SELECT implementation */
static gboolean __cdemud_device_pc_mode_select (CDEMUD_Device *self, guint8 *raw_cdb) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    
    gint transfer_len = 0;
    gint sp = 0;
    gint pf = 0;
    
    /* MODE SELECT (6) vs MODE SELECT (10) */
    if (raw_cdb[0] == PC_MODE_SELECT_6) {
        struct MODE_SELECT_6_CDB *cdb = (struct MODE_SELECT_6_CDB *)raw_cdb;
        sp = cdb->sp;
        pf = cdb->pf;
        transfer_len = cdb->length;
    } else if (raw_cdb[0] == PC_MODE_SELECT_10) {
        struct MODE_SELECT_10_CDB *cdb = (struct MODE_SELECT_10_CDB *)raw_cdb;
        sp = cdb->sp;
        pf = cdb->pf;
        transfer_len = GUINT16_FROM_BE(cdb->length);
    } else {
        /* Because bad things happen to good people... :/ */
        CDEMUD_DEBUG(self, DAEMON_DEBUG_ERROR, "%s: someone called this function when they shouldn't have :/...\n", __func__);
        __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }
    
    /* Read the parameter list */
    __cdemud_device_read_buffer(self, transfer_len);
    
    /*if (CDEMUD_DEBUG_ON(self, DAEMON_DEBUG_DEV_PC_DUMP)) {
        gint i;
        g_print(">>> MODE SELECT DATA <<<\n");
        for (i = 0; i < transfer_len; i++) {
            g_print("0x%02X ", _priv->buffer[i]);
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
        struct MODE_SENSE_6_Header *header = (struct MODE_SENSE_6_Header *)_priv->buffer;
        blkdesc_len = header->blkdesc_len;
        offset = sizeof(struct MODE_SENSE_6_Header) + blkdesc_len;
    } else if (raw_cdb[0] == PC_MODE_SELECT_10) {
        struct MODE_SENSE_10_Header *header = (struct MODE_SENSE_10_Header *)_priv->buffer;
        blkdesc_len = GUINT16_FROM_BE(header->blkdesc_len);
        offset = sizeof(struct MODE_SENSE_10_Header) + blkdesc_len;
    }
    
    /* Someday when I'm in good mood I might implement this */
    if (blkdesc_len) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: block descriptor provided... but ATAPI devices shouldn't support that\n", __func__);
        __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_PARAMETER_LIST);
        return FALSE;
    }
    
    /* Take a peek at the byte following block descriptor data */
    gint page_size = transfer_len - offset;
    
    if (page_size) {
        struct ModePage_GENERAL *mode_page_new  = (struct ModePage_GENERAL *)(_priv->buffer+offset);
        struct ModePage_GENERAL *mode_page_mask = NULL;
        struct ModePage_GENERAL *mode_page_cur  = NULL;
        
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: mode page 0x%X\n", __func__, mode_page_new->code);
        
        /* Get pointer to current data */
        mode_page_cur = __cdemud_device_get_mode_page(self, mode_page_new->code, MODE_PAGE_CURRENT);
        if (!mode_page_cur) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: we don't have mode page 0x%X\n", __func__, mode_page_new->code);
            __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_PARAMETER_LIST);
            return FALSE;
        }
        
        /* Some length checking */
        if (page_size - 2 != mode_page_cur->length) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: declared page size doesn't match length of data we were given!\n", __func__);
            __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_PARAMETER_LIST);
            return FALSE;
        }
        
        /* Some more length checking */
        if (mode_page_new->length != mode_page_cur->length) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: invalid page size!\n", __func__);
            __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_PARAMETER_LIST);
            return FALSE;
        }
        
        /* Now we need to check if only values that can be changed are set */
        mode_page_mask = __cdemud_device_get_mode_page(self, mode_page_new->code, MODE_PAGE_MASK);
        guint8 *raw_data_new  = ((guint8 *)mode_page_new) + 2;
        guint8 *raw_data_mask = ((guint8 *)mode_page_mask) + 2;
        gint i;
        
        for (i = 0; i < mode_page_new->length; i++) {
            /* Compare every byte against the mask */
            if (raw_data_new[i] & ~raw_data_mask[i]) {
                CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: invalid value set on byte %i!\n", __func__, i);
                __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_PARAMETER_LIST);
                return FALSE;
            }
        }
        
        /* And finally, copy the page */
        memcpy(mode_page_cur, mode_page_new, mode_page_new->length + 2);
    }
    
    return TRUE;
}

/* MODE SENSE implementation */
static gboolean __cdemud_device_pc_mode_sense (CDEMUD_Device *self, guint8 *raw_cdb) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    
    gint page_code = 0;
    gint transfer_len = 0;
    gint pc = 0;
    
    /* MODE SENSE (6) vs MODE SENSE (10) */
    if (raw_cdb[0] == PC_MODE_SENSE_6) {
        struct MODE_SENSE_6_CDB *cdb = (struct MODE_SENSE_6_CDB *)raw_cdb;
            
        pc = cdb->pc;
        page_code = cdb->page_code;
        transfer_len = cdb->length;

        _priv->buffer_size = sizeof(struct MODE_SENSE_6_Header);
    } else if (raw_cdb[0] == PC_MODE_SENSE_10) {
        struct MODE_SENSE_10_CDB *cdb = (struct MODE_SENSE_10_CDB *)raw_cdb;
        
        pc = cdb->pc;
        page_code = cdb->page_code;
        transfer_len = GUINT16_FROM_BE(cdb->length);
        
        _priv->buffer_size = sizeof(struct MODE_SENSE_10_Header);
    } else {
        /* Because bad things happen to good people... :/ */
        CDEMUD_DEBUG(self, DAEMON_DEBUG_ERROR, "%s: someone called this function when they shouldn't have :/...\n", __func__);
        __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }
    guint8 *ret_data = _priv->buffer+_priv->buffer_size;
    
    /* We don't support saving mode pages */
    if (pc == 0x03) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: requested saved values; we don't support saving!\n", __func__);
        __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, SAVING_PARAMETERS_NOT_SUPPORTED);
        return FALSE;
    }
    
    /* Go over *all* pages, and if we want all pages, copy 'em all, otherwise 
       copy just the one we've got request for and break the loop */
    GList *entry = NULL;
    gboolean page_found = FALSE;
    G_LIST_FOR_EACH(entry, _priv->mode_pages_list) {
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
                    CDEMUD_DEBUG(self, DAEMON_DEBUG_ERROR, "%s: PC value is 0x%X and it shouldn't be!\n", __func__, pc);
                    break;
                }
            }
            
            memcpy(ret_data, mode_page, mode_page->length + 2);
            _priv->buffer_size += mode_page->length + 2;
            ret_data += mode_page->length + 2;
                        
            if (page_code != 0x3F) {
                CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: got the page we wanted (0x%X), breaking the loop\n", __func__, page_code);
                page_found = TRUE;
                break;
            }
        }
    }
    
    /* If we aren't returning all pages, check if page was found */
    if (page_code != 0x3F && !page_found) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: page 0x%X not found!\n", __func__, page_code);
        __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }
    
    /* Header; MODE SENSE (6) vs MODE SENSE (10) */
    if (raw_cdb[0] == PC_MODE_SENSE_6) {
        struct MODE_SENSE_6_Header *ret_header = (struct MODE_SENSE_6_Header *)_priv->buffer;
        ret_header->length = _priv->buffer_size - 2;
    } else if (raw_cdb[0] == PC_MODE_SENSE_10) {
        struct MODE_SENSE_10_Header *ret_header = (struct MODE_SENSE_10_Header *)_priv->buffer;
        ret_header->length = GUINT16_TO_BE(_priv->buffer_size - 2);
    }
    
    /* Write data */
    __cdemud_device_write_buffer(self, transfer_len);

    return TRUE;
}


/* PAUSE/RESUME implementation */
static gboolean __cdemud_device_pc_pause_resume (CDEMUD_Device *self, guint8 *raw_cdb) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    struct PAUSE_RESUME_CDB *cdb = (struct PAUSE_RESUME_CDB *)raw_cdb;
    gint audio_status = 0;
    
    cdemud_audio_get_status(CDEMUD_AUDIO(_priv->audio_play), &audio_status, NULL);
    
    /* Resume */
    if (cdb->resume == 1) {
        /* MMC-3 says that if we request resume and operation can't be resumed, 
           we return error (if we're already playing, it doesn't count as an
           error) */
        if ((audio_status != AUDIO_STATUS_PAUSED)
            && (audio_status != AUDIO_STATUS_PLAYING)) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: resume requested while in invalid state!\n", __func__);
            __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, COMMAND_SEQUENCE_ERROR);
            return FALSE;
        }
                
        /* Resume; we set status to playing, and fire the callback */
        if (audio_status != AUDIO_STATUS_PLAYING) {
            cdemud_audio_resume(CDEMUD_AUDIO(_priv->audio_play), NULL);
        }
    }
    
   
    if (cdb->resume == 0) {
        /* MMC-3 also says that we return error if pause is requested and the 
           operation can't be paused (being already paused doesn't count) */
        if ((audio_status != AUDIO_STATUS_PAUSED)
            && (audio_status != AUDIO_STATUS_PLAYING)) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: pause requested while in invalid state!\n", __func__);
            __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, COMMAND_SEQUENCE_ERROR);
            return FALSE;
        }   
                
       /* Pause; stop the playback and force status to AUDIO_STATUS_PAUSED */
        if (audio_status != AUDIO_STATUS_PAUSED) {
            cdemud_audio_pause(CDEMUD_AUDIO(_priv->audio_play), NULL);
        }
    }
    
    return TRUE;
}


/* PLAY AUDIO implementation */
static gboolean __cdemud_device_pc_play_audio (CDEMUD_Device *self, guint8 *raw_cdb) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    
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
        CDEMUD_DEBUG(self, DAEMON_DEBUG_ERROR, "%s: someone called this function when they shouldn't have :/...\n", __func__);
        __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }
    
     /* Check if we have medium loaded */
    if (!_priv->loaded) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: medium not present\n", __func__);
        __cdemud_device_write_sense(self, SK_NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }
    
    CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: playing from sector 0x%X to sector 0x%X\n", __func__, start_sector, end_sector);
    
    /* Play */
    if (!cdemud_audio_start(CDEMUD_AUDIO(_priv->audio_play), start_sector, end_sector, _priv->disc, NULL)) {
        /* FIXME: write sense */
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: failed to start audio play!\n", __func__);        
        return FALSE;
    }
    
    return TRUE;
}

/* PREVENT/ALLOW MEDIUM REMOVAL implementation */
static gboolean __cdemud_device_pc_prevent_allow_medium_removal (CDEMUD_Device *self, guint8 *raw_cdb) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    struct PREVENT_ALLOW_MEDIUM_REMOVAL_CDB *cdb = (struct PREVENT_ALLOW_MEDIUM_REMOVAL_CDB*)raw_cdb;
    struct ModePage_0x2A *p_0x2A = __cdemud_device_get_mode_page(self, 0x2A, MODE_PAGE_CURRENT);

    /* That's the locking, right? */
    if (cdb->prevent) {
        /* Lock */
        _priv->locked = 1;
        /* Indicate in the mode page that we're locked */
        p_0x2A->lock_state = 1;
    } else {
        /* Unlock */
        _priv->locked = 0;
        /* Indicate in the mode page that we're unlocked */
        p_0x2A->lock_state = 0;
    }
    
    return TRUE;
}

/* READ (10) and READ (12) implementation */
static gboolean __cdemud_device_pc_read (CDEMUD_Device *self, guint8 *raw_cdb) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    gint start_sector = 0; /* MUST be signed because it may be negative! */
    gint num_sectors  = 0;
        
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
        CDEMUD_DEBUG(self, DAEMON_DEBUG_ERROR, "%s: someone called this function when they shouldn't have :/...\n", __func__);
        __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }
    
    CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: read request; start sector: 0x%X, number of sectors: %d\n", __func__, start_sector, num_sectors);
    
    /* Check if we have medium loaded (because we use track later... >.<) */
    if (!_priv->loaded) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: medium not present\n", __func__);
        __cdemud_device_write_sense(self, SK_NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }
    MIRAGE_Disc *disc = MIRAGE_DISC(_priv->disc);
    
    gint sector = 0;    

    /* Process each sector */
    for (sector = start_sector; sector < start_sector + num_sectors; sector++) {
        GObject *cur_sector = NULL;
        mirage_disc_get_sector(disc, sector, &cur_sector, NULL);
        if (!cur_sector) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: invalid sector!\n", __func__);
            __cdemud_device_write_sense_full(self, SK_ILLEGAL_REQUEST, ILLEGAL_MODE_FOR_THIS_TRACK, 0, sector);
            return FALSE;
        }
        
        __cdemud_device_flush_buffer(self);
        
        /* READ 10/12 should support only sectors with 2048-byte user data */
        gint tmp_len = 0;
        guint8 *tmp_buf = NULL;
        guint8 *cache_ptr = _priv->buffer+_priv->buffer_size;
        
        mirage_sector_get_data(MIRAGE_SECTOR(cur_sector), &tmp_buf, &tmp_len, NULL);
        if (tmp_len != 2048) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: sector 0x%X does not have 2048-byte user data (%i)\n", __func__, sector, tmp_len);
            g_object_unref(cur_sector);
            __cdemud_device_write_sense_full(self, SK_ILLEGAL_REQUEST, ILLEGAL_MODE_FOR_THIS_TRACK, 1, sector);
            return FALSE;
        }
        
        memcpy(cache_ptr, tmp_buf, tmp_len);
        _priv->buffer_size += tmp_len;

        /* Needed for some other commands */
        _priv->current_sector = sector;
        /* Free sector */
        g_object_unref(cur_sector);
        /* Write sector */
        __cdemud_device_write_buffer(self, _priv->buffer_size);
    }
        
    return TRUE;
}


/* READ CAPACITY implementation */
static gboolean __cdemud_device_pc_read_capacity (CDEMUD_Device *self, guint8 *raw_cdb) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    /*struct READ_CAPACITY_CDB *cdb = (struct READ_CAPACITY_CDB *)raw_cdb;*/
    struct READ_CAPACITY_Data *ret_data = (struct READ_CAPACITY_Data *)_priv->buffer;
    _priv->buffer_size = sizeof(struct READ_CAPACITY_Data);
    
    gint last_sector = 0;
    
    if (!_priv->loaded) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: medium not present\n", __func__);
        __cdemud_device_write_sense(self, SK_NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }
    
    /* Capacity: starting sector of last leadout - 1 */
    GObject *lsession = NULL;
    GObject *leadout  = NULL;
    
    mirage_disc_get_session_by_index(MIRAGE_DISC(_priv->disc), -1, &lsession, NULL);
    mirage_session_get_track_by_number(MIRAGE_SESSION(lsession), MIRAGE_TRACK_LEADOUT, &leadout, NULL);
    mirage_track_layout_get_start_sector(MIRAGE_TRACK(leadout), &last_sector, NULL);
    
    g_object_unref(leadout);
    g_object_unref(lsession);
    
    last_sector -= 1;
    
    ret_data->lba = GUINT32_TO_BE(last_sector);
    ret_data->block_size = GUINT32_TO_BE(2048);
    
    /* Write data */
    __cdemud_device_write_buffer(self, _priv->buffer_size);
    
    return TRUE;
}


/* READ CD and READ CD MSF implementation */
static gboolean __cdemud_device_pc_read_cd (CDEMUD_Device *self, guint8 *raw_cdb) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    gint start_sector = 0; /* MUST be signed because it may be negative! */
    gint num_sectors = 0;
    gint exp_sect_type = 0;
    gint subchan_mode = 0;
        
    /* READ CD vs READ CD MSF */
    if (raw_cdb[0] == PC_READ_CD) {
        struct READ_CD_CDB *cdb = (struct READ_CD_CDB *)raw_cdb;
        
        start_sector = GUINT32_FROM_BE(cdb->lba);
        num_sectors = GUINT24_FROM_BE(cdb->length);
        
        exp_sect_type = __helper_map_expected_sector_type(cdb->sect_type);
        subchan_mode = cdb->subchan;
    } else if (raw_cdb[0] == PC_READ_CD_MSF) {
        struct READ_CD_MSF_CDB *cdb = (struct READ_CD_MSF_CDB *)raw_cdb;
        gint32 end_sector = 0;
        
        start_sector = mirage_helper_msf2lba(cdb->start_m, cdb->start_s, cdb->start_f, TRUE);
        end_sector = mirage_helper_msf2lba(cdb->end_m, cdb->end_s, cdb->end_f, TRUE);
        num_sectors = end_sector - start_sector;
        
        exp_sect_type = __helper_map_expected_sector_type(cdb->sect_type);
        subchan_mode = cdb->subchan;
    } else {
        /* Because bad things happen to good people... :/ */
        CDEMUD_DEBUG(self, DAEMON_DEBUG_ERROR, "%s: someone called this function when they shouldn't have :/...\n", __func__);
        __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }
    
    CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: READ CD:\n-> Address: 0x%08X\n-> Length: %i\n-> Expected sector (in libMirage type): 0x%X\n-> MCSB: 0x%X\n-> SubChannel: 0x%X\n",
        __func__, start_sector, num_sectors, exp_sect_type, raw_cdb[9], subchan_mode);
    
    
    /* Check if we have medium loaded */
    if (!_priv->loaded) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: medium not present\n", __func__);
        __cdemud_device_write_sense(self, SK_NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }
    
    /* Not supported for DVD-ROMs, right? */
    if (_priv->current_profile == PROFILE_DVDROM) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: READ CD not supported on DVD Media!\n", __func__);
        __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }
    
    /* Verify the requested subchannel mode ('readcd:18' uses READ CD with transfer
       length 0x00 to determine which subchannel modes are supported; without this
       clause, call with R-W subchannel passes, causing app choke on it later (when
       there's transfer length > 0x00 and thus subchannel is verified */
    if (subchan_mode == 0x04) {
        /* invalid subchannel requested (don't support R-W yet) */
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_FIXME, "%s: R-W subchannel reading not supported yet\n", __func__);
        __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }    
    
    MIRAGE_Disc* disc = MIRAGE_DISC(_priv->disc);
    GObject* first_sector = NULL;
    gint prev_sector_type = 0;
    
    gint sector = 0;
    
    /* Read first sector to determine its type */
    mirage_disc_get_sector(disc, start_sector, &first_sector, NULL);
    if (!first_sector) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: invalid starting sector!\n", __func__);
        __cdemud_device_write_sense_full(self, SK_ILLEGAL_REQUEST, ILLEGAL_MODE_FOR_THIS_TRACK, 0, sector);
        return FALSE;
    }
    mirage_sector_get_sector_type(MIRAGE_SECTOR(first_sector), &prev_sector_type, NULL);
    g_object_unref(first_sector);
    
    /* Process each sector */
    CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: start sector: 0x%X (%i); start + num: 0x%X (%i)\n", __func__, start_sector, start_sector, start_sector+num_sectors, start_sector+num_sectors);
    for (sector = start_sector; sector < start_sector + num_sectors; sector++) {
        GObject *cur_sector = NULL;
        
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: reading sector 0x%X (%i)\n", __func__, sector, sector);
        
        mirage_disc_get_sector(disc, sector, &cur_sector, NULL);
        if (!cur_sector) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: invalid sector!\n", __func__);
            __cdemud_device_write_sense_full(self, SK_ILLEGAL_REQUEST, ILLEGAL_MODE_FOR_THIS_TRACK, 0, sector);
            return FALSE;
        }
        
        __cdemud_device_flush_buffer(self);
        
        /* Expected sector stuff check... basically, if we have CDB->ExpectedSectorType
           set, we compare its translated value with our sector type, period. However, if
           it's 0, then "The Logical Unit shall always terminate a command at the sector 
           where a transition between CD-ROM and CD-DA data occurs." */
        gint cur_sector_type = 0;
        mirage_sector_get_sector_type(MIRAGE_SECTOR(cur_sector), &cur_sector_type, NULL);
        
        /* Break if current sector type doesn't match expected one*/
        if (exp_sect_type && (cur_sector_type != exp_sect_type)) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: expected sector type mismatch (expecting %i, got %i)!\n", __func__, exp_sect_type, cur_sector_type);
            __cdemud_device_write_sense_full(self, SK_ILLEGAL_REQUEST, ILLEGAL_MODE_FOR_THIS_TRACK, 1, sector);
            return FALSE;
        }
        /* Break if mode (sector type) has changed */
        if (prev_sector_type != cur_sector_type) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: previous sector type (%i) different from current one (%i)!\n", __func__, prev_sector_type, cur_sector_type);
            __cdemud_device_write_sense_full(self, SK_ILLEGAL_REQUEST, ILLEGAL_MODE_FOR_THIS_TRACK, 0, sector);
            return FALSE;
        }

        /* Map the MCSB: operation performed on raw Byte9 */
        if (__helper_map_mcsb(&raw_cdb[9], cur_sector_type) == -1) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: invalid MCSB: %X\n", __func__, raw_cdb[9]);
            __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
            return FALSE;
        }
            
        /* We read data here... we use reading method provided by libmirage, instead
           of copying all sector's data ourselves... yeah, it's a waste of resources
           to have sector created twice this way, but I hate to duplicate code...
           FIXME: find a better way to do it? */
        guint8 *ptr = _priv->buffer+_priv->buffer_size;
        gint read_length = 0;
        
        GError *read_err = NULL;
        if (!mirage_disc_read_sector(disc, sector, raw_cdb[9], subchan_mode, ptr, &read_length, &read_err)) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: failed to read sector 0x%X: %s\n", __func__, sector, read_err->message);
            __cdemud_device_write_sense_full(self, SK_ILLEGAL_REQUEST, ILLEGAL_MODE_FOR_THIS_TRACK, 0, sector);
            return FALSE;
        }
        
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: read length: 0x%X, buffer size: 0x%X\n", __func__, read_length, _priv->buffer_size);
        _priv->buffer_size += read_length;
        
        /* Previous sector type */
        prev_sector_type = cur_sector_type;
        /* Needed for some other commands */
        _priv->current_sector = sector;
        /* Free sector */
        g_object_unref(cur_sector);
        /* Write sector */
        __cdemud_device_write_buffer(self, _priv->buffer_size);
    }
    
    return TRUE;
}


/* READ DISC INFORMATION implementation */
static gboolean __cdemud_device_pc_read_disc_information (CDEMUD_Device *self, guint8 *raw_cdb) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    struct READ_DISC_INFORMATION_CDB *cdb = (struct READ_DISC_INFORMATION_CDB *)raw_cdb;
    
    /* Check if we have medium loaded */
    if (!_priv->loaded) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: medium not present\n", __func__);
        __cdemud_device_write_sense(self, SK_NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }
    
    switch (cdb->type) {
        case 0x000: {
            struct READ_DISC_INFORMATION_Data *ret_data = (struct READ_DISC_INFORMATION_Data *)_priv->buffer;
            _priv->buffer_size = sizeof(struct READ_DISC_INFORMATION_Data);
        
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_FIXME, "%s: standard disc information\n", __func__);
            
            ret_data->length = GUINT16_TO_BE(_priv->buffer_size - 2);
            ret_data->lsession_state = 0x03; /* complete */
            ret_data->disc_status = 0x02; /* complete */
            ret_data->ftrack_disc = 0x01;
            
            MIRAGE_Disc *disc = MIRAGE_DISC(_priv->disc);
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
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_FIXME, "%s: data type 0x%X not supported!\n", __func__, cdb->type);
            __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
            return FALSE;
        }
    }
    
    /* Write data */
    __cdemud_device_write_buffer(self, _priv->buffer_size);
    
    return TRUE;
}


/* READ DVD STRUCTURE implementation */
static gboolean __cdemud_device_pc_read_dvd_structure (CDEMUD_Device *self, guint8 *raw_cdb) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    struct READ_DVD_STRUCTURE_CDB *cdb = (struct READ_DVD_STRUCTURE_CDB *)raw_cdb;
    struct READ_DVD_STRUCTURE_Header *head = (struct READ_DVD_STRUCTURE_Header *)_priv->buffer;
    _priv->buffer_size = sizeof(struct READ_DVD_STRUCTURE_Header);

    /* Check if we have medium loaded */
    if (!_priv->loaded) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: medium not present\n", __func__);
        __cdemud_device_write_sense(self, SK_NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }
    
    if (_priv->current_profile != PROFILE_DVDROM) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: READ DVD STRUCTURE is supported only with DVD media\n", __func__);
        __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, CANNOT_READ_MEDIUM_INCOMPATIBLE_FORMAT);
        return FALSE;
    }
    
    /* Try to get the structure */
    guint8 *tmp_data = NULL;
    gint tmp_len = 0;
    
    CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: requested structure: 0x%X; layer: %d\n", __func__, cdb->format, cdb->layer);
    if (!mirage_disc_get_disc_structure(MIRAGE_DISC(_priv->disc), cdb->layer, cdb->format, &tmp_data, &tmp_len, NULL)) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: structure not present on disc!\n", __func__);
        __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }
    
    memcpy(_priv->buffer+sizeof(struct READ_DVD_STRUCTURE_Header), tmp_data, tmp_len);
    _priv->buffer_size += tmp_len;
    
    g_free(tmp_data);
    
    /* Header */
    head->length = GUINT16_TO_BE(_priv->buffer_size - 2);
    
    /* Write data */
    __cdemud_device_write_buffer(self, GUINT16_FROM_BE(cdb->length));
    
    return TRUE;
}


/* READ SUB-CHANNEL implementation */
static gboolean __cdemud_device_pc_read_subchannel (CDEMUD_Device *self, guint8 *raw_cdb) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    struct READ_SUBCHANNEL_CDB *cdb = (struct READ_SUBCHANNEL_CDB *)raw_cdb;
    struct READ_SUBCHANNEL_Header *ret_header = (struct READ_SUBCHANNEL_Header *)_priv->buffer;
    _priv->buffer_size = sizeof(struct READ_SUBCHANNEL_Header);
    
    MIRAGE_Disc *disc = MIRAGE_DISC(_priv->disc);
    
    /* Check if we have medium loaded */
    if (!_priv->loaded) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: medium not present\n", __func__);
        __cdemud_device_write_sense(self, SK_NOT_READY, MEDIUM_NOT_PRESENT);
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
                struct READ_SUBCHANNEL_Data1 *ret_data = (struct READ_SUBCHANNEL_Data1 *)(_priv->buffer+_priv->buffer_size);
                _priv->buffer_size += sizeof(struct READ_SUBCHANNEL_Data1);
                
                CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: current position (sector 0x%X)\n", __func__, _priv->current_sector);
                ret_data->fmt_code = 0x01;
                
                /* Read current sector's PQ subchannel */
                guint8 tmp_buf[16];
                mirage_disc_read_sector(MIRAGE_DISC(_priv->disc), _priv->current_sector, 0x00, MIRAGE_SUBCHANNEL_PQ, tmp_buf, NULL, NULL);
                
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
                while((tmp_buf[0] & 0x0F) != 0x01) {
                    CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: got a sector that's not Mode-1 Q; taking next one!\n", __func__);
                    /* Read from next sector */
                    mirage_disc_read_sector(MIRAGE_DISC(_priv->disc), _priv->current_sector+correction, 0x00, MIRAGE_SUBCHANNEL_PQ, tmp_buf, NULL, NULL);
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
                struct READ_SUBCHANNEL_Data2 *ret_data = (struct READ_SUBCHANNEL_Data2 *)(_priv->buffer+_priv->buffer_size);
                _priv->buffer_size += sizeof(struct READ_SUBCHANNEL_Data2);

                CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: MCN/UPC/EAN\n", __func__);
                ret_data->fmt_code = 0x02;
                
                /* Go over first 100 sectors; if MCN is present, it should be there */
                gint sector = 0;
                for (sector = 0; sector < 100; sector++) {
                    guint8 tmp_buf[16];
                    
                    if (!mirage_disc_read_sector(MIRAGE_DISC(_priv->disc), sector, 0, MIRAGE_SUBCHANNEL_PQ, tmp_buf, NULL, NULL)) {
                        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: failed to read subchannel of sector 0x%X\n", __func__, sector);
                        continue;
                    }
                    
                    if ((tmp_buf[0] & 0x0F) == 0x02) {
                        /* Mode-2 Q found */
                        mirage_helper_subchannel_q_decode_mcn(&tmp_buf[1], (gchar *)ret_data->mcn);
                        ret_data->mcval = 1;
                        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: found MCN in subchannel of sector 0x%X: <%.13s>\n", __func__, sector, ret_data->mcn);                        
                        break;
                    }
                }

                break;
            }
            case 0x03: {
                /* ISRC */
                struct READ_SUBCHANNEL_Data3 *ret_data = (struct READ_SUBCHANNEL_Data3 *)(_priv->buffer+_priv->buffer_size);
                _priv->buffer_size += sizeof(struct READ_SUBCHANNEL_Data3);

                CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: ISRC\n", __func__);
                ret_data->fmt_code = 0x03;

                GObject *track = NULL;
                if (!mirage_disc_get_track_by_number(disc, cdb->track, &track, NULL)) {
                    CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: failed to get track %i!\n", __func__, cdb->track); 
                    __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
                    return FALSE;
                }
                
                /* Go over first 100 sectors; if ISRC is present, it should be there */
                gint sector = 0;
                for (sector = 0; sector < 100; sector++) {
                    guint8 tmp_buf[16];
                    
                    if (!mirage_track_read_sector(MIRAGE_TRACK(track), sector, FALSE, 0, MIRAGE_SUBCHANNEL_PQ, tmp_buf, NULL, NULL)) {
                        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: failed to read subchannel of sector 0x%X\n", __func__, sector);
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
                        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: found ISRC in subchannel of sector 0x%X: <%.12s>\n", __func__, sector, ret_data->isrc);
                        break;
                    }
                }
                
                g_object_unref(track);
                
                break;
            }
            default: {
                CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: unknown!\n", __func__);
                break;
            }
        }
    }

    /* Header */
    gint audio_status = 0;
    cdemud_audio_get_status(CDEMUD_AUDIO(_priv->audio_play), &audio_status, NULL); 
    ret_header->audio_status = audio_status; /* Audio status */
    ret_header->length = GUINT32_TO_BE(_priv->buffer_size - 4);
    
    /* Write data */
    __cdemud_device_write_buffer(self, GUINT16_FROM_BE(cdb->length));
    
    return TRUE;
}


/* READ TOC/PMA/ATIP implementation */
static gboolean __cdemud_device_pc_read_toc_pma_atip (CDEMUD_Device *self, guint8 *raw_cdb) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    struct READ_TOC_PMA_ATIP_CDB *cdb = (struct READ_TOC_PMA_ATIP_CDB *)raw_cdb;
        
    if (!_priv->loaded) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: medium not present\n", __func__);
        __cdemud_device_write_sense(self, SK_NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }
    
    /* MMC: No fabrication for DVD media is defined for forms other than 000b and 001b. */
    if ((_priv->current_profile == PROFILE_DVDROM) && !((cdb->format == 0x00) || (cdb->format == 0x01))) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: invalid format type (0x%X) for DVD-ROM image!\n", __func__, cdb->format);
        __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }
    
    MIRAGE_Disc *disc = MIRAGE_DISC(_priv->disc);
    
    /* Alcohol 120% was being a PITA claiming I was feeding it 'empty disc'...
       upon checking INF-8020, it turns out what MMC-3 specifies as control byte
       is actually used... so we do compatibility mapping here */
    if (cdb->format == 0) {
        if (cdb->control == 0x40) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: compliance to INF-8020 obviously expected... playing along\n", __func__);
            cdb->format = 0x01;
        }
        if (cdb->control == 0x80) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: compliance to INF-8020 obviously expected... playing along\n", __func__);
            cdb->format = 0x02;
        }
    }
    
    switch (cdb->format) {
        case 0x00: {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: formatted TOC\n", __func__);
            /* Formatted TOC */            
            struct READ_TOC_PMA_ATIP_0000_Header *ret_header = (struct READ_TOC_PMA_ATIP_0000_Header *)_priv->buffer;
            _priv->buffer_size = sizeof(struct READ_TOC_PMA_ATIP_0000_Header);
            struct READ_TOC_PMA_ATIP_0000_Descriptor *ret_desc = (struct READ_TOC_PMA_ATIP_0000_Descriptor *)(_priv->buffer+_priv->buffer_size);
                        
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
                    CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: starting track number (%i) exceeds last track number (%i)!\n", __func__, cdb->number, nr_tracks);
                    __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
                    return FALSE;
                }
                
                
                mirage_disc_get_number_of_tracks(disc, &nr_tracks, NULL);
                
                for (i = 0; i < nr_tracks; i++) {
                    if (!mirage_disc_get_track_by_index(disc, i, &cur_track, NULL)) {
                        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: failed to get track with index %i (whole disc)!\n", __func__, i);
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
                                                
                        _priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_0000_Descriptor);
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
            _priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_0000_Descriptor);

            g_object_unref(cur_track);
            
            /* Header */
            gint ltrack = 0;
            mirage_session_get_track_by_index(MIRAGE_SESSION(lsession), -1, &cur_track, NULL);
            mirage_track_layout_get_track_number(MIRAGE_TRACK(cur_track), &ltrack, NULL);
                        
            g_object_unref(cur_track);
            g_object_unref(lsession);
            
            ret_header->length = GUINT16_TO_BE(_priv->buffer_size - 2);
            ret_header->ftrack = 0x01;
            ret_header->ltrack = ltrack;
            
            break;
        }
        case 0x01: {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: multisession information\n", __func__);
            /* Multi-session info */
            struct READ_TOC_PMA_ATIP_0001_Data *ret_data = (struct READ_TOC_PMA_ATIP_0001_Data *)_priv->buffer;
            _priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_0001_Data);
            
            GObject *lsession = NULL;
            GObject *ftrack = NULL;
            
            mirage_disc_get_session_by_index(disc, -1, &lsession, NULL);
            
            /* Header */
            ret_data->length = GUINT16_TO_BE(_priv->buffer_size - 2);
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
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: raw TOC\n", __func__);
            /* Raw TOC */
            struct READ_TOC_PMA_ATIP_0010_Header *ret_header = (struct READ_TOC_PMA_ATIP_0010_Header *)_priv->buffer;
            _priv->buffer_size = sizeof(struct READ_TOC_PMA_ATIP_0010_Header);
            struct READ_TOC_PMA_ATIP_0010_Descriptor *ret_desc = (struct READ_TOC_PMA_ATIP_0010_Descriptor *)(_priv->buffer+_priv->buffer_size);
            
            gint i,j;
            
            /* For each session with number above the requested one... */
            gint nr_sessions = 0;
            mirage_disc_get_number_of_sessions(disc, &nr_sessions, NULL);
            for (i = 0; i < nr_sessions; i++) {
                GObject *cur_session = NULL;
                gint session_nr = 0;
                
                if (!mirage_disc_get_session_by_index(disc, i, &cur_session, NULL)) {
                    CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: failed to get session by index %i!\n", __func__, i);
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
                    
                    _priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_0010_Descriptor);
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

                    _priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_0010_Descriptor);
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
        
                    _priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_0010_Descriptor);
                    ret_desc++;
                                        
                    /* And now one TOC descriptor per track */
                    gint nr_tracks = 0;
                    mirage_session_get_number_of_tracks(MIRAGE_SESSION(cur_session), &nr_tracks, NULL);
                    
                    for (j = 0; j < nr_tracks; j++) {
                        if (!mirage_session_get_track_by_index(MIRAGE_SESSION(cur_session), j, &cur_track, NULL)) {
                            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: failed to get track with index %i in session %i!\n", __func__, j, session_nr);
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
        
                        _priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_0010_Descriptor);
                        ret_desc++;
                        g_object_unref(cur_track);
                    }
                    
                    /* If we're dealing with multisession disc, it'd probably be
                       a good idea to come up with B0 descriptors... */
                    if (nr_sessions > 1) {
                        gint leadout_length = 0;
                        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: multisession disc; cooking up a B0 descriptor for session %i!\n", __func__, session_nr);
                        
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
                        
                        _priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_0010_Descriptor);
                        ret_desc++;
                        
                        /* Add up C0 for session 1 */
                        if (session_nr == 1) {
                            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: multisession disc; cooking up a C0 descriptor for session %i!\n", __func__, session_nr);
                            
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
                            
                            _priv->buffer_size += sizeof(struct READ_TOC_PMA_ATIP_0010_Descriptor);
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

            ret_header->length = GUINT16_TO_BE(_priv->buffer_size - 2);
            ret_header->fsession = 0x01;
            ret_header->lsession = lsession_nr;
            
            break;
        }
        case 0x04: {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: ATIP\n", __func__);
            /* ATIP */
            struct READ_TOC_PMA_ATIP_0100_Header *ret_header = (struct READ_TOC_PMA_ATIP_0100_Header *)_priv->buffer;
            _priv->buffer_size = sizeof(struct READ_TOC_PMA_ATIP_0100_Header);
            
            /* Header */
            ret_header->length = GUINT16_TO_BE(_priv->buffer_size - 2);
            
            break;
        }
        case 0x05: {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: CD-Text\n", __func__);
            /* CD-TEXT */
            struct READ_TOC_PMA_ATIP_0101_Header *ret_header = (struct READ_TOC_PMA_ATIP_0101_Header *)_priv->buffer;
            _priv->buffer_size = sizeof(struct READ_TOC_PMA_ATIP_0101_Header);
            
            guint8 *tmp_data = NULL;
            gint tmp_len  = 0;
            GObject *session = NULL;
            
            /* FIXME: for the time being, return data for first session */
            mirage_disc_get_session_by_index(MIRAGE_DISC(_priv->disc), 0, &session, NULL);
            if (!mirage_session_get_cdtext_data(MIRAGE_SESSION(session), &tmp_data, &tmp_len, NULL)) {
                CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: failed to get CD-TEXT data!\n", __func__);
            }
            g_object_unref(session);
            
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: length of CD-TEXT data: 0x%X\n", __func__, tmp_len);
            
            memcpy(_priv->buffer+sizeof(struct READ_TOC_PMA_ATIP_0101_Header), tmp_data, tmp_len);
            g_free(tmp_data);
            _priv->buffer_size += tmp_len;
            
            /* Header */
            ret_header->length = GUINT16_TO_BE(_priv->buffer_size - 2);
            
            break;
        }
        default: {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_FIXME, "%s: format %X not suppoted yet\n", __func__, cdb->format);
            __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
            return FALSE;
        }    
    }
    
    /* Write data */
    __cdemud_device_write_buffer(self, GUINT16_FROM_BE(cdb->length));
    
    return TRUE;
}


/* READ TRACK INFORMATION implementation */
static gboolean __cdemud_device_pc_read_track_information (CDEMUD_Device *self, guint8 *raw_cdb) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    struct READ_TRACK_INFORMATION_CDB *cdb = (struct READ_TRACK_INFORMATION_CDB *)raw_cdb;
    struct READ_TRACK_INFORMATION_Data *ret_data = (struct READ_TRACK_INFORMATION_Data *)_priv->buffer;
    _priv->buffer_size = sizeof(struct READ_TRACK_INFORMATION_Data);
    
    MIRAGE_Disc *disc = MIRAGE_DISC(_priv->disc);
    GObject *track = NULL;
        
    if (!_priv->loaded) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: medium not present\n", __func__);
        __cdemud_device_write_sense(self, SK_NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }
    
    gint number = GUINT32_FROM_BE(cdb->number);
    
    switch (cdb->type) {
        case 0x00: {
            /* LBA */
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: requested track containing sector 0x%X\n", __func__, number);
            mirage_disc_get_track_by_address(disc, number, &track, NULL);
            break;
        }
        case 0x01: {
            /* Lead-in/Track/Invisible track */
            switch (number) {
                case 0x00: {
                    /* Lead-in: not supported */
                    CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: requested lead-in; not supported!\n", __func__);
                    __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
                    return FALSE;
                }
                case 0xFF: {
                    /* Invisible/Incomplete track: not supported */
                    CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: requested invisible/incomplete track; not supported!\n", __func__);
                    __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
                    return FALSE;
                }
                default: {
                    /* Track number */
                    CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: requested track %i\n", __func__, number);
                    mirage_disc_get_track_by_number(disc, number, &track, NULL);
                    break;
                }
            }
            break;
        }
        case 0x02: {
            /* Session number */
            GObject *session = NULL;
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: requested first track in session %i\n", __func__, number);
            mirage_disc_get_session_by_number(disc, number, &session, NULL);
            mirage_session_get_track_by_index(MIRAGE_SESSION(session), 0, &track, NULL);
            g_object_unref(session);
            break;
        }
    }
    
    /* Check if track was found */
    if (!track) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: couldn't find track!\n", __func__);
        __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
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
    
    ret_data->length = GUINT16_TO_BE(_priv->buffer_size - 2);
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
    __cdemud_device_write_buffer(self, GUINT16_FROM_BE(cdb->length));
    
    return TRUE;
}


/* REPORT KEY implementation */
static gboolean __cdemud_device_pc_report_key (CDEMUD_Device *self, guint8 *raw_cdb) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    struct REPORT_KEY_CDB *cdb = (struct REPORT_KEY_CDB *)raw_cdb;
    
    if (cdb->key_format == 0x08) {
        /* RPC */
        struct REPORT_KEY_001000_Data *data = (struct REPORT_KEY_001000_Data *)_priv->buffer;
        _priv->buffer_size = sizeof(struct REPORT_KEY_001000_Data);
        
        data->type_code = 0; /* No region setting */
        data->vendor_resets = 4;
        data->user_changes = 5;
        data->region_mask = 0xFF; /* It's what all my drives return... */
        data->rpc_scheme = 1; /* It's what all my drives return... */
        
        data->length = GUINT16_TO_BE(_priv->buffer_size - 2);
    } else {
        if (_priv->current_profile != PROFILE_DVDROM) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: not supported with non-DVD media!\n", __func__);
            __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, CANNOT_READ_MEDIUM_INCOMPATIBLE_FORMAT);
            return FALSE;
        }
        
        /* We don't support these yet */
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: FIXME: not implemented yet!\n", __func__);
        __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB);
        return FALSE;
    }
    
    /* Write data */
    __cdemud_device_write_buffer(self, GUINT16_FROM_BE(cdb->length));
    
    return TRUE;
}


/* REQUEST SENSE implementation */
static gboolean __cdemud_device_pc_request_sense (CDEMUD_Device *self, guint8 *raw_cdb) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    struct REQUEST_SENSE_CDB *cdb = (struct REQUEST_SENSE_CDB *)raw_cdb;
    struct REQUEST_SENSE_SenseFixed *sense = (struct REQUEST_SENSE_SenseFixed *)_priv->buffer;
    _priv->buffer_size = sizeof(struct REQUEST_SENSE_SenseFixed);
    
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
    cdemud_audio_get_status(CDEMUD_AUDIO(_priv->audio_play), &audio_status, NULL); 
    
    sense->sense_key = SK_NO_SENSE;
    sense->asc = NO_ADDITIONAL_SENSE_INFORMATION;
    sense->ascq = audio_status; 
    
    /* Write data */
    __cdemud_device_write_buffer(self, cdb->length);
    
    return TRUE;
}

/* SEEK (10) implementation */
static gboolean __cdemud_device_pc_seek (CDEMUD_Device *self, guint8 *raw_cdb) {
    /*CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    struct SET_CD_SPEED_CDB *cdb = (struct SET_CD_SPEED_CDB *)raw_cdb;*/
    
    CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_FIXME, "%s: nothing to do here yet...\n", __func__);
    
    return TRUE;
}

/* SET CD SPEED implementation */
static gboolean __cdemud_device_pc_set_cd_speed (CDEMUD_Device *self, guint8 *raw_cdb) {
    /*CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);*/
    struct SET_CD_SPEED_CDB *cdb = (struct SET_CD_SPEED_CDB *)raw_cdb;
    struct ModePage_0x2A *p_0x2A = __cdemud_device_get_mode_page(self, 0x2A, MODE_PAGE_CURRENT);
    
    /* Set the value to mode page and do nothing else at the moment...
       Note that we don't have to convert from BE neither for comparison (because 
       it's 0xFFFF and unsigned short) nor when setting value (because it's BE in 
       mode page anyway) */
    if (cdb->read_speed == 0xFFFF) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: setting read speed to max\n", __func__);
        p_0x2A->cur_read_speed = p_0x2A->max_read_speed;
    } else {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: setting read speed to %i kB/s\n", __func__, GUINT16_FROM_BE(cdb->read_speed));
        p_0x2A->cur_read_speed = cdb->read_speed;
    }
    
    return TRUE;
}


/* START/STOP unit implementation */
static gboolean __cdemud_device_pc_start_stop_unit (CDEMUD_Device *self, guint8 *raw_cdb) {
    /*CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);*/
    struct START_STOP_UNIT_CDB *cdb = (struct START_STOP_UNIT_CDB *)raw_cdb;
    
    if (cdb->lo_ej) {
        if (!cdb->start) {
            cdemud_device_unload_disc(self, NULL);
        }
    }
    
    return TRUE;
}


/* TEST UNIT READY implementation */
static gboolean __cdemud_device_pc_test_unit_ready (CDEMUD_Device *self, guint8 *raw_cdb) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    /*struct TEST_UNIT_READY_CDB *cdb = (struct TEST_UNIT_READY_CDB *)raw_cdb;*/
    
    /* Check if we have medium loaded */
    if (!_priv->loaded) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: medium not present\n", __func__);
        __cdemud_device_write_sense(self, SK_NOT_READY, MEDIUM_NOT_PRESENT);
        return FALSE;
    }
    
    /* SCSI requires us to report UNIT ATTENTION with NOT READY TO READY CHANGE, 
       MEDIUM MAY HAVE CHANGED whenever medium changes... this is required for
       linux SCSI layer to set medium block size properly upon disc insertion */
    if (_priv->media_changed) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: reporting media changed\n", __func__);
	    _priv->media_changed = FALSE;
        __cdemud_device_write_sense(self, SK_UNIT_ATTENTION, NOT_READY_TO_READY_CHANGE_MEDIUM_MAY_HAVE_CHANGED);
        return FALSE;
    }
    
    return TRUE;
}

/* Packet command switch */
gint cdemud_device_execute(CDEMUD_Device *self, CDEMUD_Command *cmd) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    
    gint status = STATUS_CHECK_CONDITION;
    
    _priv->cmd = cmd;
    _priv->cur_len = 0;
    
    /* Flush buffer */
    __cdemud_device_flush_buffer(self);
         
    CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_DUMP, "%s: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", __func__, 
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
          __cdemud_device_pc_get_event_status_notification, 
          FALSE },
        { PC_GET_CONFIGURATION,
          "GET CONFIGURATION",
          __cdemud_device_pc_get_configuration,
          TRUE, },
        { PC_INQUIRY,
          "INQUIRY",
          __cdemud_device_pc_inquiry, 
          FALSE },
        { PC_MODE_SELECT_6,
          "MODE SELECT (6)",
          __cdemud_device_pc_mode_select,
          TRUE },
        { PC_MODE_SELECT_10,
          "MODE SELECT (10)",
          __cdemud_device_pc_mode_select,
          TRUE },
        { PC_MODE_SENSE_6,
          "MODE SENSE (6)",
          __cdemud_device_pc_mode_sense,
          TRUE },
        { PC_MODE_SENSE_10,
          "MODE SENSE (10)",
          __cdemud_device_pc_mode_sense,
          TRUE },
        { PC_PAUSE_RESUME,
          "PAUSE/RESUME",
          __cdemud_device_pc_pause_resume,
          FALSE /* Well, it does... but in it's own, unique way :P */ },
        { PC_PLAY_AUDIO_10,
          "PLAY AUDIO (10)",
          __cdemud_device_pc_play_audio,
          TRUE },
        { PC_PLAY_AUDIO_12,
          "PLAY AUDIO (12)",
          __cdemud_device_pc_play_audio,
          TRUE },
        { PC_PLAY_AUDIO_MSF,
          "PLAY AUDIO MSF",
          __cdemud_device_pc_play_audio,
          TRUE },
        { PC_PREVENT_ALLOW_MEDIUM_REMOVAL, 
          "PREVENT/ALLOW MEDIUM REMOVAL",
          __cdemud_device_pc_prevent_allow_medium_removal, 
          TRUE },
        { PC_READ_10, 
          "READ (10)",
          __cdemud_device_pc_read, 
          TRUE },
        { PC_READ_12, 
          "READ (12)",
          __cdemud_device_pc_read, 
          TRUE },
        { PC_READ_CAPACITY, 
          "READ CAPACITY",
          __cdemud_device_pc_read_capacity, 
          FALSE },
        { PC_READ_CD,
          "READ CD",
          __cdemud_device_pc_read_cd, 
          TRUE },
        { PC_READ_CD_MSF, 
          "READ CD MSF",
          __cdemud_device_pc_read_cd,
          TRUE },
        { PC_READ_DISC_INFORMATION,
          "READ DISC INFORMATION",
          __cdemud_device_pc_read_disc_information, 
          TRUE },
        { PC_READ_DVD_STRUCTURE,
          "READ DISC STRUCTURE",
          __cdemud_device_pc_read_dvd_structure,
          TRUE },
        { PC_READ_TOC_PMA_ATIP,
          "READ TOC/PMA/ATIP",
          __cdemud_device_pc_read_toc_pma_atip, 
          TRUE },
        { PC_READ_TRACK_INFORMATION,
          "READ TRACK INFORMATION",
          __cdemud_device_pc_read_track_information, 
          TRUE },
        { PC_READ_SUBCHANNEL,
          "READ SUBCHANNEL",
          __cdemud_device_pc_read_subchannel, 
          FALSE },
        { PC_REPORT_KEY,
          "REPORT KEY",
          __cdemud_device_pc_report_key,
          TRUE },
        { PC_REQUEST_SENSE,
          "REQUEST SENSE",
          __cdemud_device_pc_request_sense, 
          FALSE },
        { PC_SEEK_10,
          "SEEK (10)",
          __cdemud_device_pc_seek,
          FALSE },
        { PC_SET_CD_SPEED,
          "SET CD SPEED",
          __cdemud_device_pc_set_cd_speed, 
          TRUE },
        { PC_START_STOP_UNIT,
          "START/STOP UNIT",
          __cdemud_device_pc_start_stop_unit, 
          TRUE },
        { PC_TEST_UNIT_READY,
          "TEST UNIT READY",
          __cdemud_device_pc_test_unit_ready, 
          FALSE },
    };
    
    /* Find the command and execute its implementation handler */
    for (i = 0; i < G_N_ELEMENTS(packet_commands); i++) {
        if (packet_commands[i].cmd == cmd->cdb[0]) {
            gboolean succeeded = FALSE;
            
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_DUMP, "%s: command: %s\n", __func__, packet_commands[i].debug_name);
            
            /* Lock */
            g_static_mutex_lock(&_priv->device_mutex);
            
            /* FIXME: If there is deferred error sense available, return CHECK CONDITION
               with that sense. We do not execute requested command. */
            
            /* Stop audio play if command disturbs it */
            if (packet_commands[i].disturbs_audio_play) {
                gint status = 0;
                cdemud_audio_get_status(CDEMUD_AUDIO(_priv->audio_play), &status, NULL);
                if (status == AUDIO_STATUS_PLAYING || status == AUDIO_STATUS_PAUSED) {
                    cdemud_audio_stop(CDEMUD_AUDIO(_priv->audio_play), NULL);
                }
            }
            /* Execute the command */
            succeeded = packet_commands[i].implementation(self, cmd->cdb);
            status = (succeeded) ? STATUS_GOOD : STATUS_CHECK_CONDITION;
            _priv->cmd->out_len = _priv->cur_len;

            /* Unlock */
            g_static_mutex_unlock(&_priv->device_mutex);
            
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: command completed with status %d\n\n", __func__, status);
            
            return status;
        }
    }
    
    /* Command not found */
    CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_FIXME, "%s: packet command %02Xh not implemented yet!\n\n", __func__, cmd->cdb[0]);
    __cdemud_device_write_sense(self, SK_ILLEGAL_REQUEST, INVALID_COMMAND_OPERATION_CODE);
    _priv->cmd->out_len = _priv->cur_len;

    return status;
}


/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
gboolean cdemud_device_initialize (CDEMUD_Device *self, gint number, gchar *audio_backend, gchar *audio_device, GObject *mirage, GError **error) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    GObject *debug_context = NULL;
    
    /* Set device number and device name */
    _priv->number = number;
    _priv->device_name = g_strdup_printf("cdemu%i", number);
    
    /* Init device mutex */
    g_static_mutex_init(&_priv->device_mutex);
    
    /* Set mirage object */
    _priv->mirage = mirage;
    g_object_ref(_priv->mirage);
    
    /* Create debug context for device */
    debug_context = g_object_new(MIRAGE_TYPE_DEBUG_CONTEXT, NULL);
    mirage_debug_context_set_name(MIRAGE_DEBUG_CONTEXT(debug_context), _priv->device_name, NULL);
    mirage_debug_context_set_domain(MIRAGE_DEBUG_CONTEXT(debug_context), "CDEMUD", NULL);
    mirage_object_set_debug_context(MIRAGE_OBJECT(self), debug_context, NULL);
    g_object_unref(debug_context);
    
    /* Allocate buffer/"cache"; 4kB should be enough for everything, I think */
    _priv->buffer = g_malloc0(4096);
    if (!_priv->buffer) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to allocate buffer!\n", __func__);
        cdemud_error(CDEMUD_E_BUFFER, error);
        return FALSE;
    }
    
    /* Create audio backend object */
    gint i;
    for (i = 0; i < G_N_ELEMENTS(audio_backends); i++) {
        if (audio_backend && !strcmp(audio_backend, audio_backends[i].name)) {
            _priv->audio_play = g_object_new(audio_backends[i].get_type_func(), NULL);
            break;
        }
    }
    if (!_priv->audio_play) {
        _priv->audio_play = g_object_new(CDEMUD_TYPE_AUDIO_NULL, NULL);
        if (!_priv->audio_play) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to create audio backend!\n", __func__);
            cdemud_error(CDEMUD_E_AUDIOBACKEND, error);
            return FALSE;
        }
    }
    /* Set parent */
    mirage_object_set_parent(MIRAGE_OBJECT(_priv->audio_play), G_OBJECT(self), NULL);
    /* Attach child... so that it'll get device's debug context */
    mirage_object_attach_child(MIRAGE_OBJECT(self), _priv->audio_play, NULL);
    /* Initialize */
    if (!cdemud_audio_initialize(CDEMUD_AUDIO(_priv->audio_play), audio_device, &_priv->current_sector, error)) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to initialize audio backend!\n", __func__);
        return FALSE;
    }
    
    /* Create debug context for disc */
    _priv->disc_debug = g_object_new(MIRAGE_TYPE_DEBUG_CONTEXT, NULL);
    mirage_debug_context_set_name(MIRAGE_DEBUG_CONTEXT(_priv->disc_debug), _priv->device_name, NULL);
    mirage_debug_context_set_domain(MIRAGE_DEBUG_CONTEXT(_priv->disc_debug), "libMirage", NULL);
        
    /* Initialise mode pages and features and set profile */
    __cdemud_device_init_mode_pages(self);
    __cdemud_device_init_features(self);
    __cdemud_device_set_profile(self, PROFILE_NONE);
    
    /* We successfully finished initialisation */
    _priv->initialized = TRUE;
    
    return TRUE;
}

gboolean cdemud_device_get_device_number (CDEMUD_Device *self, gint *number, GError **error) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    
    CDEMUD_CHECK_ARG(number);
    *number = _priv->number;
    
    return TRUE;
}

gboolean cdemud_device_get_status (CDEMUD_Device *self, gboolean *loaded, gchar **image_type, gchar ***file_names, GError **error) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    
    gboolean _loaded = FALSE;
    gchar *_image_type = NULL;
    gchar **_file_names = NULL;
    
    /* Lock */
    g_static_mutex_lock(&_priv->device_mutex);
    
    if (_priv->loaded) {
        MIRAGE_Disc *disc = MIRAGE_DISC(_priv->disc);
        MIRAGE_ParserInfo *parser_info = NULL;
        
        _loaded = TRUE;
        mirage_disc_get_filenames(disc, &_file_names, NULL);
        mirage_disc_get_parser_info(disc, &parser_info, NULL);
        _image_type = g_strdup(parser_info->id);
    } else {
        _loaded = FALSE;
        _image_type = g_strdup("N/A");
        _file_names = g_new0(gchar*, 2); /* NULL-terminated, hence 2 */
        _file_names[0] = g_strdup("N/A");
    }
    
    if (loaded) {
        *loaded = _loaded;
    }
    if (image_type) {
        *image_type = _image_type;
    } else {
        g_free(_image_type);
    }
    if (file_names) {
        *file_names = _file_names;
    } else {
        g_strfreev(_file_names);
    }
    
    /* Unlock */
    g_static_mutex_unlock(&_priv->device_mutex);
    
    return TRUE;
}

gboolean cdemud_device_load_disc (CDEMUD_Device *self, gchar **file_names, GError **error) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
        
    /* Lock */
    g_static_mutex_lock(&_priv->device_mutex);
    
    /* Well, we won't do anything if we're already loaded */
    if (!_priv->loaded) {
        /* If loading succeeded... */
        if (mirage_mirage_create_disc(MIRAGE_MIRAGE(_priv->mirage), file_names, &_priv->disc, _priv->disc_debug, error)) {
            gint media_type = 0;

            _priv->loaded = TRUE;
            _priv->media_changed = TRUE;
            
            /* Set current profile (and modify feature flags accordingly */
            mirage_disc_get_medium_type(MIRAGE_DISC(_priv->disc), &media_type, NULL);
            switch (media_type) {
                case MIRAGE_MEDIUM_CD: {
                    __cdemud_device_set_profile(self, PROFILE_CDROM);
                    break;
                }
                case MIRAGE_MEDIUM_DVD: {
                    __cdemud_device_set_profile(self, PROFILE_DVDROM);                    
                    break;
                }
                default: {
                    CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: unknown media type: 0x%X!\n", __func__, media_type);
                    break;
                }
            }
            
            /* Send notification */
            g_signal_emit_by_name(self, "device-change", CDEMUD_DEVICE_CHANGE_STATUS, NULL);
            
            succeeded = TRUE;
            goto end;
        } else {
            /* Error already set */
            succeeded = FALSE;
            goto end;
        }        
    } else {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: device already loaded\n", __func__);
        cdemud_error(CDEMUD_E_ALREADYLOADED, error);
        succeeded = FALSE;
    }

end:
    /* Unlock */
    g_static_mutex_unlock(&_priv->device_mutex);
    
    return succeeded;
}

gboolean cdemud_device_unload_disc (CDEMUD_Device *self, GError **error) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
        
    /* Lock */
    g_static_mutex_lock(&_priv->device_mutex);
    
    /* Check if the door is locked */
    if (_priv->locked) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_PC_TRACE, "%s: device is locked\n", __func__);
        cdemud_error(CDEMUD_E_DEVLOCKED, error);
        succeeded = FALSE;
        goto end;
    }
    
    /* Unload only if we're loaded */
    if (_priv->loaded) {
        /* Delete disc */
        g_object_unref(_priv->disc);
        /* We're not loaded anymore, and media got changed */
        _priv->loaded = FALSE;
        _priv->media_changed = TRUE;
        /* Current profile: None */
        __cdemud_device_set_profile(self, PROFILE_NONE);       
        
        /* Send notification */
        g_signal_emit_by_name(self, "device-change", CDEMUD_DEVICE_CHANGE_STATUS, NULL);
    }
    
end:
    /* Unlock */
    g_static_mutex_unlock(&_priv->device_mutex);
    
    return succeeded;
}

static gboolean __cdemud_device_get_debug_mask_daemon (CDEMUD_Device *self, gint *dbg_mask, GError **error) {
    GObject *context = NULL;
    gboolean succeeded = TRUE;
    
    if (mirage_object_get_debug_context(MIRAGE_OBJECT(self), &context, NULL)) {
        /* Relay to device's debug context */
        succeeded = mirage_debug_context_get_debug_mask(MIRAGE_DEBUG_CONTEXT(context), dbg_mask, error);
        g_object_unref(context);
    }
    
    return succeeded;
}

static gboolean __cdemud_device_get_debug_mask_library (CDEMUD_Device *self, gint *dbg_mask, GError **error) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    /* Relay to disc's debug context */
    return mirage_debug_context_get_debug_mask(MIRAGE_DEBUG_CONTEXT(_priv->disc_debug), dbg_mask, error);
}

gboolean cdemud_device_get_debug_mask (CDEMUD_Device *self, gchar *type, gint *dbg_mask, GError **error) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    gboolean succeeded = FALSE;
        
    CDEMUD_CHECK_ARG(type);
    CDEMUD_CHECK_ARG(dbg_mask);
    
    /* Lock */
    g_static_mutex_lock(&_priv->device_mutex);
    
    if (!g_ascii_strcasecmp(type, "daemon")) {
        /* Return daemon's debug mask */
        succeeded = __cdemud_device_get_debug_mask_daemon(self, dbg_mask, error);
    } else if (!g_ascii_strcasecmp(type, "library")) {
        /* Return library's debug mask */
        succeeded =  __cdemud_device_get_debug_mask_library(self, dbg_mask, error);
    } else {
        /* Invalid type */
        cdemud_error(CDEMUD_E_INVALIDARG, error);
        succeeded = FALSE;
    }
    
    /* Unlock */
    g_static_mutex_unlock(&_priv->device_mutex);
    
    return succeeded;
}

static gboolean __cdemud_device_set_debug_mask_daemon (CDEMUD_Device *self, gint dbg_mask, GError **error) {   
    GObject *context = NULL;
    gboolean succeeded = TRUE;
    
    if (mirage_object_get_debug_context(MIRAGE_OBJECT(self), &context, NULL)) {
        /* Relay to device's debug context */
        succeeded = mirage_debug_context_set_debug_mask(MIRAGE_DEBUG_CONTEXT(context), dbg_mask, error);
        g_object_unref(context);
        g_signal_emit_by_name(self, "device-change", CDEMUD_DEVICE_CHANGE_DAEMONDEBUGMASK, NULL);        
    }
    
    return succeeded;
}

static gboolean __cdemud_device_set_debug_mask_library (CDEMUD_Device *self, gint dbg_mask, GError **error) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    gboolean succeeded = FALSE;
        
    /* Relay to disc's debug context */
    succeeded = mirage_debug_context_set_debug_mask(MIRAGE_DEBUG_CONTEXT(_priv->disc_debug), dbg_mask, error);
    if (succeeded) {
        g_signal_emit_by_name(self, "device-change", CDEMUD_DEVICE_CHANGE_LIBRARYDEBUGMASK, NULL);
    }
        
    return succeeded;
}

gboolean cdemud_device_set_debug_mask (CDEMUD_Device *self, gchar *type, gint dbg_mask, GError **error) {
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    gboolean succeeded = FALSE;
        
    CDEMUD_CHECK_ARG(type);
    
    /* Lock */
    g_static_mutex_lock(&_priv->device_mutex);
    
    if (!g_ascii_strcasecmp(type, "daemon")) {
        /* Set daemon's debug mask */
        succeeded = __cdemud_device_set_debug_mask_daemon(self, dbg_mask, error);
    } else if (!g_ascii_strcasecmp(type, "library")) {
        /* Set library's debug mask */
        succeeded = __cdemud_device_set_debug_mask_library(self, dbg_mask, error);
    } else {
        /* Invalid type */
        cdemud_error(CDEMUD_E_INVALIDARG, error);
        succeeded = FALSE;
    }
    
    /* Unlock */
    g_static_mutex_unlock(&_priv->device_mutex);
    
    return succeeded;
}


/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static MIRAGE_ObjectClass *parent_class = NULL;

static void __cdemud_device_finalize (GObject *obj) {
    CDEMUD_Device *self = CDEMUD_DEVICE(obj);
    CDEMUD_DevicePrivate *_priv = CDEMUD_DEVICE_GET_PRIVATE(self);
    GList *entry = NULL;
    
    /* Unload disc */
    cdemud_device_unload_disc(self, NULL);
    
    /* Unref Mirage object */
    if (_priv->mirage) {
        g_object_unref(_priv->mirage);
    }
    
    /* Unref audio play object */
    if (_priv->audio_play) {
        g_object_unref(_priv->audio_play);
    }
    
    /* Unref debug context */
    if (_priv->disc_debug) {
        g_object_unref(_priv->disc_debug);
    }
    
    /* Free mode pages list; free each pointer in the array, then free the array... */
    G_LIST_FOR_EACH(entry, _priv->mode_pages_list) {
        if (entry->data) {
            GValueArray *array = entry->data;
            
            g_free(g_value_get_pointer(g_value_array_get_nth(array, 0)));
            g_free(g_value_get_pointer(g_value_array_get_nth(array, 1)));
            g_free(g_value_get_pointer(g_value_array_get_nth(array, 2)));
            
            g_value_array_free(array);
        }
    }
    g_list_free(_priv->mode_pages_list);
    
    /* Free features list */
    G_LIST_FOR_EACH(entry, _priv->features_list) {
        if (entry->data) {
            g_free(entry->data);
        }
    }
    g_list_free(_priv->features_list);
    
    /* Free buffer/"cache" */
    g_free(_priv->buffer);
            
    /* Free device name */
    g_free(_priv->device_name);
        
    /* Chain up to the parent class */
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}


static void __cdemud_device_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    CDEMUD_DeviceClass *klass = CDEMUD_DEVICE_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(CDEMUD_DevicePrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __cdemud_device_finalize;
    
    /* Signals */
    klass->signals[0] = g_signal_new("device-change", G_OBJECT_CLASS_TYPE(klass), (G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED), 0, NULL, NULL, g_cclosure_user_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT, NULL);
    
    return;
}

GType cdemud_device_get_type (void) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(CDEMUD_DeviceClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __cdemud_device_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(CDEMUD_Device),
            0,      /* n_preallocs */
            NULL    /* instance_init */
        };
        
        type = g_type_register_static(MIRAGE_TYPE_OBJECT, "CDEMUD_Device", &info, 0);
    }
    
    return type;
}
