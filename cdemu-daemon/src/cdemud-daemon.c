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


#define __debug__ "Daemon"


/* Daemon's name on D-Bus */
#define CDEMUD_DBUS_NAME "net.sf.cdemu.CDEMUD_Daemon"

#define DBUS_ERROR_CDEMUD "net.sf.cdemu.CDEMUD_Daemon.CDEmuDaemon"
#define DBUS_ERROR_LIBMIRAGE "net.sf.cdemu.CDEMUD_Daemon.libMirage"

/* Introspection data for the service we are exporting */
static const gchar introspection_xml[] = 
    "<node>"
    "    <interface name='net.sf.cdemu.CDEMUD_Daemon'>"
    "        <!-- Information-related methods -->"
    "        <method name='GetDaemonVersion'>"
    "            <arg name='version' type='s' direction='out'/>"
    "        </method>"
    "        <method name='GetLibraryVersion'>"
    "            <arg name='version' type='s' direction='out'/>"
    "        </method>"
    "        <method name='GetDaemonInterfaceVersion'>"
    "            <arg name='version' type='i' direction='out'/>"
    "        </method>"
    "        <method name='EnumDaemonDebugMasks'>"
    "            <arg name='masks' type='a(si)' direction='out'/>"
    "        </method>"
    "        <method name='EnumLibraryDebugMasks'>"
    "            <arg name='masks' type='a(si)' direction='out'/>"
    "        </method>"
    "        <method name='EnumSupportedParsers'>"
    "            <arg name='parsers' type='a(ssss)' direction='out'/>"
    "        </method>"
    "        <method name='EnumSupportedFragments'>"
    "            <arg name='fragments' type='a(ss)' direction='out'/>"
    "        </method>"

    "        <!-- Device control methods -->"
    "        <method name='GetNumberOfDevices'>"
    "            <arg name='number_of_devices' type='i' direction='out'/>"
    "        </method>"
    "        <method name='DeviceGetMapping'>"
    "            <arg name='device_number' type='i' direction='in'/>"
    "            <arg name='sr_device' type='s' direction='out'/>"
    "            <arg name='sg_device' type='s' direction='out'/>"
    "        </method>"
    "        <method name='DeviceGetStatus'>"
    "            <arg name='device_number' type='i' direction='in'/>"
    "            <arg name='loaded' type='b' direction='out'/>"
    "            <arg name='file_names' type='as' direction='out'/>"
    "        </method>"
    "        <method name='DeviceLoad'>"
    "            <arg name='device_number' type='i' direction='in'/>"
    "            <arg name='file_names' type='as' direction='in'/>"
    "            <arg name='parameters' type='a{sv}' direction='in'/>"
    "        </method>"
    "        <method name='DeviceUnload'>"
    "            <arg name='device_number' type='i' direction='in'/>"
    "        </method>"
    "        <method name='DeviceGetOption'>"
    "            <arg name='device_number' type='i' direction='in'/>"
    "            <arg name='option_name' type='s' direction='in'/>"
    "            <arg name='option_values' type='v' direction='out'/>"
    "        </method>"
    "        <method name='DeviceSetOption'>"
    "            <arg name='device_number' type='i' direction='in'/>"
    "            <arg name='option_name' type='s' direction='in'/>"
    "            <arg name='option_values' type='v' direction='in'/>"
    "        </method>"

    "        <!-- Notification signals -->"
    "        <signal name='DeviceStatusChanged'>"
    "            <arg name='device_number' type='i' direction='out'/>"
    "        </signal>"
    "        <signal name='DeviceOptionChanged'>"
    "            <arg name='device_number' type='i' direction='out'/>"
    "            <arg name='option' type='s' direction='out'/>"
    "        </signal>"
    "        <signal name='DeviceMappingsReady'/>"
    "    </interface>"
    "</node>";
    

/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define CDEMUD_DAEMON_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), CDEMUD_TYPE_DAEMON, CDEMUD_DaemonPrivate))

typedef struct {
    gchar *version;
    gchar *audio_backend;

    GMainLoop *main_loop;

    /* Control device */
    gchar *ctl_device;

    /* Devices */
    gint number_of_devices;
    GList *list_of_devices;

    /* Device mapping */
    guint mapping_id;
    gint mapping_attempt;

    /* D-Bus */
    GDBusConnection *connection;
    guint owner_id;
    GDBusNodeInfo *introspection_data;
} CDEMUD_DaemonPrivate;

/* D-Bus interface */
static void __cdemud_daemon_dbus_handle_method_call (GDBusConnection *connection, const gchar *sender, const gchar *object_path, const gchar *interface_name, const gchar *method_name, GVariant *parameters, GDBusMethodInvocation *invocation, gpointer user_data);

static const GDBusInterfaceVTable dbus_interface_vtable = {
    __cdemud_daemon_dbus_handle_method_call,
    NULL,
    NULL,
    { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL }
};


/******************************************************************************\
 *                              Private functions                             *
\******************************************************************************/
static void __cdemud_daemon_device_status_changed_handler (GObject *device, CDEMUD_Daemon *self) {
    CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);
    
    if (_priv->connection) {
        /* Get device number */  
        gint number;
        cdemud_device_get_device_number(CDEMUD_DEVICE(device), &number, NULL);

        /* Emit signal on D-Bus */
        g_dbus_connection_emit_signal(
            _priv->connection,
            NULL,
            "/CDEMUD_Daemon",
            "net.sf.cdemu.CDEMUD_Daemon",
            "DeviceStatusChanged",
            g_variant_new("(i)", number),
            NULL
        );
    }

    return;
}

static void __cdemud_daemon_device_option_changed_handler (GObject *device, gchar *option, CDEMUD_Daemon *self) {
    CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);

    if (_priv->connection) {
        /* Get device number */  
        gint number;
        cdemud_device_get_device_number(CDEMUD_DEVICE(device), &number, NULL);

        /* Emit signal on D-Bus */
        g_dbus_connection_emit_signal(
            _priv->connection,
            NULL,
            "/CDEMUD_Daemon",
            "net.sf.cdemu.CDEMUD_Daemon",
            "DeviceOptionChanged",
            g_variant_new("(is)", number, option),
            NULL
        );
    }

    return;
}


static gboolean __cdemud_daemon_destroy_devices (CDEMUD_Daemon *self, GError **error G_GNUC_UNUSED) {
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

    /* If we're done here, it's time to send the "DeviceMappingsReady" signal */
    if (!run_again) {
        if (_priv->connection) {
            /* Emit signal on D-Bus */
            g_dbus_connection_emit_signal(
                _priv->connection,
                NULL,
                "/CDEMUD_Daemon",
                "net.sf.cdemu.CDEMUD_Daemon",
                "DeviceMappingsReady",
                g_variant_new("()"),
                NULL
            );
        }
    }

    return run_again;
}

static void __register_error_domain (const gchar *prefix, GType code_enum) {
    GEnumClass *klass = g_type_class_ref(code_enum);
    gint i;

    volatile gsize quark = 0;

    gint num_entries = klass->n_values;
    GDBusErrorEntry *entries = g_new0(GDBusErrorEntry, num_entries);

    /* Map from the GEnum to GDBusErrorEntry */
    for (i = 0; i < num_entries; i++) {
        entries[i].error_code = klass->values[i].value;
        entries[i].dbus_error_name = g_strdup_printf("%s.%s", prefix, klass->values[i].value_nick);
    }
    
    /* Register the domain */
    g_dbus_error_register_error_domain(prefix, &quark, entries, num_entries);

    /* Cleanup */
    for (i = 0; i < num_entries; i++) {
        g_free((gchar *)entries[i].dbus_error_name);
    }
    g_free(entries);

    g_type_class_unref(klass);
}

static void __cdemud_daemon_on_bus_acquired (GDBusConnection *connection, const gchar *name G_GNUC_UNUSED, gpointer user_data) {
    CDEMUD_Daemon *self = CDEMUD_DAEMON(user_data);
    CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);

    /* Store connection */
    _priv->connection = connection;

    /* Register object */
    g_dbus_connection_register_object(
        connection,
        "/CDEMUD_Daemon",
        _priv->introspection_data->interfaces[0],
        &dbus_interface_vtable,
        self,
        NULL,
        NULL
    );
}


static void __cdemud_daemon_on_name_lost (GDBusConnection *connection G_GNUC_UNUSED, const gchar *name G_GNUC_UNUSED, gpointer user_data) {
    CDEMUD_Daemon *self = CDEMUD_DAEMON(user_data);
    cdemud_daemon_stop_daemon(self, NULL);    
}


/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
gboolean cdemud_daemon_initialize_and_start (CDEMUD_Daemon *self, gint num_devices, gchar *ctl_device, gchar *audio_driver, gboolean system_bus, GError **error) {
    CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);
    GObject *debug_context;

    GDBusProxy *dbus_proxy;
    GError *dbus_error = NULL;
    GVariant *dbus_reply;
    gboolean name_taken;

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

    /* Register D-Bus error domains */
    __register_error_domain(DBUS_ERROR_CDEMUD, CDEMUD_TYPE_ERROR);
    __register_error_domain(DBUS_ERROR_LIBMIRAGE, MIRAGE_TYPE_ERROR);

    /* Make sure the D-BUS name we're going to use isn't taken already (by another
       instance of the server). We're actually going to claim it once we create
       the devices, but we want to avoid the device creation if name claim is
       already doomed to fail... */
    dbus_proxy = g_dbus_proxy_new_for_bus_sync(
        system_bus ? G_BUS_TYPE_SYSTEM : G_BUS_TYPE_SESSION,
        G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS | G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
        NULL,
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        NULL,
        &dbus_error
    );

    if (!dbus_proxy) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to get proxy for 'org.freedesktop.DBus' on %s bus: %s!\n", __debug__, system_bus ? "system" : "session", dbus_error->message);
        g_error_free(dbus_error);
        cdemud_error(CDEMUD_E_DBUSCONNECT, error);
        return FALSE;
    }

    dbus_reply = g_dbus_proxy_call_sync(
        dbus_proxy,
        "NameHasOwner",
        g_variant_new("(s)", CDEMUD_DBUS_NAME),
        G_DBUS_CALL_FLAGS_NO_AUTO_START,
        -1,
        NULL,
        &dbus_error
    );

    if (!dbus_reply) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to check if name '%s' is already taken on %s bus!\n", __debug__, CDEMUD_DBUS_NAME, system_bus ? "system" : "session");
        g_error_free(dbus_error);
        cdemud_error(CDEMUD_E_DBUSCONNECT, error);
        g_object_unref(dbus_proxy);
        return FALSE;
    }

    g_variant_get(dbus_reply, "(b)", &name_taken);

    g_variant_unref(dbus_reply);
    g_object_unref(dbus_proxy);

    if (name_taken) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: name '%s' is already taken on %s bus! Is there another instance already running?\n", __debug__, CDEMUD_DBUS_NAME, system_bus ? "system" : "session");
        cdemud_error(CDEMUD_E_DBUSNAMEREQUEST, error);
        return FALSE;
    }


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


    /* Create introspection data from our embedded xml */
    _priv->introspection_data = g_dbus_node_info_new_for_xml(introspection_xml, NULL);
    
    /* Claim name on D-BUS */
    _priv->owner_id = g_bus_own_name(
        system_bus ? G_BUS_TYPE_SYSTEM : G_BUS_TYPE_SESSION,
        CDEMUD_DBUS_NAME,
        G_BUS_NAME_OWNER_FLAGS_NONE,
        __cdemud_daemon_on_bus_acquired,
        NULL,
        __cdemud_daemon_on_name_lost,
        self,
        NULL
    );


    /* Run the main loop */
    g_main_loop_run(_priv->main_loop);


    /* Release D-Bus name */
    g_bus_unown_name(_priv->owner_id);

    /* Release introspection data */
    g_dbus_node_info_unref(_priv->introspection_data);

    return TRUE;
}

gboolean cdemud_daemon_stop_daemon (CDEMUD_Daemon *self, GError **error G_GNUC_UNUSED) {
    CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);

    /* Stop the main loop */
    g_main_loop_quit(_priv->main_loop);

    return TRUE;
}


/******************************************************************************\
 *                           D-BUS interface functions                        *
\******************************************************************************/
static GVariantBuilder *__encode_masks (const MIRAGE_DebugMask *masks, gint num_masks) {
    GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("a(si)"));
    gint i;
    for (i = 0; i < num_masks; i++) {
        g_variant_builder_add(builder, "(si)", masks[i].name, masks[i].value);
    }
    return builder;
}


static gboolean __add_parser (gpointer data, gpointer user_data) {
    MIRAGE_ParserInfo* info = data;
    GVariantBuilder *builder = user_data;
    g_variant_builder_add(builder, "(ssss)", info->id, info->name, info->description, info->mime_type);
    return TRUE;
}

static GVariantBuilder *__encode_parsers () {
    GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("a(ssss)"));
    libmirage_for_each_parser(__add_parser, builder, NULL);
    return builder;
}


static gboolean __add_fragment (gpointer data, gpointer user_data) {
    MIRAGE_FragmentInfo* info = data;
    GVariantBuilder *builder = user_data;
    g_variant_builder_add(builder, "(ss)", info->id, info->name);
    return TRUE;
}

static GVariantBuilder *__encode_fragments () {
    GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("a(ss)"));
    libmirage_for_each_fragment(__add_fragment, builder, NULL);
    return builder;
}


static void __cdemud_daemon_dbus_handle_method_call (GDBusConnection *connection G_GNUC_UNUSED, const gchar *sender G_GNUC_UNUSED, const gchar *object_path G_GNUC_UNUSED, const gchar *interface_name G_GNUC_UNUSED, const gchar *method_name, GVariant *parameters, GDBusMethodInvocation *invocation, gpointer user_data) {
    CDEMUD_Daemon *self = CDEMUD_DAEMON(user_data);
    CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);

    GError *error = NULL;
    gboolean succeeded = FALSE;
    GVariant *ret = NULL;

    if (!g_strcmp0(method_name, "DeviceLoad")) {
        /* *** DeviceLoad *** */
        gint device_number;
        gchar **filenames;
        GVariant *options;
        
        GObject *device;

        g_variant_get(parameters, "(i^as@a{sv})", &device_number, &filenames, &options);
        device = __cdemud_daemon_get_device(self, device_number, &error);
        if (device) {
            succeeded = cdemud_device_load_disc(CDEMUD_DEVICE(device), filenames, options, &error);
        }
        g_strfreev(filenames);
        g_variant_unref(parameters);
    } else if (!g_strcmp0(method_name, "DeviceUnload")) {
        /* *** DeviceUnload *** */
        gint device_number;
        GObject *device;

        g_variant_get(parameters, "(i)", &device_number);
        device = __cdemud_daemon_get_device(self, device_number, &error);
        if (device) {
            succeeded = cdemud_device_unload_disc(CDEMUD_DEVICE(device), &error);
        }
    } else if (!g_strcmp0(method_name, "DeviceGetStatus")) {
        /* *** DeviceGetStatus *** */
        gint device_number;
        GObject *device;

        g_variant_get(parameters, "(i)", &device_number);
        device = __cdemud_daemon_get_device(self, device_number, &error);
        if (device) {
            gboolean loaded;
            gchar **file_names;

            succeeded = cdemud_device_get_status(CDEMUD_DEVICE(device), &loaded, &file_names, &error);
            if (succeeded) {
                ret = g_variant_new("(b^as)", loaded, file_names);
                g_strfreev(file_names);
            }
        }
    } else if (!g_strcmp0(method_name, "DeviceSetOption")) {
        /* *** DeviceSetOption *** */
        gint device_number;
        gchar *option_name;
        GVariant *option_value;
        GObject *device;

        g_variant_get(parameters, "(isv)", &device_number, &option_name, &option_value);
        device = __cdemud_daemon_get_device(self, device_number, &error);
        if (device) {
            succeeded = cdemud_device_set_option(CDEMUD_DEVICE(device), option_name, option_value, &error);
        }
        
        g_free(option_name);
        g_variant_unref(option_value);        
    } else if (!g_strcmp0(method_name, "DeviceGetOption")) {
        /* *** DeviceGetOption *** */
        gint device_number;
        gchar *option_name;
        GObject *device;

        g_variant_get(parameters, "(is)", &device_number, &option_name);
        device = __cdemud_daemon_get_device(self, device_number, &error);
        if (device) {
            GVariant *option_value;
            succeeded = cdemud_device_get_option(CDEMUD_DEVICE(device), option_name, &option_value, &error);
            if (succeeded) {
                ret = g_variant_new("(v)", option_value);
            }
        }
        
        g_free(option_name);
    } else if (!g_strcmp0(method_name, "GetNumberOfDevices")) {
        /* *** GetNumberOfDevices *** */
        ret = g_variant_new("(i)", _priv->number_of_devices);
        succeeded = TRUE;
    } else if (!g_strcmp0(method_name, "DeviceGetMapping")) {
        /* *** DeviceGetMapping *** */
        gint device_number;
        GObject *device;

        g_variant_get(parameters, "(i)", &device_number);
        device = __cdemud_daemon_get_device(self, device_number, &error);
        if (device) {
            gchar *sr_device, *sg_device;
            
            succeeded = cdemud_device_get_mapping(CDEMUD_DEVICE(device), &sr_device, &sg_device, &error);
            if (succeeded) {
                ret = g_variant_new("(ss)", sr_device ? sr_device : "", sg_device ? sg_device : "");
                g_free(sr_device);
                g_free(sg_device);
            }
        }
    } else if (!g_strcmp0(method_name, "GetDaemonInterfaceVersion")) {
        /* *** GetDaemonInterfaceVersion *** */
        ret = g_variant_new("(i)", DAEMON_INTERFACE_VERSION);
        succeeded = TRUE;        
    } else if (!g_strcmp0(method_name, "GetDaemonVersion")) {
        /* *** GetDaemonVersion *** */
        ret = g_variant_new("(s)", _priv->version);
        succeeded = TRUE;
    } else if (!g_strcmp0(method_name, "GetLibraryVersion")) {
        /* *** GetLibraryVersion *** */
        ret = g_variant_new("(s)", mirage_version_long);
        succeeded = TRUE;
    } else if (!g_strcmp0(method_name, "EnumDaemonDebugMasks")) {
        /* *** EnumDaemonDebugMasks *** */
        static const MIRAGE_DebugMask dbg_masks[] = {
            { "DAEMON_DEBUG_DEVICE", DAEMON_DEBUG_DEVICE },
            { "DAEMON_DEBUG_MMC", DAEMON_DEBUG_MMC },
            { "DAEMON_DEBUG_DELAY", DAEMON_DEBUG_DELAY },
            { "DAEMON_DEBUG_AUDIOPLAY", DAEMON_DEBUG_AUDIOPLAY },
            { "DAEMON_DEBUG_KERNEL_IO", DAEMON_DEBUG_KERNEL_IO },
        };

        ret = g_variant_new("(a(si))", __encode_masks(dbg_masks, G_N_ELEMENTS(dbg_masks)));
        succeeded = TRUE;
    } else if (!g_strcmp0(method_name, "EnumLibraryDebugMasks")) {
        /* *** EnumLibraryDebugMasks *** */
        const MIRAGE_DebugMask *dbg_masks;
        gint num_dbg_masks;

        succeeded = libmirage_get_supported_debug_masks(&dbg_masks, &num_dbg_masks, &error);
        if (succeeded) {
            ret = g_variant_new("(a(si))", __encode_masks(dbg_masks, num_dbg_masks));
        }
    } else if (!g_strcmp0(method_name, "EnumSupportedParsers")) {
        /* *** EnumSupportedParsers *** */
        ret = g_variant_new("(a(ssss))", __encode_parsers());
        succeeded = TRUE;
    } else if (!g_strcmp0(method_name, "EnumSupportedFragments")) {
        /* *** EnumSupportedFragments *** */
        ret = g_variant_new("(a(ss))", __encode_fragments());
        succeeded = TRUE;
    } else {
        cdemud_error(CDEMUD_E_GENERIC, &error);
    }

    if (succeeded) {
        g_dbus_method_invocation_return_value(invocation, ret);
    } else {
        /* We need to map the code */
        if (error->domain == MIRAGE_ERROR) {
            error->domain = g_quark_from_string(DBUS_ERROR_LIBMIRAGE);
        } else if (error->domain == CDEMUD_ERROR) {
            error->domain = g_quark_from_string(DBUS_ERROR_CDEMUD);
        }
        g_dbus_method_invocation_return_gerror(invocation, error);
    }
}


/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static MIRAGE_ObjectClass *parent_class = NULL;

static void __cdemud_daemon_instance_init (GTypeInstance *instance, gpointer g_class G_GNUC_UNUSED) {
    CDEMUD_Daemon *self = CDEMUD_DAEMON(instance);
    CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);

    /* Set version string */
    _priv->version = g_strdup(PACKAGE_VERSION);

    /* D-Bus data */
    _priv->connection = NULL;
    _priv->introspection_data = NULL;
    _priv->owner_id = 0;

    return;
}

static void __cdemud_daemon_finalize (GObject *obj) {
    CDEMUD_Daemon *self = CDEMUD_DAEMON(obj);
    CDEMUD_DaemonPrivate *_priv = CDEMUD_DAEMON_GET_PRIVATE(self);

    /* Free main loop */
    g_main_loop_unref(_priv->main_loop);

    /* Destroy devices */
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

static void __cdemud_daemon_class_init (gpointer g_class, gpointer g_class_data G_GNUC_UNUSED) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(g_class);
    CDEMUD_DaemonClass *klass = CDEMUD_DAEMON_CLASS(g_class);

    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(CDEMUD_DaemonPrivate));

    /* Initialize GObject methods */
    gobject_class->finalize = __cdemud_daemon_finalize;

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
            __cdemud_daemon_instance_init,   /* instance_init */
            NULL    /* value_table */
        };

        type = g_type_register_static(MIRAGE_TYPE_OBJECT, "CDEMUD_Daemon", &info, 0);
    }

    return type;
}
