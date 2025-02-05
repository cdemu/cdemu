/*
 *  libMirage: writer
 *  Copyright (C) 2014 Rok Mandeljc
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
 * SECTION: mirage-writer
 * @title: MirageWriter
 * @short_description: Base object for image writer implementations.
 * @see_also: #MirageDisc, #MirageParser
 * @include: mirage-writer.h
 *
 * #MirageWriter object is a base object for image writer implementations.
 *
 * In contrast to image loading process, which is done by a single
 * function call, the image writing process is designed to be interactive:
 * first, the user creates a #MirageDisc object. On created disc, the
 * medium type should be set, and the filename should be changed to the
 * desired output filename, before passing the disc object to
 * mirage_writer_open_image(), which initializes the writer. Afterwards,
 * the disc layout should be populated with session(s) and track(s), and
 * tracks with fragments. Fragments for tracks are requested from writer
 * using mirage_writer_create_fragment() function. After the fragment
 * is set up for the track, sectors can be written to it using mirage_track_put_sector(),
 * or mirage_disc_put_sector(). To finish image writing, call mirage_writer_finalize_image(),
 * which might write the image descriptor file and rename image data files,
 * if necessary.
 *
 * The above-outlined process makes image writing flexible enough to
 * accomodate both scenarios where all image data to be written is given
 * in advance (e.g., from another image) and cases where image data is
 * arriving incrementally, sector-by-sector.
 *
 * A #MirageWriter provides mirage_writer_convert_image() function, which
 * allows writer to be used for converting an existing image by copying
 * all relevant data from it. In addition, conversion progress reporting
 * can be controlled using mirage_writer_set_conversion_progress_step()
 * and followed using #MirageWriter::conversion-progress.
 *
 * To control image writer parameters, #MirageWriter implements parameter
 * sheet with API for defining parameters and validation/retrieval of
 * user-supplied values.
 */

#include "mirage/config.h"
#include "mirage/mirage.h"

#include <glib/gi18n-lib.h>

#define __debug__ "Writer"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
struct _MirageWriterPrivate
{
    MirageWriterInfo info;

    /* Parameters, given by user */
    GHashTable *parameters;

    /* Parameter sheet, specified by writer implementation */
    GHashTable *parameter_sheet;
    GList *parameter_sheet_list; /* For keeping order */

    /* Progress signalling */
    guint progress_step;
};


/**********************************************************************\
 *                           Writer info API                          *
\**********************************************************************/
static void mirage_writer_info_generate (MirageWriterInfo *info, const gchar *id, const gchar *name)
{
    /* Free old fields */
    mirage_writer_info_free(info);

    /* Copy ID and name */
    info->id = g_strdup(id);
    info->name = g_strdup(name);
}

/**
 * mirage_writer_info_copy:
 * @info: (in): a #MirageWriterInfo to copy data from
 * @dest: (in): a #MirageWriterInfo to copy data to
 *
 * Copies parser information from @info to @dest.
 */
void mirage_writer_info_copy (const MirageWriterInfo *info, MirageWriterInfo *dest)
{
    dest->id = g_strdup(info->id);
    dest->name = g_strdup(info->name);
}

/**
 * mirage_writer_info_free:
 * @info: (in): a #MirageWriterInfo to free
 *
 * Frees the allocated fields in @info (but not the structure itself!).
 */
void mirage_writer_info_free (MirageWriterInfo *info)
{
    g_free(info->id);
    g_free(info->name);
}



/**********************************************************************\
 *                         Parameter sheet API                        *
\**********************************************************************/
static void free_parameter_sheet_entry (MirageWriterParameter *parameter)
{
    g_free(parameter->name);
    g_free(parameter->description);

    g_variant_unref(parameter->default_value);

    if (parameter->enum_values) {
        g_variant_unref(parameter->enum_values);
    }

    g_free(parameter);
}

static void mirage_writer_add_parameter (MirageWriter *self, const gchar *id, const gchar *name, const gchar *description, GVariant *default_value, GVariant *enum_values)
{
    MirageWriterParameter *parameter = g_new0(MirageWriterParameter, 1);

    parameter->name = g_strdup(name);
    parameter->description = g_strdup(description);

    parameter->default_value = g_variant_ref_sink(default_value);

    if (enum_values) {
        parameter->enum_values = g_variant_ref_sink(enum_values);
    }

    /* Insert into hash table for quick lookup */
    g_hash_table_insert(self->priv->parameter_sheet, (gpointer)id, parameter);

    /* Insert into list for ordered access */
    self->priv->parameter_sheet_list = g_list_append(self->priv->parameter_sheet_list, g_strdup(id));
}


/**
 * mirage_writer_add_parameter_boolean:
 * @self: a #MirageWriter
 * @id: (in): parameter ID
 * @name: (in): parameter name
 * @description: (in): parameter description
 * @default_value: (in): default value
 *
 * Adds a boolean parameter to writer's parameter sheet.
 */
void mirage_writer_add_parameter_boolean (MirageWriter *self, const gchar *id, const gchar *name, const gchar *description, gboolean default_value)
{
    mirage_writer_add_parameter(self, id, name, description, g_variant_new("b", default_value), NULL);
}

/**
 * mirage_writer_add_parameter_string:
 * @self: a #MirageWriter
 * @id: (in): parameter ID
 * @name: (in): parameter name
 * @description: (in): parameter description
 * @default_value: (in): default value
 *
 * Adds a string parameter to writer's parameter sheet.
 */
void mirage_writer_add_parameter_string (MirageWriter *self, const gchar *id, const gchar *name, const gchar *description, const gchar *default_value)
{
    mirage_writer_add_parameter(self, id, name, description, g_variant_new("s", default_value), NULL);
}

/**
 * mirage_writer_add_parameter_int:
 * @self: a #MirageWriter
 * @id: (in): parameter ID
 * @name: (in): parameter name
 * @description: (in): parameter description
 * @default_value: (in): default value
 *
 * Adds an integer parameter to writer's parameter sheet.
 */
void mirage_writer_add_parameter_int (MirageWriter *self, const gchar *id, const gchar *name, const gchar *description, gint default_value)
{
    mirage_writer_add_parameter(self, id, name, description, g_variant_new("i", default_value), NULL);
}

/**
 * mirage_writer_add_parameter_enum:
 * @self: a #MirageWriter
 * @id: (in): parameter ID
 * @name: (in): parameter name
 * @description: (in): parameter description
 * @default_value: (in): default value
 * @...: (in): %NULL-terminated list of valid string values
 *
 * Adds an enum parameter to writer's parameter sheet. The enum parameters
 * are treated as string parameters, except they have a pre-defined set
 * of valid values.
 */
void mirage_writer_add_parameter_enum (MirageWriter *self, const gchar *id, const gchar *name, const gchar *description, const gchar *default_value, ...)
{
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE_STRING_ARRAY);

    va_list args;
    va_start(args, default_value);
    while (TRUE) {
        const gchar *value = va_arg(args, const gchar *);
        if (!value) {
            break;
        }
        g_variant_builder_add(&builder, "s", value);
    }
    va_end(args);

    mirage_writer_add_parameter(self, id, name, description, g_variant_new("s", default_value), g_variant_builder_end(&builder));
}


/**
 * mirage_writer_lookup_parameter_ids:
 * @self: a #MirageWriter
 *
 * Retrieves the list of parameter ID strings from writer's parameter
 * sheet. The IDs are ordered in the same way as they were inserted into
 * the parameter sheet.
 *
 * Returns: (transfer none) (element-type gchar*): the list of parameter
 * IDs. The list and its elements belong to the writer and should not be
 * modified.
 */
GList *mirage_writer_lookup_parameter_ids (MirageWriter *self)
{
    return self->priv->parameter_sheet_list;
}

/**
 * mirage_writer_lookup_parameter_info:
 * @self: a #MirageWriter
 * @id: parameter ID string
 *
 * Looks up the parameter with specified @id in writer's parameter sheet
 * and retrieves the parameter's information structure.
 *
 * Returns: the pointer to parameter's information structure on success,
 * %NULL on failure. The information structure belong to the writer and
 * should not be modified.
 */
const MirageWriterParameter *mirage_writer_lookup_parameter_info (MirageWriter *self, const gchar *id)
{
    return g_hash_table_lookup(self->priv->parameter_sheet, id);
}


static gboolean mirage_writer_validate_parameters (MirageWriter *self, GHashTable *parameters, GError **error)
{
    GHashTableIter iter;
    gpointer key, value;

    /* Iterate over user-supplied parameters */
    g_hash_table_iter_init(&iter, parameters);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        /* Look up the parameter in writer's parameter sheet */
        MirageWriterParameter *sheet_entry = g_hash_table_lookup(self->priv->parameter_sheet, key);

        /* For now, we just skip unhandled parameters */
        if (!sheet_entry) {
            continue;
        }

        /* Validate the type */
        if (!g_variant_is_of_type(value, g_variant_get_type(sheet_entry->default_value))) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_WRITER_ERROR, Q_("Parameter '%s' has invalid type (expected '%s', got '%s')!"), (const gchar *)key, g_variant_get_type_string(value), g_variant_get_type_string(sheet_entry->default_value));
            return FALSE;
        }

        /* If parameter is an enum, validate the value */
        if (sheet_entry->enum_values) {
            gboolean valid = FALSE;

            for (guint i = 0; i < g_variant_n_children(sheet_entry->enum_values); i++) {
                GVariant *enum_value = g_variant_get_child_value(sheet_entry->enum_values, i);
                valid = g_variant_equal(value, enum_value);
                g_variant_unref(enum_value);

                if (valid) {
                    break;
                }
            }

            if (!valid) {
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_WRITER_ERROR, Q_("Parameter '%s' has invalid value!"), (const gchar *)key);
                return FALSE;
            }
        }
    }

    return TRUE;
}


/**********************************************************************\
 *                       Parameter retrieval API                      *
\**********************************************************************/
static GVariant *mirage_writer_get_parameter (MirageWriter *self, const gchar *id)
{
    GVariant *value;

    /* First, try to get it from user-set parameters */
    value = g_hash_table_lookup(self->priv->parameters, id);

    if (!value) {
        const MirageWriterParameter *parameter = g_hash_table_lookup(self->priv->parameter_sheet, id);
        value = parameter->default_value;
    }

    g_assert(value != NULL); /* If we fail here, there's bug in the code */

    return value;
}


/**
 * mirage_writer_get_parameter_boolean:
 * @self: a #MirageWriter
 * @id: parameter ID string
 *
 * Looks up the boolean parameter with specified @id from user-supplied
 * writer parameters and returns its value. If user did not supply the
 * parameter, its default value is returned instead.
 *
 * Returns: user-supplied parameter value.
 */
gboolean mirage_writer_get_parameter_boolean (MirageWriter *self, const gchar *id)
{
    return g_variant_get_boolean(mirage_writer_get_parameter(self, id));
}

/**
 * mirage_writer_get_parameter_string:
 * @self: a #MirageWriter
 * @id: parameter ID string
 *
 * Looks up the string parameter with specified @id from user-supplied
 * writer parameters and returns its value. If user did not supply the
 * parameter, its default value is returned instead.
 *
 * Returns: user-supplied parameter value.
 */
const gchar *mirage_writer_get_parameter_string (MirageWriter *self, const gchar *id)
{
    return g_variant_get_string(mirage_writer_get_parameter(self, id), NULL);
}

/**
 * mirage_writer_get_parameter_int:
 * @self: a #MirageWriter
 * @id: parameter ID string
 *
 * Looks up the string parameter with specified @id from user-supplied
 * writer parameters and returns its value. If user did not supply the
 * parameter, its default value is returned instead.
 *
 * Returns: user-supplied parameter value.
 */
gint mirage_writer_get_parameter_int (MirageWriter *self, const gchar *id)
{
    return g_variant_get_int32(mirage_writer_get_parameter(self, id));
}

/**
 * mirage_writer_get_parameter_enum:
 * @self: a #MirageWriter
 * @id: parameter ID string
 *
 * Looks up the enum parameter with specified @id from user-supplied
 * writer parameters and returns its value. If user did not supply the
 * parameter, its default value is returned instead.
 *
 * Returns: user-supplied parameter value.
 */
const gchar *mirage_writer_get_parameter_enum (MirageWriter *self, const gchar *id)
{
    return g_variant_get_string(mirage_writer_get_parameter(self, id), NULL);
}


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
/**
 * mirage_writer_generate_info:
 * @self: a #MirageWriter
 * @id: (in): writer ID
 * @name: (in): writer name
 *
 * Generates writer information from the input fields. It is intended as a function
 * for creating writer information in writer implementations.
 */
void mirage_writer_generate_info (MirageWriter *self, const gchar *id, const gchar *name)
{
    mirage_writer_info_generate(&self->priv->info, id, name);
}

/**
 * mirage_writer_get_info:
 * @self: a #MirageWriter
 *
 * Retrieves writer information.
 *
 * Returns: (transfer none): a pointer to writer information structure.  The
 * structure belongs to object and should not be modified.
 */
const MirageWriterInfo *mirage_writer_get_info (MirageWriter *self)
{
    return &self->priv->info;
}


/**
 * mirage_writer_open_image:
 * @self: a #MirageWriter
 * @disc: (in): disc object for which the image will be created
 * @parameters: (in) (element-type utf8 GLib.Variant): writer parameters
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Initializes the image writer to start the writing process for @disc.
 * The latter should have set the image filename(s) and medium type, but
 * does not have to have disc layout (sessions, tracks) built yet.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_writer_open_image (MirageWriter *self, MirageDisc *disc, GHashTable *parameters, GError **error)
{
    /* Clear old parameters */
    if (self->priv->parameters) {
        g_hash_table_unref(self->priv->parameters);
        self->priv->parameters = NULL;
    }

    /* Store new parameters */
    if (parameters) {
        /* Validate parameters */
        if (!mirage_writer_validate_parameters(self, parameters, error)) {
            return FALSE;
        }

        /* Store pointer and ref */
        self->priv->parameters = g_hash_table_ref(parameters);
    }

    /* Provided by implementation */
    return MIRAGE_WRITER_GET_CLASS(self)->open_image_impl(self, disc, error);
}

/**
 * mirage_writer_create_fragment:
 * @self: a #MirageWriter
 * @track: (in): track for which the fragment is to be created
 * @role: (in): fragment role
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Creates a fragment with specified @role for given @track. The latter
 * needs to already be part of disc's layout.
 *
 * The role helps writer implementation determine what kind of fragment
 * to create; for example, some writer implementations create dummy
 * fragments for pregaps.
 *
 * Returns: (transfer full): pointer to created fragment object on success, %NULL on failure
 */
MirageFragment *mirage_writer_create_fragment (MirageWriter *self, MirageTrack *track, MirageFragmentRole role, GError **error)
{
    return MIRAGE_WRITER_GET_CLASS(self)->create_fragment(self, track, role, error);
}


/**
 * mirage_writer_finalize_image:
 * @self: a #MirageWriter
 * @disc: (in): disc object for which the image is being written
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Finalizes the image, possibly creating the image descriptor file if
 * necessary.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_writer_finalize_image (MirageWriter *self, MirageDisc *disc, GError **error)
{
    /* Provided by implementation */
    gboolean succeeded = MIRAGE_WRITER_GET_CLASS(self)->finalize_image(self, disc, error);

    /* Free parameters */
    if (self->priv->parameters) {
        g_hash_table_unref(self->priv->parameters);
        self->priv->parameters = NULL;
    }

    return succeeded;
}


/**
 * mirage_writer_get_conversion_progress_step:
 * @self: a #MirageWriter
 *
 * Retrieves conversion progress step setting.
 *
 * Returns: the value of conversion progress step.
 */
guint mirage_writer_get_conversion_progress_step (MirageWriter *self)
{
    return self->priv->progress_step;
}

/**
 * mirage_writer_set_conversion_progress_step:
 * @self: a #MirageWriter
 * @step: new conversion progress step value
 *
 * Sets conversion progress step. Setting @step to 0 disables conversion
 * progress reporting.
 */
void mirage_writer_set_conversion_progress_step (MirageWriter *self, guint step)
{
    self->priv->progress_step = step;
}


/**
 * mirage_writer_convert_image:
 * @self: a #MirageWriter
 * @filename: (in): filename of output image
 * @original_disc: (in): disc layout obtained from original image
 * @parameters: (in) (element-type utf8 GLib.Variant): writer parameters
 * @cancellable: (in) (allow-none): optional %GCancellable object, NULL to ignore.
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Convenience function that converts an already-opened image stored in
 * @original_disc and writes it to @filename. If conversion progress
 * reporting is enabled via mirage_writer_set_conversion_progress_step(),
 * the #MirageWriter::conversion-progress signal is emitted at specified
 * time intervals during conversion.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_writer_convert_image (MirageWriter *self, const gchar *filename, MirageDisc *original_disc, GHashTable *parameters, GCancellable *cancellable, GError **error)
{
    /* Conversion progress tracking */
    gint num_all_sectors = mirage_disc_layout_get_length(original_disc);
    gint disc_layout_start = mirage_disc_layout_get_start_sector(original_disc);
    guint progress_step_size = num_all_sectors*self->priv->progress_step/100;
    guint conversion_progress = 0;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: image conversion; filename '%s', original disc: %p\n", __debug__, filename, original_disc);

    /* Create disc */
    MirageDisc *new_disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_set_parent(MIRAGE_OBJECT(new_disc), self);
    mirage_disc_set_filename(new_disc, filename);

    /* Copy properties from original disc */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: copying disc properties...\n", __debug__);
    mirage_disc_set_medium_type(new_disc, mirage_disc_get_medium_type(original_disc));

    mirage_disc_layout_set_first_session(new_disc, mirage_disc_layout_get_first_session(original_disc));
    mirage_disc_layout_set_first_track(new_disc, mirage_disc_layout_get_first_track(original_disc));
    mirage_disc_layout_set_start_sector(new_disc, mirage_disc_layout_get_start_sector(original_disc));

    /* FIXME: copy disc structures */

    /* Copy DPM data, if available */
    gint start, resolution, num_entries;
    const guint32 *data;
    mirage_disc_get_dpm_data(original_disc, &start, &resolution, &num_entries, &data);
    if (num_entries) {
        mirage_disc_set_dpm_data(new_disc, start, resolution, num_entries, data);
    }

    /* Open image */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: initializing writer...\n", __debug__);
    if (!mirage_writer_open_image(self, new_disc, parameters, error)) {
        g_object_unref(new_disc);
        return FALSE;
    }

    /* Iterate over sessions and tracks, and copy them */
    gint num_sessions = mirage_disc_get_number_of_sessions(original_disc);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: disc has %d sessions...\n", __debug__, num_sessions);
    for (gint i = 0; i < num_sessions; i++) {
        /* Create and add session */
        MirageSession *original_session = mirage_disc_get_session_by_index(original_disc, i, NULL);
        MirageSession *new_session = g_object_new(MIRAGE_TYPE_SESSION, NULL);

        gint num_languages;
        gint num_tracks;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: processing session %d...\n", __debug__, i);

        mirage_disc_add_session_by_index(new_disc, i, new_session);

        /* Copy session properties */
        mirage_session_set_session_type(new_session, mirage_session_get_session_type(original_session));
        mirage_session_set_mcn(new_session, mirage_session_get_mcn(original_session));

        /* Languages */
        num_languages = mirage_session_get_number_of_languages(original_session);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: session has %d languages...\n", __debug__, num_languages);
        for (gint j = 0; j < num_languages; j++) {
            MirageLanguage *language = mirage_session_get_language_by_index(original_session, j, NULL);
            gint language_code = mirage_language_get_code(language);

            mirage_session_add_language(new_session, language_code, language, NULL);

            g_object_unref(language);
        }

        /* Tracks */
        num_tracks = mirage_session_get_number_of_tracks(original_session);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: session has %d tracks...\n", __debug__, num_tracks);
        for (gint j = 0; j < num_tracks; j++) {
            gint num_indices;
            gint num_fragments;

            gint track_start;
            gint num_sectors;

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: processing track %d...\n", __debug__, j);

            /* Create and add track */
            MirageTrack *original_track = mirage_session_get_track_by_index(original_session, j, NULL);
            MirageTrack *new_track = g_object_new(MIRAGE_TYPE_TRACK, NULL);

            mirage_session_add_track_by_index(new_session, j, new_track);

            /* Copy track properties */
            mirage_track_set_flags(new_track, mirage_track_get_flags(original_track));
            mirage_track_set_sector_type(new_track, mirage_track_get_sector_type(original_track));
            mirage_track_set_isrc(new_track, mirage_track_get_isrc(original_track));

            /* Track start (also needed for fragments) */
            track_start = mirage_track_get_track_start(original_track);
            mirage_track_set_track_start(new_track, track_start);

            /* Indices */
            num_indices = mirage_track_get_number_of_indices(original_track);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: track has %d indices\n", __debug__, num_indices);
            for (gint k = 0; k < num_indices; k++) {
                MirageIndex *index = mirage_track_get_index_by_number(original_track, k, NULL);
                gint index_address = mirage_index_get_number(index);

                mirage_track_add_index(new_track, index_address, NULL);

                g_object_unref(index);
            }

            /* Languages */
            num_languages = mirage_track_get_number_of_languages(original_track);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: track has %d languages\n", __debug__, num_languages);
            for (gint k = 0; k < num_languages; k++) {
                MirageLanguage *language = mirage_track_get_language_by_index(original_track, k, NULL);
                gint language_code = mirage_language_get_code(language);

                mirage_track_add_language(new_track, language_code, language, NULL);

                g_object_unref(language);
            }

            /* Fragments */
            num_fragments = mirage_track_get_number_of_fragments(original_track);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: track has %d fragments\n", __debug__, num_fragments);
            for (gint k = 0; k < num_fragments; k++) {
                /* Get original fragment, its address and length */
                MirageFragment *fragment = mirage_track_get_fragment_by_index(original_track, k, NULL);
                gint fragment_address = mirage_fragment_get_address(fragment);
                gint fragment_length = mirage_fragment_get_length(fragment);
                g_object_unref(fragment);

                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: fragment %d: address %d, length: %d\n", __debug__, k, fragment_address, fragment_length);

                /* Request new fragment from writer */
                fragment = NULL;
                if (fragment_address + fragment_length <= track_start) {
                    /* Pure pregap fragment - lies completely before the track start */
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: fragment is a pure-pregap fragment\n", __debug__);
                    fragment = mirage_writer_create_fragment(self, new_track, MIRAGE_FRAGMENT_PREGAP, error);
                } else {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: fragment is a data (or pregap + data) fragment\n", __debug__);
                    fragment = mirage_writer_create_fragment(self, new_track, MIRAGE_FRAGMENT_DATA, error);
                }

                if (!fragment) {
                    g_object_unref(new_track);
                    g_object_unref(original_track);
                    g_object_unref(new_session);
                    g_object_unref(original_session);
                    g_object_unref(new_disc);
                    return FALSE;
                }

                /* Set fragment length */
                mirage_fragment_set_length(fragment, fragment_length);

                /* Add to track */
                mirage_track_add_fragment(new_track, k, fragment);

                g_object_unref(fragment);
            }

            /* Now, copy sectors, one by one */
            num_sectors = mirage_track_layout_get_length(original_track);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WRITER, "%s: copying sectors (%d)\n", __debug__, num_sectors);
            for (gint sector_address = 0; sector_address < num_sectors; sector_address++) {
                gboolean succeeded = TRUE;

                /* Get sector from original track using track-relative address... */
                MirageSector *sector = mirage_track_get_sector(original_track, sector_address, FALSE, error);
                if (sector) {
                    if (progress_step_size) {
                        gint sector_count = mirage_sector_get_address(sector) - disc_layout_start;

                        if (sector_count >= conversion_progress*progress_step_size) {
                            g_signal_emit_by_name(self, "conversion-progress", conversion_progress*self->priv->progress_step, NULL);
                            conversion_progress++;
                        }
                    }

                    /* ... and put it into new track */
                    succeeded = mirage_track_put_sector(new_track, sector, error);

                    g_object_unref(sector);
                } else {
                    succeeded = FALSE;
                }

                /* Check if conversion is to be cancelled at user's request */
                succeeded &= !g_cancellable_set_error_if_cancelled(cancellable, error);

                if (!succeeded) {
                    g_object_unref(new_track);
                    g_object_unref(original_track);
                    g_object_unref(new_session);
                    g_object_unref(original_session);
                    g_object_unref(new_disc);
                    return FALSE;
                }
            }

            g_object_unref(new_track);
            g_object_unref(original_track);
        }

        g_object_unref(new_session);
        g_object_unref(original_session);
    }

    /* Finalize image */
    if (!mirage_writer_finalize_image(self, new_disc, error)) {
        g_object_unref(new_disc);
        return FALSE;
    }

    g_object_unref(new_disc);
    return TRUE;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE(MirageWriter, mirage_writer, MIRAGE_TYPE_OBJECT)


static void mirage_writer_init (MirageWriter *self)
{
    self->priv = mirage_writer_get_instance_private(self);

    /* Make sure all fields are empty */
    memset(&self->priv->info, 0, sizeof(self->priv->info));

    /* Initialize parameter sheet */
    self->priv->parameter_sheet = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, (GDestroyNotify)free_parameter_sheet_entry);

    /* Conversion progress; disabled by default */
    self->priv->progress_step = 0;
}

static void mirage_writer_dispose (GObject *gobject)
{
    MirageWriter *self = MIRAGE_WRITER(gobject);

    /* Free user-set parameters */
    if (self->priv->parameters) {
        g_hash_table_unref(self->priv->parameters);
        self->priv->parameters = NULL;
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_writer_parent_class)->dispose(gobject);
}

static void mirage_writer_finalize (GObject *gobject)
{
    MirageWriter *self = MIRAGE_WRITER(gobject);

    /* Free parameter sheet */
    if (self->priv->parameter_sheet) {
        g_hash_table_unref(self->priv->parameter_sheet);
        self->priv->parameter_sheet = NULL;
    }

    g_list_free_full(self->priv->parameter_sheet_list, (GDestroyNotify)g_free);
    self->priv->parameter_sheet_list = NULL;

    /* Free info structure */
    mirage_writer_info_free(&self->priv->info);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_writer_parent_class)->finalize(gobject);
}

static void mirage_writer_class_init (MirageWriterClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = mirage_writer_dispose;
    gobject_class->finalize = mirage_writer_finalize;

    klass->open_image_impl = NULL;
    klass->create_fragment = NULL;
    klass->finalize_image = NULL;

    /* Signals */
    /**
     * MirageWriter::conversion-progress:
     * @object: a #MirageWriter
     * @progress: percentual image conversion progress
     *
     * Emitted when the image conversion progress reaches a new progress mark.
     */
    g_signal_new("conversion-progress", G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_VOID__UINT, G_TYPE_NONE, 1, G_TYPE_UINT, NULL);
}
