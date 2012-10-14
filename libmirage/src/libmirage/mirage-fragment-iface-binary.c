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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"

#define __debug__ "Fragment"


/**********************************************************************\
 *                            Main data file                          *
\**********************************************************************/
/**
 * mirage_fragment_iface_binary_main_data_set_stream:
 * @self: a #MirageFragmentIfaceBinary
 * @stream: (in) (transfer full): a #GInputStream on main data file
 *
 * <para>
 * Sets main data stream.
 * </para>
 **/
void mirage_fragment_iface_binary_main_data_set_stream (MirageFragmentIfaceBinary *self, GInputStream *stream)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->main_data_set_stream(self, stream);
}

/**
 * mirage_fragment_iface_binary_main_data_get_filename:
 * @self: a #MirageFragmentIfaceBinary
 *
 * <para>
 * Retrieves filename of main data file.
 * </para>
 *
 * Returns: (transfer none): pointer to main data file name string.
 * The string belongs to object and should not be modified.
 **/
const gchar *mirage_fragment_iface_binary_main_data_get_filename (MirageFragmentIfaceBinary *self)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->main_data_get_filename(self);
}

/**
 * mirage_fragment_iface_binary_main_data_set_offset:
 * @self: a #MirageFragmentIfaceBinary
 * @offset: (in): main data file offset
 *
 * <para>
 * Sets main data file offset.
 * </para>
 **/
void mirage_fragment_iface_binary_main_data_set_offset (MirageFragmentIfaceBinary *self, guint64 offset)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->main_data_set_offset(self, offset);
}

/**
 * mirage_fragment_iface_binary_main_data_get_offset:
 * @self: a #MirageFragmentIfaceBinary
 *
 * <para>
 * Retrieves main data file offset.
 * </para>
 *
 * Returns: main data file offset
 **/
guint64 mirage_fragment_iface_binary_main_data_get_offset (MirageFragmentIfaceBinary *self)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->main_data_get_offset(self);
}


/**
 * mirage_fragment_iface_binary_main_data_set_size:
 * @self: a #MirageFragmentIfaceBinary
 * @size: (in): main data file sector size
 *
 * <para>
 * Sets main data file sector size.
 * </para>
 **/
void mirage_fragment_iface_binary_main_data_set_size (MirageFragmentIfaceBinary *self, gint size)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->main_data_set_size(self, size);
}

/**
 * mirage_fragment_iface_binary_main_data_get_size:
 * @self: a #MirageFragmentIfaceBinary
 *
 * <para>
 * Retrieves main data file sector size.
 * </para>
 *
 * Returns: main data file sector size
 **/
gint mirage_fragment_iface_binary_main_data_get_size (MirageFragmentIfaceBinary *self)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->main_data_get_size(self);
}


/**
 * mirage_fragment_iface_binary_main_data_set_format:
 * @self: a #MirageFragmentIfaceBinary
 * @format: (in): main data file format
 *
 * <para>
 * Sets main data file format. @format must be one of #MirageMainDataFormat.
 * </para>
 **/
void mirage_fragment_iface_binary_main_data_set_format (MirageFragmentIfaceBinary *self, gint format)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->main_data_set_format(self, format);
}

/**
 * mirage_fragment_iface_binary_main_data_get_format:
 * @self: a #MirageFragmentIfaceBinary
 *
 * <para>
 * Retrieves main data file format.
 * </para>
 *
 * Returns: main data file format
 **/
gint mirage_fragment_iface_binary_main_data_get_format (MirageFragmentIfaceBinary *self)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->main_data_get_format(self);
}


/**
 * mirage_fragment_iface_binary_main_data_get_position:
 * @self: a #MirageFragmentIfaceBinary
 * @address: (in): address
 *
 * <para>
 * Calculates position of data for sector at address @address within
 * main data file and stores it in @position.
 * </para>
 *
 * Returns: position in main data file
 **/
guint64 mirage_fragment_iface_binary_main_data_get_position (MirageFragmentIfaceBinary *self, gint address)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->main_data_get_position(self, address);
}


/**********************************************************************\
 *                        Subchannel data file                        *
\**********************************************************************/
/**
 * mirage_fragment_iface_binary_subchannel_data_set_stream:
 * @self: a #MirageFragmentIfaceBinary
 * @stream: (in) (transfer full): a #GInputStream on subchannel data file
 *
 * <para>
 * Sets subchannel data stream.
 * </para>
 **/
void mirage_fragment_iface_binary_subchannel_data_set_stream (MirageFragmentIfaceBinary *self, GInputStream *stream)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->subchannel_data_set_stream(self, stream);
}

/**
 * mirage_fragment_iface_binary_subchannel_data_get_filename:
 * @self: a #MirageFragmentIfaceBinary
 *
 * <para>
 * Retrieves subchannel data file name.
 * </para>
 *
 * Returns: (transfer none): pointer to subchannel data file name string.
 * The string belongs to object and should not be modified.
 **/
const gchar *mirage_fragment_iface_binary_subchannel_data_get_filename (MirageFragmentIfaceBinary *self)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->subchannel_data_get_filename(self);
}


/**
 * mirage_fragment_iface_binary_subchannel_data_set_offset:
 * @self: a #MirageFragmentIfaceBinary
 * @offset: (in): subchannel data file offset
 *
 * <para>
 * Sets subchannel data file offset.
 * </para>
 **/
void mirage_fragment_iface_binary_subchannel_data_set_offset (MirageFragmentIfaceBinary *self, guint64 offset)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->subchannel_data_set_offset(self, offset);
}

/**
 * mirage_fragment_iface_binary_subchannel_data_get_offset:
 * @self: a #MirageFragmentIfaceBinary
 *
 * <para>
 * Retrieves subchannel data file offset.
 * </para>
 *
 * Returns: subchannel data file offset
 **/
guint64 mirage_fragment_iface_binary_subchannel_data_get_offset (MirageFragmentIfaceBinary *self)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->subchannel_data_get_offset(self);
}

/**
 * mirage_fragment_iface_binary_subchannel_data_set_size:
 * @self: a #MirageFragmentIfaceBinary
 * @size: (in): subchannel data file sector size
 *
 * <para>
 * Sets subchannel data file sector size.
 * </para>
 **/
void mirage_fragment_iface_binary_subchannel_data_set_size (MirageFragmentIfaceBinary *self, gint size)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->subchannel_data_set_size(self, size);
}

/**
 * mirage_fragment_iface_binary_subchannel_data_get_size:
 * @self: a #MirageFragmentIfaceBinary
 *
 * <para>
 * Retrieves subchannel data file sector size.
 * </para>
 *
 * Returns: subchannel data file sector size
 **/
gint mirage_fragment_iface_binary_subchannel_data_get_size (MirageFragmentIfaceBinary *self)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->subchannel_data_get_size(self);
}


/**
 * mirage_fragment_iface_binary_subchannel_data_set_format:
 * @self: a #MirageFragmentIfaceBinary
 * @format: (in): subchannel data file format
 *
 * <para>
 * Sets subchannel data file format. @format must be a combination of
 * #MirageSubchannelDataFormat.
 * </para>
 **/
void mirage_fragment_iface_binary_subchannel_data_set_format (MirageFragmentIfaceBinary *self, gint format)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->subchannel_data_set_format(self, format);
}

/**
 * mirage_fragment_iface_binary_subchannel_data_get_format:
 * @self: a #MirageFragmentIfaceBinary
 *
 * <para>
 * Retrieves subchannel data file format.
 * </para>
 *
 * Returns: subchannel data file format
 **/
gint mirage_fragment_iface_binary_subchannel_data_get_format (MirageFragmentIfaceBinary *self)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->subchannel_data_get_format(self);
}


/**
 * mirage_fragment_iface_binary_subchannel_data_get_position:
 * @self: a #MirageFragmentIfaceBinary
 * @address: (in): address
 *
 * <para>
 * Calculates position of data for sector at address @address within
 * subchannel data file and stores it in @position.
 * </para>
 *
 * Returns: position in subchannel data file
 **/
guint64 mirage_fragment_iface_binary_subchannel_data_get_position (MirageFragmentIfaceBinary *self, gint address)
{
    return MIRAGE_FRAGMENT_IFACE_BINARY_GET_INTERFACE(self)->subchannel_data_get_position(self, address);
}


GType mirage_fragment_iface_binary_get_type (void) {
    static GType iface_type = 0;
    if (iface_type == 0) {
        static const GTypeInfo info = {
            sizeof(MirageFragmentIfaceBinaryInterface),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            NULL,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            0,
            0,      /* n_preallocs */
            NULL,   /* instance_init */
            NULL    /* value_table */
        };

        iface_type = g_type_register_static(G_TYPE_INTERFACE, "MirageFragmentIfaceBinary", &info, 0);
    }

    return iface_type;
}
