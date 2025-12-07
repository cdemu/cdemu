/*
 *  libMirage: type definitions
 *  Copyright (C) 2013-2014 Rok Mandeljc
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
 * MirageSectorType:
 * @MIRAGE_SECTOR_MODE0: Mode 0 sector
 * @MIRAGE_SECTOR_AUDIO: Audio sector
 * @MIRAGE_SECTOR_MODE1: Mode 1 sector
 * @MIRAGE_SECTOR_MODE2: Mode 2 Formless sector
 * @MIRAGE_SECTOR_MODE2_FORM1: Mode 2 Form 1 sector
 * @MIRAGE_SECTOR_MODE2_FORM2: Mode 2 Form 2 sector
 * @MIRAGE_SECTOR_MODE2_MIXED: Mode 2 Mixed sector
 * @MIRAGE_SECTOR_RAW: raw sector (automatic sector type detection)
 * @MIRAGE_SECTOR_RAW_SCRAMBLED: scrambled raw sector (automatic sector type detection)
 *
 * Sector type. Also implies track mode.
 */
typedef enum _MirageSectorType
{
    MIRAGE_SECTOR_MODE0,
    MIRAGE_SECTOR_AUDIO,
    MIRAGE_SECTOR_MODE1,
    MIRAGE_SECTOR_MODE2,
    MIRAGE_SECTOR_MODE2_FORM1,
    MIRAGE_SECTOR_MODE2_FORM2,
    MIRAGE_SECTOR_MODE2_MIXED,
    MIRAGE_SECTOR_RAW,
    MIRAGE_SECTOR_RAW_SCRAMBLED,
} MirageSectorType;


G_END_DECLS
