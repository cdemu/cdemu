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

#include "cdemud.h"


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define CDEMUD_AUDIO_NULL_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), CDEMUD_TYPE_AUDIO_NULL, CDEMUD_Audio_NULLPrivate))

typedef struct {    
    /* Mutex */
    GStaticMutex mutex;
    
    /* Pointer to disc */
    GObject *disc;
    
    /* Sector */
    gint cur_sector;
    gint end_sector;

    gint *cur_sector_ptr;
    
    /* Timer ID */
    guint timeout_id;
        
    gint status;
} CDEMUD_Audio_NULLPrivate;


static gboolean __cdemud_audio_null_callback (CDEMUD_Audio_NULL *self) {
    CDEMUD_Audio_NULLPrivate *_priv = CDEMUD_AUDIO_NULL_GET_PRIVATE(CDEMUD_AUDIO_NULL(self));
    gboolean succeeded = TRUE;
    
    /* Lock */
    g_static_mutex_lock(&_priv->mutex);
    
    if (_priv->status != AUDIO_STATUS_PLAYING) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_AUDIOPLAY, "%s: callback called when we're not supposed to be playing... but we're safe due to mutex!\n", __func__);        
        return FALSE;
    }
    
    /* "Process" sectors; we go over one second (75 sectors), check their type
       and keep track of where we are... and that's all a NULL driver really does */
    gint i;
    for (i = 0; i < 75; i++) {
        GObject *sector = NULL;
        GError *error = NULL;
        gint type = 0;
        
        if (_priv->cur_sector > _priv->end_sector) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_AUDIOPLAY, "%s: reached the end\n", __func__);
            _priv->status = AUDIO_STATUS_COMPLETED; /* Audio operation successfully completed */
            succeeded = FALSE;
            goto end;
        }
        
        if (!mirage_disc_get_sector(MIRAGE_DISC(_priv->disc), _priv->cur_sector, &sector, &error)) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_AUDIOPLAY, "%s: failed to get sector 0x%X: %s\n", __func__, _priv->cur_sector, error->message);
            g_error_free(error);
            _priv->status = AUDIO_STATUS_ERROR; /* Audio operation stopped due to error */
            succeeded = FALSE;
            goto end;
        }
        
        /* This one covers both sector not being an audio one and sector changing 
           from audio to data one */
        mirage_sector_get_sector_type(MIRAGE_SECTOR(sector), &type, NULL);

        g_object_unref(sector); /* Unref here; we won't need it anymore... */

        if (type != MIRAGE_MODE_AUDIO) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_AUDIOPLAY, "%s: non audio sector!\n", __func__);
            _priv->status = AUDIO_STATUS_ERROR; /* Audio operation stopped due to error */
            succeeded = FALSE;
            goto end;
        }
        
        if (_priv->cur_sector_ptr) {
            *_priv->cur_sector_ptr = _priv->cur_sector;
        }
        _priv->cur_sector++;
    }

end:
    /* Unlock */
    g_static_mutex_unlock(&_priv->mutex);
    
    return succeeded;
}

static gboolean __cdemud_audio_null_start_playing (CDEMUD_Audio_NULL *self, GError **error) {
    CDEMUD_Audio_NULLPrivate *_priv = CDEMUD_AUDIO_NULL_GET_PRIVATE(self);
    
    /* Set the status */
    _priv->status = AUDIO_STATUS_PLAYING;
    
    /* Try playing */
    if (!__cdemud_audio_null_callback(self)) {
        return FALSE;
    }

    /* Add callback */
    _priv->timeout_id = g_timeout_add(1000, (GSourceFunc)__cdemud_audio_null_callback, CDEMUD_AUDIO_NULL(self));
    
    return TRUE;
}

static gboolean __cdemud_audio_null_stop_playing (CDEMUD_Audio_NULL *self, GError **error) {
    CDEMUD_Audio_NULLPrivate *_priv = CDEMUD_AUDIO_NULL_GET_PRIVATE(self);

    /* Remove callback */
    if (_priv->timeout_id) {
        g_source_remove(_priv->timeout_id);
        _priv->timeout_id = 0;
    }
    
    /* We can't tell whether we're stopped or paused, so the upper layer needs
       to set appropriate status */
    
    return TRUE;
}


/******************************************************************************\
 *
\******************************************************************************/
static gboolean __cdemud_audio_null_initialize (CDEMUD_Audio *self, gchar *device, gint *cur_sector_ptr, GError **error) {
    CDEMUD_Audio_NULLPrivate *_priv = CDEMUD_AUDIO_NULL_GET_PRIVATE(CDEMUD_AUDIO_NULL(self));
        
    /* Init device mutex */
    g_static_mutex_init(&_priv->mutex);
    
    _priv->cur_sector_ptr = cur_sector_ptr;
    
    _priv->status = AUDIO_STATUS_NOSTATUS;
    
    return TRUE;
}

static gboolean __cdemud_audio_null_start (CDEMUD_Audio *self, gint start, gint end, GObject *disc, GError **error) {
    CDEMUD_Audio_NULLPrivate *_priv = CDEMUD_AUDIO_NULL_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    
    /* Lock */
    g_static_mutex_lock(&_priv->mutex);
    
    /* Play is valid only if we're not playing already or paused */
    if (_priv->status != AUDIO_STATUS_PLAYING && _priv->status != AUDIO_STATUS_PAUSED) {
        /* Set start and end sector, and disc... We should have been stopped properly
           before, which means we don't have to unref the previous disc reference */
        _priv->cur_sector = start;
        _priv->end_sector = end;
        _priv->disc = disc;
        
        /* Reference disc for the time of playing */
        g_object_ref(_priv->disc);
        
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_AUDIOPLAY, "%s: starting playback...\n", __func__);
        if (!__cdemud_audio_null_start_playing(CDEMUD_AUDIO_NULL(self), error)) {
            /* Free disc reference */
            g_object_unref(_priv->disc);
            _priv->disc = NULL;
            
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_AUDIOPLAY, "%s: failed to start playback!\n", __func__);
            succeeded = FALSE;
            goto end;
        }
    } else {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_AUDIOPLAY, "%s: play called when paused or already playing!\n", __func__);
        cdemud_error(CDEMUD_E_AUDIOINVALIDSTATE, error);
        succeeded = FALSE;
        goto end;
    }
    
end:
    /* Unlock */
    g_static_mutex_unlock(&_priv->mutex);
    
    return succeeded;
}

static gboolean __cdemud_audio_null_resume (CDEMUD_Audio *self, GError **error) {
    CDEMUD_Audio_NULLPrivate *_priv = CDEMUD_AUDIO_NULL_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    
    /* Lock */
    g_static_mutex_lock(&_priv->mutex);
    
    /* Resume is valid only if we're paused */
    if (_priv->status == AUDIO_STATUS_PAUSED) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_AUDIOPLAY, "%s: starting playback...\n", __func__);
        if (!__cdemud_audio_null_start_playing(CDEMUD_AUDIO_NULL(self), error)) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_AUDIOPLAY, "%s: failed to start playback!\n", __func__);
            succeeded = FALSE;
            goto end;
        }
    } else {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_AUDIOPLAY, "%s: resume called when not paused!\n", __func__);
        cdemud_error(CDEMUD_E_AUDIOINVALIDSTATE, error);
        succeeded = FALSE;
        goto end;
    }
    
end:
    /* Unlock */
    g_static_mutex_unlock(&_priv->mutex);
    
    return succeeded;
}

static gboolean __cdemud_audio_null_pause (CDEMUD_Audio *self, GError **error) {
    CDEMUD_Audio_NULLPrivate *_priv = CDEMUD_AUDIO_NULL_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    
    /* Lock */
    g_static_mutex_lock(&_priv->mutex);
    
    /* Pause is valid only if we are playing */
    if (_priv->status == AUDIO_STATUS_PLAYING) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_AUDIOPLAY, "%s: stopping playback...\n", __func__);
        if (__cdemud_audio_null_stop_playing(CDEMUD_AUDIO_NULL(self), error)) {
            /* Change status */
            _priv->status = AUDIO_STATUS_PAUSED;
        } else {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_AUDIOPLAY, "%s: failed to stop playback!\n", __func__);
            succeeded = FALSE;
            goto end;
        }
    } else {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_AUDIOPLAY, "%s: pause called when not playing!\n", __func__);
        cdemud_error(CDEMUD_E_AUDIOINVALIDSTATE, error);
        succeeded = FALSE;
        goto end;
    }
    
end:
    /* Unlock */
    g_static_mutex_unlock(&_priv->mutex);
    
    return succeeded;
}

static gboolean __cdemud_audio_null_stop (CDEMUD_Audio *self, GError **error) {
    CDEMUD_Audio_NULLPrivate *_priv = CDEMUD_AUDIO_NULL_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
        
    /* Lock */
    g_static_mutex_lock(&_priv->mutex);
    
    /* Stop is valid only if we are playing or paused */
    if (_priv->status == AUDIO_STATUS_PLAYING || _priv->status == AUDIO_STATUS_PAUSED) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_AUDIOPLAY, "%s: stopping playback...\n", __func__);
        if (__cdemud_audio_null_stop_playing(CDEMUD_AUDIO_NULL(self), error)) {
            /* Change status */
            _priv->status = AUDIO_STATUS_NOSTATUS;
            /* Release disc reference */
            g_object_unref(_priv->disc);
            _priv->disc = NULL;
        } else {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_AUDIOPLAY, "%s: failed to stop playback!\n", __func__);
            succeeded = FALSE;
            goto end;
        }
    } else {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_AUDIOPLAY, "%s: stop called when not playing nor paused!\n", __func__);
        cdemud_error(CDEMUD_E_AUDIOINVALIDSTATE, error);
        succeeded = FALSE;
        goto end;
    }

end:
    /* Unlock */
    g_static_mutex_unlock(&_priv->mutex);
    
    return succeeded;
}

static gboolean __cdemud_audio_null_get_status (CDEMUD_Audio *self, gint *status, GError **error) {
    CDEMUD_Audio_NULLPrivate *_priv = CDEMUD_AUDIO_NULL_GET_PRIVATE(self);
    
    /* Lock */
    g_static_mutex_lock(&_priv->mutex);
    /* Return status */
    *status = _priv->status;
    /* Unlock */
    g_static_mutex_unlock(&_priv->mutex);
    
    return TRUE;
}

/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static CDEMUD_AudioClass *parent_class = NULL;

static void __cdemud_audio_null_class_init (gpointer g_class, gpointer g_class_data) {
    CDEMUD_AudioClass *class_audio = CDEMUD_AUDIO_CLASS(g_class);
    CDEMUD_Audio_NULLClass *klass = CDEMUD_AUDIO_NULL_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(CDEMUD_Audio_NULLPrivate));
    
    /* Initialize CDEMUD_Audio methods */
    class_audio->initialize = __cdemud_audio_null_initialize;
    class_audio->start = __cdemud_audio_null_start;
    class_audio->resume = __cdemud_audio_null_resume;
    class_audio->pause = __cdemud_audio_null_pause;
    class_audio->stop = __cdemud_audio_null_stop;
    class_audio->get_status = __cdemud_audio_null_get_status;
    
    return;
}

GType cdemud_audio_null_get_type (void) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(CDEMUD_Audio_NULLClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __cdemud_audio_null_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(CDEMUD_Audio_NULL),
            0,      /* n_preallocs */
            NULL    /* instance_init */
        };
        
        type = g_type_register_static(CDEMUD_TYPE_AUDIO, "CDEMUD_Audio_NULL", &info, 0);
    }
    
    return type;
}
