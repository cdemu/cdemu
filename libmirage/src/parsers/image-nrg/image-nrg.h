/*
 *  libMirage: NRG image plugin
 *  Copyright (C) 2006-2012 Rok Mandeljc
 * 
 *  Reverse-engineering work in March, 2005 by Henrik Stokseth.
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
#include "image-nrg-parser.h"


G_BEGIN_DECLS

#pragma pack(1)

typedef struct
{
    guint32 __dummy1__;
    gchar mcn[13];
    guint8 __dummy2__;
    guint8 _session_type; /* ? */
    guint8 _num_sessions; /* ? */
    guint8 first_track;
    guint8 last_track;
} NRG_DAO_Header; /* length: 22 bytes */

typedef struct
{
    gchar isrc[12];
    guint16 sector_size;
    guint8 mode_code;
    guint8 __dummy1__;
    guint16 __dummy2__;
    /* The following fields are 32-bit in old format and 64-bit in new format */
    guint64 pregap_offset; /* Pregap offset in file */
    guint64 start_offset; /* Track start offset in file */
    guint64 end_offset; /* Track end offset */
} NRG_DAO_Block;

typedef struct
{
    guint8 adr_ctl;
    guint8 track;
    guint8 index;
    guint8 __dummy1__;
    guint32 start_sector;
} NRG_CUE_Block;

typedef struct
{
    guint64 offset;
    guint64 size;
    guint8 __dummy1__[3];
    guint8 mode;
    guint32 sector;
    guint64 __dummy2__;
} NRG_ETN_Block;

#pragma pack()

typedef struct
{
    gchar block_id[4];
    guint64 offset;
    guint32 length;

    guint64 subblocks_offset;
    guint32 subblocks_length;
    guint32 num_subblocks;
} NRGBlockIndexEntry;

typedef enum
{
    MEDIA_NONE      = 0x00000, /* No media present (NeroAPI >= 5.5.9.4) */
    MEDIA_CD        = 0x00001, /* CD-R/RW */
    MEDIA_DDCD      = 0x00002, /* DDCD-R/RW */
    MEDIA_DVD_M     = 0x00004, /* DVD-R/RW */
    MEDIA_DVD_P     = 0x00008, /* DVD+RW */
    MEDIA_DVD_RAM   = 0x00010, /* DVD-RAM */
    MEDIA_ML        = 0x00020, /* ML (Multi Level disc) */
    MEDIA_MRW       = 0x00040, /* Mt. Rainier */

    /* NeroAPI >= 5.5.9.4 */
    MEDIA_NO_CDR    = 0x00080, /* Exclude CD-R */
    MEDIA_NO_CDRW   = 0x00100, /* Exclude CD-RW */
    MEDIA_CDRW      = MEDIA_CD|MEDIA_NO_CDR,  /* CD-RW */
    MEDIA_CDR       = MEDIA_CD|MEDIA_NO_CDRW, /* CD-R */
    MEDIA_DVD_ROM   = 0x00200, /* DVD-ROM (non writable) */
    MEDIA_CDROM     = 0x00400, /* CD-ROM (non writable) */

    /* NeroAPI >= 5.5.9.10 */
    MEDIA_NO_DVD_M_RW   = 0x00800, /* Exclude DVD-RW */
    MEDIA_NO_DVD_M_R    = 0x01000, /* Exclude DVD-R */
    MEDIA_NO_DVD_P_RW   = 0x02000, /* Exclude DVD+RW */
    MEDIA_NO_DVD_P_R    = 0x04000, /* Exclude DVD+R */
    MEDIA_DVD_M_R   = MEDIA_DVD_M|MEDIA_NO_DVD_M_RW, /* DVD-R */
    MEDIA_DVD_M_RW  = MEDIA_DVD_M|MEDIA_NO_DVD_M_R, /* DVD-RW */
    MEDIA_DVD_P_R   = MEDIA_DVD_P|MEDIA_NO_DVD_P_RW, /* DVD+R */
    MEDIA_DVD_P_RW  = MEDIA_DVD_P|MEDIA_NO_DVD_P_R, /* DVD+RW */
    MEDIA_FPACKET   = 0x08000, /* Fixed Packetwriting */
    MEDIA_VPACKET   = 0x10000, /* Variable Packetwriting */
    MEDIA_PACKETW   = MEDIA_MRW|MEDIA_FPACKET|MEDIA_VPACKET, /* a bit mask for packetwriting */
    
    /* NeroAPI >= 5.5.10.4 */
    MEDIA_HDB = 0x20000, /* HD-Burn */

    /* NeroAPI >= 6.0.0.29 */
    MEDIA_DVD_P_R9 = 0x40000, /* DVD+R Double Layer 9GB */

    /* NeroAPI >= 6.6.0.8 */
    MEDIA_DVD_M_R9      = 0x80000, /* DVD-R Dual Layer 9GB */
    MEDIA_DVD_ANY_R9 = MEDIA_DVD_P_R9|MEDIA_DVD_M_R9, /* Any DVD Dual/Double Layer 9GB */
    MEDIA_DVD_ANY       = MEDIA_DVD_M|MEDIA_DVD_P|MEDIA_DVD_RAM|MEDIA_DVD_ANY_R9, /* Any DVD-Media */

    /* NeroAPI >= 6.6.0.8 */
    MEDIA_BD_ROM    = 0x100000, /* Blu-ray Disc ROM */
    MEDIA_BD_R      = 0x200000, /* Blu-ray Disc Recordable */
    MEDIA_BD_RE     = 0x400000, /* Blu-ray Disc Rewritable */
    MEDIA_BD        = MEDIA_BD_R | MEDIA_BD_RE, /* Any recordable Blu-ray Disc media */
    MEDIA_BD_ANY    = MEDIA_BD|MEDIA_BD_ROM, /* Any Blu-ray Disc media */

    /* NeroAPI >= 6.6.0.1001 */
    MEDIA_HD_DVD_ROM    = 0x0800000, /* HD DVD ROM */
    MEDIA_HD_DVD_R      = 0x1000000, /* HD DVD Recordable */
    MEDIA_HD_DVD_RW     = 0x2000000, /* HD DVD Rewritable */
    MEDIA_HD_DVD        = MEDIA_HD_DVD_R|MEDIA_HD_DVD_RW, /* Any recordable HD DVD media */
    MEDIA_HD_DVD_ANY    = MEDIA_HD_DVD|MEDIA_HD_DVD_ROM, /* Any HD DVD media */
} NERO_MEDIA_TYPE;

G_END_DECLS

#endif /* __IMAGE_NRG_H__ */
