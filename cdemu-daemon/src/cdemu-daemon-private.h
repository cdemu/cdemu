/*
 *  CDEmu daemon: Daemon object - private
 *  Copyright (C) 2012 Rok Mandeljc
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

#ifndef __CDEMU_DAEMON_PRIVATE_H__
#define __CDEMU_DAEMON_PRIVATE_H__

#define CDEMU_DAEMON_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), CDEMU_TYPE_DAEMON, CdemuDaemonPrivate))

struct _CdemuDaemonPrivate
{
    gchar *version;

    GMainLoop *main_loop;

    /* Options */
    gchar *ctl_device;
    gchar *audio_driver;

    /* Devices */
    GList *devices;

    /* D-Bus */
    GDBusConnection *connection;
    guint owner_id;
};

/* Device management */
gboolean cdemu_daemon_add_device (CdemuDaemon *self);
gboolean cdemu_daemon_remove_device (CdemuDaemon *self);
CdemuDevice *cdemu_daemon_get_device (CdemuDaemon *self, gint device_number, GError **error);

/* Daemon's D-BUS API */
gboolean cdemu_daemon_dbus_check_if_name_is_available (CdemuDaemon *self, GBusType bus_type);
void cdemu_daemon_dbus_register_on_bus (CdemuDaemon *self, GBusType bus_type);
void cdemu_daemon_dbus_cleanup (CdemuDaemon *self);

void cdemu_daemon_dbus_emit_device_status_changed (CdemuDaemon *self, gint number);
void cdemu_daemon_dbus_emit_device_option_changed (CdemuDaemon *self, gint number, const gchar *option);
void cdemu_daemon_dbus_emit_device_mapping_ready (CdemuDaemon *self, gint number);
void cdemu_daemon_dbus_emit_device_added (CdemuDaemon *self);
void cdemu_daemon_dbus_emit_device_removed (CdemuDaemon *self);

#endif /* __CDEMU_DAEMON_PRIVATE_H__ */
