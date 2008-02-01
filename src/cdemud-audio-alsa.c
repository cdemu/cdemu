/*
 *  CDEmuD: ALSA audio play object
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

#ifdef ALSA_BACKEND

/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define CDEMUD_AUDIO_ALSA_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), CDEMUD_TYPE_AUDIO_ALSA, CDEMUD_Audio_ALSAPrivate))

typedef struct {    
    /* Mutex */
    GStaticMutex mutex;

    /* PCM */
    snd_pcm_t *handle;
	snd_pcm_hw_params_t *hwparams;
	snd_pcm_sw_params_t *swparams;
    snd_pcm_uframes_t buffer_size;
    snd_pcm_uframes_t period_size;
    snd_async_handler_t *ahandler;

    /* Pointer to disc */
    GObject *disc;
    
    /* Sector */
    gint cur_sector;
    gint end_sector;
    
    gint *cur_sector_ptr;
    
    gint status;
} CDEMUD_Audio_ALSAPrivate;



static gboolean __cdemud_audio_alsa_feed_buffer (CDEMUD_Audio_ALSA *self, GError **error) {
    CDEMUD_Audio_ALSAPrivate *_priv = CDEMUD_AUDIO_ALSA_GET_PRIVATE(CDEMUD_AUDIO_ALSA(self));
    gint err = 0;
    snd_pcm_uframes_t avail = 0;
    
    avail = snd_pcm_avail_update(_priv->handle);
    /* Since we're writing data in fixed 588 chunks (regardless of period), 
       we need to check against that and not period */
    while (avail >= 588) {
        GObject *sector = NULL;
        GError *error = NULL;
        
        gint sector_type = 0;
        guint8 *tmp_buf = NULL;
        gint tmp_len = 0;
        
        /* If we have reached the end, return FALSE... */
        if (_priv->cur_sector > _priv->end_sector) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_AUDIOPLAY, "%s: reached the end\n", __func__);
            _priv->status = AUDIO_STATUS_COMPLETED;
            return FALSE;
        }
        
        if (!mirage_disc_get_sector(MIRAGE_DISC(_priv->disc),  _priv->cur_sector, &sector, &error)) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_AUDIOPLAY, "%s: failed to get sector 0x%X: %s\n", __func__, _priv->cur_sector, error->message);
            g_error_free(error);
            _priv->status = AUDIO_STATUS_ERROR;
            return FALSE;
        }
        
        mirage_sector_get_sector_type(MIRAGE_SECTOR(sector), &sector_type, NULL);
        
        if (sector_type == MIRAGE_MODE_AUDIO) {
            mirage_sector_get_data(MIRAGE_SECTOR(sector), &tmp_buf, &tmp_len, NULL);
            err = snd_pcm_writei(_priv->handle, tmp_buf, 588);
            if (err != 588) {
                CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_AUDIOPLAY, "%s: failed to write whole sector!\n", __func__);
                _priv->status = AUDIO_STATUS_ERROR;
                return FALSE;
            }
        } else {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_AUDIOPLAY, "%s: non audio sector!\n", __func__);
            _priv->status = AUDIO_STATUS_ERROR;
            return FALSE;
        }
        
        g_object_unref(sector);
        if (_priv->cur_sector_ptr) {
            *_priv->cur_sector_ptr = _priv->cur_sector;
        }
        _priv->cur_sector++;
        
        avail = snd_pcm_avail_update(_priv->handle);
    }
    
    _priv->status = AUDIO_STATUS_PLAYING;
    return TRUE;
}


static gboolean __cdemud_audio_alsa_set_hwparams (CDEMUD_Audio_ALSA *self, GError **error) {
    CDEMUD_Audio_ALSAPrivate *_priv = CDEMUD_AUDIO_ALSA_GET_PRIVATE(CDEMUD_AUDIO_ALSA(self));
    gint err;
    
    gint dir;
    guint rate = 44100, rrate;
    guint channels = 2;
    snd_pcm_uframes_t period_size = 588; /* Period size in frames (588 frames = 1 CD-ROM sector) */
    snd_pcm_uframes_t buffer_size = 588*10; /* Ring buffer size in frames */
    
	/* Get current parameters */
	err = snd_pcm_hw_params_any(_priv->handle, _priv->hwparams);
	if (err < 0) {
		CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: broken configuration for playback: no configuration available: %s\n", __func__, snd_strerror(err));
		return FALSE;
	}
    
	/* Set hardware resampling */
	err = snd_pcm_hw_params_set_rate_resample(_priv->handle, _priv->hwparams, 1);
	if (err < 0) {
		CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: resampling setup failed for playback: %s\n", __func__, snd_strerror(err));
		return FALSE;
	}
    
	/* Set the interleaved read/write format */
	err = snd_pcm_hw_params_set_access(_priv->handle, _priv->hwparams, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0) {
		CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: access type not available for playback: %s\n", __func__, snd_strerror(err));
		return FALSE;
	}
    
	/* Set the sample format */
	err = snd_pcm_hw_params_set_format(_priv->handle, _priv->hwparams, SND_PCM_FORMAT_S16);
	if (err < 0) {
		CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: sample format not available for playback: %s\n", __func__, snd_strerror(err));
		return FALSE;
	}
    
	/* Set the count of channels */
	err = snd_pcm_hw_params_set_channels(_priv->handle, _priv->hwparams, channels);
	if (err < 0) {
		CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: channels count (%i) not available for playbacks: %s\n", __func__, channels, snd_strerror(err));
		return FALSE;
	}
    
	/* Set the stream rate */
    rrate = rate;
	err = snd_pcm_hw_params_set_rate_near(_priv->handle, _priv->hwparams, &rrate, 0);
	if (err < 0) {
		CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: rate %iHz not available for playback: %s\n", __func__, rate, snd_strerror(err));
		return FALSE;
	}
	if (rrate != rate) {
		CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: rate doesn't match (requested %iHz, got %iHz)\n", __func__, rate, err);
		return FALSE;
	}
    
	/* Set the buffer time */
	err = snd_pcm_hw_params_set_buffer_size_near(_priv->handle, _priv->hwparams, &buffer_size);
	if (err < 0) {
		CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to set buffer frames %li for playback: %s\n", __func__, buffer_size, snd_strerror(err));
		return FALSE;
	}
	err = snd_pcm_hw_params_get_buffer_size(_priv->hwparams, &_priv->buffer_size);
	if (err < 0) {
		CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to get buffer size for playback: %s\n", __func__, snd_strerror(err));
		return FALSE;
	}
    
	/* Set the period time */
	err = snd_pcm_hw_params_set_period_size_near(_priv->handle, _priv->hwparams, &period_size, &dir);
	if (err < 0) {
		CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to set period time %li for playback: %s\n", __func__, period_size, snd_strerror(err));
		return FALSE;
	}
	err = snd_pcm_hw_params_get_period_size(_priv->hwparams, &_priv->period_size, &dir);
	if (err < 0) {
		CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to get period size for playback: %s\n", __func__, snd_strerror(err));
		return FALSE;
	}
    
	/* Write the parameters to device */
	err = snd_pcm_hw_params(_priv->handle, _priv->hwparams);
	if (err < 0) {
		CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to set hw params for playback: %s\n", __func__, snd_strerror(err));
		return FALSE;
	}
    
	return TRUE;
}


static gboolean __cdemud_audio_alsa_set_swparams (CDEMUD_Audio_ALSA *self, GError **error) {
    CDEMUD_Audio_ALSAPrivate *_priv = CDEMUD_AUDIO_ALSA_GET_PRIVATE(CDEMUD_AUDIO_ALSA(self));
    gint err;

	/* Get current parameters */
	err = snd_pcm_sw_params_current(_priv->handle, _priv->swparams);
	if (err < 0) {
		CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to determine current swparams for playback: %s\n", __func__, snd_strerror(err));
		return FALSE;
	}
    
	/* Start the transfer when the buffer is almost full */
	err = snd_pcm_sw_params_set_start_threshold(_priv->handle, _priv->swparams, 0/*(_priv->buffer_size / _priv->period_size) * _priv->period_size*/);
	if (err < 0) {
		CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to set start threshold mode for playback: %s\n", __func__, snd_strerror(err));
		return FALSE;
	}
    
	/* Allow the transfer when at least one CD-ROM sector of samples can be processed */
	err = snd_pcm_sw_params_set_avail_min(_priv->handle, _priv->swparams, 588/*_priv->period_size*/);
	if (err < 0) {
		CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to set avail min for playback: %s\n", __func__, snd_strerror(err));
		return FALSE;
	}
    
	/* Align all transfers to 1 sample */
	err = snd_pcm_sw_params_set_xfer_align(_priv->handle, _priv->swparams, 1);
	if (err < 0) {
		CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to set transfer align for playback: %s\n", __func__, snd_strerror(err));
		return FALSE;
	}
    
	/* Write the parameters to the playback device */
	err = snd_pcm_sw_params(_priv->handle, _priv->swparams);
	if (err < 0) {
		CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to set sw params for playback: %s\n", __func__, snd_strerror(err));
		return FALSE;
	}
    
	return TRUE;
}

static gboolean __cdemud_audio_alsa_start_playing (CDEMUD_Audio_ALSA *self, GError **error) {
    CDEMUD_Audio_ALSAPrivate *_priv = CDEMUD_AUDIO_ALSA_GET_PRIVATE(CDEMUD_AUDIO_ALSA(self));
    gint err = 0;
        
    err = snd_pcm_prepare(_priv->handle);
	if (err < 0) {
		CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: snd_pcm_prepare error: %s\n", __func__, snd_strerror(err));
        return FALSE;
	}
    
	err = snd_pcm_start(_priv->handle);
	if (err < 0) {
		CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: snd_pcm_start error: %s\n", __func__, snd_strerror(err));
        return FALSE;
	}
    
    /* Set the status */
    _priv->status = AUDIO_STATUS_PLAYING;
    /* Try feeding buffer */
    if (!__cdemud_audio_alsa_feed_buffer(CDEMUD_AUDIO_ALSA(self), error)) {
        return FALSE;
    }
    
    return TRUE;
}

static gboolean __cdemud_audio_alsa_stop_playing (CDEMUD_Audio_ALSA *self, GError **error) {
    CDEMUD_Audio_ALSAPrivate *_priv = CDEMUD_AUDIO_ALSA_GET_PRIVATE(CDEMUD_AUDIO_ALSA(self));
    gint err = 0;
    
    if ((err = snd_pcm_drop(_priv->handle)) < 0) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: snd_pcm_drop error: %s\n", __func__, snd_strerror(err));
        cdemud_error(CDEMUD_E_GENERIC, error);
        return FALSE;
    }
    
    /* We can't tell whether we're stopped or paused, so the upper layer needs
       to set appropriate status */
    
    return TRUE;
}

static void __cdemud_audio_alsa_async_callback (snd_async_handler_t *ahandler) {
    CDEMUD_Audio_ALSA *self = snd_async_handler_get_callback_private(ahandler);
    CDEMUD_Audio_ALSAPrivate *_priv = CDEMUD_AUDIO_ALSA_GET_PRIVATE(CDEMUD_AUDIO_ALSA(self));
    
    /* Lock */
    g_static_mutex_lock(&_priv->mutex);
    
    /* If we're playing, we feed buffer */
    if (_priv->status == AUDIO_STATUS_PLAYING) {
        /* Feed buffer... and if that fails, stop the playback */
        if (!__cdemud_audio_alsa_feed_buffer(CDEMUD_AUDIO_ALSA(self), NULL)) {
            CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_AUDIOPLAY, "%s: feed buffer returned FALSE...\n", __func__);
            /* Stop playing */
            if (__cdemud_audio_alsa_stop_playing(CDEMUD_AUDIO_ALSA(self), NULL)) {
                /* Release disc reference */
                g_object_unref(_priv->disc);
                _priv->disc = NULL;
            } else {
                CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_AUDIOPLAY, "%s: failed to stop playback, consider ourselves FUBAR!\n", __func__);
                goto end;
            }
        }
    } else {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_AUDIOPLAY, "%s: asynchronous callback called when we're not supposed to be playing... but we're safe due to mutex!\n", __func__);        
    }
    
end:
    /* Unlock */
    g_static_mutex_unlock(&_priv->mutex);
    
    return;
}


/******************************************************************************\
 *
\******************************************************************************/
static gboolean __cdemud_audio_alsa_initialize (CDEMUD_Audio *self, gchar *device, gint *cur_sector_ptr, GError **error) {
    CDEMUD_Audio_ALSAPrivate *_priv = CDEMUD_AUDIO_ALSA_GET_PRIVATE(CDEMUD_AUDIO_ALSA(self));
    gint err = 0;  
        
    /* Set object parameters */
    _priv->cur_sector_ptr = cur_sector_ptr;
    if (!device) {
        device = "default";
    }
    
    /* Init device mutex */
    g_static_mutex_init(&_priv->mutex);
    
    /* Allocate hardware and software params */
    snd_pcm_hw_params_alloca(&_priv->hwparams);
    snd_pcm_sw_params_alloca(&_priv->swparams);
    
    /* Open PCM */
    if ((err = snd_pcm_open(&_priv->handle, device, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to open device '%s': %s\n", __func__, device, snd_strerror(err));
        return FALSE;
    }
    
    /* Set hardware parameters */
    if (!__cdemud_audio_alsa_set_hwparams(CDEMUD_AUDIO_ALSA(self), error)) {
        return FALSE;
    }
    
    /* Set software parameters */
    if (!__cdemud_audio_alsa_set_swparams(CDEMUD_AUDIO_ALSA(self), error)) {
        return FALSE;
    }
    
    /* Register asynchronous handler */
	err = snd_async_add_pcm_handler(&_priv->ahandler, _priv->handle, __cdemud_audio_alsa_async_callback, self);
	if (err < 0) {
		CDEMUD_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to register async handler: %s\n", __func__, snd_strerror(err));
		return FALSE;
	}
    
    _priv->status = AUDIO_STATUS_NOSTATUS;
    
    return TRUE;
}

static gboolean __cdemud_audio_alsa_start (CDEMUD_Audio *self, gint start, gint end, GObject *disc, GError **error) {
    CDEMUD_Audio_ALSAPrivate *_priv = CDEMUD_AUDIO_ALSA_GET_PRIVATE(self);
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
        if (!__cdemud_audio_alsa_start_playing(CDEMUD_AUDIO_ALSA(self), error)) {
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

static gboolean __cdemud_audio_alsa_resume (CDEMUD_Audio *self, GError **error) {
    CDEMUD_Audio_ALSAPrivate *_priv = CDEMUD_AUDIO_ALSA_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    
    /* Lock */
    g_static_mutex_lock(&_priv->mutex);
    
    /* Resume is valid only if we're paused */
    if (_priv->status == AUDIO_STATUS_PAUSED) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_AUDIOPLAY, "%s: starting playback...\n", __func__);
        if (!__cdemud_audio_alsa_start_playing(CDEMUD_AUDIO_ALSA(self), error)) {
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

static gboolean __cdemud_audio_alsa_pause (CDEMUD_Audio *self, GError **error) {
    CDEMUD_Audio_ALSAPrivate *_priv = CDEMUD_AUDIO_ALSA_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    
    /* Lock */
    g_static_mutex_lock(&_priv->mutex);
    
    /* Pause is valid only if we are playing */
    if (_priv->status == AUDIO_STATUS_PLAYING) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_AUDIOPLAY, "%s: stopping playback...\n", __func__);
        if (__cdemud_audio_alsa_stop_playing(CDEMUD_AUDIO_ALSA(self), error)) {
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

static gboolean __cdemud_audio_alsa_stop (CDEMUD_Audio *self, GError **error) {
    CDEMUD_Audio_ALSAPrivate *_priv = CDEMUD_AUDIO_ALSA_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
        
    /* Lock */
    g_static_mutex_lock(&_priv->mutex);
    
    /* Stop is valid only if we are playing or paused */
    if (_priv->status == AUDIO_STATUS_PLAYING || _priv->status == AUDIO_STATUS_PAUSED) {
        CDEMUD_DEBUG(self, DAEMON_DEBUG_DEV_AUDIOPLAY, "%s: stopping playback...\n", __func__);
        if (__cdemud_audio_alsa_stop_playing(CDEMUD_AUDIO_ALSA(self), error)) {
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

static gboolean __cdemud_audio_alsa_get_status (CDEMUD_Audio *self, gint *status, GError **error) {
    CDEMUD_Audio_ALSAPrivate *_priv = CDEMUD_AUDIO_ALSA_GET_PRIVATE(self);
    
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

static void __cdemud_audio_alsa_finalize (GObject *obj) {
    CDEMUD_Audio_ALSA *self = CDEMUD_AUDIO_ALSA(obj);                                                
    CDEMUD_Audio_ALSAPrivate *_priv = CDEMUD_AUDIO_ALSA_GET_PRIVATE(self);
        
    snd_pcm_close(_priv->handle);
    
    /* Chain up to the parent class */
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __cdemud_audio_alsa_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    CDEMUD_AudioClass *class_audio = CDEMUD_AUDIO_CLASS(g_class);
    CDEMUD_Audio_ALSAClass *klass = CDEMUD_AUDIO_ALSA_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(CDEMUD_Audio_ALSAPrivate));
    
    /* Initialize GObject members */
    class_gobject->finalize = __cdemud_audio_alsa_finalize;
    
    /* Initialize CDEMUD_Audio methods */
    class_audio->initialize = __cdemud_audio_alsa_initialize;
    class_audio->start = __cdemud_audio_alsa_start;
    class_audio->resume = __cdemud_audio_alsa_resume;
    class_audio->pause = __cdemud_audio_alsa_pause;
    class_audio->stop = __cdemud_audio_alsa_stop;
    class_audio->get_status = __cdemud_audio_alsa_get_status;
    
    return;
}

GType cdemud_audio_alsa_get_type (void) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(CDEMUD_Audio_ALSAClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __cdemud_audio_alsa_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(CDEMUD_Audio_ALSA),
            0,      /* n_preallocs */
            NULL    /* instance_init */
        };
        
        type = g_type_register_static(CDEMUD_TYPE_AUDIO, "CDEMUD_Audio_ALSA", &info, 0);
    }
    
    return type;
}

#endif /* ALSA_BACKEND */
