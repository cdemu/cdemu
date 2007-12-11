/*
 *  CDEmuD: Debugging
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

#ifndef __CDEMUD_DEBUG_H__
#define __CDEMUD_DEBUG_H__

/* Debug masks */
typedef enum {
    /* Debug types; need to be same as in libMirage because we use its debug context */
    DAEMON_DEBUG_ERROR         = MIRAGE_DEBUG_ERROR,
    DAEMON_DEBUG_WARNING       = MIRAGE_DEBUG_WARNING,
    /* Debug masks */
    DAEMON_DEBUG_DEV_PC_FIXME  = 0x0001,
    DAEMON_DEBUG_DEV_PC_TRACE  = 0x0002,
    DAEMON_DEBUG_DEV_PC_DUMP   = 0x0004,
    DAEMON_DEBUG_DEV_AUDIOPLAY = 0x0008
} CDEMUD_DeviceDebugMasks;

/* Debug macro */
#define CDEMUD_DEBUG(obj, lvl, format, msg...) {                        \
    mirage_object_debug_message(MIRAGE_OBJECT(obj), lvl, format, msg);   \
}

//#define CDEMUD_DEBUG_ON(dev, lvl) (CDEMUD_DEVICE_GET_PRIVATE(dev)->debug_mask & lvl)

#endif /* __CDEMUD_DEBUG_H__ */
