/*
 *  libMirage: contextual interface
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

/**
 * SECTION: mirage-contextual
 * @title: MirageContextual
 * @short_description: Interface for attaching context to objects.
 * @see_also: #MirageContext, #MirageObject
 * @include: mirage-contextual.h
 *
 * #MirageContextual provides an interface that allows attachment of a
 * #MirageContext to the object implementing it. The object must implement
 * two functions for getting and setting the context - mirage_contextual_get_context()
 * and mirage_contextual_set_context().
 *
 * In addition, #MirageContextual provides some shared code that can be
 * used by implementations. Most notable are debugging facilities, in
 * form of functions mirage_contextual_debug_message() and
 * mirage_contextual_debug_messagev(), which print debug messages depending
 * on the settings of the attached context.
 *
 * Furthermore, for convenience of image parser and filter stream implementations,
 * passthrough is provided for some functions of #MirageContext. Using these
 * functions is equivalent to getting and verifying the attached context,
 * calling its function directly, and releasing the reference.
 */

#include "mirage/config.h"
#include "mirage/mirage.h"

#include <glib/gi18n-lib.h>


/**********************************************************************\
 *                   Context setting and retrieval                    *
\**********************************************************************/
/**
 * mirage_contextual_set_context:
 * @self: a #MirageContextual
 * @context: (in) (transfer full): debug context (a #MirageContext)
 *
 * Sets/attaches a context.
 */
void mirage_contextual_set_context (MirageContextual *self, MirageContext *context)
{
    return MIRAGE_CONTEXTUAL_GET_INTERFACE(self)->set_context(self, context);
}

/**
 * mirage_contextual_get_context:
 * @self: a #MirageContextual
 *
 * Retrieves the attached context.
 *
 * Returns: (transfer full): attached context (a #MirageContext), or %NULL.
 * The reference to context is incremented, and should be released using g_object_unref()
 * when no longer needed.
 */
MirageContext *mirage_contextual_get_context (MirageContextual *self)
{
    return MIRAGE_CONTEXTUAL_GET_INTERFACE(self)->get_context(self);
}


/**********************************************************************\
 *                          Debug messages                            *
\**********************************************************************/
/**
 * mirage_contextual_debug_messagev:
 * @self: a #MirageContextual
 * @level: (in): debug level
 * @format: (in): message format. See the printf() documentation.
 * @args: (in): parameters to insert into the format string.
 *
 * Outputs debug message with verbosity level @level, format string @format and
 * format arguments @args. The message is displayed if debug context has mask
 * that covers @level, or if @level is either %MIRAGE_DEBUG_WARNING or
 * %MIRAGE_DEBUG_ERROR.
 */
void mirage_contextual_debug_messagev (MirageContextual *self, gint level, gchar *format, va_list args)
{
    const gchar *name = NULL;
    const gchar *domain = NULL;
    gint debug_mask = 0;

    MirageContext *context;

    /* Try getting debug context */
    context = mirage_contextual_get_context(self);
    if (context) {
        name = mirage_context_get_debug_name(context);
        domain = mirage_context_get_debug_domain(context);
        debug_mask = mirage_context_get_debug_mask(context);
        g_object_unref(context);
    }

    /* Error, warning or debug; in any case, if name is set, print it
       before the actual message */
    if (level == MIRAGE_DEBUG_ERROR) {
        if (name) {
            g_log(domain, G_LOG_LEVEL_ERROR, "%s: ", name);
        }
        g_logv(domain, G_LOG_LEVEL_ERROR, format, args);
    } else if (level == MIRAGE_DEBUG_WARNING) {
        if (name) {
            g_log(domain, G_LOG_LEVEL_WARNING, "%s: ", name);
        }
        g_logv(domain, G_LOG_LEVEL_WARNING, format, args);
    } else if (debug_mask & level) {
        if (name) {
            g_log(domain, G_LOG_LEVEL_DEBUG, "%s: ", name);
        }
        g_logv(domain, G_LOG_LEVEL_DEBUG, format, args);
    }
}

/**
 * mirage_contextual_debug_message:
 * @self: a #MirageContextual
 * @level: (in): debug level
 * @format: (in): message format. See the printf() documentation.
 * @...: (in): parameters to insert into the format string.
 *
 * Outputs debug message with verbosity level @level, format string @format and
 * format arguments @Varargs. The message is displayed if debug context has mask
 * that covers @level, or if @level is either %MIRAGE_DEBUG_WARNING or
 * %MIRAGE_DEBUG_ERROR.
 */
void mirage_contextual_debug_message (MirageContextual *self, gint level, gchar *format, ...)
{
    va_list args;
    va_start(args, format);
    mirage_contextual_debug_messagev(self, level, format, args);
    va_end(args);
}


/**
 * mirage_contextual_debug_is_active:
 * @self: a #MirageContextual
 * @level: (in): debug level
 *
 * Checks whether debug messages at debug level @level are currently
 * active or not.
 *
 * Returns: a boolean indicating whether debug messages at debug level
 * @level are currently active or not.
 */
gboolean mirage_contextual_debug_is_active (MirageContextual *self, gint level)
{
    gint debug_mask = 0;

    MirageContext *context;

    /* Always on for error and warning levels */
    if (level == MIRAGE_DEBUG_ERROR || level == MIRAGE_DEBUG_WARNING) {
        return TRUE;
    }

    /* Try getting debug context */
    context = mirage_contextual_get_context(self);
    if (context) {
        debug_mask = mirage_context_get_debug_mask(context);
        g_object_unref(context);
    }

    return debug_mask & level;
}


/**
 * mirage_contextual_debug_print_buffer:
 * @self: a #MirageContextual
 * @level: (in): debug level
 * @prefix: (in) (allow-none): prefix, or none
 * @width: (in): output width
 * @buffer: (in) (array length=buffer_length): buffer to print
 * @buffer_length: (in): buffer length
 *
 * Prints contents of @buffer as a sequence of @buffer_length two-digit
 * hexadecimal numbers, arranged in lines of @width numbers. Each line
 * is optionally prefixed by @prefix. If specified debug @level is not
 * active (masked by context), this function does nothing.
 */
void mirage_contextual_debug_print_buffer (MirageContextual *self, gint level, const gchar *prefix, gint width, const guint8 *buffer, gint buffer_length)
{
    /* No-op if specified debug level is not active */
    if (!MIRAGE_DEBUG_ON(self, level)) {
        return;
    }

    const gint num_lines = (buffer_length + width - 1) / width; /* Number of lines */
    const gint num_chars = width*3 + 1; /* Max. number of characters per line */
    gchar *line_str = g_malloc(num_chars); /* Three characters per element, plus terminating NULL */

    const guint8 *buffer_ptr = buffer;

    for (gint l = 0; l < num_lines; l++) {
        gchar *ptr = line_str;
        gint num = MIN(width, buffer_length);

        memset(ptr, 0, num_chars);

        for (gint i = 0; i < num; i++) {
            ptr += g_sprintf(ptr, "%02hhX ", *buffer_ptr);
            buffer_ptr++;
            buffer_length--;
        }

        if (prefix) {
            MIRAGE_DEBUG(self, level, "%s: %s\n", prefix, line_str);
        } else {
            MIRAGE_DEBUG(self, level, "%s\n", line_str);
        }
    }

    g_free(line_str);
}



/**********************************************************************\
 *        Convenience functions, passthrough to MirageContext         *
\**********************************************************************/
/**
 * mirage_contextual_get_option:
 * @self: a #MirageContextual
 * @name: (in): option name
 *
 * Retrieves option named @name from the context.
 *
 * <note>
 * This is a convenience function that retrieves a #MirageContext from
 * @self and calls mirage_context_get_option().
 * </note>
 *
 * Returns: (transfer full): a #GVariant containing the option value on
 * success, %NULL on failure.
 */
GVariant *mirage_contextual_get_option (MirageContextual *self, const gchar *name)
{
    MirageContext *context = mirage_contextual_get_context(self);
    GVariant *value = NULL;

    if (context) {
        value = mirage_context_get_option(context, name);
        g_object_unref(context);
    }

    return value;
}


/**
 * mirage_contextual_obtain_password:
 * @self: a #MirageContextual
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Obtains password string, using the #MiragePasswordFunction callback
 * that was provided via mirage_context_set_password_function().
 *
 * <note>
 * This is a convenience function that retrieves a #MirageContext from
 * @self and calls mirage_context_obtain_password().
 * </note>
 *
 * Returns: password string on success, %NULL on failure. The string should be
 * freed with g_free() when no longer needed.
 */
gchar *mirage_contextual_obtain_password (MirageContextual *self, GError **error)
{
    MirageContext *context = mirage_contextual_get_context(self);
    gchar *password = NULL;

    if (!context) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, Q_("Context not set!"));
    } else {
        password = mirage_context_obtain_password(context, error);
        g_object_unref(context);
    }

    return password;
}


/**
 * mirage_contextual_create_input_stream:
 * @self: a #MirageContextual
 * @filename: (in): filename to create input stream on
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Opens a file pointed to by @filename and creates a chain of filter
 * streams on top of it.
 *
 * <note>
 * This is a convenience function that retrieves a #MirageContext from
 * @self and calls mirage_context_create_input_stream().
 * </note>
 *
 * Returns: (transfer full): on success, an object implementing #MirageStream
 * interface is returned, which can be used to access data stored in file.
 * On failure, %NULL is returned. The reference to the object should be
 * released using g_object_unref() when no longer needed.
 */
MirageStream *mirage_contextual_create_input_stream (MirageContextual *self, const gchar *filename, GError **error)
{
    MirageContext *context = mirage_contextual_get_context(self);
    MirageStream *stream = NULL;

    if (!context) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, Q_("Context not set!"));
    } else {
        stream = mirage_context_create_input_stream(context, filename, error);
        g_object_unref(context);
    }

    return stream;
}


/**
 * mirage_contextual_create_output_stream:
 * @self: a #MirageContextual
 * @filename: (in): filename to create output stream on
 * @filter_chain: (in) (allow-none) (array zero-terminated=1): NULL-terminated array of strings describing types of filters to include in the filter chain, or %NULL
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Opens a file pointed to by @filename and creates a chain of filter
 * streams on top of it.
 *
 * <note>
 * This is a convenience function that retrieves a #MirageContext from
 * @self and calls mirage_context_create_output_stream().
 * </note>
 *
 * Returns: (transfer full): on success, an object implementing #MirageStream
 * interface is returned, which can be used to write data to file.
 * On failure, %NULL is returned. The reference to the object should be
 * released using g_object_unref() when no longer needed.
 */
MirageStream *mirage_contextual_create_output_stream (MirageContextual *self, const gchar *filename, const gchar **filter_chain, GError **error)
{
    MirageContext *context = mirage_contextual_get_context(self);
    MirageStream *stream = NULL;

    if (!context) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, Q_("Context not set!"));
    } else {
        stream = mirage_context_create_output_stream(context, filename, filter_chain, error);
        g_object_unref(context);
    }

    return stream;
}


GType mirage_contextual_get_type (void) {
    static GType iface_type = 0;
    if (iface_type == 0) {
        static const GTypeInfo info = {
            sizeof(MirageContextualInterface),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            NULL,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            0,
            0,      /* n_preallocs */
            NULL,   /* instance_init */
            NULL    /* value_table */
        };

        iface_type = g_type_register_static(G_TYPE_INTERFACE, "MirageContextual", &info, 0);
    }

    return iface_type;
}
