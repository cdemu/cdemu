/*
 *  CDEmu daemon: audio - private
 *  Copyright (C) 2012-2014 Rok Mandeljc
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

#pragma once

struct _CdemuAudioPrivate
{
    /* Thread */
    GThread *playback_thread;

    /* libao device */
    gint driver_id;
    ao_sample_format format;

    ao_device *device;

    /* Pointer to disc */
    MirageDisc *disc;

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
