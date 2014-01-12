/*
 *  CDEmu daemon: Type definitions
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __CDEMU_TYPES_H__
#define __CDEMU_TYPES_H__

/* For now, only definitions that are shared among several headers are
   listed here, and the rest are kept in their corresponding headers. */

G_BEGIN_DECLS

typedef struct _CdemuAudio CdemuAudio;
typedef struct _CdemuDevice CdemuDevice;
typedef struct _CdemuCommand CdemuCommand;
typedef struct _CdemuRecording CdemuRecording;

G_END_DECLS

#endif /* __CDEMU_TYPES_H__ */
