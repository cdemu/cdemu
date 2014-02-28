/*
 *  Image Analyzer: Sector analysis window
 *  Copyright (C) 2011-2014 Rok Mandeljc
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

#include "dump.h"
#include "sector-analysis-window.h"


/**********************************************************************\
 *                            Private structure                       *
\**********************************************************************/
#define IA_SECTOR_ANALYSIS_WINDOW_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), IA_TYPE_SECTOR_ANALYSIS_WINDOW, IaSectorAnalysisWindowPrivate))

struct _IaSectorAnalysisWindowPrivate {
    /* Text entry */
    GtkWidget *text_view;
    GtkTextBuffer *buffer;

    /* Disc */
    MirageDisc *disc;
};


/**********************************************************************\
 *                      Text buffer manipulation                      *
\**********************************************************************/
static void ia_sector_analysis_window_clear_text (IaSectorAnalysisWindow *self)
{
    gtk_text_buffer_set_text(self->priv->buffer, "", -1);
}

static void ia_sector_analysis_window_append_text (IaSectorAnalysisWindow *self, const gchar *tag_name, const gchar *format, ...)
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
static void ia_sector_analysis_window_ui_callback_analyze (GtkWidget *button G_GNUC_UNUSED, IaSectorAnalysisWindow *self)
{
    MirageSession *session;
    MirageTrack *track;
    MirageSector *sector;
    gint num_sessions, num_tracks;
    gint session_number, session_start, session_length;
    gint track_number, track_start, track_length;

    /* Clear buffer */
    ia_sector_analysis_window_clear_text(self);

    /* Get image */
    if (!self->priv->disc) {
        ia_sector_analysis_window_append_text(self, NULL, "No image loaded!\n");
        return;
    }

    ia_sector_analysis_window_append_text(self, NULL, "Performing sector analysis...\n\n");

    /* Go over sessions */
    num_sessions = mirage_disc_get_number_of_sessions(self->priv->disc);
    for (gint i = 0; i < num_sessions; i++) {
        /* Get session and its properties */
        session = mirage_disc_get_session_by_index(self->priv->disc, i, NULL);
        session_number = mirage_session_layout_get_session_number(session);
        session_start = mirage_session_layout_get_start_sector(session);
        session_length = mirage_session_layout_get_length(session);
        num_tracks = mirage_session_get_number_of_tracks(session);

        ia_sector_analysis_window_append_text(self, "tag_section", "Session #%d: ", session_number);
        ia_sector_analysis_window_append_text(self, NULL, "start: %d, length %d, %d tracks\n\n", session_start, session_length, num_tracks);

        for (gint j = 0; j < num_tracks; j++) {
            /* Get track and its properties */
            track = mirage_session_get_track_by_index(session, j, NULL);
            track_number = mirage_track_layout_get_track_number(track);
            track_start = mirage_track_layout_get_start_sector(track);
            track_length = mirage_track_layout_get_length(track);

            ia_sector_analysis_window_append_text(self, "tag_section", "Track #%d: ", track_number);
            ia_sector_analysis_window_append_text(self, NULL, "start: %d, length %d\n\n", track_start, track_length);

            for (gint address = track_start; address < track_start + track_length; address++) {
                /* Get sector */
                sector = mirage_track_get_sector(track, address, TRUE, NULL);
                if (!sector) {
                    ia_sector_analysis_window_append_text(self, "tag_section", "Sector %d (0x%X): ", address, address);
                    ia_sector_analysis_window_append_text(self, NULL, "FAILED TO GET SECTOR!\n");
                    continue;
                }

                if (!mirage_sector_verify_lec(sector)) {
                    ia_sector_analysis_window_append_text(self, "tag_section", "Sector %d (0x%X): ", address, address);
                    ia_sector_analysis_window_append_text(self, NULL, "L-EC error\n");
                }

                 if (!mirage_sector_verify_subchannel_crc(sector)) {
                    ia_sector_analysis_window_append_text(self, "tag_section", "Sector %d (0x%X): ", address, address);
                    ia_sector_analysis_window_append_text(self, NULL, "Subchannel CRC error\n");
                }

                g_object_unref(sector);
            }

            ia_sector_analysis_window_append_text(self, NULL, "\n");

            g_object_unref(track);
        }

        g_object_unref(session);
    }

    ia_sector_analysis_window_append_text(self, NULL, "Sector analysis complete!\n");

    return;
}


/**********************************************************************\
 *                              GUI setup                             *
\**********************************************************************/
static void setup_gui (IaSectorAnalysisWindow *self)
{
    GtkWidget *grid, *scrolledwindow, *button;

    /* Window */
    gtk_window_set_title(GTK_WINDOW(self), "Sector analysis");
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
    gtk_grid_attach(GTK_GRID(grid), scrolledwindow, 0, 0, 1, 1);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwindow), GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);

    /* Text */
    self->priv->text_view = gtk_text_view_new();
    gtk_container_add(GTK_CONTAINER(scrolledwindow), self->priv->text_view);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(self->priv->text_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(self->priv->text_view), FALSE);

    self->priv->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->priv->text_view));
    gtk_text_buffer_create_tag(self->priv->buffer, "tag_section", /*"foreground", "#000000",*/ "weight", PANGO_WEIGHT_BOLD, NULL);

    /* Button */
    button = gtk_button_new_with_label("Analyze");
    g_signal_connect(button, "clicked", G_CALLBACK(ia_sector_analysis_window_ui_callback_analyze), self);
    gtk_grid_attach(GTK_GRID(grid), button, 0, 1, 1, 1);
}


/**********************************************************************\
 *                              Disc set                              *
\**********************************************************************/
void ia_sector_analysis_window_set_disc (IaSectorAnalysisWindow *self, MirageDisc *disc)
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


/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
G_DEFINE_TYPE(IaSectorAnalysisWindow, ia_sector_analysis_window, GTK_TYPE_WINDOW);

static void ia_sector_analysis_window_init (IaSectorAnalysisWindow *self)
{
    self->priv = IA_SECTOR_ANALYSIS_WINDOW_GET_PRIVATE(self);

    self->priv->disc = NULL;

    setup_gui(self);
}

static void ia_sector_analysis_window_class_init (IaSectorAnalysisWindowClass *klass)
{
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(IaSectorAnalysisWindowPrivate));
}
