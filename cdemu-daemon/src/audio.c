/*
 *  CDEmu daemon: audio
 *  Copyright (C) 2006-2014 Rok Mandeljc
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "cdemu.h"
#include "audio-private.h"

#define __debug__ "AudioPlay"


/**********************************************************************\
 *                          Playback functions                        *
\**********************************************************************/
static gpointer cdemu_audio_playback_thread (CdemuAudio *self)
{
    gint audio_driver_id = self->priv->driver_id;

    /* Open audio device */
    CDEMU_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: opening audio device\n", __debug__);
    self->priv->device = ao_open_live(audio_driver_id, &self->priv->format, NULL /* no options */);
    if (self->priv->device == NULL) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to open audio device; falling back to null driver!\n", __debug__);

        /* Retry with null */
        audio_driver_id = ao_driver_id("null");
        self->priv->device = ao_open_live(audio_driver_id, &self->priv->format, NULL /* no options */);
        if (self->priv->device == NULL) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to open 'null' audio device!\n", __debug__);
            return NULL;
        }
    }

    /* Activate null driver hack, if needed (otherwise disable it) */
    if (audio_driver_id == ao_driver_id("null")) {
        self->priv->null_hack = TRUE;
    } else {
        self->priv->null_hack = FALSE;
    }

    CDEMU_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: playback thread start\n", __debug__);

    while (1) {
        /* Process sectors; we go over playing range, check sectors' type, keep
           track of where we are and try to produce some sound. libao's play
           function should keep our timing */
        MirageSector *sector;
        GError *error = NULL;
        const guint8 *tmp_buffer;
        gint tmp_len;
        gint type;

        /* Make playback thread interruptible (i.e. if status is changed, it's
           going to end */
        if (self->priv->status != AUDIO_STATUS_PLAYING) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: playback thread interrupted\n", __debug__);
            break;
        }

        /* Check if we have already reached the end */
        if (self->priv->cur_sector > self->priv->end_sector) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: playback thread reached the end\n", __debug__);
            self->priv->status = AUDIO_STATUS_COMPLETED; /* Audio operation successfully completed */
            break;
        }


        /*** Lock device mutex ***/
        g_mutex_lock(self->priv->device_mutex);

        /* Get sector */
        CDEMU_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: playing sector %d (0x%X)\n", __debug__, self->priv->cur_sector, self->priv->cur_sector);
        sector = mirage_disc_get_sector(self->priv->disc, self->priv->cur_sector, &error);
        if (!sector) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: failed to get sector 0x%X: %s\n", __debug__, self->priv->cur_sector, error->message);
            g_error_free(error);
            self->priv->status = AUDIO_STATUS_ERROR; /* Audio operation stopped due to error */
            g_mutex_unlock(self->priv->device_mutex);
            break;
        }

        /* This one covers both sector not being an audio one and sector changing
           from audio to data one */
        type = mirage_sector_get_sector_type(sector);
        if (type != MIRAGE_SECTOR_AUDIO) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: non-audio sector!\n", __debug__);
            g_object_unref(sector); /* Unref here; we won't need it anymore... */
            self->priv->status = AUDIO_STATUS_ERROR; /* Audio operation stopped due to error */
            g_mutex_unlock(self->priv->device_mutex);
            break;
        }

        /* Save current position */
        if (self->priv->cur_sector_ptr) {
            *self->priv->cur_sector_ptr = self->priv->cur_sector;
        }
        self->priv->cur_sector++;

        /*** Unlock device mutex ***/
        g_mutex_unlock(self->priv->device_mutex);


        /* Play sector */
        mirage_sector_get_data(sector, &tmp_buffer, &tmp_len, NULL);
        if (ao_play(self->priv->device, (gchar *)tmp_buffer, tmp_len) == 0) {
            CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: playback error!\n", __debug__);
            self->priv->status = AUDIO_STATUS_ERROR; /* Audio operation stopped due to error */
            break;
        }

        /* Hack: account for null driver's behaviour; for other libao drivers, ao_play
           seems to return after the data is played, which is what we rely on for our
           timing. However, null driver, as it has no device to write to, returns
           immediately. Until this is fixed in libao, we'll have to emulate the delay
           ourselves */
        if (self->priv->null_hack) {
            g_usleep(1*G_USEC_PER_SEC/75); /* One sector = 1/75th of second */
        }

        g_object_unref(sector);
    }

    CDEMU_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: playback thread end\n", __debug__);

    /* Close audio device */
    CDEMU_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: closing audio device\n", __debug__);
    ao_close(self->priv->device);
    self->priv->device = 0;

    return NULL;
}

static void cdemu_audio_join_thread(CdemuAudio *self) {
    if (!self->priv->playback_thread)
        return;

    CDEMU_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: waiting for thread to finish\n", __debug__);
    g_thread_join(self->priv->playback_thread);
    self->priv->playback_thread = NULL;
    CDEMU_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: thread finished\n", __debug__);
}

static void cdemu_audio_start_playing (CdemuAudio *self)
{
    GError *local_error = NULL;

    /* Wait for an old thread to finish before starting a new one; Otherwise we get
       a race between audio setup and teardown. */
    cdemu_audio_join_thread(self);

    /* Set the status */
    self->priv->status = AUDIO_STATUS_PLAYING;

    /* Start the playback thread; thread must be joinable, so we can wait for it
       to end */
    self->priv->playback_thread = g_thread_try_new("CDEmu Device Audio Play thread", (GThreadFunc)cdemu_audio_playback_thread, self, &local_error);

    if (!self->priv->playback_thread) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: failed to create audio playback thread: %s\n", __debug__, local_error->message);
        g_error_free(local_error);
    }
}

static void cdemu_audio_stop_playing (CdemuAudio *self, gint status)
{
    /* We can't tell whether we're stopped or paused, so the upper layer needs
       to provide us appropriate status */
    self->priv->status = status;
    cdemu_audio_join_thread(self);
}


/**********************************************************************\
 *                                 Public API                         *
\**********************************************************************/
/* NOTE: these functions are called from packet-command implementations,
   and therefore with device_mutex held! */
void cdemu_audio_initialize (CdemuAudio *self, const gchar *driver, gint *cur_sector_ptr, GMutex *device_mutex_ptr)
{
    self->priv->cur_sector_ptr = cur_sector_ptr;
    self->priv->device_mutex = device_mutex_ptr;

    self->priv->status = AUDIO_STATUS_NOSTATUS;

    /* Get driver ID */
    if (!g_strcmp0(driver, "default")) {
        self->priv->driver_id = ao_default_driver_id();
    } else {
        self->priv->driver_id = ao_driver_id(driver);
    }

    if (self->priv->driver_id == -1) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_WARNING, "%s: cannot find driver '%s', using 'null' instead!\n", __debug__, driver);
        self->priv->driver_id = ao_driver_id("null");
    }

    /* Set the audio format */
    self->priv->format.bits = 16;
    self->priv->format.channels = 2;
    self->priv->format.rate = 44100;
    self->priv->format.byte_format = AO_FMT_LITTLE;

    /* *Don't* open the device here; we'll do it when we actually start playing */
}

gboolean cdemu_audio_start (CdemuAudio *self, gint start, gint end, MirageDisc *disc)
{
    gboolean succeeded = TRUE;

    /* Play is valid only if we're not playing already or paused */
    if (self->priv->status != AUDIO_STATUS_PLAYING && self->priv->status != AUDIO_STATUS_PAUSED) {
        /* Set start and end sector, and disc... We should have been stopped properly
           before, which means we don't have to unref the previous disc reference */
        self->priv->cur_sector = start;
        self->priv->end_sector = end;
        self->priv->disc = g_object_ref(disc); /* Reference disc for the time of playing */

        CDEMU_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: starting playback (0x%X->0x%X)...\n", __debug__, self->priv->cur_sector, self->priv->end_sector);
        cdemu_audio_start_playing(self);
    } else {
        CDEMU_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: play called when paused or already playing!\n", __debug__);
        succeeded = FALSE;
    }

    return succeeded;
}

gboolean cdemu_audio_resume (CdemuAudio *self)
{
    gboolean succeeded = TRUE;

    /* Resume is valid only if we're paused */
    if (self->priv->status == AUDIO_STATUS_PAUSED) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: resuming playback (0x%X->0x%X)...\n", __debug__, self->priv->cur_sector, self->priv->end_sector);
        cdemu_audio_start_playing(self);
    } else {
        CDEMU_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: resume called when not paused!\n", __debug__);
        succeeded = FALSE;
    }

    return succeeded;
}

gboolean cdemu_audio_pause (CdemuAudio *self)
{
    gboolean succeeded = TRUE;

    /* Pause is valid only if we are playing */
    if (self->priv->status == AUDIO_STATUS_PLAYING) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: pausing playback...\n", __debug__);
        cdemu_audio_stop_playing(self, AUDIO_STATUS_PAUSED);
    } else {
        CDEMU_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: pause called when not playing!\n", __debug__);
        succeeded = FALSE;
    }

    return succeeded;
}

gboolean cdemu_audio_stop (CdemuAudio *self)
{
    gboolean succeeded = TRUE;

    /* Stop is valid only if we are playing or paused */
    if (self->priv->status == AUDIO_STATUS_PLAYING || self->priv->status == AUDIO_STATUS_PAUSED) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: stopping playback...\n", __debug__);
        cdemu_audio_stop_playing(self, AUDIO_STATUS_NOSTATUS);
        /* Release disc reference */
        g_object_unref(self->priv->disc);
        self->priv->disc = NULL;
    } else {
        CDEMU_DEBUG(self, DAEMON_DEBUG_AUDIOPLAY, "%s: stop called when not playing nor paused!\n", __debug__);
        succeeded = FALSE;
    }

    return succeeded;
}

gint cdemu_audio_get_status (CdemuAudio *self)
{
    /* Check if there is an old playback thread and reap it */
    if (self->priv->status == AUDIO_STATUS_COMPLETED || self->priv->status == AUDIO_STATUS_ERROR) {
        cdemu_audio_join_thread(self);
    }

    /* Return status */
    return self->priv->status;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE_WITH_PRIVATE(CdemuAudio, cdemu_audio, MIRAGE_TYPE_OBJECT)

static void cdemu_audio_init (CdemuAudio *self)
{
    self->priv = cdemu_audio_get_instance_private(self);

    self->priv->playback_thread = NULL;
    self->priv->device = NULL;
    self->priv->disc = NULL;
    self->priv->device_mutex = NULL;
}

static void cdemu_audio_finalize (GObject *gobject)
{
    CdemuAudio *self = CDEMU_AUDIO(gobject);

    /* Force the playback to stop */
    cdemu_audio_stop(self);

    /* Chain up to the parent class */
    G_OBJECT_CLASS(cdemu_audio_parent_class)->finalize(gobject);
}

static void cdemu_audio_class_init (CdemuAudioClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = cdemu_audio_finalize;
}
