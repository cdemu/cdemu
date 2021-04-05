/*
 *  CDEmu daemon: main header
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


#ifndef __CDEMU_H__
#define __CDEMU_H__

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include <ao/ao.h>

#include <glib/gi18n.h>
#include <locale.h>

#include <glib-object.h>
#include <gio/gio.h>

#include <mirage/mirage.h>

#include "types.h"

#include "audio.h"

#include "mmc-features.h"
#include "mmc-mode-pages.h"
#include "mmc-packet-commands.h"
#include "mmc-sense-constants.h"

#include "error.h"
#include "debug.h"

#include "daemon.h"
#include "device.h"

#endif /* __CDEMU_H__ */
