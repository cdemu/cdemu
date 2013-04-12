/*
 *  libMirage: CSO file filter
 *  Copyright (C) 2012 Henrik Stokseth
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

#ifndef __FILTER_CSO_H__
#define __FILTER_CSO_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <zlib.h>

#include "mirage.h"
#include "filter-cso-file-filter.h"

G_BEGIN_DECLS

#pragma pack(1)
typedef struct
{
    gchar   magic[4];       /* "CISO" signature                   */
    guint32 header_size;    /* One tool fail to set this value    */
    guint64 total_bytes;    /* Uncompressed data size             */
    guint32 block_size;     /* Uncompressed sector size           */
    guint8  version;        /* Version                            */
    guint8  idx_align;      /* Alignment of index value = 2^align */
    guint16 reserved;       /* Reserved                           */
} ciso_header_t;            /* Length: 24 bytes                   */
#pragma pack()

G_END_DECLS

#endif /* __FILTER_CSO_H__ */
