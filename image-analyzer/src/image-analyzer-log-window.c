/*
 *  Image Analyzer: Parser log window
 *  Copyright (C) 2007-2012 Rok Mandeljc
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

#include <glib.h>
#include <gtk/gtk.h>

#include "image-analyzer-log-window.h"
#include "image-analyzer-log-window-private.h"


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
void image_analyzer_log_window_clear_log (IMAGE_ANALYZER_LogWindow *self)
{
    GtkTextBuffer *buffer;

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->priv->text_view));
    gtk_text_buffer_set_text(buffer, "", -1);
}

void image_analyzer_log_window_append_to_log (IMAGE_ANALYZER_LogWindow *self, const gchar *message)
{
    if (message) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->priv->text_view));
        GtkTextIter iter;

        gtk_text_buffer_get_end_iter(buffer, &iter);
        gtk_text_buffer_insert(buffer, &iter, message, -1);
    }
}

gchar *image_analyzer_log_window_get_log_text (IMAGE_ANALYZER_LogWindow *self)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->priv->text_view));
    GtkTextIter start, end;

    gtk_text_buffer_get_bounds(buffer, &start, &end);

    return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}


/**********************************************************************\
 *                              GUI setup                             *
\**********************************************************************/
static void setup_gui (IMAGE_ANALYZER_LogWindow *self)
{
    GtkWidget *vbox, *scrolledwindow;

    gtk_window_set_title(GTK_WINDOW(self), "libMirage log");
    gtk_window_set_default_size(GTK_WINDOW(self), 600, 400);
    gtk_container_set_border_width(GTK_CONTAINER(self), 5);

    /* VBox */
#ifdef GTK3_ENABLED
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
#else
    vbox = gtk_vbox_new(FALSE, 5);
#endif
    gtk_container_add(GTK_CONTAINER(self), vbox);

    /* Scrolled window */
    scrolledwindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(vbox), scrolledwindow, TRUE, TRUE, 0);

    /* Text */
    self->priv->text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(self->priv->text_view), FALSE);
    gtk_container_add(GTK_CONTAINER(scrolledwindow), self->priv->text_view);

    return;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE(IMAGE_ANALYZER_LogWindow, image_analyzer_log_window, GTK_TYPE_WINDOW);

static void image_analyzer_log_window_init (IMAGE_ANALYZER_LogWindow *self)
{
    self->priv = IMAGE_ANALYZER_LOG_WINDOW_GET_PRIVATE(self);

    setup_gui(self);
}

static void image_analyzer_log_window_class_init (IMAGE_ANALYZER_LogWindowClass *klass)
{
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(IMAGE_ANALYZER_LogWindowPrivate));
}
