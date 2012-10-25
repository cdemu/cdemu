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

#ifndef __MIRAGE_AUDIO_FRAGMENT_H__
#define __MIRAGE_AUDIO_FRAGMENT_H__


G_BEGIN_DECLS


/**********************************************************************\
 *                  MirageAudioFragment interface                *
\**********************************************************************/
#define MIRAGE_TYPE_AUDIO_FRAGMENT             (mirage_audio_fragment_get_type())
#define MIRAGE_AUDIO_FRAGMENT(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_AUDIO_FRAGMENT, MirageAudioFragment))
#define MIRAGE_IS_AUDIO_FRAGMENT(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_AUDIO_FRAGMENT))
#define MIRAGE_AUDIO_FRAGMENT_GET_INTERFACE(inst)  (G_TYPE_INSTANCE_GET_INTERFACE((inst), MIRAGE_TYPE_AUDIO_FRAGMENT, MirageAudioFragmentInterface))

/**
 * MirageAudioFragment:
 *
 * A fragment object that provides access to audio data.
 */
typedef struct _MirageAudioFragment          MirageAudioFragment;
typedef struct _MirageAudioFragmentInterface MirageAudioFragmentInterface;

/**
 * MirageAudioFragmentInterface:
 * @parent_iface: the parent interface
 * @set_stream: sets audio file stream
 * @get_filename: retrieves filename of audio file
 * @set_offset: sets offset within audio file
 * @get_offset: retrieves offset within audio file
 *
 * Provides an interface for implementing fragment objects that provide
 * access to audio data.
 */
struct _MirageAudioFragmentInterface
{
    GTypeInterface parent_iface;

    /* Interface methods */
    void (*set_stream) (MirageAudioFragment *self, GInputStream *stream);
    const gchar *(*get_filename) (MirageAudioFragment *self);
    void (*set_offset) (MirageAudioFragment *self, gint offset);
    gint (*get_offset) (MirageAudioFragment *self);
};

/* Used by MIRAGE_TYPE_AUDIO_FRAGMENT */
GType mirage_audio_fragment_get_type (void);

void mirage_audio_fragment_set_stream (MirageAudioFragment *self, GInputStream *stream);
const gchar *mirage_audio_fragment_get_filename (MirageAudioFragment *self);
void mirage_audio_fragment_set_offset (MirageAudioFragment *self, gint offset);
gint mirage_audio_fragment_get_offset (MirageAudioFragment *self);

G_END_DECLS

#endif /* __MIRAGE_AUDIO_FRAGMENT_H__ */
