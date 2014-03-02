/*
 *  libMirage: B6T image
 *  Copyright (C) 2007-2014 Rok Mandeljc
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

#ifndef __IMAGE_B6T_H__
#define __IMAGE_B6T_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"
#include "image-b6t-parser.h"


G_BEGIN_DECLS

typedef enum {
    UNKNOWN     = 0x0000,   /* No Media */
    NRDISC      = 0x0001,   /* Non removable disc */
    RMDISC      = 0x0002,   /* Removable Disc */
    MOE         = 0x0003,   /* Magneto Optical Erasable */
    OPTICWO     = 0x0004,   /* Optical Write Once */
    ASMO        = 0x0005,   /* Advanced Storage - Magneto Optical */
    CDROM       = 0x0008,   /* CD-ROM (not writable) */
    CDR         = 0x0009,   /* CD-R (Write once) */
    CDRW        = 0x000A,   /* CD-RW (R/W) */
    DVDROM      = 0x0010,   /* DVD-ROM (not writable) */
    DVDR        = 0x0011,   /* DVD-R */
    DVDRAM      = 0x0012,   /* DVD-RAM */
    DVDRW       = 0x0013,   /* DVD-RW Restricted overwrite */
    DVDRWSEQ    = 0x0014,   /* DVD-RW Sequential Recording */
    DLDVDR      = 0x0015,   /* Dual Layer DVD-R */
    DLDVDRJMP   = 0x0016,   /* Dual Layer DVD-R Jump Recording */
    DVDPLUSRW   = 0x001A,   /* DVD+RW */
    DVDPLUSR    = 0x001B,   /* DVD+R */
    DDCDROM     = 0x0020,   /* Double Density CD-ROM */
    DDCDR       = 0x0021,   /* Double Density CD-R (Write once) */
    DDCDRW      = 0x0022,   /* Double Density CD-RW */
    DLDVDPLUSRW = 0x002A,   /* Dual Layer DVD+RW */
    DLDVDPLUSR  = 0x002B,   /* Dual Layer DVD+R */
    BDROM       = 0x0040,   /* BluRay Disc ROM (not writable) */
    BDR         = 0x0041,   /* BluRay Disc Sequential Recording */
    BDRE        = 0x0042,   /* BluRay Disc Random Recording */

    HDBURNCDROM = 0x0080,   /* HDBurn CD-R */
    HDBURNCDR   = 0x0081,   /* HDBurn CD-R */
    HDBURNCDRW  = 0x0082,   /* HDBurn CD-RW */

    NODEVICE    = 0xFFFE,
    CUSTOM      = 0xFFFF   /* doesn't conform to any standard media */
} B6T_MediaType;



#pragma pack(1)

/* This is the first chunk of data that follows the signature; it consists of
   112 bytes that are more or less undeciphered, but it seems to contain lengths
   of other blocks at the end */
typedef struct
{
    guint32 __dummy1__; /* Default value: 0x00000002 */
    guint32 __dummy2__; /* Default value: 0x00000002 */
    guint32 __dummy3__; /* Default value: 0x00000006 */
    guint32 __dummy4__; /* Default value: 0x00000000 */

    guint32 __dummy5__; /* Default value: 0x00000000 */
    guint32 __dummy6__; /* Default value: 0x00000000 */
    guint32 __dummy7__; /* Default value: 0x00000000 */
    guint32 __dummy8__; /* Default value: 0x00000000 */

    guint16 disc_type; /* 0x08 is CD-ROM, 0x10 is DVD-ROM; it seems to be
                          equivalent to current profile from GET CONFIGURATION */
    guint16 num_sessions; /* Number of sessions */
    guint32 __dummy9__; /* Default value: 0x00000002 */
    guint32 __dummy10__; /* Default value: 0x00000000 */
    guint32 __dummy11__; /* Default value: 0x00000000 */

    guint8 mcn_valid;
    gchar mcn[13];
    guint8 __dummy12__;
    guint8 __dummy13__;

    guint32 __dummy14__; /* Default value: 0x00000000 */
    guint32 __dummy15__; /* Default value: 0x00000000 */
    guint32 __dummy16__; /* Default value: 0x00000000 */
    guint32 __dummy17__; /* Default value: 0x00000000 */

    guint16 pma_data_length; /* PMA data length, as read by READ TOC/PMA/ATIP, format 3 */
    guint16 atip_data_length; /* ATIP data length, as read by READ TOC/PMA/ATIP, format 4 */
    guint16 cdtext_data_length; /* CD-TEXT data length; read by READ TOC/PMA/ATIP, format 5 */
    guint16 cdrom_info_length; /* Length of block which seemingly contains additional info about disc (CD-ROM) */
    guint32 dvdrom_bca_length; /* BCA length for DVD-ROM */
    guint32 __dummy19__; /* Default value: 0x00000000 */

    guint32 __dummy20__; /* Default value: 0x00000000 */
    guint32 __dummy21__; /* Default value: 0x00000000 */
    guint32 dvdrom_structures_length; /* Disc structures data length for DVD-ROM */
    guint32 dvdrom_info_length; /* Same as cdrom_info_length above, but for DVD-ROM */
} B6T_DiscBlock_1;

/* This is chunk of bytes that represents lengths of blocks that follow... */
typedef struct
{
    guint32 mode_page_2a_length; /* Length of Mode Page 0x2A */
    guint32 unknown1_length; /* Unknown; seems to be always 4 bytes? */
    guint32 datablocks_length; /* Length of data-blocks data */
    guint32 sessions_length; /* Length of sessions data */
    guint32 dpm_data_length; /* Length of internal DPM data */
} B6T_DiscBlock_2;


/* Drive identifiers, taken from INQUIRY data */
typedef struct
{
    gchar  vendor[8];
    gchar  product[16];
    gchar  revision[4];
    guint8 vendor_specific[20]; /* note: may not be a valid string */
} B6T_DriveIdentifiers;

/* Structure that represents data block... I think the idea behind them is similar
   to the idea behind fragments in libMirage... */
typedef struct
{
    guint32 type; /* In one way or another; different values here mean different type of data... */
    guint32 length_bytes; /* Length of data block, in bytes */
    guint32 __dummy1__;
    guint32 __dummy2__;
    guint32 __dummy3__;
    guint32 __dummy4__;
    guint32 offset; /* Offset in image file */
    guint32 __dummy5__;
    guint32 __dummy6__;
    guint32 __dummy7__;
    gint32 start_sector; /* First sector for which the block holds data (signed integer!) */
    gint32 length_sectors; /* Length of data block, in sectors (also signed due to arithmetics!) */
    guint32 filename_length; /* Filename length */

    /* Fields below will have to be read separately */
    gchar *filename; /* Original filename is UTF-16, but we'll convert it... */
    guint32 __dummy8__;
} B6T_DataBlock;

/* Session descriptor; 16 bytes, more or less figured out */
typedef struct
{
    guint16 number; /* Session number */
    guint8 num_entries; /* Number of entries */
    guint8 __dummy1__; /* Not sure... seems to be fixed at 3... */

    gint32 session_start; /* Session start address */

    gint32 session_end; /* Session end address */

    guint16 first_track; /* First track in session */
    guint16 last_track; /* Last track in session */
} B6T_Session;

/* Generic track descriptor; this is 64-byte version, so in case this is 'real'
   track descriptor, additional 8 bytes will have to be read manually */
typedef struct
{
    guint8 type; /* Track descriptor type */
    guint8 __dummy1__; /* Unknown; seems to be set to 1 for data and 0 to audio tracks */
    guint8 __dummy2__; /* Unknown; seems to be set to 1 for data and 0 to audio tracks */
    guint8 __dummy3__; /* Unknown; seems to be set to 1 for data and 0 to audio tracks */

    guint32 __dummy4__;

    guint8 subchannel; /* If set to 4, it means subchannel is present */
    guint8 __dummy5__;
    guint8 ctl;
    guint8 adr;

    guint8 point; /* Point */
    guint8 __dummy6__;
    guint8 min;
    guint8 sec;

    guint8 frame;
    guint8 zero;
    guint8 pmin;
    guint8 psec;

    guint8 pframe;
    guint8 __dummy7__;

    guint32 pregap; /* Pregap length */

    guint32 __dummy8__;
    guint32 __dummy9__;
    guint32 __dummy10__;
    guint32 __dummy11__;

    gint32 start_sector;
    gint32 length;

    guint32 __dummy12__;
    guint32 __dummy13__;
    guint32 session_number;

    guint16 __dummy14__;
} B6T_Track;

#pragma pack()

G_END_DECLS

#endif /* __IMAGE_B6T_H__ */
