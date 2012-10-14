/*
 *  libMirage: Contextual interface
 *  Copyright (C) 2012 Rok Mandeljc
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"


/**********************************************************************\
 *                   Context setting and retrieval                    *
\**********************************************************************/
/**
 * mirage_contextual_set_context:
 * @self: a #MirageContextual
 * @context: (in) (transfer full): debug context (a #MirageContext)
 *
 * <para>
 * Sets object's debug context.
 * </para>
 **/
void mirage_contextual_set_context (MirageContextual *self, MirageContext *context)
{
    return MIRAGE_CONTEXTUAL_GET_INTERFACE(self)->set_context(self, context);
}

/**
 * mirage_contextual_get_context:
 * @self: a #MirageContextual
 *
 * <para>
 * Retrieves object's debug context.
 * </para>
 *
 * Returns: (transfer full): object's debug context (a #MirageContext), or %NULL.
 * The reference to debug context is incremented, and should be released using g_object_unref()
 * when no longer needed.
 **/
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
 * <para>
 * Outputs debug message with verbosity level @level, format string @format and
 * format arguments @args. The message is displayed if debug context has mask
 * that covers @level, or if @level is either %MIRAGE_DEBUG_WARNING or
 * %MIRAGE_DEBUG_ERROR.
 * </para>
 **/
void mirage_contextual_debug_messagev (MirageContextual *self, gint level, gchar *format, va_list args)
{
    const gchar *name = NULL;
    const gchar *domain = NULL;
    gint debug_mask = 0;

    gchar *new_format;

    MirageContext *context;

    /* Try getting debug context */
    context = mirage_contextual_get_context(self);
    if (context) {
        name = mirage_context_get_debug_name(context);
        domain = mirage_context_get_debug_domain(context);
        debug_mask = mirage_context_get_debug_mask(context);
        g_object_unref(context);
    }

    /* If we have a name, prepend it */
    if (name) {
        new_format = g_strdup_printf("%s: %s", name, format);
    } else {
        new_format = g_strdup(format);
    }

    if (level == MIRAGE_DEBUG_ERROR) {
        g_logv(domain, G_LOG_LEVEL_ERROR, format, args);
    } else if (level == MIRAGE_DEBUG_WARNING) {
        g_logv(domain, G_LOG_LEVEL_WARNING, format, args);
    } else if (debug_mask & level) {
        g_logv(domain, G_LOG_LEVEL_DEBUG, format, args);
    }

    g_free(new_format);
}

/**
 * mirage_contextual_debug_message:
 * @self: a #MirageContextual
 * @level: (in): debug level
 * @format: (in): message format. See the printf() documentation.
 * @...: (in): parameters to insert into the format string.
 *
 * <para>
 * Outputs debug message with verbosity level @level, format string @format and
 * format arguments @Varargs. The message is displayed if debug context has mask
 * that covers @level, or if @level is either %MIRAGE_DEBUG_WARNING or
 * %MIRAGE_DEBUG_ERROR.
 * </para>
 **/
void mirage_contextual_debug_message (MirageContextual *self, gint level, gchar *format, ...)
{
    va_list args;
    va_start(args, format);
    mirage_contextual_debug_messagev(self, level, format, args);
    va_end(args);
}


/**********************************************************************\
 *        Convenience functions, passthrough to MirageContext         *
\**********************************************************************/
/**
 * mirage_contextual_get_option:
 * @self: a #MirageContextual
 * @name: (in): option name
 *
 * <para>
 * Retrieves option named @name from the context.
 * </para>
 *
 * <note>
 * This is a convenience function that retrieves a #MirageContext from
 * @self and calls mirage_context_get_option().
 * </note>
 *
 * Returns: (transfer full): a #GVariant containing the option value on
 * success, %NULL on failure.
 **/
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
 * <para>
 * Obtains password string, using the #MiragePasswordFunction callback
 * that was provided via mirage_context_set_password_function().
 * </para>
 *
 * <note>
 * This is a convenience function that retrieves a #MirageContext from
 * @self and calls mirage_context_obtain_password().
 * </note>
 *
 * Returns: password string on success, %NULL on failure. The string should be
 * freed with g_free() when no longer needed.
 **/
gchar *mirage_contextual_obtain_password (MirageContextual *self, GError **error)
{
    MirageContext *context = mirage_contextual_get_context(self);
    gchar *password = NULL;

    if (!context) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Context not set!");
    } else {
        password = mirage_context_obtain_password(context, error);
        g_object_unref(context);
    }

    return password;
}


/**
 * mirage_contextual_create_fragment:
 * @self: a #MirageContextual
 * @fragment_interface: (in): interface that fragment should implement
 * @stream: (in): the data stream that fragment should be able to handle
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Creates a #MirageFragment implementation that implements interface specified
 * by @fragment_interface and can handle data stored in @stream.
 * </para>
 *
 * <note>
 * This is a convenience function that retrieves a #MirageContext from
 * @self and calls mirage_context_create_fragment().
 * </note>
 *
 * Returns: (transfer full): a #MirageFragment object on success, %NULL on failure. The reference
 * to the object should be released using g_object_unref() when no longer needed.
 **/
MirageFragment *mirage_contextual_create_fragment (MirageContextual *self, GType fragment_interface, GInputStream *stream, GError **error)
{
    MirageContext *context = mirage_contextual_get_context(self);
    MirageFragment *fragment = NULL;

    if (!context) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Context not set!");
    } else {
        fragment = mirage_context_create_fragment(context, fragment_interface, stream, error);
        g_object_unref(context);
    }

    return fragment;
}

/**
 * mirage_contextual_create_file_stream:
 * @self: a #MirageContextual
 * @filename: (in): filename to create stream on
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Opens a file pointed to by @filename and creates a chain of file filters
 * on top of it.
 * </para>
 *
 * <note>
 * This is a convenience function that retrieves a #MirageContext from
 * @self and calls mirage_context_create_file_stream().
 * </note>
 *
 * Returns: (transfer full): on success, an object inheriting #GFilterInputStream (and therefore
 * #GInputStream) and implementing #GSeekable interface is returned, which
 * can be used to access data stored in file. On failure, %NULL is returned.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GInputStream *mirage_contextual_create_file_stream (MirageContextual *self, const gchar *filename, GError **error)
{
    MirageContext *context = mirage_contextual_get_context(self);
    GInputStream *stream = NULL;

    if (!context) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Context not set!");
    } else {
        stream = mirage_context_create_file_stream(context, filename, error);
        g_object_unref(context);
    }

    return stream;
}


/**
 * mirage_contextual_get_file_stream_filename:
 * @self: a #MirageContextual
 * @stream: (in): a #GInputStream
 *
 * <para>
 * Traverses the chain of file filters and retrieves the filename on which
 * the #GFileInputStream, located at the bottom of the chain, was opened.
 * </para>
 *
 * <note>
 * This is a convenience function that retrieves a #MirageContext from
 * @self and calls mirage_context_get_file_stream_filename().
 * </note>
 *
 * Returns: (transfer none): on success, a pointer to filename on which
 * the underyling file stream was opened. On failure, %NULL is returned.
 **/
const gchar *mirage_contextual_get_file_stream_filename (MirageContextual *self, GInputStream *stream)
{
    MirageContext *context = mirage_contextual_get_context(self);
    const gchar *filename = NULL;

    if (context) {
        filename = mirage_context_get_file_stream_filename(context, stream);
        g_object_unref(context);
    }

    return filename;
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
