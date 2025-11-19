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

#ifndef __CDEMU_DAEMON_H__
#define __CDEMU_DAEMON_H__

G_BEGIN_DECLS

/* Daemon settings */
typedef struct _CdemuDaemonSettings CdemuDaemonSettings;
struct _CdemuDaemonSettings
{
    GBusType bus_type;

    gchar *ctl_device;
    gchar *audio_driver;

    gint num_devices;

    guint cdemu_debug_mask;
    guint mirage_debug_mask;

    gboolean use_system_sleep_handler;
};

/**********************************************************************\
 *                        CdemuDaemon object                          *
\**********************************************************************/
#define CDEMU_TYPE_DAEMON            (cdemu_daemon_get_type())
#define CDEMU_DAEMON(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), CDEMU_TYPE_DAEMON, CdemuDaemon))
#define CDEMU_DAEMON_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), CDEMU_TYPE_DAEMON, CdemuDaemonClass))
#define CDEMU_IS_DAEMON(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), CDEMU_TYPE_DAEMON))
#define CDEMU_IS_DAEMON_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), CDEMU_TYPE_DAEMON))
#define CDEMU_DAEMON_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), CDEMU_TYPE_DAEMON, CdemuDaemonClass))

typedef struct _CdemuDaemon           CdemuDaemon;
typedef struct _CdemuDaemonClass      CdemuDaemonClass;
typedef struct _CdemuDaemonPrivate    CdemuDaemonPrivate;

struct _CdemuDaemon
{
    MirageObject parent_instance;

    /*< private >*/
    CdemuDaemonPrivate *priv;
};

struct _CdemuDaemonClass
{
    MirageObjectClass parent_class;
};


/* Used by CDEMU_TYPE_DAEMON */
GType cdemu_daemon_get_type (void);

/* Public API */
gboolean cdemu_daemon_initialize_and_start (CdemuDaemon *self, const CdemuDaemonSettings *settings);
void cdemu_daemon_stop_daemon (CdemuDaemon *self);
CdemuDevice *cdemu_daemon_get_device (CdemuDaemon *self, gint device_number, GError **error);


G_END_DECLS

#endif /* __CDEMU_DAEMON_H__ */
