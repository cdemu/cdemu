/*
 *  CDEmuD: Device object
 *  Copyright (C) 2006-2008 Rok Mandeljc
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

#ifndef __CDEMUD_DEVICE_H__
#define __CDEMUD_DEVICE_H__


G_BEGIN_DECLS

#define CDEMUD_TYPE_DEVICE            (cdemud_device_get_type())
#define CDEMUD_DEVICE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), CDEMUD_TYPE_DEVICE, CDEMUD_Device))
#define CDEMUD_DEVICE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), CDEMUD_TYPE_DEVICE, CDEMUD_DeviceClass))
#define CDEMUD_IS_DEVICE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), CDEMUD_TYPE_DEVICE))
#define CDEMUD_IS_DEVICE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), CDEMUD_TYPE_DEVICE))
#define CDEMUD_DEVICE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), CDEMUD_TYPE_DEVICE, CDEMUD_DeviceClass))

typedef enum {
    CDEMUD_DEVICE_CHANGE_STATUS           = 1,
    CDEMUD_DEVICE_CHANGE_DAEMONDEBUGMASK  = 2,
    CDEMUD_DEVICE_CHANGE_LIBRARYDEBUGMASK = 3,
} CDEMUD_DeviceChangeType;

typedef struct {
    MIRAGE_Object parent;
} CDEMUD_Device;

typedef struct {
    MIRAGE_ObjectClass parent;
    
    /* Class members */
    guint signals[1]; /* Signals */
} CDEMUD_DeviceClass;


typedef struct {
    guint8 cdb[12];
    guint8 *in;
    guint in_len;
    guint8 *out;
    guint out_len;
} CDEMUD_Command;

/* Used by CDEMUD_TYPE_DEVICE */
GType cdemud_device_get_type (void);

/* Public API */
gboolean cdemud_device_initialize (CDEMUD_Device *self, gint number, gchar *audio_backend, gchar *audio_device, GObject *mirage, GError **error);
gboolean cdemud_device_get_device_number (CDEMUD_Device *self, gint *number, GError **error);
gboolean cdemud_device_get_status (CDEMUD_Device *self, gboolean *loaded, gchar **image_type, gchar ***file_names, GError **error);
gboolean cdemud_device_load_disc (CDEMUD_Device *self, gchar **file_names, GError **error);
gboolean cdemud_device_unload_disc (CDEMUD_Device *self, GError **error);
gboolean cdemud_device_get_debug_mask (CDEMUD_Device *self, gchar *type, gint *dbg_mask, GError **error);
gboolean cdemud_device_set_debug_mask (CDEMUD_Device *self, gchar *type, gint dbg_mask, GError **error);
gint cdemud_device_execute(CDEMUD_Device *self, CDEMUD_Command *cmd);

G_END_DECLS

#endif /* __CDEMUD_DEVICE_H__ */
