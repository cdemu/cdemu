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

#pragma once

#define CDEMU_ERROR (cdemu_error_quark ())
#define CDEMU_TYPE_ERROR (cdemu_error_get_type ())

/* Error codes */
enum
{
    CDEMU_ERROR_INVALID_ARGUMENT,
    CDEMU_ERROR_ALREADY_LOADED,
    CDEMU_ERROR_DEVICE_LOCKED,
    CDEMU_ERROR_DAEMON_ERROR,
};

#include <glib.h>

GQuark cdemu_error_quark (void);
GType  cdemu_error_get_type (void);
