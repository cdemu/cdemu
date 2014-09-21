/*
 *  CDEmu daemon: device - disc load/unload
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

#define __debug__ "Device"


/**********************************************************************\
 *                              Load disc                             *
\**********************************************************************/
static gboolean cdemu_device_load_disc_private (CdemuDevice *self, gchar **filenames, GVariant *options, GError **error)
{
    gint medium_type;

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
    }

    /* Load... */
    self->priv->disc = mirage_context_load_image(self->priv->mirage_context, filenames, error);

    /* Check if loading succeeded */
    if (!self->priv->disc) {
        return FALSE;
    }

    /* Set current profile (and modify feature flags accordingly */
    medium_type = mirage_disc_get_medium_type(self->priv->disc);
    switch (medium_type) {
        case MIRAGE_MEDIUM_CD: {
            cdemu_device_set_profile(self, ProfileIndex_CDROM);
            break;
        }
        case MIRAGE_MEDIUM_DVD: {
            cdemu_device_set_profile(self, ProfileIndex_DVDROM);
            break;
        }
        default: {
            CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: unknown media type: 0x%X!\n", __debug__, medium_type);
            break;
        }
    }

    /* Mark loaded discs as non-writable */
    self->priv->recordable_disc = FALSE;
    self->priv->rewritable_disc = FALSE;

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
 *                         Create (blank) disc                        *
\**********************************************************************/
static gboolean cdemu_device_create_blank_disc_private (CdemuDevice *self, const gchar *filename, GVariant *options, GError **error)
{
    const gchar *writer_id = NULL;
    gint medium_type = MIRAGE_MEDIUM_CD;
    gint medium_capacity = 80*60*75; /* 80 min */

    /* Well, we won't do anything if we're already loaded */
    if (self->priv->loaded) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_MMC, "%s: device already loaded\n", __debug__);
        g_set_error(error, CDEMU_ERROR, CDEMU_ERROR_ALREADY_LOADED, "Device is already loaded!");
        return FALSE;
    }

    /* Extract and parse some of options */
    GHashTable *writer_parameters = g_hash_table_new(g_str_hash, g_str_equal);

    for (guint i = 0; i < g_variant_n_children(options); i++) {
        gchar *key;
        GVariant *value;

        g_variant_get_child(options, i, "{sv}", &key, &value);

        if (!g_ascii_strcasecmp(key, "writer-id")) {
            writer_id = g_variant_get_string(value, NULL);
        } else if (!g_ascii_strcasecmp(key, "medium-type")) {
            const gchar *medium_string = g_variant_get_string(value, NULL);
            if (!g_ascii_strcasecmp(medium_string, "cdr74")) {
                /* 74-minute CD-R (650 MB) */
                medium_type = MIRAGE_MEDIUM_CD;
                medium_capacity = 74*60*75;
            } else if (!g_ascii_strcasecmp(medium_string, "cdr80")) {
                /* 80-minute CD-R (700 MB) */
                medium_type = MIRAGE_MEDIUM_CD;
                medium_capacity = 80*60*75;
            } else if (!g_ascii_strcasecmp(medium_string, "cdr90")) {
                /* 90-minute CD-R (800 MB) */
                medium_type = MIRAGE_MEDIUM_CD;
                medium_capacity = 90*60*75;
            } else if (!g_ascii_strcasecmp(medium_string, "cdr99")) {
                /* 99-minute CD-R (900 MB) */
                medium_type = MIRAGE_MEDIUM_CD;
                medium_capacity = 99*60*75;
            } else if (!g_ascii_strcasecmp(medium_string, "dvd+r")) {
                /* DVD+R Single-Layer */
                medium_type = MIRAGE_MEDIUM_DVD;
                medium_capacity = 2295104;
            } else {
                g_set_error(error, CDEMU_ERROR, CDEMU_ERROR_INVALID_ARGUMENT, "Invalid medium type '%s'!", medium_string);
                return FALSE;
            }
        } else if (g_str_has_prefix(key, "writer.")) {
            g_hash_table_insert(writer_parameters, key, value);
        }
    }

    /* Image writer ID must be provided in parameters */
    if (!writer_id) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: no image writer ID provided in parameters!\n", __debug__);
        g_set_error(error, CDEMU_ERROR, CDEMU_ERROR_DAEMON_ERROR, "no image writer ID provided in parameters!");
        g_hash_table_unref(writer_parameters);
        return FALSE;
    }

    /* Create writer and attach our context to it */
    self->priv->image_writer = mirage_create_writer(writer_id, error);
    if (!self->priv->image_writer) {
        g_hash_table_unref(writer_parameters);
        return FALSE;
    }
    mirage_contextual_set_context(MIRAGE_CONTEXTUAL(self->priv->image_writer), self->priv->mirage_context);

    /* Create blank disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_contextual_set_context(MIRAGE_CONTEXTUAL(self->priv->disc), self->priv->mirage_context);

    /* Set filenames */
    mirage_disc_set_filename(self->priv->disc, filename);

    /* Emulate 80-min CD-R or DVD+R SL for now */
    self->priv->recordable_disc = TRUE;
    self->priv->rewritable_disc = FALSE;

    mirage_disc_set_medium_type(self->priv->disc, medium_type);
    self->priv->medium_capacity = medium_capacity;
    if (medium_type == MIRAGE_MEDIUM_CD) {
        self->priv->medium_leadin = -11077;
        mirage_disc_layout_set_start_sector(self->priv->disc, -150);
    } else {
        self->priv->medium_leadin = 0;
        mirage_disc_layout_set_start_sector(self->priv->disc, 0);
    }

    /* Initialize image writer with this disc */
    if (!mirage_writer_open_image(self->priv->image_writer, self->priv->disc, writer_parameters, error)) {
        g_object_unref(self->priv->disc);
        self->priv->disc = NULL;

        g_object_unref(self->priv->image_writer);
        self->priv->image_writer = NULL;

        g_hash_table_unref(writer_parameters);

        return FALSE;
    }
    g_hash_table_unref(writer_parameters);


    self->priv->num_written_sectors = 0;

    self->priv->open_session = NULL;
    self->priv->open_track = NULL;

    cdemu_device_set_profile(self, (medium_type == MIRAGE_MEDIUM_CD) ? ProfileIndex_CDR : ProfileIndex_DVDPLUSR);

    /* Set default recording mode */
    cdemu_device_recording_set_mode(self, 1); /* TAO */

    /* Loading succeeded */
    self->priv->loaded = TRUE;
    self->priv->media_event = MEDIA_EVENT_NEW_MEDIA;

    /* Send notification */
    g_signal_emit_by_name(self, "status-changed", NULL);

    return TRUE;
}


gboolean cdemu_device_create_blank_disc (CdemuDevice *self, const gchar *filename, GVariant *options, GError **error)
{
    gboolean succeeded = TRUE;

    /* Create */
    g_mutex_lock(self->priv->device_mutex);
    succeeded = cdemu_device_create_blank_disc_private(self, filename, options, error);
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
        self->priv->disc = NULL;

        /* We're not loaded anymore, and media got changed */
        self->priv->loaded = FALSE;
        self->priv->media_event = MEDIA_EVENT_MEDIA_REMOVAL;

        /* Clear burning emulation stuff */
        if (self->priv->open_track) {
            g_object_unref(self->priv->open_track);
            self->priv->open_track = NULL;
        }

        if (self->priv->open_session) {
            g_object_unref(self->priv->open_session);
            self->priv->open_session = NULL;
        }

        if (self->priv->image_writer) {
            g_object_unref(self->priv->image_writer);
            self->priv->image_writer = NULL;
        }

        self->priv->disc_closed = FALSE;
        self->priv->recordable_disc = FALSE;
        self->priv->rewritable_disc = FALSE;
        self->priv->num_written_sectors = 0;

        if (self->priv->leadin_cdtext_packs) {
            g_slist_free_full(self->priv->leadin_cdtext_packs, (GDestroyNotify)g_free);
            self->priv->leadin_cdtext_packs = NULL;
        }
        self->priv->num_leadin_cdtext_packs = 0;

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
