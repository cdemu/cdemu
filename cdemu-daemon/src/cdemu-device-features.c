 /*
 *  CDEmu daemon: Device object - Features and profiles
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

#include "cdemu.h"
#include "cdemu-device-private.h"

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
 *                      Feature declaration macros                    *
\**********************************************************************/
#define FEATURE_DEFINITION_START(CODE) \
    if (1) { \
        struct Feature_##CODE *feature = g_new0(struct Feature_##CODE, 1); \
        /* Initialize feature */ \
        feature->code = GUINT16_TO_BE(CODE); \
        feature->length = sizeof(struct Feature_##CODE) - 4;

#define FEATURE_DEFINITION_END() \
        /* Insert into list */ \
        self->priv->features_list = g_list_insert_sorted(self->priv->features_list, feature, (GCompareFunc)compare_features); \
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
    /* Feature 0x0000: Profile List */
    /* IMPLEMENTATION NOTE: persistent; we support two profiles; CD-ROM and
       DVD-ROM. Version is left at 0x00, as per INF8090 */
    FEATURE_DEFINITION_START(0x0000)
    feature->per = 1;

    feature->profiles[0].profile = GUINT16_TO_BE((Profile) PROFILE_CDROM);
    feature->profiles[1].profile = GUINT16_TO_BE((Profile) PROFILE_DVDROM);

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


void cdemu_device_features_cleanup (CdemuDevice *self)
{
    GList *entry;
    G_LIST_FOR_EACH(entry, self->priv->features_list) {
        if (entry->data) {
            g_free(entry->data);
        }
    }
    g_list_free(self->priv->features_list);

}

/**********************************************************************\
 *                              Profiles                              *
\**********************************************************************/
/* Here we define profiles and features associated with them. When we set a
   certain profile, features get their 'current' bit reset (unless 'persistent'
   bit is set), and then if they are associated with the profile, they are set
   again */
static guint32 Features_PROFILE_PROFILE_CDROM[] =
{
    0x0010, /* Random Readable Feature */
    0x001D, /* Multi-read Feature */
    0x001E, /* CD Read Feature */
    0x0103, /* CD External Audio Play Feature */
    0x0107, /* Real Time Streaming Feature */
};

static guint32 Features_PROFILE_PROFILE_DVDROM[] =
{
    0x0010, /* Random Readable Feature */
    0x001F, /* DVD Read Feature */
    0x0106, /* DVD CSS Feature */
    0x0107, /* Real Time Streaming Feature */
};


static void cdemu_device_set_current_features (CdemuDevice *self, guint32 *feats, gint feats_len)
{
    /* Go over the features list and reset 'current' bits of features that
       don't have 'persistent' bit set */
    GList *entry;
    G_LIST_FOR_EACH(entry, self->priv->features_list) {
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

void cdemu_device_set_profile (CdemuDevice *self, Profile profile)
{
    /* Set current profile */
    self->priv->current_profile = profile;

    /* Features */
    switch (profile) {
        case PROFILE_NONE: {
            /* Current features */
            cdemu_device_set_current_features(self, NULL, 0);
            /* Profiles */
            struct Feature_0x0000 *f_0x0000 = cdemu_device_get_feature(self, 0x0000);
            f_0x0000->profiles[0].cur = 0;
            f_0x0000->profiles[1].cur = 0;
            break;
        }
        case PROFILE_CDROM: {
            /* Current features */
            cdemu_device_set_current_features(self, Features_PROFILE_PROFILE_CDROM, G_N_ELEMENTS(Features_PROFILE_PROFILE_CDROM));
            /* Profiles */
            struct Feature_0x0000 *f_0x0000 = cdemu_device_get_feature(self, 0x0000);
            f_0x0000->profiles[0].cur = 1;
            f_0x0000->profiles[1].cur = 0;
            break;
        }
        case PROFILE_DVDROM: {
            /* Current features */
            cdemu_device_set_current_features(self, Features_PROFILE_PROFILE_DVDROM, G_N_ELEMENTS(Features_PROFILE_PROFILE_DVDROM));
           /* Profiles */
            struct Feature_0x0000 *f_0x0000 = cdemu_device_get_feature(self, 0x0000);
            f_0x0000->profiles[0].cur = 0;
            f_0x0000->profiles[1].cur = 1;
            break;
        }
    }
}
