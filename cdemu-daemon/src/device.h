/*
 *  CDEmu daemon: device
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

#ifndef __CDEMU_DEVICE_H__
#define __CDEMU_DEVICE_H__


G_BEGIN_DECLS

/**********************************************************************\
 *                        CdemuDevice object                          *
\**********************************************************************/
#define CDEMU_TYPE_DEVICE            (cdemu_device_get_type())
#define CDEMU_DEVICE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), CDEMU_TYPE_DEVICE, CdemuDevice))
#define CDEMU_DEVICE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), CDEMU_TYPE_DEVICE, CdemuDeviceClass))
#define CDEMU_IS_DEVICE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), CDEMU_TYPE_DEVICE))
#define CDEMU_IS_DEVICE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), CDEMU_TYPE_DEVICE))
#define CDEMU_DEVICE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), CDEMU_TYPE_DEVICE, CdemuDeviceClass))

typedef struct _CdemuDeviceClass      CdemuDeviceClass;
typedef struct _CdemuDevicePrivate    CdemuDevicePrivate;

struct _CdemuDevice
{
    MirageObject parent_instance;

    /*< private >*/
    CdemuDevicePrivate *priv;
};

struct _CdemuDeviceClass
{
    MirageObjectClass parent_class;
};


/* Used by CDEMU_TYPE_DEVICE */
GType cdemu_device_get_type (void);

/* Public API */
gboolean cdemu_device_initialize (CdemuDevice *self, gint number, const gchar *audio_driver);

gint cdemu_device_get_device_number (CdemuDevice *self);

gboolean cdemu_device_get_status (CdemuDevice *self, gchar ***filenames);

gboolean cdemu_device_load_disc (CdemuDevice *self, gchar **filenames, GVariant *options, GError **error);
gboolean cdemu_device_create_blank_disc (CdemuDevice *self, const gchar *filename, GVariant *options, GError **error);
gboolean cdemu_device_unload_disc (CdemuDevice *self, GError **error);

GVariant *cdemu_device_get_option (CdemuDevice *self, gchar *option_name, GError **error);
gboolean cdemu_device_set_option (CdemuDevice *self, gchar *option_name, GVariant *option_value, GError **error);

gboolean cdemu_device_setup_mapping (CdemuDevice *self);
void cdemu_device_get_mapping (CdemuDevice *self, gchar **sr_device, gchar **sg_device);

gboolean cdemu_device_start (CdemuDevice *self, const gchar *ctl_device);
void cdemu_device_stop (CdemuDevice *self);

G_END_DECLS

#endif /* __CDEMU_DEVICE_H__ */
