/*
 *  CDEmuD: Daemon object
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

#include "cdemud.h"
#include "cdemud-service-glue.h"
#include "cdemud-marshallers.h"


/* Userspace <-> Kernel bridge */
#define TO_SECTOR(len) ((len + 511) / 512)
#define MAX_SENSE 256
#define MAX_SECTORS 128
#define OTHER_SECTORS TO_SECTOR(MAX_SENSE + sizeof(struct vhba_response))
#define BUF_SIZE (512 * (MAX_SECTORS + OTHER_SECTORS))

struct vhba_request {
    guint32 tag;
    guint32 lun;
#define MAX_COMMAND_SIZE       16

    guint8 cdb[MAX_COMMAND_SIZE];
    guint8 cdb_len;
    guint32 data_len;
};

struct vhba_response {
    guint32 tag;
    guint32 status;
    guint32 data_len;
};


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define CDEMUD_DAEMON_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), CDEMUD_TYPE_DAEMON, CDEMUD_DaemonPrivate))

typedef struct {    
    gboolean initialized;
    
    gchar *version;
    gchar *audio_backend;
        
    GMainLoop *main_loop;
    
    DBusGConnection *bus;
    
    gchar *ctl_device;
    
    gint number_of_devices;
    GList *list_of_devices;
    
    guint mapping_id;
    gint mapping_attempt;
} CDEMUD_DaemonPrivate;


/******************************************************************************\
 *                              Private functions                             *
\******************************************************************************/
static void __cdemud_daemon_device_status_changed_handler (GObject *device, CDEMUD_Daemon *self) {
    /*CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);*/
    gint number = 0;
        
    cdemud_device_get_device_number(CDEMUD_DEVICE(device), &number, NULL);
    g_signal_emit_by_name(self, "device-status-changed", number, NULL);
    
    return;
}

static void __cdemud_daemon_device_option_changed_handler (GObject *device, gchar *option, CDEMUD_Daemon *self) {
    /*CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);*/
    gint number = 0;
        
    cdemud_device_get_device_number(CDEMUD_DEVICE(device), &number, NULL);
    g_signal_emit_by_name(self, "device-option-changed", number, option, NULL);
    
    return;
}


static void __cdemud_daemon_io_close (gpointer data) {
    GIOChannel *ch = data;
    gint fd = g_io_channel_unix_get_fd(ch);

    close(fd);
    g_io_channel_unref(ch);
}

static gboolean __cdemud_daemon_destroy_devices (CDEMUD_Daemon *self, GError **error) {
    CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);
    
    GList *entry = NULL;
    
    G_LIST_FOR_EACH(entry, _priv->list_of_devices) {
        GObject *dev = entry->data;
        if (dev) {
            g_object_unref(dev);
        }
    }
    
    g_list_free(_priv->list_of_devices);
    
    return TRUE;
}

static gboolean __cdemud_daemon_get_device (CDEMUD_Daemon *self, gint device_number, GObject **device, GError **error) {
    CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);
    
    CDEMUD_CHECK_ARG(device);
    
    if (device_number >= 0 && device_number < _priv->number_of_devices) {
        *device = g_list_nth_data(_priv->list_of_devices, device_number);
    } else {
        cdemud_error(CDEMUD_E_INVALIDDEVICE, error);
        return FALSE;
    }
    
    return TRUE;
}

static gboolean __cdemud_daemon_io_handler (GIOChannel *source, GIOCondition condition, gpointer data) {
    CDEMUD_Device *dev = CDEMUD_DEVICE(data);
    gint fd = g_io_channel_unix_get_fd(source);
    CDEMUD_Command cmd;
    guchar buf[BUF_SIZE] = {0};
    struct vhba_request *vreq = (void *) buf;
    struct vhba_response *vres = (void *) buf;
    
    if (read(fd, vreq, BUF_SIZE) < sizeof(*vreq)) {
        CDEMUD_DEBUG(dev, DAEMON_DEBUG_ERROR, "%s: failed to read request from control device!\n", __func__);
        return FALSE;
    }

    /* Initialize CDEMUD_Command */
    memcpy(cmd.cdb, vreq->cdb, vreq->cdb_len);
    if (vreq->cdb_len < 12)
            memset(cmd.cdb + vreq->cdb_len, 0, 12 - vreq->cdb_len);

    cmd.in = (guint8 *) (vreq + 1);
    cmd.out = (guint8 *) (vres + 1);
    cmd.in_len = cmd.out_len = vreq->data_len;
    
    if (cmd.out_len > BUF_SIZE - sizeof(*vres))
            cmd.out_len = BUF_SIZE - sizeof(*vres);

    /* Note that vreq and vres share buffer */
    vres->tag = vreq->tag;
    vres->status = cdemud_device_execute(dev, &cmd);

    vres->data_len = cmd.out_len;

    if (write(fd, (void *)vres, BUF_SIZE) < sizeof(*vres)) {
        CDEMUD_DEBUG(dev, DAEMON_DEBUG_ERROR, "%s: failed to write response to control device!\n", __func__);
        return FALSE;
    }

    return TRUE;
}

static gboolean __cdemud_daemon_build_device_mapping_callback (gpointer data) {
    CDEMUD_Daemon *self = data;
    CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);
    gboolean run_again = FALSE;
    gint i;
    
    for (i = 0; i < _priv->number_of_devices; i++) {
        GObject *dev = NULL;
        GIOChannel *ch = NULL;
        gint fd = 0;
        gint ioctl_ret = 0;
        gint32 id[4] = {0};
        
        __cdemud_daemon_get_device(self, i, &dev, NULL);
        ch = g_object_get_data(dev, "iochannel");
        fd = g_io_channel_unix_get_fd(ch);
        
        ioctl_ret = ioctl(fd, 0xBEEF001, id);
                
        if (ioctl_ret == -ENODEV) {
            /* ENODEV means the device hasn't been registered yet; try again later */
            run_again = TRUE;
            break;
        } else if (ioctl_ret < 0) {
            /* Other errors */
            CDEMUD_DEBUG(dev, DAEMON_DEBUG_WARNING, "%s: error while performing ioctl (%d); device mapping info will not be available\n", __func__, ioctl_ret);
            run_again = FALSE;
            break;
        } else {
            /* FIXME: until we figure how to get SCSI CD-ROM and SCSI Generic Device
               device paths directly from kernel, we'll have to live with parsing of the
               sysfs dir :/ */
            struct sysfs_device *sysfs_dev = NULL;
            gchar *scsi_bus_id = g_strdup_printf("%i:%i:%i:%i", id[0], id[1], id[2], id[3]);
        
            sysfs_dev = sysfs_open_device("scsi", scsi_bus_id);
            g_free(scsi_bus_id);
            
            if (!sysfs_dev) {
                CDEMUD_DEBUG(dev, DAEMON_DEBUG_WARNING, "%s: sysfs device for device #%i could not be opened; device mapping info for this device will not be available\n", __func__, i);
                continue;
            }
            
            /* Actually, we use sysfs only to get ourselves the device path (for
               example, if sysfs is not mounted under /sys, etc.). For the rest,
               we'll use GDir and its content listing */
            GDir *dir_dev = g_dir_open(sysfs_dev->path, 0, NULL);
            const gchar *entry_name = NULL;
            gchar path_sr[16] = "";
            gchar path_sg[16] = "";
                
            if (dir_dev) {
                while ((entry_name = g_dir_read_name(dir_dev))) {
                    /* SCSI CD-ROM device */
                    if (!strlen(path_sr)) {
                        if (sscanf(entry_name, "block:%s", path_sr) == 1) {
                            continue;
                        }
                    
                        if (!g_ascii_strcasecmp(entry_name, "block")) {
                            gchar *dirpath = g_build_filename(sysfs_dev->path, entry_name, NULL);
                            GDir *tmp_dir = g_dir_open(dirpath, 0, NULL);
                            const gchar *tmp_sr = g_dir_read_name(tmp_dir);
                                                
                            strcpy(path_sr, tmp_sr);
                        
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
                            gchar *symlink = g_build_filename(sysfs_dev->path, entry_name, NULL);
                            gchar *tmp_path = g_file_read_link(symlink, NULL);
                            gchar *tmp_sg = g_path_get_basename(tmp_path);
                            
                            strcpy(path_sg, tmp_sg);
                            
                            g_free(tmp_sg);
                            g_free(tmp_path);
                            g_free(symlink);
                            
                            continue;
                        }
                    }
                }
                g_dir_close(dir_dev);
            }

            sysfs_close_device(sysfs_dev);

            /* Actual path building */
            gchar *fullpath_sr = NULL;
            gchar *fullpath_sg = NULL;
            
            if (strlen(path_sr)) {
                fullpath_sr = g_strconcat("/dev/", path_sr, NULL);
            } else {
                CDEMUD_DEBUG(dev, DAEMON_DEBUG_WARNING, "%s: device mapping (SCSI CD-ROM) for device #%i could not be determined; device mapping info for this device will not be available\n", __func__, i);
            }
            
            if (strlen(path_sg)) {
                fullpath_sg = g_strconcat("/dev/", path_sg, NULL);
            } else {
                CDEMUD_DEBUG(dev, DAEMON_DEBUG_WARNING, "%s: device mapping (SCSI generic) for device #%i could not be determined; device mapping info for this device will not be available\n", __func__, i);
            }
            
            cdemud_device_set_mapping(CDEMUD_DEVICE(dev), fullpath_sr, fullpath_sg, NULL);
            
            g_free(fullpath_sr);
            g_free(fullpath_sg);
        }
    }
    
    /* After five attempts, give up */
    if (_priv->mapping_attempt++ > 5) {
        run_again = FALSE;
    }
        
    /* If we're done here, it's time to send the "daemon-started" signal */
    if (!run_again) {
        g_signal_emit_by_name(G_OBJECT(self), "daemon-started", NULL);
    }
    
    return run_again;
}

/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
gboolean cdemud_daemon_initialize (CDEMUD_Daemon *self, gint num_devices, gchar *ctl_device, gchar *audio_driver, gboolean system_bus, GError **error) {
    CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);
    GObject *debug_context = NULL;
    DBusGProxy *bus_proxy = NULL;
    gint bus_type = system_bus ? DBUS_BUS_SYSTEM : DBUS_BUS_SESSION;
    guint result = 0;
    gint i;
    
    /* Debug context; so that we get daemon's errors/warnings from the very beginning */
    debug_context = g_object_new(MIRAGE_TYPE_DEBUG_CONTEXT, NULL);
    mirage_debug_context_set_name(MIRAGE_DEBUG_CONTEXT(debug_context), "cdemud", NULL);
    mirage_debug_context_set_domain(MIRAGE_DEBUG_CONTEXT(debug_context), "CDEMUD", NULL);
    mirage_object_set_debug_context(MIRAGE_OBJECT(self), debug_context, NULL);
    g_object_unref(debug_context);
    
    /* Control device */
    _priv->ctl_device = g_strdup(ctl_device);
    
    /* Number of devices */
    _priv->number_of_devices = num_devices;
            
    /* Glib's main loop */
    _priv->main_loop = g_main_loop_new(NULL, FALSE);
    
    /* Initialize libao */
    ao_initialize();
    
    /* Initialize our DBUS interface; unless told to use system bus, we'll use
       session one */
    dbus_g_object_type_install_info(CDEMUD_TYPE_DAEMON, &dbus_glib_cdemud_daemon_object_info);
    _priv->bus = dbus_g_bus_get(bus_type, error);
    if (!_priv->bus) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to get %s bus!\n", __func__, system_bus ? "system" : "session");
        cdemud_error(CDEMUD_E_DBUSCONNECT, error);
        return FALSE;
    }
    
    bus_proxy = dbus_g_proxy_new_for_name(_priv->bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus");
    
    if (!dbus_g_proxy_call(bus_proxy, "RequestName", error, G_TYPE_STRING, "net.sf.cdemu.CDEMUD_Daemon", G_TYPE_UINT, DBUS_NAME_FLAG_DO_NOT_QUEUE, G_TYPE_INVALID, G_TYPE_UINT, &result, G_TYPE_INVALID)) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to request name on %s bus!\n", __func__, system_bus ? "system" : "session");
        cdemud_error(CDEMUD_E_DBUSNAMEREQUEST, error);
        return FALSE;
    }
    
    /* Make sure we're primary owner of requested name... otherwise it's likely
       that an instance is already running on that bus */
    if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to become primary owner of name on %s bus; is there another instance already running?\n", __func__, system_bus ? "system" : "session");
        cdemud_error(CDEMUD_E_DBUSNAMEREQUEST, error);
        return FALSE;
    }

    dbus_g_connection_register_g_object(_priv->bus, "/CDEMUD_Daemon", G_OBJECT(self));
    
    /* Create desired number of devices */   
    for (i = 0; i < _priv->number_of_devices; i++) {
        GObject *dev = g_object_new(CDEMUD_TYPE_DEVICE, NULL);
        
        if (cdemud_device_initialize(CDEMUD_DEVICE(dev), i, audio_driver, NULL)) {
            gint fd = 0;
            GIOChannel *ch = NULL;
            
            /* Set parent */
            mirage_object_set_parent(MIRAGE_OBJECT(dev), G_OBJECT(self), NULL);
            /* Don't attach child... MIRAGE_Objects pass debug context to children,
               and CDEMUD_Devices have each its own context... */
            /* Add handling for signals from the device... this allows us to 
               pass them on via DBUS */
            g_signal_connect(dev, "status-changed", (GCallback)__cdemud_daemon_device_status_changed_handler, self);
            g_signal_connect(dev, "option-changed", (GCallback)__cdemud_daemon_device_option_changed_handler, self);
            
            /* Open control device and set up I/O channel */
            fd = open(_priv->ctl_device, O_RDWR | O_SYNC | O_NONBLOCK);
            if (fd < 0) {
                CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to open control device %s!\n", __func__, _priv->ctl_device);
                g_object_unref(dev);
                cdemud_error(CDEMUD_E_CTLDEVICE, error);
                return FALSE;
            }
            ch = g_io_channel_unix_new(fd);
            g_io_add_watch(ch, G_IO_IN, __cdemud_daemon_io_handler, dev);

            g_object_set_data_full(dev, "iochannel", ch, __cdemud_daemon_io_close);
            
            /* Add it to devices list */
            _priv->list_of_devices = g_list_append(_priv->list_of_devices, dev);
        } else {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to initialize device %i!\n", __func__, i);
            g_object_unref(dev);
            cdemud_error(CDEMUD_E_DEVICEINITFAILED, error);
            return FALSE;
        }            
    }
    
    /* In order to build device mapping, we'll have to fire our callback sometime
       after the daemon actually starts (so that command handler is actually
       active and the SCSI layer actually does device registration on the kernel
       side)... */
    _priv->mapping_id = g_timeout_add(1000, __cdemud_daemon_build_device_mapping_callback, self);
    
    /* We successfully finished initialization */
    _priv->initialized = TRUE;
    
    return TRUE;
}

gboolean cdemud_daemon_start_daemon (CDEMUD_Daemon *self, GError **error) {
    CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);
    
    if (!_priv->initialized) {
        cdemud_error(CDEMUD_E_OBJNOTINIT, error);
        return FALSE;
    }
        
    /* We don't emit "daemon-started" signal here anymore; we do it after the
       device maps are complete... */
    g_main_loop_run(_priv->main_loop);
    
    return TRUE;
}

gboolean cdemud_daemon_stop_daemon (CDEMUD_Daemon *self, GError **error) {
    CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);

    g_main_loop_quit(_priv->main_loop);
    g_signal_emit_by_name(G_OBJECT(self), "daemon-stopped", NULL);
    
    return TRUE;
}


/******************************************************************************\
 *                           DBUS interface functions                         *
\******************************************************************************/
gboolean cdemud_daemon_get_daemon_version (CDEMUD_Daemon *self, gchar **version, GError **error) {
    CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);
    /* Copy version string */
    *version = g_strdup(_priv->version);
    return TRUE;
}

gboolean cdemud_daemon_get_library_version (CDEMUD_Daemon *self, gchar **version, GError **error) {
    const gchar *libver = libmirage_get_version(error);
    
    if (!libver) {
        return FALSE;
    }
    
    *version = g_strdup(libver);
    return TRUE;
}

gboolean cdemud_daemon_get_daemon_interface_version (CDEMUD_Daemon *self, gint *version, GError **error) {
    *version = DAEMON_INTERFACE_VERSION;
    return TRUE;
}

static GPtrArray *__encode_masks (const MIRAGE_DebugMask *masks, gint num_masks) {
    GPtrArray *ret_masks = g_ptr_array_new();
    gint i;
    
    for (i = 0; i < num_masks; i++) {
        /* Create value array */
        GValueArray *mask_entry = g_value_array_new(2);
        /* Mask name */
        g_value_array_append(mask_entry, NULL);
        g_value_init(g_value_array_get_nth(mask_entry, 0), G_TYPE_STRING);
        g_value_set_string(g_value_array_get_nth(mask_entry, 0), masks[i].name);
        /* Mask value */
        g_value_array_append(mask_entry, NULL);
        g_value_init(g_value_array_get_nth(mask_entry, 1), G_TYPE_INT);
        g_value_set_int(g_value_array_get_nth(mask_entry, 1), masks[i].value);
        /* Add mask's value array to masks' pointer array */
        g_ptr_array_add(ret_masks, mask_entry);
    }
    
    return ret_masks;
}

gboolean cdemud_daemon_enum_daemon_debug_masks (CDEMUD_Daemon *self, GPtrArray **masks, GError **error) {
    /*CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);*/
    static const MIRAGE_DebugMask dbg_masks[] = {
        { "DAEMON_DEBUG_DEV_FIXME", DAEMON_DEBUG_DEV_FIXME },
        { "DAEMON_DEBUG_DEV_PC_TRACE", DAEMON_DEBUG_DEV_PC_TRACE },
        { "DAEMON_DEBUG_DEV_PC_DUMP", DAEMON_DEBUG_DEV_PC_DUMP },
        { "DAEMON_DEBUG_DEV_DELAY", DAEMON_DEBUG_DEV_DELAY },
        { "DAEMON_DEBUG_DEV_AUDIOPLAY", DAEMON_DEBUG_DEV_AUDIOPLAY },
    };
    
    *masks = __encode_masks(dbg_masks, G_N_ELEMENTS(dbg_masks));
    return TRUE;
}

gboolean cdemud_daemon_enum_library_debug_masks (CDEMUD_Daemon *self, GPtrArray **masks, GError **error) {
    /*CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);*/
    const MIRAGE_DebugMask *dbg_masks;
    gint num_dbg_masks;
    
    /* Get masks */
    if (!libmirage_get_supported_debug_masks(&dbg_masks, &num_dbg_masks, error)) {
        return FALSE;
    }
    
    *masks = __encode_masks(dbg_masks, num_dbg_masks);
    return TRUE;
}

static gboolean __cdemud_daemon_add_supported_parser (gpointer data, gpointer user_data) {
    MIRAGE_ParserInfo* info = data;
    GPtrArray* parsers = user_data;
    
    GValueArray *parser_entry = NULL;
    
    /* Create value array */
    parser_entry = g_value_array_new(7);
        
    /* ID */
    g_value_array_append(parser_entry, NULL);
    g_value_init(g_value_array_get_nth(parser_entry, 0), G_TYPE_STRING);
    g_value_set_string(g_value_array_get_nth(parser_entry, 0), info->id);
    
    /* Name */
    g_value_array_append(parser_entry, NULL);
    g_value_init(g_value_array_get_nth(parser_entry, 1), G_TYPE_STRING);
    g_value_set_string(g_value_array_get_nth(parser_entry, 1), info->name);
    
    /* Version */
    g_value_array_append(parser_entry, NULL);
    g_value_init(g_value_array_get_nth(parser_entry, 2), G_TYPE_STRING);
    g_value_set_string(g_value_array_get_nth(parser_entry, 2), info->version);
        
    /* Author */
    g_value_array_append(parser_entry, NULL);
    g_value_init(g_value_array_get_nth(parser_entry, 3), G_TYPE_STRING);
    g_value_set_string(g_value_array_get_nth(parser_entry, 3), info->author);
    
    /* Multiple files support */
    g_value_array_append(parser_entry, NULL);
    g_value_init(g_value_array_get_nth(parser_entry, 4), G_TYPE_BOOLEAN);
    g_value_set_boolean(g_value_array_get_nth(parser_entry, 4), info->multi_file);
    
    /* Description */
    g_value_array_append(parser_entry, NULL);
    g_value_init(g_value_array_get_nth(parser_entry, 5), G_TYPE_STRING);
    g_value_set_string(g_value_array_get_nth(parser_entry, 5), info->description);
    
    /* Suffixes */
    g_value_array_append(parser_entry, NULL);
    g_value_init(g_value_array_get_nth(parser_entry, 6), G_TYPE_STRV);
    g_value_set_boxed(g_value_array_get_nth(parser_entry, 6), info->suffixes);
    
    /* Add mask's value array to masks' pointer array */
    g_ptr_array_add(parsers, parser_entry);
    
    return TRUE;
}

gboolean cdemud_daemon_enum_supported_parsers (CDEMUD_Daemon *self, GPtrArray **parsers, GError **error) {
    /*CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);*/
    *parsers = g_ptr_array_new();
    return libmirage_for_each_parser(__cdemud_daemon_add_supported_parser, *parsers, error);
}


static gboolean __cdemud_daemon_add_supported_fragment (gpointer data, gpointer user_data) {
    MIRAGE_FragmentInfo* info = data;
    GPtrArray* fragments = user_data;
    
    GValueArray *fragment_entry = NULL;
    
    /* Create value array */
    fragment_entry = g_value_array_new(7);
    
    /* ID */
    g_value_array_append(fragment_entry, NULL);
    g_value_init(g_value_array_get_nth(fragment_entry, 0), G_TYPE_STRING);
    g_value_set_string(g_value_array_get_nth(fragment_entry, 0), info->id);
    
    /* Name */
    g_value_array_append(fragment_entry, NULL);
    g_value_init(g_value_array_get_nth(fragment_entry, 1), G_TYPE_STRING);
    g_value_set_string(g_value_array_get_nth(fragment_entry, 1), info->name);
    
    /* Version */
    g_value_array_append(fragment_entry, NULL);
    g_value_init(g_value_array_get_nth(fragment_entry, 2), G_TYPE_STRING);
    g_value_set_string(g_value_array_get_nth(fragment_entry, 2), info->version);
        
    /* Author */
    g_value_array_append(fragment_entry, NULL);
    g_value_init(g_value_array_get_nth(fragment_entry, 3), G_TYPE_STRING);
    g_value_set_string(g_value_array_get_nth(fragment_entry, 3), info->author);
    
    /* Interface */
    g_value_array_append(fragment_entry, NULL);
    g_value_init(g_value_array_get_nth(fragment_entry, 4), G_TYPE_STRING);
    g_value_set_string(g_value_array_get_nth(fragment_entry, 4), info->interface);
    
    /* Suffixes */
    g_value_array_append(fragment_entry, NULL);
    g_value_init(g_value_array_get_nth(fragment_entry, 5), G_TYPE_STRV);
    g_value_set_boxed(g_value_array_get_nth(fragment_entry, 5), info->suffixes);
    
    /* Add mask's value array to masks' pointer array */
    g_ptr_array_add(fragments, fragment_entry);
    
    return TRUE;
}

gboolean cdemud_daemon_enum_supported_fragments (CDEMUD_Daemon *self, GPtrArray **fragments, GError **error) {
    /*CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);*/
    *fragments = g_ptr_array_new();
    return libmirage_for_each_fragment(__cdemud_daemon_add_supported_fragment, *fragments, error);
}

gboolean cdemud_daemon_get_number_of_devices (CDEMUD_Daemon *self, gint *number_of_devices, GError **error) {
    CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);
    *number_of_devices = _priv->number_of_devices;
    return TRUE;
}

gboolean cdemud_daemon_device_get_status (CDEMUD_Daemon *self, gint device_number, gboolean *loaded, gchar ***file_names, GError **error) {
    /*CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);*/
    GObject *dev = NULL;

    if (__cdemud_daemon_get_device(self, device_number, &dev, error)) {
        if (cdemud_device_get_status(CDEMUD_DEVICE(dev), loaded, file_names, error)) {
            return TRUE;
        }
    }
    
    return FALSE;
}

gboolean cdemud_daemon_device_get_mapping (CDEMUD_Daemon *self, gint device_number, gchar **sr_device, gchar **sg_device, GError **error) {
    /*CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);*/
    GObject *dev = NULL;

    if (__cdemud_daemon_get_device(self, device_number, &dev, error)) {
        if (cdemud_device_get_mapping(CDEMUD_DEVICE(dev), sr_device, sg_device, error)) {
            return TRUE;
        }
    }
    
    return FALSE;
}

gboolean cdemud_daemon_device_load (CDEMUD_Daemon *self, gint device_number, gchar **file_names, GHashTable *parameters, GError **error) {
    /*CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);*/
    GObject *dev = NULL;
        
    if (__cdemud_daemon_get_device(self, device_number, &dev, error)) {
        if (cdemud_device_load_disc(CDEMUD_DEVICE(dev), file_names, parameters, error)) {
            return TRUE;
        }
    }
    
    return FALSE;
}

gboolean cdemud_daemon_device_unload (CDEMUD_Daemon *self, gint device_number, GError **error) {
    /*CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);*/
    GObject *dev = NULL;
        
    if (__cdemud_daemon_get_device(self, device_number, &dev, error)) {
        if (cdemud_device_unload_disc(CDEMUD_DEVICE(dev), error)) {
            return TRUE;
        }
    }
    
    return FALSE;
}

gboolean cdemud_daemon_device_get_option (CDEMUD_Daemon *self, gint device_number, gchar *option_name, GPtrArray **option_values, GError **error) {
    /*CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);*/
    GObject *dev = NULL;
        
    if (__cdemud_daemon_get_device(self, device_number, &dev, error)) {
        if (cdemud_device_get_option(CDEMUD_DEVICE(dev), option_name, option_values, error)) {
            return TRUE;
        }
    }
    
    return FALSE;
}

gboolean cdemud_daemon_device_set_option (CDEMUD_Daemon *self, gint device_number, gchar *option_name, GPtrArray *option_values, GError **error) {
    /*CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);*/
    GObject *dev = NULL;
        
    if (__cdemud_daemon_get_device(self, device_number, &dev, error)) {
        if (cdemud_device_set_option(CDEMUD_DEVICE(dev), option_name, option_values, error)) {
            return TRUE;
        }
    }
    
    return FALSE;
}

/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static MIRAGE_ObjectClass *parent_class = NULL;

static void __cdemud_daemon_instance_init (GTypeInstance *instance, gpointer g_class) {
    CDEMUD_Daemon *self = CDEMUD_DAEMON(instance);
    CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);
    
    /* Set version string */
    _priv->version = g_strdup(PACKAGE_VERSION);
    
    return;
}

static void __cdemud_daemon_finalize (GObject *obj) {
    CDEMUD_Daemon *self = CDEMUD_DAEMON(obj);
    CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);
    
    g_main_loop_unref(_priv->main_loop);

    __cdemud_daemon_destroy_devices(self, NULL);
        
    /* Free control device path */
    g_free(_priv->ctl_device);
    
    /* Free version string */
    g_free(_priv->version);
    
    /* Shutdown libao */
    ao_shutdown();
    
    /* Chain up to the parent class */
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __cdemud_daemon_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(g_class);
    CDEMUD_DaemonClass *klass = CDEMUD_DAEMON_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(CDEMUD_DaemonPrivate));
    
    /* Initialize GObject methods */
    gobject_class->finalize = __cdemud_daemon_finalize;
    
    /* Signal handlers */        
    klass->signals[0] = g_signal_new("daemon-started", G_OBJECT_CLASS_TYPE(klass), (G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED), 0, NULL, NULL, g_cclosure_user_marshal_VOID__VOID, G_TYPE_NONE, 0, NULL);
    klass->signals[1] = g_signal_new("daemon-stopped", G_OBJECT_CLASS_TYPE(klass), (G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED), 0, NULL, NULL, g_cclosure_user_marshal_VOID__VOID, G_TYPE_NONE, 0, NULL);
    klass->signals[2] = g_signal_new("device-status-changed", G_OBJECT_CLASS_TYPE(klass), (G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED), 0, NULL, NULL, g_cclosure_user_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT, NULL);
    klass->signals[3] = g_signal_new("device-option-changed", G_OBJECT_CLASS_TYPE(klass), (G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED), 0, NULL, NULL, g_cclosure_user_marshal_VOID__INT_STRING, G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_STRING, NULL);
    
    return;
}

GType cdemud_daemon_get_type (void) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(CDEMUD_DaemonClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __cdemud_daemon_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(CDEMUD_Daemon),
            0,      /* n_preallocs */
            __cdemud_daemon_instance_init    /* instance_init */
        };
        
        type = g_type_register_static(MIRAGE_TYPE_OBJECT, "CDEMUD_Daemon", &info, 0);
    }
    
    return type;
}
