/*
 *  CDEmuD: Audio play object
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

#ifndef __CDEMUD_AUDIO_H__
#define __CDEMUD_AUDIO_H__

G_BEGIN_DECLS

#define CDEMUD_TYPE_AUDIO            (cdemud_audio_get_type())
#define CDEMUD_AUDIO(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), CDEMUD_TYPE_AUDIO, CDEMUD_Audio))
#define CDEMUD_AUDIO_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), CDEMUD_TYPE_AUDIO, CDEMUD_AudioClass))
#define CDEMUD_IS_AUDIO(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), CDEMUD_TYPE_AUDIO))
#define CDEMUD_IS_AUDIO_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), CDEMUD_TYPE_AUDIO))
#define CDEMUD_AUDIO_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), CDEMUD_TYPE_AUDIO, CDEMUD_AudioClass))

typedef struct _CDEMUD_Audio        CDEMUD_Audio;
typedef struct _CDEMUD_AudioClass   CDEMUD_AudioClass;
typedef struct _CDEMUD_AudioPrivate CDEMUD_AudioPrivate;

struct _CDEMUD_Audio
{
    MIRAGE_Object parent_instance;

    /*< private >*/
    CDEMUD_AudioPrivate *priv;
};

struct _CDEMUD_AudioClass
{
    MIRAGE_ObjectClass parent_class;
};


/* Used by CDEMUD_TYPE_AUDIO */
GType cdemud_audio_get_type (void);

/* Public API */
void cdemud_audio_initialize (CDEMUD_Audio *self, gchar *driver, gint *cur_sector_ptr, GMutex *device_mutex_ptr);
gboolean cdemud_audio_start (CDEMUD_Audio *self, gint start, gint end, GObject *disc);
gboolean cdemud_audio_resume (CDEMUD_Audio *self);
gboolean cdemud_audio_pause (CDEMUD_Audio *self);
gboolean cdemud_audio_stop (CDEMUD_Audio *self);
gint cdemud_audio_get_status (CDEMUD_Audio *self);

G_END_DECLS

#endif /* __CDEMUD_AUDIO_H__ */
