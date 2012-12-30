/*
 *  CDEmu daemon: Audio play object
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

#ifndef __CDEMU_AUDIO_H__
#define __CDEMU_AUDIO_H__

G_BEGIN_DECLS

#define CDEMU_TYPE_AUDIO            (cdemu_audio_get_type())
#define CDEMU_AUDIO(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), CDEMU_TYPE_AUDIO, CdemuAudio))
#define CDEMU_AUDIO_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), CDEMU_TYPE_AUDIO, CdemuAudioClass))
#define CDEMU_IS_AUDIO(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), CDEMU_TYPE_AUDIO))
#define CDEMU_IS_AUDIO_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), CDEMU_TYPE_AUDIO))
#define CDEMU_AUDIO_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), CDEMU_TYPE_AUDIO, CdemuAudioClass))

typedef struct _CdemuAudio        CdemuAudio;
typedef struct _CdemuAudioClass   CdemuAudioClass;
typedef struct _CdemuAudioPrivate CdemuAudioPrivate;

struct _CdemuAudio
{
    MirageObject parent_instance;

    /*< private >*/
    CdemuAudioPrivate *priv;
};

struct _CdemuAudioClass
{
    MirageObjectClass parent_class;
};


/* Used by CDEMU_TYPE_AUDIO */
GType cdemu_audio_get_type (void);

/* Public API */
void cdemu_audio_initialize (CdemuAudio *self, const gchar *driver, gint *cur_sector_ptr, GMutex *device_mutex_ptr);
gboolean cdemu_audio_start (CdemuAudio *self, gint start, gint end, MirageDisc *disc);
gboolean cdemu_audio_resume (CdemuAudio *self);
gboolean cdemu_audio_pause (CdemuAudio *self);
gboolean cdemu_audio_stop (CdemuAudio *self);
gint cdemu_audio_get_status (CdemuAudio *self);

G_END_DECLS

#endif /* __CDEMU_AUDIO_H__ */
