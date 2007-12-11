/*
 *  CDEmuD: Audio play interface
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

#include "cdemud.h"


/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
gboolean cdemud_audio_initialize (CDEMUD_Audio *self, gchar *device, gint *cur_sector_ptr, GError **error) {
    return CDEMUD_AUDIO_GET_CLASS(self)->initialize(self, device, cur_sector_ptr, error);
}

gboolean cdemud_audio_start (CDEMUD_Audio *self, gint start, gint end, GObject *disc, GError **error) {
    return CDEMUD_AUDIO_GET_CLASS(self)->start(self, start, end, disc, error);
}

gboolean cdemud_audio_resume (CDEMUD_Audio *self, GError **error) {
    return CDEMUD_AUDIO_GET_CLASS(self)->resume(self, error);
}

gboolean cdemud_audio_pause (CDEMUD_Audio *self, GError **error) {
    return CDEMUD_AUDIO_GET_CLASS(self)->pause(self, error);
}

gboolean cdemud_audio_stop (CDEMUD_Audio *self, GError **error) {
    return CDEMUD_AUDIO_GET_CLASS(self)->stop(self, error);
}

gboolean cdemud_audio_get_status (CDEMUD_Audio *self, gint *status, GError **error) {
    return CDEMUD_AUDIO_GET_CLASS(self)->get_status(self, status, error);
}

/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static MIRAGE_ObjectClass *parent_class = NULL;

static void __cdemud_audio_class_init (gpointer g_class, gpointer g_class_data) {
    CDEMUD_AudioClass *klass = CDEMUD_AUDIO_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Following functions are provided by implementations */
    klass->initialize = NULL;
    klass->start = NULL;
    klass->resume = NULL;
    klass->pause = NULL;
    klass->stop = NULL;
    klass->get_status = NULL;
        
    return;
}

GType cdemud_audio_get_type (void) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(CDEMUD_AudioClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __cdemud_audio_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(CDEMUD_Audio),
            0,      /* n_preallocs */
            NULL    /* instance_init */
        };
        type = g_type_register_static(MIRAGE_TYPE_OBJECT, "CDEMUD_Audio", &info, 0);
    }
    return type;
}
