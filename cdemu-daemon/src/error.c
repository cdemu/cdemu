/*
 *  CDEmu daemon: error handling
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


GQuark cdemu_error_quark (void)
{
    static GQuark q = 0;

    if (q == 0) {
        q = g_quark_from_static_string("cdemu-error");
    }

    return q;
}


#define ENUM_ENTRY(NAME, DESC) {NAME, "" #NAME "", DESC}
GType cdemu_error_get_type (void)
{
    static GType type = 0;
    if (type == 0) {
        static const GEnumValue values[] = {
            ENUM_ENTRY(CDEMU_ERROR_INVALID_ARGUMENT, "InvalidArgument"),
            ENUM_ENTRY(CDEMU_ERROR_ALREADY_LOADED, "AlreadyLoaded"),
            ENUM_ENTRY(CDEMU_ERROR_DEVICE_LOCKED, "DeviceLocked"),
            ENUM_ENTRY(CDEMU_ERROR_DAEMON_ERROR, "DaemonError"),
            {0, 0, 0}
        };

        type = g_enum_register_static("CDEmuDaemonError", values);
    }

    return type;
}

