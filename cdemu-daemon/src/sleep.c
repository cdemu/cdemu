/*
 *  CDEmu daemon: logind Sleep Event Handler
 *  Copyright (C) 2013-2014 Rok Mandeljc
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

#include <glib.h>
#include <gio/gio.h>

#include "cdemu.h"
#include "sleep.h"
#include "freedesktop-login-manager.h"
#include "daemon-private.h"

#define UNUSED(x) (void)(x)

static void _proxy_created(GObject *src, GAsyncResult *res, CdemuDaemon *daemon)
{
	UNUSED(src);

	FreedesktopLoginManager *proxy;
	GError *error = NULL;

	proxy = freedesktop_login_manager_proxy_new_for_bus_finish(res, &error);
	if (proxy == NULL) {
		g_warning(Q_("Failed to create logind proxy: %s"), error->message);
		g_error_free(error);
		return;
	}

	g_object_set_data_full(G_OBJECT(daemon), "logind-proxy", proxy, g_object_unref);

	g_signal_connect_swapped(proxy, "prepare-for-sleep",
	                         G_CALLBACK(cdemu_daemon_prepare_for_sleep), daemon);
}


void setup_sleep_event(CdemuDaemon *daemon)
{
	freedesktop_login_manager_proxy_new_for_bus(
	    G_BUS_TYPE_SYSTEM,
	    G_DBUS_PROXY_FLAGS_NONE,
	    "org.freedesktop.login1",
	    "/org/freedesktop/login1",
	    NULL,
	    (GAsyncReadyCallback) _proxy_created,
	    daemon
	);
}

void cleanup_sleep_event(CdemuDaemon *daemon)
{
	FreedesktopLoginManager *proxy;
	proxy = g_object_steal_data(G_OBJECT(daemon), "logind-proxy");
	if (proxy == NULL) return;
	g_object_unref(proxy);
}
