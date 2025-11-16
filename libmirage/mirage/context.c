/*
 *  libMirage: context
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
 * SECTION: mirage-context
 * @title: MirageContext
 * @short_description: Context object.
 * @see_also: #MirageContextual, #MirageObject
 * @include: mirage-context.h
 *
 * #MirageContext provides a context, which is attached to libMirage's
 * objects. This way, it allows sharing and propagation of settings, such
 * as debug verbosity, parser options, password function, etc. It also
 * provides I/O stream caching, which is used by image parsers and writers.
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

#include "mirage/config.h"
#include "mirage/mirage.h"

#include <glib/gi18n-lib.h>


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
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
    GDestroyNotify password_data_destroy;

    /* Stream cache */
    GHashTable *input_stream_cache;
    GHashTable *output_stream_cache;
};


/**********************************************************************\
 *                       Input stream cache                           *
\**********************************************************************/
static inline void mirage_context_input_stream_cache_remove (MirageContext *self, gpointer stream)
{
    GHashTableIter iter;
    gpointer key, value;

    /* Iterate over table and remove the entry corresponding to stream */
    g_hash_table_iter_init(&iter, self->priv->input_stream_cache);

    while (g_hash_table_iter_next(&iter, &key, &value)) {
        if (value == stream) {
            g_hash_table_iter_remove(&iter);
        }
    }
}

static inline void mirage_context_input_stream_cache_add (MirageContext *self, const gchar *filename, gpointer stream)
{
    /* Insert into the table */
    g_hash_table_insert(self->priv->input_stream_cache, g_strdup(filename), stream);

    /* Cleanup when stream is destroyed */
    g_object_weak_ref(stream, (GWeakNotify)mirage_context_input_stream_cache_remove, self);
}


/**********************************************************************\
 *                       Output stream cache                          *
\**********************************************************************/
static inline void mirage_context_output_stream_cache_remove (MirageContext *self, gpointer stream)
{
    GHashTableIter iter;
    gpointer key, value;

    /* Iterate over table and remove the entry corresponding to stream */
    g_hash_table_iter_init(&iter, self->priv->output_stream_cache);

    while (g_hash_table_iter_next(&iter, &key, &value)) {
        if (value == stream) {
            g_hash_table_iter_remove(&iter);
        }
    }
}

static inline void mirage_context_output_stream_cache_add (MirageContext *self, const gchar *filename, gpointer stream)
{
    /* Insert into the table */
    g_hash_table_insert(self->priv->output_stream_cache, g_strdup(filename), stream);

    /* Cleanup when stream is destroyed */
    g_object_weak_ref(stream, (GWeakNotify)mirage_context_output_stream_cache_remove, self);
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
 * @func: (in) (nullable) (scope notified) (closure user_data) (destroy destroy): a password function pointer
 * @user_data: (in) (nullable): pointer to user data to be passed to the password function
 * @destroy: (in) (nullable): destroy notify for @user_data, or %NULL
 *
 * Sets the password function to context. The function is used by parsers
 * that support encrypted images to obtain password for unlocking such images.
 *
 * Both @func and @user_data can be %NULL; in that case the appropriate setting
 * will be reset.
 */
void mirage_context_set_password_function (MirageContext *self, MiragePasswordFunction func, gpointer user_data, GDestroyNotify destroy)
{
    /* Destroy old password function's data, if necessary */
    if (self->priv->password_data_destroy) {
        self->priv->password_data_destroy(self->priv->password_data);
    }

    /* Store the pointers */
    self->priv->password_function = func;
    self->priv->password_data = user_data;
    self->priv->password_data_destroy = destroy;
}

/**
 * mirage_context_obtain_password:
 * @self: a #MirageContext
 * @error: (out) (optional): location to store error, or %NULL
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
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, Q_("Context does not have a password function!"));
        return NULL;
    }

    /* Call the function pointer */
    password = (*self->priv->password_function)(self->priv->password_data);

    if (!password) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LIBRARY_ERROR, Q_("Password has not been provided!"));
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
 * @error: (out) (optional): location to store error, or %NULL
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
    MirageStream **streams;

    gint num_parsers;
    const GType *parser_types;

    gint num_filenames = g_strv_length(filenames);

    if (!num_filenames) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("No image files given!"));
        return NULL;
    }

    /* Get the list of supported parsers */
    if (!mirage_get_parsers_type(&parser_types, &num_parsers, error)) {
        return NULL;
    }

    /* Create streams */
    streams = g_new0(MirageStream *, num_filenames+1);
    for (gint i = 0; i < num_filenames; i++) {
        streams[i] = mirage_context_create_input_stream(self, filenames[i], error);
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
            /* MIRAGE_ERROR_CANNOT_HANDLE is the only acceptable error
               here; anything else indicates that parser attempted to
               handle image and failed */
            if (local_error->code == MIRAGE_ERROR_CANNOT_HANDLE) {
                g_error_free(local_error);
            } else {
                g_propagate_error(error, local_error);
                goto end;
            }
        }
    }

    /* No parser found */
    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("No parser can handle the image file!"));

end:
    /* Close streams */
    for (gint i = 0; streams[i]; i++) {
        g_object_unref(streams[i]);
    }
    g_free(streams);

    return disc;
}


/**********************************************************************\
 *                 Public API: file stream creation                   *
\**********************************************************************/
/**
 * mirage_context_create_input_stream:
 * @self: a #MirageContext
 * @filename: (in): filename to create input stream on
 * @error: (out) (optional): location to store error, or %NULL
 *
 * Opens a file pointed to by @filename and creates a chain of filter
 * streams on top of it.
 *
 * Returns: (transfer full): on success, an object implementing #MirageStream
 * interface is returned, which can be used to access data stored in file.
 * On failure, %NULL is returned. The reference to the object should be
 * released using g_object_unref() when no longer needed.
 */
MirageStream *mirage_context_create_input_stream (MirageContext *self, const gchar *filename, GError **error)
{
    MirageFileStream *file_stream;
    MirageStream *stream;
    GError *local_error = NULL;

    gint num_filter_streams;
    const GType *filter_stream_types;

    /* Check if we are already caching the stream */
    stream = g_hash_table_lookup(self->priv->input_stream_cache, filename);
    if (stream) {
        return g_object_ref(stream);
    }

    /* Get the list of supported file filters */
    if (!mirage_get_filter_streams_type(&filter_stream_types, &num_filter_streams, error)) {
        return NULL;
    }

    /* Open MirageFileStream on the file */
    file_stream = g_object_new(MIRAGE_TYPE_FILE_STREAM, NULL);
    if (!mirage_file_stream_open(file_stream, filename, FALSE, &local_error)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DATA_FILE_ERROR, Q_("Failed to open read-only file stream on data file: %s!"), local_error->message);
        g_error_free(local_error);
        g_object_unref(file_stream);
        return NULL;
    }

    /* Construct a chain of filter streams */
    gboolean found_new;

    stream = MIRAGE_STREAM(file_stream);

    do {
        found_new = FALSE;

        for (gint i = 0; i < num_filter_streams; i++) {
            /* Try opening filter stream on top of underlying stream */
            MirageFilterStream *filter_stream = g_object_new(filter_stream_types[i], NULL);
            mirage_contextual_set_context(MIRAGE_CONTEXTUAL(filter_stream), self);

            if (!mirage_filter_stream_open(filter_stream, stream, FALSE, &local_error)) {
                /* Cannot handle data format... */
                g_object_unref(filter_stream);

                /* MIRAGE_ERROR_CANNOT_HANDLE is the only acceptable
                   error here; anything else indicates that filter stream
                   attempted to handle underlying stream and failed */
                if (local_error->code == MIRAGE_ERROR_CANNOT_HANDLE) {
                    g_error_free(local_error);
                    local_error = NULL;
                } else {
                    g_propagate_error(error, local_error);
                    g_object_unref(stream);
                    return NULL;
                }
            } else {
                /* Release reference to (now) underlying stream */
                g_object_unref(stream);

                /* Filter stream becomes new underlying stream */
                stream = MIRAGE_STREAM(filter_stream);

                /* Repeat the whole cycle */
                found_new = TRUE;
                break;
            }
        }
    } while (found_new);

    /* Make sure that the stream we're returning is rewound to the beginning */
    mirage_stream_seek(stream, 0, G_SEEK_SET, NULL);

    /* Add to file stream cache */
    mirage_context_input_stream_cache_add(self, filename, stream);

    return stream;
}

/**
 * mirage_context_create_output_stream:
 * @self: a #MirageContext
 * @filename: (in): filename to create output stream on
 * @filter_chain: (in) (nullable) (array zero-terminated=1): NULL-terminated array of strings describing types of filters to include in the filter chain, or %NULL
 * @error: (out) (optional): location to store error, or %NULL
 *
 * Opens a file pointed to by @filename and optionally creates a chain
 * of filter streams on top of it.
 *
 * The chain of filters is described by @filter-chain, which is a
 * NULL-terminated array of strings corresponding to type names of
 * filters, with the last filter being the one on the top. Filters are
 * passed by their name types because their actual type values are
 * determined when the plugins are loaded. If invalid filter is specified
 * in the chain, this function will fail. It is the caller's responsibility
 * to construct a valid filter chain.
 *
 * Returns: (transfer full): on success, an object implementing #MirageStream
 * interface is returned, which can be used to write data to file.
 * On failure, %NULL is returned. The reference to the object should be
 * released using g_object_unref() when no longer needed.
 */
MirageStream *mirage_context_create_output_stream (MirageContext *self, const gchar *filename, const gchar **filter_chain, GError **error)
{
    MirageFileStream *file_stream;
    MirageStream *stream;
    GError *local_error = NULL;

    /* Check if we are already caching the stream */
    stream = g_hash_table_lookup(self->priv->output_stream_cache, filename);
    if (stream) {
        return g_object_ref(stream);
    }

    /* Open MirageFileStream on the file */
    file_stream = g_object_new(MIRAGE_TYPE_FILE_STREAM, NULL);
    if (!mirage_file_stream_open(file_stream, filename, TRUE, &local_error)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DATA_FILE_ERROR, Q_("Failed to open read-write file stream on data file: %s!"), local_error->message);
        g_error_free(local_error);
        g_object_unref(file_stream);
        return NULL;
    }

    /* Construct filter chain */
    stream = MIRAGE_STREAM(file_stream);

    if (filter_chain) {
        for (gint i = 0; filter_chain[i]; i++) {
            /* Look-up the filter type */
            GType filter_type = g_type_from_name(filter_chain[i]);
            if (!filter_type) {
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, Q_("Invalid filter type '%s' in filter chain!"), filter_chain[i]);
                g_object_unref(stream);
                return NULL;
            }

            /* Try opening filter stream on top of underlying stream */
            MirageFilterStream *filter_stream = g_object_new(filter_type, NULL);
            mirage_contextual_set_context(MIRAGE_CONTEXTUAL(filter_stream), self);

            if (!mirage_filter_stream_open(filter_stream, stream, TRUE, &local_error)) {
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_STREAM_ERROR, Q_("Failed to create filter type '%s': %s!"), filter_chain[i], local_error->message);
                g_error_free(local_error);
                g_object_unref(filter_stream);
                g_object_unref(stream);
                return NULL;
            } else {
                /* Release reference to (now) underlying stream */
                g_object_unref(stream);

                /* Filter stream becomes new underlying stream */
                stream = MIRAGE_STREAM(filter_stream);
            }
        }
    }

    /* Make sure that the stream we're returning is rewound to the beginning */
    mirage_stream_seek(stream, 0, G_SEEK_SET, NULL);

    /* Add to file stream cache */
    mirage_context_output_stream_cache_add(self, filename, stream);

    return stream;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE_WITH_PRIVATE(MirageContext, mirage_context, G_TYPE_OBJECT)


static void mirage_context_init (MirageContext *self)
{
    self->priv = mirage_context_get_instance_private(self);

    self->priv->domain = NULL;
    self->priv->name = NULL;

    self->priv->password_function = NULL;
    self->priv->password_data = NULL;
    self->priv->password_data_destroy = NULL;

    /* Options */
    self->priv->options = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_variant_unref);

    /* Stream cache */
    self->priv->input_stream_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    self->priv->output_stream_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
}

static void mirage_context_finalize (GObject *gobject)
{
    MirageContext *self = MIRAGE_CONTEXT(gobject);

    g_free(self->priv->domain);
    g_free(self->priv->name);

    /* Destroy password function's data, if necessary */
    if (self->priv->password_data_destroy) {
        self->priv->password_data_destroy(self->priv->password_data);
    }

    /* Free options */
    g_hash_table_unref(self->priv->options);

    /* Free stream cache */
    g_hash_table_unref(self->priv->input_stream_cache);
    g_hash_table_unref(self->priv->output_stream_cache);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_context_parent_class)->finalize(gobject);
}

static void mirage_context_class_init (MirageContextClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = mirage_context_finalize;
}
