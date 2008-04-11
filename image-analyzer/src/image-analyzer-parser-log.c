/*
 *  MIRAGE Image Analyzer: Parser log window
 *  Copyright (C) 2007-2008 Rok Mandeljc
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


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define IMAGE_ANALYZER_PARSER_LOG_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), IMAGE_ANALYZER_TYPE_PARSER_LOG, IMAGE_ANALYZER_ParserLogPrivate))

typedef struct {
    /* Text entry */
    GtkWidget *text_view;
} IMAGE_ANALYZER_ParserLogPrivate;


/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
gboolean image_analyzer_parser_log_clear_log (IMAGE_ANALYZER_ParserLog *self, GError **error) {
    IMAGE_ANALYZER_ParserLogPrivate *_priv = IMAGE_ANALYZER_PARSER_LOG_GET_PRIVATE(self);
    GtkTextBuffer *buffer = NULL;
    
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(_priv->text_view));
    
    gtk_text_buffer_set_text(buffer, "", -1);
        
    return TRUE;
}

gboolean image_analyzer_parser_log_append_to_log (IMAGE_ANALYZER_ParserLog *self, gchar *message, GError **error) {
    IMAGE_ANALYZER_ParserLogPrivate *_priv = IMAGE_ANALYZER_PARSER_LOG_GET_PRIVATE(self);
    GtkTextBuffer *buffer = NULL;
    GtkTextIter iter;
    
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(_priv->text_view));
    gtk_text_buffer_get_end_iter(buffer, &iter);
    
    if (message) {
        gtk_text_buffer_insert(buffer, &iter, message, -1);
    }
    
    return TRUE;
}

/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static GtkWindowClass *parent_class = NULL;

static void __image_analyzer_parser_log_instance_init (GTypeInstance *instance, gpointer g_class) {
    IMAGE_ANALYZER_ParserLog *self = IMAGE_ANALYZER_PARSER_LOG(instance);
    IMAGE_ANALYZER_ParserLogPrivate *_priv = IMAGE_ANALYZER_PARSER_LOG_GET_PRIVATE(self);
    
    GtkWidget *vbox = NULL;
    GtkWidget *scrolledwindow = NULL;
    
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
    _priv->text_view = gtk_text_view_new();
    gtk_text_view_set_editable(_priv->text_view, FALSE);
    gtk_container_add(GTK_CONTAINER(scrolledwindow), _priv->text_view);

    return;
}

static void __image_analyzer_parser_log_class_init (gpointer g_class, gpointer g_class_data) {
    IMAGE_ANALYZER_ParserLogClass *klass = IMAGE_ANALYZER_PARSER_LOG_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(IMAGE_ANALYZER_ParserLogPrivate));
        
    return;
}

GType image_analyzer_parser_log_get_type (void) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(IMAGE_ANALYZER_ParserLogClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __image_analyzer_parser_log_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(IMAGE_ANALYZER_ParserLog),
            0,      /* n_preallocs */
            __image_analyzer_parser_log_instance_init    /* instance_init */
        };
        
        type = g_type_register_static(GTK_TYPE_WINDOW, "IMAGE_ANALYZER_ParserLog", &info, 0);
    }
    
    return type;
}
