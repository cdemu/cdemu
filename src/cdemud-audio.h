/*
 *  CDEmuD: Audio play object
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

#ifndef __CDEMUD_AUDIO_H__
#define __CDEMUD_AUDIO_H__


G_BEGIN_DECLS

#define CDEMUD_TYPE_AUDIO            (cdemud_audio_get_type())
#define CDEMUD_AUDIO(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), CDEMUD_TYPE_AUDIO, CDEMUD_Audio))
#define CDEMUD_AUDIO_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), CDEMUD_TYPE_AUDIO, CDEMUD_AudioClass))
#define CDEMUD_IS_AUDIO(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), CDEMUD_TYPE_AUDIO))
#define CDEMUD_IS_AUDIO_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), CDEMUD_TYPE_AUDIO))
#define CDEMUD_AUDIO_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), CDEMUD_TYPE_AUDIO, CDEMUD_AudioClass))

typedef struct {
    gchar *name;
    GType (*get_type_func) (void); 
    /* It might be preferrable to have GType here, but we're using this struct
       for our const array of supported audio backends... since it's not 
       initialized at runtime, we have to use function pointers instead of GTypes */
} CDEMUD_AudioBackend;

typedef struct {
    MIRAGE_Object parent;
}CDEMUD_Audio;

typedef struct {
    MIRAGE_ObjectClass parent;
    
    /* Class members */   
    gboolean (*initialize) (CDEMUD_Audio *self, gchar *device, gint *cur_sector_ptr, GError **error);
    gboolean (*start) (CDEMUD_Audio *self, gint start, gint end, GObject *disc, GError **error);
    gboolean (*resume) (CDEMUD_Audio *self, GError **error);
    gboolean (*pause) (CDEMUD_Audio *self, GError **error);
    gboolean (*stop) (CDEMUD_Audio *self, GError **error);
    gboolean (*get_status) (CDEMUD_Audio *self, gint *status, GError **error);
} CDEMUD_AudioClass;

/* Used by CDEMUD_TYPE_AUDIO */
GType cdemud_audio_get_type (void);

/* Public API */
gboolean cdemud_audio_initialize (CDEMUD_Audio *self, gchar *device, gint *cur_sector_ptr, GError **error);
gboolean cdemud_audio_start (CDEMUD_Audio *self, gint start, gint end, GObject *disc, GError **error);
gboolean cdemud_audio_resume (CDEMUD_Audio *self, GError **error);
gboolean cdemud_audio_pause (CDEMUD_Audio *self, GError **error);
gboolean cdemud_audio_stop (CDEMUD_Audio *self, GError **error);
gboolean cdemud_audio_get_status (CDEMUD_Audio *self, gint *status, GError **error);

G_END_DECLS

#endif /* __CDEMUD_AUDIO_H__ */
