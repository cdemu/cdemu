/*
 *  libMirage: Debugging facilities
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __MIRAGE_DEBUG_H__
#define __MIRAGE_DEBUG_H__

/**
 * SECTION: mirage-debug
 * @title: Debug
 * @short_description: Debugging facilities.
 * @see_also: #MirageContext, #MirageContextual
 * @include: mirage-debug.h
 *
 * libMirage supports changing of debug message verbosity on fly,
 * without the need to restart the application. This is achieved by using
 * #MirageContext objects, which are attached to objects implementing
 * #MirageContextual interface. Such an object is #MirageObject, which
 * can obtain context from its parent. This way, all objects in the
 * hierarchy share the same context, and same debug verbosity setting.
 *
 * Debug verbosity can be controlled via mask, which can be set to the
 * context using mirage_context_set_debug_mask(). See #MirageDebugMasks.
 * The actual printing of debug messages within the code is achieved by
 * mirage_contextual_debug_messagev() or mirage_contextual_debug_message(),
 * or by convenience macro MIRAGE_DEBUG().
 */

/**
 * MirageDebugMasks:
 * @MIRAGE_DEBUG_ERROR: error message
 * @MIRAGE_DEBUG_WARNING: warning message
 * @MIRAGE_DEBUG_PARSER: message belonging to image parser and file stream parser
 * @MIRAGE_DEBUG_DISC: message belonging to disc
 * @MIRAGE_DEBUG_SESSION: message belonging to session
 * @MIRAGE_DEBUG_TRACK: message belonging to track
 * @MIRAGE_DEBUG_SECTOR: message belonging to sector
 * @MIRAGE_DEBUG_FRAGMENT: message belonging to fragment
 * @MIRAGE_DEBUG_CDTEXT: message belonging to CD-TEXT encoder/decoder
 * @MIRAGE_DEBUG_FILE_IO: messages belonging to file filter I/O operations
 * @MIRAGE_DEBUG_IMAGE_ID: messages belonging to image identification part of image parsers
 *
 * Debug message types and debug masks used to control verbosity of various
 * parts of libMirage.
 *
 * All masks except %MIRAGE_DEBUG_ERROR and %MIRAGE_DEBUG_WARNING can be combined
 * together to control verbosity of libMirage.
 */
typedef enum _MirageDebugMasks
{
    /* Debug types */
    MIRAGE_DEBUG_ERROR    = -1,
    MIRAGE_DEBUG_WARNING  = -2,
    /* Debug masks */
    MIRAGE_DEBUG_PARSER  = 0x1,
    MIRAGE_DEBUG_DISC = 0x2,
    MIRAGE_DEBUG_SESSION = 0x4,
    MIRAGE_DEBUG_TRACK = 0x8,
    MIRAGE_DEBUG_SECTOR = 0x10,
    MIRAGE_DEBUG_FRAGMENT = 0x20,
    MIRAGE_DEBUG_CDTEXT = 0x40,
    MIRAGE_DEBUG_FILE_IO = 0x80,
    MIRAGE_DEBUG_IMAGE_ID = 0x100,
} MirageDebugMasks;


/**
 * MIRAGE_DEBUG:
 * @obj: (in): object
 * @lvl: (in): debug level
 * @...: (in): debug message
 *
 * Debugging macro, provided for convenience. It performs cast to
 * #MirageContextual interface on @obj and calls mirage_contextual_debug_message()
 * with debug level @lvl and debug message, specified by format string and
 * format arguments.
 */
#define MIRAGE_DEBUG(obj, lvl, ...) mirage_contextual_debug_message(MIRAGE_CONTEXTUAL(obj), lvl, __VA_ARGS__)


#endif /* __MIRAGE_DEBUG_H__ */
