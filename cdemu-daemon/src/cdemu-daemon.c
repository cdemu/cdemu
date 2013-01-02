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

static void device_mapping_ready_handler (CdemuDevice *device, CdemuDaemon *self)
{
    gint number = cdemu_device_get_device_number(device);
    cdemu_daemon_dbus_emit_device_mapping_ready(self, number);
}


/**********************************************************************\
 *                     Device restart on inactivity                   *
\**********************************************************************/
struct DaemonDevicePtr {
    CdemuDaemon *daemon;
    CdemuDevice *device;
};


/* Stage 2 of device restart: start */
static gboolean device_restart_stage2 (struct DaemonDevicePtr *data)
{
    CdemuDaemon *self = data->daemon;
    CdemuDevice *device = data->device;

    /* Start */
    if (!cdemu_device_start(device, self->priv->ctl_device)) {
        CDEMU_DEBUG(device, DAEMON_DEBUG_WARNING, "%s: failed to restart device!\n", __debug__);
    } else {
        CDEMU_DEBUG(device, DAEMON_DEBUG_DEVICE, "%s: device started successfully\n", __debug__);
    }

    /* Free the pointer structure */
    g_free(data);

    return FALSE;
}

/* Stage 1 of device restart: stop */
static gboolean device_restart_stage1 (struct DaemonDevicePtr *data)
{
    CdemuDevice *device = data->device;
    gint interval = 5; /* 5 seconds */

    /* Properly stop the device (I/O thread clenup) */
    cdemu_device_stop(device);

    CDEMU_DEBUG(device, DAEMON_DEBUG_DEVICE, "%s: device stopped; starting in %d seconds...\n", __debug__, interval);

    /* Schedule device to be started after some time has passed */
    g_timeout_add_seconds(interval, (GSourceFunc)device_restart_stage2, data);

    return FALSE;
}

/* The actual signal handler; since the signal is emitted from the device's
   I/O thread, this handler is also executed there... so we need to get
   into our main thread first, which is done by scheduling an idle function */
static void device_inactive_handler (CdemuDevice *device, CdemuDaemon *self)
{
    struct DaemonDevicePtr *data = g_new(struct DaemonDevicePtr, 1);
    data->daemon = self;
    data->device = device;

    CDEMU_DEBUG(device, DAEMON_DEBUG_DEVICE, "%s: restarting device...\n", __debug__);
    g_idle_add((GSourceFunc)device_restart_stage1, data);
}


/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
gboolean cdemu_daemon_initialize_and_start (CdemuDaemon *self, gint num_devices, gchar *ctl_device, gchar *audio_driver, gboolean system_bus)
{
    MirageContext *context;
    GBusType bus_type = system_bus ? G_BUS_TYPE_SYSTEM : G_BUS_TYPE_SESSION;

    /* Create a MirageContext and use it as debug context */
    context = g_object_new(MIRAGE_TYPE_CONTEXT, NULL);
    mirage_context_set_debug_name(context, "cdemu");
    mirage_context_set_debug_domain(context, "CDEMU");
    mirage_contextual_set_context(MIRAGE_CONTEXTUAL(self), context);
    g_object_unref(context);

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

        /* Initialize device */
        if (!cdemu_device_initialize(dev, i, audio_driver)) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to initialize device %i!\n", __debug__, i);
            g_object_unref(dev);
            return FALSE;
        }

        /* Don't set parent, as devices have their own debug contexts */
        /* Add handling for signals from the device... this allows us to
           pass them on via DBUS */
        g_signal_connect(dev, "status-changed", (GCallback)device_status_changed_handler, self);
        g_signal_connect(dev, "option-changed", (GCallback)device_option_changed_handler, self);
        g_signal_connect(dev, "device-inactive", (GCallback)device_inactive_handler, self);
        g_signal_connect(dev, "mapping-ready", (GCallback)device_mapping_ready_handler, self);

        /* Start device */
        if (!cdemu_device_start(dev, self->priv->ctl_device)) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to start device %i!\n", __debug__, i);
            g_object_unref(dev);
            return FALSE;
        }

        /* Add it to devices list */
        self->priv->list_of_devices = g_list_append(self->priv->list_of_devices, dev);
    }

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
    self->priv->version = g_strdup(CDEMU_DAEMON_VERSION);

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
