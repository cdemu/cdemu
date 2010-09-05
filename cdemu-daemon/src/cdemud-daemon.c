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

#include "cdemud.h"
#include "cdemud-service-glue.h"
#include "cdemud-marshallers.h"


#define __debug__ "Daemon"


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

static GObject *__cdemud_daemon_get_device (CDEMUD_Daemon *self, gint device_number, GError **error) {
    CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);

    if (device_number >= 0 && device_number < _priv->number_of_devices) {
        return g_list_nth_data(_priv->list_of_devices, device_number);
    }

    cdemud_error(CDEMUD_E_INVALIDDEVICE, error);
    return NULL;
}

static gboolean __cdemud_daemon_build_device_mapping_callback (gpointer data) {
    CDEMUD_Daemon *self = data;
    CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);
    gboolean run_again = FALSE;
    gint i;

    /* Try to setup mapping on each device */
    for (i = 0; i < _priv->number_of_devices; i++) {
        GObject *dev = __cdemud_daemon_get_device(self, i, NULL);

        if (!cdemud_device_setup_mapping(CDEMUD_DEVICE(dev))) {
            /* FALSE means that device hasn't been registered yet; try again later */
            run_again = TRUE;
            break;
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
    GObject *debug_context;
    DBusGProxy *bus_proxy;
    GError *dbus_error = NULL;
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
    _priv->bus = dbus_g_bus_get(bus_type, &dbus_error);
    if (!_priv->bus) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to get %s bus: %s!\n", __debug__, system_bus ? "system" : "session", dbus_error->message);
        g_error_free(dbus_error);
        cdemud_error(CDEMUD_E_DBUSCONNECT, error);
        return FALSE;
    }

    bus_proxy = dbus_g_proxy_new_for_name(_priv->bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus");

    if (!dbus_g_proxy_call(bus_proxy, "RequestName", &dbus_error, G_TYPE_STRING, "net.sf.cdemu.CDEMUD_Daemon", G_TYPE_UINT, DBUS_NAME_FLAG_DO_NOT_QUEUE, G_TYPE_INVALID, G_TYPE_UINT, &result, G_TYPE_INVALID)) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to request name on %s bus!\n", __debug__, system_bus ? "system" : "session");
        g_error_free(dbus_error);
        cdemud_error(CDEMUD_E_DBUSNAMEREQUEST, error);
        return FALSE;
    }

    /* Make sure we're primary owner of requested name... otherwise it's likely
       that an instance is already running on that bus */
    if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to become primary owner of name on %s bus; is there another instance already running?\n", __debug__, system_bus ? "system" : "session");
        cdemud_error(CDEMUD_E_DBUSNAMEREQUEST, error);
        return FALSE;
    }

    dbus_g_connection_register_g_object(_priv->bus, "/CDEMUD_Daemon", G_OBJECT(self));

    /* Create desired number of devices */
    for (i = 0; i < _priv->number_of_devices; i++) {
        /* Create CDEmu device object */
        GObject *dev =  g_object_new(CDEMUD_TYPE_DEVICE, NULL);

        if (cdemud_device_initialize(CDEMUD_DEVICE(dev), i, _priv->ctl_device, audio_driver, NULL)) {
            /* Set parent */
            mirage_object_set_parent(MIRAGE_OBJECT(dev), G_OBJECT(self), NULL);
            /* Don't attach child... MIRAGE_Objects pass debug context to children,
               and CDEMUD_Devices have each its own context... */
            /* Add handling for signals from the device... this allows us to
               pass them on via DBUS */
            g_signal_connect(dev, "status-changed", (GCallback)__cdemud_daemon_device_status_changed_handler, self);
            g_signal_connect(dev, "option-changed", (GCallback)__cdemud_daemon_device_option_changed_handler, self);

            /* Add it to devices list */
            _priv->list_of_devices = g_list_append(_priv->list_of_devices, dev);
        } else {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to initialize device %i!\n", __debug__, i);
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
    *version = g_strdup(mirage_version_long);
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
        { "DAEMON_DEBUG_DEVICE", DAEMON_DEBUG_DEVICE },
        { "DAEMON_DEBUG_MMC", DAEMON_DEBUG_MMC },
        { "DAEMON_DEBUG_DELAY", DAEMON_DEBUG_DELAY },
        { "DAEMON_DEBUG_AUDIOPLAY", DAEMON_DEBUG_AUDIOPLAY },
        { "DAEMON_DEBUG_KERNEL_IO", DAEMON_DEBUG_KERNEL_IO },
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
    parser_entry = g_value_array_new(4);

    /* ID */
    g_value_array_append(parser_entry, NULL);
    g_value_init(g_value_array_get_nth(parser_entry, 0), G_TYPE_STRING);
    g_value_set_string(g_value_array_get_nth(parser_entry, 0), info->id);

    /* Name */
    g_value_array_append(parser_entry, NULL);
    g_value_init(g_value_array_get_nth(parser_entry, 1), G_TYPE_STRING);
    g_value_set_string(g_value_array_get_nth(parser_entry, 1), info->name);

    /* Description */
    g_value_array_append(parser_entry, NULL);
    g_value_init(g_value_array_get_nth(parser_entry, 2), G_TYPE_STRING);
    g_value_set_string(g_value_array_get_nth(parser_entry, 2), info->description);

    /* MIME type */
    g_value_array_append(parser_entry, NULL);
    g_value_init(g_value_array_get_nth(parser_entry, 3), G_TYPE_STRING);
    g_value_set_string(g_value_array_get_nth(parser_entry, 3), info->mime_type);

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
    fragment_entry = g_value_array_new(2);

    /* ID */
    g_value_array_append(fragment_entry, NULL);
    g_value_init(g_value_array_get_nth(fragment_entry, 0), G_TYPE_STRING);
    g_value_set_string(g_value_array_get_nth(fragment_entry, 0), info->id);

    /* Name */
    g_value_array_append(fragment_entry, NULL);
    g_value_init(g_value_array_get_nth(fragment_entry, 1), G_TYPE_STRING);
    g_value_set_string(g_value_array_get_nth(fragment_entry, 1), info->name);

    /* Add fragment's array to fragments' pointer array */
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
    GObject *dev = __cdemud_daemon_get_device(self, device_number, error);

    if (!dev) {
        return FALSE;
    }

    return cdemud_device_get_status(CDEMUD_DEVICE(dev), loaded, file_names, error);
}

gboolean cdemud_daemon_device_get_mapping (CDEMUD_Daemon *self, gint device_number, gchar **sr_device, gchar **sg_device, GError **error) {
    /*CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);*/
    GObject *dev = __cdemud_daemon_get_device(self, device_number, error);

    if (!dev) {
        return FALSE;
    }

    return cdemud_device_get_mapping(CDEMUD_DEVICE(dev), sr_device, sg_device, error);
}

gboolean cdemud_daemon_device_load (CDEMUD_Daemon *self, gint device_number, gchar **file_names, GHashTable *parameters, GError **error) {
    /*CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);*/
    GObject *dev = __cdemud_daemon_get_device(self, device_number, error);

    if (!dev) {
        return FALSE;
    }

    return cdemud_device_load_disc(CDEMUD_DEVICE(dev), file_names, parameters, error);
}

gboolean cdemud_daemon_device_unload (CDEMUD_Daemon *self, gint device_number, GError **error) {
    /*CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);*/
    GObject *dev = __cdemud_daemon_get_device(self, device_number, error);

    if (!dev) {
        return FALSE;
    }

    return cdemud_device_unload_disc(CDEMUD_DEVICE(dev), error);
}

gboolean cdemud_daemon_device_get_option (CDEMUD_Daemon *self, gint device_number, gchar *option_name, GPtrArray **option_values, GError **error) {
    /*CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);*/
    GObject *dev = __cdemud_daemon_get_device(self, device_number, error);

    if (!dev) {
        return FALSE;
    }

    return cdemud_device_get_option(CDEMUD_DEVICE(dev), option_name, option_values, error);
}

gboolean cdemud_daemon_device_set_option (CDEMUD_Daemon *self, gint device_number, gchar *option_name, GPtrArray *option_values, GError **error) {
    /*CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);*/
    GObject *dev = __cdemud_daemon_get_device(self, device_number, error);

    if (!dev) {
        return FALSE;
    }

    return cdemud_device_set_option(CDEMUD_DEVICE(dev), option_name, option_values, error);
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
