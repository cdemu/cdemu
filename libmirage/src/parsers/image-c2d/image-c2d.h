/*
 *  libMirage: C2D image parser
 *  Copyright (C) 2006-2008 Henrik Stokseth
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

#ifndef __IMAGE_C2D_H__
#define __IMAGE_C2D_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"
#include "image-c2d-disc.h"


G_BEGIN_DECLS

#define C2D_SIGNATURE "Roxio Image File Format 3.0"

#pragma pack(1)

typedef struct {
    guint8  signature[28]; /* "Roxio Image File Format 3.0" + \0 */
    guint16 dummy[2];      /* (unknown) */
    guint32 block_size;    /* Length of header block (200) */
    guint16 dummy2[7];     /* (unknown) */
    guint16 track_blocks;  /* Number of track blocks */
    guint32 session_size;  /* Size of session(?) block */
    guint64 offset_tracks; /* Offset to track blocks */
    guint32 dummy3[35];    /* (unknown) */
} C2D_HeaderBlock;  /* length: 200 bytes */

typedef struct {
    guint32 dummy[18];     /* (unknown) */
} C2D_SessionBlock;  /* length: 72 bytes */

typedef struct {
    guint32 block_size;   /* Length of this track block (44) */
    guint32 first_sector; /* First sector in track */   
    guint32 last_sector;  /* Last sector in track */
    guint64 image_offset; /* Image offset of track */
    guint32 sector_size;  /* Bytes per sector */
    guint16 dummy[6];     /* (unknown) */
    guint8  mode;
    guint8  session;
    guint8  point;
    guint8  index;
    guint32 dummy2;       /* (unknown) */
} C2D_TrackBlock;  /* length: 44 bytes */

#pragma pack()

GTypeModule *global_module;

G_END_DECLS


#endif /* __IMAGE_C2D_H__ */
