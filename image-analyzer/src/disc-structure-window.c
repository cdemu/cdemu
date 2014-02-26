/*
 *  Image Analyzer: Disc structure window
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

#include "disc-structure-window.h"
#include "disc-structure-window-private.h"


/**********************************************************************\
 *                      Text buffer manipulation                      *
\**********************************************************************/
static gboolean ia_disc_structure_window_clear_text (IaDiscStructure *self)
{
    gtk_text_buffer_set_text(self->priv->buffer, "", -1);
    return TRUE;
}

static gboolean ia_disc_structure_window_append_text (IaDiscStructure *self, const gchar *tag_name, const gchar *format, ...)
{
    GtkTextIter iter;
    gchar *string;
    va_list args;

    gtk_text_buffer_get_end_iter(self->priv->buffer, &iter);

    va_start(args, format);
    string = g_strdup_vprintf(format, args);
    va_end(args);

    if (tag_name) {
        gtk_text_buffer_insert_with_tags_by_name(self->priv->buffer, &iter, string, -1, tag_name, NULL);
    } else {
        gtk_text_buffer_insert(self->priv->buffer, &iter, string, -1);
    }

    g_free(string);

    return TRUE;
}


/**********************************************************************\
 *                             UI callbacks                           *
\**********************************************************************/
static void ia_disc_structure_window_ui_callback_get_structure (GtkWidget *button G_GNUC_UNUSED, IaDiscStructure *self)
{
    GError *error = NULL;
    gboolean succeeded;
    gint layer, type;

    const guint8 *tmp_buf;
    gint tmp_len;

    /* Read address from spin button */
    type = gtk_spin_button_get_value(GTK_SPIN_BUTTON(self->priv->spinbutton_type));
    layer = gtk_spin_button_get_value(GTK_SPIN_BUTTON(self->priv->spinbutton_layer));

    /* Clear buffer */
    ia_disc_structure_window_clear_text(self);

    /* Get image */
    if (!self->priv->disc) {
        ia_disc_structure_window_append_text(self, NULL, "No image loaded!\n");
        return;
    }

    /* Get structure from disc */
    succeeded = mirage_disc_get_disc_structure(self->priv->disc, layer, type, &tmp_buf, &tmp_len, &error);
    if (!succeeded) {
        ia_disc_structure_window_append_text(self, NULL, "Failed to get structure: %s\n", error->message);
        g_error_free(error);
        return;
    }

    /* Dump structure */
    for (gint i = 0; i < tmp_len; i++) {
        ia_disc_structure_window_append_text(self, NULL, "%02hhX ", tmp_buf[i]);
    }
}


/**********************************************************************\
 *                              GUI setup                             *
\**********************************************************************/
static void setup_gui (IaDiscStructure *self)
{
    GtkWidget *vbox, *scrolledwindow, *hbox, *button, *label;
    GtkAdjustment *adjustment;

    /* Window */
    gtk_window_set_title(GTK_WINDOW(self), "Disc structure");
    gtk_window_set_default_size(GTK_WINDOW(self), 600, 400);
    gtk_container_set_border_width(GTK_CONTAINER(self), 5);

    /* VBox */
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(self), vbox);

    /* Scrolled window */
    scrolledwindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(vbox), scrolledwindow, TRUE, TRUE, 0);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwindow), GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);

    /* Text */
    self->priv->text_view = gtk_text_view_new();
    gtk_container_add(GTK_CONTAINER(scrolledwindow), self->priv->text_view);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(self->priv->text_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(self->priv->text_view), FALSE);

    self->priv->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->priv->text_view));

    /* HBox */
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    /* Label: Layer */
    label = gtk_label_new("Layer: ");
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

    /* Spin button: Layer */
    adjustment = gtk_adjustment_new(0, 0, 1, 1, 75, 0);
    self->priv->spinbutton_layer = gtk_spin_button_new(GTK_ADJUSTMENT(adjustment), 1, 0);
    gtk_box_pack_start(GTK_BOX(hbox), self->priv->spinbutton_layer, TRUE, TRUE, 0);

    /* Label: Type */
    label = gtk_label_new("Type: ");
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

    /* Spin button: Type */
    adjustment = gtk_adjustment_new(0, G_MININT64, G_MAXINT64, 1, 75, 0);
    self->priv->spinbutton_type = gtk_spin_button_new(GTK_ADJUSTMENT(adjustment), 1, 0);
    gtk_box_pack_start(GTK_BOX(hbox), self->priv->spinbutton_type, TRUE, TRUE, 0);

    /* Button */
    button = gtk_button_new_with_label("Get structure");
    g_signal_connect(button, "clicked", G_CALLBACK(ia_disc_structure_window_ui_callback_get_structure), self);
    gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
}


/**********************************************************************\
 *                              Disc set                              *
\**********************************************************************/
void ia_disc_structure_window_set_disc (IaDiscStructure *self, MirageDisc *disc)
{
    /* Release old disc */
    if (self->priv->disc) {
        g_object_unref(self->priv->disc);
        self->priv->disc = NULL;
    }

    /* Set new disc */
    if (disc) {
        self->priv->disc = g_object_ref(disc);
    }
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE(IaDiscStructure, ia_disc_structure_window, GTK_TYPE_WINDOW);

static void ia_disc_structure_window_init (IaDiscStructure *self)
{
    self->priv = IA_DISC_STRUCTURE_WINDOW_GET_PRIVATE(self);

    self->priv->disc = NULL;

    setup_gui(self);
}

static void ia_disc_structure_window_dispose (GObject *gobject)
{
    IaDiscStructure *self = IA_DISC_STRUCTURE_WINDOW(gobject);

    /* Unref disc */
    if (self->priv->disc) {
        g_object_unref(self->priv->disc);
        self->priv->disc = NULL;
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(ia_disc_structure_window_parent_class)->dispose(gobject);
}

static void ia_disc_structure_window_class_init (IaDiscStructureClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = ia_disc_structure_window_dispose;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(IaDiscStructurePrivate));
}
