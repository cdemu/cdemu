/*
 *  CDEmuD: Daemon object
 *  Copyright (C) 2006-2010 Rok Mandeljc
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

#ifndef __CDEMUD_DAEMON_H__
#define __CDEMUD_DAEMON_H__


G_BEGIN_DECLS

#define CDEMUD_TYPE_DAEMON            (cdemud_daemon_get_type())
#define CDEMUD_DAEMON(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), CDEMUD_TYPE_DAEMON, CDEMUD_Daemon))
#define CDEMUD_DAEMON_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), CDEMUD_TYPE_DAEMON, CDEMUD_DaemonClass))
#define CDEMUD_IS_DAEMON(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), CDEMUD_TYPE_DAEMON))
#define CDEMUD_IS_DAEMON_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), CDEMUD_TYPE_DAEMON))
#define CDEMUD_DAEMON_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), CDEMUD_TYPE_DAEMON, CDEMUD_DaemonClass))


typedef struct {
    MIRAGE_Object parent;
} CDEMUD_Daemon;

typedef struct {
    MIRAGE_ObjectClass parent;
} CDEMUD_DaemonClass;


/* Used by CDEMUD_TYPE_DAEMON */
GType cdemud_daemon_get_type (void);

/* Public API */
gboolean cdemud_daemon_initialize_and_start (CDEMUD_Daemon *self, gint num_devices, gchar *ctl_device, gchar *audio_driver, gboolean system_bus, GError **error);
gboolean cdemud_daemon_stop_daemon (CDEMUD_Daemon *self, GError **error);


G_END_DECLS

#endif /* __CDEMUD_DAEMON_H__ */
