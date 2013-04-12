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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __MIRAGE_SECTOR_H__
#define __MIRAGE_SECTOR_H__

#include "mirage-types.h"


G_BEGIN_DECLS

/**
 * MirageSectorSubchannelFormat:
 * @MIRAGE_SUBCHANNEL_PW: PW subchannel; 96 bytes, interleaved P-W
 * @MIRAGE_SUBCHANNEL_PQ: PQ subchannel; 16 bytes, Q subchannel
 * @MIRAGE_SUBCHANNEL_RW: RW subchannel; 96 bytes, deinterleaved R-W
 *
 * Subchannel selection flags.
 */
typedef enum _MirageSectorSubchannelFormat
{
    MIRAGE_SUBCHANNEL_PW = 0x01,
    MIRAGE_SUBCHANNEL_PQ = 0x02,
    MIRAGE_SUBCHANNEL_RW = 0x03
} MirageSectorSubchannelFormat;

/**
 * MirageSectorValidData:
 * @MIRAGE_VALID_SYNC: sync pattern valid
 * @MIRAGE_VALID_HEADER: header valid
 * @MIRAGE_VALID_SUBHEADER: subheader valid
 * @MIRAGE_VALID_DATA: user data valid
 * @MIRAGE_VALID_EDC_ECC: EDC/ECC data valid
 * @MIRAGE_VALID_SUBCHAN: subchannel valid
 *
 * Sector data validity flags.
 */
typedef enum _MirageSectorValidData
{
    MIRAGE_VALID_SYNC      = 0x01,
    MIRAGE_VALID_HEADER    = 0x02,
    MIRAGE_VALID_SUBHEADER = 0x04,
    MIRAGE_VALID_DATA      = 0x08,
    MIRAGE_VALID_EDC_ECC   = 0x10,
    MIRAGE_VALID_SUBCHAN   = 0x20,
} MirageSectorValidData;


/**********************************************************************\
 *                         MirageSector object                        *
\**********************************************************************/
#define MIRAGE_TYPE_SECTOR            (mirage_sector_get_type())
#define MIRAGE_SECTOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_SECTOR, MirageSector))
#define MIRAGE_SECTOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_SECTOR, MirageSectorClass))
#define MIRAGE_IS_SECTOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_SECTOR))
#define MIRAGE_IS_SECTOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_SECTOR))
#define MIRAGE_SECTOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_SECTOR, MirageSectorClass))

typedef struct _MirageSectorClass      MirageSectorClass;
typedef struct _MirageSectorPrivate    MirageSectorPrivate;

/**
 * MirageSector:
 *
 * All the fields in the <structname>MirageSector</structname>
 * structure are private to the #MirageSector implementation and
 * should never be accessed directly.
 */
struct _MirageSector
{
    MirageObject parent_instance;

    /*< private >*/
    MirageSectorPrivate *priv;
};

/**
 * MirageSectorClass:
 * @parent_class: the parent class
 *
 * The class structure for the <structname>MirageSector</structname> type.
 */
struct _MirageSectorClass
{
    MirageObjectClass parent_class;
};

/* Used by MIRAGE_TYPE_SECTOR */
GType mirage_sector_get_type (void);

/* Public API */
gboolean mirage_sector_feed_data (MirageSector *self, gint address, MirageTrack *track, GError **error);

MirageTrackModes mirage_sector_get_sector_type (MirageSector *self);

gboolean mirage_sector_get_sync (MirageSector *self, const guint8 **ret_buf, gint *ret_len, GError **error);
gboolean mirage_sector_get_header (MirageSector *self, const guint8 **ret_buf, gint *ret_len, GError **error);
gboolean mirage_sector_get_subheader (MirageSector *self, const guint8 **ret_buf, gint *ret_len, GError **error);
gboolean mirage_sector_get_data (MirageSector *self, const guint8 **ret_buf, gint *ret_len, GError **error);
gboolean mirage_sector_get_edc_ecc (MirageSector *self, const guint8 **ret_buf, gint *ret_len, GError **error);
gboolean mirage_sector_get_subchannel (MirageSector *self, MirageSectorSubchannelFormat format, const guint8 **ret_buf, gint *ret_len, GError **error);

gboolean mirage_sector_verify_lec (MirageSector *self);
gboolean mirage_sector_verify_subchannel_crc (MirageSector *self);

G_END_DECLS

#endif /* __MIRAGE_SECTOR_H__ */
