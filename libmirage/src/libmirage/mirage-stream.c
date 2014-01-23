/*
 *  libMirage: Stream interface
 *  Copyright (C) 2014 Rok Mandeljc
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

/**
 * SECTION: mirage-stream
 * @title: MirageStream
 * @short_description: Interface for streams.
 * @see_also: #MirageFileStream, #MirageFilterStream
 * @include: mirage-stream.h
 *
 * TODO
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"
#include "mirage-compat-input-stream.h"


/**********************************************************************\
 *                        Stream information                          *
\**********************************************************************/
const gchar *mirage_stream_get_filename (MirageStream *self)
{
    return MIRAGE_STREAM_GET_INTERFACE(self)->get_filename(self);
}

gboolean mirage_stream_is_writable (MirageStream *self)
{
    return MIRAGE_STREAM_GET_INTERFACE(self)->is_writable(self);
}


/**********************************************************************\
 *                           I/O functions                            *
\**********************************************************************/
gssize mirage_stream_read (MirageStream *self, void *buffer, gsize count, GError **error)
{
    return MIRAGE_STREAM_GET_INTERFACE(self)->read(self, buffer, count, error);
}

gssize mirage_stream_write (MirageStream *self, const void *buffer, gsize count, GError **error)
{
    return MIRAGE_STREAM_GET_INTERFACE(self)->write(self, buffer, count, error);
}

gboolean mirage_stream_seek (MirageStream *self, goffset offset, GSeekType type, GError **error)
{
    return MIRAGE_STREAM_GET_INTERFACE(self)->seek(self, offset, type, error);
}

goffset mirage_stream_tell (MirageStream *self)
{
    return MIRAGE_STREAM_GET_INTERFACE(self)->tell(self);
}

GInputStream *mirage_stream_get_g_input_stream (MirageStream *self)
{
    return g_object_new(MIRAGE_TYPE_COMPAT_INPUT_STREAM, "stream", self, NULL);
}


/**********************************************************************\
 *                           Interface init                           *
\**********************************************************************/
GType mirage_stream_get_type (void) {
    static GType iface_type = 0;
    if (iface_type == 0) {
        static const GTypeInfo info = {
            sizeof(MirageStreamInterface),
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

        iface_type = g_type_register_static(G_TYPE_INTERFACE, "MirageStream", &info, 0);
    }

    return iface_type;
}
