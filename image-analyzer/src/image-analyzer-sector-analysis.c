/*
 *  MIRAGE Image Analyzer: Sector analysis window
 *  Copyright (C) 2011 Rok Mandeljc
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
#include <mirage.h>
#include "image-analyzer-dump.h"
#include "image-analyzer-application.h"
#include "image-analyzer-sector-analysis.h"


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define IMAGE_ANALYZER_SECTOR_ANALYSIS_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), IMAGE_ANALYZER_TYPE_SECTOR_ANALYSIS, IMAGE_ANALYZER_SectorAnalysisPrivate))

typedef struct {
    /* Application */
    GObject *application;

    /* Text entry */
    GtkWidget *text_view;
    GtkTextBuffer *buffer;
} IMAGE_ANALYZER_SectorAnalysisPrivate;

typedef enum {
    PROPERTY_APPLICATION = 1,
} IMAGE_ANALYZER_SectorAnalysisProperties;


/******************************************************************************\
 *                          Text buffer manipulation                          *
\******************************************************************************/
static gboolean __image_analyzer_sector_analysis_clear_text (IMAGE_ANALYZER_SectorAnalysis *self) {
    IMAGE_ANALYZER_SectorAnalysisPrivate *_priv = IMAGE_ANALYZER_SECTOR_ANALYSIS_GET_PRIVATE(self);
    gtk_text_buffer_set_text(_priv->buffer, "", -1);
    return TRUE;
}

static gboolean __image_analyzer_sector_analysis_append_text (IMAGE_ANALYZER_SectorAnalysis *self, const gchar *tag_name, const gchar *format, ...) {
    IMAGE_ANALYZER_SectorAnalysisPrivate *_priv = IMAGE_ANALYZER_SECTOR_ANALYSIS_GET_PRIVATE(self);
    GtkTextIter iter;
    gchar *string;
    va_list args;

    gtk_text_buffer_get_end_iter(_priv->buffer, &iter);

    va_start(args, format);
    string = g_strdup_vprintf(format, args);
    va_end(args);

    if (tag_name) {
        gtk_text_buffer_insert_with_tags_by_name(_priv->buffer, &iter, string, -1, tag_name, NULL);
    } else {
        gtk_text_buffer_insert(_priv->buffer, &iter, string, -1);
    }

    g_free(string);

    /* Force refresh */
    gtk_widget_draw(GTK_WIDGET(_priv->text_view), NULL);

    return TRUE;
}



/******************************************************************************\
 *                                 UI callbacks                               *
\******************************************************************************/
static void __image_analyzer_sector_analysis_ui_callback_analyze (GtkWidget *button, gpointer user_data) {
    IMAGE_ANALYZER_SectorAnalysis *self = IMAGE_ANALYZER_SECTOR_ANALYSIS(user_data);
    IMAGE_ANALYZER_SectorAnalysisPrivate *_priv = IMAGE_ANALYZER_SECTOR_ANALYSIS_GET_PRIVATE(self);
    GError *error = NULL;
    GObject *disc, *session, *track, *sector;
    gint num_sessions, num_tracks;
    gint session_number, session_start, session_length;
    gint track_number, track_start, track_length, track_pregap;
    gint i, j, address;

    /* Clear buffer */
    __image_analyzer_sector_analysis_clear_text(self);

    /* Get image */
    if (!image_analyzer_application_get_loaded_image(IMAGE_ANALYZER_APPLICATION(_priv->application), &disc)) {
        __image_analyzer_sector_analysis_append_text(self, NULL, "No image loaded!\n");
        return;
    }

    __image_analyzer_sector_analysis_append_text(self, NULL, "Performing sector analysis...\n\n");

    /* Go over sessions */
    mirage_disc_get_number_of_sessions(MIRAGE_DISC(disc), &num_sessions, NULL);
    for (i = 0; i < num_sessions; i++) {
        /* Get session and its properties */
        mirage_disc_get_session_by_index(MIRAGE_DISC(disc), i, &session, NULL);
        mirage_session_layout_get_session_number(MIRAGE_SESSION(session), &session_number, NULL);
        mirage_session_layout_get_start_sector(MIRAGE_SESSION(session), &session_start, NULL);
        mirage_session_layout_get_length(MIRAGE_SESSION(session), &session_length, NULL);
        mirage_session_get_number_of_tracks(MIRAGE_SESSION(session), &num_tracks, NULL);

        __image_analyzer_sector_analysis_append_text(self, "tag_section", "Session #%d: ", session_number);
        __image_analyzer_sector_analysis_append_text(self, NULL, "start: %d, length %d, %d tracks\n\n", session_start, session_length, num_tracks);

        for (j = 0; j < num_tracks; j++) {
            /* Get track and its properties */
            mirage_session_get_track_by_index(MIRAGE_SESSION(session), j, &track, NULL);
            mirage_track_layout_get_track_number(MIRAGE_TRACK(track), &track_number, NULL);
            mirage_track_layout_get_start_sector(MIRAGE_TRACK(track), &track_start, NULL);
            mirage_track_layout_get_length(MIRAGE_TRACK(track), &track_length, NULL);

            __image_analyzer_sector_analysis_append_text(self, "tag_section", "Track #%d: ", track_number);
            __image_analyzer_sector_analysis_append_text(self, NULL, "start: %d, length %d\n\n", track_start, track_length);

            for (address = track_start; address < track_start + track_length; address++) {
                /* Get sector */
                mirage_track_get_sector(MIRAGE_TRACK(track), address, TRUE, &sector, NULL);

                if (!mirage_sector_verify_lec(MIRAGE_SECTOR(sector))) {
                    __image_analyzer_sector_analysis_append_text(self, "tag_section", "Sector %d (%X): ", address, address);
                    __image_analyzer_sector_analysis_append_text(self, NULL, "L-EC error\n");
                }

                 if (!mirage_sector_verify_subchannel_crc(MIRAGE_SECTOR(sector))) {
                    __image_analyzer_sector_analysis_append_text(self, "tag_section", "Sector %d (%X): ", address, address);
                    __image_analyzer_sector_analysis_append_text(self, NULL, "Subchannel CRC error\n");
                }

                g_object_unref(sector);
            }

            __image_analyzer_sector_analysis_append_text(self, NULL, "\n");

            g_object_unref(track);
        }

        g_object_unref(session);
    }


    __image_analyzer_sector_analysis_append_text(self, NULL, "Sector analysis complete!\n");

    g_object_unref(disc);

    return;
}


/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static GtkWindowClass *parent_class = NULL;

static void __image_analyzer_sector_analysis_get_property (GObject *obj, guint param_id, GValue *value, GParamSpec *pspec) {
    IMAGE_ANALYZER_SectorAnalysis *self = IMAGE_ANALYZER_SECTOR_ANALYSIS(obj);
    IMAGE_ANALYZER_SectorAnalysisPrivate *_priv = IMAGE_ANALYZER_SECTOR_ANALYSIS_GET_PRIVATE(self);

    switch (param_id) {
        case PROPERTY_APPLICATION: {
            g_value_set_object(value, _priv->application);
            break;
        }
        default: {
            G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, param_id, pspec);
            break;
        }
    }

    return;
}

static void __image_analyzer_sector_analysis_set_property (GObject *obj, guint param_id, const GValue *value, GParamSpec *pspec) {
    IMAGE_ANALYZER_SectorAnalysis *self = IMAGE_ANALYZER_SECTOR_ANALYSIS(obj);
    IMAGE_ANALYZER_SectorAnalysisPrivate *_priv = IMAGE_ANALYZER_SECTOR_ANALYSIS_GET_PRIVATE(self);

    switch (param_id) {
        case PROPERTY_APPLICATION: {
            _priv->application = g_value_get_object(value);
            break;
        }
        default: {
            G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, param_id, pspec);
            break;
        }
    }

    return;
}

static void __image_analyzer_sector_analysis_instance_init (GTypeInstance *instance, gpointer g_class) {
    IMAGE_ANALYZER_SectorAnalysis *self = IMAGE_ANALYZER_SECTOR_ANALYSIS(instance);
    IMAGE_ANALYZER_SectorAnalysisPrivate *_priv = IMAGE_ANALYZER_SECTOR_ANALYSIS_GET_PRIVATE(self);

    GtkWidget *vbox, *scrolledwindow, *hbox, *button;

    gtk_window_set_title(GTK_WINDOW(self), "Sector analysis");
    gtk_window_set_default_size(GTK_WINDOW(self), 600, 400);
    gtk_container_set_border_width(GTK_CONTAINER(self), 5);

    /* VBox */
    vbox = gtk_vbox_new(FALSE, 5);
    gtk_container_add(GTK_CONTAINER(self), vbox);

    /* Scrolled window */
    scrolledwindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(vbox), scrolledwindow, TRUE, TRUE, 0);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwindow), GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);

    /* Text */
    _priv->text_view = gtk_text_view_new();
    gtk_container_add(GTK_CONTAINER(scrolledwindow), _priv->text_view);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(_priv->text_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(_priv->text_view), FALSE);


    _priv->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(_priv->text_view));
    gtk_text_buffer_create_tag(_priv->buffer, "tag_section", "foreground", "#000000", "weight", PANGO_WEIGHT_BOLD, NULL);

    /* HBox */
    hbox = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    /* Button */
    button = gtk_button_new_with_label("Analyze");
    g_signal_connect(button, "clicked", G_CALLBACK(__image_analyzer_sector_analysis_ui_callback_analyze), self);
    gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);

    return;
}

static void __image_analyzer_sector_analysis_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    IMAGE_ANALYZER_SectorAnalysisClass *klass = IMAGE_ANALYZER_SECTOR_ANALYSIS_CLASS(g_class);

    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(IMAGE_ANALYZER_SectorAnalysisPrivate));

    /* Initialize GObject methods */
    class_gobject->get_property = __image_analyzer_sector_analysis_get_property;
    class_gobject->set_property = __image_analyzer_sector_analysis_set_property;

    /* Install properties */
    g_object_class_install_property(class_gobject, PROPERTY_APPLICATION, g_param_spec_object("application", "Application", "Parent application", IMAGE_ANALYZER_TYPE_APPLICATION, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    return;
}

GType image_analyzer_sector_analysis_get_type (void) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(IMAGE_ANALYZER_SectorAnalysisClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __image_analyzer_sector_analysis_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(IMAGE_ANALYZER_SectorAnalysis),
            0,      /* n_preallocs */
            __image_analyzer_sector_analysis_instance_init    /* instance_init */
        };

        type = g_type_register_static(GTK_TYPE_WINDOW, "IMAGE_ANALYZER_SectorAnalysis", &info, 0);
    }

    return type;
}
