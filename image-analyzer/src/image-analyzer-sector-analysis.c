/*
 *  Image Analyzer: Sector analysis window
 *  Copyright (C) 2011-2012 Rok Mandeljc
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
#include "image-analyzer-sector-analysis.h"
#include "image-analyzer-sector-analysis-private.h"


/**********************************************************************\
 *                      Text buffer manipulation                      *
\**********************************************************************/
static void image_analyzer_sector_analysis_clear_text (IMAGE_ANALYZER_SectorAnalysis *self)
{
    gtk_text_buffer_set_text(self->priv->buffer, "", -1);
}

static void image_analyzer_sector_analysis_append_text (IMAGE_ANALYZER_SectorAnalysis *self, const gchar *tag_name, const gchar *format, ...)
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
}



/**********************************************************************\
 *                             UI callbacks                           *
\**********************************************************************/
static void image_analyzer_sector_analysis_ui_callback_analyze (GtkWidget *button G_GNUC_UNUSED, IMAGE_ANALYZER_SectorAnalysis *self)
{
    GObject *session, *track, *sector;
    gint num_sessions, num_tracks;
    gint session_number, session_start, session_length;
    gint track_number, track_start, track_length;
    gint i, j, address;

    /* Clear buffer */
    image_analyzer_sector_analysis_clear_text(self);

    /* Get image */
    if (!self->priv->disc) {
        image_analyzer_sector_analysis_append_text(self, NULL, "No image loaded!\n");
        return;
    }

    image_analyzer_sector_analysis_append_text(self, NULL, "Performing sector analysis...\n\n");

    /* Go over sessions */
    num_sessions = mirage_disc_get_number_of_sessions(MIRAGE_DISC(self->priv->disc));
    for (i = 0; i < num_sessions; i++) {
        /* Get session and its properties */
        session = mirage_disc_get_session_by_index(MIRAGE_DISC(self->priv->disc), i, NULL);
        session_number = mirage_session_layout_get_session_number(MIRAGE_SESSION(session));
        session_start = mirage_session_layout_get_start_sector(MIRAGE_SESSION(session));
        session_length = mirage_session_layout_get_length(MIRAGE_SESSION(session));
        num_tracks = mirage_session_get_number_of_tracks(MIRAGE_SESSION(session));

        image_analyzer_sector_analysis_append_text(self, "tag_section", "Session #%d: ", session_number);
        image_analyzer_sector_analysis_append_text(self, NULL, "start: %d, length %d, %d tracks\n\n", session_start, session_length, num_tracks);

        for (j = 0; j < num_tracks; j++) {
            /* Get track and its properties */
            track = mirage_session_get_track_by_index(MIRAGE_SESSION(session), j, NULL);
            track_number = mirage_track_layout_get_track_number(MIRAGE_TRACK(track));
            track_start = mirage_track_layout_get_start_sector(MIRAGE_TRACK(track));
            track_length = mirage_track_layout_get_length(MIRAGE_TRACK(track));

            image_analyzer_sector_analysis_append_text(self, "tag_section", "Track #%d: ", track_number);
            image_analyzer_sector_analysis_append_text(self, NULL, "start: %d, length %d\n\n", track_start, track_length);

            for (address = track_start; address < track_start + track_length; address++) {
                /* Get sector */
                sector = mirage_track_get_sector(MIRAGE_TRACK(track), address, TRUE, NULL);
                if (!sector) {
                    image_analyzer_sector_analysis_append_text(self, "tag_section", "Sector %d (%X): ", address, address);
                    image_analyzer_sector_analysis_append_text(self, NULL, "FAILED TO GET SECTOR!\n");
                    continue;
                }

                if (!mirage_sector_verify_lec(MIRAGE_SECTOR(sector))) {
                    image_analyzer_sector_analysis_append_text(self, "tag_section", "Sector %d (%X): ", address, address);
                    image_analyzer_sector_analysis_append_text(self, NULL, "L-EC error\n");
                }

                 if (!mirage_sector_verify_subchannel_crc(MIRAGE_SECTOR(sector))) {
                    image_analyzer_sector_analysis_append_text(self, "tag_section", "Sector %d (%X): ", address, address);
                    image_analyzer_sector_analysis_append_text(self, NULL, "Subchannel CRC error\n");
                }

                g_object_unref(sector);
            }

            image_analyzer_sector_analysis_append_text(self, NULL, "\n");

            g_object_unref(track);
        }

        g_object_unref(session);
    }

    image_analyzer_sector_analysis_append_text(self, NULL, "Sector analysis complete!\n");

    return;
}


/**********************************************************************\
 *                              GUI setup                             *
\**********************************************************************/
static void setup_gui (IMAGE_ANALYZER_SectorAnalysis *self)
{
    GtkWidget *vbox, *scrolledwindow, *hbox, *button;

    /* Window */
    gtk_window_set_title(GTK_WINDOW(self), "Sector analysis");
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
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwindow), GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);

    /* Text */
    self->priv->text_view = gtk_text_view_new();
    gtk_container_add(GTK_CONTAINER(scrolledwindow), self->priv->text_view);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(self->priv->text_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(self->priv->text_view), FALSE);


    self->priv->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->priv->text_view));
    gtk_text_buffer_create_tag(self->priv->buffer, "tag_section", "foreground", "#000000", "weight", PANGO_WEIGHT_BOLD, NULL);

    /* HBox */
#ifdef GTK3_ENABLED
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
#else
    hbox = gtk_hbox_new(FALSE, 5);
#endif
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    /* Button */
    button = gtk_button_new_with_label("Analyze");
    g_signal_connect(button, "clicked", G_CALLBACK(image_analyzer_sector_analysis_ui_callback_analyze), self);
    gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
}


/**********************************************************************\
 *                              Disc set                              *
\**********************************************************************/
void image_analyzer_sector_analysis_set_disc (IMAGE_ANALYZER_SectorAnalysis *self, GObject *disc)
{
    /* Release old disc */
    if (self->priv->disc) {
        g_object_unref(self->priv->disc);
    }

    /* Set new disc */
    self->priv->disc = disc;
    if (disc) {
        g_object_ref(disc);
    }
}


/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
G_DEFINE_TYPE(IMAGE_ANALYZER_SectorAnalysis, image_analyzer_sector_analysis, GTK_TYPE_WINDOW);

static void image_analyzer_sector_analysis_init (IMAGE_ANALYZER_SectorAnalysis *self)
{
    self->priv = IMAGE_ANALYZER_SECTOR_ANALYSIS_GET_PRIVATE(self);

    self->priv->disc = NULL;

    setup_gui(self);
}

static void image_analyzer_sector_analysis_class_init (IMAGE_ANALYZER_SectorAnalysisClass *klass)
{
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(IMAGE_ANALYZER_SectorAnalysisPrivate));
}
