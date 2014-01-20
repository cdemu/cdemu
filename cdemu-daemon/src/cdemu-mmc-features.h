/*
 *  CDEmuD: MMC-3 device features definitions
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __CDEMUD_MMC_FEATURES_H__
#define __CDEMUD_MMC_FEATURES_H__

#pragma pack(1)

/* FEATURES */
struct FeatureGeneral
{
    guint16 code;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved1 : 2;
        guint8 ver       : 4;
        guint8 per       : 1;
        guint8 cur       : 1;
    #else
        guint8 cur       : 1;
        guint8 per       : 1;
        guint8 ver       : 4;
        guint8 reserved1 : 2;
    #endif

    guint8 length;
};

struct Profile
{
    guint16 profile;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved1 : 2;
        guint8 ver       : 4;
        guint8 per       : 1;
        guint8 cur       : 1;
    #else
        guint8 cur       : 1;
        guint8 per       : 1;
        guint8 ver       : 4;
        guint8 reserved1 : 2;
    #endif

    guint8 reserved2;
};

typedef enum {
    ProfileIndex_NONE = -1,
    ProfileIndex_CDROM,
    ProfileIndex_CDR,
    ProfileIndex_DVDROM,
    ProfileIndex_DVDPLUSR,
    NumProfiles
} ProfileIndex;

/* Profile List */
struct Feature_0x0000
{
    guint16 code;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved1 : 2;
        guint8 ver       : 4;
        guint8 per       : 1;
        guint8 cur       : 1;
    #else
        guint8 cur       : 1;
        guint8 per       : 1;
        guint8 ver       : 4;
        guint8 reserved1 : 2;
    #endif

    guint8 length;

    /* We support several profiles, whose indices are declared in the
       enum above */
    struct Profile profiles[NumProfiles];
};


/* Core Feature */
struct Feature_0x0001
{
    guint16 code;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved1 : 2;
        guint8 ver       : 4;
        guint8 per       : 1;
        guint8 cur       : 1;
    #else
        guint8 cur       : 1;
        guint8 per       : 1;
        guint8 ver       : 4;
        guint8 reserved1 : 2;
    #endif

    guint8 length;

    guint32 interface;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved2 : 6;
        guint8 inq2      : 1;
        guint8 dbevent   : 1;
    #else
        guint8 dbevent   : 1;
        guint8 inq2      : 1;
        guint8 reserved2 : 6;
    #endif

    guint8 reserved3[3];
};

/* Morphing Feature */
struct Feature_0x0002
{
    guint16 code;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved1 : 2;
        guint8 ver       : 4;
        guint8 per       : 1;
        guint8 cur       : 1;
    #else
        guint8 cur       : 1;
        guint8 per       : 1;
        guint8 ver       : 4;
        guint8 reserved1 : 2;
    #endif

    guint8 length;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved2 : 6;
        guint8 ocevent   : 1;
        guint8 async     : 1;
    #else
        guint8 async     : 1;
        guint8 ocevent   : 1;
        guint8 reserved2 : 6;
    #endif

    guint8 reserved3[3];
};

/* Removable Medium Feature */
struct Feature_0x0003
{
    guint16 code;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved1 : 2;
        guint8 ver       : 4;
        guint8 per       : 1;
        guint8 cur       : 1;
    #else
        guint8 cur       : 1;
        guint8 per       : 1;
        guint8 ver       : 4;
        guint8 reserved1 : 2;
    #endif

    guint8 length;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 mechanism : 3;
        guint8 reserved2 : 1;
        guint8 eject     : 1;
        guint8 prvnt_jmp : 1;
        guint8 reserved3 : 1;
        guint8 lock      : 1;
    #else
        guint8 lock      : 1;
        guint8 reserved3 : 1;
        guint8 prvnt_jmp : 1;
        guint8 eject     : 1;
        guint8 reserved2 : 1;
        guint8 mechanism : 3;
    #endif

    guint8 reserved4[3];
};

/* Random Readable Feature */
struct Feature_0x0010
{
    guint16 code;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved1 : 2;
        guint8 ver       : 4;
        guint8 per       : 1;
        guint8 cur       : 1;
    #else
        guint8 cur       : 1;
        guint8 per       : 1;
        guint8 ver       : 4;
        guint8 reserved1 : 2;
    #endif

    guint8 length;

    guint32 block_size;
    guint16 blocking;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved2 : 7;
        guint8 pp        : 1;
    #else
        guint8 pp        : 1;
        guint8 reserved2 : 7;
    #endif

    guint8 reserved3;
};

/* Multi-read Feature */
struct Feature_0x001D
{
    guint16 code;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved1 : 2;
        guint8 ver       : 4;
        guint8 per       : 1;
        guint8 cur       : 1;
    #else
        guint8 cur       : 1;
        guint8 per       : 1;
        guint8 ver       : 4;
        guint8 reserved1 : 2;
    #endif

    guint8 length;
};

/* CD Read Feature */
struct Feature_0x001E
{
    guint16 code;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved1 : 2;
        guint8 ver       : 4;
        guint8 per       : 1;
        guint8 cur       : 1;
    #else
        guint8 cur       : 1;
        guint8 per       : 1;
        guint8 ver       : 4;
        guint8 reserved1 : 2;
    #endif

    guint8 length;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 dap       : 1;
        guint8 reserved2 : 5;
        guint8 c2flags   : 1;
        guint8 cdtext    : 1;
    #else
        guint8 cdtext    : 1;
        guint8 c2flags   : 1;
        guint8 reserved2 : 5;
        guint8 dap       : 1;
    #endif

    guint8 reserved3[3];
};

/* DVD Read Feature */
struct Feature_0x001F
{
    guint16 code;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved1 : 2;
        guint8 ver       : 4;
        guint8 per       : 1;
        guint8 cur       : 1;
    #else
        guint8 cur       : 1;
        guint8 per       : 1;
        guint8 ver       : 4;
        guint8 reserved1 : 2;
    #endif

    guint8 length;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved2 : 7;
        guint8 multi110  : 1;
    #else
        guint8 multi110  : 1;
        guint8 reserved2 : 7;
    #endif

    guint8 reserved3;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved4 : 7;
        guint8 dualr     : 1;
    #else
        guint8 dualr     : 1;
        guint8 reserved4 : 7;
    #endif

    guint8 reserved5;
};

/* Incremental Streaming Writable Feature */
struct Feature_0x0021
{
    guint16 code;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved1 : 2;
        guint8 ver       : 4;
        guint8 per       : 1;
        guint8 cur       : 1;
    #else
        guint8 cur       : 1;
        guint8 per       : 1;
        guint8 ver       : 4;
        guint8 reserved1 : 2;
    #endif

    guint8 length;

    guint16 data_block_types_supported;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved2 : 7;
        guint8 buf       : 1;
    #else
        guint8 buf       : 1;
        guint8 reserved2 : 7;
    #endif

    guint8 num_link_sizes;

    guint8 link_sizes[1];
    guint8 pad[3];
};


/* DVD+R Feature */
struct Feature_0x002B
{
    guint16 code;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved1 : 2;
        guint8 ver       : 4;
        guint8 per       : 1;
        guint8 cur       : 1;
    #else
        guint8 cur       : 1;
        guint8 per       : 1;
        guint8 ver       : 4;
        guint8 reserved1 : 2;
    #endif

    guint8 length;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved2  : 7;
        guint8 write      : 1;
    #else
        guint8 write      : 1;
        guint8 reserved2  : 7;
    #endif

    guint8 reserved3[3];
};


/* CD Track at Once Feature */
struct Feature_0x002D
{
    guint16 code;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved1 : 2;
        guint8 ver       : 4;
        guint8 per       : 1;
        guint8 cur       : 1;
    #else
        guint8 cur       : 1;
        guint8 per       : 1;
        guint8 ver       : 4;
        guint8 reserved1 : 2;
    #endif

    guint8 length;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved2  : 1;
        guint8 buf        : 1;
        guint8 reserved3  : 1;
        guint8 rw_raw     : 1;
        guint8 rw_pack    : 1;
        guint8 test_write : 1;
        guint8 cd_rw      : 1;
        guint8 rw_subcode : 1;
    #else
        guint8 rw_subcode : 1;
        guint8 cd_rw      : 1;
        guint8 test_write : 1;
        guint8 rw_pack    : 1;
        guint8 rw_raw     : 1;
        guint8 reserved3  : 1;
        guint8 buf        : 1;
        guint8 reserved2  : 1;
    #endif

    guint8  reserved4;

    guint16 data_type_supported;
};

/* Power Management Feature */
struct Feature_0x0100
{
    guint16 code;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved1 : 2;
        guint8 ver       : 4;
        guint8 per       : 1;
        guint8 cur       : 1;
    #else
        guint8 cur       : 1;
        guint8 per       : 1;
        guint8 ver       : 4;
        guint8 reserved1 : 2;
    #endif

    guint8 length;
};

/* CD External Audio Play Feature */
struct Feature_0x0103
{
    guint16 code;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved1 : 2;
        guint8 ver       : 4;
        guint8 per       : 1;
        guint8 cur       : 1;
    #else
        guint8 cur       : 1;
        guint8 per       : 1;
        guint8 ver       : 4;
        guint8 reserved1 : 2;
    #endif

    guint8 length;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved2 : 5;
        guint8 scan      : 1;
        guint8 scm       : 1;
        guint8 sv        : 1;
    #else
        guint8 sv        : 1;
        guint8 scm       : 1;
        guint8 scan      : 1;
        guint8 reserved2 : 5;
    #endif

    guint8 reserved3;

    guint16 vol_lvls;
};

/* DVD CSS Feature */
struct Feature_0x0106
{
    guint16 code;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved1 : 2;
        guint8 ver       : 4;
        guint8 per       : 1;
        guint8 cur       : 1;
    #else
        guint8 cur       : 1;
        guint8 per       : 1;
        guint8 ver       : 4;
        guint8 reserved1 : 2;
    #endif

    guint8 length;

    guint8 reserved2[3];

    guint8 css_ver;
};

/* Real Time Streaming Feature */
struct Feature_0x0107
{
    guint16 code;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved1 : 2;
        guint8 ver       : 4;
        guint8 per       : 1;
        guint8 cur       : 1;
    #else
        guint8 cur       : 1;
        guint8 per       : 1;
        guint8 ver       : 4;
        guint8 reserved1 : 2;
    #endif

    guint8 length;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved2 : 3;
        guint8 rbcb      : 1;
        guint8 scs       : 1;
        guint8 mp2a      : 1;
        guint8 wspd      : 1;
        guint8 sw        : 1;
    #else
        guint8 sw        : 1;
        guint8 wspd      : 1;
        guint8 mp2a      : 1;
        guint8 scs       : 1;
        guint8 rbcb      : 1;
        guint8 reserved2 : 3;
    #endif

    guint8 reserved3[3];
};

/* Disc Control Blocks Feature */
struct Feature_0x010A
{
    guint16 code;

    #if G_BYTE_ORDER == G_BIG_ENDIAN
        guint8 reserved1 : 2;
        guint8 ver       : 4;
        guint8 per       : 1;
        guint8 cur       : 1;
    #else
        guint8 cur       : 1;
        guint8 per       : 1;
        guint8 ver       : 4;
        guint8 reserved1 : 2;
    #endif

    guint8 length;
};


/* Profile codes */
typedef enum {
    PROFILE_NONE = 0x0000,
    PROFILE_CDROM = 0x0008,
    PROFILE_CDR = 0x0009,
    PROFILE_DVDROM = 0x0010,
    PROFILE_DVDPLUSR = 0x001B,
} ProfileCode;

#pragma pack()

#endif /* __CDEMUD_MMC_FEATURES_H__ */
