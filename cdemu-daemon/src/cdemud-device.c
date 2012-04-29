/*
 *  CDEmuD: Device object
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

#define __debug__ "Device"


/**********************************************************************\
 *                              Device ID                             *
\**********************************************************************/
static void cdemud_device_set_device_id (CDEMUD_Device *self, const gchar *vendor_id, const gchar *product_id, const gchar *revision, const gchar *vendor_specific)
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
gboolean cdemud_device_initialize (CDEMUD_Device *self, gint number, gchar *ctl_device, gchar *audio_driver, GError **error)
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
    mirage_debug_context_set_name(MIRAGE_DEBUG_CONTEXT(debug_context), self->priv->device_name, NULL);
    mirage_debug_context_set_domain(MIRAGE_DEBUG_CONTEXT(debug_context), "CDEMUD", NULL);
    mirage_object_set_debug_context(MIRAGE_OBJECT(self), debug_context, NULL);
    g_object_unref(debug_context);

    /* Open control device and set up I/O channel */
    self->priv->io_channel = g_io_channel_new_file(ctl_device, "r+", NULL);
    if (!self->priv->io_channel) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to open control device %s!\n", __debug__, ctl_device);
        cdemud_error(CDEMUD_E_CTLDEVICE, error);
        return FALSE;
    }

    /* Allocate buffer/"cache"; 4kB should be enough for everything, I think */
    self->priv->buffer = g_malloc0(4096);
    if (!self->priv->buffer) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to allocate buffer!\n", __debug__);
        cdemud_error(CDEMUD_E_BUFFER, error);
        return FALSE;
    }

    /* Create audio play object */
    self->priv->audio_play = g_object_new(CDEMUD_TYPE_AUDIO, NULL);
    /* Set parent */
    mirage_object_set_parent(MIRAGE_OBJECT(self->priv->audio_play), G_OBJECT(self), NULL);
    /* Attach child... so that it'll get device's debug context */
    mirage_object_attach_child(MIRAGE_OBJECT(self), self->priv->audio_play, NULL);
    /* Initialize */
    if (!cdemud_audio_initialize(CDEMUD_AUDIO(self->priv->audio_play), audio_driver, &self->priv->current_sector, self->priv->device_mutex, error)) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to initialize audio backend!\n", __debug__);
        return FALSE;
    }

    /* Create debug context for disc */
    self->priv->disc_debug = g_object_new(MIRAGE_TYPE_DEBUG_CONTEXT, NULL);
    mirage_debug_context_set_name(MIRAGE_DEBUG_CONTEXT(self->priv->disc_debug), self->priv->device_name, NULL);
    mirage_debug_context_set_domain(MIRAGE_DEBUG_CONTEXT(self->priv->disc_debug), "libMirage", NULL);

    /* Set up default device ID */
    cdemud_device_set_device_id(self, "CDEmu   ", "Virt. CD/DVD-ROM", "1.10", "    cdemu.sf.net    ");

    /* Initialise mode pages and features and set profile */
    cdemud_device_mode_pages_init(self);
    cdemud_device_features_init(self);
    cdemud_device_set_profile(self, PROFILE_NONE);

    /* Enable DPM and disable transfer rate emulation by default */
    self->priv->dpm_emulation = TRUE;
    self->priv->tr_emulation = FALSE;

    /* Start the I/O thread */
    self->priv->io_thread = cdemud_device_create_io_thread(self, error);
    if (!self->priv->io_thread) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to start I/O thread!\n", __debug__);
        cdemud_error(CDEMUD_E_GENERIC, error);
        return FALSE;
    }

    return TRUE;
}


/**********************************************************************\
 *                            Device number                           *
\**********************************************************************/
gboolean cdemud_device_get_device_number (CDEMUD_Device *self, gint *number, GError **error)
{
    CDEMUD_CHECK_ARG(number);
    *number = self->priv->number;
    return TRUE;
}


/**********************************************************************\
 *                            Device status                           *
\**********************************************************************/
static gboolean cdemud_device_get_status_private (CDEMUD_Device *self, gboolean *loaded, gchar ***file_names, GError **error G_GNUC_UNUSED)
{
    if (self->priv->loaded) {
        MIRAGE_Disc *disc = MIRAGE_DISC(self->priv->disc);
        if (loaded) {
            *loaded = TRUE;
        }
        if (file_names) {
            gchar **tmp_filenames;
            mirage_disc_get_filenames(disc, &tmp_filenames, NULL);
            *file_names = g_strdupv(tmp_filenames);
        }
    } else {
        if (loaded) {
            *loaded = FALSE;
        }
        if (file_names) {
            *file_names = g_new0(gchar *, 1); /* NULL-terminated, hence 1 */
        }
    }

    return TRUE;
}

gboolean cdemud_device_get_status (CDEMUD_Device *self, gboolean *loaded, gchar ***file_names, GError **error)
{
    gboolean succeeded = TRUE;

    g_mutex_lock(self->priv->device_mutex);
    succeeded = cdemud_device_get_status_private(self, loaded, file_names, error);
    g_mutex_unlock(self->priv->device_mutex);

    return succeeded;
}


/**********************************************************************\
 *                            Device options                          *
\**********************************************************************/
gboolean cdemud_device_get_option (CDEMUD_Device *self, gchar *option_name, GVariant **option_value, GError **error)
{
    gboolean succeeded = TRUE;
    
    *option_value = NULL;

    /* Lock */
    g_mutex_lock(self->priv->device_mutex);
    
    /* Get option */
    if (!g_strcmp0(option_name, "dpm-emulation")) {
        /* *** dpm-emulation *** */
        *option_value = g_variant_new("b", self->priv->dpm_emulation);
    } else if (!g_strcmp0(option_name, "tr-emulation")) {
        /* *** tr-emulation *** */
        *option_value = g_variant_new("b", self->priv->tr_emulation);
    } else if (!g_strcmp0(option_name, "device-id")) {
        /* *** device-id *** */
        *option_value = g_variant_new("(ssss)", self->priv->id_vendor_id, self->priv->id_product_id, self->priv->id_revision, self->priv->id_vendor_specific);
    } else if (!g_strcmp0(option_name, "daemon-debug-mask")) {
        /* *** daemon-debug-mask *** */
        GObject *context;
        succeeded = mirage_object_get_debug_context(MIRAGE_OBJECT(self), &context, error);
        if (succeeded) {
            gint mask;
            succeeded = mirage_debug_context_get_debug_mask(MIRAGE_DEBUG_CONTEXT(context), &mask, error);
            g_object_unref(context);
            
            *option_value = g_variant_new("i", mask);
        }
    } else if (!g_strcmp0(option_name, "library-debug-mask")) {
        /* *** library-debug-mask *** */
        gint mask;
        succeeded = mirage_debug_context_get_debug_mask(MIRAGE_DEBUG_CONTEXT(self->priv->disc_debug), &mask, error);
        if (succeeded) {
            *option_value = g_variant_new("i", mask);
        }
    } else {
        /* Option not found */
        CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: option '%s' not found; client bug?\n", __debug__, option_name);
        cdemud_error(CDEMUD_E_INVALIDARG, error);
        succeeded = FALSE;
    }
    
    /* Unlock */
    g_mutex_unlock(self->priv->device_mutex);

    return succeeded;
}

gboolean cdemud_device_set_option (CDEMUD_Device *self, gchar *option_name, GVariant *option_value, GError **error)
{
    gboolean succeeded = TRUE;

    /* Lock */
    g_mutex_lock(self->priv->device_mutex);
    
    /* Get option */
    if (!g_strcmp0(option_name, "dpm-emulation")) {
        /* *** dpm-emulation *** */
        if (!g_variant_is_of_type(option_value, G_VARIANT_TYPE("b"))) {
            cdemud_error(CDEMUD_E_INVALIDARG, error);
            succeeded = FALSE;
        } else {
            g_variant_get(option_value, "b", &self->priv->dpm_emulation);
        }
    } else if (!g_strcmp0(option_name, "tr-emulation")) {
        /* *** tr-emulation *** */
        if (!g_variant_is_of_type(option_value, G_VARIANT_TYPE("b"))) {
            cdemud_error(CDEMUD_E_INVALIDARG, error);
            succeeded = FALSE;
        } else {
            g_variant_get(option_value, "b", &self->priv->tr_emulation);
        }
    } else if (!g_strcmp0(option_name, "device-id")) {
        /* *** device-id *** */
        if (!g_variant_is_of_type(option_value, G_VARIANT_TYPE("(ssss)"))) {
            cdemud_error(CDEMUD_E_INVALIDARG, error);
            succeeded = FALSE;
        } else {
            gchar *vendor_id, *product_id, *revision, *vendor_specific;
            g_variant_get(option_value, "(ssss)", &vendor_id, &product_id, &revision, &vendor_specific);
            
            cdemud_device_set_device_id(self, vendor_id, product_id, revision, vendor_specific);
            
            g_free(vendor_id);
            g_free(product_id);
            g_free(revision);
            g_free(vendor_specific);
        }
    } else if (!g_strcmp0(option_name, "daemon-debug-mask")) {
        /* *** daemon-debug-mask *** */
        if (!g_variant_is_of_type(option_value, G_VARIANT_TYPE("i"))) {
            cdemud_error(CDEMUD_E_INVALIDARG, error);
            succeeded = FALSE;
        } else {
            GObject *context;
            succeeded = mirage_object_get_debug_context(MIRAGE_OBJECT(self), &context, error);
            if (succeeded) {
                gint mask;
                g_variant_get(option_value, "i", &mask);
                succeeded = mirage_debug_context_set_debug_mask(MIRAGE_DEBUG_CONTEXT(context), mask, error);
                g_object_unref(context);
            }
        }
    } else if (!g_strcmp0(option_name, "library-debug-mask")) {
        /* *** library-debug-mask *** */
        if (!g_variant_is_of_type(option_value, G_VARIANT_TYPE("i"))) {
            cdemud_error(CDEMUD_E_INVALIDARG, error);
            succeeded = FALSE;
        } else {
            gint mask;
            g_variant_get(option_value, "i", &mask);
            succeeded = mirage_debug_context_set_debug_mask(MIRAGE_DEBUG_CONTEXT(self->priv->disc_debug), mask, error);
        }
    } else {
        /* Option not found */
        CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: option '%s' not found; client bug?\n", __debug__, option_name);
        
        cdemud_error(CDEMUD_E_INVALIDARG, error);
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
G_DEFINE_TYPE(CDEMUD_Device, cdemud_device, MIRAGE_TYPE_OBJECT);

static void cdemud_device_init (CDEMUD_Device *self)
{
    self->priv = CDEMUD_DEVICE_GET_PRIVATE(self);

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

static void cdemud_device_dispose (GObject *gobject)
{
    CDEMUD_Device *self = CDEMUD_DEVICE(gobject);

    /* Stop the I/O thread */
    cdemud_device_stop_io_thread(self);

    /* Unref I/O channel */
    if (self->priv->io_channel) {
        g_io_channel_unref(self->priv->io_channel);
        self->priv->io_channel = NULL;
    }

    /* Unload disc */
    cdemud_device_unload_disc(self, NULL);

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
    return G_OBJECT_CLASS(cdemud_device_parent_class)->dispose(gobject);
}

static void cdemud_device_finalize (GObject *gobject)
{
    CDEMUD_Device *self = CDEMUD_DEVICE(gobject);
    
    /* Free mode pages */
    cdemud_device_mode_pages_cleanup(self);
    
    /* Free features */
    cdemud_device_features_cleanup(self);

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
    return G_OBJECT_CLASS(cdemud_device_parent_class)->finalize(gobject);
}

static void cdemud_device_class_init (CDEMUD_DeviceClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = cdemud_device_dispose;
    gobject_class->finalize = cdemud_device_finalize;

    /* Signals */
    klass->signals[0] = g_signal_new("status-changed", G_OBJECT_CLASS_TYPE(klass), (G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED), 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, NULL);
    klass->signals[1] = g_signal_new("option-changed", G_OBJECT_CLASS_TYPE(klass), (G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED), 0, NULL, NULL, g_cclosure_marshal_VOID__STRING, G_TYPE_NONE, 1, G_TYPE_STRING, NULL);

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(CDEMUD_DevicePrivate));
}
