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
    
    /* Class members */
    guint signals[4]; /* Signals */
} CDEMUD_DaemonClass;


/* Used by CDEMUD_TYPE_DAEMON */
GType cdemud_daemon_get_type (void);

/* Public API */
gboolean cdemud_daemon_initialize (CDEMUD_Daemon *self, gint num_devices, gchar *ctl_device, gchar *audio_driver, gboolean system_bus, GError **error);
gboolean cdemud_daemon_start_daemon (CDEMUD_Daemon *self, GError **error);
gboolean cdemud_daemon_stop_daemon (CDEMUD_Daemon *self, GError **error);

/* DBUS interface functions */
gboolean cdemud_daemon_hail (CDEMUD_Daemon *self, GError **error);

gboolean cdemud_daemon_get_daemon_version (CDEMUD_Daemon *self, gchar **version, GError **error);
gboolean cdemud_daemon_get_library_version (CDEMUD_Daemon *self, gchar **version, GError **error);
gboolean cdemud_daemon_get_daemon_interface_version (CDEMUD_Daemon *self, gint *version, GError **error);

gboolean cdemud_daemon_enum_daemon_debug_masks (CDEMUD_Daemon *self, GPtrArray **masks, GError **error);
gboolean cdemud_daemon_enum_library_debug_masks (CDEMUD_Daemon *self, GPtrArray **masks, GError **error);

gboolean cdemud_daemon_enum_supported_parsers (CDEMUD_Daemon *self, GPtrArray **parsers, GError **error);
gboolean cdemud_daemon_enum_supported_fragments (CDEMUD_Daemon *self, GPtrArray **fragments, GError **error);

gboolean cdemud_daemon_get_number_of_devices (CDEMUD_Daemon *self, gint *number_of_devices, GError **error);
gboolean cdemud_daemon_device_get_status (CDEMUD_Daemon *self, gint device_number, gboolean *loaded, gchar ***file_names, GError **error);
gboolean cdemud_daemon_device_get_mapping (CDEMUD_Daemon *self, gint device_number, gchar **sr_device, gchar **sg_device, GError **error);
gboolean cdemud_daemon_device_load (CDEMUD_Daemon *self, gint device_number, gchar **file_names, GHashTable *parameters, GError **error);
gboolean cdemud_daemon_device_unload (CDEMUD_Daemon *self, gint device_number, GError **error);

gboolean cdemud_daemon_device_get_option (CDEMUD_Daemon *self, gint device_number, gchar *option_name, GPtrArray **option_values, GError **error);
gboolean cdemud_daemon_device_set_option (CDEMUD_Daemon *self, gint device_number, gchar *option_name, GPtrArray *option_values, GError **error);

G_END_DECLS

#endif /* __CDEMUD_DAEMON_H__ */
