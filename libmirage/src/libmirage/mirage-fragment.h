/*
 *  libMirage: Fragment object
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

#ifndef __MIRAGE_FRAGMENT_H__
#define __MIRAGE_FRAGMENT_H__

#include "mirage-types.h"


G_BEGIN_DECLS

/**
 * MirageMainDataFormat:
 * @MIRAGE_MAIN_DATA: binary data
 * @MIRAGE_MAIN_AUDIO: audio data
 * @MIRAGE_MAIN_AUDIO_SWAP: audio data that needs to be swapped
 *
 * Track file data formats.
 */
typedef enum _MirageMainDataFormat
{
    MIRAGE_MAIN_DATA  = 0x01,
    MIRAGE_MAIN_AUDIO = 0x02,
    MIRAGE_MAIN_AUDIO_SWAP = 0x04,
} MirageMainDataFormat;

/**
 * MirageSubchannelDataFormat:
 * @MIRAGE_SUBCHANNEL_INT: internal subchannel (i.e. included in track file)
 * @MIRAGE_SUBCHANNEL_EXT: external subchannel (i.e. provided by separate file)
 * @MIRAGE_SUBCHANNEL_PW96_INT: P-W subchannel, 96 bytes, interleaved
 * @MIRAGE_SUBCHANNEL_PW96_LIN: P-W subchannel, 96 bytes, linear
 * @MIRAGE_SUBCHANNEL_RW96: R-W subchannel, 96 bytes, deinterleaved
 * @MIRAGE_SUBCHANNEL_PQ16: PQ subchannel, 16 bytes
 *
 * Subchannel file data formats.
 */
typedef enum _MirageSubchannelDataFormat
{
    MIRAGE_SUBCHANNEL_INT = 0x01,
    MIRAGE_SUBCHANNEL_EXT = 0x02,

    MIRAGE_SUBCHANNEL_PW96_INT = 0x10,
    MIRAGE_SUBCHANNEL_PW96_LIN = 0x20,
    MIRAGE_SUBCHANNEL_RW96     = 0x40,
    MIRAGE_SUBCHANNEL_PQ16     = 0x80,
} MirageSubchannelDataFormat;


/**********************************************************************\
 *                        MirageFragment object                       *
\**********************************************************************/
#define MIRAGE_TYPE_FRAGMENT            (mirage_fragment_get_type())
#define MIRAGE_FRAGMENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FRAGMENT, MirageFragment))
#define MIRAGE_FRAGMENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_FRAGMENT, MirageFragmentClass))
#define MIRAGE_IS_FRAGMENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FRAGMENT))
#define MIRAGE_IS_FRAGMENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_FRAGMENT))
#define MIRAGE_FRAGMENT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_FRAGMENT, MirageFragmentClass))

typedef struct _MirageFragmentClass    MirageFragmentClass;
typedef struct _MirageFragmentPrivate  MirageFragmentPrivate;

/**
 * MirageFragment:
 *
 * All the fields in the <structname>MirageFragment</structname>
 * structure are private to the #MirageFragment implementation and
 * should never be accessed directly.
 */
struct _MirageFragment
{
    MirageObject parent_instance;

    /*< private >*/
    MirageFragmentPrivate *priv;
};

/**
 * MirageFragmentClass:
 * @parent_class: the parent class
 *
 * The class structure for the <structname>MirageFragment</structname> type.
 */
struct _MirageFragmentClass
{
    MirageObjectClass parent_class;
};

/* Used by MIRAGE_TYPE_FRAGMENT */
GType mirage_fragment_get_type (void);

/* Address/length */
void mirage_fragment_set_address (MirageFragment *self, gint address);
gint mirage_fragment_get_address (MirageFragment *self);

void mirage_fragment_set_length (MirageFragment *self, gint length);
gint mirage_fragment_get_length (MirageFragment *self);

gboolean mirage_fragment_use_the_rest_of_file (MirageFragment *self, GError **error);

/* Main data */
void mirage_fragment_main_data_set_stream (MirageFragment *self, GInputStream *stream);
const gchar *mirage_fragment_main_data_get_filename (MirageFragment *self);
void mirage_fragment_main_data_set_offset (MirageFragment *self, guint64 offset);
guint64 mirage_fragment_main_data_get_offset (MirageFragment *self);
void mirage_fragment_main_data_set_size (MirageFragment *self, gint size);
gint mirage_fragment_main_data_get_size (MirageFragment *self);
void mirage_fragment_main_data_set_format (MirageFragment *self, gint format);
gint mirage_fragment_main_data_get_format (MirageFragment *self);

gboolean mirage_fragment_read_main_data (MirageFragment *self, gint address, guint8 **buffer, gint *length, GError **error);

/* Subchannel */
void mirage_fragment_subchannel_data_set_stream (MirageFragment *self, GInputStream *stream);
const gchar *mirage_fragment_subchannel_data_get_filename (MirageFragment *self);
void mirage_fragment_subchannel_data_set_offset (MirageFragment *self, guint64 offset);
guint64 mirage_fragment_subchannel_data_get_offset (MirageFragment *self);
void mirage_fragment_subchannel_data_set_size (MirageFragment *self, gint size);
gint mirage_fragment_subchannel_data_get_size (MirageFragment *self);
void mirage_fragment_subchannel_data_set_format (MirageFragment *self, gint format);
gint mirage_fragment_subchannel_data_get_format (MirageFragment *self);

gboolean mirage_fragment_read_subchannel_data (MirageFragment *self, gint address, guint8 **buffer, gint *length, GError **error);


G_END_DECLS

#endif /* __MIRAGE_FRAGMENT_H__ */
