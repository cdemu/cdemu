/*
 *  libMirage: Disc structures definitions
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

#ifndef __MIRAGE_DISC_STRUCTURES_H__
#define __MIRAGE_DISC_STRUCTURES_H__


G_BEGIN_DECLS

/* Note: although at the moment these have the same layout as structures defined
   in MMC-3, they are libMirage's internal representation of disc structures and
   could thus change any time */

typedef struct
{
    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8  book_type   : 4;
        guint8  part_ver    : 4;
    #else
        guint8  part_ver    : 4;
        guint8  book_type   : 4;
    #endif

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8  disc_size   : 4;
        guint8  max_rate    : 4;
    #else
        guint8  max_rate    : 4;
        guint8  disc_size   : 4;
    #endif

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8  __dummy1__  : 1;
        guint8  num_layers  : 2;
        guint8  track_path  : 1;
        guint8  layer_type  : 4;
    #else
        guint8  layer_type  : 4;
        guint8  track_path  : 1;
        guint8  num_layers  : 2;
        guint8  __dummy1__  : 1;
    #endif

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8  linear_density  : 4;
        guint8  track_density   : 4;
    #else
        guint8  track_density   : 4;
        guint8  linear_density  : 4;
    #endif

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint32 __dummy2__      : 8;
        guint32 data_start      : 24;
    #else
        guint32 data_start      : 24;
        guint32 __dummy2__      : 8;
    #endif

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint32 __dummy3__      : 8;
        guint32 data_end        : 24;
    #else
        guint32 data_end        : 24;
        guint32 __dummy3__      : 8;
    #endif

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint32 __dummy4__      : 8;
        guint32 layer0_end      : 24;
    #else
        guint32 layer0_end      : 24;
        guint32 __dummy4__      : 8;
    #endif

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8  bca             : 1;
        guint8  __dummy5__      : 7;
    #else
        guint8  __dummy5__      : 7;
        guint8  bca             : 1;
    #endif

    guint8 media_specific[2031];
} MirageDiscStructurePhysicalInfo;

typedef struct
{
   guint8   copy_protection;
   guint8   region_info;
   guint8   __dummy1__;
   guint8   __dummy2__;
} MirageDiscStructureCopyright;

typedef struct
{
    guint8  disc_manufacturing_data[2048];
} MirageDiscStructureManufacturingData;

G_END_DECLS

#endif /* __MIRAGE_DISC_STRUCTURES_H__ */
