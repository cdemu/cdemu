/*
 *  CDEmuD: Device object - Userspace <-> Kernel bridge
 *  Copyright (C) 2006-2012 Rok Mandeljc
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

#include "cdemud.h"
#include "cdemud-device-private.h"

#define __debug__ "Kernel I/O"


#define TO_SECTOR(len) ((len + 511) / 512)
#define MAX_SENSE 256
#define MAX_SECTORS 256
#define OTHER_SECTORS TO_SECTOR(MAX_SENSE + sizeof(struct vhba_response))
#define BUF_SIZE (512 * (MAX_SECTORS + OTHER_SECTORS))

struct vhba_request
{
    guint32 tag;
    guint32 lun;
#define MAX_COMMAND_SIZE       16

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


/**********************************************************************\
 *                         Data buffer I/O                            *
\**********************************************************************/
void cdemud_device_write_buffer (CDEMUD_Device *self, guint32 length)
{
    guint32 len;

    CDEMUD_DEBUG(self, DAEMON_DEBUG_KERNEL_IO, "%s: write request (%d bytes)\n", __debug__, length);

    len = MIN(self->priv->buffer_size, length);
    if (self->priv->cur_len + len > self->priv->cmd->out_len) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_KERNEL_IO, "%s: OUT buffer too small, truncating!\n", __debug__);
        len = self->priv->cmd->out_len - self->priv->cur_len;
    }

    CDEMUD_DEBUG(self, DAEMON_DEBUG_KERNEL_IO, "%s: copying %d bytes to OUT buffer at offset %d\n", __debug__, len, self->priv->cur_len);
    memcpy(self->priv->cmd->out + self->priv->cur_len, self->priv->buffer, len);
    self->priv->cur_len += len;
}

void cdemud_device_read_buffer (CDEMUD_Device *self, guint32 length)
{
    guint32 len;

    CDEMUD_DEBUG(self, DAEMON_DEBUG_KERNEL_IO, "%s: read request (%d bytes)\n", __debug__, length);

    len = MIN(self->priv->cmd->in_len, length);
    CDEMUD_DEBUG(self, DAEMON_DEBUG_KERNEL_IO, "%s: copying %d bytes from IN buffer\n", __debug__, len);
    memcpy(self->priv->buffer, self->priv->cmd->in, len);
    self->priv->buffer_size = len;
}


void cdemud_device_flush_buffer (CDEMUD_Device *self)
{
    CDEMUD_DEBUG(self, DAEMON_DEBUG_KERNEL_IO, "%s: flushing buffer\n", __debug__);

    memset(self->priv->buffer, 0, self->priv->buffer_size);
    self->priv->buffer_size = 0;
}


/**********************************************************************\
 *                       Sense buffer I/O                             *
\**********************************************************************/
void cdemud_device_write_sense_full (CDEMUD_Device *self, guint8 sense_key, guint16 asc_ascq, gint ili, guint32 command_info)
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

    CDEMUD_DEBUG(self, DAEMON_DEBUG_KERNEL_IO, "%s: writing sense (%d bytes) to OUT buffer\n", __debug__, sizeof(struct REQUEST_SENSE_SenseFixed));

    memcpy(self->priv->cmd->out, &sense, sizeof(struct REQUEST_SENSE_SenseFixed));
    self->priv->cur_len = sizeof(struct REQUEST_SENSE_SenseFixed);
}

void cdemud_device_write_sense (CDEMUD_Device *self, guint8 sense_key, guint16 asc_ascq)
{
    return cdemud_device_write_sense_full(self, sense_key, asc_ascq, 0, 0x0000);
}


/**********************************************************************\
 *                    Kernel <-> userspace I/O                        *
\**********************************************************************/
static gboolean cdemud_device_io_handler (GIOChannel *source, GIOCondition condition G_GNUC_UNUSED, CDEMUD_Device *self)
{
    gint fd = g_io_channel_unix_get_fd(source);

    CDEMUD_Command cmd;
    guchar *buf = g_try_malloc0(BUF_SIZE);
    struct vhba_request *vreq = (void *) buf;
    struct vhba_response *vres = (void *) buf;

	if (!buf) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_ERROR, "%s: failed to allocate memory.\n", __debug__);
        return FALSE;
	}

    if (read(fd, vreq, BUF_SIZE) < sizeof(*vreq)) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_ERROR, "%s: failed to read request from control device!\n", __debug__);
        return FALSE;
    }

    /* Initialize CDEMUD_Command */
    memcpy(cmd.cdb, vreq->cdb, vreq->cdb_len);
    if (vreq->cdb_len < 12) {
        memset(cmd.cdb + vreq->cdb_len, 0, 12 - vreq->cdb_len);
    }

    cmd.in = (guint8 *) (vreq + 1);
    cmd.out = (guint8 *) (vres + 1);
    cmd.in_len = cmd.out_len = vreq->data_len;

    if (cmd.out_len > BUF_SIZE - sizeof(*vres)) {
        cmd.out_len = BUF_SIZE - sizeof(*vres);
    }

    /* Note that vreq and vres share buffer */
    vres->tag = vreq->tag;
    vres->status = cdemud_device_execute_command(self, &cmd);

    vres->data_len = cmd.out_len;

    if (write(fd, (void *)vres, BUF_SIZE) < sizeof(*vres)) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_ERROR, "%s: failed to write response to control device!\n", __debug__);
        return FALSE;
    }

	g_free(buf);

    return TRUE;
}

static gpointer cdemud_device_io_thread (CDEMUD_Device *self)
{
    GSource *source;

    /* Create thread context and main loop */
    GMainContext *main_context = g_main_context_new();
    self->priv->main_loop = g_main_loop_new(main_context, FALSE);

    /* Create watch source */
    source = g_io_create_watch(self->priv->io_channel, G_IO_IN);
    g_source_set_callback(source, (GSourceFunc)cdemud_device_io_handler, self, NULL);
    g_source_attach(source, self->priv->main_context);
    g_source_unref(source);

    /* Run */
    g_main_loop_run(self->priv->main_loop);

    /* Cleanup */
    g_main_context_unref(main_context);

    return NULL;
}


GThread *cdemud_device_create_io_thread (CDEMUD_Device *self)
{
    return g_thread_create((GThreadFunc)cdemud_device_io_thread, self, TRUE, NULL);
}

void cdemud_device_stop_io_thread (CDEMUD_Device *self)
{
    if (self->priv->main_loop) {
        if (g_main_loop_is_running(self->priv->main_loop)) {
            g_main_loop_quit(self->priv->main_loop);
            /* Wait for the thread to finish */
            g_thread_join(self->priv->io_thread);
        }

        g_main_loop_unref(self->priv->main_loop);
        self->priv->main_loop = NULL;
    }
}
