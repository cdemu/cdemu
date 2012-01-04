/*
 *  CDEmuD: Main header
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


#ifndef __CDEMUD_H__
#define __CDEMUD_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include <ao/ao.h>

#include <glib-object.h>
#include <gio/gio.h>

#include <mirage.h>


#include "cdemud-audio.h"

#include "cdemud-mmc-features.h"
#include "cdemud-mode-pages.h"
#include "cdemud-packet-commands.h"
#include "cdemud-sense-constants.h"

#include "cdemud-error.h"
#include "cdemud-debug.h"

#include "cdemud-daemon.h"
#include "cdemud-device.h"


/* Commonly used macros */
#define CDEMUD_CHECK_ARG(param) \
    if (!param) { \
        cdemud_error(CDEMUD_E_INVALIDARG, error); \
        return FALSE; \
    }

#define G_LIST_FOR_EACH(cursor,list) \
    for ((cursor) = (list); (cursor); (cursor) = (cursor)->next)

#endif /* __CDEMUD_H__ */
