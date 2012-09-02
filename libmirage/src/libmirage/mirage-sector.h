/*
 *  libMirage: Sector object
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

#ifndef __MIRAGE_SECTOR_H__
#define __MIRAGE_SECTOR_H__


G_BEGIN_DECLS

/**
 * MIRAGE_Sector_MCSB:
 * @MIRAGE_MCSB_SYNC: sync pattern
 * @MIRAGE_MCSB_SUBHEADER: subheader
 * @MIRAGE_MCSB_HEADER: header
 * @MIRAGE_MCSB_DATA: user data
 * @MIRAGE_MCSB_EDC_ECC: EDC/ECC code
 * @MIRAGE_MCSB_C2_2: C2 layer 2 error
 * @MIRAGE_MCSB_C2_1: C2 layer 1 error
 *
 * <para>
 * Main channel selection flags.
 * </para>
 **/
typedef enum
{
    MIRAGE_MCSB_SYNC      = 0x80,
    MIRAGE_MCSB_SUBHEADER = 0x40,
    MIRAGE_MCSB_HEADER    = 0x20,
    MIRAGE_MCSB_DATA      = 0x10,
    MIRAGE_MCSB_EDC_ECC   = 0x08,
    MIRAGE_MCSB_C2_2      = 0x04,
    MIRAGE_MCSB_C2_1      = 0x02,
} MIRAGE_Sector_MCSB;

/**
 * MIRAGE_Sector_SubchannelFormat:
 * @MIRAGE_SUBCHANNEL_PW: PW subchannel; 96 bytes, interleaved P-W
 * @MIRAGE_SUBCHANNEL_PQ: PQ subchannel; 16 bytes, Q subchannel
 * @MIRAGE_SUBCHANNEL_RW: RW subchannel; 96 bytes, deinterleaved R-W
 *
 * <para>
 * Subchannel selection flags.
 * </para>
 **/
typedef enum
{
    MIRAGE_SUBCHANNEL_PW = 0x01,
    MIRAGE_SUBCHANNEL_PQ = 0x02,
    MIRAGE_SUBCHANNEL_RW = 0x03
} MIRAGE_Sector_SubchannelFormat;

/**
 * MIRAGE_Sector_ValidData:
 * @MIRAGE_VALID_SYNC: sync pattern valid
 * @MIRAGE_VALID_HEADER: header valid
 * @MIRAGE_VALID_SUBHEADER: subheader valid
 * @MIRAGE_VALID_DATA: user data valid
 * @MIRAGE_VALID_EDC_ECC: EDC/ECC data valid
 * @MIRAGE_VALID_SUBCHAN: subchannel valid
 *
 * <para>
 * Sector data validity flags.
 * </para>
 **/
typedef enum
{
    MIRAGE_VALID_SYNC      = 0x01,
    MIRAGE_VALID_HEADER    = 0x02,
    MIRAGE_VALID_SUBHEADER = 0x04,
    MIRAGE_VALID_DATA      = 0x08,
    MIRAGE_VALID_EDC_ECC   = 0x10,
    MIRAGE_VALID_SUBCHAN   = 0x20,
} MIRAGE_Sector_ValidData;


/**
 * mirage_sync_pattern:
 *
 * <para>
 * A 12-byte sync pattern, found at the beginning of non-audio sectors.
 * </para>
 **/
extern const guint8 mirage_sync_pattern[12];


/******************************************************************************\
 *                              Base sector class                             *
\******************************************************************************/
#define MIRAGE_TYPE_SECTOR            (mirage_sector_get_type())
#define MIRAGE_SECTOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_SECTOR, MIRAGE_Sector))
#define MIRAGE_SECTOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_SECTOR, MIRAGE_SectorClass))
#define MIRAGE_IS_SECTOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_SECTOR))
#define MIRAGE_IS_SECTOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_SECTOR))
#define MIRAGE_SECTOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_SECTOR, MIRAGE_SectorClass))

typedef struct _MIRAGE_Sector           MIRAGE_Sector;
typedef struct _MIRAGE_SectorClass      MIRAGE_SectorClass;
typedef struct _MIRAGE_SectorPrivate    MIRAGE_SectorPrivate;

/**
 * MIRAGE_Sector:
 *
 * <para>
 * Contains private data only, and should be accessed using the functions below.
 * </para>
 **/
struct _MIRAGE_Sector
{
    MIRAGE_Object parent_instance;

    /*< private >*/
    MIRAGE_SectorPrivate *priv;
};

struct _MIRAGE_SectorClass
{
    MIRAGE_ObjectClass parent_class;
} ;

/* Used by MIRAGE_TYPE_SECTOR */
GType mirage_sector_get_type (void);


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
gboolean mirage_sector_feed_data (MIRAGE_Sector *self, gint address, GObject *track, GError **error);

gint mirage_sector_get_sector_type (MIRAGE_Sector *self);

gboolean mirage_sector_get_sync (MIRAGE_Sector *self, const guint8 **ret_buf, gint *ret_len, GError **error);
gboolean mirage_sector_get_header (MIRAGE_Sector *self, const guint8 **ret_buf, gint *ret_len, GError **error);
gboolean mirage_sector_get_subheader (MIRAGE_Sector *self, const guint8 **ret_buf, gint *ret_len, GError **error);
gboolean mirage_sector_get_data (MIRAGE_Sector *self, const guint8 **ret_buf, gint *ret_len, GError **error);
gboolean mirage_sector_get_edc_ecc (MIRAGE_Sector *self, const guint8 **ret_buf, gint *ret_len, GError **error);
gboolean mirage_sector_get_subchannel (MIRAGE_Sector *self, gint format, const guint8 **ret_buf, gint *ret_len, GError **error);

gboolean mirage_sector_verify_lec (MIRAGE_Sector *self);
gboolean mirage_sector_verify_subchannel_crc (MIRAGE_Sector *self);

G_END_DECLS

#endif /* __MIRAGE_SECTOR_H__ */
