/*
 *  libMirage: CIF image
 *  Copyright (C) 2008-2014 Henrik Stokseth
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

#ifndef __IMAGE_CIF_H__
#define __IMAGE_CIF_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <mirage/mirage.h>

#include "parser.h"


G_BEGIN_DECLS

/* Image type */
typedef enum {
    DATA      = 0x01,
    MIXED     = 0x02,
    MUSIC     = 0x03,
    ENCHANCED = 0x04,
    VIDEO     = 0x05,
    BOOTABLE  = 0x06,
    MP3       = 0x07
} CIF_Image;

/* Session type */
typedef enum {
    CDDA    = 0x00,
    CDROM   = 0x01,
    CDROMXA = 0x03
} CIF_Session;

/* Track type */
typedef enum {
    AUDIO       = 0x00, /* Audio */
    MODE1       = 0x01, /* Mode 1 */
    MODE2_FORM1 = 0x02, /* Mode 2 Form 1 (not verified!) */
    MODE2_MIXED = 0x04  /* Mode 2 Mixed */
} CIF_Track;

/* The CIF file format is compatible with the joint IBM/Microsoft
Resource Interchange File Format (RIFF) standard of 1991, see references:
http://en.wikipedia.org/wiki/Resource_Interchange_File_Format
*/

#pragma pack(1)

typedef struct
{
    gchar riff[4]; /* "RIFF" */
    guint32 length; /* Length of block (including the type field) */
    gchar type[4];
} CIF_Header; /* length: 12 bytes */

typedef struct
{
    gchar riff[4]; /* "RIFF" */
    guint32 length; /* Length of block this entry points to (plus four bytes) */
    gchar type[4]; /* "adio", "info" */
    guint32 offset; /* Offset of track block in image */
    guint8 dummy[6]; /* (unknown) */
} CIF_OffsetEntry; /* length: 22 bytes */

typedef struct
{
    guint16 descriptor_length; /* Full length of descriptor */
    guint16 num_sessions; /* Number of sessions on disc */
    guint16 num_tracks; /* Number of tracks on disc */
    guint16 title_length; /* Length of disc title */
    guint16 descriptor_length2; /* Repeated length of descriptor */
    guint16 dummy1;
    guint16 image_type; /* Image type; see CIF_IMAGE_* values */
    guint16 dummy2;
    gchar title_and_artist[]; /* Zero-terminated title and artist string */
} CIF_DiscDescriptor; /* length: 16 bytes + variable title and artist name + a byte that appears sometimes */

typedef struct
{
    guint16 descriptor_length; /* Full length of descriptor */
    guint16 num_tracks; /* Number of tracks */
    guint16 dummy1; /* Always 1? */
    guint16 dummy2; /* 1 for images with data track */
    guint16 dummy3; /* Always 0? */
    guint16 session_type; /* Session type; see CIF_SESSION_* values */
    guint16 dummy4; /* Always 0? */
    guint16 dummy5; /* Always 0? */
    guint16 dummy6; /* Always 0? */
} CIF_SessionDescriptor; /* length: 18 bytes */


typedef struct
{
    guint16 descriptor_length; /* Full length of descriptor */
    guint16 dummy1;
    guint32 num_sectors; /* Length of track in sectors */
    guint16 dummy2;
    guint16 type; /* Track type; see CIF_TYPE_* values */
    guint16 dummy3;
    guint16 dummy4;
    guint16 dummy5;
    guint16 dao_mode; /* 0 = TAO, 4 = DAO */
    guint16 dummy7;
    guint16 sector_data_size; /* Sector data size (not the actual stored size!) */
} CIF_TrackDescriptor; /* length: 24 bytes + audio/data part */

typedef struct
{
    guint8 dummy1[205];
    gchar isrc[12]; /* ISRC */
    guint8 dummy2[28];
    guint32 fadein_length; /* Measured in frames */
    guint32 dummy3[2];
    guint32 fadeout_length; /* Measured in frames */
    guint32 dummy4[2];
    gchar title[]; /* Zero-terminated string. */
} CIF_AudioTrackDescriptor; /* length: 269 bytes + variable title */

typedef struct
{
    guint8 dummy[272]; /* Appears to be a fixed pattern? */
} CIF_DataTrackDescriptor; /* length: 272 bytes */

#pragma pack()


G_END_DECLS

#endif /* __IMAGE_CIF_H__ */
