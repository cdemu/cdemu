/*
 *  CDEmu daemon: daemon
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
#include "daemon-private.h"

#define __debug__ "Daemon"


/* Object definition */
G_DEFINE_TYPE_WITH_PRIVATE(CdemuDaemon, cdemu_daemon, MIRAGE_TYPE_OBJECT)


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
    g_idle_add(G_SOURCE_FUNC(device_restart_callback), data);
}


/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
gboolean cdemu_daemon_initialize_and_start (CdemuDaemon *self, const CdemuDaemonSettings *settings)
{
    MirageContext *context;

    self->priv->ctl_device = g_strdup(settings->ctl_device);
    self->priv->audio_driver = g_strdup(settings->audio_driver);

    self->priv->cdemu_debug_mask = settings->cdemu_debug_mask;
    self->priv->mirage_debug_mask = settings->mirage_debug_mask;

    self->priv->bus_type = settings->bus_type;

    /* Create a MirageContext and use it as debug context */
    context = g_object_new(MIRAGE_TYPE_CONTEXT, NULL);
    mirage_context_set_debug_name(context, "cdemu");
    mirage_context_set_debug_domain(context, "CDEMU");
    mirage_context_set_debug_mask(context, self->priv->cdemu_debug_mask);
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
    if (!cdemu_daemon_dbus_check_if_name_is_available(self, self->priv->bus_type)) {
        return FALSE;
    }

    /* Try connecting to org.freedesktop.login1.Manager interface on /org/freedesktop/login1
       so we can stop/start devices when system enters/exits suspend/hibernation. */
    if (settings->use_system_sleep_handler) {
#if ENABLE_LOGIND_SLEEP_HANDLER
        GError *error = NULL;

        CDEMU_DEBUG(self, DAEMON_DEBUG_SLEEP_HANDLER, "%s: connecting to org.freedesktop.login1.Manager interface on /org/freedesktop/login1...\n", __debug__);
        self->priv->login_manager_proxy = freedesktop_login_manager_proxy_new_for_bus_sync(
            G_BUS_TYPE_SYSTEM, /* always on system bus! */
            G_DBUS_PROXY_FLAGS_NONE,
            "org.freedesktop.login1",
            "/org/freedesktop/login1",
            NULL,
            &error
        );
        if (self->priv->login_manager_proxy) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_SLEEP_HANDLER, "%s: successfully connected to org.freedesktop.login1.Manager!\n", __debug__);

            /* Try obtaining the system sleep inhibitor lock */
            cdemu_daemon_obtain_system_sleep_inhibitor_lock(self);

            /* Connect handler for "prepare-for-sleep" signal */
            g_signal_connect_swapped(self->priv->login_manager_proxy, "prepare-for-sleep", G_CALLBACK(cdemu_daemon_prepare_for_system_sleep), self);
        } else {
            /* Non-fatal error - just emit a warning */
            CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to connect to org.freedesktop.login1.Manager: %s!\n", __debug__, error->message);
            g_error_free(error);
        }
#else
        CDEMU_DEBUG(self, DAEMON_DEBUG_SLEEP_HANDLER, "%s: system sleep handler was disabled at build time.\n", __debug__);
#endif
    } else {
        CDEMU_DEBUG(self, DAEMON_DEBUG_SLEEP_HANDLER, "%s: system sleep handler is disabled via settings.\n", __debug__);
    }

    /* Create desired number of devices */
    for (gint i = 0; i < settings->num_devices; i++) {
        if (!cdemu_daemon_add_device(self)) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to create device!\n", __debug__);
            return FALSE;
        }
    }

    /* Register on D-Bus bus */
    cdemu_daemon_dbus_register_on_bus(self, self->priv->bus_type);

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
 *               System sleep signal handler (optional)               *
\**********************************************************************/
#if ENABLE_LOGIND_SLEEP_HANDLER

void cdemu_daemon_prepare_for_system_sleep (CdemuDaemon *self, gboolean start)
{
    GList *iter;

    CDEMU_DEBUG(self, DAEMON_DEBUG_SLEEP_HANDLER, "%s: received prepare-for-sleep signal, start=%d\n", __debug__, start);

    if (start) {
        /* System is entering sleep/hibernation. */

        /* Stop devices. */
        CDEMU_DEBUG(self, DAEMON_DEBUG_SLEEP_HANDLER, "%s: stopping devices...\n", __debug__);
        for (iter = self->priv->devices; iter != NULL; iter = g_list_next(iter)) {
            cdemu_device_stop(iter->data);
        }

        /* Release the sleep inhibitor lock, if held. */
        if (self->priv->system_sleep_inhibitor_fds) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_SLEEP_HANDLER, "%s: releasing sleep inhibitor lock...\n", __debug__);
            g_object_unref(self->priv->system_sleep_inhibitor_fds);
            self->priv->system_sleep_inhibitor_fds = NULL;
        } else {
            CDEMU_DEBUG(self, DAEMON_DEBUG_SLEEP_HANDLER, "%s: no sleep inhibitor lock to release\n", __debug__);
        }
    } else {
        /* System has awoken from sleep/hibernation. */

        /* Try re-taking the sleep inhibitor lock. */
        CDEMU_DEBUG(self, DAEMON_DEBUG_SLEEP_HANDLER, "%s: re-taking sleep inhibitor lock...\n", __debug__);
        cdemu_daemon_obtain_system_sleep_inhibitor_lock(self);

        /* Start devices. */
        CDEMU_DEBUG(self, DAEMON_DEBUG_SLEEP_HANDLER, "%s: re-starting devices...\n", __debug__);
        for (iter = self->priv->devices; iter != NULL; iter = g_list_next(iter)) {
            if (!cdemu_device_start(iter->data, self->priv->ctl_device)) {
                CDEMU_DEBUG(iter->data, DAEMON_DEBUG_WARNING, "%s: failed to start device after wake up!\n", __debug__);
            }
        }
    }
}

void cdemu_daemon_obtain_system_sleep_inhibitor_lock (CdemuDaemon *self)
{
    gboolean succeeded;
    GError *error = NULL;

    if (self->priv->system_sleep_inhibitor_fds) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: trying to obtain system sleep inhibitor lock when one is already held!\n", __debug__);
        return;
    }

    CDEMU_DEBUG(self, DAEMON_DEBUG_SLEEP_HANDLER, "%s: trying to obtain system sleep inhibitor lock...\n", __debug__);
    succeeded = freedesktop_login_manager_call_inhibit_sync(
        self->priv->login_manager_proxy,
        "sleep",
        Q_("CDEmu Daemon"),
        Q_("CDEmu Daemon needs to deactivate virtual optical drives"),
        "delay",
        0,
        -1, // timeout
        NULL,
        NULL,
        &self->priv->system_sleep_inhibitor_fds,
        NULL,
        &error
    );

    if (succeeded) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_SLEEP_HANDLER, "%s: successfully obtained system sleep inhibitor lock.\n", __debug__);
    } else {
        CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to obtain system sleep inhibitor lock: %s!\n", __debug__, error->message);
        g_error_free(error);
    }
}

#endif


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
    if (!cdemu_device_initialize(device, device_number, self->priv->audio_driver, self->priv->cdemu_debug_mask, self->priv->mirage_debug_mask)) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to initialize device #%i\n", __debug__, device_number);
        g_object_unref(device);
        return FALSE;
    }

    /* Don't set parent, as devices have their own debug contexts */

    /* Add handling for signals from the device... this allows us to
       pass them on via DBUS */
    g_signal_connect(device, "status-changed", G_CALLBACK(device_status_changed_handler), self);
    g_signal_connect(device, "option-changed", G_CALLBACK(device_option_changed_handler), self);
    g_signal_connect(device, "kernel-io-error", G_CALLBACK(device_kernel_io_error_handler), self);
    g_signal_connect(device, "mapping-ready", G_CALLBACK(device_mapping_ready_handler), self);

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
        g_set_error(error, CDEMU_ERROR, CDEMU_ERROR_INVALID_ARGUMENT, Q_("Invalid device number!"));
        return NULL;
    }
    return g_object_ref(device);
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
static void cdemu_daemon_init (CdemuDaemon *self)
{
    self->priv = cdemu_daemon_get_instance_private(self);

    self->priv->main_loop = NULL;
    self->priv->devices = NULL;
    self->priv->ctl_device = NULL;
    self->priv->audio_driver = NULL;

    self->priv->cdemu_debug_mask = 0;
    self->priv->mirage_debug_mask = 0;

    /* Set version string */
    self->priv->version = g_strdup(CDEMU_DAEMON_VERSION);

    /* D-Bus data */
    self->priv->connection = NULL;
    self->priv->owner_id = 0;

#if ENABLE_LOGIND_SLEEP_HANDLER
    /* org.freedesktop.login1.Manager proxy */
    self->priv->login_manager_proxy = NULL;
    self->priv->system_sleep_inhibitor_fds = NULL;
#endif
}

static void cdemu_daemon_dispose (GObject *gobject)
{
    CdemuDaemon *self = CDEMU_DAEMON(gobject);

#if ENABLE_LOGIND_SLEEP_HANDLER
    /* Release sleep inhibitor lock (if held) */
    if (self->priv->system_sleep_inhibitor_fds) {
        g_object_unref(self->priv->system_sleep_inhibitor_fds);
        self->priv->system_sleep_inhibitor_fds = NULL;
    }

    /* Unref org.freedesktop.login1.Manager proxy */
    if (self->priv->login_manager_proxy) {
        g_object_unref(self->priv->login_manager_proxy);
        self->priv->login_manager_proxy = NULL;
    }
#endif

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
    G_OBJECT_CLASS(cdemu_daemon_parent_class)->dispose(gobject);
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
    G_OBJECT_CLASS(cdemu_daemon_parent_class)->finalize(gobject);
}

static void cdemu_daemon_class_init (CdemuDaemonClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = cdemu_daemon_dispose;
    gobject_class->finalize = cdemu_daemon_finalize;
}
