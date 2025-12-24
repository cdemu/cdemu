/*
 *  CDEmu daemon: daemon - private
 *  Copyright (C) 2012-2014 Rok Mandeljc
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

#pragma once

#if ENABLE_LOGIND_SLEEP_HANDLER
#include "freedesktop-login-manager.h"
#endif

struct _CdemuDaemonPrivate
{
    gchar *version;

    GMainLoop *main_loop;

    /* Options */
    gchar *ctl_device;
    gchar *audio_driver;

    guint cdemu_debug_mask; /* Default debug mask for CDEmu devices */
    guint mirage_debug_mask; /* Default debug mask for underlying libMirage context */

    /* Devices */
    GList *devices;

    /* D-Bus */
    GBusType bus_type;
    GDBusConnection *connection;
    guint owner_id;

#if ENABLE_LOGIND_SLEEP_HANDLER
    /* org.freedesktop.login1.Manager for notification of system sleep/hibernation */
    FreedesktopLoginManager *login_manager_proxy;
    GUnixFDList *system_sleep_inhibitor_fds; /* Inhibitor lock file descriptor(s). */
#endif
};

/* Device management */
gboolean cdemu_daemon_add_device (CdemuDaemon *self);
gboolean cdemu_daemon_remove_device (CdemuDaemon *self);
CdemuDevice *cdemu_daemon_get_device (CdemuDaemon *self, gint device_number, GError **error);

#if ENABLE_LOGIND_SLEEP_HANDLER
void cdemu_daemon_prepare_for_system_sleep (CdemuDaemon *self, gboolean start);
void cdemu_daemon_obtain_system_sleep_inhibitor_lock (CdemuDaemon *self);
#endif

/* Daemon's D-BUS API */
gboolean cdemu_daemon_dbus_check_if_name_is_available (CdemuDaemon *self, GBusType bus_type);
void cdemu_daemon_dbus_register_on_bus (CdemuDaemon *self, GBusType bus_type);
void cdemu_daemon_dbus_cleanup (CdemuDaemon *self);

void cdemu_daemon_dbus_emit_device_status_changed (CdemuDaemon *self, gint number);
void cdemu_daemon_dbus_emit_device_option_changed (CdemuDaemon *self, gint number, const gchar *option);
void cdemu_daemon_dbus_emit_device_mapping_ready (CdemuDaemon *self, gint number);
void cdemu_daemon_dbus_emit_device_added (CdemuDaemon *self);
void cdemu_daemon_dbus_emit_device_removed (CdemuDaemon *self);
