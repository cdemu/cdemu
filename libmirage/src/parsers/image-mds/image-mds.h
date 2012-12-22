/*
 *  libMirage: MDS image parser
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

#ifndef __IMAGE_MDS_H__
#define __IMAGE_MDS_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"
#include "image-mds-parser.h"


G_BEGIN_DECLS

typedef enum {
    CD          = 0x00, /* CD-ROM */
    CD_R        = 0x01, /* CD-R */
    CD_RW       = 0x02, /* CD-RW */
    DVD         = 0x10, /* DVD-ROM */
    DVD_MINUS_R = 0x12  /* DVD-R */
} MDS_Medium;

typedef enum {
    UNKNOWN     = 0x00,
    AUDIO       = 0xA9, /* sector size = 2352 */
    MODE1       = 0xAA, /* sector size = 2048 */
    MODE2       = 0xAB, /* sector size = 2336 */
    MODE2_FORM1 = 0xAC, /* sector size = 2048 */
    MODE2_FORM2 = 0xAD  /* sector size = 2324 (+4) */
} MDS_TrackMode;

typedef enum {
    NONE           = 0x00, /* no subchannel */
    PW_INTERLEAVED = 0x08  /* 96-byte PW subchannel, interleaved */
} MDS_SubChan;

typedef enum {
    TRACK_FIRST   = 0xA0, /* info about first track */
    TRACK_LAST    = 0xA1, /* info about last track  */
    TRACK_LEADOUT = 0xA2  /* info about lead-out    */
} MDS_Point;

#pragma pack(1)

typedef struct
{
    guint8 signature[16]; /* "MEDIA DESCRIPTOR" */
    guint8 version[2]; /* Version ? */
    guint16 medium_type; /* Medium type */
    guint16 num_sessions; /* Number of sessions */
    guint16 __dummy1__[2]; /* Wish I knew... */
    guint16 bca_len; /* Length of BCA data (DVD-ROM) */
    guint32 __dummy2__[2];
    guint32 bca_data_offset; /* Offset to BCA data (DVD-ROM) */
    guint32 __dummy3__[6]; /* Probably more offsets */
    guint32 disc_structures_offset; /* Offset to disc structures */
    guint32 __dummy4__[3]; /* Probably more offsets */
    guint32 sessions_blocks_offset; /* Offset to session blocks */
    guint32 dpm_blocks_offset; /* offset to DPM data blocks */
} MDS_Header; /* length: 88 bytes */


typedef struct
{
    gint32 session_start; /* Session's start address */
    gint32 session_end; /* Session's end address */
    guint16 session_number; /* Session number */
    guint8 num_all_blocks; /* Number of all data blocks. */
    guint8 num_nontrack_blocks; /* Number of lead-in data blocks */
    guint16 first_track; /* First track in session */
    guint16 last_track; /* Last track in session */
    guint32 __dummy1__; /* (unknown) */
    guint32 tracks_blocks_offset; /* Offset of lead-in+regular track data blocks. */
} MDS_SessionBlock; /* length: 24 bytes */


typedef struct
{
    guint8 mode; /* Track mode */
    guint8 subchannel; /* Subchannel mode */
    guint8 adr_ctl; /* Adr/Ctl */
    guint8 __dummy2__; /* Track flags? */
    guint8 point; /* Track number. (>0x99 is lead-in track) */

    guint32 __dummy3__;
    guint8 min; /* Min */
    guint8 sec; /* Sec */
    guint8 frame; /* Frame */
    guint32 extra_offset; /* Start offset of this track's extra block. */
    guint16 sector_size; /* Sector size. */

    guint8 __dummy4__[18];
    guint32 start_sector; /* Track start sector (PLBA). */
    guint64 start_offset; /* Track start offset. */
    guint32 number_of_files; /* Number of files */
    guint32 footer_offset; /* Start offset of footer. */
    guint8 __dummy6__[24];
} MDS_TrackBlock; /* length: 80 bytes */


typedef struct
{
    guint32 pregap; /* Number of sectors in pregap. */
    guint32 length; /* Number of sectors in track. */
} MDS_TrackExtraBlock; /* length: 8 bytes */


typedef struct
{
    guint32 filename_offset; /* Start offset of image filename. */
    guint32 widechar_filename; /* Seems to be set to 1 if widechar filename is used */
    guint32 __dummy1__;
    guint32 __dummy2__;
} MDS_Footer; /* length: 16 bytes */


#pragma pack()

G_END_DECLS

#endif /* __IMAGE_MDS_H__ */
