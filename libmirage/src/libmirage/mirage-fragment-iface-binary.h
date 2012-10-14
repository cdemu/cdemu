/*
 *  libMirage: FragmentIfaceBinary interface
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

#ifndef __MIRAGE_FRAGMENT_IFACE_BINARY_H__
#define __MIRAGE_FRAGMENT_IFACE_BINARY_H__


G_BEGIN_DECLS


/**
 * MirageMainDataFormat:
 * @MIRAGE_MAIN_DATA: binary data
 * @MIRAGE_MAIN_AUDIO: audio data
 * @MIRAGE_MAIN_AUDIO_SWAP: audio data that needs to be swapped
 *
 * <para>
 * Track file data formats.
 * </para>
 **/
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
 * <para>
 * Subchannel file data formats.
 * </para>
 **/
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
 *                MirageFragmentIfaceBinary interface                 *
\**********************************************************************/
#define MIRAGE_TYPE_FRAGMENT_IFACE_BINARY                 (mirage_fragment_iface_binary_get_type())
#define MIRAGE_FRAGMENT_IFACE_BINARY(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FRAGMENT_IFACE_BINARY, MirageFragmentIfaceBinary))
#define MIRAGE_IS_FRAGMENT_IFACE_BINARY(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FRAGMENT_IFACE_BINARY))
#define MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(inst)  (G_TYPE_INSTANCE_GET_INTERFACE((inst), MIRAGE_TYPE_FRAGMENT_IFACE_BINARY, MirageFragmentIfaceBinaryInterface))

/**
 * MirageFragmentIfaceBinary:
 *
 * <para>
 * Dummy interface structure.
 * </para>
 **/
typedef struct _MirageFragmentIfaceBinary             MirageFragmentIfaceBinary;
typedef struct _MirageFragmentIfaceBinaryInterface    MirageFragmentIfaceBinaryInterface;

struct _MirageFragmentIfaceBinaryInterface
{
    GTypeInterface parent_iface;

    /* Interface methods */
    void (*main_data_set_stream) (MirageFragmentIfaceBinary *self, GInputStream *stream);
    const gchar *(*main_data_get_filename) (MirageFragmentIfaceBinary *self);
    void (*main_data_set_offset) (MirageFragmentIfaceBinary *self, guint64 offset);
    guint64 (*main_data_get_offset) (MirageFragmentIfaceBinary *self);
    void (*main_data_set_size) (MirageFragmentIfaceBinary *self, gint size);
    gint (*main_data_get_size) (MirageFragmentIfaceBinary *self);
    void (*main_data_set_format) (MirageFragmentIfaceBinary *self, gint format);
    gint (*main_data_get_format) (MirageFragmentIfaceBinary *self);

    guint64 (*main_data_get_position) (MirageFragmentIfaceBinary *self, gint address);

    void (*subchannel_data_set_stream) (MirageFragmentIfaceBinary *self, GInputStream *stream);
    const gchar *(*subchannel_data_get_filename) (MirageFragmentIfaceBinary *self);
    void (*subchannel_data_set_offset) (MirageFragmentIfaceBinary *self, guint64 offset);
    guint64 (*subchannel_data_get_offset) (MirageFragmentIfaceBinary *self);
    void (*subchannel_data_set_size) (MirageFragmentIfaceBinary *self, gint size);
    gint (*subchannel_data_get_size) (MirageFragmentIfaceBinary *self);
    void (*subchannel_data_set_format) (MirageFragmentIfaceBinary *self, gint format);
    gint (*subchannel_data_get_format) (MirageFragmentIfaceBinary *self);

    guint64 (*subchannel_data_get_position) (MirageFragmentIfaceBinary *self, gint address);
};

/* Used by MIRAGE_TYPE_FRAGMENT_IFACE_BINARY */
GType mirage_fragment_iface_binary_get_type (void);

/* Main data file */
void mirage_fragment_iface_binary_main_data_set_stream (MirageFragmentIfaceBinary *self, GInputStream *stream);
const gchar *mirage_fragment_iface_binary_main_data_get_filename (MirageFragmentIfaceBinary *self);
void mirage_fragment_iface_binary_main_data_set_offset (MirageFragmentIfaceBinary *self, guint64 offset);
guint64 mirage_fragment_iface_binary_main_data_get_offset (MirageFragmentIfaceBinary *self);
void mirage_fragment_iface_binary_main_data_set_size (MirageFragmentIfaceBinary *self, gint size);
gint mirage_fragment_iface_binary_main_data_get_size (MirageFragmentIfaceBinary *self);
void mirage_fragment_iface_binary_main_data_set_format (MirageFragmentIfaceBinary *self, gint format);
gint mirage_fragment_iface_binary_main_data_get_format (MirageFragmentIfaceBinary *self);

guint64 mirage_fragment_iface_binary_main_data_get_position (MirageFragmentIfaceBinary *self, gint address);

/* Subchannel file */
void mirage_fragment_iface_binary_subchannel_data_set_stream (MirageFragmentIfaceBinary *self, GInputStream *stream);
const gchar *mirage_fragment_iface_binary_subchannel_data_get_filename (MirageFragmentIfaceBinary *self);
void mirage_fragment_iface_binary_subchannel_data_set_offset (MirageFragmentIfaceBinary *self, guint64 offset);
guint64 mirage_fragment_iface_binary_subchannel_data_get_offset (MirageFragmentIfaceBinary *self);
void mirage_fragment_iface_binary_subchannel_data_set_size (MirageFragmentIfaceBinary *self, gint size);
gint mirage_fragment_iface_binary_subchannel_data_get_size (MirageFragmentIfaceBinary *self);
void mirage_fragment_iface_binary_subchannel_data_set_format (MirageFragmentIfaceBinary *self, gint format);
gint mirage_fragment_iface_binary_subchannel_data_get_format (MirageFragmentIfaceBinary *self);

guint64 mirage_fragment_iface_binary_subchannel_data_get_position (MirageFragmentIfaceBinary *self, gint address);

G_END_DECLS

#endif /* __MIRAGE_FRAGMENT_IFACE_BINARY_H__ */
