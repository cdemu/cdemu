/*
 *  libMirage: MacBinary file filter
 *  Copyright (C) 2013 Henrik Stokseth
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

#ifndef __FILTER_MACBINARY_H__
#define __FILTER_MACBINARY_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


#include "mirage.h"
#include "filter-macbinary-file-filter.h"

G_BEGIN_DECLS

typedef enum {
    MC_INITED    = 1,
    MC_CHANGED   = 2,
    MC_BUSY      = 4,
    MC_BOZO      = 8,
    MC_SYSTEM    = 16,
    MC_BUNDLE    = 32,
    MC_INVISIBLE = 64,
    MC_LOCKED    = 128    
} finder_flag_t;

typedef enum {
    MC_PROTECTED = 1
} macbinary_flag_t;

#pragma pack(1)
typedef struct
{
    guint8  version;        /* Version (equals zero for v2.0)     */
    guint8  fn_length;      /* Length of filename                 */
    gchar   filename[63];   /* File name (not NUL terminated)     */
    gchar   filetype[4];    /* File type                          */
    gchar   creator[4];     /* File creator                       */
    guint8  finder_flags;   /* Finder flags (LSB)                 */
    guint8  reserved_1;     /* Reserved (always zero)             */
    guint16 vert_pos;       /* Vertical position in window        */
    guint16 horiz_pos;      /* Horizontal position in window      */
    guint16 window_id;      /* Window or folder ID                */
    guint8  flags;          /* File flags                         */
    guint8  reserved_2;     /* Reserved (always zero)             */
    guint32 datafork_len;   /* Data fork length                   */
    guint32 resfork_len;    /* Resource fork length               */
    guint32 created;        /* Creation date                      */
    guint32 modified;       /* Modification date                  */
    guint16 getinfo_len;    /* Length of get info comment         */
    /* The following fields are present only in the v2.0 format   */
    guint8  finder_flags_2; /* Finder flags (MSB)                 */
    guint16 reserved_4[7];  /* Reserved (always zero)             */
    guint32 unpacked_len;   /* Total uncompressed length          */
    guint16 secondary_len;  /* Length of secondary header         */
    guint8  pack_ver;       /* Version used to pack (x-129)       */
    guint8  unpack_ver;     /* Version needed to unpack (x-129)   */
    guint16 crc16;          /* CRC16-XModem of previous 124 bytes */
    guint16 reserved_3;     /* Reserved (always zero)             */
} macbinary_header_t;       /* Length: 128 bytes                  */
#pragma pack()

G_END_DECLS

#endif /* __FILTER_MACBINARY_H__ */
