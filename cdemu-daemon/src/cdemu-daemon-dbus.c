/*
 *  CDEmu daemon: Daemon object - D-Bus
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

#include "cdemu.h"
#include "cdemu-daemon-private.h"

#define __debug__ "Daemon: D-Bus"


/* Daemon's name on D-Bus */
#define CDEMU_DAEMON_DBUS_NAME "net.sf.cdemu.CDEmuDaemon"

#define DBUS_ERROR_CDEMU "net.sf.cdemu.CDEmuDaemon.errorDaemon"
#define DBUS_ERROR_LIBMIRAGE "net.sf.cdemu.CDEmuDaemon.errorMirage"

static const gchar introspection_xml[];


/**********************************************************************\
 *                    D-Bus error registration                        *
\**********************************************************************/
static void register_error_domain (const gchar *prefix, GType code_enum)
{
    GEnumClass *klass = g_type_class_ref(code_enum);

    volatile gsize quark = 0;

    gint num_entries = klass->n_values;
    GDBusErrorEntry *entries = g_new0(GDBusErrorEntry, num_entries);

    /* Map from the GEnum to GDBusErrorEntry */
    for (gint i = 0; i < num_entries; i++) {
        entries[i].error_code = klass->values[i].value;
        entries[i].dbus_error_name = g_strdup_printf("%s.%s", prefix, klass->values[i].value_nick);
    }

    /* Register the domain */
    g_dbus_error_register_error_domain(prefix, &quark, entries, num_entries);

    /* Cleanup */
    for (gint i = 0; i < num_entries; i++) {
        g_free((gchar *)entries[i].dbus_error_name);
    }
    g_free(entries);

    g_type_class_unref(klass);
}


/**********************************************************************\
 *                     D-Bus interface implementation                 *
\**********************************************************************/
/* Helper that encodes the list of masks */
static GVariantBuilder *encode_masks (const MirageDebugMask *masks, gint num_masks)
{
    GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("a(si)"));

    for (gint i = 0; i < num_masks; i++) {
        g_variant_builder_add(builder, "(si)", masks[i].name, masks[i].value);
    }
    return builder;
}


/* Helper that encodes the list of supported parsers */
static gboolean append_parser_to_builder (MirageParserInfo *info, GVariantBuilder *builder)
{
    GVariantBuilder types_builder;
    GVariant *types;

    g_variant_builder_init(&types_builder, G_VARIANT_TYPE("a(ss)"));
    for (gint i = 0; i < info->num_types; i++) {
        g_variant_builder_add(&types_builder, "(ss)", info->description[i], info->mime_type[i]);
    }
    types = g_variant_builder_end(&types_builder);

    g_variant_builder_add(builder, "(ss@a(ss))", info->id, info->name, types);
    return TRUE;
}

static GVariantBuilder *encode_parsers ()
{
    GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("a(ssa(ss))"));
    mirage_enumerate_parsers((MirageEnumParserInfoCallback)append_parser_to_builder, builder, NULL);
    return builder;
}


/* Helper that encodes the list of supported file filters */
static gboolean append_filter_to_builder (MirageFileFilterInfo *info, GVariantBuilder *builder)
{
    GVariantBuilder types_builder;
    GVariant *types;

    g_variant_builder_init(&types_builder, G_VARIANT_TYPE("a(ss)"));
    for (gint i = 0; i < info->num_types; i++) {
        g_variant_builder_add(&types_builder, "(ss)", info->description[i], info->mime_type[i]);
    }
    types = g_variant_builder_end(&types_builder);

    g_variant_builder_add(builder, "(ss@a(ss))", info->id, info->name, types);
    return TRUE;
}

static GVariantBuilder *encode_file_filters ()
{
    GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("a(ssa(ss))"));
    mirage_enumerate_file_filters((MirageEnumFileFilterInfoCallback)append_filter_to_builder, builder, NULL);
    return builder;
}


/* Helper that encodes the list of supported fragments */
static gboolean append_fragment_to_builder (MirageFragmentInfo *info, GVariantBuilder *builder)
{
    g_variant_builder_add(builder, "(ss)", info->id, info->name);
    return TRUE;
}

static GVariantBuilder *encode_fragments ()
{
    GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("a(ss)"));
    mirage_enumerate_fragments((MirageEnumFragmentInfoCallback)append_fragment_to_builder, builder, NULL);
    return builder;
}

/* D-Bus method handler */
static void cdemu_daemon_dbus_handle_method_call (GDBusConnection *connection G_GNUC_UNUSED, const gchar *sender G_GNUC_UNUSED, const gchar *object_path G_GNUC_UNUSED, const gchar *interface_name G_GNUC_UNUSED, const gchar *method_name, GVariant *parameters, GDBusMethodInvocation *invocation, CdemuDaemon *self)
{
    GError *error = NULL;
    gboolean succeeded = FALSE;
    GVariant *ret = NULL;

    if (!g_strcmp0(method_name, "DeviceLoad")) {
        /* *** DeviceLoad *** */
        gint device_number;
        gchar **filenames;
        GVariant *options;

        CdemuDevice *device;

        g_variant_get(parameters, "(i^as@a{sv})", &device_number, &filenames, &options);
        device = cdemu_daemon_get_device(self, device_number, &error);
        if (device) {
            succeeded = cdemu_device_load_disc(device, filenames, options, &error);
        }

        g_object_unref(device);
        g_strfreev(filenames);
    } else if (!g_strcmp0(method_name, "DeviceUnload")) {
        /* *** DeviceUnload *** */
        gint device_number;
        CdemuDevice *device;

        g_variant_get(parameters, "(i)", &device_number);
        device = cdemu_daemon_get_device(self, device_number, &error);
        if (device) {
            succeeded = cdemu_device_unload_disc(device, &error);
        }

        g_object_unref(device);
    } else if (!g_strcmp0(method_name, "DeviceGetStatus")) {
        /* *** DeviceGetStatus *** */
        gint device_number;
        CdemuDevice *device;

        g_variant_get(parameters, "(i)", &device_number);
        device = cdemu_daemon_get_device(self, device_number, &error);
        if (device) {
            gboolean loaded;
            gchar **file_names;

            loaded = cdemu_device_get_status(device, &file_names);
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
        CdemuDevice *device;

        g_variant_get(parameters, "(isv)", &device_number, &option_name, &option_value);
        device = cdemu_daemon_get_device(self, device_number, &error);
        if (device) {
            succeeded = cdemu_device_set_option(device, option_name, option_value, &error);
        }

        g_object_unref(device);
        g_free(option_name);
        g_variant_unref(option_value);
    } else if (!g_strcmp0(method_name, "DeviceGetOption")) {
        /* *** DeviceGetOption *** */
        gint device_number;
        gchar *option_name;
        CdemuDevice *device;

        g_variant_get(parameters, "(is)", &device_number, &option_name);
        device = cdemu_daemon_get_device(self, device_number, &error);
        if (device) {
            GVariant *option_value = cdemu_device_get_option(device, option_name, &error);
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
        CdemuDevice *device;

        g_variant_get(parameters, "(i)", &device_number);
        device = cdemu_daemon_get_device(self, device_number, &error);
        if (device) {
            gchar *sr_device, *sg_device;

            cdemu_device_get_mapping(device, &sr_device, &sg_device);
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
        static const MirageDebugMask dbg_masks[] = {
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
        const MirageDebugMask *dbg_masks;
        gint num_dbg_masks;

        succeeded = mirage_get_supported_debug_masks(&dbg_masks, &num_dbg_masks, &error);
        if (succeeded) {
            ret = g_variant_new("(a(si))", encode_masks(dbg_masks, num_dbg_masks));
        }
    } else if (!g_strcmp0(method_name, "EnumSupportedParsers")) {
        /* *** EnumSupportedParsers *** */
        ret = g_variant_new("(a(ssa(ss)))", encode_parsers());
        succeeded = TRUE;
    } else if (!g_strcmp0(method_name, "EnumSupportedFileFilters")) {
        /* *** EnumSupportedFileFilters *** */
        ret = g_variant_new("(a(ssa(ss)))", encode_file_filters());
        succeeded = TRUE;
    } else if (!g_strcmp0(method_name, "EnumSupportedFragments")) {
        /* *** EnumSupportedFragments *** */
        ret = g_variant_new("(a(ss))", encode_fragments());
        succeeded = TRUE;
    } else {
        g_set_error(&error, CDEMU_ERROR, CDEMU_ERROR_INVALID_ARGUMENT, "Invalid method name '%s'!", method_name);
    }

    if (succeeded) {
        g_dbus_method_invocation_return_value(invocation, ret);
    } else {
        /* We need to map the code */
        if (error->domain == MIRAGE_ERROR) {
            error->domain = g_quark_from_string(DBUS_ERROR_LIBMIRAGE);
        } else if (error->domain == CDEMU_ERROR) {
            error->domain = g_quark_from_string(DBUS_ERROR_CDEMU);
        }
        g_dbus_method_invocation_return_gerror(invocation, error);
    }
}

/* Interface VTable */
static const GDBusInterfaceVTable dbus_interface_vtable = {
    (GDBusInterfaceMethodCallFunc)cdemu_daemon_dbus_handle_method_call,
    NULL,
    NULL,
    { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL }
};


static void on_bus_acquired (GDBusConnection *connection, const gchar *name G_GNUC_UNUSED, CdemuDaemon *self)
{
    /* Create introspection data from our embedded xml */
    GDBusNodeInfo *introspection_data;

    /* Store connection */
    self->priv->connection = connection;

    /* Register D-Bus error domains */
    register_error_domain(DBUS_ERROR_CDEMU, CDEMU_TYPE_ERROR);
    register_error_domain(DBUS_ERROR_LIBMIRAGE, MIRAGE_TYPE_ERROR);

    /* Register object */
    introspection_data = g_dbus_node_info_new_for_xml(introspection_xml, NULL);

    g_dbus_connection_register_object(
        connection,
        "/Daemon",
        introspection_data->interfaces[0],
        &dbus_interface_vtable,
        self,
        NULL,
        NULL
    );

    g_dbus_node_info_unref(introspection_data);
}


static void on_name_lost (GDBusConnection *connection G_GNUC_UNUSED, const gchar *name G_GNUC_UNUSED, CdemuDaemon *self)
{
    cdemu_daemon_stop_daemon(self);
}


/**********************************************************************\
 *                    Daemon's D-Bus functions                        *
\**********************************************************************/
gboolean cdemu_daemon_dbus_check_if_name_is_available (CdemuDaemon *self, GBusType bus_type)
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
        CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to get proxy for 'org.freedesktop.DBus' on %s bus: %s!\n", __debug__, bus_type == G_BUS_TYPE_SYSTEM ? "system" : "session", dbus_error->message);
        g_error_free(dbus_error);
        return FALSE;
    }

    dbus_reply = g_dbus_proxy_call_sync(
        dbus_proxy,
        "NameHasOwner",
        g_variant_new("(s)", CDEMU_DAEMON_DBUS_NAME),
        G_DBUS_CALL_FLAGS_NO_AUTO_START,
        -1,
        NULL,
        &dbus_error
    );

    if (!dbus_reply) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to check if name '%s' is already taken on %s bus!\n", __debug__, CDEMU_DAEMON_DBUS_NAME, bus_type == G_BUS_TYPE_SYSTEM ? "system" : "session");
        g_error_free(dbus_error);
        g_object_unref(dbus_proxy);
        return FALSE;
    }

    g_variant_get(dbus_reply, "(b)", &name_taken);

    g_variant_unref(dbus_reply);
    g_object_unref(dbus_proxy);

    if (name_taken) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: name '%s' is already taken on %s bus! Is there another instance already running?\n", __debug__, CDEMU_DAEMON_DBUS_NAME, bus_type == G_BUS_TYPE_SYSTEM ? "system" : "session");
        return FALSE;
    }

    return TRUE;
}

void cdemu_daemon_dbus_register_on_bus (CdemuDaemon *self, GBusType bus_type)
{
    /* Claim name on D-BUS */
    self->priv->owner_id = g_bus_own_name(
        bus_type,
        CDEMU_DAEMON_DBUS_NAME,
        G_BUS_NAME_OWNER_FLAGS_NONE,
        (GBusAcquiredCallback)on_bus_acquired,
        NULL,
        (GBusNameLostCallback)on_name_lost,
        self,
        NULL
    );
}

void cdemu_daemon_dbus_cleanup (CdemuDaemon *self)
{
    /* Release D-Bus name */
    g_bus_unown_name(self->priv->owner_id);
}


/**********************************************************************\
 *                      D-Bus Signal emission                         *
\**********************************************************************/
void cdemu_daemon_dbus_emit_device_status_changed (CdemuDaemon *self, gint number)
{
    if (self->priv->connection) {
        g_dbus_connection_emit_signal(self->priv->connection, NULL,
            "/Daemon", CDEMU_DAEMON_DBUS_NAME,
            "DeviceStatusChanged", g_variant_new("(i)", number),
            NULL);
    }
}

void cdemu_daemon_dbus_emit_device_option_changed (CdemuDaemon *self, gint number, const gchar *option)
{
    if (self->priv->connection) {
        g_dbus_connection_emit_signal(self->priv->connection, NULL,
            "/Daemon", CDEMU_DAEMON_DBUS_NAME,
            "DeviceOptionChanged", g_variant_new("(is)", number, option),
            NULL);
    }
}

void cdemu_daemon_dbus_emit_device_mappings_ready (CdemuDaemon *self)
{
    if (self->priv->connection) {
        g_dbus_connection_emit_signal(self->priv->connection, NULL,
            "/Daemon", CDEMU_DAEMON_DBUS_NAME,
            "DeviceMappingsReady", g_variant_new("()"),
            NULL);
    }
}


/**********************************************************************\
 *                   Embedded introspection data                      *
\**********************************************************************/
static const gchar introspection_xml[] =
    "<node>"
    "    <interface name='" CDEMU_DAEMON_DBUS_NAME "'>"
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
    "            <arg name='parsers' type='a(ssa(ss))' direction='out'/>"
    "        </method>"
    "        <method name='EnumSupportedFileFilters'>"
    "            <arg name='filters' type='a(ssa(ss))' direction='out'/>"
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
