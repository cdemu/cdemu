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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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

static gboolean device_restart_callback (struct DaemonDevicePtr *data)
{
    CdemuDaemon *self = data->daemon;
    CdemuDevice *device = data->device;

    /* Properly stop the device (I/O thread clenup) */
    cdemu_device_stop(device);

    /* Start the device */
    if (!cdemu_device_start(device, self->priv->ctl_device)) {
        CDEMU_DEBUG(device, DAEMON_DEBUG_WARNING, "%s: failed to restart device!\n", __debug__);
    } else {
        CDEMU_DEBUG(device, DAEMON_DEBUG_DEVICE, "%s: device started successfully\n", __debug__);
    }

    /* Free the pointer structure */
    g_free(data);

    return FALSE;
}

/* The signal handler; since the signal is emitted from the device's
   I/O thread, this handler is also executed there... so we need to get
   into our main thread first, which is done by scheduling an idle function */
static void device_kernel_io_error_handler (CdemuDevice *device, CdemuDaemon *self)
{
    struct DaemonDevicePtr *data = g_new(struct DaemonDevicePtr, 1);
    data->daemon = self;
    data->device = device;

    CDEMU_DEBUG(device, DAEMON_DEBUG_DEVICE, "%s: restarting device...\n", __debug__);
    g_idle_add((GSourceFunc)device_restart_callback, data);
}


/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
gboolean cdemu_daemon_initialize_and_start (CdemuDaemon *self, gint num_devices, gchar *ctl_device, gchar *audio_driver, gboolean system_bus)
{
    MirageContext *context;
    GBusType bus_type = system_bus ? G_BUS_TYPE_SYSTEM : G_BUS_TYPE_SESSION;

    self->priv->ctl_device = g_strdup(ctl_device);
    self->priv->audio_driver = g_strdup(audio_driver);

    /* Create a MirageContext and use it as debug context */
    context = g_object_new(MIRAGE_TYPE_CONTEXT, NULL);
    mirage_context_set_debug_name(context, "cdemu");
    mirage_context_set_debug_domain(context, "CDEMU");
    mirage_contextual_set_context(MIRAGE_CONTEXTUAL(self), context);
    g_object_unref(context);

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
    for (gint i = 0; i < num_devices; i++) {
        if (!cdemu_daemon_add_device(self)) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to create device!\n", __debug__);
            return FALSE;
        }
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


/**********************************************************************\
 *                          Device management                         *
\**********************************************************************/
gboolean cdemu_daemon_add_device (CdemuDaemon *self)
{
    CdemuDevice *device;
    gint device_number;

    device_number = g_list_length(self->priv->devices);

    /* Create and initialize device object */
    device = g_object_new(CDEMU_TYPE_DEVICE, NULL);
    if (!cdemu_device_initialize(device, device_number, self->priv->audio_driver)) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to initialize device #%i\n", __debug__, device_number);
        g_object_unref(device);
        return FALSE;
    }

    /* Don't set parent, as devices have their own debug contexts */

    /* Add handling for signals from the device... this allows us to
       pass them on via DBUS */
    g_signal_connect(device, "status-changed", (GCallback)device_status_changed_handler, self);
    g_signal_connect(device, "option-changed", (GCallback)device_option_changed_handler, self);
    g_signal_connect(device, "kernel-io-error", (GCallback)device_kernel_io_error_handler, self);
    g_signal_connect(device, "mapping-ready", (GCallback)device_mapping_ready_handler, self);

    /* Start device */
    if (!cdemu_device_start(device, self->priv->ctl_device)) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to start device #%i!\n", __debug__, device_number);
        g_object_unref(device);
        return FALSE;
    }

    /* Add it to devices list */
    self->priv->devices = g_list_append(self->priv->devices, device);

    /* Emit signal */
    cdemu_daemon_dbus_emit_device_added(self);

    return TRUE;
}

gboolean cdemu_daemon_remove_device (CdemuDaemon *self)
{
    /* Get last device entry */
    GList *entry = g_list_last(self->priv->devices);
    if (!entry) {
        return FALSE;
    }

    /* Release the device, which is enough to stop and free it */
    g_object_unref(entry->data);

    /* Remove and free node from the list */
    self->priv->devices = g_list_delete_link(self->priv->devices, entry);

    /* Emit signal */
    cdemu_daemon_dbus_emit_device_removed(self);

    return TRUE;
}

CdemuDevice *cdemu_daemon_get_device (CdemuDaemon *self, gint device_number, GError **error)
{
    CdemuDevice *device = g_list_nth_data(self->priv->devices, device_number);
    if (!device) {
        g_set_error(error, CDEMU_ERROR, CDEMU_ERROR_INVALID_ARGUMENT, "Invalid device number!");
        return NULL;
    }
    return g_object_ref(device);
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE(CdemuDaemon, cdemu_daemon, MIRAGE_TYPE_OBJECT);

static void cdemu_daemon_init (CdemuDaemon *self)
{
    self->priv = CDEMU_DAEMON_GET_PRIVATE(self);

    self->priv->main_loop = NULL;
    self->priv->devices = NULL;
    self->priv->ctl_device = NULL;
    self->priv->audio_driver = NULL;

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
    for (GList *entry = self->priv->devices; entry; entry = entry->next) {
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

    g_free(self->priv->ctl_device);
    g_free(self->priv->audio_driver);

    /* Free devices list */
    g_list_free(self->priv->devices);

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
