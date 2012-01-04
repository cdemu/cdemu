/*
 *  CDEmuD: Error handling
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


GQuark cdemud_error_quark (void)
{
    static GQuark q = 0;

    if (q == 0) {
        q = g_quark_from_static_string("cdemud-error");
    }

    return q;
}


#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }
GType cdemud_error_get_type (void)
{
    static GType type = 0;
    if (type == 0) {
        static const GEnumValue values[] = {
            ENUM_ENTRY(CDEMUD_E_INVALIDARG, "InvalidArgument"),

            ENUM_ENTRY(CDEMUD_E_NODRIVER, "NoDriver"),
            ENUM_ENTRY(CDEMUD_E_NODEVICES, "NoDevices"),

            ENUM_ENTRY(CDEMUD_E_DEVICEINITFAILED, "DeviceInitializationFailed"),

            ENUM_ENTRY(CDEMUD_E_DBUSCONNECT, "DBusConnect"),
            ENUM_ENTRY(CDEMUD_E_DBUSNAMEREQUEST, "DBusNameRequest"),

            ENUM_ENTRY(CDEMUD_E_INVALIDDEVICE, "InvalidDevice"),

            ENUM_ENTRY(CDEMUD_E_AUDIOBACKEND, "AudioBackend"),
            ENUM_ENTRY(CDEMUD_E_AUDIOINVALIDSTATE, "AudioInvalidState"),

            ENUM_ENTRY(CDEMUD_E_CTLDEVICE, "ControlDevice"),
            ENUM_ENTRY(CDEMUD_E_BUFFER, "Buffer"),
            ENUM_ENTRY(CDEMUD_E_ALREADYLOADED, "AlreadyLoaded"),
            ENUM_ENTRY(CDEMUD_E_DEVLOCKED, "DeviceLocked"),

            ENUM_ENTRY(CDEMUD_E_GENERIC, "Generic"),
            { 0, 0, 0 }
        };

        type = g_enum_register_static("CDEmuDaemonError", values);
    }

    return type;
}


void cdemud_error (gint errcode, GError **error)
{
    struct {
        gint errcode;
        gchar *errstring;
    } errors[] = {
        { CDEMUD_E_INVALIDARG, "Invalid argument." },

        { CDEMUD_E_NODRIVER, "No driver found." },
        { CDEMUD_E_NODEVICES, "No devices found." },

        { CDEMUD_E_DEVICEINITFAILED, "Device initialization failed." },

        { CDEMUD_E_DBUSCONNECT, "Failed to connect to D-BUS bus." },
        { CDEMUD_E_DBUSNAMEREQUEST, "Name request on D-BUS failed." },

        { CDEMUD_E_INVALIDDEVICE, "Invalid device number." },
        { CDEMUD_E_AUDIOBACKEND, "Failed to create audio backend." },
        { CDEMUD_E_AUDIOINVALIDSTATE, "Invalid audio state." },

        { CDEMUD_E_CTLDEVICE, "Failed to open control device." },
        { CDEMUD_E_BUFFER, "Failed to allocate device buffer." },
        { CDEMUD_E_ALREADYLOADED, "Device is already loaded." },
        { CDEMUD_E_DEVLOCKED, "Device is locked." },

        { CDEMUD_E_GENERIC, "Generic error." },
    };

    if (!error) {
        return;
    }

    if (*error) {
        g_error_free(*error);
        *error = NULL;
    }

    gint i;
    for (i = 0; i < G_N_ELEMENTS(errors); i++) {
        if (errors[i].errcode == errcode) {
            g_set_error(error, CDEMUD_ERROR, errors[i].errcode, "%s", errors[i].errstring);
            return;
        }
    }

    /* Generic error */
	g_set_error(error, CDEMUD_ERROR, errors[i-1].errcode, "%s", errors[i-1].errstring);

    return;
}
