/*
 *  CDEmu daemon: device - userspace <-> kernel bridge
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

#include "cdemu.h"
#include "device-private.h"

#define __debug__ "Kernel I/O"


#define TO_SECTOR(len) ((len + 511) / 512)
#define MAX_SENSE 256
#define MAX_SECTORS 256
#define OTHER_SECTORS TO_SECTOR(MAX_SENSE + sizeof(struct vhba_response))
#define BUF_SIZE (512 * (MAX_SECTORS + OTHER_SECTORS))

/* Kernel I/O structures, also defined in VHBA module's source */
#define MAX_COMMAND_SIZE 16

struct vhba_request
{
    guint32 tag;
    guint32 lun;
    guint8 cdb[MAX_COMMAND_SIZE];
    guint8 cdb_len;
    guint32 data_len;
};

struct vhba_response
{
    guint32 tag;
    guint32 status;
    guint32 data_len;
};


/* Compute the required kernel I/O buffer size */
gsize cdemu_device_get_kernel_io_buffer_size (CdemuDevice *self G_GNUC_UNUSED)
{
    return BUF_SIZE;
}


/**********************************************************************\
 *             Data buffer (cache) <-> kernel I/O buffer              *
\**********************************************************************/
void cdemu_device_write_buffer (CdemuDevice *self, guint32 length)
{
    guint32 len;

    CDEMU_DEBUG(self, DAEMON_DEBUG_KERNEL_IO, "%s: write data from cache (%d bytes)\n", __debug__, length);

    /* Minimum of requested write length and actual data in our cache */
    len = MIN(self->priv->buffer_size, length);

    /* Make sure there is enough space in (remaining) command output buffer */
    if (self->priv->cmd_out_buffer_pos + len > self->priv->cmd->out_len) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_KERNEL_IO, "%s: OUT buffer too small, truncating!\n", __debug__);
        len = self->priv->cmd->out_len - self->priv->cmd_out_buffer_pos;
    }

    CDEMU_DEBUG(self, DAEMON_DEBUG_KERNEL_IO, "%s: copying %d bytes to OUT buffer at offset %d\n", __debug__, len, self->priv->cmd_out_buffer_pos);
    memcpy(self->priv->cmd->out + self->priv->cmd_out_buffer_pos, self->priv->buffer, len);
    self->priv->cmd_out_buffer_pos += len;
}

void cdemu_device_read_buffer (CdemuDevice *self, guint32 length)
{
    guint32 len;

    CDEMU_DEBUG(self, DAEMON_DEBUG_KERNEL_IO, "%s: read data to cache (%d bytes)\n", __debug__, length);

    /* Minimum of requested read length and (remaining) data in command input buffer */
    len = MIN(self->priv->cmd->in_len - self->priv->cmd_in_buffer_pos, length);

    /* Make sure there is enough space in our cache */
    if (len > self->priv->buffer_capacity) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: copy request size (%d) exceeds our cache size (%d); truncating!buffer\n", __debug__, len, self->priv->buffer_capacity);
        len = self->priv->buffer_capacity;
    }

    CDEMU_DEBUG(self, DAEMON_DEBUG_KERNEL_IO, "%s: copying %d bytes from IN buffer at offset %d\n", __debug__, len, self->priv->cmd_in_buffer_pos);
    memcpy(self->priv->buffer, self->priv->cmd->in + self->priv->cmd_in_buffer_pos, len);
    self->priv->buffer_size = len;
    self->priv->cmd_in_buffer_pos += len;
}


void cdemu_device_flush_buffer (CdemuDevice *self)
{
    CDEMU_DEBUG(self, DAEMON_DEBUG_KERNEL_IO, "%s: flushing cache\n", __debug__);

    memset(self->priv->buffer, 0, self->priv->buffer_size);
    self->priv->buffer_size = 0;
}


/**********************************************************************\
 *                       Sense buffer I/O                             *
\**********************************************************************/
void cdemu_device_write_sense_full (CdemuDevice *self, SenseKey sense_key, guint16 asc_ascq, gint ili, guint32 command_info)
{
    /* Initialize sense */
    struct REQUEST_SENSE_SenseFixed sense;

    memset(&sense, 0, sizeof(struct REQUEST_SENSE_SenseFixed));
    sense.res_code = 0x70; /* Current error */
    sense.valid = 0;
    sense.length = 0x0A; /* Additional sense length */

    /* Sense key and ASC/ASCQ */
    sense.sense_key = sense_key;
    sense.asc = (asc_ascq & 0xFF00) >> 8; /* ASC */
    sense.ascq = (asc_ascq & 0x00FF) >> 0; /* ASCQ */
    /* ILI bit */
    sense.ili = ili;
    /* Command information */
    sense.cmd_info[0] = (command_info & 0xFF000000) >> 24;
    sense.cmd_info[1] = (command_info & 0x00FF0000) >> 16;
    sense.cmd_info[2] = (command_info & 0x0000FF00) >>  8;
    sense.cmd_info[3] = (command_info & 0x000000FF) >>  0;

    CDEMU_DEBUG(self, DAEMON_DEBUG_KERNEL_IO, "%s: writing sense (%" G_GSIZE_MODIFIER "d bytes) to OUT buffer\n", __debug__, sizeof(struct REQUEST_SENSE_SenseFixed));

    /* Write sense directly into command's output buffer */
    memcpy(self->priv->cmd->out, &sense, sizeof(struct REQUEST_SENSE_SenseFixed));
    self->priv->cmd_out_buffer_pos = sizeof(struct REQUEST_SENSE_SenseFixed);
}

void cdemu_device_write_sense (CdemuDevice *self, SenseKey sense_key, guint16 asc_ascq)
{
    return cdemu_device_write_sense_full(self, sense_key, asc_ascq, 0, 0x0000);
}


/**********************************************************************\
 *                    Kernel <-> userspace I/O                        *
\**********************************************************************/
static gboolean cdemu_device_io_handler (GIOChannel *source, GIOCondition condition G_GNUC_UNUSED, CdemuDevice *self)
{
    gint fd = g_io_channel_unix_get_fd(source);
    gssize ret;

    CdemuCommand cmd;
    struct vhba_request *vreq = (gpointer)self->priv->kernel_io_buffer;
    struct vhba_response *vres = (gpointer)self->priv->kernel_io_buffer;

    CDEMU_DEBUG(self, DAEMON_DEBUG_KERNEL_IO, "%s: I/O handler invoked\n", __debug__);

    /* Read request */
    CDEMU_DEBUG(self, DAEMON_DEBUG_KERNEL_IO, "%s: reading request\n", __debug__);

    ret = read(fd, vreq, BUF_SIZE);
    if (ret < (gssize)sizeof(struct vhba_request)) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to read request from control device (%" G_GSIZE_MODIFIER "d bytes; at least %" G_GSIZE_MODIFIER "d required)!\n", __debug__, ret, sizeof(struct vhba_request));
        /* Signal the kernel I/O error, so daemon can restart the device */
        g_signal_emit_by_name(self, "kernel-io-error", NULL);
        return TRUE;
    }

    CDEMU_DEBUG(self, DAEMON_DEBUG_KERNEL_IO, "%s: successfully read request; cmd %02Xh, in/out len %d, tag %d\n", __debug__, vreq->cdb[0], vreq->data_len, vreq->tag);

    /* Initialize CDEMU_Command */
    memcpy(cmd.cdb, vreq->cdb, vreq->cdb_len);
    if (vreq->cdb_len < 12) {
        memset(cmd.cdb + vreq->cdb_len, 0, 12 - vreq->cdb_len);
    }

    cmd.in = (guint8 *)(vreq + 1);
    cmd.out = (guint8 *)(vres + 1);
    cmd.in_len = cmd.out_len = vreq->data_len;

    if (cmd.out_len > BUF_SIZE - sizeof(struct vhba_response)) {
        cmd.out_len = BUF_SIZE - sizeof(struct vhba_response);
    }

    /* Reset command in/out buffer positions */
    self->priv->cmd = &cmd;
    self->priv->cmd_out_buffer_pos = 0;
    self->priv->cmd_in_buffer_pos = 0;

    /* Note that vreq and vres share buffer */
    vres->tag = vreq->tag;
    vres->status = cdemu_device_execute_command(self, cmd.cdb);

    vres->data_len = self->priv->cmd_out_buffer_pos;

    /* Write response */
    CDEMU_DEBUG(self, DAEMON_DEBUG_KERNEL_IO, "%s: writing response\n", __debug__);

    ret = write(fd, vres, BUF_SIZE);
    if (ret < (gssize)sizeof(struct vhba_response)) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to write response to control device (%" G_GSIZE_MODIFIER "d bytes; at least %" G_GSIZE_MODIFIER "d required)!\n", __debug__, ret, sizeof(struct vhba_response));
        /* Signal the kernel I/O error, so daemon can restart the device */
        g_signal_emit_by_name(self, "kernel-io-error", NULL);
        return TRUE;
    }

    CDEMU_DEBUG(self, DAEMON_DEBUG_KERNEL_IO, "%s: I/O handler done\n\n", __debug__);

    return TRUE;
}


static gpointer cdemu_device_io_thread (CdemuDevice *self)
{
	CDEMU_DEBUG(self, DAEMON_DEBUG_KERNEL_IO, "%s: I/O thread started\n", __debug__);
    g_main_loop_run(self->priv->main_loop);
	CDEMU_DEBUG(self, DAEMON_DEBUG_KERNEL_IO, "%s: I/O thread finished\n\n", __debug__);

    return NULL;
}


/**********************************************************************\
 *                      Start/stop functions                          *
\**********************************************************************/
gboolean cdemu_device_start (CdemuDevice *self, const gchar *ctl_device)
{
    GError *local_error = NULL;

    /* Open control device and set up I/O channel */
    self->priv->io_channel = g_io_channel_new_file(ctl_device, "r+", &local_error);
    if (!self->priv->io_channel) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to open control device %s: %s!\n", __debug__, ctl_device, local_error->message);
        g_error_free(local_error);
        return FALSE;
    }

    /* Try setting non-blocking operation */
    if (g_io_channel_set_flags(self->priv->io_channel, G_IO_FLAG_NONBLOCK, &local_error) != G_IO_STATUS_NORMAL) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to set NONBLOCK flag to control device: %s!\n", __debug__, local_error->message);
        g_error_free(local_error);
    }

    /* Create I/O watch */
    self->priv->io_watch = g_io_create_watch(self->priv->io_channel, G_IO_IN);
    g_source_set_callback(self->priv->io_watch, (GSourceFunc)cdemu_device_io_handler, self, NULL);
    g_source_attach(self->priv->io_watch, self->priv->main_context);

    /* Start I/O thread */
#if !GLIB_CHECK_VERSION(2, 32, 0)
    self->priv->io_thread = g_thread_create((GThreadFunc)cdemu_device_io_thread, self, TRUE, &local_error);
#else
    self->priv->io_thread = g_thread_try_new("I/O thread", (GThreadFunc)cdemu_device_io_thread, self, &local_error);
#endif

    if (!self->priv->io_thread) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to start I/O thread: %s\n", __debug__, local_error->message);
        g_error_free(local_error);
        return FALSE;
    }

    return TRUE;
}

void cdemu_device_stop (CdemuDevice *self)
{
    /* Stop the I/O thread */
    if (self->priv->main_loop) {
        if (g_main_loop_is_running(self->priv->main_loop)) {
            g_main_loop_quit(self->priv->main_loop);
        }
    }

	/* Destroy the I/O watch */
	if (self->priv->io_watch) {
		g_source_destroy(self->priv->io_watch);
		g_source_unref(self->priv->io_watch);
		self->priv->io_watch = NULL;
	}

    /* Close the I/O channel */
    if (self->priv->io_channel) {
        g_io_channel_unref(self->priv->io_channel);
        self->priv->io_channel = NULL;
    }

    /* Unref thread */
    if (self->priv->io_thread) {
        /* Wait for the thread to finish (also releases the reference
           to thread object) */
        g_thread_join(self->priv->io_thread);

        self->priv->io_thread = NULL;
    }

    /* Clear device mappings */
    if (self->priv->device_sg) {
        g_free(self->priv->device_sg);
        self->priv->device_sg = NULL;
    }
    if (self->priv->device_sr) {
        g_free(self->priv->device_sr);
        self->priv->device_sr = NULL;
    }
}

