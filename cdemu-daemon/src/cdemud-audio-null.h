/*
 *  CDEmuD: NULL audio play object
 *  Copyright (C) 2006-2007 Rok Mandeljc
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

#ifndef __CDEMUD_AUDIO_NULL_H__
#define __CDEMUD_AUDIO_NULL_H__


G_BEGIN_DECLS

#define CDEMUD_TYPE_AUDIO_NULL            (cdemud_audio_null_get_type())
#define CDEMUD_AUDIO_NULL(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), CDEMUD_TYPE_AUDIO_NULL, CDEMUD_Audio_NULL))
#define CDEMUD_AUDIO_NULL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), CDEMUD_TYPE_AUDIO_NULL, CDEMUD_Audio_NULLClass))
#define CDEMUD_IS_AUDIO_NULL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), CDEMUD_TYPE_AUDIO_NULL))
#define CDEMUD_IS_AUDIO_NULL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), CDEMUD_TYPE_AUDIO_NULL))
#define CDEMUD_AUDIO_NULL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), CDEMUD_TYPE_AUDIO_NULL, CDEMUD_Audio_NULLClass))


typedef struct {
    CDEMUD_Audio parent;
} CDEMUD_Audio_NULL;

typedef struct {
    CDEMUD_AudioClass parent;
} CDEMUD_Audio_NULLClass;


/* Used by CDEMUD_TYPE_AUDIO_NULL */
GType cdemud_audio_null_get_type (void);

G_END_DECLS

#endif /* __CDEMUD_AUDIO_NULL_H__ */
