 /*
 *  CDEmu daemon: Device object - Disc load/unload
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

#define __debug__ "Device"


/**********************************************************************\
 *                              Load disc                             *
\**********************************************************************/
static gboolean cdemu_device_load_disc_private (CdemuDevice *self, gchar **filenames, GVariant *options, GError **error)
{
    gint media_type;
    gboolean blank_disc = FALSE;

     /* Well, we won't do anything if we're already loaded */
    if (self->priv->loaded) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: device already loaded\n", __debug__);
        g_set_error(error, CDEMU_ERROR, CDEMU_ERROR_ALREADY_LOADED, "Device is already loaded!");
        return FALSE;
    }

    /* Set options to the context */
    mirage_context_clear_options(self->priv->mirage_context);
    for (guint i = 0; i < g_variant_n_children(options); i++) {
        gchar *key;
        GVariant *value;

        g_variant_get_child(options, i, "{sv}", &key, &value);
        mirage_context_set_option(self->priv->mirage_context, key, value);

        /* Check if we are required to create a blank disc */
        if (!g_strcmp0(key, "create")) {
            blank_disc = TRUE;
        }
    }

    if (!blank_disc) {
        /* Load... */
        self->priv->disc = mirage_context_load_image(self->priv->mirage_context, filenames, error);

        /* Check if loading succeeded */
        if (!self->priv->disc) {
            return FALSE;
        }

        /* Set current profile (and modify feature flags accordingly */
        media_type = mirage_disc_get_medium_type(self->priv->disc);
        switch (media_type) {
            case MIRAGE_MEDIUM_CD: {
                cdemu_device_set_profile(self, ProfileIndex_CDROM);
                break;
            }
            case MIRAGE_MEDIUM_DVD: {
                cdemu_device_set_profile(self, ProfileIndex_DVDROM);
                break;
            }
            default: {
                CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: unknown media type: 0x%X!\n", __debug__, media_type);
                break;
            }
        }

        /* Mark loaded discs as non-writable */
        self->priv->recordable_disc = FALSE;
        self->priv->rewritable_disc = FALSE;
    } else {
        /* Create blank disc */
        self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);

#if 0
        /* Even a blank disc has one session and one track */
        MirageSession *session = g_object_new(MIRAGE_TYPE_SESSION, NULL);
        mirage_disc_add_session_by_number(self->priv->disc, 1, session, NULL);
        g_object_unref(session);

        MirageTrack *track = g_object_new(MIRAGE_TYPE_TRACK, NULL);
        mirage_disc_add_track_by_number(self->priv->disc, 1, track, NULL);
        g_object_unref(track);
#endif

        /* Set filenames */
        mirage_disc_set_filenames(self->priv->disc, (const gchar **)filenames);

        /* Emulate 80-min CD-R for now */
        self->priv->recordable_disc = TRUE;
        self->priv->rewritable_disc = FALSE;
        self->priv->medium_capacity = 80*60*75;

        self->priv->next_writable_address = 0;
        self->priv->open_session = NULL;
        self->priv->open_track = NULL;

        cdemu_device_set_profile(self, ProfileIndex_CDR);
    }

    /* Loading succeeded */
    self->priv->loaded = TRUE;
    self->priv->media_event = MEDIA_EVENT_NEW_MEDIA;

    /* Send notification */
    g_signal_emit_by_name(self, "status-changed", NULL);

    return TRUE;
}

gboolean cdemu_device_load_disc (CdemuDevice *self, gchar **filenames, GVariant *options, GError **error)
{
    gboolean succeeded = TRUE;

    /* Load */
    g_mutex_lock(self->priv->device_mutex);
    succeeded = cdemu_device_load_disc_private(self, filenames, options, error);
    g_mutex_unlock(self->priv->device_mutex);

    return succeeded;
}


/**********************************************************************\
 *                              Unload disc                           *
\**********************************************************************/
gboolean cdemu_device_unload_disc_private (CdemuDevice *self, gboolean force, GError **error)
{
    /* We have to report eject request, even if it doesn't actually get carried
       out, for example because device is locked. However, this should give
       HAL a clue that it might be good idea to unlock the device... */
    self->priv->media_event = MEDIA_EVENT_EJECTREQUEST;

    /* Check if the door is locked */
    if (!force && self->priv->locked) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: device is locked\n", __debug__);
        g_set_error(error, CDEMU_ERROR, CDEMU_ERROR_DEVICE_LOCKED, "Device is locked!");
        return FALSE;
    }

    /* Unload only if we're loaded */
    if (self->priv->loaded) {
        /* Delete disc */
        g_object_unref(self->priv->disc);
        /* We're not loaded anymore, and media got changed */
        self->priv->loaded = FALSE;
        self->priv->media_event = MEDIA_EVENT_MEDIA_REMOVAL;
        /* Current profile: None */
        cdemu_device_set_profile(self, ProfileIndex_NONE);

        /* Send notification */
        g_signal_emit_by_name(self, "status-changed", NULL);
    }

    return TRUE;
}

gboolean cdemu_device_unload_disc (CdemuDevice *self, GError **error)
{
    gboolean succeeded;

    g_mutex_lock(self->priv->device_mutex);
    succeeded = cdemu_device_unload_disc_private(self, FALSE, error);
    g_mutex_unlock(self->priv->device_mutex);

    /* Currently, the only case of unload command failing is when device is
       locked. However, in that case, the unload attempt is reported, and if
       HAL daemon is running, it will try to unlock the device and unload it
       again. So in order not to bother clients with device locked error when
       device will most likely get unloaded anyway, we ignore the command's
       return status here... */
    succeeded = TRUE;
    return succeeded;
}
