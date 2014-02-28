/*
 *  Image Analyzer: Application window
 *  Copyright (C) 2007-2014 Rok Mandeljc
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <gtk/gtk.h>
#include <mirage.h>

#include "application-window.h"

#include "disc-tree-dump.h"

#include "log-window.h"
#include "sector-analysis-window.h"
#include "sector-read-window.h"
#include "disc-structure-window.h"
#include "disc-topology-window.h"
#include "writer-dialog.h"


/**********************************************************************\
 *                            Private structure                       *
\**********************************************************************/
#define IA_APPLICATION_WINDOW_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), IA_TYPE_APPLICATION_WINDOW, IaApplicationWindowPrivate))

struct _IaApplicationWindowPrivate
{
    /* Disc */
    gboolean loaded;
    MirageDisc *disc;
    IaDiscTreeDump *disc_dump;

    /* Dialogs and windows */
    GtkWidget *open_image_dialog;
    GtkWidget *image_writer_dialog;
    GtkWidget *open_dump_dialog;
    GtkWidget *save_dump_dialog;
    GtkWidget *log_window;
    GtkWidget *read_sector_window;
    GtkWidget *sector_analysis_window;
    GtkWidget *disc_topology_window;
    GtkWidget *disc_structure_window;

    /* Status bar */
    GtkWidget *statusbar;
    guint context_id;

    /* Debug */
    MirageContext *mirage_context;
    gboolean debug_to_stdout;
};


/**********************************************************************\
 *                      Debug and logging redirection                 *
\**********************************************************************/
static void capture_log (const gchar *log_domain G_GNUC_UNUSED, GLogLevelFlags log_level, const gchar *message, IaApplicationWindow *self)
{
    /* Append to log */
    ia_log_window_append_to_log(IA_LOG_WINDOW(self->priv->log_window), message);

    /* Errors, critical errors and warnings are always printed to stdout */
    if (log_level & G_LOG_LEVEL_ERROR) {
        g_print("ERROR: %s", message);
        return;
    }
    if (log_level & G_LOG_LEVEL_CRITICAL) {
        g_print("CRITICAL: %s", message);
        return;
    }
    if (log_level & G_LOG_LEVEL_WARNING) {
        g_print("WARNING: %s", message);
        return;
    }

    /* Debug messages are printed to stdout only if user requested so */
    if (self->priv->debug_to_stdout) {
        g_print("%s", message);
    }
}

static void ia_application_window_set_debug_to_stdout (IaApplicationWindow *self, gboolean enabled)
{
    /* Debug flag */
    self->priv->debug_to_stdout = enabled;

    /* Set to GUI (libMirage log window) */
    ia_log_window_set_debug_to_stdout(IA_LOG_WINDOW(self->priv->log_window), enabled);
}

static void ia_application_window_set_debug_mask (IaApplicationWindow *self, gint debug_mask)
{
    /* Debug mask */
    mirage_context_set_debug_mask(self->priv->mirage_context, debug_mask);
}

static void ia_application_window_change_debug_mask (IaApplicationWindow *self)
{
    GtkWidget *dialog, *content_area;
    GtkWidget *vbox;
    GtkWidget **entries = NULL;
    gint mask;

    const MirageDebugMaskInfo *valid_masks;
    gint num_valid_masks;

    /* Get list of supported debug masks */
    mirage_get_supported_debug_masks(&valid_masks, &num_valid_masks, NULL);

    /* Get mask from debug context */
    mask = mirage_context_get_debug_mask(self->priv->mirage_context);

    /* Construct dialog */
    dialog = gtk_dialog_new_with_buttons("Debug mask",
                                         GTK_WINDOW(self->priv->log_window),
                                         GTK_DIALOG_MODAL,
                                         "OK", GTK_RESPONSE_ACCEPT,
                                         "Cancel", GTK_RESPONSE_REJECT,
                                         NULL);

    /* Create the mask widgets */
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_add(GTK_CONTAINER(content_area), vbox);

    entries = g_new(GtkWidget *, num_valid_masks);
    for (gint i = 0; i < num_valid_masks; i++) {
        entries[i] = gtk_check_button_new_with_label(valid_masks[i].name);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(entries[i]), mask & valid_masks[i].value);
        gtk_box_pack_start(GTK_BOX(vbox), entries[i], FALSE, FALSE, 0);
    }

    gtk_widget_show_all(vbox);

    /* Run the dialog */
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        mask = 0;

        for (gint i = 0; i < num_valid_masks; i++) {
            if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(entries[i]))) {
                mask |= valid_masks[i].value;
            }
        }

        /* Set the mask */
        mirage_context_set_debug_mask(self->priv->mirage_context, mask);
    }

    /* Destroy dialog */
    gtk_widget_destroy(dialog);
    g_free(entries);
}


/**********************************************************************\
 *                              Status message                        *
\**********************************************************************/
static void ia_application_window_message (IaApplicationWindow *self, gchar *format, ...)
{
    gchar *message;
    va_list args;

    /* Pop message (so that anything set previously will be removed */
    gtk_statusbar_pop(GTK_STATUSBAR(self->priv->statusbar), self->priv->context_id);

    /* Push message */
    va_start(args, format);
    message = g_strdup_vprintf(format, args);
    va_end(args);

    gtk_statusbar_pop(GTK_STATUSBAR(self->priv->statusbar), self->priv->context_id);
    gtk_statusbar_push(GTK_STATUSBAR(self->priv->statusbar), self->priv->context_id, message);

    g_free(message);
}


/**********************************************************************\
 *                           Open/close image                         *
\**********************************************************************/
static gchar *ia_application_window_get_password (IaApplicationWindow *self)
{
    gchar *password;
    GtkDialog *dialog;
    GtkWidget *hbox, *entry, *label;
    gint result;

    dialog = GTK_DIALOG(gtk_dialog_new_with_buttons(
        "Enter password",
        GTK_WINDOW(self),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "OK", GTK_RESPONSE_OK,
        "Cancel", GTK_RESPONSE_REJECT,
        NULL));
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

    gtk_box_set_spacing(GTK_BOX(gtk_dialog_get_content_area(dialog)), 5);

    label = gtk_label_new("The image you are trying to load is encrypted.");
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(dialog)), label, TRUE, TRUE, 0);

    entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new("Password: "), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(dialog)), hbox, FALSE, FALSE, 0);

    /* Run dialog */
    gtk_widget_show_all(GTK_WIDGET(dialog));
    result = gtk_dialog_run(dialog);
    switch (result) {
        case GTK_RESPONSE_OK: {
            password = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
            break;
        }
        default: {
            password = NULL;
            break;
        }
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));

    return password;
}

static gboolean ia_application_window_close_image_or_dump (IaApplicationWindow *self)
{
    /* Clear disc tree dump */
    ia_disc_tree_dump_clear(self->priv->disc_dump);

    /* Clear disc reference in child windows */
    ia_disc_structure_window_set_disc(IA_DISC_STRUCTURE_WINDOW(self->priv->disc_structure_window), NULL);
    ia_disc_topology_window_set_disc(IA_DISC_TOPOLOGY_WINDOW(self->priv->disc_topology_window), NULL);
    ia_sector_read_window_set_disc(IA_SECTOR_READ_WINDOW(self->priv->read_sector_window), NULL);
    ia_sector_analysis_window_set_disc(IA_SECTOR_ANALYSIS_WINDOW(self->priv->sector_analysis_window), NULL);

    /* Release disc reference */
    if (self->priv->disc) {
        g_object_unref(self->priv->disc);
        self->priv->disc = NULL;
    }

    /* Print message only if something was loaded */
    if (self->priv->loaded) {
        ia_application_window_message(self, "Image/dump closed.");
    }

    /* Clear the log */
    ia_log_window_clear_log(IA_LOG_WINDOW(self->priv->log_window));

    self->priv->loaded = FALSE;

    return TRUE;
}

static gboolean ia_application_window_open_image (IaApplicationWindow *self, gchar **filenames)
{
    GError *error = NULL;

    /* Close any opened image or dump */
    ia_application_window_close_image_or_dump(self);

    /* Load image */
    self->priv->disc = mirage_context_load_image(self->priv->mirage_context, filenames, &error);
    if (!self->priv->disc) {
        g_warning("Failed to create disc: %s\n", error->message);
        ia_application_window_message(self, "Failed to open image: %s", error->message);
        g_error_free(error);
        return FALSE;
    }

    /* Dump disc */
    ia_disc_tree_dump_create_from_disc(self->priv->disc_dump, self->priv->disc);

    /* Set disc reference in child windows */
    ia_disc_structure_window_set_disc(IA_DISC_STRUCTURE_WINDOW(self->priv->disc_structure_window), self->priv->disc);
    ia_disc_topology_window_set_disc(IA_DISC_TOPOLOGY_WINDOW(self->priv->disc_topology_window), self->priv->disc);
    ia_sector_read_window_set_disc(IA_SECTOR_READ_WINDOW(self->priv->read_sector_window), self->priv->disc);
    ia_sector_analysis_window_set_disc(IA_SECTOR_ANALYSIS_WINDOW(self->priv->sector_analysis_window), self->priv->disc);

    self->priv->loaded = TRUE;

    ia_application_window_message(self, "Image successfully opened.");

    return TRUE;
}

static void ia_application_window_open_dump (IaApplicationWindow *self, gchar *filename)
{
    GError *error = NULL;

    /* Close any opened image or dump */
    ia_application_window_close_image_or_dump(self);

    /* Load XML to dump */
    if (!ia_disc_tree_dump_load_xml_dump(self->priv->disc_dump, filename, &error)) {
        g_warning("Failed to dump disc: %s!\n", error->message);
        ia_application_window_message(self, "Failed to load dump!");
        g_error_free(error);
    } else {
        self->priv->loaded = TRUE;
        ia_application_window_message(self, "Dump successfully opened.");
    }
}

static void ia_application_window_save_dump (IaApplicationWindow *self, gchar *filename)
{
    GError *error = NULL;

    if (!ia_disc_tree_dump_save_xml_dump(self->priv->disc_dump, filename, &error)) {
        g_warning("Failed to create dump: %s\n", error->message);
        ia_application_window_message(self, "Failed to create dump!");
        g_error_free(error);
    }
}


/**********************************************************************\
 *                          Image conversion                          *
\**********************************************************************/
static void update_conversion_progress (GtkProgressBar *progress_bar, guint progress, MirageWriter *writer G_GNUC_UNUSED)
{
    /* Update progress bar */
    gtk_progress_bar_set_fraction(progress_bar, progress/100.0);

    /* Process events */
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
}

static void cancel_conversion (GCancellable *cancellable, gint response, GtkButton *button G_GNUC_UNUSED)
{
    if (response == GTK_RESPONSE_CANCEL || response == GTK_RESPONSE_DELETE_EVENT) {
        g_cancellable_cancel(cancellable);
    }
}


static void ia_application_window_convert_image (IaApplicationWindow *self, MirageWriter *writer, const gchar *filename, GHashTable *writer_parameters)
{
    GError *local_error = NULL;
    gboolean succeeded;

    /* Make sure writer has our context */
    mirage_contextual_set_context(MIRAGE_CONTEXTUAL(writer), self->priv->mirage_context);

    /* Create progess dialog */
    GtkWidget *progress_dialog = gtk_dialog_new();
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(progress_dialog));
    GtkWidget *progress_bar = gtk_progress_bar_new();

    g_signal_connect(progress_dialog, "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), NULL); /* We need this because we don't do "run" on the dialog */
    gtk_window_set_title(GTK_WINDOW(progress_dialog), "Image conversion progress");
    gtk_window_set_transient_for(GTK_WINDOW(progress_dialog), GTK_WINDOW(self));
    gtk_window_set_modal(GTK_WINDOW(progress_dialog), TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(progress_dialog), 5);
    gtk_dialog_add_button(GTK_DIALOG(progress_dialog), "Cancel", GTK_RESPONSE_CANCEL);
    gtk_container_add(GTK_CONTAINER(content_area), progress_bar);
    gtk_widget_show_all(progress_dialog);

    /* Conversion cancelling support */
    GCancellable *cancellable = g_cancellable_new();
    g_signal_connect_object(progress_dialog, "response", G_CALLBACK(cancel_conversion), cancellable, G_CONNECT_SWAPPED);

    /* Set up writer's conversion progress reporting */
    mirage_writer_set_conversion_progress_step(writer, 5);
    g_signal_connect_object(writer, "conversion-progress", G_CALLBACK(update_conversion_progress), progress_bar, G_CONNECT_SWAPPED);

    /* Convert */
    succeeded = mirage_writer_convert_image(writer, filename, self->priv->disc, writer_parameters, cancellable, &local_error);
    g_object_unref(cancellable);

    /* Destroy progress dialog */
    gtk_widget_destroy(progress_dialog);

    /* Display success/error message */
    GtkWidget *message_dialog;
    if (!succeeded) {
        message_dialog = gtk_message_dialog_new(
            GTK_WINDOW(self),
            GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_CLOSE,
            local_error->message);

        g_error_free(local_error);
    } else {
        message_dialog = gtk_message_dialog_new(
            GTK_WINDOW(self),
            GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_CLOSE,
            "Image conversion succeeded.");
    }

    gtk_dialog_run(GTK_DIALOG(message_dialog));
    gtk_widget_destroy(message_dialog);
}


/**********************************************************************\
 *                             UI callbacks                           *
\**********************************************************************/
static void open_image_activated (GSimpleAction *action G_GNUC_UNUSED, GVariant *parameter G_GNUC_UNUSED, gpointer user_data)
{
    IaApplicationWindow *self = IA_APPLICATION_WINDOW(user_data);

    /* Run the dialog */
    if (gtk_dialog_run(GTK_DIALOG(self->priv->open_image_dialog)) == GTK_RESPONSE_ACCEPT) {
        GSList *filenames_list;
        GSList *entry;
        gint num_filenames;
        gchar **filenames;
        gint i = 0;

        /* Get filenames from dialog */
        filenames_list = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(self->priv->open_image_dialog));

        /* Create strings array */
        num_filenames = g_slist_length(filenames_list);
        filenames = g_new0(gchar *, num_filenames + 1); /* NULL-terminated */

        /* GSList -> strings array conversion */
        entry = filenames_list;
        while (entry != NULL) {
            filenames[i++] = entry->data;
            entry = g_slist_next(entry);
        }

        /* Open image */
        ia_application_window_open_image(self, filenames);

        /* Free filenames list */
        g_slist_free(filenames_list);
        /* Free filenames array */
        g_strfreev(filenames);
    }

    gtk_widget_hide(GTK_WIDGET(self->priv->open_image_dialog));
}

static void convert_image_activated (GSimpleAction *action G_GNUC_UNUSED, GVariant *parameter G_GNUC_UNUSED, gpointer user_data)
{
    IaApplicationWindow *self = IA_APPLICATION_WINDOW(user_data);

    /* We need an opened image for this */
    if (!self->priv->disc) {
        return;
    }

    /* Run the image writer dialog */
    MirageWriter *writer;
    GHashTable *writer_parameters;
    const gchar *filename;

    gint response;

    while (TRUE) {
        response = gtk_dialog_run(GTK_DIALOG(self->priv->image_writer_dialog));

        if (response == GTK_RESPONSE_ACCEPT) {
            /* Validate filename */
            filename = ia_writer_dialog_get_filename(IA_WRITER_DIALOG(self->priv->image_writer_dialog));
            if (!filename || !strlen(filename)) {
                GtkWidget *message_dialog = gtk_message_dialog_new(
                    GTK_WINDOW(self->priv->image_writer_dialog),
                    GTK_DIALOG_DESTROY_WITH_PARENT,
                    GTK_MESSAGE_ERROR,
                    GTK_BUTTONS_CLOSE,
                    "Image filename/basename not set!");

                gtk_dialog_run(GTK_DIALOG(message_dialog));
                gtk_widget_destroy(message_dialog);
                continue;
            }

            /* Validate writer */
            writer = ia_writer_dialog_get_writer(IA_WRITER_DIALOG(self->priv->image_writer_dialog));
            if (!writer) {
                GtkWidget *message_dialog = gtk_message_dialog_new(
                    GTK_WINDOW(self->priv->image_writer_dialog),
                    GTK_DIALOG_DESTROY_WITH_PARENT,
                    GTK_MESSAGE_ERROR,
                    GTK_BUTTONS_CLOSE,
                    "No image writer is chosen!");

                gtk_dialog_run(GTK_DIALOG(message_dialog));
                gtk_widget_destroy(message_dialog);
                continue;
            }

            /* Get writer parameters */
            writer_parameters = ia_writer_dialog_get_writer_parameters(IA_WRITER_DIALOG(self->priv->image_writer_dialog));

            break;
        } else {
            break;
        }
    }

    /* Hide the dialog */
    gtk_widget_hide(GTK_WIDGET(self->priv->image_writer_dialog));

    /* Conversion */
    if (response == GTK_RESPONSE_ACCEPT) {
        /* Convert */
        ia_application_window_convert_image(self, writer, filename, writer_parameters);
        /* Cleanup */
        g_object_unref(writer);
        g_hash_table_unref(writer_parameters);
    }
}

static void open_dump_activated (GSimpleAction *action G_GNUC_UNUSED, GVariant *parameter G_GNUC_UNUSED, gpointer user_data)
{
    IaApplicationWindow *self = IA_APPLICATION_WINDOW(user_data);

    /* Run the dialog */
    if (gtk_dialog_run(GTK_DIALOG(self->priv->open_dump_dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar *filename;

        /* Get filenames from dialog */
        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(self->priv->open_dump_dialog));

        /* Open dump */
        ia_application_window_open_dump(self, filename);

        /* Free filename */
        g_free(filename);
    }

    gtk_widget_hide(GTK_WIDGET(self->priv->open_dump_dialog));
}


static void save_dump_activated (GSimpleAction *action G_GNUC_UNUSED, GVariant *parameter G_GNUC_UNUSED, gpointer user_data)
{
    IaApplicationWindow *self = IA_APPLICATION_WINDOW(user_data);

    /* We need an opened image or dump for this */
    if (!self->priv->loaded) {
        return;
    }

    /* Run the dialog */
    if (gtk_dialog_run(GTK_DIALOG(self->priv->save_dump_dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar *filename;

        /* Get filenames from dialog */
        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(self->priv->save_dump_dialog));

        /* Save */
        ia_application_window_save_dump(self, filename);

        /* Free filename */
        g_free(filename);
    }

    gtk_widget_hide(GTK_WIDGET(self->priv->save_dump_dialog));
}


static void close_activated (GSimpleAction *action G_GNUC_UNUSED, GVariant *parameter G_GNUC_UNUSED, gpointer user_data)
{
    IaApplicationWindow *self = IA_APPLICATION_WINDOW(user_data);
    gtk_widget_destroy(GTK_WIDGET(self));
}

static void log_window_activated (GSimpleAction *action G_GNUC_UNUSED, GVariant *parameter G_GNUC_UNUSED, gpointer user_data)
{
    IaApplicationWindow *self = IA_APPLICATION_WINDOW(user_data);
    gtk_widget_show_all(GTK_WIDGET(self->priv->log_window));
    gtk_window_present(GTK_WINDOW(self->priv->log_window));
}

static void read_sector_window_activated (GSimpleAction *action G_GNUC_UNUSED, GVariant *parameter G_GNUC_UNUSED, gpointer user_data)
{
    IaApplicationWindow *self = IA_APPLICATION_WINDOW(user_data);
    gtk_widget_show_all(GTK_WIDGET(self->priv->read_sector_window));
    gtk_window_present(GTK_WINDOW(self->priv->read_sector_window));
}

static void sector_analysis_window_activated (GSimpleAction *action G_GNUC_UNUSED, GVariant *parameter G_GNUC_UNUSED, gpointer user_data)
{
    IaApplicationWindow *self = IA_APPLICATION_WINDOW(user_data);
    gtk_widget_show_all(GTK_WIDGET(self->priv->sector_analysis_window));
    gtk_window_present(GTK_WINDOW(self->priv->sector_analysis_window));
}

static void disc_topology_window_activated (GSimpleAction *action G_GNUC_UNUSED, GVariant *parameter G_GNUC_UNUSED, gpointer user_data)
{
    IaApplicationWindow *self = IA_APPLICATION_WINDOW(user_data);
    gtk_widget_show_all(GTK_WIDGET(self->priv->disc_topology_window));
    gtk_window_present(GTK_WINDOW(self->priv->disc_topology_window));
}

static void disc_structure_window_activated (GSimpleAction *action G_GNUC_UNUSED, GVariant *parameter G_GNUC_UNUSED, gpointer user_data)
{
    IaApplicationWindow *self = IA_APPLICATION_WINDOW(user_data);
    gtk_widget_show_all(GTK_WIDGET(self->priv->disc_structure_window));
    gtk_window_present(GTK_WINDOW(self->priv->disc_structure_window));
}


static void callback_debug_to_stdout_change_requested (IaLogWindow *log_window G_GNUC_UNUSED, gboolean value, IaApplicationWindow *self)
{
    /* Set the new value */
    ia_application_window_set_debug_to_stdout(self, value);
}

static void callback_debug_mask_change_requested (IaLogWindow *log_window G_GNUC_UNUSED, IaApplicationWindow *self)
{
    /* Propagate to helper function */
    ia_application_window_change_debug_mask(self);
}


/**********************************************************************\
 *                              GUI setup                             *
\**********************************************************************/
static GtkWidget *build_dialog_open_image (GtkWindow *parent_window)
{
    GtkWidget *dialog;
    GtkFileFilter *filter_all;

    const MirageParserInfo *parsers;
    gint num_parsers;

    const MirageFilterStreamInfo *filter_streams;
    gint num_filter_streams;

    dialog = gtk_file_chooser_dialog_new(
        "Open File",
        parent_window,
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Open", GTK_RESPONSE_ACCEPT,
        NULL);

    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);
    gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(dialog), FALSE);

    /* "All files" filter */
    filter_all = gtk_file_filter_new();
    gtk_file_filter_set_name(filter_all, "All files");
    gtk_file_filter_add_pattern(filter_all, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter_all);

    /* "All images" filter */
    filter_all = gtk_file_filter_new();
    gtk_file_filter_set_name(filter_all, "All images");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter_all);

    /* Get list of supported parsers */
    mirage_get_parsers_info(&parsers, &num_parsers, NULL);
    for (gint i = 0; i < num_parsers; i++) {
        const MirageParserInfo *info = &parsers[i];
        GtkFileFilter *filter;

        /* Go over all types (NULL-terminated list) */
        for (gint j = 0; info->description[j]; j++) {
            /* Create a parser-specific file chooser filter */
            filter = gtk_file_filter_new();
            gtk_file_filter_set_name(filter, info->description[j]);
            gtk_file_filter_add_mime_type(filter, info->mime_type[j]);
            gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

            /* "All images" filter */
            gtk_file_filter_add_mime_type(filter_all, info->mime_type[j]);
        }
    }

    /* Get list of supported file filters */
    mirage_get_filter_streams_info(&filter_streams, &num_filter_streams, NULL);
    for (gint i = 0; i < num_filter_streams; i++) {
        const MirageFilterStreamInfo *info = &filter_streams[i];
        GtkFileFilter *filter;

        /* Go over all types (NULL-terminated list) */
        for (gint j = 0; info->description[j]; j++) {
            /* Create a parser-specific file chooser filter */
            filter = gtk_file_filter_new();
            gtk_file_filter_set_name(filter, info->description[j]);
            gtk_file_filter_add_mime_type(filter, info->mime_type[j]);
            gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

            /* "All images" filter */
            gtk_file_filter_add_mime_type(filter_all, info->mime_type[j]);
        }
    }

    return dialog;
}

static GtkWidget *build_dialog_open_dump (GtkWindow *parent_window)
{
    GtkWidget *dialog;
    GtkFileFilter *filter;

    dialog = gtk_file_chooser_dialog_new(
        "Open File",
        parent_window,
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Open", GTK_RESPONSE_ACCEPT,
        NULL);

    gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(dialog), FALSE);

    /* "XML files" filter */
    filter= gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "XML files");
    gtk_file_filter_add_pattern(filter, "*.xml");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    /* "All files" filter */
    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "All files");
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    return dialog;
}

static GtkWidget *build_dialog_save_dump (GtkWindow *parent_window)
{
    GtkWidget *dialog;
    GtkFileFilter *filter;

    dialog = gtk_file_chooser_dialog_new(
        "Save File",
        parent_window,
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Save", GTK_RESPONSE_ACCEPT,
        NULL);

    gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(dialog), FALSE);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);

    /* "XML files" filter */
    filter= gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "XML files");
    gtk_file_filter_add_pattern(filter, "*.xml");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    /* "All files" filter */
    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "All files");
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    return dialog;
}

static GActionEntry win_entries[] = {
    /* Image actions */
    { "open_image", open_image_activated, NULL, NULL, NULL, {0} },
    { "convert_image", convert_image_activated, NULL, NULL, NULL, {0} },
    { "open_dump", open_dump_activated, NULL, NULL, NULL, {0} },
    { "save_dump", save_dump_activated, NULL, NULL, NULL, {0} },
    { "close", close_activated, NULL, NULL, NULL, {0} },
    /* Tools actions */
    { "log_window", log_window_activated, NULL, NULL, NULL, {0} },
    { "read_sector_window", read_sector_window_activated, NULL, NULL, NULL, {0} },
    { "sector_analysis_window", sector_analysis_window_activated, NULL, NULL, NULL, {0} },
    { "disc_topology_window", disc_topology_window_activated, NULL, NULL, NULL, {0} },
    { "disc_structure_window", disc_structure_window_activated, NULL, NULL, NULL, {0} },
};

static void create_gui (IaApplicationWindow *self)
{
    GtkWidget *vbox;

    /* Window */
    gtk_window_set_title(GTK_WINDOW(self), "Image analyzer");
    gtk_window_set_default_size(GTK_WINDOW(self), 300, 400);
    gtk_container_set_border_width(GTK_CONTAINER(self), 5);

    /* Actions */
    g_action_map_add_action_entries(G_ACTION_MAP(self), win_entries, G_N_ELEMENTS(win_entries), self);

    /* VBox */
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(self), vbox);

    /* Scrolled window */
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, TRUE, TRUE, 0);

    /* Tree view */
    GtkWidget *treeview = gtk_tree_view_new();
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeview), FALSE);
    gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), GTK_TREE_MODEL(ia_disc_tree_dump_get_treestore(self->priv->disc_dump)));

    gtk_container_add(GTK_CONTAINER(scrolled_window), treeview);

    GtkTreeViewColumn *column = gtk_tree_view_column_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    gtk_tree_view_column_add_attribute(column, renderer, "text", 0);

    /* Status bar */
    self->priv->statusbar = gtk_statusbar_new();
    gtk_box_pack_start(GTK_BOX(vbox), self->priv->statusbar, FALSE, FALSE, 0);
    self->priv->context_id = gtk_statusbar_get_context_id(GTK_STATUSBAR(self->priv->statusbar), "Message");

    /* Load/save dialogs */
    self->priv->open_image_dialog = build_dialog_open_image(GTK_WINDOW(self));
    self->priv->open_dump_dialog = build_dialog_open_dump(GTK_WINDOW(self));
    self->priv->save_dump_dialog = build_dialog_save_dump(GTK_WINDOW(self));

    /* Image writer dialog */
    self->priv->image_writer_dialog = g_object_new(IA_TYPE_WRITER_DIALOG, NULL);

    /* Log window */
    self->priv->log_window = g_object_new(IA_TYPE_LOG_WINDOW, NULL);
    g_signal_connect(self->priv->log_window, "delete_event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    g_signal_connect(self->priv->log_window, "debug_to_stdout_change_requested", G_CALLBACK(callback_debug_to_stdout_change_requested), self);
    g_signal_connect(self->priv->log_window, "debug_mask_change_requested", G_CALLBACK(callback_debug_mask_change_requested), self);

    /* Read sector window */
    self->priv->read_sector_window = g_object_new(IA_TYPE_SECTOR_READ_WINDOW, NULL);
    g_signal_connect(self->priv->read_sector_window, "delete_event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);

    /* Sector analysis window */
    self->priv->sector_analysis_window = g_object_new(IA_TYPE_SECTOR_ANALYSIS_WINDOW, NULL);
    g_signal_connect(self->priv->sector_analysis_window, "delete_event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);

    /* Disc topology dialog */
    self->priv->disc_topology_window = g_object_new(IA_TYPE_DISC_TOPOLOGY_WINDOW, NULL);
    g_signal_connect(self->priv->disc_topology_window, "delete_event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    ia_disc_topology_window_set_disc(IA_DISC_TOPOLOGY_WINDOW(self->priv->disc_topology_window), NULL);

    /* Disc structure dialog */
    self->priv->disc_structure_window = g_object_new(IA_TYPE_DISC_STRUCTURE_WINDOW, NULL);
    g_signal_connect(self->priv->disc_structure_window, "delete_event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    ia_disc_structure_window_set_disc(IA_DISC_STRUCTURE_WINDOW(self->priv->disc_structure_window), NULL);
}


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
#if 0
gboolean ia_application_window_run (IaApplicationWindow *self, gchar **open_image, gboolean debug_to_stdout, gint debug_mask_initial)
{
    /* Set the debug flags and masks */
    ia_application_window_set_debug_to_stdout(self, debug_to_stdout);
    ia_application_window_set_debug_mask(self, debug_mask_initial);

    /* Open image, if provided */
    if (g_strv_length(open_image)) {
        /* If it ends with .xml, we treat it as a dump file */
        if (mirage_helper_has_suffix(open_image[0], ".xml")) {
            ia_application_window_open_dump(self, open_image[0]);
        } else {
            ia_application_window_open_image(self, open_image);
        }
    }

    /* Show window */
    gtk_widget_show_all(self);

    return TRUE;
}
#endif


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE(IaApplicationWindow, ia_application_window, GTK_TYPE_APPLICATION_WINDOW);

static void ia_application_window_init (IaApplicationWindow *self)
{
    self->priv = IA_APPLICATION_WINDOW_GET_PRIVATE(self);

    self->priv->disc = NULL;

    /* Setup libMirage context */
    self->priv->mirage_context = g_object_new(MIRAGE_TYPE_CONTEXT, NULL);
    mirage_context_set_debug_domain(self->priv->mirage_context, "libMirage");
    mirage_context_set_debug_mask(self->priv->mirage_context, MIRAGE_DEBUG_PARSER);
    mirage_context_set_password_function(self->priv->mirage_context, (MiragePasswordFunction)ia_application_window_get_password, self);

    /* Disc tree dump */
    self->priv->disc_dump = g_object_new(IA_TYPE_DISC_TREE_DUMP, NULL);

    /* Setup GUI */
    create_gui(self);

    /* Setup log handler */
    g_log_set_handler("libMirage", G_LOG_LEVEL_MASK, (GLogFunc)capture_log, self);
}

static void ia_application_window_dispose (GObject *gobject)
{
    IaApplicationWindow *self = IA_APPLICATION_WINDOW(gobject);

    /* Close image */
    if (self->priv->disc) {
        g_object_unref(self->priv->disc);
        self->priv->disc = NULL;
    }

    if (self->priv->disc_dump) {
        g_object_unref(self->priv->disc_dump);
        self->priv->disc_dump = NULL;
    }

    if (self->priv->mirage_context) {
        g_object_unref(self->priv->mirage_context);
        self->priv->mirage_context = NULL;
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(ia_application_window_parent_class)->dispose(gobject);
}

static void ia_application_window_finalize (GObject *gobject)
{
    IaApplicationWindow *self = IA_APPLICATION_WINDOW(gobject);

    gtk_widget_destroy(self->priv->open_image_dialog);
    gtk_widget_destroy(self->priv->image_writer_dialog);
    gtk_widget_destroy(self->priv->open_dump_dialog);
    gtk_widget_destroy(self->priv->save_dump_dialog);
    gtk_widget_destroy(self->priv->log_window);
    gtk_widget_destroy(self->priv->read_sector_window);
    gtk_widget_destroy(self->priv->sector_analysis_window);
    gtk_widget_destroy(self->priv->disc_topology_window);
    gtk_widget_destroy(self->priv->disc_structure_window);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(ia_application_window_parent_class)->finalize(gobject);
}

static void ia_application_window_class_init (IaApplicationWindowClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = ia_application_window_dispose;
    gobject_class->finalize = ia_application_window_finalize;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(IaApplicationWindowPrivate));
}
