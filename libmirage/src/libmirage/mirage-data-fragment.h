/*
 *  libMirage: DataFragment interface
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

#ifndef __MIRAGE_DATA_FRAGMENT_H__
#define __MIRAGE_DATA_FRAGMENT_H__


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
 *                MirageDataFragment interface                 *
\**********************************************************************/
#define MIRAGE_TYPE_DATA_FRAGMENT                 (mirage_data_fragment_get_type())
#define MIRAGE_DATA_FRAGMENT(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_DATA_FRAGMENT, MirageDataFragment))
#define MIRAGE_IS_DATA_FRAGMENT(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_DATA_FRAGMENT))
#define MIRAGE_DATA_FRAGMENT_GET_INTERFACE(inst)  (G_TYPE_INSTANCE_GET_INTERFACE((inst), MIRAGE_TYPE_DATA_FRAGMENT, MirageDataFragmentInterface))

/**
 * MirageDataFragment:
 *
 * A fragment object that provides access to raw binary data.
 */
typedef struct _MirageDataFragment             MirageDataFragment;
typedef struct _MirageDataFragmentInterface    MirageDataFragmentInterface;

/**
 * MirageDataFragmentInterface:
 * @parent_iface: the parent interface
 * @main_data_set_stream: sets main data stream
 * @main_data_get_filename: retrieves filename of main data file
 * @main_data_set_offset: sets main data file offset
 * @main_data_get_offset: retrieves main data file offset
 * @main_data_set_size: sets main data file sector size
 * @main_data_get_size: retrieves main data file sector size
 * @main_data_set_format: sets main data file format
 * @main_data_get_format: retrieves main data file format
 * @main_data_get_position: calculates position of main data for sector at specified address
 * @subchannel_data_set_stream: sets subchannel data stream
 * @subchannel_data_get_filename: retrieves subchannel data file name
 * @subchannel_data_set_offset: sets subchannel data file offset
 * @subchannel_data_get_offset: retrieves subchannel data file offset
 * @subchannel_data_set_size: sets subchannel data file sector size
 * @subchannel_data_get_size: retrieves subchannel data file sector size
 * @subchannel_data_set_format: sets subchannel data file format
 * @subchannel_data_get_format: retrieves subchannel data file format
 * @subchannel_data_get_position: calculates position of subchannel data for sector at specified address
 *
 * Provides an interface for implementing fragment objects that provide
 * access to raw binary data.
 */
struct _MirageDataFragmentInterface
{
    GTypeInterface parent_iface;

    /* Interface methods */
    void (*main_data_set_stream) (MirageDataFragment *self, GInputStream *stream);
    const gchar *(*main_data_get_filename) (MirageDataFragment *self);
    void (*main_data_set_offset) (MirageDataFragment *self, guint64 offset);
    guint64 (*main_data_get_offset) (MirageDataFragment *self);
    void (*main_data_set_size) (MirageDataFragment *self, gint size);
    gint (*main_data_get_size) (MirageDataFragment *self);
    void (*main_data_set_format) (MirageDataFragment *self, gint format);
    gint (*main_data_get_format) (MirageDataFragment *self);

    guint64 (*main_data_get_position) (MirageDataFragment *self, gint address);

    void (*subchannel_data_set_stream) (MirageDataFragment *self, GInputStream *stream);
    const gchar *(*subchannel_data_get_filename) (MirageDataFragment *self);
    void (*subchannel_data_set_offset) (MirageDataFragment *self, guint64 offset);
    guint64 (*subchannel_data_get_offset) (MirageDataFragment *self);
    void (*subchannel_data_set_size) (MirageDataFragment *self, gint size);
    gint (*subchannel_data_get_size) (MirageDataFragment *self);
    void (*subchannel_data_set_format) (MirageDataFragment *self, gint format);
    gint (*subchannel_data_get_format) (MirageDataFragment *self);

    guint64 (*subchannel_data_get_position) (MirageDataFragment *self, gint address);
};

/* Used by MIRAGE_TYPE_DATA_FRAGMENT */
GType mirage_data_fragment_get_type (void);

/* Main data file */
void mirage_data_fragment_main_data_set_stream (MirageDataFragment *self, GInputStream *stream);
const gchar *mirage_data_fragment_main_data_get_filename (MirageDataFragment *self);
void mirage_data_fragment_main_data_set_offset (MirageDataFragment *self, guint64 offset);
guint64 mirage_data_fragment_main_data_get_offset (MirageDataFragment *self);
void mirage_data_fragment_main_data_set_size (MirageDataFragment *self, gint size);
gint mirage_data_fragment_main_data_get_size (MirageDataFragment *self);
void mirage_data_fragment_main_data_set_format (MirageDataFragment *self, gint format);
gint mirage_data_fragment_main_data_get_format (MirageDataFragment *self);

guint64 mirage_data_fragment_main_data_get_position (MirageDataFragment *self, gint address);

/* Subchannel file */
void mirage_data_fragment_subchannel_data_set_stream (MirageDataFragment *self, GInputStream *stream);
const gchar *mirage_data_fragment_subchannel_data_get_filename (MirageDataFragment *self);
void mirage_data_fragment_subchannel_data_set_offset (MirageDataFragment *self, guint64 offset);
guint64 mirage_data_fragment_subchannel_data_get_offset (MirageDataFragment *self);
void mirage_data_fragment_subchannel_data_set_size (MirageDataFragment *self, gint size);
gint mirage_data_fragment_subchannel_data_get_size (MirageDataFragment *self);
void mirage_data_fragment_subchannel_data_set_format (MirageDataFragment *self, gint format);
gint mirage_data_fragment_subchannel_data_get_format (MirageDataFragment *self);

guint64 mirage_data_fragment_subchannel_data_get_position (MirageDataFragment *self, gint address);

G_END_DECLS

#endif /* __MIRAGE_DATA_FRAGMENT_H__ */
