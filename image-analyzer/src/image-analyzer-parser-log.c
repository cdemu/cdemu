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

#include "image-analyzer-parser-log.h"
#include "image-analyzer-parser-log-private.h"


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
void image_analyzer_parser_log_clear_log (IMAGE_ANALYZER_ParserLog *self)
{
    GtkTextBuffer *buffer;
    
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->priv->text_view));
    gtk_text_buffer_set_text(buffer, "", -1);
}

void image_analyzer_parser_log_append_to_log (IMAGE_ANALYZER_ParserLog *self, gchar *message)
{
    GtkTextBuffer *buffer;
    GtkTextIter iter;
    
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->priv->text_view));
    gtk_text_buffer_get_end_iter(buffer, &iter);
    
    if (message) {
        gtk_text_buffer_insert(buffer, &iter, message, -1);
    }
}


/**********************************************************************\
 *                              GUI setup                             * 
\**********************************************************************/
static void setup_gui (IMAGE_ANALYZER_ParserLog *self)
{
    GtkWidget *vbox, *scrolledwindow;
    
    gtk_window_set_title(GTK_WINDOW(self), "Parser log");
    gtk_window_set_default_size(GTK_WINDOW(self), 600, 400);
    gtk_container_set_border_width(GTK_CONTAINER(self), 5);

    /* VBox */
    vbox = gtk_vbox_new(FALSE, 5);
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
G_DEFINE_TYPE(IMAGE_ANALYZER_ParserLog, image_analyzer_parser_log, GTK_TYPE_WINDOW);

static void image_analyzer_parser_log_init (IMAGE_ANALYZER_ParserLog *self)
{
    self->priv = IMAGE_ANALYZER_PARSER_LOG_GET_PRIVATE(self);

    setup_gui(self);
}

static void image_analyzer_parser_log_class_init (IMAGE_ANALYZER_ParserLogClass *klass)
{
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(IMAGE_ANALYZER_ParserLogPrivate));
}
