/*
 *  libMirage: Type definitions
 *  Copyright (C) 2013 Rok Mandeljc
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

#ifndef __MIRAGE_TYPES_H__
#define __MIRAGE_TYPES_H__

/* For now, only definitions that are shared among several headers are
   listed here, and the rest are kept in their corresponding headers. */

G_BEGIN_DECLS

typedef struct _MirageDisc MirageDisc;
typedef struct _MirageFragment MirageFragment;
typedef struct _MirageIndex MirageIndex;
typedef struct _MirageLanguage MirageLanguage;
typedef struct _MirageSector MirageSector;
typedef struct _MirageSession MirageSession;
typedef struct _MirageTrack MirageTrack;


/**
 * MirageTrackModes:
 * @MIRAGE_MODE_MODE0: Mode 0
 * @MIRAGE_MODE_AUDIO: Audio
 * @MIRAGE_MODE_MODE1: Mode 1
 * @MIRAGE_MODE_MODE2: Mode 2 Formless
 * @MIRAGE_MODE_MODE2_FORM1: Mode 2 Form 1
 * @MIRAGE_MODE_MODE2_FORM2: Mode 2 Form 2
 * @MIRAGE_MODE_MODE2_MIXED: Mode 2 Mixed
 *
 * Track modes.
 */
typedef enum _MirageTrackModes
{
    MIRAGE_MODE_MODE0       = 0x00,
    MIRAGE_MODE_AUDIO       = 0x01,
    MIRAGE_MODE_MODE1       = 0x02,
    MIRAGE_MODE_MODE2       = 0x03,
    MIRAGE_MODE_MODE2_FORM1 = 0x04,
    MIRAGE_MODE_MODE2_FORM2 = 0x05,
    MIRAGE_MODE_MODE2_MIXED = 0x06,
} MirageTrackModes;


G_END_DECLS

#endif /* __MIRAGE_TYPES_H__ */
