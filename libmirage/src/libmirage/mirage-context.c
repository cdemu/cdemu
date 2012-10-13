/*
 *  libMirage: Context object and Contextual interface
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
 *                          Contextual interface                      *
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


/**
 * mirage_contextual_messagev:
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
 * <para>
 * This is a convenience function that retrieves a #MirageContext from
 * @self and uses it to create fragment by calling mirage_context_create_fragment().
 * </para>
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


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_CONTEXT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_CONTEXT, MirageContextPrivate))

struct _MirageContextPrivate
{
    gchar *name; /* Debug context name... e.g. 'Device 1' */
    gchar *domain; /* Debug context domain... e.g. 'libMirage' */

    gint debug_mask; /* Debug mask */
};


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
/**
 * mirage_context_set_debug_domain:
 * @self: a #MirageContext
 * @domain: (in): domain name
 *
 * <para>
 * Sets debug context's domain name to @domain.
 * </para>
 **/
void mirage_context_set_debug_domain (MirageContext *self, const gchar *domain)
{
    /* Set domain */
    g_free(self->priv->domain);
    self->priv->domain = g_strdup(domain);
}

/**
 * mirage_context_get_debug_domain:
 * @self: a #MirageContext
 *
 * <para>
 * Retrieves debug context's domain name.
 * </para>
 *
 * Returns: (transfer none): pointer to buffer containing the domain name, or %NULL. The buffer belongs to the object and should not be modified.
 **/
const gchar *mirage_context_get_debug_domain (MirageContext *self)
{
    return self->priv->domain;
}


/**
 * mirage_context_set_debug_name:
 * @self: a #MirageContext
 * @name: (in): name
 *
 * <para>
 * Sets debug context's name to @name.
 * </para>
 **/
void mirage_context_set_debug_name (MirageContext *self, const gchar *name)
{
    /* Set name */
    g_free(self->priv->name);
    self->priv->name = g_strdup(name);
}

/**
 * mirage_context_get_debug_name:
 * @self: a #MirageContext
 *
 * <para>
 * Retrieves debug context's name.
 * </para>
 *
 * Returns: (transfer none): pointer to buffer containing the name, or %NULL. The buffer belongs to the object and should not be modified.
 **/
const gchar *mirage_context_get_debug_name (MirageContext *self)
{
    return self->priv->name;
}


/**
 * mirage_context_set_debug_mask:
 * @self: a #MirageContext
 * @debug_mask: (in): debug mask
 *
 * <para>
 * Sets debug context's debug mask.
 * </para>
 **/
void mirage_context_set_debug_mask (MirageContext *self, gint debug_mask)
{
    /* Set debug mask */
    self->priv->debug_mask = debug_mask;
}

/**
 * mirage_context_get_debug_mask:
 * @self: a #MirageContext
 *
 * <para>
 * Retrieves debug context's debug mask.
 * </para>
 *
 * Returns: debug context's debug mask
 **/
gint mirage_context_get_debug_mask (MirageContext *self)
{
    /* Return debug mask */
    return self->priv->debug_mask;
}



/**
 * mirage_context_load_image:
 * @self: a #MirageContext
 * @filenames: (in) (array zero-terminated=1): filename(s)
 * @params: (in) (allow-none) (element-type gchar* GValue): parser parameters, or %NULL
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Creates a #MirageDisc object representing image stored in @filenames. @filenames
 * is a NULL-terminated list of filenames containing image data. The function tries
 * to find a parser that can handle give filename(s) and uses it to load the data
 * into disc object.
 * </para>
 *
 * <para>
 * @params, if not %NULL, is a #GHashTable containing parser parameters (such as
 * password, encoding, etc.) - it must have strings for its keys and values of
 * #GValue type. The hash table is passed to the parser; whether parameters are
 * actually used (or supported) by the parser, however, depends on the parser
 * implementation. If parser does not support a parameter, it will be ignored.
 * </para>
 *
 * <para>
 * If multiple filenames are provided and parser supports only single-file images,
 * only the first filename is used.
 * </para>
 *
 * Returns: (transfer full): a #MirageDisc object on success, %NULL on failure. The reference to
 * the object should be released using g_object_unref() when no longer needed.
 **/
MirageDisc *mirage_context_load_image (MirageContext *self, gchar **filenames, GHashTable *params, GError **error)
{
    MirageDisc *disc = NULL;
    GInputStream **streams;

    gint num_parsers;
    GType *parser_types;

    /* Get the list of supported parsers */
    if (!mirage_get_parsers_type(&parser_types, &num_parsers, error)) {
        return NULL;
    }

    /* Create streams */
    streams = g_new0(GInputStream *, g_strv_length(filenames)+1);
    for (gint i = 0; filenames[i]; i++) {
        streams[i] = mirage_create_file_stream(filenames[i], self, error);
        if (!streams[i]) {
            goto end;
        }
    }

    /* Go over all parsers */
    for (gint i = 0; i < num_parsers; i++) {
        GError *local_error = NULL;
        MirageParser *parser;

        /* Create parser object */
        parser = g_object_new(parser_types[i], NULL);

        /* Attach context to parser */
        mirage_contextual_set_context(MIRAGE_CONTEXTUAL(parser), self);

        /* Pass the parameters to parser */
        mirage_parser_set_params(parser, params);

        /* Try loading image */
        disc = mirage_parser_load_image(parser, streams, &local_error);

        /* Free parser */
        g_object_unref(parser);

        /* If loading succeeded, break the loop */
        if (disc) {
            goto end;
        } else {
            /* MIRAGE_ERROR_CANNOT_HANDLE is the only acceptable error here; anything
               other indicates that parser attempted to handle image and failed */
            if (local_error->code == MIRAGE_ERROR_CANNOT_HANDLE) {
                g_error_free(local_error);
            } else {
                g_propagate_error(error, local_error);
                goto end;
            }
        }
    }

    /* No parser found */
    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, "No parser can handle the image file!");

end:
    /* Close streams */
    for (gint i = 0; streams[i]; i++) {
        g_object_unref(streams[i]);
    }
    g_free(streams);

    return disc;
}


/**
 * mirage_context_create_fragment:
 * @self: a #MirageContext
 * @fragment_interface: (in): interface that fragment should implement
 * @stream: (in): the data stream that fragment should be able to handle
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Creates a #MirageFragment implementation that implements interface specified
 * by @fragment_interface and can handle data stored in @stream.
 * </para>
 *
 * Returns: (transfer full): a #MirageFragment object on success, %NULL on failure. The reference
 * to the object should be released using g_object_unref() when no longer needed.
 **/
MirageFragment *mirage_context_create_fragment (MirageContext *self, GType fragment_interface, GInputStream *stream, GError **error)
{
    gboolean succeeded = TRUE;
    MirageFragment *fragment = NULL;

    gint num_fragments;
    GType *fragment_types;

    /* Get the list of supported fragments */
    if (!mirage_get_fragments_type(&fragment_types, &num_fragments, error)) {
        return NULL;
    }

    /* Go over all fragments */
    for (gint i = 0; i < num_fragments; i++) {
        /* Create fragment; check if it implements requested interface, then
           try to load data... if we fail, we try next one */
        fragment = g_object_new(fragment_types[i], NULL);

        /* Check if requested interface is supported */
        succeeded = G_TYPE_CHECK_INSTANCE_TYPE((fragment), fragment_interface);
        if (succeeded) {
            /* Set context */
            mirage_contextual_set_context(MIRAGE_CONTEXTUAL(fragment), self);

            /* Check if fragment can handle file format */
            succeeded = mirage_fragment_can_handle_data_format(fragment, stream, NULL);
            if (succeeded) {
                return fragment;
            }
        }

        g_object_unref(fragment);
        fragment = NULL;
    }

    /* No fragment found */
    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "No fragment can handle the given data file!");
    return fragment;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE(MirageContext, mirage_context, G_TYPE_OBJECT);


static void mirage_context_init (MirageContext *self)
{
    self->priv = MIRAGE_CONTEXT_GET_PRIVATE(self);

    self->priv->domain = NULL;
    self->priv->name = NULL;
}

static void mirage_context_finalize (GObject *gobject)
{
    MirageContext *self = MIRAGE_CONTEXT(gobject);

    g_free(self->priv->domain);
    g_free(self->priv->name);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_context_parent_class)->finalize(gobject);
}

static void mirage_context_class_init (MirageContextClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = mirage_context_finalize;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageContextPrivate));
}
