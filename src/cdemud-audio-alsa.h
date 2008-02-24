/*
 *  CDEmuD: ALSA audio play object
 *  Copyright (C) 2006-2008 Rok Mandeljc
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

#ifndef __CDEMUD_AUDIO_ALSA_H__
#define __CDEMUD_AUDIO_ALSA_H__

#ifdef ALSA_BACKEND

#include <alsa/asoundlib.h>


G_BEGIN_DECLS

#define CDEMUD_TYPE_AUDIO_ALSA            (cdemud_audio_alsa_get_type())
#define CDEMUD_AUDIO_ALSA(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), CDEMUD_TYPE_AUDIO_ALSA, CDEMUD_Audio_ALSA))
#define CDEMUD_AUDIO_ALSA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), CDEMUD_TYPE_AUDIO_ALSA, CDEMUD_Audio_ALSAClass))
#define CDEMUD_IS_AUDIO_ALSA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), CDEMUD_TYPE_AUDIO_ALSA))
#define CDEMUD_IS_AUDIO_ALSA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), CDEMUD_TYPE_AUDIO_ALSA))
#define CDEMUD_AUDIO_ALSA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), CDEMUD_TYPE_AUDIO_ALSA, CDEMUD_Audio_ALSAClass))


typedef struct {
    CDEMUD_Audio parent;
} CDEMUD_Audio_ALSA;

typedef struct {
    CDEMUD_AudioClass parent;
} CDEMUD_Audio_ALSAClass;


/* Used by CDEMUD_TYPE_AUDIO_ALSA */
GType cdemud_audio_alsa_get_type (void);

G_END_DECLS

#endif /* ALSA_BACKEND */

#endif /* __CDEMUD_AUDIO_ALSA_H__ */
