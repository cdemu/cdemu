/*
 *  libMirage: Context object
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

/**
 * SECTION: mirage-context
 * @title: MirageContext
 * @short_description: Context object.
 * @see_also: #MirageContextual, #MirageObject, #MirageFileFilter
 * @include: mirage-context.h
 *
 * #MirageContext provides a context, which is attached to libMirage's
 * objects. This way, it allows sharing and propagation of settings, such
 * as debug verbosity, parser options, password function, etc. It also
 * provides file stream caching and #GInputStream to filename resolution,
 * which is used by image parsers.
 *
 * Due to all the properties it holds, #MirageContext is designed as the
 * core object of libMirage and provides the library's main functionality,
 * the loading of image files. Therefore, loading an image usually looks
 * as follows:
 * - initialize the library using mirage_initialize()
 * - create a #MirageContext object, i.e. using g_object_new() and %MIRAGE_TYPE_CONTEXT
 * - (optionally) set debug name, domain and mask
 * - load the image using mirage_context_load_image()
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_CONTEXT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_CONTEXT, MirageContextPrivate))

struct _MirageContextPrivate
{
    /* Debugging */
    gchar *name; /* Debug context name... e.g. 'Device 1' */
    gchar *domain; /* Debug context domain... e.g. 'libMirage' */

    gint debug_mask; /* Debug mask */

    /* Options */
    GHashTable *options;

    /* Password function */
    MiragePasswordFunction password_function;
    gpointer password_data;

    /* Data stream cache */
    GHashTable *stream_cache;

    /* GFileInputStream -> filename mapping */
    GHashTable *file_streams_map;
};


/**********************************************************************\
 *                          Stream cache                              *
\**********************************************************************/
static inline void mirage_context_stream_cache_remove (MirageContext *self, gpointer stream)
{
    GHashTableIter iter;
    gpointer key, value;

    /* Iterate over table and remove the entry corresponding to stream */
    g_hash_table_iter_init(&iter, self->priv->stream_cache);

    while (g_hash_table_iter_next(&iter, &key, &value)) {
        if (value == stream) {
            g_hash_table_iter_remove(&iter);
            break;
        }
    }
}

static inline void mirage_context_stream_cache_add (MirageContext *self, const gchar *filename, gpointer stream)
{
    /* Insert into the table */
    g_hash_table_insert(self->priv->stream_cache, g_strdup(filename), stream);

    /* Cleanup when stream is destroyed */
    g_object_weak_ref(stream, (GWeakNotify)mirage_context_stream_cache_remove, self);
}


/**********************************************************************\
 *          GFileInputStream -> filename mapping management           *
\**********************************************************************/
static inline void mirage_context_file_streams_map_remove (MirageContext *self, gpointer stream)
{
    g_hash_table_remove(self->priv->file_streams_map, stream);
}

static inline void mirage_context_file_streams_map_add (MirageContext *self, gpointer stream, const gchar *filename)
{
    /* Insert into the table */
    g_hash_table_insert(self->priv->file_streams_map, stream, g_strdup(filename));

    /* Cleanup when stream is destroyed */
    g_object_weak_ref(stream, (GWeakNotify)mirage_context_file_streams_map_remove, self);
}


/**********************************************************************\
 *                       Public API: debugging                        *
\**********************************************************************/
/**
 * mirage_context_set_debug_domain:
 * @self: a #MirageContext
 * @domain: (in): domain name
 *
 * Sets debug context's domain name to @domain.
 */
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
 * Retrieves debug context's domain name.
 *
 * Returns: (transfer none): pointer to buffer containing the domain name, or %NULL. The buffer belongs to the object and should not be modified.
 */
const gchar *mirage_context_get_debug_domain (MirageContext *self)
{
    return self->priv->domain;
}


/**
 * mirage_context_set_debug_name:
 * @self: a #MirageContext
 * @name: (in): name
 *
 * Sets debug context's name to @name.
 */
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
 * Retrieves debug context's name.
 *
 * Returns: (transfer none): pointer to buffer containing the name, or %NULL. The buffer belongs to the object and should not be modified.
 */
const gchar *mirage_context_get_debug_name (MirageContext *self)
{
    return self->priv->name;
}


/**
 * mirage_context_set_debug_mask:
 * @self: a #MirageContext
 * @debug_mask: (in): debug mask
 *
 * Sets debug context's debug mask.
 */
void mirage_context_set_debug_mask (MirageContext *self, gint debug_mask)
{
    /* Set debug mask */
    self->priv->debug_mask = debug_mask;
}

/**
 * mirage_context_get_debug_mask:
 * @self: a #MirageContext
 *
 * Retrieves debug context's debug mask.
 *
 * Returns: debug context's debug mask
 */
gint mirage_context_get_debug_mask (MirageContext *self)
{
    /* Return debug mask */
    return self->priv->debug_mask;
}


/**********************************************************************\
 *                        Public API: options                         *
\**********************************************************************/
/**
 * mirage_context_clear_options:
 * @self: a #MirageContext
 *
 * Clears all the options from the context.
 */
void mirage_context_clear_options (MirageContext *self)
{
    /* Remove all entries from hash table */
    g_hash_table_remove_all(self->priv->options);
}

/**
 * mirage_context_set_option:
 * @self: a #MirageContext
 * @name: (in): option name
 * @value: (in) (transfer full): option value
 *
 * Sets an option to the context. If option with the specified name already
 * exists, it is replaced.
 */
void mirage_context_set_option (MirageContext *self, const gchar *name, GVariant *value)
{
    g_variant_ref_sink(value); /* Claim reference */
	g_hash_table_replace(self->priv->options, g_strdup(name), value); /* Use replace() instead of insert() so that old key gets released */
}

/**
 * mirage_context_get_option:
 * @self: a #MirageContext
 * @name: (in): option name
 *
 * Retrieves option named @name from the context.
 *
 * Returns: (transfer full): pointer to a #GVariant containing the option
 * value on success, %NULL on failure.
 */
GVariant *mirage_context_get_option (MirageContext *self, const gchar *name)
{
    GVariant *value = g_hash_table_lookup(self->priv->options, name);
    if (value) {
        g_variant_ref(value);
    }
    return value;
}


/**********************************************************************\
 *                       Public API: password                         *
\**********************************************************************/
/**
 * mirage_context_set_password_function:
 * @self: a #MirageContext
 * @func: (in) (allow-none) (scope call): a password function pointer
 * @user_data: (in) (closure): pointer to user data to be passed to the password function
 *
 * Sets the password function to context. The function is used by parsers
 * that support encrypted images to obtain password for unlocking such images.
 *
 * Both @func and @user_data can be %NULL; in that case the appropriate setting
 * will be reset.
 */
void mirage_context_set_password_function (MirageContext *self, MiragePasswordFunction func, gpointer user_data)
{
    /* Store the pointers */
    self->priv->password_function = func;
    self->priv->password_data = user_data;
}

/**
 * mirage_context_obtain_password:
 * @self: a #MirageContext
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Obtains password string, using the #MiragePasswordFunction callback
 * that was provided via mirage_context_set_password_function().
 *
 * Returns: password string on success, %NULL on failure. The string should be
 * freed with g_free() when no longer needed.
 */
gchar *mirage_context_obtain_password (MirageContext *self, GError **error)
{
    gchar *password;

    /* Make sure we have a password function */
    if (!self->priv->password_function) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Context does not have a password function!");
        return NULL;
    }

    /* Call the function pointer */
    password = (*self->priv->password_function)(self->priv->password_data);

    if (!password) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, "Password has not been provided!");
    }

    return password;
}


/**********************************************************************\
 *                     Public API: image loading                      *
\**********************************************************************/
/**
 * mirage_context_load_image:
 * @self: a #MirageContext
 * @filenames: (in) (array zero-terminated=1): filename(s)
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Creates a #MirageDisc object representing image stored in @filenames. @filenames
 * is a NULL-terminated list of filenames containing image data. The function tries
 * to find a parser that can handle give filename(s) and uses it to load the data
 * into disc object.
 *
 * If multiple filenames are provided and parser supports only single-file images,
 * only the first filename is used.
 *
 * Returns: (transfer full): a #MirageDisc object on success, %NULL on failure. The reference to
 * the object should be released using g_object_unref() when no longer needed.
 */
MirageDisc *mirage_context_load_image (MirageContext *self, gchar **filenames, GError **error)
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


/**********************************************************************\
 *                   Public API: fragment creation                    *
\**********************************************************************/
/**
 * mirage_context_create_fragment:
 * @self: a #MirageContext
 * @fragment_interface: (in): interface that fragment should implement
 * @stream: (in): the data stream that fragment should be able to handle
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Creates a #MirageFragment implementation that implements interface specified
 * by @fragment_interface and can handle data stored in @stream.
 *
 * Returns: (transfer full): a #MirageFragment object on success, %NULL on failure. The reference
 * to the object should be released using g_object_unref() when no longer needed.
 */
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
 *                 Public API: file stream creation                   *
\**********************************************************************/
/**
 * mirage_context_create_file_stream:
 * @self: a #MirageContext
 * @filename: (in): filename to create stream on
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Opens a file pointed to by @filename and creates a chain of file filters
 * on top of it.
 *
 * Returns: (transfer full): on success, an object inheriting #GFilterInputStream (and therefore
 * #GInputStream) and implementing #GSeekable interface is returned, which
 * can be used to access data stored in file. On failure, %NULL is returned.
 * The reference to the object should be released using g_object_unref()
 * when no longer needed.
 */
GInputStream *mirage_context_create_file_stream (MirageContext *self, const gchar *filename, GError **error)
{
    GInputStream *stream;
    GFile *file;
    GFileType file_type;
    GError *local_error = NULL;

    gint num_file_filters;
    GType *file_filter_types;

    /* Check if we are already caching the stream */
    stream = g_hash_table_lookup(self->priv->stream_cache, filename);
    if (stream) {
        g_object_ref(stream);
        return stream;
    }

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

    /* Add to file stream cache */
    mirage_context_stream_cache_add(self, filename, stream);

    return stream;
}


/**
 * mirage_context_get_file_stream_filename:
 * @self: a #MirageContext
 * @stream: (in): a #GInputStream
 *
 * Traverses the chain of file filters and retrieves the filename on which
 * the #GFileInputStream, located at the bottom of the chain, was opened.
 *
 * Returns: (transfer none): on success, a pointer to filename on which
 * the underyling file stream was opened. On failure, %NULL is returned.
 */
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

    /* Options */
    self->priv->options = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_variant_unref);

    /* Stream cache */
    self->priv->stream_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    /* GFileInputStream -> filename map */
    self->priv->file_streams_map = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
}

static void mirage_context_finalize (GObject *gobject)
{
    MirageContext *self = MIRAGE_CONTEXT(gobject);

    g_free(self->priv->domain);
    g_free(self->priv->name);

    /* Free options */
    g_hash_table_unref(self->priv->options);

    /* Free stream cache */
    g_hash_table_unref(self->priv->stream_cache);

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
