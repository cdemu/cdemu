/*
 *  CDEmuD: Audio play object - private
 *  Copyright (C) 2012 Rok Mandeljc
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

#ifndef __CDEMUD_AUDIO_PRIVATE_H__
#define __CDEMUD_AUDIO_PRIVATE_H__

#define CDEMUD_AUDIO_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), CDEMUD_TYPE_AUDIO, CDEMUD_AudioPrivate))

struct _CDEMUD_AudioPrivate
{
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
};

#endif /* __CDEMUD_AUDIO_PRIVATE_H__ */
