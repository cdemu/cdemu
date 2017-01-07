/*
 *  libMirage: MDX image
 *  Copyright (C) 2006-2014 Henrik Stokseth
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

#ifndef __IMAGE_MDX_H__
#define __IMAGE_MDX_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <mirage/mirage.h>
#include <glib/gi18n-lib.h>

#include "parser.h"


G_BEGIN_DECLS

#define MDX_SIGNATURE "MEDIA DESCRIPTOR"

#pragma pack(1)

typedef struct
{
    gchar signature[16]; /* "MEDIA DESCRIPTOR" */
    guint8 version[2]; /* 2, 0 */
    gchar copyright[26]; /* "(C) 2000-2010 DT Soft Ltd." */
    gint32 __dummy__; /* 0xFFFFFFFF (-1) */
    guint64 footer_offset; /* Offset to footer */
    guint64 blockinfo_size; /* Footer size minus this value always equals 0x1c0 (448) */
    /* This means the footer has two blocks, one fixed-size and one variable-sized */
} MDX_Header; /* length: 64 bytes */

typedef struct
{
    gchar signature[16]; /* "MEDIA DESCRIPTOR" */
    guint8 version[2]; /* 2, 0 */
    gchar copyright[26]; /* "(C) 2000-2010 DT Soft Ltd." */
    guint32 blockinfo_size; /* Footer size minus this value always equals 0x1c0 (448) */
    /* In MDS, this value is 4-byte int; does this mean it is also in MDX? */
} MDS_Header; /* length: 48 */

#pragma pack()

G_END_DECLS

#endif /* __IMAGE_MDX_H__ */
