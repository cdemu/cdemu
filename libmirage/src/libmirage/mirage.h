/*
 *  libMirage: Main header
 *  Copyright (C) 2006-2007 Rok Mandeljc
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


#ifndef __MIRAGE_H__
#define __MIRAGE_H__

#include <glib.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#include <gmodule.h>

#include <string.h>

#include <linux/cdrom.h>

#include "mirage-debug.h"
#include "mirage-error.h"
#include "mirage-plugin.h"

#include "mirage-object.h"

#include "mirage-mirage.h"
#include "mirage-disc.h"
#include "mirage-fragment.h"
#include "mirage-index.h"
#include "mirage-language.h"
#include "mirage-session.h"
#include "mirage-track.h"
#include "mirage-sector.h"

#include "mirage-disc-structures.h"

#include "mirage-cdtext-encdec.h"

#include "mirage-utils.h"

#endif /* __MIRAGE_H__ */
