/*
 *  CDEmuD: Audio play object
 *  Copyright (C) 2006-2010 Rok Mandeljc
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

#define __debug__ "AudioPlay"

/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define CDEMUD_AUDIO_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), CDEMUD_TYPE_AUDIO, CDEMUD_AudioPrivate))

typedef struct {    
    /* Thread */
    GThread *playback_thread;
    
    /* libao device */
    gint driver_id;
    ao_sample_format format;

    ao_device *device;
    
    /* Pointer to disc */
    GObject *disc;

    GMutex *device_mutex;
    
    /* Sector */
    gint cur_sector;
    gint end_sector;

    gint *cur_sector_ptr;
    
    /* Status */
    gint status;
    
    /* A hack to account for null driver's behaviour */
    gboolean null_hack;
} CDEMUD_AudioPrivate;


static gpointer __cdemud_audio_playback_thread (gpointer data) {
    CDEMUD_Audio *self = data;
    CDEMUD_AudioPrivate *_priv = CDEMUD_AUDIO_GET_PRIVATE(self);

    /* Open audio device */
    CDEMUD_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: opening audio device\n", __debug__);
    _priv->device = ao_open_live(_priv->driver_id, &_priv->format, NULL /* no options */);
    if (_priv->device == NULL) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to open audio device!\n", __debug__);
        return NULL;
    }
    
    /* Activate null driver hack, if needed (otherwise disable it) */
    if (_priv->driver_id == ao_driver_id("null")) {
        _priv->null_hack = TRUE;
    } else {
        _priv->null_hack = FALSE;
    }
    
    CDEMUD_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: playback thread start\n", __debug__);
    
    while (1) {
        /* Process sectors; we go over playing range, check sectors' type, keep 
           track of where we are and try to produce some sound. libao's play
           function should keep our timing */
        GObject *sector = NULL;
        GError *error = NULL;
        const guint8 *tmp_buffer = NULL;
        gint tmp_len = 0;
        gint type = 0;
        
        /* Make playback thread interruptible (i.e. if status is changed, it's
           going to end */
        if (_priv->status != AUDIO_STATUS_PLAYING) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: playback thread interrupted\n", __debug__);
            break;
        }
        
        /* Check if we have already reached the end */
        if (_priv->cur_sector > _priv->end_sector) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: playback thread reached the end\n", __debug__);
            _priv->status = AUDIO_STATUS_COMPLETED; /* Audio operation successfully completed */
            break;
        }
        
        
        /*** Lock device mutex ***/
        g_mutex_lock(_priv->device_mutex);
        
        /* Get sector */
        CDEMUD_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: playing sector %d (0x%X)\n", __debug__, _priv->cur_sector, _priv->cur_sector);
        if (!mirage_disc_get_sector(MIRAGE_DISC(_priv->disc), _priv->cur_sector, &sector, &error)) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: failed to get sector 0x%X: %s\n", __debug__, _priv->cur_sector, error->message);
            g_error_free(error);
            _priv->status = AUDIO_STATUS_ERROR; /* Audio operation stopped due to error */
            g_mutex_unlock(_priv->device_mutex);
            break;
        }
            
        /* This one covers both sector not being an audio one and sector changing 
           from audio to data one */
        mirage_sector_get_sector_type(MIRAGE_SECTOR(sector), &type, NULL);
        if (type != MIRAGE_MODE_AUDIO) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: non-audio sector!\n", __debug__);
            g_object_unref(sector); /* Unref here; we won't need it anymore... */
            _priv->status = AUDIO_STATUS_ERROR; /* Audio operation stopped due to error */
            g_mutex_unlock(_priv->device_mutex);
            break;
        }
        
        /* Save current position */
        if (_priv->cur_sector_ptr) {
            *_priv->cur_sector_ptr = _priv->cur_sector;
        }
        _priv->cur_sector++;
        
        /*** Unlock device mutex ***/
        g_mutex_unlock(_priv->device_mutex);
        
        
        /* Play sector */
        mirage_sector_get_data(MIRAGE_SECTOR(sector), &tmp_buffer, &tmp_len, NULL);
        if (ao_play(_priv->device, (gchar *)tmp_buffer, tmp_len) == 0) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_ERROR, "%s: playback error!\n", __debug__);
            _priv->status = AUDIO_STATUS_ERROR; /* Audio operation stopped due to error */
            break;
        }
        
        /* Hack: account for null driver's behaviour; for other libao drivers, ao_play
           seems to return after the data is played, which is what we rely on for our
           timing. However, null driver, as it has no device to write to, returns
           immediately. Until this is fixed in libao, we'll have to emulate the delay
           ourselves */
        if (_priv->null_hack) {
            g_usleep(1*G_USEC_PER_SEC/75); /* One sector = 1/75th of second */
        }
        
        g_object_unref(sector);
    }

    CDEMUD_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: playback thread end\n", __debug__);
    
    /* Close audio device */
    CDEMUD_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: closing audio device\n", __debug__);
    ao_close(_priv->device);
    _priv->device = 0;
    
    return NULL;
}

static gboolean __cdemud_audio_start_playing (CDEMUD_Audio *self, GError **error G_GNUC_UNUSED) {
    CDEMUD_AudioPrivate *_priv = CDEMUD_AUDIO_GET_PRIVATE(self);
    
    /* Set the status */
    _priv->status = AUDIO_STATUS_PLAYING;
    
    /* Start the playback thread; thread must be joinable, so we can wait for it
       to end */
    _priv->playback_thread = g_thread_create(__cdemud_audio_playback_thread, self, TRUE, NULL);
    
    return TRUE;
}

static gboolean __cdemud_audio_stop_playing (CDEMUD_Audio *self, gint status, GError **error G_GNUC_UNUSED) {
    CDEMUD_AudioPrivate *_priv = CDEMUD_AUDIO_GET_PRIVATE(self);
    
    /* We can't tell whether we're stopped or paused, so the upper layer needs
       to provide us appropriate status */
    _priv->status = status;
    
    /* Wait for the thread to finish */
    if (_priv->playback_thread) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: waiting for thread to finish\n", __debug__);
        g_thread_join(_priv->playback_thread);
        _priv->playback_thread = NULL;
        CDEMUD_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: thread finished\n", __debug__);
    }
    
    return TRUE;
}


/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
gboolean cdemud_audio_initialize (CDEMUD_Audio *self, gchar *driver, gint *cur_sector_ptr, GMutex *device_mutex_ptr, GError **error G_GNUC_UNUSED) {
    CDEMUD_AudioPrivate *_priv = CDEMUD_AUDIO_GET_PRIVATE(CDEMUD_AUDIO(self));
        
    _priv->cur_sector_ptr = cur_sector_ptr;
    _priv->device_mutex = device_mutex_ptr;
    
    _priv->status = AUDIO_STATUS_NOSTATUS;
	
    /* Get driver ID */
    if (!strcmp(driver, "default")) {
        _priv->driver_id = ao_default_driver_id();
    } else {
        _priv->driver_id = ao_driver_id(driver);
    }
    
    if (_priv->driver_id == -1) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: cannot find driver '%s', using 'null' instead!\n", __debug__, driver);
        _priv->driver_id = ao_driver_id("null");
    }
    
    /* Set the audio format */    
    _priv->format.bits = 16;
    _priv->format.channels = 2;
    _priv->format.rate = 44100;
    _priv->format.byte_format = AO_FMT_LITTLE;

    /* *Don't* open the device here; we'll do it when we actually start playing */

    return TRUE;
}

gboolean cdemud_audio_start (CDEMUD_Audio *self, gint start, gint end, GObject *disc, GError **error) {
    CDEMUD_AudioPrivate *_priv = CDEMUD_AUDIO_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    
    /* Lock */
   // g_static_mutex_lock(&_priv->mutex);
    
    /* Play is valid only if we're not playing already or paused */
    if (_priv->status != AUDIO_STATUS_PLAYING && _priv->status != AUDIO_STATUS_PAUSED) {
        /* Set start and end sector, and disc... We should have been stopped properly
           before, which means we don't have to unref the previous disc reference */
        _priv->cur_sector = start;
        _priv->end_sector = end;
        _priv->disc = disc;
        
        /* Reference disc for the time of playing */
        g_object_ref(_priv->disc);
        
        CDEMUD_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: starting playback (0x%X->0x%X)...\n", __debug__, _priv->cur_sector, _priv->end_sector);
        if (!__cdemud_audio_start_playing(CDEMUD_AUDIO(self), error)) {
            /* Free disc reference */
            g_object_unref(_priv->disc);
            _priv->disc = NULL;
            
            CDEMUD_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: failed to start playback!\n", __debug__);
            succeeded = FALSE;
            goto end;
        }
    } else {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: play called when paused or already playing!\n", __debug__);
        cdemud_error(CDEMUD_E_AUDIOINVALIDSTATE, error);
        succeeded = FALSE;
        goto end;
    }
    
end:
    /* Unlock */
  //  g_static_mutex_unlock(&_priv->mutex);
    
    return succeeded;
}

gboolean cdemud_audio_resume (CDEMUD_Audio *self, GError **error) {
    CDEMUD_AudioPrivate *_priv = CDEMUD_AUDIO_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    
    /* Lock */
   // g_static_mutex_lock(&_priv->mutex);
    
    /* Resume is valid only if we're paused */
    if (_priv->status == AUDIO_STATUS_PAUSED) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: resuming playback (0x%X->0x%X)...\n", __debug__);
        if (!__cdemud_audio_start_playing(CDEMUD_AUDIO(self), error)) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: failed to start playback!\n", __debug__);
            succeeded = FALSE;
            goto end;
        }
    } else {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: resume called when not paused!\n", __debug__);
        cdemud_error(CDEMUD_E_AUDIOINVALIDSTATE, error);
        succeeded = FALSE;
        goto end;
    }
    
end:
    /* Unlock */
   // g_static_mutex_unlock(&_priv->mutex);
    
    return succeeded;
}

gboolean cdemud_audio_pause (CDEMUD_Audio *self, GError **error) {
    CDEMUD_AudioPrivate *_priv = CDEMUD_AUDIO_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    
    /* Pause is valid only if we are playing */
    if (_priv->status == AUDIO_STATUS_PLAYING) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: pausing playback...\n", __debug__);
        
        if (!__cdemud_audio_stop_playing(CDEMUD_AUDIO(self), AUDIO_STATUS_PAUSED, error)) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: failed to pause playback!\n", __debug__);
            succeeded = FALSE;
            goto end;
        }
    } else {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: pause called when not playing!\n", __debug__);
        cdemud_error(CDEMUD_E_AUDIOINVALIDSTATE, error);
        succeeded = FALSE;
        goto end;
    }
    
end:    
    return succeeded;
}

gboolean cdemud_audio_stop (CDEMUD_Audio *self, GError **error) {
    CDEMUD_AudioPrivate *_priv = CDEMUD_AUDIO_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    
    /* Stop is valid only if we are playing or paused */
    if (_priv->status == AUDIO_STATUS_PLAYING || _priv->status == AUDIO_STATUS_PAUSED) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: stopping playback...\n", __debug__);
        
        if (__cdemud_audio_stop_playing(CDEMUD_AUDIO(self), AUDIO_STATUS_NOSTATUS, error)) {
            /* Release disc reference */
            g_object_unref(_priv->disc);
            _priv->disc = NULL;
        } else {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: failed to stop playback!\n", __debug__);
            succeeded = FALSE;
            goto end;
        }
    } else {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: stop called when not playing nor paused!\n", __debug__);
        cdemud_error(CDEMUD_E_AUDIOINVALIDSTATE, error);
        succeeded = FALSE;
        goto end;
    }

end:
    return succeeded;
}

gboolean cdemud_audio_get_status (CDEMUD_Audio *self, gint *status, GError **error G_GNUC_UNUSED) {
    CDEMUD_AudioPrivate *_priv = CDEMUD_AUDIO_GET_PRIVATE(self);
    
    /* Return status */
    *status = _priv->status;
    
    return TRUE;
}

/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static MIRAGE_ObjectClass *parent_class = NULL;

static void __cdemud_audio_finalize (GObject *obj) {
    CDEMUD_Audio *self = CDEMUD_AUDIO(obj);                                                
    /*CDEMUD_AudioPrivate *_priv = CDEMUD_AUDIO_GET_PRIVATE(self);*/
        
    /* Force the playback to stop */
    cdemud_audio_stop(self, NULL);
    
    /* Chain up to the parent class */
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __cdemud_audio_class_init (gpointer g_class, gpointer g_class_data G_GNUC_UNUSED) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    CDEMUD_AudioClass *klass = CDEMUD_AUDIO_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(CDEMUD_AudioPrivate));
    
    /* Initialize GObject members */
    class_gobject->finalize = __cdemud_audio_finalize;
    
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
            NULL,   /* instance_init */
            NULL,   /* value_table */
        };
        
        type = g_type_register_static(MIRAGE_TYPE_OBJECT, "CDEMUD_Audio", &info, 0);
    }
    
    return type;
}
