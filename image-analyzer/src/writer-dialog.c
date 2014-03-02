/*
 *  Image Analyzer: Writer dialog
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <gtk/gtk.h>
#include <mirage.h>

#include "writer-dialog.h"


/**********************************************************************\
 *                            Private structure                       *
\**********************************************************************/
#define IA_WRITER_DIALOG_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), IA_TYPE_WRITER_DIALOG, IaWriterDialogPrivate))

struct _IaWriterDialogPrivate
{
    GtkWidget *entry_filename;

    GtkWidget *frame_writer;
    GtkWidget *writer_options_ui;
    GHashTable *writer_options_widgets;

    MirageWriter *image_writer;
};


/**********************************************************************\
 *                             Callbacks                              *
\**********************************************************************/
static void cb_choose_file_clicked (IaWriterDialog *self, GtkButton *button G_GNUC_UNUSED)
{
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Select output image file",
        GTK_WINDOW(self),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Save", GTK_RESPONSE_ACCEPT,
        NULL);

    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
    gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(dialog), FALSE);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(self->priv->entry_filename), filename);
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}

static void cb_writer_changed (IaWriterDialog *self, GtkComboBox *combobox)
{
    /* Destroy old writer UI */
    if (self->priv->writer_options_ui) {
        gtk_widget_destroy(self->priv->writer_options_ui);
        self->priv->writer_options_ui = NULL;
    }

    if (self->priv->image_writer) {
        g_object_unref(self->priv->image_writer);
        self->priv->image_writer = NULL;
    }

    g_hash_table_remove_all(self->priv->writer_options_widgets);

    /* Hide Writer frame */
    gtk_widget_hide(self->priv->frame_writer);

    /* Get selected writer ID and create a writer */
    gchar *writer_id = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combobox));
    if (!writer_id) {
        return;
    }

    self->priv->image_writer = mirage_create_writer(writer_id, NULL);

    g_free(writer_id);

    if (!self->priv->image_writer) {
        return;
    }

    /* Build writer options GUI */
    GtkWidget *grid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);

    gint row = 0;

    GList *parameter_ids = mirage_writer_lookup_parameter_ids(self->priv->image_writer);

    for (GList *iter = g_list_first(parameter_ids); iter; iter = g_list_next(iter)) {
        const gchar *id = iter->data;
        const MirageWriterParameter *info = mirage_writer_lookup_parameter_info(self->priv->image_writer, id);

        GtkWidget *widget;
        gboolean needs_label = TRUE;

        if (info->enum_values) {
            /* Enum; create a combo box */
            gint num_enum_values = g_variant_n_children(info->enum_values);
            widget = gtk_combo_box_text_new();

            /* Fill values */
            for (gint i = 0; i < num_enum_values; i++) {
                const gchar *enum_value;
                g_variant_get_child(info->enum_values, i, "&s", &enum_value);
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widget), enum_value);
            }

            /* Default value */
            const gchar *default_value = g_variant_get_string(info->default_value, NULL);
            GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(widget));
            GtkTreeIter tree_iter;
            gboolean tree_iter_valid;

            tree_iter_valid = gtk_tree_model_get_iter_first(model, &tree_iter);
            while (tree_iter_valid) {
                gchar *enum_value;
                gboolean found;

                gtk_tree_model_get(model, &tree_iter, 0, &enum_value, -1);
                found = !g_strcmp0(default_value, enum_value);
                g_free(enum_value);

                if (found) {
                    gtk_combo_box_set_active_iter(GTK_COMBO_BOX(widget), &tree_iter);
                    break;
                }
            }
        } else if (g_variant_is_of_type(info->default_value, G_VARIANT_TYPE_STRING)) {
            /* String; create a text entry */
            widget = gtk_entry_new();
            /* Default value */
            gtk_entry_set_text(GTK_ENTRY(widget), g_variant_get_string(info->default_value, NULL));
        } else if (g_variant_is_of_type(info->default_value, G_VARIANT_TYPE_BOOLEAN)) {
            /* Boolean; create a check button */
            widget = gtk_check_button_new_with_label(info->name);
            needs_label = FALSE;
            /* Default value */
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), g_variant_get_boolean(info->default_value));
        } else if (g_variant_is_of_type(info->default_value, G_VARIANT_TYPE_INT32)) {
            /* Integer; create a spin button */
            widget = gtk_spin_button_new_with_range(G_MININT32, G_MAXINT32, 1);
            gtk_spin_button_set_digits(GTK_SPIN_BUTTON(widget), 0);
            /* Default value */
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(widget), g_variant_get_int32(info->default_value));
        } else {
            continue;
        }

        /* Attach widget, adding label if necessary */
        if (needs_label) {
            gchar *label_text = g_strdup_printf("%s: ", info->name);
            GtkWidget *label = gtk_label_new(label_text);
            g_free(label_text);

            gtk_widget_set_tooltip_text(label, info->description);
            gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);

            gtk_widget_set_hexpand(widget, TRUE);
            gtk_grid_attach_next_to(GTK_GRID(grid), widget, label, GTK_POS_RIGHT, 1, 1);
        } else {
            gtk_widget_set_hexpand(widget, TRUE);
            gtk_grid_attach(GTK_GRID(grid), widget, 0, row, 2, 1);
        }

        /* Add widget to our list */
        g_hash_table_insert(self->priv->writer_options_widgets, (gpointer)id, widget);

        row++;
    }

    /* Set and display writer options GUI */
    gtk_container_add(GTK_CONTAINER(self->priv->frame_writer), grid);
    self->priv->writer_options_ui = grid;

    gtk_widget_show_all(self->priv->frame_writer);
}


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
const gchar *ia_writer_dialog_get_filename (IaWriterDialog *self)
{
    return gtk_entry_get_text(GTK_ENTRY(self->priv->entry_filename));
}

MirageWriter *ia_writer_dialog_get_writer (IaWriterDialog *self)
{
    if (self->priv->image_writer) {
        return g_object_ref(self->priv->image_writer);
    }
    return NULL;
}

GHashTable *ia_writer_dialog_get_writer_parameters (IaWriterDialog *self)
{
    GHashTable *writer_parameters = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, (GDestroyNotify)g_variant_unref);

    /* Iterate over our writer options widgets and collect their values */
    GHashTableIter iter;
    gpointer parameter_id, widget;

    g_hash_table_iter_init(&iter, self->priv->writer_options_widgets);
    while (g_hash_table_iter_next(&iter, &parameter_id, &widget)) {
        GVariant *value;

        if (GTK_IS_ENTRY(widget)) {
            value = g_variant_new("s", gtk_entry_get_text(GTK_ENTRY(widget)));
        } else if (GTK_IS_SPIN_BUTTON(widget)) {
            value = g_variant_new("i", gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget)));
        } else if (GTK_IS_CHECK_BUTTON(widget)) {
            value = g_variant_new("b", gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)));
        } else if (GTK_IS_COMBO_BOX_TEXT(widget)) {
            value = g_variant_new("s", gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widget)));
        } else {
            continue;
        }

        g_hash_table_insert(writer_parameters, parameter_id, value);
    }

    return writer_parameters;
}


static void setup_gui (IaWriterDialog *self)
{
    GtkWidget *frame;
    GtkWidget *grid;
    GtkWidget *label, *entry, *button;
    GtkWidget *combobox;
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(self));

    /* Window */
    gtk_window_set_title(GTK_WINDOW(self), "Convert image");
    gtk_container_set_border_width(GTK_CONTAINER(self), 5);

    /* Buttons */
    gtk_dialog_add_buttons(GTK_DIALOG(self), "OK", GTK_RESPONSE_ACCEPT, "Cancel", GTK_RESPONSE_REJECT, NULL);

    /* Frame: image settings */
    frame = gtk_frame_new("Output image");
    gtk_container_add(GTK_CONTAINER(content_area), frame);

    grid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_container_add(GTK_CONTAINER(frame), grid);

    /* Filename */
    label = gtk_label_new("Filename: ");
    gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);

    entry = gtk_entry_new();
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_grid_attach_next_to(GTK_GRID(grid), entry, label, GTK_POS_RIGHT, 1, 1);
    self->priv->entry_filename = entry;

    button = gtk_button_new_with_label("Choose");
    gtk_grid_attach_next_to(GTK_GRID(grid), button, entry, GTK_POS_RIGHT, 1, 1);
    g_signal_connect_swapped(button, "clicked", G_CALLBACK(cb_choose_file_clicked), self);

    /* Writer */
    label = gtk_label_new("Writer: ");
    gtk_grid_attach(GTK_GRID(grid), label, 0, 1, 1, 1);

    combobox = gtk_combo_box_text_new();
    g_signal_connect_swapped(combobox, "changed", G_CALLBACK(cb_writer_changed), self);
    gtk_grid_attach_next_to(GTK_GRID(grid), combobox, label, GTK_POS_RIGHT, 2, 1);

    /* Populate combo box */
    gint num_writers;
    const MirageWriterInfo *writers_info;

    mirage_get_writers_info(&writers_info, &num_writers, NULL);

    for (gint i = 0; i < num_writers; i++) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combobox), writers_info[i].id);
    }

    /* Frame: writer options */
    frame = gtk_frame_new("Writer options");
    gtk_container_add(GTK_CONTAINER(content_area), frame);

    self->priv->frame_writer = frame;
    self->priv->writer_options_ui = NULL;
    self->priv->writer_options_widgets = g_hash_table_new(g_str_hash, g_str_equal);

    gtk_widget_show_all(content_area);
    gtk_widget_hide(self->priv->frame_writer);
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE(IaWriterDialog, ia_writer_dialog, GTK_TYPE_DIALOG);

static void ia_writer_dialog_init (IaWriterDialog *self)
{
    self->priv = IA_WRITER_DIALOG_GET_PRIVATE(self);

    setup_gui(self);
}

static void ia_writer_dialog_dispose (GObject *gobject)
{
    IaWriterDialog *self = IA_WRITER_DIALOG(gobject);

    if (self->priv->image_writer) {
        g_object_unref(self->priv->image_writer);
        self->priv->image_writer = NULL;
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(ia_writer_dialog_parent_class)->dispose(gobject);
}

static void ia_writer_dialog_finalize (GObject *gobject)
{
    IaWriterDialog *self = IA_WRITER_DIALOG(gobject);

    g_hash_table_destroy(self->priv->writer_options_widgets);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(ia_writer_dialog_parent_class)->finalize(gobject);
}

static void ia_writer_dialog_class_init (IaWriterDialogClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = ia_writer_dialog_dispose;
    gobject_class->finalize = ia_writer_dialog_finalize;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(IaWriterDialogPrivate));
}
