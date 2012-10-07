/*
 *  CDEmu daemon: Daemon object
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
#include "cdemu-daemon-private.h"

#define __debug__ "Daemon"


/**********************************************************************\
 *                           Signal handlers                          *
\**********************************************************************/
static void device_status_changed_handler (CdemuDevice *device, CdemuDaemon *self)
{
    gint number = cdemu_device_get_device_number(device);
    cdemu_daemon_dbus_emit_device_status_changed(self, number);
}

static void device_option_changed_handler (CdemuDevice *device, gchar *option, CdemuDaemon *self)
{
    gint number = cdemu_device_get_device_number(device);
    cdemu_daemon_dbus_emit_device_option_changed(self, number, option);
}


/**********************************************************************\
 *                           Device mapping                           *
\**********************************************************************/
static gboolean device_mapping_callback (CdemuDaemon *self)
{
    gboolean run_again = FALSE;

    /* Try to setup mapping on each device */
    for (gint i = 0; i < self->priv->number_of_devices; i++) {
        CdemuDevice *device = cdemu_daemon_get_device(self, i, NULL);

        run_again = !cdemu_device_setup_mapping(device);
        g_object_unref(device);

        /* Try again later? */
        if (run_again) {
            break;
        }
    }

    /* After five attempts, give up */
    if (self->priv->mapping_attempt++ > 5) {
        run_again = FALSE;
    }

    /* If we're done here, it's time to send the "DeviceMappingsReady" signal */
    if (!run_again) {
        cdemu_daemon_dbus_emit_device_mappings_ready(self);
    }

    return run_again;
}


/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
gboolean cdemu_daemon_initialize_and_start (CdemuDaemon *self, gint num_devices, gchar *ctl_device, gchar *audio_driver, gboolean system_bus)
{
    MirageDebugContext *debug_context;
    GBusType bus_type = system_bus ? G_BUS_TYPE_SYSTEM : G_BUS_TYPE_SESSION;

    /* Debug context; so that we get daemon's errors/warnings from the very beginning */
    debug_context = g_object_new(MIRAGE_TYPE_DEBUG_CONTEXT, NULL);
    mirage_debug_context_set_name(debug_context, "cdemu");
    mirage_debug_context_set_domain(debug_context, "CDEMU");
    mirage_debuggable_set_debug_context(MIRAGE_DEBUGGABLE(self), debug_context);
    g_object_unref(debug_context);

    /* Control device */
    self->priv->ctl_device = g_strdup(ctl_device);

    /* Number of devices */
    self->priv->number_of_devices = num_devices;

    /* Glib's main loop */
    self->priv->main_loop = g_main_loop_new(NULL, FALSE);

    /* Initialize libao */
    ao_initialize();

    /* Make sure the D-BUS name we're going to use isn't taken already (by another
       instance of the server). We're actually going to claim it once we create
       the devices, but we want to avoid the device creation if name claim is
       already doomed to fail... */
    if (!cdemu_daemon_dbus_check_if_name_is_available(self, bus_type)) {
        return FALSE;
    }

    /* Create desired number of devices */
    for (gint i = 0; i < self->priv->number_of_devices; i++) {
        /* Create CDEmu device object */
        CdemuDevice *dev = g_object_new(CDEMU_TYPE_DEVICE, NULL);

        if (cdemu_device_initialize(dev, i, self->priv->ctl_device, audio_driver)) {
            /* Don't set parent, as devices have their own debug contexts */
            /* Add handling for signals from the device... this allows us to
               pass them on via DBUS */
            g_signal_connect(dev, "status-changed", (GCallback)device_status_changed_handler, self);
            g_signal_connect(dev, "option-changed", (GCallback)device_option_changed_handler, self);

            /* Add it to devices list */
            self->priv->list_of_devices = g_list_append(self->priv->list_of_devices, dev);
        } else {
            CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to initialize device %i!\n", __debug__, i);
            g_object_unref(dev);
            return FALSE;
        }
    }

    /* In order to build device mapping, we'll have to fire our callback sometime
       after the daemon actually starts (so that command handler is actually
       active and the SCSI layer actually does device registration on the kernel
       side)... */
    self->priv->mapping_id = g_timeout_add(1000, (GSourceFunc)device_mapping_callback, self);


    /* Register on D-Bus bus */
    cdemu_daemon_dbus_register_on_bus(self, bus_type);

    /* Run the main loop */
    g_main_loop_run(self->priv->main_loop);


    /* Cleanup D-Bus */
    cdemu_daemon_dbus_cleanup(self);

    return TRUE;
}

void cdemu_daemon_stop_daemon (CdemuDaemon *self)
{
    /* Stop the main loop */
    g_main_loop_quit(self->priv->main_loop);
}


CdemuDevice *cdemu_daemon_get_device (CdemuDaemon *self, gint device_number, GError **error)
{
    /* Get device */
    if (device_number >= 0 && device_number < self->priv->number_of_devices) {
        CdemuDevice *device = g_list_nth_data(self->priv->list_of_devices, device_number);
        g_object_ref(device);
        return device;
    }

    g_set_error(error, CDEMU_ERROR, CDEMU_ERROR_INVALID_ARGUMENT, "Invalid device number!");
    return NULL;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE(CdemuDaemon, cdemu_daemon, MIRAGE_TYPE_OBJECT);

static void cdemu_daemon_init (CdemuDaemon *self)
{
    self->priv = CDEMU_DAEMON_GET_PRIVATE(self);

    self->priv->main_loop = NULL;
    self->priv->list_of_devices = NULL;
    self->priv->ctl_device = NULL;

    /* Set version string */
    self->priv->version = g_strdup(PACKAGE_VERSION);

    /* D-Bus data */
    self->priv->connection = NULL;
    self->priv->owner_id = 0;
}

static void cdemu_daemon_dispose (GObject *gobject)
{
    CdemuDaemon *self = CDEMU_DAEMON(gobject);

    /* Unref main loop */
    g_main_loop_unref(self->priv->main_loop);

    /* Unref all devices */
    for (GList *entry = self->priv->list_of_devices; entry; entry = entry->next) {
        CdemuDevice *dev = entry->data;
        if (dev) {
            g_object_unref(dev);
            entry->data = NULL;
        }
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(cdemu_daemon_parent_class)->dispose(gobject);
}

static void cdemu_daemon_finalize (GObject *gobject)
{
    CdemuDaemon *self = CDEMU_DAEMON(gobject);

    /* Free devices list */
    g_list_free(self->priv->list_of_devices);

    /* Free control device path */
    g_free(self->priv->ctl_device);

    /* Free version string */
    g_free(self->priv->version);

    /* Shutdown libao */
    ao_shutdown();

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(cdemu_daemon_parent_class)->finalize(gobject);
}

static void cdemu_daemon_class_init (CdemuDaemonClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = cdemu_daemon_dispose;
    gobject_class->finalize = cdemu_daemon_finalize;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(CdemuDaemonPrivate));
}
