/*
 *  libMirage: MDS image parser
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

#ifndef __IMAGE_MDS_H__
#define __IMAGE_MDS_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"
#include "image-mds-disc.h"


G_BEGIN_DECLS

#define MDS_DISCMEDIA_CD            0x00    /* CD-ROM */
#define MDS_DISCMEDIA_CD_R          0x01    /* CD-R */
#define MDS_DISCMEDIA_CD_RW         0x02    /* CD-RW */
#define MDS_DISCMEDIA_DVD           0x10    /* DVD-ROM   */
#define MDS_DISCMEDIA_DVD_MINUS_R   0x12    /* DVD-R */
 
#define MDS_TRACKMODE_UNKNOWN       0x00
#define MDS_TRACKMODE_AUDIO         0xA9    /* sector size = 2352      */
#define MDS_TRACKMODE_MODE1         0xAA    /* sector size = 2048      */
#define MDS_TRACKMODE_MODE2         0xAB    /* sector size = 2336      */
#define MDS_TRACKMODE_MODE2_FORM1   0xAC    /* sector size = 2048      */
#define MDS_TRACKMODE_MODE2_FORM2   0xAD    /* sector size = 2324 (+4) */

#define MDS_SUBCHAN_NONE            0x00    /* no subchannel */
#define MDS_SUBCHAN_PW_INTERLEAVED  0x08    /* 96-byte PW subchannel, interleaved */

#define MDS_POINT_TRACK_FIRST       0xA0    /* info about first track */
#define MDS_POINT_TRACK_LAST        0xA1    /* info about last track  */
#define MDS_POINT_TRACK_LEADOUT     0xA2    /* info about lead-out    */


#pragma pack(1)

typedef struct {
    guint8  signature[16];          /* "MEDIA DESCRIPTOR" */
    guint8  version[2];             /* Version ? */
    guint16 medium_type;            /* Medium type */
    guint16 num_sessions;           /* Number of sessions */
    guint16 __dummy1__[2];          /* Wish I knew... */
    guint16 bca_len;                /* Length of BCA data (DVD-ROM) */
    guint32 __dummy2__[2];
    guint32 bca_data_offset;        /* Offset to BCA data (DVD-ROM) */
    guint32 __dummy3__[6];          /* Probably more offsets */
    guint32 disc_structures_offset; /* Offset to disc structures */
    guint32 __dummy4__[3];          /* Probably more offsets */
    guint32 sessions_blocks_offset; /* Offset to session blocks */
    guint32 dpm_blocks_offset;      /* offset to DPM data blocks */
} MDS_Header;   /* length: 88 bytes */

typedef struct {
    gint32 session_start;           /* Session's start address */
    gint32 session_end;             /* Session's end address */
    guint16 session_number;         /* (Unknown) */
    guint8  num_all_blocks;         /* Number of all data blocks. */
    guint8  num_nontrack_blocks;    /* Number of lead-in data blocks */
    guint16 first_track;            /* Total number of sessions in image? */
    guint16 last_track;             /* Number of regular track data blocks. */
    guint32 __dummy2__;             /* (unknown) */
    guint32 tracks_blocks_offset;   /* Offset of lead-in+regular track data blocks. */
} MDS_SessionBlock; /* length: 24 bytes */

typedef struct {
    guint8  mode;                   /* Track mode */
    guint8  subchannel;             /* Subchannel mode */
    guint8  adr_ctl;                /* Adr/Ctl */
    guint8  __dummy2__;             /* 0x0002 Track flags? */
    guint8  point;                  /* 0x0004 Track number. (>0x99 is lead-in track) */
    
    guint32 __dummy3__;             /* 0x0005 (unknown) */
    guint8  min;                    /* 0x0009 (if track >= 0xA0 -> info about track ###) */
                                    /*        (if track = 0xA2 -> min. @ lead-out) */
    guint8  sec;                    /* 0x000a (if track = 0xA2 -> sec. @ lead-out) */
    guint8  frame;                  /* 0x000b (if track = 0xA2 -> frame @ lead-out) */
    guint32 extra_offset;           /* 0x000c Start offset of this track's extra block. */
    guint16 sector_size;            /* 0x0010 Sector size. */
    
    guint8  __dummy4__[18];         /* 0x0012 (unknown) */
    guint32 start_sector;           /* 0x0024 Track start sector. PLBA. */
    guint64 start_offset;           /* 0x0028 Track start offset. */
    guint8  session;                /* 0x0030 Session or index? */
    guint8  __dummy5__[3];          /* 0x0031 (unknown) */
    guint32 footer_offset;          /* 0x0034 Start offset of footer. */
    guint8  __dummy6__[24];         /* 0x0038 (unknown) */
} MDS_TrackBlock; /* length: 80 bytes */

typedef struct {
    guint32 pregap;                 /* 0x0000 Number of sectors in pregap. */
    guint32 length;                 /* 0x0004 Number of sectors in track. */
} MDS_TrackExtraBlock; /* length: 8 bytes */

typedef struct {
    guint32 filename_offset;        /* Start offset of image filename. */
    guint32 __dummy1__[3];          /* Three more offsets to something, I'd imagine */
} MDS_Footer; /* length: 16 bytes */

typedef struct {
    guint32 dpm_sector;             /* shameless assumption */
} MDS_DPMBlock; /* length: 4 bytes */

#pragma pack()


GTypeModule *global_module;

G_END_DECLS

#endif /* __IMAGE_MDS_H__ */
