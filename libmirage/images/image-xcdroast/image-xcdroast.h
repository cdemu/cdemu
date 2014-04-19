/*
 *  libMirage: X-CD-Roast image
 *  Copyright (C) 2009-2014 Rok Mandeljc
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

#ifndef __IMAGE_XCDROAST_H__
#define __IMAGE_XCDROAST_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>

#include <mirage/mirage.h>

#include "parser.h"


G_BEGIN_DECLS

typedef struct
{
    gchar *cdtitle;
    gint cdsize;
    gchar *discid;
} DISC_Info;

typedef struct
{
    gint number;
    gint type;
    gint size;
    gint startsec;
    gchar *file;
} TOC_Track;

typedef enum {
    DATA  = 0,
    AUDIO = 1
} TrackType;

typedef struct
{
    gchar *file;
    gint track;
    gint num_tracks;

    gchar *title;
    gchar *artist;

    gint size;

    gint type; /* 0: data,  1: audio */

    gint rec_type; /* 0: incremental, 1: uninterrupted */

    gint preemp; /* 0: linear, 1: preemp */
    gint copyperm; /* 0: denied, 1: allowed */
    gint stereo; /* 0: quadro, 1: stereo */

    gchar *cd_title;
    gchar *cd_artist;
    gchar *cd_discid;
} XINF_Track;

G_END_DECLS

#endif /* __IMAGE_XCDROAST_H__ */
