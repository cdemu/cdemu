/*
 *  libMirage: AudioFragment interface
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
 * SECTION: mirage-audio-fragment
 * @title: MirageAudioFragment
 * @short_description: Audio Fragment interface.
 * @see_also: #MirageFragment
 * @include: mirage-fragment-iface-audio.h
 *
 * #MirageAudioFragment is Audio Fragment interface that can be
 * implemented by a #MirageFragment implementation.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"

#define __debug__ "Fragment"


/**
 * mirage_audio_fragment_set_stream:
 * @self: a #MirageAudioFragment
 * @stream: (in) (transfer full): a #GInputStream on audio file
 *
 * Sets audio file stream.
 */
void mirage_audio_fragment_set_stream (MirageAudioFragment *self, GInputStream *stream)
{
    return MIRAGE_AUDIO_FRAGMENT_GET_INTERFACE(self)->set_stream(self, stream);
}

/**
 * mirage_audio_fragment_get_filename:
 * @self: a #MirageAudioFragment
 *
 * Retrieves filename of audio file.
 *
 * Returns: (transfer none): pointer to audio file name string. The
 * string belongs to object and should not be modified.
 */
const gchar *mirage_audio_fragment_get_filename (MirageAudioFragment *self)
{
    return MIRAGE_AUDIO_FRAGMENT_GET_INTERFACE(self)->get_filename(self);
}

/**
 * mirage_audio_fragment_set_offset:
 * @self: a #MirageAudioFragment
 * @offset: (in): offset
 *
 * Sets offset within audio file, in sectors.
 */
void mirage_audio_fragment_set_offset (MirageAudioFragment *self, gint offset)
{
    return MIRAGE_AUDIO_FRAGMENT_GET_INTERFACE(self)->set_offset(self, offset);
}

/**
 * mirage_audio_fragment_get_offset:
 * @self: a #MirageAudioFragment
 *
 * Retrieves offset within audio file, in sectors.
 *
 * Returns: offset
 */
gint mirage_audio_fragment_get_offset (MirageAudioFragment *self)
{
    return MIRAGE_AUDIO_FRAGMENT_GET_INTERFACE(self)->get_offset(self);
}

GType mirage_audio_fragment_get_type (void) {
    static GType iface_type = 0;
    if (iface_type == 0) {
        static const GTypeInfo info = {
            sizeof(MirageAudioFragmentInterface),
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

        iface_type = g_type_register_static(G_TYPE_INTERFACE, "MirageAudioFragment", &info, 0);
    }

    return iface_type;
}
