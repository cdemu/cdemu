/*
 *  CDEmuD: MMC-3 device features definitions
 *  Copyright (C) 2006-2009 Rok Mandeljc
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

#ifndef __CDEMUD_MMC_FEATURES_H__
#define __CDEMUD_MMC_FEATURES_H__

#define BIG_ENDIAN_BITLFIELD    (G_BYTE_ORDER == G_BIG_ENDIAN)
#define LITTLE_ENDIAN_BITFIELD  (G_BYTE_ORDER == G_LITTLE_ENDIAN)

#pragma pack(1)

/* FEATURES */
struct Feature_GENERAL {
    guint16 code;
    
    #if BIG_ENDIAN_BITFIELD
        guint8  __dummy__   : 2;
        guint8  ver         : 4;
        guint8  per         : 1;
        guint8  cur         : 1;
    #elif LITTLE_ENDIAN_BITFIELD
        guint8  cur         : 1;
        guint8  per         : 1;
        guint8  ver         : 4;
        guint8  __dummy1__  : 2;
    #endif
    
    guint8  length;
};

struct Profile {
    guint16 profile;
    
    #if BIG_ENDIAN_BITFIELD
        guint8  __dummy1__  : 7;
        guint8  cur         : 1;
    #elif LITTLE_ENDIAN_BITFIELD
        guint8  cur         : 1;
        guint8  __dummy1__  : 7;
    #endif
    
    guint8  __dummy2__;
};

/* Profile List */
struct Feature_0x0000 {
    guint16 code;
    
    #if BIG_ENDIAN_BITFIELD
        guint8  __dummy__   : 2;
        guint8  ver         : 4;
        guint8  per         : 1;
        guint8  cur         : 1;
    #elif LITTLE_ENDIAN_BITFIELD
        guint8  cur         : 1;
        guint8  per         : 1;
        guint8  ver         : 4;
        guint8  __dummy1__  : 2;
    #endif
    
    guint8  length;
    
    /* We support only two profiles; CD-ROM and DVD-ROM (surprise surprise...) */
    struct Profile  profiles[2];
};


/* Core Feature */
struct Feature_0x0001 {
    guint16 code;
    
    #if BIG_ENDIAN_BITFIELD
        guint8  __dummy__   : 2;
        guint8  ver         : 4;
        guint8  per         : 1;
        guint8  cur         : 1;
    #elif LITTLE_ENDIAN_BITFIELD
        guint8  cur         : 1;
        guint8  per         : 1;
        guint8  ver         : 4;
        guint8  __dummy1__  : 2;
    #endif
    
    guint8  length;
        
    guint32 interface;
    
    #if BIG_ENDIAN_BITFIELD
        guint8  __dummy2__  : 6;
        guint8  inq2        : 1;
        guint8  dbevent     : 1;
    #elif LITTLE_ENDIAN_BITFIELD
        guint8  dbevent     : 1;
        guint8  inq2        : 1;
        guint8  __dummy2__  : 6;
    #endif
    
    guint8 __dummy3__[3];
};

/* Morphing Feature */
struct Feature_0x0002 {
    guint16 code;
    
    #if BIG_ENDIAN_BITFIELD
        guint8  __dummy__   : 2;
        guint8  ver         : 4;
        guint8  per         : 1;
        guint8  cur         : 1;
    #elif LITTLE_ENDIAN_BITFIELD
        guint8  cur         : 1;
        guint8  per         : 1;
        guint8  ver         : 4;
        guint8  __dummy1__  : 2;
    #endif
    
    guint8  length;

    #if BIG_ENDIAN_BITFIELD
        guint8  __dummy2__  : 6;
        guint8  ocevent     : 1;
        guint8  async       : 1;
    #elif LITTLE_ENDIAN_BITFIELD
        guint8  async       : 1;
        guint8  ocevent     : 1;
        guint8  __dummy2__  : 6;
    #endif
    
    guint8  __dummy3__[3];
};

/* Removable Medium Feature */
struct Feature_0x0003 {
    guint16 code;
    
    #if BIG_ENDIAN_BITFIELD
        guint8  __dummy__   : 2;
        guint8  ver         : 4;
        guint8  per         : 1;
        guint8  cur         : 1;
    #elif LITTLE_ENDIAN_BITFIELD
        guint8  cur         : 1;
        guint8  per         : 1;
        guint8  ver         : 4;
        guint8  __dummy1__  : 2;
    #endif
    
    guint8  length;

    #if BIG_ENDIAN_BITFIELD
        guint8  mechanism   : 3;
        guint8  __dummy2__  : 1;
        guint8  eject       : 1;
        guint8  prvnt_jmp   : 1;
        guint8  __dummy3__  : 1;
        guint8  lock        : 1;
    #elif LITTLE_ENDIAN_BITFIELD
        guint8  lock        : 1;
        guint8  __dummy3__  : 1;
        guint8  prvnt_jmp   : 1;
        guint8  eject       : 1;
        guint8  __dummy2__  : 1;
        guint8  mechanism   : 3;
    #endif    
    
    guint8  __dummy4__[3];
};

/* Random Readable Feature */
struct Feature_0x0010 {
    guint16 code;
    
    #if BIG_ENDIAN_BITFIELD
        guint8  __dummy__   : 2;
        guint8  ver         : 4;
        guint8  per         : 1;
        guint8  cur         : 1;
    #elif LITTLE_ENDIAN_BITFIELD
        guint8  cur         : 1;
        guint8  per         : 1;
        guint8  ver         : 4;
        guint8  __dummy1__  : 2;
    #endif
    
    guint8  length;

    guint32 block_size;
    guint16 blocking;
    
    #if BIG_ENDIAN_BITFIELD
        guint8  __dummy2__  : 7;
        guint8  pp          : 1;
    #elif LITTLE_ENDIAN_BITFIELD
        guint8  pp          : 1;
        guint8  __dummy2__  : 7;
    #endif
    
    guint8  __dummy3__;
};

/* Multi-read Feature */
struct Feature_0x001D {
    guint16 code;
    
    #if BIG_ENDIAN_BITFIELD
        guint8  __dummy__   : 2;
        guint8  ver         : 4;
        guint8  per         : 1;
        guint8  cur         : 1;
    #elif LITTLE_ENDIAN_BITFIELD
        guint8  cur         : 1;
        guint8  per         : 1;
        guint8  ver         : 4;
        guint8  __dummy1__  : 2;
    #endif
    
    guint8  length;
};

/* CD Read Feature */
struct Feature_0x001E {
    guint16 code;
    
    #if BIG_ENDIAN_BITFIELD
        guint8  __dummy__   : 2;
        guint8  ver         : 4;
        guint8  per         : 1;
        guint8  cur         : 1;
    #elif LITTLE_ENDIAN_BITFIELD
        guint8  cur         : 1;
        guint8  per         : 1;
        guint8  ver         : 4;
        guint8  __dummy1__  : 2;
    #endif
    
    guint8  length;
    
    #if BIG_ENDIAN_BITFIELD
        guint8  dap         : 1;
        guint8  __dummy2__  : 5;
        guint8  c2flags     : 1;
        guint8  cdtext      : 1;
    #elif LITTLE_ENDIAN_BITFIELD
        guint8  cdtext      : 1;
        guint8  c2flags     : 1;
        guint8  __dummy2__  : 5;
        guint8  dap         : 1;
    #endif

    guint8  __dummy3__[3];
};

/* DVD Read Feature */
struct Feature_0x001F {
    guint16 code;
    
    #if BIG_ENDIAN_BITFIELD
        guint8  __dummy__   : 2;
        guint8  ver         : 4;
        guint8  per         : 1;
        guint8  cur         : 1;
    #elif LITTLE_ENDIAN_BITFIELD
        guint8  cur         : 1;
        guint8  per         : 1;
        guint8  ver         : 4;
        guint8  __dummy1__  : 2;
    #endif
    
    guint8  length;
    
    #if BIG_ENDIAN_BITFIELD
        guint8  __dummy2__  : 7;
        guint8  multi110    : 1;
    #elif LITTLE_ENDIAN_BITFIELD
        guint8  multi110    : 1;
        guint8  __dummy2__  : 7;
    #endif

    guint8  __dummy3__;
    
    #if BIG_ENDIAN_BITFIELD
        guint8  __dummy4__  : 7;
        guint8  dualr       : 1;
    #elif LITTLE_ENDIAN_BITFIELD
        guint8  dualr       : 1;
        guint8  __dummy4__  : 7;
    #endif

    guint8  __dummy5__;
};

/* Power Management Feature */
struct Feature_0x0100 {
    guint16 code;
    
    #if BIG_ENDIAN_BITFIELD
        guint8  __dummy__   : 2;
        guint8  ver         : 4;
        guint8  per         : 1;
        guint8  cur         : 1;
    #elif LITTLE_ENDIAN_BITFIELD
        guint8  cur         : 1;
        guint8  per         : 1;
        guint8  ver         : 4;
        guint8  __dummy1__  : 2;
    #endif
    
    guint8  length;
};

/* CD External Audio Play Feature */
struct Feature_0x0103 {
    guint16 code;
    
    #if BIG_ENDIAN_BITFIELD
        guint8  __dummy__   : 2;
        guint8  ver         : 4;
        guint8  per         : 1;
        guint8  cur         : 1;
    #elif LITTLE_ENDIAN_BITFIELD
        guint8  cur         : 1;
        guint8  per         : 1;
        guint8  ver         : 4;
        guint8  __dummy1__  : 2;
    #endif
    
    guint8  length;
    
    #if BIG_ENDIAN_BITFIELD
        guint8  __dummy2__  : 5;
        guint8  scan        : 1;
        guint8  scm         : 1;
        guint8  sv          : 1;
    #elif LITTLE_ENDIAN_BITFIELD
        guint8  sv          : 1;
        guint8  scm         : 1;
        guint8  scan        : 1;
        guint8  __dummy2__  : 5;
    #endif
    
    guint8  __dummy3__;
    
    guint16 vol_lvls;
};

/* DVD CSS Feature */
struct Feature_0x0106 {
    guint16 code;
    
    #if BIG_ENDIAN_BITFIELD
        guint8  __dummy__   : 2;
        guint8  ver         : 4;
        guint8  per         : 1;
        guint8  cur         : 1;
    #elif LITTLE_ENDIAN_BITFIELD
        guint8  cur         : 1;
        guint8  per         : 1;
        guint8  ver         : 4;
        guint8  __dummy1__  : 2;
    #endif
    
    guint8  length;
    
    guint8  __dummy2__[3];
    
    guint8  css_ver;
};

/* Real Time Streaming Feature */
struct Feature_0x0107 {
    guint16 code;
    
    #if BIG_ENDIAN_BITFIELD
        guint8  __dummy__   : 2;
        guint8  ver         : 4;
        guint8  per         : 1;
        guint8  cur         : 1;
    #elif LITTLE_ENDIAN_BITFIELD
        guint8  cur         : 1;
        guint8  per         : 1;
        guint8  ver         : 4;
        guint8  __dummy1__  : 2;
    #endif
    
    guint8  length;
    
    #if BIG_ENDIAN_BITFIELD
        guint8  __dummy2__  : 3;
        guint8  rbcb        : 1;
        guint8  scs         : 1;
        guint8  mp2a        : 1;
        guint8  wspd        : 1;
        guint8  sw          : 1;
    #elif LITTLE_ENDIAN_BITFIELD
        guint8  sw          : 1;
        guint8  wspd        : 1;
        guint8  mp2a        : 1;
        guint8  scs         : 1;
        guint8  rbcb        : 1;
        guint8  __dummy2__  : 3;
    #endif
    
    guint8  __dummy3__[3];
};

/* PROFILES */
#define PROFILE_NONE    0x0000
#define PROFILE_CDROM   0x0008
#define PROFILE_DVDROM  0x0010

#pragma pack()

#endif /* __CDEMUD_MMC_FEATURES_H__ */
