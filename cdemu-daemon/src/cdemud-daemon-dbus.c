/*
 *  CDEmuD: Daemon object - D-Bus
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

#include "cdemud.h"
#include "cdemud-daemon-private.h"

#define __debug__ "Daemon: D-Bus"


/* Daemon's name on D-Bus */
#define CDEMUD_DBUS_NAME "net.sf.cdemu.CDEMUD_Daemon"

#define DBUS_ERROR_CDEMUD "net.sf.cdemu.CDEMUD_Daemon.CDEmuDaemon"
#define DBUS_ERROR_LIBMIRAGE "net.sf.cdemu.CDEMUD_Daemon.libMirage"

static const gchar introspection_xml[];


/**********************************************************************\
 *                    D-Bus error registration                        *
\**********************************************************************/
static void register_error_domain (const gchar *prefix, GType code_enum)
{
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


/**********************************************************************\
 *                     D-Bus interface implementation                 *
\**********************************************************************/
/* Helper that encodes the list of masks */
static GVariantBuilder *encode_masks (const MIRAGE_DebugMask *masks, gint num_masks)
{
    GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("a(si)"));
    gint i;
    for (i = 0; i < num_masks; i++) {
        g_variant_builder_add(builder, "(si)", masks[i].name, masks[i].value);
    }
    return builder;
}


/* Helper that encodes the list of supported parsers */
static gboolean append_parser_to_builder (MIRAGE_ParserInfo *parser, GVariantBuilder *builder)
{
    g_variant_builder_add(builder, "(ssss)", parser->id, parser->name, parser->description, parser->mime_type);
    return TRUE;
}

static GVariantBuilder *encode_parsers ()
{
    GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("a(ssss)"));
    libmirage_for_each_parser((MIRAGE_CallbackFunction)append_parser_to_builder, builder, NULL);
    return builder;
}

/* Helper that encodes the list of supported fragments */
static gboolean append_fragment_to_builder (MIRAGE_FragmentInfo *fragment, GVariantBuilder *builder)
{
    g_variant_builder_add(builder, "(ss)", fragment->id, fragment->name);
    return TRUE;
}

static GVariantBuilder *encode_fragments ()
{
    GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("a(ss)"));
    libmirage_for_each_fragment((MIRAGE_CallbackFunction)append_fragment_to_builder, builder, NULL);
    return builder;
}

/* D-Bus method handler */
static void cdemud_daemon_dbus_handle_method_call (GDBusConnection *connection G_GNUC_UNUSED, const gchar *sender G_GNUC_UNUSED, const gchar *object_path G_GNUC_UNUSED, const gchar *interface_name G_GNUC_UNUSED, const gchar *method_name, GVariant *parameters, GDBusMethodInvocation *invocation, CDEMUD_Daemon *self)
{
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
        device = cdemud_daemon_get_device(self, device_number, &error);
        if (device) {
            succeeded = cdemud_device_load_disc(CDEMUD_DEVICE(device), filenames, options, &error);
        }

        g_object_unref(device);
        g_strfreev(filenames);
    } else if (!g_strcmp0(method_name, "DeviceUnload")) {
        /* *** DeviceUnload *** */
        gint device_number;
        GObject *device;

        g_variant_get(parameters, "(i)", &device_number);
        device = cdemud_daemon_get_device(self, device_number, &error);
        if (device) {
            succeeded = cdemud_device_unload_disc(CDEMUD_DEVICE(device), &error);
        }

        g_object_unref(device);
    } else if (!g_strcmp0(method_name, "DeviceGetStatus")) {
        /* *** DeviceGetStatus *** */
        gint device_number;
        GObject *device;

        g_variant_get(parameters, "(i)", &device_number);
        device = cdemud_daemon_get_device(self, device_number, &error);
        if (device) {
            gboolean loaded;
            gchar **file_names;

            loaded = cdemud_device_get_status(CDEMUD_DEVICE(device), &file_names);
            ret = g_variant_new("(b^as)", loaded, file_names);
            g_strfreev(file_names);

            succeeded = TRUE;
        }

        g_object_unref(device);
    } else if (!g_strcmp0(method_name, "DeviceSetOption")) {
        /* *** DeviceSetOption *** */
        gint device_number;
        gchar *option_name;
        GVariant *option_value;
        GObject *device;

        g_variant_get(parameters, "(isv)", &device_number, &option_name, &option_value);
        device = cdemud_daemon_get_device(self, device_number, &error);
        if (device) {
            succeeded = cdemud_device_set_option(CDEMUD_DEVICE(device), option_name, option_value, &error);
        }

        g_object_unref(device);
        g_free(option_name);
        g_variant_unref(option_value);
    } else if (!g_strcmp0(method_name, "DeviceGetOption")) {
        /* *** DeviceGetOption *** */
        gint device_number;
        gchar *option_name;
        GObject *device;

        g_variant_get(parameters, "(is)", &device_number, &option_name);
        device = cdemud_daemon_get_device(self, device_number, &error);
        if (device) {
            GVariant *option_value = cdemud_device_get_option(CDEMUD_DEVICE(device), option_name, &error);
            if (option_value) {
                ret = g_variant_new("(v)", option_value);
                succeeded = TRUE;
            }
        }

        g_object_unref(device);
        g_free(option_name);
    } else if (!g_strcmp0(method_name, "GetNumberOfDevices")) {
        /* *** GetNumberOfDevices *** */
        ret = g_variant_new("(i)", self->priv->number_of_devices);
        succeeded = TRUE;
    } else if (!g_strcmp0(method_name, "DeviceGetMapping")) {
        /* *** DeviceGetMapping *** */
        gint device_number;
        GObject *device;

        g_variant_get(parameters, "(i)", &device_number);
        device = cdemud_daemon_get_device(self, device_number, &error);
        if (device) {
            gchar *sr_device, *sg_device;

            cdemud_device_get_mapping(CDEMUD_DEVICE(device), &sr_device, &sg_device);
            ret = g_variant_new("(ss)", sr_device ? sr_device : "", sg_device ? sg_device : "");
            succeeded = TRUE;

            g_free(sr_device);
            g_free(sg_device);
        }

        g_object_unref(device);
    } else if (!g_strcmp0(method_name, "GetDaemonInterfaceVersion")) {
        /* *** GetDaemonInterfaceVersion *** */
        ret = g_variant_new("(i)", DAEMON_INTERFACE_VERSION);
        succeeded = TRUE;
    } else if (!g_strcmp0(method_name, "GetDaemonVersion")) {
        /* *** GetDaemonVersion *** */
        ret = g_variant_new("(s)", self->priv->version);
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

        ret = g_variant_new("(a(si))", encode_masks(dbg_masks, G_N_ELEMENTS(dbg_masks)));
        succeeded = TRUE;
    } else if (!g_strcmp0(method_name, "EnumLibraryDebugMasks")) {
        /* *** EnumLibraryDebugMasks *** */
        const MIRAGE_DebugMask *dbg_masks;
        gint num_dbg_masks;

        succeeded = libmirage_get_supported_debug_masks(&dbg_masks, &num_dbg_masks, &error);
        if (succeeded) {
            ret = g_variant_new("(a(si))", encode_masks(dbg_masks, num_dbg_masks));
        }
    } else if (!g_strcmp0(method_name, "EnumSupportedParsers")) {
        /* *** EnumSupportedParsers *** */
        ret = g_variant_new("(a(ssss))", encode_parsers());
        succeeded = TRUE;
    } else if (!g_strcmp0(method_name, "EnumSupportedFragments")) {
        /* *** EnumSupportedFragments *** */
        ret = g_variant_new("(a(ss))", encode_fragments());
        succeeded = TRUE;
    } else {
        g_set_error(&error, CDEMUD_ERROR, CDEMUD_ERROR_INVALID_ARGUMENT, "Invalid method name '%s'!", method_name);
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

/* Interface VTable */
static const GDBusInterfaceVTable dbus_interface_vtable = {
    (GDBusInterfaceMethodCallFunc)cdemud_daemon_dbus_handle_method_call,
    NULL,
    NULL,
    { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL }
};


static void on_bus_acquired (GDBusConnection *connection, const gchar *name G_GNUC_UNUSED, CDEMUD_Daemon *self)
{
    /* Create introspection data from our embedded xml */
    GDBusNodeInfo *introspection_data;

    /* Store connection */
    self->priv->connection = connection;

    /* Register D-Bus error domains */
    register_error_domain(DBUS_ERROR_CDEMUD, CDEMUD_TYPE_ERROR);
    register_error_domain(DBUS_ERROR_LIBMIRAGE, MIRAGE_TYPE_ERROR);

    /* Register object */
    introspection_data = g_dbus_node_info_new_for_xml(introspection_xml, NULL);

    g_dbus_connection_register_object(
        connection,
        "/CDEMUD_Daemon",
        introspection_data->interfaces[0],
        &dbus_interface_vtable,
        self,
        NULL,
        NULL
    );

    g_dbus_node_info_unref(introspection_data);
}


static void on_name_lost (GDBusConnection *connection G_GNUC_UNUSED, const gchar *name G_GNUC_UNUSED, CDEMUD_Daemon *self)
{
    cdemud_daemon_stop_daemon(self);
}


/**********************************************************************\
 *                    Daemon's D-Bus functions                        *
\**********************************************************************/
gboolean cdemud_daemon_dbus_check_if_name_is_available (CDEMUD_Daemon *self, GBusType bus_type)
{
    GDBusProxy *dbus_proxy;
    GError *dbus_error = NULL;
    GVariant *dbus_reply;
    gboolean name_taken;

    dbus_proxy = g_dbus_proxy_new_for_bus_sync(
        bus_type,
        G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS | G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
        NULL,
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        NULL,
        &dbus_error
    );

    if (!dbus_proxy) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to get proxy for 'org.freedesktop.DBus' on %s bus: %s!\n", __debug__, bus_type == G_BUS_TYPE_SYSTEM ? "system" : "session", dbus_error->message);
        g_error_free(dbus_error);
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
        CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to check if name '%s' is already taken on %s bus!\n", __debug__, CDEMUD_DBUS_NAME, bus_type == G_BUS_TYPE_SYSTEM ? "system" : "session");
        g_error_free(dbus_error);
        g_object_unref(dbus_proxy);
        return FALSE;
    }

    g_variant_get(dbus_reply, "(b)", &name_taken);

    g_variant_unref(dbus_reply);
    g_object_unref(dbus_proxy);

    if (name_taken) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: name '%s' is already taken on %s bus! Is there another instance already running?\n", __debug__, CDEMUD_DBUS_NAME, bus_type == G_BUS_TYPE_SYSTEM ? "system" : "session");
        return FALSE;
    }

    return TRUE;
}

void cdemud_daemon_dbus_register_on_bus (CDEMUD_Daemon *self, GBusType bus_type)
{
    /* Claim name on D-BUS */
    self->priv->owner_id = g_bus_own_name(
        bus_type,
        CDEMUD_DBUS_NAME,
        G_BUS_NAME_OWNER_FLAGS_NONE,
        (GBusAcquiredCallback)on_bus_acquired,
        NULL,
        (GBusNameLostCallback)on_name_lost,
        self,
        NULL
    );
}

void cdemud_daemon_dbus_cleanup (CDEMUD_Daemon *self)
{
    /* Release D-Bus name */
    g_bus_unown_name(self->priv->owner_id);
}


/**********************************************************************\
 *                      D-Bus Signal emission                         *
\**********************************************************************/
void cdemud_daemon_dbus_emit_device_status_changed (CDEMUD_Daemon *self, gint number)
{
    if (self->priv->connection) {
        g_dbus_connection_emit_signal(self->priv->connection, NULL,
            "/CDEMUD_Daemon", "net.sf.cdemu.CDEMUD_Daemon",
            "DeviceStatusChanged", g_variant_new("(i)", number),
            NULL);
    }
}

void cdemud_daemon_dbus_emit_device_option_changed (CDEMUD_Daemon *self, gint number, const gchar *option)
{
    if (self->priv->connection) {
        g_dbus_connection_emit_signal(self->priv->connection, NULL,
            "/CDEMUD_Daemon", "net.sf.cdemu.CDEMUD_Daemon",
            "DeviceOptionChanged", g_variant_new("(is)", number, option),
            NULL);
    }
}

void cdemud_daemon_dbus_emit_device_mappings_ready (CDEMUD_Daemon *self)
{
    if (self->priv->connection) {
        g_dbus_connection_emit_signal(self->priv->connection, NULL,
            "/CDEMUD_Daemon", "net.sf.cdemu.CDEMUD_Daemon",
            "DeviceMappingsReady", g_variant_new("()"),
            NULL);
    }
}


/**********************************************************************\
 *                   Embedded introspection data                      *
\**********************************************************************/
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
