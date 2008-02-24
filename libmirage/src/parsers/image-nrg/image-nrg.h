/*
 *  libMirage: NRG image plugin
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

#ifndef __IMAGE_NRG_H__
#define __IMAGE_NRG_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"
#include "image-nrg-disc.h"


G_BEGIN_DECLS

#pragma pack(1)

typedef struct {
    guint32 __dummy1__;
    gchar   mcn[13];
    guint8  __dummy2__;
    guint8  _session_type;  /* ? */
    guint8  _num_sessions;  /* ? */
    guint8  first_track;
    guint8  last_track;
} NRG_DAO_Header;  /* length: 22 bytes */

typedef struct {
    gchar   isrc[12];
    guint16 sector_size;
    guint8  mode_code;
    guint8  __dummy1__;
    guint16 __dummy2__;
    /* The following fields are 32-bit in old format and 64-bit in new format */
    guint64 pregap_offset; /* Pregap offset in file */
    guint64 start_offset; /* Track start offset in file */
    guint64 end_offset; /* Track end offset */
} NRG_DAO_Block;

typedef struct {
    guint8  adr_ctl;
    guint8  track;
    guint8  index;
    guint8  __dummy1__;
    guint32 start_sector;
} NRG_CUE_Block;

#pragma pack()


GTypeModule *global_module;

G_END_DECLS

#endif /* __IMAGE_NRG_H__ */
