/*
 *  CDEmu daemon: Device object
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

#define __debug__ "Device"


/**********************************************************************\
 *                              Device ID                             *
\**********************************************************************/
static void cdemu_device_set_device_id (CdemuDevice *self, const gchar *vendor_id, const gchar *product_id, const gchar *revision, const gchar *vendor_specific)
{
    g_free(self->priv->id_vendor_id);
    self->priv->id_vendor_id = g_strndup(vendor_id, 8);

    g_free(self->priv->id_product_id);
    self->priv->id_product_id = g_strndup(product_id, 16);

    g_free(self->priv->id_revision);
    self->priv->id_revision = g_strndup(revision, 4);

    g_free(self->priv->id_vendor_specific);
    self->priv->id_vendor_specific = g_strndup(vendor_specific, 20);
}



/**********************************************************************\
 *                            Device init                             *
\**********************************************************************/
gboolean cdemu_device_initialize (CdemuDevice *self, gint number, gchar *ctl_device, gchar *audio_driver)
{
    GObject *debug_context;

    self->priv->mapping_complete = FALSE;

    /* Set device number and device name */
    self->priv->number = number;
    self->priv->device_name = g_strdup_printf("cdemu%i", number);

    /* Init device mutex */
    self->priv->device_mutex = g_mutex_new();

    /* Create debug context for device */
    debug_context = g_object_new(MIRAGE_TYPE_DEBUG_CONTEXT, NULL);
    mirage_debug_context_set_name(MIRAGE_DEBUG_CONTEXT(debug_context), self->priv->device_name);
    mirage_debug_context_set_domain(MIRAGE_DEBUG_CONTEXT(debug_context), "CDEMU");
    mirage_debuggable_set_debug_context(MIRAGE_DEBUGGABLE(self), debug_context);
    g_object_unref(debug_context);

    /* Open control device and set up I/O channel */
    self->priv->io_channel = g_io_channel_new_file(ctl_device, "r+", NULL);
    if (!self->priv->io_channel) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to open control device %s!\n", __debug__, ctl_device);
        return FALSE;
    }

    /* Allocate buffer/"cache"; 4kB should be enough for everything, I think */
    self->priv->buffer = g_try_malloc0(4096);
    if (!self->priv->buffer) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to allocate buffer!\n", __debug__);
        return FALSE;
    }

    /* Create audio play object */
    self->priv->audio_play = g_object_new(CDEMU_TYPE_AUDIO, NULL);
    /* Set parent */
    mirage_object_set_parent(MIRAGE_OBJECT(self->priv->audio_play), G_OBJECT(self));
    /* Attach child... so that it'll get device's debug context */
    mirage_object_attach_child(MIRAGE_OBJECT(self), self->priv->audio_play);
    /* Initialize */
    cdemu_audio_initialize(CDEMU_AUDIO(self->priv->audio_play), audio_driver, &self->priv->current_address, self->priv->device_mutex);

    /* Create debug context for disc */
    self->priv->disc_debug = g_object_new(MIRAGE_TYPE_DEBUG_CONTEXT, NULL);
    mirage_debug_context_set_name(MIRAGE_DEBUG_CONTEXT(self->priv->disc_debug), self->priv->device_name);
    mirage_debug_context_set_domain(MIRAGE_DEBUG_CONTEXT(self->priv->disc_debug), "libMirage");

    /* Set up default device ID */
    cdemu_device_set_device_id(self, "CDEmu   ", "Virt. CD/DVD-ROM", "1.10", "    cdemu.sf.net    ");

    /* Initialise mode pages and features and set profile */
    cdemu_device_mode_pages_init(self);
    cdemu_device_features_init(self);
    cdemu_device_set_profile(self, PROFILE_NONE);

    /* Enable DPM and disable transfer rate emulation by default */
    self->priv->dpm_emulation = TRUE;
    self->priv->tr_emulation = FALSE;

    /* Start the I/O thread */
    self->priv->io_thread = cdemu_device_create_io_thread(self);
    if (!self->priv->io_thread) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to start I/O thread!\n", __debug__);
        return FALSE;
    }

    return TRUE;
}


/**********************************************************************\
 *                            Device number                           *
\**********************************************************************/
gint cdemu_device_get_device_number (CdemuDevice *self)
{
    return self->priv->number;
}


/**********************************************************************\
 *                            Device status                           *
\**********************************************************************/
gboolean cdemu_device_get_status (CdemuDevice *self, gchar ***file_names)
{
    gboolean loaded;

    g_mutex_lock(self->priv->device_mutex);

    loaded = self->priv->loaded;
    if (loaded) {
        MirageDisc *disc = MIRAGE_DISC(self->priv->disc);
        if (file_names) {
            const gchar **tmp_filenames = mirage_disc_get_filenames(disc);
            *file_names = g_strdupv((gchar **)tmp_filenames);
        }
    } else {
        if (file_names) {
            *file_names = g_new0(gchar *, 1); /* NULL-terminated, hence 1 */
        }
    }

    g_mutex_unlock(self->priv->device_mutex);

    return loaded;
}


/**********************************************************************\
 *                            Device options                          *
\**********************************************************************/
GVariant *cdemu_device_get_option (CdemuDevice *self, gchar *option_name, GError **error)
{
    GVariant *option_value = NULL;

    /* Lock */
    g_mutex_lock(self->priv->device_mutex);

    /* Get option */
    if (!g_strcmp0(option_name, "dpm-emulation")) {
        /* *** dpm-emulation *** */
        option_value = g_variant_new("b", self->priv->dpm_emulation);
    } else if (!g_strcmp0(option_name, "tr-emulation")) {
        /* *** tr-emulation *** */
        option_value = g_variant_new("b", self->priv->tr_emulation);
    } else if (!g_strcmp0(option_name, "device-id")) {
        /* *** device-id *** */
        option_value = g_variant_new("(ssss)", self->priv->id_vendor_id, self->priv->id_product_id, self->priv->id_revision, self->priv->id_vendor_specific);
    } else if (!g_strcmp0(option_name, "daemon-debug-mask")) {
        /* *** daemon-debug-mask *** */
        GObject *context = mirage_debuggable_get_debug_context(MIRAGE_DEBUGGABLE(self));
        if (context) {
            gint mask = mirage_debug_context_get_debug_mask(MIRAGE_DEBUG_CONTEXT(context));
            option_value = g_variant_new("i", mask);
        }
    } else if (!g_strcmp0(option_name, "library-debug-mask")) {
        /* *** library-debug-mask *** */
        gint mask = mirage_debug_context_get_debug_mask(MIRAGE_DEBUG_CONTEXT(self->priv->disc_debug));
        option_value = g_variant_new("i", mask);
    } else {
        /* Option not found */
        CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: option '%s' not found; client bug?\n", __debug__, option_name);
        g_set_error(error, CDEMU_ERROR, CDEMU_ERROR_INVALID_ARGUMENT, "Invalid option name '%s'!", option_name);
    }

    /* Unlock */
    g_mutex_unlock(self->priv->device_mutex);

    return option_value;
}

gboolean cdemu_device_set_option (CdemuDevice *self, gchar *option_name, GVariant *option_value, GError **error)
{
    gboolean succeeded = TRUE;

    /* Lock */
    g_mutex_lock(self->priv->device_mutex);

    /* Get option */
    if (!g_strcmp0(option_name, "dpm-emulation")) {
        /* *** dpm-emulation *** */
        if (!g_variant_is_of_type(option_value, G_VARIANT_TYPE("b"))) {
            g_set_error(error, CDEMU_ERROR, CDEMU_ERROR_INVALID_ARGUMENT, "Invalid argument type for option '%s'!", option_name);
            succeeded = FALSE;
        } else {
            g_variant_get(option_value, "b", &self->priv->dpm_emulation);
        }
    } else if (!g_strcmp0(option_name, "tr-emulation")) {
        /* *** tr-emulation *** */
        if (!g_variant_is_of_type(option_value, G_VARIANT_TYPE("b"))) {
            g_set_error(error, CDEMU_ERROR, CDEMU_ERROR_INVALID_ARGUMENT, "Invalid argument type for option '%s'!", option_name);
            succeeded = FALSE;
        } else {
            g_variant_get(option_value, "b", &self->priv->tr_emulation);
        }
    } else if (!g_strcmp0(option_name, "device-id")) {
        /* *** device-id *** */
        if (!g_variant_is_of_type(option_value, G_VARIANT_TYPE("(ssss)"))) {
            g_set_error(error, CDEMU_ERROR, CDEMU_ERROR_INVALID_ARGUMENT, "Invalid argument type for option '%s'!", option_name);
            succeeded = FALSE;
        } else {
            gchar *vendor_id, *product_id, *revision, *vendor_specific;
            g_variant_get(option_value, "(ssss)", &vendor_id, &product_id, &revision, &vendor_specific);

            cdemu_device_set_device_id(self, vendor_id, product_id, revision, vendor_specific);

            g_free(vendor_id);
            g_free(product_id);
            g_free(revision);
            g_free(vendor_specific);
        }
    } else if (!g_strcmp0(option_name, "daemon-debug-mask")) {
        /* *** daemon-debug-mask *** */
        if (!g_variant_is_of_type(option_value, G_VARIANT_TYPE("i"))) {
            g_set_error(error, CDEMU_ERROR, CDEMU_ERROR_INVALID_ARGUMENT, "Invalid argument type for option '%s'!", option_name);
            succeeded = FALSE;
        } else {
            GObject *context = mirage_debuggable_get_debug_context(MIRAGE_DEBUGGABLE(self));
            if (context) {
                gint mask;
                g_variant_get(option_value, "i", &mask);
                mirage_debug_context_set_debug_mask(MIRAGE_DEBUG_CONTEXT(context), mask);
            }
        }
    } else if (!g_strcmp0(option_name, "library-debug-mask")) {
        /* *** library-debug-mask *** */
        if (!g_variant_is_of_type(option_value, G_VARIANT_TYPE("i"))) {
            g_set_error(error, CDEMU_ERROR, CDEMU_ERROR_INVALID_ARGUMENT, "Invalid argument type for option '%s'!", option_name);
            succeeded = FALSE;
        } else {
            gint mask;
            g_variant_get(option_value, "i", &mask);
            mirage_debug_context_set_debug_mask(MIRAGE_DEBUG_CONTEXT(self->priv->disc_debug), mask);
        }
    } else {
        /* Option not found */
        CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: option '%s' not found; client bug?\n", __debug__, option_name);
        g_set_error(error, CDEMU_ERROR, CDEMU_ERROR_INVALID_ARGUMENT, "Invalid option name '%s'!", option_name);
        succeeded = FALSE;
    }

    /* Unlock */
    g_mutex_unlock(self->priv->device_mutex);

    /* Signal that option has been changed */
    if (succeeded) {
        g_signal_emit_by_name(self, "option-changed", option_name, NULL);
    }

    return succeeded;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE(CdemuDevice, cdemu_device, MIRAGE_TYPE_OBJECT);

static void cdemu_device_init (CdemuDevice *self)
{
    self->priv = CDEMU_DEVICE_GET_PRIVATE(self);

    self->priv->io_channel = NULL;
    self->priv->io_thread = NULL;
    self->priv->main_context = NULL;
    self->priv->main_loop = NULL;

    self->priv->device_name = NULL;
    self->priv->device_mutex = NULL;

    self->priv->buffer = NULL;

    self->priv->audio_play = NULL;

    self->priv->disc = NULL;
    self->priv->disc_debug = NULL;

    self->priv->mode_pages_list = NULL;

    self->priv->features_list = NULL;

    self->priv->id_vendor_id = NULL;
    self->priv->id_product_id = NULL;
    self->priv->id_revision = NULL;
    self->priv->id_vendor_specific = NULL;

    self->priv->device_sg = NULL;
    self->priv->device_sr = NULL;
}

static void cdemu_device_dispose (GObject *gobject)
{
    CdemuDevice *self = CDEMU_DEVICE(gobject);

    /* Stop the I/O thread */
    cdemu_device_stop_io_thread(self);

    /* Unref I/O channel */
    if (self->priv->io_channel) {
        g_io_channel_unref(self->priv->io_channel);
        self->priv->io_channel = NULL;
    }

    /* Unload disc */
    cdemu_device_unload_disc(self, NULL);

    /* Unref audio play object */
    if (self->priv->audio_play) {
        g_object_unref(self->priv->audio_play);
        self->priv->audio_play = NULL;
    }

    /* Unref debug context */
    if (self->priv->disc_debug) {
        g_object_unref(self->priv->disc_debug);
        self->priv->disc_debug = NULL;
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(cdemu_device_parent_class)->dispose(gobject);
}

static void cdemu_device_finalize (GObject *gobject)
{
    CdemuDevice *self = CDEMU_DEVICE(gobject);

    /* Free mode pages */
    cdemu_device_mode_pages_cleanup(self);

    /* Free features */
    cdemu_device_features_cleanup(self);

    /* Free device map */
    g_free(self->priv->device_sg);
    g_free(self->priv->device_sr);

    /* Free buffer/"cache" */
    g_free(self->priv->buffer);

    /* Free device name */
    g_free(self->priv->device_name);

    /* Free device ID */
    g_free(self->priv->id_vendor_id);
    g_free(self->priv->id_product_id);
    g_free(self->priv->id_revision);
    g_free(self->priv->id_vendor_specific);

    /* Free mutex */
    g_mutex_free(self->priv->device_mutex);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(cdemu_device_parent_class)->finalize(gobject);
}

static void cdemu_device_class_init (CdemuDeviceClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = cdemu_device_dispose;
    gobject_class->finalize = cdemu_device_finalize;

    /* Signals */
    klass->signals[0] = g_signal_new("status-changed", G_OBJECT_CLASS_TYPE(klass), (G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED), 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, NULL);
    klass->signals[1] = g_signal_new("option-changed", G_OBJECT_CLASS_TYPE(klass), (G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED), 0, NULL, NULL, g_cclosure_marshal_VOID__STRING, G_TYPE_NONE, 1, G_TYPE_STRING, NULL);

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(CdemuDevicePrivate));
}
