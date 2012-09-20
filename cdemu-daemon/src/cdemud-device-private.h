/*
 *  CDEmuD: Device object - private
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

#ifndef __CDEMUD_DEVICE_PRIVATE_H__
#define __CDEMUD_DEVICE_PRIVATE_H__

#define CDEMUD_DEVICE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), CDEMUD_TYPE_DEVICE, CDEMUD_DevicePrivate))

typedef struct _CDEMUD_Command CDEMUD_Command;

struct _CDEMUD_Command
{
    guint8 cdb[12];
    guint8 *in;
    guint in_len;
    guint8 *out;
    guint out_len;
};

struct _CDEMUD_DevicePrivate
{
    /* Device I/O thread */
    GIOChannel *io_channel;

    GThread *io_thread;
    GMainContext *main_context;
    GMainLoop *main_loop;

    /* Device stuff */
    gint number;
    gchar *device_name;

    /* Device mutex */
    GMutex *device_mutex;

    /* Command */
    CDEMUD_Command *cmd;
    guint cur_len;

    /* Buffer/"cache" */
    guint8 *buffer;
    gint buffer_size;

    /* Audio play */
    GObject *audio_play;

    /* Disc */
    gboolean loaded;
    GObject *disc;
    GObject *disc_debug; /* Debug context for disc */

    /* Locked flag */
    gboolean locked;
    /* Media changed flag */
    gint media_event;

    /* Last accessed sector */
    gint current_sector;

    /* Mode pages */
    GList *mode_pages_list;

    /* Current device profile */
    Profile current_profile;
    /* Features */
    GList *features_list;

    /* Delay emulation */
    GTimeVal delay_begin;
    gint delay_amount;
    gdouble current_angle;

    gboolean dpm_emulation;
    gboolean tr_emulation;

    /* Device ID */
    gchar *id_vendor_id;
    gchar *id_product_id;
    gchar *id_revision;
    gchar *id_vendor_specific;

    /* Device mapping */
    gboolean mapping_complete;
    gchar *device_sr;
    gchar *device_sg;
};


/* Some fields are of 3-byte size... */
#define GUINT24_FROM_BE(x) (GUINT32_FROM_BE(x) >> 8)
#define GUINT24_TO_BE(x)   (GUINT32_TO_BE(x) >> 8)


/* Commands */
gint cdemud_device_execute_command (CDEMUD_Device *self, CDEMUD_Command *cmd);

/* Delay emulation */
void cdemud_device_delay_begin (CDEMUD_Device *self, gint address, gint num_sectors);
void cdemud_device_delay_finalize (CDEMUD_Device *self);

/* Features */
gpointer cdemud_device_get_feature (CDEMUD_Device *self, gint feature);
void cdemud_device_features_init (CDEMUD_Device *self);
void cdemud_device_features_cleanup (CDEMUD_Device *self);
void cdemud_device_set_profile (CDEMUD_Device *self, Profile profile);

/* Kernel <-> userspace I/O */
void cdemud_device_write_buffer (CDEMUD_Device *self, guint32 length);
void cdemud_device_read_buffer (CDEMUD_Device *self, guint32 length);
void cdemud_device_flush_buffer (CDEMUD_Device *self);

void cdemud_device_write_sense_full (CDEMUD_Device *self, SenseKey sense_key, guint16 asc_ascq, gint ili, guint32 command_info);
void cdemud_device_write_sense (CDEMUD_Device *self, SenseKey sense_key, guint16 asc_ascq);

GThread *cdemud_device_create_io_thread (CDEMUD_Device *self);
void cdemud_device_stop_io_thread (CDEMUD_Device *self);

/* Load/unload */
gboolean cdemud_device_unload_disc_private (CDEMUD_Device *self, gboolean force, GError **error);

/* Mode pages */
gpointer cdemud_device_get_mode_page (CDEMUD_Device *self, gint page, gint type);
void cdemud_device_mode_pages_init (CDEMUD_Device *self);
void cdemud_device_mode_pages_cleanup (CDEMUD_Device *self);



#endif /* __CDEMUD_DEVICE_PRIVATE_H__ */
