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

#ifndef __MIRAGE_FRAGMENT_IFACE_AUDIO_H__
#define __MIRAGE_FRAGMENT_IFACE_AUDIO_H__


G_BEGIN_DECLS


/**********************************************************************\
 *                  MirageFragmentIfaceAudio interface                *
\**********************************************************************/
#define MIRAGE_TYPE_FRAGMENT_IFACE_AUDIO             (mirage_fragment_iface_audio_get_type())
#define MIRAGE_FRAGMENT_IFACE_AUDIO(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FRAGMENT_IFACE_AUDIO, MirageFragmentIfaceAudio))
#define MIRAGE_IS_FRAGMENT_IFACE_AUDIO(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FRAGMENT_IFACE_AUDIO))
#define MIRAGE_FRAGMENT_IFACE_AUDIO_GET_INTERFACE(inst)  (G_TYPE_INSTANCE_GET_INTERFACE((inst), MIRAGE_TYPE_FRAGMENT_IFACE_AUDIO, MirageFragmentIfaceAudioInterface))

/**
 * MirageFragmentIfaceAudio:
 *
 * <para>
 * Dummy interface structure.
 * </para>
 **/
typedef struct _MirageFragmentIfaceAudio          MirageFragmentIfaceAudio;
typedef struct _MirageFragmentIfaceAudioInterface MirageFragmentIfaceAudioInterface;

struct _MirageFragmentIfaceAudioInterface
{
    GTypeInterface parent_iface;

    /* Interface methods */
    void (*set_stream) (MirageFragmentIfaceAudio *self, GInputStream *stream);
    const gchar *(*get_filename) (MirageFragmentIfaceAudio *self);
    void (*set_offset) (MirageFragmentIfaceAudio *self, gint offset);
    gint (*get_offset) (MirageFragmentIfaceAudio *self);
};

/* Used by MIRAGE_TYPE_FRAGMENT_IFACE_AUDIO */
GType mirage_fragment_iface_audio_get_type (void);

void mirage_fragment_iface_audio_set_stream (MirageFragmentIfaceAudio *self, GInputStream *stream);
const gchar *mirage_fragment_iface_audio_get_filename (MirageFragmentIfaceAudio *self);
void mirage_fragment_iface_audio_set_offset (MirageFragmentIfaceAudio *self, gint offset);
gint mirage_fragment_iface_audio_get_offset (MirageFragmentIfaceAudio *self);

G_END_DECLS

#endif /* __MIRAGE_FRAGMENT_IFACE_AUDIO_H__ */
