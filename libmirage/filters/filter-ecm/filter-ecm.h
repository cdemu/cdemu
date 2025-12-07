/*
 *  libMirage: ECM filter
 *  Copyright (C) 2012-2014 Rok Mandeljc
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

#include "mirage/config.h"
#include <mirage/mirage.h>

#include <glib/gi18n-lib.h>

#include "filter-stream.h"

G_BEGIN_DECLS


typedef enum
{
    ECM_RAW = 0,
    ECM_MODE1_2352 = 1,
    ECM_MODE2_FORM1_2336 = 2,
    ECM_MODE2_FORM2_2336 = 3,
} ECM_PartType;


G_END_DECLS
