/*
 *  libMirage: MDX image plugin
 *  Copyright (C) 2006-2012 Henrik Stokseth
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

#ifndef __IMAGE_MDX_H__
#define __IMAGE_MDX_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"
#include "image-mdx-parser.h"


G_BEGIN_DECLS

#define MDX_SIGNATURE "MEDIA DESCRIPTOR"

#pragma pack(1)

typedef struct {
	gchar    signature[16]; /* "MEDIA DESCRIPTOR" */
	guchar  version[2]; /* 2, 0 */
	gchar    copyright[26]; /* "(C) 2000-2010 DT Soft Ltd." */
	gint32   __dummy1__; /* 0xFFFFFFFF (-1) */
	gint64   footer_offset; /* Offset to footer */
	guint64 __dummy2__;
} MDX_Header; /* length: 64 bytes */

#pragma pack()

G_END_DECLS

#endif /* __IMAGE_MDX_H__ */
