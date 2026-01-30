/*
 *  CDEmu daemon: device - device mapping
 *  Copyright (C) 2006-2026 Rok Mandeljc
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

#define __debug__ "Mapping"


gboolean cdemu_device_setup_mapping (CdemuDevice *self)
{
    gboolean try_again = FALSE;
    gint ioctl_ret;
    gint32 id[4] = {0};

    /* Perform IOCTL */
    ioctl_ret = ioctl(g_io_channel_unix_get_fd(self->priv->io_channel), 0xBEEF001, id);

    if (ioctl_ret == -ENODEV) {
        /* ENODEV means the device hasn't been registered yet... */
        try_again = TRUE; /* ... try again later */
    } else if (ioctl_ret < 0) {
        /* Other errors */
        CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: error while performing ioctl (%d); device mapping info will not be available\n", __debug__, ioctl_ret);
    } else {
        /* FIXME: until we figure how to get SCSI CD-ROM and SCSI Generic Device
         * device paths directly from kernel, we'll have to live with parsing of the
         * sysfs dir :/ */
        gchar *sysfs_dev_path = g_strdup_printf("/sys/bus/scsi/devices/%i:%i:%i:%i", id[0], id[1], id[2], id[3]);
        GDir *dir_dev = g_dir_open(sysfs_dev_path, 0, NULL);

        gchar path_sr[16] = "";
        gchar path_sg[16] = "";

        if (!dir_dev) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: sysfs device for device #%i (%s) could not be opened; device mapping info for this device will not be available\n", __debug__, self->priv->number, sysfs_dev_path);
        } else {
            /* Iterate through sysfs dir */
            const gchar *entry_name;
            while ((entry_name = g_dir_read_name(dir_dev))) {
                /* SCSI CD-ROM device */
                if (!strlen(path_sr)) {
                    if (sscanf(entry_name, "block:%s", path_sr) == 1) {
                        continue;
                    }

                    if (!g_ascii_strcasecmp(entry_name, "block")) {
                        gchar *dirpath = g_build_filename(sysfs_dev_path, entry_name, NULL);
                        GDir *tmp_dir = g_dir_open(dirpath, 0, NULL);
                        const gchar *tmp_sr = g_dir_read_name(tmp_dir);

                        g_strlcpy(path_sr, tmp_sr, sizeof(path_sr));

                        g_dir_close(tmp_dir);
                        g_free(dirpath);
                        continue;
                    }
                }

                /* SCSI generic device */
                if (!strlen(path_sg)) {
                    if (sscanf(entry_name, "scsi_generic:%s", path_sg) == 1) {
                        continue;
                    }

                    if (!g_ascii_strcasecmp(entry_name, "generic")) {
                        gchar *symlink = g_build_filename(sysfs_dev_path, entry_name, NULL);
                        gchar *tmp_path = g_file_read_link(symlink, NULL);
                        gchar *tmp_sg = g_path_get_basename(tmp_path);

                        g_strlcpy(path_sg, tmp_sg, sizeof(path_sg));

                        g_free(tmp_sg);
                        g_free(tmp_path);
                        g_free(symlink);

                        continue;
                    }
                }
            }
            g_dir_close(dir_dev);

            /* Actual path building */
            if (strlen(path_sr)) {
                self->priv->device_sr = g_strconcat("/dev/", path_sr, NULL);
            } else {
                CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: device mapping (SCSI CD-ROM) for device #%i could not be determined; device mapping info for this device will not be available\n", __debug__, self->priv->number);
                self->priv->device_sr = NULL;
            }

            if (strlen(path_sg)) {
                self->priv->device_sg = g_strconcat("/dev/", path_sg, NULL);
            } else {
                CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: device mapping (SCSI generic) for device #%i could not be determined; device mapping info for this device will not be available\n", __debug__, self->priv->number);
                self->priv->device_sg = NULL;
            }
        }

        g_free(sysfs_dev_path);
    }

    /* If we won't be repeating the callback, emit the 'ready' signal */
    if (!try_again) {
        g_signal_emit_by_name(self, "mapping-ready", NULL);
    }

    return try_again;
}

void cdemu_device_get_mapping (CdemuDevice *self, gchar **sr_device, gchar **sg_device)
{
    /* Return values, if applicable */
    if (self->priv->device_sr) {
        *sr_device = g_strdup(self->priv->device_sr);
    } else {
        *sr_device = NULL;
    }

    if (self->priv->device_sg) {
        *sg_device = g_strdup(self->priv->device_sg);
    } else {
        *sg_device = NULL;
    }
}
