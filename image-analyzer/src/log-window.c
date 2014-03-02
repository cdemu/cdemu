/*
 *  Image analyzer: log window
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

#include "log-window.h"


/**********************************************************************\
 *                            Private structure                       *
\**********************************************************************/
#define IA_LOG_WINDOW_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), IA_TYPE_LOG_WINDOW, IaLogWindowPrivate))

struct _IaLogWindowPrivate
{
    /* Text entry */
    GtkWidget *text_view;

    /* Mirror to stdout check button */
    GtkWidget *checkbutton_stdout;

    /* Pointer to Mirage context (no ref held!) */
    MirageContext *mirage_context;
};


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
void ia_log_window_clear_log (IaLogWindow *self)
{
    GtkTextBuffer *buffer;

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->priv->text_view));
    gtk_text_buffer_set_text(buffer, "", -1);
}

void ia_log_window_append_to_log (IaLogWindow *self, const gchar *message)
{
    if (message) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->priv->text_view));
        GtkTextIter iter;

        gtk_text_buffer_get_end_iter(buffer, &iter);
        gtk_text_buffer_insert(buffer, &iter, message, -1);
    }
}


gchar *ia_log_window_get_log_text (IaLogWindow *self)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->priv->text_view));
    GtkTextIter start, end;

    gtk_text_buffer_get_bounds(buffer, &start, &end);

    return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}


void ia_log_window_set_debug_to_stdout (IaLogWindow *self, gboolean enabled)
{
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->priv->checkbutton_stdout), enabled);
}

gboolean ia_log_window_get_debug_to_stdout (IaLogWindow *self)
{
    return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(self->priv->checkbutton_stdout));
}


/**********************************************************************\
 *                             GUI callbacks                          *
\**********************************************************************/
static void clear_button_clicked (GtkButton *button G_GNUC_UNUSED, IaLogWindow *self)
{
    ia_log_window_clear_log(self);
}

static void debug_mask_button_clicked (GtkButton *button G_GNUC_UNUSED, IaLogWindow *self)
{
    GtkWidget *dialog, *content_area;
    GtkWidget *grid;
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
                                         GTK_WINDOW(self),
                                         GTK_DIALOG_MODAL,
                                         "OK", GTK_RESPONSE_ACCEPT,
                                         "Cancel", GTK_RESPONSE_REJECT,
                                         NULL);

    /* Create the mask widgets */
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 2);
    gtk_orientable_set_orientation(GTK_ORIENTABLE(grid), GTK_ORIENTATION_VERTICAL);

    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_add(GTK_CONTAINER(content_area), grid);

    entries = g_new(GtkWidget *, num_valid_masks);
    for (gint i = 0; i < num_valid_masks; i++) {
        entries[i] = gtk_check_button_new_with_label(valid_masks[i].name);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(entries[i]), mask & valid_masks[i].value);
        gtk_container_add(GTK_CONTAINER(grid), entries[i]);
    }

    gtk_widget_show_all(grid);

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
 *                              GUI setup                             *
\**********************************************************************/
static void setup_gui (IaLogWindow *self)
{
    GtkWidget *grid, *scrolledwindow;
    GtkWidget *checkbutton, *button1, *button2;

    gtk_window_set_title(GTK_WINDOW(self), "libMirage log");
    gtk_window_set_default_size(GTK_WINDOW(self), 600, 400);
    gtk_container_set_border_width(GTK_CONTAINER(self), 5);

    /* Grid */
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
    gtk_container_add(GTK_CONTAINER(self), grid);

    /* Scrolled window */
    scrolledwindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_hexpand(scrolledwindow, TRUE);
    gtk_widget_set_vexpand(scrolledwindow, TRUE);
    gtk_grid_attach(GTK_GRID(grid), scrolledwindow, 0, 0, 3, 1);

    /* Text */
    self->priv->text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(self->priv->text_view), FALSE);
    gtk_container_add(GTK_CONTAINER(scrolledwindow), self->priv->text_view);

    /* Mirror to stdout checkbox */
    checkbutton = gtk_check_button_new_with_label("Mirror to stdout");
    gtk_grid_attach(GTK_GRID(grid), checkbutton, 0, 1, 1, 1);
    self->priv->checkbutton_stdout = checkbutton;

    /* Clear button */
    button1 = gtk_button_new_with_label("Clear");
    gtk_grid_attach_next_to(GTK_GRID(grid), button1, checkbutton, GTK_POS_RIGHT, 1, 1);
    g_signal_connect(button1, "clicked", G_CALLBACK(clear_button_clicked), self);

    /* Debug mask button */
    button2 = gtk_button_new_with_label("Debug mask");
    gtk_grid_attach_next_to(GTK_GRID(grid), button2, button1, GTK_POS_RIGHT, 1, 1);
    g_signal_connect(button2, "clicked", G_CALLBACK(debug_mask_button_clicked), self);

    gtk_widget_show_all(grid);
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE(IaLogWindow, ia_log_window, GTK_TYPE_WINDOW);

enum {
    PROP_0,
    PROP_MIRAGE_CONTEXT,
    N_PROPERTIES
};
static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

static void ia_log_window_init (IaLogWindow *self)
{
    self->priv = IA_LOG_WINDOW_GET_PRIVATE(self);

    self->priv->mirage_context = NULL;

    setup_gui(self);
}

static void ia_log_window_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    IaLogWindow *self = IA_LOG_WINDOW(object);

    switch (property_id) {
        case PROP_MIRAGE_CONTEXT: {
            self->priv->mirage_context = g_value_get_pointer(value);
            break;
        }
        default: {
            /* We don't have any other property... */
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
        }
    }
}

static void ia_log_window_class_init (IaLogWindowClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->set_property = ia_log_window_set_property;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(IaLogWindowPrivate));

    /* Register properties */
    obj_properties[PROP_MIRAGE_CONTEXT] = g_param_spec_pointer("mirage-context",
        "Mirage context pointer",
        "Set Mirage context pointer",
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE);

    g_object_class_install_properties(gobject_class, N_PROPERTIES, obj_properties);
}
