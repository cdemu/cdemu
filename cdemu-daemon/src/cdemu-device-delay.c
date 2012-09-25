 /*
 *  CDEmu daemon: Device object - Delay emulation
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

#include "cdemu.h"
#include "cdemu-device-private.h"

#define __debug__ "Delay Emulation"


/**********************************************************************\
 *                      Delay calculation                             *
\**********************************************************************/
static void cdemu_device_delay_increase (CdemuDevice *self, gint address, gint num_sectors)
{
    gdouble rps = 12000.0/60; /* Rotations per second; fixed at 12000 RPMs for now */
    gdouble dpm_angle = 0;
    gdouble dpm_density = 0;

    if (!mirage_disc_get_dpm_data_for_sector(MIRAGE_DISC(self->priv->disc), address, &dpm_angle, &dpm_density, NULL)) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_DELAY, "%s: failed to get DPM data for sector 0x%X\n", __debug__, address);
        return;
    }

    /* Seek delay; emulates the time the laser head needs to move to the sector
       you want to read. Related to random access. Also the part that makes
       certain copy protections believe they're dealing with the real disc.

       Essentially, number of rotations needed to seek over certain amount of
       sectors changes with the sector density. Therefore, reading time depends
       on where on the disc the seek is performed, again, in the same way that
       sector density changes.

       Copy protections seem to issue a series of seeks (actually, it's READ 10
       commands, but they skip over bunch of sectors) at different locations on
       the disc. It seems to follow the following pattern: first seek is "short"
       (~13 sectors), followed by a "long" seek (~300 sectors), and then the
       whole thing is repeated. The times per-se don't matter (as they're
       determined by drive speed and other factors), but the ratios between them
       do - they represent sector density pattern. The catch is, the ratios seem
       to be determined for "short" seeks and for "long" seeks, separately. And
       it would seem that if one of ratio sequences is close enough to expected
       sector density pattern, disc passes the test.

       Now the problem is that for long seeks, the time doesn't seem to be
       proportional to the amount of rotations. It makes sense, because to seek
       from beginning to end of the disc, it would take alot of rotations. The
       only logical conclusion would be that the laser head doesn't follow the
       spiral, like it does in case of small seeks, but takes a shortcut instead,
       thereby saving time.

       So until I figure how exactly to emulate that shortcutting time, we're
       doing it the following way: for all seeks that require less than 10
       rotations, emulate delay time that's proportional to number of rotations
       (~50 ms max). If seek requires more than 10 rotations, we "move" head so
       that it requires less than 10 rotations; head moving always requires 20 ms.
       This way, the delay shouldn't be getting longer than ~70 ms, and sector
       density measurements should still pass. */
    if (self->priv->dpm_emulation) {
        gdouble rotations = 0;

        /* Actually, if we were to read a sector we've just read, we'd have to
           perform a full rotation... but I guess we could say we've cached the
           data? */
        rotations = fabs(dpm_angle - self->priv->current_angle);
        self->priv->current_angle = dpm_angle;

        CDEMU_DEBUG(self, DAEMON_DEBUG_DELAY, "%s: 0x%X->0x%X (%d): %f rotations\n", __debug__, self->priv->current_address, address, abs(self->priv->current_address - address), rotations);

        /* We emulate moving the head if amount of rotations exceeds 10 */
        if (rotations >= 10.0) {
            /* Reduce the number of rotations */
            while (rotations >= 10.0) {
                rotations -= 10.0;
            }
            self->priv->delay_amount += 20.0*1000; /* Shortcut takes about 20 ms */
        }

        self->priv->delay_amount += rotations/rps*1000000; /* Delay, in microseconds */
    }

    /* Transfer delay; emulates the time needed to read all the sectors. Related
       to sequential access. Not really a crucial thing to emulate, but it gives
       a nice(r) CAV curve in Nero CDSpeed.

       This works on the same principle as the seek delay emulation above. It
       could've been done by emulating seek delay for every sector to be read,
       but it takes less function calls to do it here this way...
    */
    if (self->priv->tr_emulation) {
        gdouble spr = 360.0/dpm_density; /* Sectors per rotation */
        gdouble sps = spr*rps; /* Sectors per second */

        CDEMU_DEBUG(self, DAEMON_DEBUG_DELAY, "%s: %d sectors at %f sectors/second\n", __debug__, num_sectors, sps);
        self->priv->delay_amount += num_sectors/sps*1000000; /* Delay, in microseconds */
    }
}


/**********************************************************************\
 *                          Delay API                                 *
\**********************************************************************/
void cdemu_device_delay_begin (CdemuDevice *self, gint address, gint num_sectors)
{
    /* Simply get current time here; we'll need it to compensate for processing
       time when performing actual delay */
    g_get_current_time(&self->priv->delay_begin);

    /* Reset delay */
    self->priv->delay_amount = 0;

    /* Increase delay */
    cdemu_device_delay_increase(self, address, num_sectors);
}

void cdemu_device_delay_finalize (CdemuDevice *self)
{
    GTimeVal delay_now;
    GTimeVal delay_diff;

    gint delay;

    /* If there's no delay to perform, don't bother doing anything... */
    if (!self->priv->delay_amount) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_DELAY, "%s: no delay to perform\n", __debug__);
        return;
    }

    /* Get current time */
    g_get_current_time(&delay_now);

    /* Calculate time difference */
    delay_diff.tv_sec = delay_now.tv_sec - self->priv->delay_begin.tv_sec;
    delay_diff.tv_usec = delay_now.tv_usec - self->priv->delay_begin.tv_usec;

    CDEMU_DEBUG(self, DAEMON_DEBUG_DELAY, "%s: calculated delay: %i microseconds\n", __debug__, self->priv->delay_amount);
    CDEMU_DEBUG(self, DAEMON_DEBUG_DELAY, "%s: processing time: %i seconds, %i microseconds\n", __debug__, delay_diff.tv_sec, delay_diff.tv_usec);

    /* Compensate for the processing time */
    delay = self->priv->delay_amount - (delay_diff.tv_sec * G_USEC_PER_SEC + delay_diff.tv_usec);
    CDEMU_DEBUG(self, DAEMON_DEBUG_DELAY, "%s: actual delay: %i microseconds\n", __debug__, delay);

    if (delay < 0) {
        CDEMU_DEBUG(self, DAEMON_DEBUG_DELAY, "%s: spent too much time processing, bailing out!\n", __debug__);
        return;
    }

    g_usleep(delay);
}

