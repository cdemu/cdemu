/*
 *  CDEmuD: error handling
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

#ifndef __CDEMUD_ERROR_H__
#define __CDEMUD_ERROR_H__

#define CDEMUD_ERROR (cdemud_error_quark ())
#define CDEMUD_TYPE_ERROR (cdemud_error_get_type ())

/* Error codes */
enum
{
    CDEMUD_ERROR_INVALID_ARGUMENT,
    CDEMUD_ERROR_ALREADY_LOADED,
    CDEMUD_ERROR_DEVICE_LOCKED,
};

#include <glib.h>

GQuark cdemud_error_quark (void);
GType  cdemud_error_get_type (void);

#endif /* __CDEMUD_ERROR_H__ */
