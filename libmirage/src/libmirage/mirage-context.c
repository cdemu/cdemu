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


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_CONTEXT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_CONTEXT, MirageContextPrivate))

struct _MirageContextPrivate
{
    gchar *name; /* Debug context name... e.g. 'Device 1' */
    gchar *domain; /* Debug context domain... e.g. 'libMirage' */

    gint debug_mask; /* Debug mask */

    /* GFileInputStream -> filename mapping */
    GHashTable *file_streams_map;
};


/**********************************************************************\
 *          GFileInputStream -> filename mapping management           *
\**********************************************************************/
static inline void mirage_context_stream_destroyed_handler (MirageContext *self, gpointer stream)
{
    g_hash_table_remove(self->priv->file_streams_map, stream);
}

static inline void mirage_context_file_streams_map_add (MirageContext *self, gpointer stream, const gchar *filename)
{
    /* Insert into the table */
    g_hash_table_insert(self->priv->file_streams_map, stream, g_strdup(filename));

    /* Enable cleanup when stream is destroyed */
    g_object_weak_ref(stream, (GWeakNotify)mirage_context_stream_destroyed_handler, self);
}


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
        streams[i] = mirage_context_create_file_stream(self, filenames[i], error);
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


/**
 * mirage_context_create_file_stream:
 * @self: a #MirageContext
 * @filename: (in): filename to create stream on
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Opens a file pointed to by @filename and creates a chain of file filters
 * on top of it.
 * </para>
 *
 * Returns: (transfer full): on success, an object inheriting #GFilterInputStream (and therefore
 * #GInputStream) and implementing #GSeekable interface is returned, which
 * can be used to access data stored in file. On failure, %NULL is returned.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 **/
GInputStream *mirage_context_create_file_stream (MirageContext *self, const gchar *filename, GError **error)
{
    GInputStream *stream;
    GFile *file;
    GFileType file_type;
    GError *local_error = NULL;

    gint num_file_filters;
    GType *file_filter_types;

    /* Get the list of supported file filters */
    if (!mirage_get_file_filters_type(&file_filter_types, &num_file_filters, error)) {
        return NULL;
    }

    /* Open file; at the bottom of the chain, there's always a GFileStream */
    file = g_file_new_for_path(filename);

    /* Make sure file is either a valid regular file or a symlink/shortcut */
    file_type = g_file_query_file_type(file, G_FILE_QUERY_INFO_NONE, NULL);
    if (!(file_type == G_FILE_TYPE_REGULAR || file_type == G_FILE_TYPE_SYMBOLIC_LINK || file_type == G_FILE_TYPE_SHORTCUT)) {
        g_object_unref(file);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DATA_FILE_ERROR, "Invalid data file provided for stream!");
        return NULL;
    }

    /* Create stream */
    stream = G_INPUT_STREAM(g_file_read(file, NULL, &local_error));

    g_object_unref(file);

    if (!stream) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DATA_FILE_ERROR, "Failed to open file stream on data file: %s!", local_error->message);
        g_error_free(local_error);
        return NULL;
    }

    /* Store the GFileInputStream -> filename mapping */
    mirage_context_file_streams_map_add(self, stream, filename);

    /* Construct a chain of filters */
    MirageFileFilter *filter;
    gboolean found_new;

    do {
        found_new = FALSE;

        for (gint i = 0; i < num_file_filters; i++) {
            /* Create filter object and check if it can handle data */
            filter = g_object_new(file_filter_types[i], "base-stream", stream, "close-base-stream", FALSE, NULL);

            mirage_contextual_set_context(MIRAGE_CONTEXTUAL(filter), self);

            if (!mirage_file_filter_can_handle_data_format(filter, NULL)) {
                /* Cannot handle data format... */
                g_object_unref(filter);
            } else {
                /* Release reference to (now) underlying stream */
                g_object_unref(stream);

                /* Now the underlying stream should be closed when we close filter's stream */
                g_filter_input_stream_set_close_base_stream(G_FILTER_INPUT_STREAM(filter), TRUE);

                /* Filter becomes new underlying stream */
                stream = G_INPUT_STREAM(filter);

                /* Repeat the whole cycle */
                found_new = TRUE;
                break;
            }
        }
    } while (found_new);

    /* Make sure that the stream we're returning is rewound to the beginning */
    g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);

    return stream;
}


/**
 * mirage_context_get_file_stream_filename:
 * @self: a #MirageContext
 * @stream: (in): a #GInputStream
 *
 * <para>
 * Traverses the chain of file filters and retrieves the filename on which
 * the #GFileInputStream, located at the bottom of the chain, was opened.
 * </para>
 *
 * Returns: (transfer none): on success, a pointer to filename on which
 * the underyling file stream was opened. On failure, %NULL is returned.
 **/
const gchar *mirage_context_get_file_stream_filename (MirageContext *self, GInputStream *stream)
{
    if (G_IS_FILTER_INPUT_STREAM(stream)) {
        /* Recursively traverse the filter stream chain */
        GInputStream *base_stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(stream));
        if (!base_stream) {
            return NULL;
        } else {
            return mirage_context_get_file_stream_filename(self, base_stream);
        }
    } else if (G_IS_FILE_INPUT_STREAM(stream)) {
        /* We are at the bottom; get filename from our mapping table */
        return g_hash_table_lookup(self->priv->file_streams_map, stream);
    }

    /* Invalid stream type */
    return NULL;
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

    self->priv->file_streams_map = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
}

static void mirage_context_finalize (GObject *gobject)
{
    MirageContext *self = MIRAGE_CONTEXT(gobject);

    g_free(self->priv->domain);
    g_free(self->priv->name);

    /* Free GFileInputStream -> filename map */
    g_hash_table_unref(self->priv->file_streams_map);

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
