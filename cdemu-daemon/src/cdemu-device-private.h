/*
 *  CDEmu daemon: Device object - private
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

#ifndef __CDEMU_DEVICE_PRIVATE_H__
#define __CDEMU_DEVICE_PRIVATE_H__

#define CDEMU_DEVICE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), CDEMU_TYPE_DEVICE, CdemuDevicePrivate))

struct _CdemuCommand
{
    guint8 cdb[12];
    guint8 *in;
    guint in_len;
    guint8 *out;
    guint out_len;
};

struct _CdemuDevicePrivate
{
    /* Device I/O thread */
    GIOChannel *io_channel;

    GThread *io_thread;
    GMainContext *main_context;
    GMainLoop *main_loop;
    GSource *io_watch;

    /* Device stuff */
    gint number;
    gchar *device_name;

    /* Device mutex */
    GMutex *device_mutex;

    /* Command */
    CdemuCommand *cmd;
    guint cur_len;

    /* Kernel I/O buffer */
    guint8 *kernel_io_buffer;

    /* Buffer/"cache" */
    guint8 *buffer;
    guint buffer_size;

    /* Audio play */
    CdemuAudio *audio_play;

    /* Disc */
    gboolean loaded;
    MirageDisc *disc;
    MirageContext *mirage_context; /* libMirage context */

    /* Locked flag */
    gboolean locked;
    /* Media changed flag */
    gint media_event;

    /* Last accessed sector */
    gint current_address;

    /* Mode pages */
    GList *mode_pages_list;

    /* Current device profile */
    ProfileCode current_profile;
    /* Features */
    GList *features_list;

    /* Delay emulation */
    GTimeVal delay_begin;
    gint delay_amount;
    gdouble current_angle;

    gboolean dpm_emulation;
    gboolean tr_emulation;
    gboolean bad_sector_emulation;

    /* Device ID */
    gchar *id_vendor_id;
    gchar *id_product_id;
    gchar *id_revision;
    gchar *id_vendor_specific;

    /* Device mapping */
    gboolean mapping_complete;
    gchar *device_sr;
    gchar *device_sg;
    
    /* Burning emulation */
    gint medium_capacity;
    gboolean recordable_disc;
    gboolean rewritable_disc;
    GList *write_descriptors;
};


/* Some fields are of 3-byte size... */
#define GUINT24_FROM_BE(x) (GUINT32_FROM_BE(x) >> 8)
#define GUINT24_TO_BE(x)   (GUINT32_TO_BE(x) >> 8)


/* Commands */
gint cdemu_device_execute_command (CdemuDevice *self, CdemuCommand *cmd);

/* Delay emulation */
void cdemu_device_delay_begin (CdemuDevice *self, gint address, gint num_sectors);
void cdemu_device_delay_finalize (CdemuDevice *self);

/* Features */
gpointer cdemu_device_get_feature (CdemuDevice *self, gint feature);
void cdemu_device_features_init (CdemuDevice *self);
void cdemu_device_features_cleanup (CdemuDevice *self);
void cdemu_device_set_profile (CdemuDevice *self, ProfileIndex profile_index);

/* Kernel <-> userspace I/O */
gsize cdemu_device_get_kernel_io_buffer_size (CdemuDevice *self);

void cdemu_device_write_buffer (CdemuDevice *self, guint32 length);
void cdemu_device_read_buffer (CdemuDevice *self, guint32 length);
void cdemu_device_flush_buffer (CdemuDevice *self);

void cdemu_device_write_sense_full (CdemuDevice *self, SenseKey sense_key, guint16 asc_ascq, gint ili, guint32 command_info);
void cdemu_device_write_sense (CdemuDevice *self, SenseKey sense_key, guint16 asc_ascq);

/* Load/unload */
gboolean cdemu_device_unload_disc_private (CdemuDevice *self, gboolean force, GError **error);

/* Mode pages */
gpointer cdemu_device_get_mode_page (CdemuDevice *self, gint page, gint type);
void cdemu_device_mode_pages_init (CdemuDevice *self);
void cdemu_device_mode_pages_cleanup (CdemuDevice *self);



#endif /* __CDEMU_DEVICE_PRIVATE_H__ */
