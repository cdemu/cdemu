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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __CDEMU_DAEMON_PRIVATE_H__
#define __CDEMU_DAEMON_PRIVATE_H__

#define CDEMU_DAEMON_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), CDEMU_TYPE_DAEMON, CdemuDaemonPrivate))

struct _CdemuDaemonPrivate
{
    gchar *version;

    GMainLoop *main_loop;

    /* Control device */
    gchar *ctl_device;

    /* Devices */
    gint number_of_devices;
    GList *list_of_devices;

    /* D-Bus */
    GDBusConnection *connection;
    guint owner_id;
};

/* Daemon's D-BUS API */
gboolean cdemu_daemon_dbus_check_if_name_is_available (CdemuDaemon *self, GBusType bus_type);
void cdemu_daemon_dbus_register_on_bus (CdemuDaemon *self, GBusType bus_type);
void cdemu_daemon_dbus_cleanup (CdemuDaemon *self);

void cdemu_daemon_dbus_emit_device_status_changed (CdemuDaemon *self, gint number);
void cdemu_daemon_dbus_emit_device_option_changed (CdemuDaemon *self, gint number, const gchar *option);
void cdemu_daemon_dbus_emit_device_mapping_ready (CdemuDaemon *self, gint number);


#endif /* __CDEMU_DAEMON_PRIVATE_H__ */
