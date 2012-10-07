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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __MIRAGE_DEBUG_H__
#define __MIRAGE_DEBUG_H__


/**
 * MirageDebugMasks:
 * @MIRAGE_DEBUG_ERROR: error message
 * @MIRAGE_DEBUG_WARNING: warning message
 * @MIRAGE_DEBUG_PARSER: message belonging to disc parser and file stream parser
 * @MIRAGE_DEBUG_DISC: message belonging to disc
 * @MIRAGE_DEBUG_SESSION: message belonging to session
 * @MIRAGE_DEBUG_TRACK: message belonging to track
 * @MIRAGE_DEBUG_SECTOR: message belonging to sector
 * @MIRAGE_DEBUG_FRAGMENT: message belonging to fragment
 * @MIRAGE_DEBUG_CDTEXT: message belonging to CD-TEXT encoder/decoder
 * @MIRAGE_DEBUG_FILE_IO: messages belonging to file filter I/O operations
 *
 * <para>
 * Debug message types and debug masks used to control verbosity of various
 * parts of libMirage.
 * </para>
 *
 * <para>
 * All masks except %MIRAGE_DEBUG_ERROR and %MIRAGE_DEBUG_WARNING can be combined
 * together to control verbosity of libMirage.
 * </para>
 **/
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
} MirageDebugMasks;


/**
 * MIRAGE_DEBUG:
 * @obj: (in): object
 * @lvl: (in): debug level
 * @msg...: (in): debug message
 *
 * <para>
 * Debugging macro, provided for convenience. It performs cast to #MirageDebuggable
 * interface on @obj, acquires its #MirageDebugContext and calls mirage_debug_context_message()
 * with debug level @lvl and debug message @msg....
 * </para>
 **/
#define MIRAGE_DEBUG(obj, lvl, ...) { \
    mirage_debug_context_message(mirage_debuggable_get_debug_context(MIRAGE_DEBUGGABLE(obj)), lvl, __VA_ARGS__); \
}


G_BEGIN_DECLS


/**********************************************************************\
 *                      MirageDebugContext object                     *
\**********************************************************************/
#define MIRAGE_TYPE_DEBUG_CONTEXT            (mirage_debug_context_get_type())
#define MIRAGE_DEBUG_CONTEXT(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_DEBUG_CONTEXT, MirageDebugContext))
#define MIRAGE_DEBUG_CONTEXT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_DEBUG_CONTEXT, MirageDebugContextClass))
#define MIRAGE_IS_DEBUG_CONTEXT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_DEBUG_CONTEXT))
#define MIRAGE_IS_DEBUG_CONTEXT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_DEBUG_CONTEXT))
#define MIRAGE_DEBUG_CONTEXT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_DEBUG_CONTEXT, MirageDebugContextClass))

typedef struct _MirageDebugContext         MirageDebugContext;
typedef struct _MirageDebugContextClass    MirageDebugContextClass;
typedef struct _MirageDebugContextPrivate  MirageDebugContextPrivate;

/**
 * MirageDebugContext:
 *
 * <para>
 * Contains private data only, and should be accessed using the functions below.
 * </para>
 **/
struct _MirageDebugContext
{
    GObject parent_instance;

    /*< private >*/
    MirageDebugContextPrivate *priv;
};

struct _MirageDebugContextClass
{
    GObjectClass parent_class;
};

/* Used by MIRAGE_TYPE_DEBUG_CONTEXT */
GType mirage_debug_context_get_type (void);

void mirage_debug_context_set_debug_mask (MirageDebugContext *self, gint debug_mask);
gint mirage_debug_context_get_debug_mask (MirageDebugContext *self);

void mirage_debug_context_set_domain (MirageDebugContext *self, const gchar *domain);
const gchar *mirage_debug_context_get_domain (MirageDebugContext *self);

void mirage_debug_context_set_name (MirageDebugContext *self, const gchar *name);
const gchar *mirage_debug_context_get_name (MirageDebugContext *self);

void mirage_debug_context_message (MirageDebugContext *self, gint level, gchar *format, ...);
void mirage_debug_context_messagev (MirageDebugContext *self, gint level, gchar *format, va_list args);


/**********************************************************************\
 *                     MirageDebuggable interface                     *
\**********************************************************************/
#define MIRAGE_TYPE_DEBUGGABLE                 (mirage_debuggable_get_type())
#define MIRAGE_DEBUGGABLE(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_DEBUGGABLE, MirageDebuggable))
#define MIRAGE_IS_DEBUGGABLE(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_DEBUGGABLE))
#define MIRAGE_DEBUGGABLE_GET_INTERFACE(inst)  (G_TYPE_INSTANCE_GET_INTERFACE((inst), MIRAGE_TYPE_DEBUGGABLE, MirageDebuggableInterface))

/**
 * MirageDebuggable:
 *
 * <para>
 * Dummy interface structure.
 * </para>
 **/
typedef struct _MirageDebuggable             MirageDebuggable;
typedef struct _MirageDebuggableInterface    MirageDebuggableInterface;

struct _MirageDebuggableInterface
{
    GTypeInterface parent_iface;

    /* Interface methods */
    void (*set_debug_context) (MirageDebuggable *self, MirageDebugContext *debug_context);
    MirageDebugContext *(*get_debug_context) (MirageDebuggable *self);
};

/* Used by MIRAGE_TYPE_DEBUGGABLE */
GType mirage_debuggable_get_type (void);

void mirage_debuggable_set_debug_context (MirageDebuggable *self, MirageDebugContext *debug_context);
MirageDebugContext *mirage_debuggable_get_debug_context (MirageDebuggable *self);


G_END_DECLS

#endif /* __MIRAGE_DEBUG_H__ */
