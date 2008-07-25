/*
 *  libMirage: ISO image plugin
 *  Copyright (C) 2006-2008 Rok Mandeljc
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

#ifndef __IMAGE_ISO_H__
#define __IMAGE_ISO_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"
#include "image-iso-disc.h"


G_BEGIN_DECLS

struct iso_volume_descriptor {
	guint8 type; 
	guint8 id[5];
	guint8 version;
	guint8 data[2041];
};

#define ISO_VD_BOOT_RECORD   0
#define ISO_VD_PRIMARY       1
#define ISO_VD_SUPPLEMENTARY 2
#define ISO_VD_PARTITION     3
#define ISO_VD_END           255

#define ISO_STANDARD_ID "CD001"

#define ISOFS_BLOCK_SIZE 2048

GTypeModule *global_module;

G_END_DECLS

#endif /* __IMAGE_ISO_H__ */
