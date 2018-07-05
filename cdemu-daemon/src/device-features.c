 /*
 *  CDEmu daemon: device - features and profiles
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
 *                                Helpers                             *
\**********************************************************************/
static gint compare_features (struct FeatureGeneral *feature1, struct FeatureGeneral *feature2)
{
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

static gint find_feature (struct FeatureGeneral *feature, gconstpointer code_ptr)
{
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


/**********************************************************************\
 *                      Feature declaration helpers                   *
\**********************************************************************/
static struct FeatureGeneral *initialize_feature (gint code, gint size)
{
    struct FeatureGeneral *feature = g_malloc0(size);

    feature->code = GUINT16_TO_BE(code);
    feature->length = size - 4;

    return feature;
}

static GList *append_feature (GList *list, struct FeatureGeneral *feature)
{
    return g_list_insert_sorted(list, feature, (GCompareFunc)compare_features);
}


/**********************************************************************\
 *                           Features API                             *
\**********************************************************************/
gpointer cdemu_device_get_feature (CdemuDevice *self, gint feature)
{
    GList *entry = NULL;
    entry = g_list_find_custom(self->priv->features_list, GINT_TO_POINTER(feature), (GCompareFunc)find_feature);

    if (entry) {
        return entry->data;
    }

    return NULL;
}

void cdemu_device_features_init (CdemuDevice *self)
{
    struct FeatureGeneral *general_feature;

    /* Feature 0x0000: Profile List */
    /* IMPLEMENTATION NOTE: persistent; we support several profiles.
       Version is left at 0x00, as per INF8090 */
    general_feature = initialize_feature(0x0000, sizeof(struct Feature_0x0000));
    if (general_feature) {
        struct Feature_0x0000 *feature = (struct Feature_0x0000 *)general_feature;

        feature->per = 1;

        feature->profiles[ProfileIndex_CDROM].profile = GUINT16_TO_BE(PROFILE_CDROM);
        feature->profiles[ProfileIndex_CDR].profile = GUINT16_TO_BE(PROFILE_CDR);
        feature->profiles[ProfileIndex_DVDROM].profile = GUINT16_TO_BE(PROFILE_DVDROM);
        feature->profiles[ProfileIndex_DVDPLUSR].profile = GUINT16_TO_BE(PROFILE_DVDPLUSR);
        feature->profiles[ProfileIndex_BDROM].profile = GUINT16_TO_BE(PROFILE_BDROM);
        feature->profiles[ProfileIndex_BDR_SRM].profile = GUINT16_TO_BE(PROFILE_BDR_SRM);
    }
    self->priv->features_list = append_feature(self->priv->features_list, general_feature);

    /* Feature 0x0001: Core Feature */
    /* IMPLEMENTATION NOTE: persistent; INF8090 requires us to set version to
       0x02. We emulate ATAPI device, thus interface is set to ATAPI. We don't
       support everything that's required for INQ2 bit (namely, vital product
       information) and we don't support device busy event class */
    general_feature = initialize_feature(0x0001, sizeof(struct Feature_0x0001));
    if (general_feature) {
        struct Feature_0x0001 *feature = (struct Feature_0x0001 *)general_feature;

        feature->per = 1;
        feature->ver = 0x02;

        feature->interface = GUINT32_TO_BE(0x02); /* ATAPI */
    }
    self->priv->features_list = append_feature(self->priv->features_list, general_feature);


    /* Feature 0x0002: Morphing Feature */
    /* IMPLEMENTATION NOTE: persistent; version set to 0x01 as per INF8090. Both
       Async and OCEvent bits are left at 0. */
    general_feature = initialize_feature(0x0002, sizeof(struct Feature_0x0002));
    if (general_feature) {
        struct Feature_0x0002 *feature = (struct Feature_0x0002 *)general_feature;

        feature->per = 1;
        feature->ver = 0x01;
    }
    self->priv->features_list = append_feature(self->priv->features_list, general_feature);


    /* Feature 0x0003: Removable Medium Feature */
    /* IMPLEMENTATION NOTE: persistent; version left at 0x00 as there's none
       specified in INF8090. Mechanism is set to 'Tray' (0x001), and we support
       both eject and lock. Prevent jumper is not present. */
    general_feature = initialize_feature(0x0003, sizeof(struct Feature_0x0003));
    if (general_feature) {
        struct Feature_0x0003 *feature = (struct Feature_0x0003 *)general_feature;

        feature->per = 1;

        feature->mechanism = 0x001;
        feature->eject = 1;
        feature->lock = 1;
    }
    self->priv->features_list = append_feature(self->priv->features_list, general_feature);


    /* Feature 0x0010: Random Readable Feature */
    /* IMPLEMENTATION NOTE: non-persistent; version left at 0x00. Block size is
       2048 bytes and we set blocking to 1 (as recommended for most CD-ROMs...
       it's non-essential, really). Read-write error recovery page is present */
    general_feature = initialize_feature(0x0010, sizeof(struct Feature_0x0010));
    if (general_feature) {
        struct Feature_0x0010 *feature = (struct Feature_0x0010 *)general_feature;

        feature->block_size = GUINT32_TO_BE(2048);
        feature->blocking = GUINT16_TO_BE(1);
        feature->pp = 1;
    }
    self->priv->features_list = append_feature(self->priv->features_list, general_feature);


    /* Feature 0x001D: Multi-read Feature */
    /* IMPLEMENTATION NOTE: non-persistent; version left at 0x00. No other content. */
    general_feature = initialize_feature(0x001D, sizeof(struct Feature_0x001D));
    self->priv->features_list = append_feature(self->priv->features_list, general_feature);


    /* Feature 0x001E: CD Read Feature */
    /* IMPLEMENTATION NOTE: non-persistent; version set to 0x02 as per INF8090.
       Both C2Flags and CDText bits are set, while DAP is not supported. */
    general_feature = initialize_feature(0x001E, sizeof(struct Feature_0x001E));
    if (general_feature) {
        struct Feature_0x001E *feature = (struct Feature_0x001E *)general_feature;

        feature->ver = 0x02;

        feature->c2flags = 1;
        feature->cdtext = 1;
    }
    self->priv->features_list = append_feature(self->priv->features_list, general_feature);


    /* Feature 0x001F: DVD Read Feature */
    /* IMPLEMENTATION NOTE: non-persistent; version set to 0x01 as per INF8090.
       We claim we conform to DVD Multi Specification Version 1.1 and that we
       support dual-layer DVD-R */
    general_feature = initialize_feature(0x001F, sizeof(struct Feature_0x001F));
    if (general_feature) {
        struct Feature_0x001F *feature = (struct Feature_0x001F *)general_feature;

        feature->ver = 0x01;

        feature->multi110 = 1;
        feature->dualr = 1;
    }
    self->priv->features_list = append_feature(self->priv->features_list, general_feature);


    /* Feature 0x0021: Incremental Streaming Writable Feature */
    /* IMPLEMENTATION NOTE: non-persistent; version set to 0x01 as per MMC3 */
    general_feature = initialize_feature(0x0021, sizeof(struct Feature_0x0021));
    if (general_feature) {
        struct Feature_0x0021 *feature = (struct Feature_0x0021 *)general_feature;

        feature->ver = 0x01;

        feature->data_block_types_supported = 0xFF; /* Support all */

        feature->buf = 1; /* We support dual-cross linking */

        feature->num_link_sizes = 1; /* 1 for CD-R */
        feature->link_sizes[0] = 7; /* As per MMC3 */
    }
    self->priv->features_list = append_feature(self->priv->features_list, general_feature);

    /* Feature 0x002B: DVD+R Feature */
    /* IMPLEMENTATION NOTE: non-persistent; version set to 0x00 as per MMC5 */
    general_feature = initialize_feature(0x002B, sizeof(struct Feature_0x002B));
    if (general_feature) {
        struct Feature_0x002B *feature = (struct Feature_0x002B *)general_feature;

        feature->ver = 0x00;

        feature->write = 1; /* We support DVD+R writing */
    }
    self->priv->features_list = append_feature(self->priv->features_list, general_feature);


    /* Feature 0x002D: CD Track at Once Feature */
    /* IMPLEMENTATION NOTE: non-persistent; version set to 0x02 as per MMC3 */
    general_feature = initialize_feature(0x002D, sizeof(struct Feature_0x002D));
    if (general_feature) {
        struct Feature_0x002D *feature = (struct Feature_0x002D *)general_feature;

        feature->ver = 0x02;

        feature->buf = 1; /* We support dual-cross linking */
        feature->rw_raw = 1;
        feature->rw_pack = 1;
        feature->test_write = 1;
        feature->cd_rw = 1;
        feature->rw_subcode = 1;

        feature->data_type_supported = 0xFFFF; /* Support all */
    }
    self->priv->features_list = append_feature(self->priv->features_list, general_feature);

    /* Feature 0x0040: BD Read Feature */
    /* IMPLEMENTATION NOTE: non-persistent; version set to 0x00 as per MMC5 */
    general_feature = initialize_feature(0x0040, sizeof(struct Feature_0x0040));
    if (general_feature) {
        struct Feature_0x0040 *feature = (struct Feature_0x0040 *)general_feature;

        feature->ver = 0x00;

        /* Claim that we can read everything */
        feature->class0_bdre_read_support = 0xFFFF;
        feature->class1_bdre_read_support = 0xFFFF;
        feature->class2_bdre_read_support = 0xFFFF;
        feature->class3_bdre_read_support = 0xFFFF;

        feature->class0_bdr_read_support = 0xFFFF;
        feature->class1_bdr_read_support = 0xFFFF;
        feature->class2_bdr_read_support = 0xFFFF;
        feature->class3_bdr_read_support = 0xFFFF;

        feature->class0_bdrom_read_support = 0xFFFF;
        feature->class1_bdrom_read_support = 0xFFFF;
        feature->class2_bdrom_read_support = 0xFFFF;
        feature->class3_bdrom_read_support = 0xFFFF;
    }
    self->priv->features_list = append_feature(self->priv->features_list, general_feature);

    /* Feature 0x0041: BD Write Feature */
    /* IMPLEMENTATION NOTE: non-persistent; version set to 0x00 as per MMC5 */
    general_feature = initialize_feature(0x0041, sizeof(struct Feature_0x0041));
    if (general_feature) {
        struct Feature_0x0041 *feature = (struct Feature_0x0041 *)general_feature;

        feature->ver = 0x00;

        /* Claim that we can write everything */
        feature->class0_bdre_write_support = 0xFFFF;
        feature->class1_bdre_write_support = 0xFFFF;
        feature->class2_bdre_write_support = 0xFFFF;
        feature->class3_bdre_write_support = 0xFFFF;

        feature->class0_bdr_write_support = 0xFFFF;
        feature->class1_bdr_write_support = 0xFFFF;
        feature->class2_bdr_write_support = 0xFFFF;
        feature->class3_bdr_write_support = 0xFFFF;
    }
    self->priv->features_list = append_feature(self->priv->features_list, general_feature);

    /* Feature 0x0100: Power Management Feature */
    /* IMPLEMENTATION NOTE: persistent; version left at 0x00. No other content. */
    general_feature = initialize_feature(0x0100, sizeof(struct Feature_0x0100));
    if (general_feature) {
        struct Feature_0x0100 *feature = (struct Feature_0x0100 *)general_feature;

        feature->per = 1;
    }
    self->priv->features_list = append_feature(self->priv->features_list, general_feature);


    /* Feature 0x0103: CD External Audio Play Feature */
    /* IMPLEMENTATION NOTE: non-persistent; version left at 0x00. Separate volume
       and separate channel mute are supported, and so is scan. Volume levels is
       set to 0x100. */
    general_feature = initialize_feature(0x0103, sizeof(struct Feature_0x0103));
    if (general_feature) {
        struct Feature_0x0103 *feature = (struct Feature_0x0103 *)general_feature;

        feature->scm = 1;
        feature->sv = 1;
        feature->scan = 1;
        feature->vol_lvls = GUINT16_TO_BE(0x0100);
    }
    self->priv->features_list = append_feature(self->priv->features_list, general_feature);


    /* Feature 0x0106: DVD CSS Feature */
    /* IMPLEMENTATION NOTE: non-persistent; version left at 0x00. CSS version is
       set to 0x01 as per INF8090. */
    general_feature = initialize_feature(0x0106, sizeof(struct Feature_0x0106));
    if (general_feature) {
        struct Feature_0x0106 *feature = (struct Feature_0x0106 *)general_feature;

        feature->css_ver = 0x01;
    }
    self->priv->features_list = append_feature(self->priv->features_list, general_feature);


    /* Feature 0x0107: Real Time Streaming Feature */
    /* IMPLEMENTATION NOTE: non-persistent; version is set to 0x03 as per INF8090.
       We claim to support pretty much everything here. */
    general_feature = initialize_feature(0x0107, sizeof(struct Feature_0x0107));
    if (general_feature) {
        struct Feature_0x0107 *feature = (struct Feature_0x0107 *)general_feature;

        feature->ver = 0x03;

        feature->rbcb = 1;
        feature->scs = 1;
        feature->mp2a = 1;
        feature->wspd = 1;
        feature->sw = 1;
    }
    self->priv->features_list = append_feature(self->priv->features_list, general_feature);

    /* Feature 0x010A: Disc Control Blocks Feature */
    /* IMPLEMENTATION NOTE: non-persistent; version is set to 0x00 as per MMC3.
       We return same three DCB descriptors as my drive does. */
    general_feature = initialize_feature(0x010A, sizeof(struct Feature_0x010A) + 3*4);
    if (general_feature) {
        struct Feature_0x010A *feature = (struct Feature_0x010A *)general_feature;

        feature->ver = 0x00;

        /* Set descriptors */
        guint8 *descriptor = (guint8 *)(feature + 1);

        descriptor[0] = 0x46; /* F */
        descriptor[1] = 0x44; /* D */
        descriptor[2] = 0x43; /* C */
        descriptor[3] = 0x00; /* ver. 0 */

        descriptor += 4;

        descriptor[0] = 0x53; /* S */
        descriptor[1] = 0x44; /* D */
        descriptor[2] = 0x43; /* C */
        descriptor[3] = 0x00; /* ver. 0 */

        descriptor += 4;

        descriptor[0] = 0x54; /* T */
        descriptor[1] = 0x4F; /* O */
        descriptor[2] = 0x43; /* C */
        descriptor[3] = 0x00; /* ver. 0 */
    }
    self->priv->features_list = append_feature(self->priv->features_list, general_feature);
}


void cdemu_device_features_cleanup (CdemuDevice *self)
{
    for (GList *entry = self->priv->features_list; entry; entry = entry->next) {
        if (entry->data) {
            g_free(entry->data);
        }
    }
    g_list_free(self->priv->features_list);

}


/**********************************************************************\
 *                       Write Speed Descriptors                      *
\**********************************************************************/
struct PerformanceDescriptor
{
    gboolean mrw;
    gboolean exact;
    gint wrc;
    gint rc; /* Rotation control value, for Mode Page 0x2A (seems to differ from WRC from GET PERFORMANCE) */
    gint read_speed;
    gint write_speed;
};

/* The values below are taken from my drive; NOTE: the amount of items
   in this list must match the size of descriptor list in Mode Page 0x2A! */
static const struct PerformanceDescriptor WriteDescriptors_CD[] = {
    { FALSE, FALSE, 0, 0, 0x00001B90, 0x00001B90 },
    { FALSE, FALSE, 0, 0, 0x0000160D, 0x0000160D },
    { FALSE, FALSE, 0, 0, 0x0000108A, 0x0000108A },
    { FALSE, FALSE, 0, 0, 0x00000B07, 0x00000B07 },
    { FALSE, FALSE, 0, 0, 0x000006E4, 0x000006E4 },
    { FALSE, FALSE, 0, 0, 0x000002C2, 0x000002C2 },
};

static const struct PerformanceDescriptor WriteDescriptors_DVD[] = {
    { FALSE, FALSE, 0, 0, 0x00005690, 0x00005690 },
    { FALSE, FALSE, 0, 0, 0x000040EC, 0x000040EC },
    { FALSE, FALSE, 0, 0, 0x0000361A, 0x0000361A },
    { FALSE, FALSE, 0, 0, 0x00002B48, 0x00002B48 },
    { FALSE, FALSE, 0, 0, 0x00002076, 0x00002076 },
    { FALSE, FALSE, 0, 0, 0x00001B0D, 0x00001B0D },
    { FALSE, FALSE, 0, 0, 0x000015A4, 0x000015A4 },
    { FALSE, FALSE, 0, 0, 0x000011DA, 0x000011DA },
    { FALSE, FALSE, 0, 0, 0x0000103B, 0x0000103B },
    { FALSE, FALSE, 0, 0, 0x00000CFC, 0x00000CFC },
    { FALSE, FALSE, 0, 0, 0x00000AD2, 0x00000AD2 },
    { FALSE, FALSE, 0, 0, 0x00000569, 0x00000569 },
};

static const struct PerformanceDescriptor WriteDescriptors_BD[] = {
    { FALSE, FALSE, 0, 0, 0x0000231E, 0x0000231E },
};

static void cdemu_device_set_write_speed_descriptors (CdemuDevice *self, ProfileIndex profile_index)
{
    /* Clear old list and its elements */
    if (self->priv->write_descriptors) {
        g_list_free_full(self->priv->write_descriptors, g_free);
        self->priv->write_descriptors = NULL;
    }

    /* Select performance list descriptor */
    const struct PerformanceDescriptor *descriptors;
    gint num_descriptors;

    gint end_sector;
    if (self->priv->loaded) {
        if (self->priv->recordable_disc) {
            end_sector = self->priv->medium_capacity;
        } else {
            end_sector = mirage_disc_layout_get_length(self->priv->disc);
        }
    } else {
        end_sector = 0x0023127F;
    }

    switch (profile_index) {
        case ProfileIndex_CDROM:
        case ProfileIndex_CDR: {
            descriptors = WriteDescriptors_CD;
            num_descriptors = G_N_ELEMENTS(WriteDescriptors_CD);
            break;
        }
        case ProfileIndex_DVDROM:
        case ProfileIndex_DVDPLUSR: {
            descriptors = WriteDescriptors_DVD;
            num_descriptors = G_N_ELEMENTS(WriteDescriptors_DVD);
            break;
        }
        case ProfileIndex_BDROM:
        case ProfileIndex_BDR_SRM: {
            descriptors = WriteDescriptors_BD;
            num_descriptors = G_N_ELEMENTS(WriteDescriptors_BD);
            break;
        }
        case ProfileIndex_NONE:
        default: {
            descriptors = WriteDescriptors_CD;
            num_descriptors = G_N_ELEMENTS(WriteDescriptors_CD);
            break;
        }
    }

    /* Build/populate the descriptor list(s) */
    struct ModePage_0x2A_WriteSpeedPerformanceDescriptor *p_0x2A_descs = (struct ModePage_0x2A_WriteSpeedPerformanceDescriptor *)(((struct ModePage_0x2A *)cdemu_device_get_mode_page(self, 0x2A, MODE_PAGE_CURRENT)) + 1);

    for (gint i = 0; i < num_descriptors; i++) {
        const struct PerformanceDescriptor *source_descriptor = &descriptors[i];

        /* List of descriptors for GET PERFORMANCE */
        struct GET_PERFORMANCE_03_Descriptor *perf_descriptor = g_new0(struct GET_PERFORMANCE_03_Descriptor, 1);

        perf_descriptor->wrc = source_descriptor->wrc;
        perf_descriptor->mrw = source_descriptor->mrw;
        perf_descriptor->exact = source_descriptor->exact;
        perf_descriptor->end_lba = GUINT32_TO_BE(end_sector);
        perf_descriptor->read_speed = GUINT32_TO_BE(source_descriptor->read_speed);
        perf_descriptor->write_speed = GUINT32_TO_BE(source_descriptor->write_speed);

        self->priv->write_descriptors = g_list_append(self->priv->write_descriptors, perf_descriptor);

        /* List of descriptors for Mode Page 0x2A */
        p_0x2A_descs[i].rc = source_descriptor->rc;
        p_0x2A_descs[i].write_speed = GUINT16_TO_BE(source_descriptor->write_speed);
    }
}


/**********************************************************************\
 *                              Profiles                              *
\**********************************************************************/
/* Here we define profiles and features associated with them. When we set a
   certain profile, features get their 'current' bit reset (unless 'persistent'
   bit is set), and then if they are associated with the profile, they are set
   again */
static guint32 ActiveFeatures_CDROM[] =
{
    /* 0x0000: Profile List; persistent */
    /* 0x0001: Core; persistent */
    /* 0x0002: Morphing; persistent */
    /* 0x0003: Removable Medium; persistent */
    0x0010, /* Random Readable */
    0x001D, /* Multi-read */
    0x001E, /* CD Read */
    0x0103, /* CD External Audio Play */
    0x0107, /* Real Time Streaming */
};

static guint32 ActiveFeatures_CDR[] =
{
    /* 0x0000: Profile List; persistent */
    /* 0x0001: Core; persistent */
    /* 0x0002: Morphing; persistent */
    /* 0x0003: Removable Medium; persistent */
    0x0010, /* Random Readable */
    0x001D, /* Multi-read */
    0x001E, /* CD Read */
    0x0021, /* Incremental Streaming Writable */
    0x002D, /* CD Track at Once */
    0x0103, /* CD External Audio Play */
    0x0107, /* Real Time Streaming */
};

static guint32 ActiveFeatures_DVDROM[] =
{
    /* 0x0000: Profile List; persistent */
    /* 0x0001: Core; persistent */
    /* 0x0002: Morphing; persistent */
    /* 0x0003: Removable Medium; persistent */
    0x0010, /* Random Readable */
    0x001F, /* DVD Read */
    0x0106, /* DVD CSS */
    0x0107, /* Real Time Streaming */
};

static guint32 ActiveFeatures_DVDPLUSR[] =
{
    /* 0x0000: Profile List; persistent */
    /* 0x0001: Core; persistent */
    /* 0x0002: Morphing; persistent */
    /* 0x0003: Removable Medium; persistent */
    0x0010, /* Random Readable */
    0x001F, /* DVD Read */
    0x002B, /* DVD+R */
    0x0106, /* DVD CSS */
    0x0107, /* Real Time Streaming */
    0x010A, /* DCBs */
};

static guint32 ActiveFeatures_BDROM[] =
{
    /* 0x0000: Profile List; persistent */
    /* 0x0001: Core; persistent */
    /* 0x0002: Morphing; persistent */
    /* 0x0003: Removable Medium; persistent */
    0x0010, /* Random Readable */
    0x0040, /* BD Read */
    0x0107, /* Real Time Streaming */
};

static guint32 ActiveFeatures_BDR_SRM[] =
{
    /* 0x0000: Profile List; persistent */
    /* 0x0001: Core; persistent */
    /* 0x0002: Morphing; persistent */
    /* 0x0003: Removable Medium; persistent */
    0x0010, /* Random Readable */
    0x0021, /* Incremental Streaming Writable */
    0x0040, /* BD Read */
    0x0041, /* BD Write */
    0x0107, /* Real Time Streaming */
};


static void cdemu_device_set_current_features (CdemuDevice *self, guint32 *feats, gint feats_len)
{
    /* Go over the features list and reset 'current' bits of features that
       don't have 'persistent' bit set */
    for (GList *entry = self->priv->features_list; entry; entry = entry->next) {
        struct FeatureGeneral *feature = entry->data;

        if (!feature->per) {
            feature->cur = 0;
        } else {
            feature->cur = 1;
        }
    };

    /* Now go over list of input features and set their 'current' bits */
    for (gint i = 0; i < feats_len; i++) {
        struct FeatureGeneral *feature = cdemu_device_get_feature(self, feats[i]);
        if (feature) {
            feature->cur = 1;
        } else {
            CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: feature 0x%X not found; shouldn't happen!\n", __debug__, feats[i]);
        }
    }
}

void cdemu_device_set_profile (CdemuDevice *self, ProfileIndex profile_index)
{
    /* Clear 'current' bit on all profiles */
    struct Feature_0x0000 *f_0x0000 = cdemu_device_get_feature(self, 0x0000);
    for (guint i = 0; i < G_N_ELEMENTS(f_0x0000->profiles); i++) {
        f_0x0000->profiles[i].cur = 0;
    }

    /* Set active features and profile */
    switch (profile_index) {
        case ProfileIndex_NONE: {
            /* Current profile */
            self->priv->current_profile = PROFILE_NONE;
            /* Current features */
            cdemu_device_set_current_features(self, NULL, 0);
            break;
        }
        case ProfileIndex_CDROM: {
            /* Current profile */
            self->priv->current_profile = PROFILE_CDROM;
            /* Current features */
            cdemu_device_set_current_features(self, ActiveFeatures_CDROM, G_N_ELEMENTS(ActiveFeatures_CDROM));
            /* Set 'current bit' on profiles */
            f_0x0000->profiles[ProfileIndex_CDROM].cur = 1;
            break;
        }
        case ProfileIndex_CDR: {
            /* Current profile */
            self->priv->current_profile = PROFILE_CDR;
            /* Current features */
            cdemu_device_set_current_features(self, ActiveFeatures_CDR, G_N_ELEMENTS(ActiveFeatures_CDR));
            /* Set 'current bit' on profiles */
            f_0x0000->profiles[ProfileIndex_CDR].cur = 1;
            f_0x0000->profiles[ProfileIndex_CDROM].cur = 1;
            break;
        }
        case ProfileIndex_DVDROM: {
            /* Current profile */
            self->priv->current_profile = PROFILE_DVDROM;
            /* Current features */
            cdemu_device_set_current_features(self, ActiveFeatures_DVDROM, G_N_ELEMENTS(ActiveFeatures_DVDROM));
            /* Set 'current bit' on profiles */
            f_0x0000->profiles[ProfileIndex_DVDROM].cur = 1;
            break;
        }
        case ProfileIndex_DVDPLUSR: {
            /* Current profile */
            self->priv->current_profile = PROFILE_DVDPLUSR;
            /* Current features */
            cdemu_device_set_current_features(self, ActiveFeatures_DVDPLUSR, G_N_ELEMENTS(ActiveFeatures_DVDPLUSR));
            /* Set 'current bit' on profiles */
            f_0x0000->profiles[ProfileIndex_DVDPLUSR].cur = 1;
            break;
        }
        case ProfileIndex_BDROM: {
            /* Current profile */
            self->priv->current_profile = PROFILE_BDROM;
            /* Current features */
            cdemu_device_set_current_features(self, ActiveFeatures_BDROM, G_N_ELEMENTS(ActiveFeatures_BDROM));
            /* Set 'current bit' on profiles */
            f_0x0000->profiles[ProfileIndex_BDROM].cur = 1;
            break;
        }
        case ProfileIndex_BDR_SRM: {
            /* Current profile */
            self->priv->current_profile = PROFILE_BDR_SRM;
            /* Current features */
            cdemu_device_set_current_features(self, ActiveFeatures_BDR_SRM, G_N_ELEMENTS(ActiveFeatures_BDR_SRM));
            /* Set 'current bit' on profiles */
            f_0x0000->profiles[ProfileIndex_BDROM].cur = 1;
            f_0x0000->profiles[ProfileIndex_BDR_SRM].cur = 1;
            break;
        }
        default: {
            CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: unhandled profile index %d; shouldn't happen!\n", __debug__, profile_index);
            return;
        }
    }

    /* Modify write speed descriptors */
    cdemu_device_set_write_speed_descriptors(self, profile_index);
}
