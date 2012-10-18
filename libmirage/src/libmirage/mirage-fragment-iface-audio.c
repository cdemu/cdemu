/*
 *  libMirage: FragmentIfaceAudio interface
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
 * SECTION: mirage-fragment-iface-audio
 * @title: MirageFragmentIfaceAudio
 * @short_description: Audio Fragment interface.
 * @see_also: #MirageFragment
 * @include: mirage-fragment-iface-audio.h
 *
 * #MirageFragmentIfaceAudio is Audio Fragment interface that can be
 * implemented by a #MirageFragment implementation.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"

#define __debug__ "Fragment"


/**
 * mirage_fragment_iface_audio_set_stream:
 * @self: a #MirageFragmentIfaceAudio
 * @stream: (in) (transfer full): a #GInputStream on audio file
 *
 * Sets audio file stream.
 */
void mirage_fragment_iface_audio_set_stream (MirageFragmentIfaceAudio *self, GInputStream *stream)
{
    return MIRAGE_FRAGMENT_IFACE_AUDIO_GET_INTERFACE(self)->set_stream(self, stream);
}

/**
 * mirage_fragment_iface_audio_get_filename:
 * @self: a #MirageFragmentIfaceAudio
 *
 * Retrieves filename of audio file.
 *
 * Returns: (transfer none): pointer to audio file name string. The
 * string belongs to object and should not be modified.
 */
const gchar *mirage_fragment_iface_audio_get_filename (MirageFragmentIfaceAudio *self)
{
    return MIRAGE_FRAGMENT_IFACE_AUDIO_GET_INTERFACE(self)->get_filename(self);
}

/**
 * mirage_fragment_iface_audio_set_offset:
 * @self: a #MirageFragmentIfaceAudio
 * @offset: (in): offset
 *
 * Sets offset within audio file, in sectors.
 */
void mirage_fragment_iface_audio_set_offset (MirageFragmentIfaceAudio *self, gint offset)
{
    return MIRAGE_FRAGMENT_IFACE_AUDIO_GET_INTERFACE(self)->set_offset(self, offset);
}

/**
 * mirage_fragment_iface_audio_get_offset:
 * @self: a #MirageFragmentIfaceAudio
 *
 * Retrieves offset within audio file, in sectors.
 *
 * Returns: offset
 */
gint mirage_fragment_iface_audio_get_offset (MirageFragmentIfaceAudio *self)
{
    return MIRAGE_FRAGMENT_IFACE_AUDIO_GET_INTERFACE(self)->get_offset(self);
}

GType mirage_fragment_iface_audio_get_type (void) {
    static GType iface_type = 0;
    if (iface_type == 0) {
        static const GTypeInfo info = {
            sizeof(MirageFragmentIfaceAudioInterface),
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

        iface_type = g_type_register_static(G_TYPE_INTERFACE, "MirageFragmentIfaceAudio", &info, 0);
    }

    return iface_type;
}
