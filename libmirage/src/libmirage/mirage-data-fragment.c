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

/**
 * SECTION: mirage-data-fragment
 * @title: MirageDataFragment
 * @short_description: Binary Fragment interface.
 * @see_also: #MirageFragment
 * @include: mirage-fragment-iface-binary.h
 *
 * #MirageDataFragment is Binary Fragment interface that can be
 * implemented by a #MirageFragment implementation.
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
 * mirage_data_fragment_main_data_set_stream:
 * @self: a #MirageDataFragment
 * @stream: (in) (transfer full): a #GInputStream on main data file
 *
 * Sets main data stream.
 */
void mirage_data_fragment_main_data_set_stream (MirageDataFragment *self, GInputStream *stream)
{
    return MIRAGE_DATA_FRAGMENT_GET_INTERFACE(self)->main_data_set_stream(self, stream);
}

/**
 * mirage_data_fragment_main_data_get_filename:
 * @self: a #MirageDataFragment
 *
 * Retrieves filename of main data file.
 *
 * Returns: (transfer none): pointer to main data file name string.
 * The string belongs to object and should not be modified.
 */
const gchar *mirage_data_fragment_main_data_get_filename (MirageDataFragment *self)
{
    return MIRAGE_DATA_FRAGMENT_GET_INTERFACE(self)->main_data_get_filename(self);
}

/**
 * mirage_data_fragment_main_data_set_offset:
 * @self: a #MirageDataFragment
 * @offset: (in): main data file offset
 *
 * Sets main data file offset.
 */
void mirage_data_fragment_main_data_set_offset (MirageDataFragment *self, guint64 offset)
{
    return MIRAGE_DATA_FRAGMENT_GET_INTERFACE(self)->main_data_set_offset(self, offset);
}

/**
 * mirage_data_fragment_main_data_get_offset:
 * @self: a #MirageDataFragment
 *
 * Retrieves main data file offset.
 *
 * Returns: main data file offset
 */
guint64 mirage_data_fragment_main_data_get_offset (MirageDataFragment *self)
{
    return MIRAGE_DATA_FRAGMENT_GET_INTERFACE(self)->main_data_get_offset(self);
}


/**
 * mirage_data_fragment_main_data_set_size:
 * @self: a #MirageDataFragment
 * @size: (in): main data file sector size
 *
 * Sets main data file sector size.
 */
void mirage_data_fragment_main_data_set_size (MirageDataFragment *self, gint size)
{
    return MIRAGE_DATA_FRAGMENT_GET_INTERFACE(self)->main_data_set_size(self, size);
}

/**
 * mirage_data_fragment_main_data_get_size:
 * @self: a #MirageDataFragment
 *
 * Retrieves main data file sector size.
 *
 * Returns: main data file sector size
 */
gint mirage_data_fragment_main_data_get_size (MirageDataFragment *self)
{
    return MIRAGE_DATA_FRAGMENT_GET_INTERFACE(self)->main_data_get_size(self);
}


/**
 * mirage_data_fragment_main_data_set_format:
 * @self: a #MirageDataFragment
 * @format: (in): main data file format
 *
 * Sets main data file format. @format must be one of #MirageMainDataFormat.
 */
void mirage_data_fragment_main_data_set_format (MirageDataFragment *self, gint format)
{
    return MIRAGE_DATA_FRAGMENT_GET_INTERFACE(self)->main_data_set_format(self, format);
}

/**
 * mirage_data_fragment_main_data_get_format:
 * @self: a #MirageDataFragment
 *
 * Retrieves main data file format.
 *
 * Returns: main data file format
 */
gint mirage_data_fragment_main_data_get_format (MirageDataFragment *self)
{
    return MIRAGE_DATA_FRAGMENT_GET_INTERFACE(self)->main_data_get_format(self);
}


/**
 * mirage_data_fragment_main_data_get_position:
 * @self: a #MirageDataFragment
 * @address: (in): address
 *
 * Calculates position of data for sector at address @address within
 * main data file and stores it in @position.
 *
 * Returns: position in main data file
 */
guint64 mirage_data_fragment_main_data_get_position (MirageDataFragment *self, gint address)
{
    return MIRAGE_DATA_FRAGMENT_GET_INTERFACE(self)->main_data_get_position(self, address);
}


/**********************************************************************\
 *                        Subchannel data file                        *
\**********************************************************************/
/**
 * mirage_data_fragment_subchannel_data_set_stream:
 * @self: a #MirageDataFragment
 * @stream: (in) (transfer full): a #GInputStream on subchannel data file
 *
 * Sets subchannel data stream.
 */
void mirage_data_fragment_subchannel_data_set_stream (MirageDataFragment *self, GInputStream *stream)
{
    return MIRAGE_DATA_FRAGMENT_GET_INTERFACE(self)->subchannel_data_set_stream(self, stream);
}

/**
 * mirage_data_fragment_subchannel_data_get_filename:
 * @self: a #MirageDataFragment
 *
 * Retrieves subchannel data file name.
 *
 * Returns: (transfer none): pointer to subchannel data file name string.
 * The string belongs to object and should not be modified.
 */
const gchar *mirage_data_fragment_subchannel_data_get_filename (MirageDataFragment *self)
{
    return MIRAGE_DATA_FRAGMENT_GET_INTERFACE(self)->subchannel_data_get_filename(self);
}


/**
 * mirage_data_fragment_subchannel_data_set_offset:
 * @self: a #MirageDataFragment
 * @offset: (in): subchannel data file offset
 *
 * Sets subchannel data file offset.
 */
void mirage_data_fragment_subchannel_data_set_offset (MirageDataFragment *self, guint64 offset)
{
    return MIRAGE_DATA_FRAGMENT_GET_INTERFACE(self)->subchannel_data_set_offset(self, offset);
}

/**
 * mirage_data_fragment_subchannel_data_get_offset:
 * @self: a #MirageDataFragment
 *
 * Retrieves subchannel data file offset.
 *
 * Returns: subchannel data file offset
 */
guint64 mirage_data_fragment_subchannel_data_get_offset (MirageDataFragment *self)
{
    return MIRAGE_DATA_FRAGMENT_GET_INTERFACE(self)->subchannel_data_get_offset(self);
}

/**
 * mirage_data_fragment_subchannel_data_set_size:
 * @self: a #MirageDataFragment
 * @size: (in): subchannel data file sector size
 *
 * Sets subchannel data file sector size.
 */
void mirage_data_fragment_subchannel_data_set_size (MirageDataFragment *self, gint size)
{
    return MIRAGE_DATA_FRAGMENT_GET_INTERFACE(self)->subchannel_data_set_size(self, size);
}

/**
 * mirage_data_fragment_subchannel_data_get_size:
 * @self: a #MirageDataFragment
 *
 * Retrieves subchannel data file sector size.
 *
 * Returns: subchannel data file sector size
 */
gint mirage_data_fragment_subchannel_data_get_size (MirageDataFragment *self)
{
    return MIRAGE_DATA_FRAGMENT_GET_INTERFACE(self)->subchannel_data_get_size(self);
}


/**
 * mirage_data_fragment_subchannel_data_set_format:
 * @self: a #MirageDataFragment
 * @format: (in): subchannel data file format
 *
 * Sets subchannel data file format. @format must be a combination of
 * #MirageSubchannelDataFormat.
 */
void mirage_data_fragment_subchannel_data_set_format (MirageDataFragment *self, gint format)
{
    return MIRAGE_DATA_FRAGMENT_GET_INTERFACE(self)->subchannel_data_set_format(self, format);
}

/**
 * mirage_data_fragment_subchannel_data_get_format:
 * @self: a #MirageDataFragment
 *
 * Retrieves subchannel data file format.
 *
 * Returns: subchannel data file format
 */
gint mirage_data_fragment_subchannel_data_get_format (MirageDataFragment *self)
{
    return MIRAGE_DATA_FRAGMENT_GET_INTERFACE(self)->subchannel_data_get_format(self);
}


/**
 * mirage_data_fragment_subchannel_data_get_position:
 * @self: a #MirageDataFragment
 * @address: (in): address
 *
 * Calculates position of data for sector at address @address within
 * subchannel data file and stores it in @position.
 *
 * Returns: position in subchannel data file
 */
guint64 mirage_data_fragment_subchannel_data_get_position (MirageDataFragment *self, gint address)
{
    return MIRAGE_DATA_FRAGMENT_GET_INTERFACE(self)->subchannel_data_get_position(self, address);
}


GType mirage_data_fragment_get_type (void) {
    static GType iface_type = 0;
    if (iface_type == 0) {
        static const GTypeInfo info = {
            sizeof(MirageDataFragmentInterface),
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

        iface_type = g_type_register_static(G_TYPE_INTERFACE, "MirageDataFragment", &info, 0);
    }

    return iface_type;
}
