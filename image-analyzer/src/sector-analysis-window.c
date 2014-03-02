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

#include "sector-analysis-window.h"
#include "disc-tree-dump.h"


/**********************************************************************\
 *                            Private structure                       *
\**********************************************************************/
#define IA_SECTOR_ANALYSIS_WINDOW_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), IA_TYPE_SECTOR_ANALYSIS_WINDOW, IaSectorAnalysisWindowPrivate))

struct _IaSectorAnalysisWindowPrivate {
    /* Text entry */
    GtkWidget *text_view;
    GtkTextBuffer *buffer;

    /* Progress bar */
    GCancellable *cancellable;
    GtkWidget *progressbar;
    GtkWidget *cancel_button;

    GtkWidget *analyze_button;

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
    gint disc_start_sector, disc_length;
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

    /* Reset cancellable, hide analysis button and show progress bar &
       cancel button */
    g_cancellable_reset(self->priv->cancellable);
    gtk_widget_hide(self->priv->analyze_button);
    gtk_widget_show(self->priv->progressbar);
    gtk_widget_show(self->priv->cancel_button);

    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(self->priv->progressbar), "Analyzing sectors...");
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(self->priv->progressbar), 0);

    /* Disc start sector and disc layout length */
    disc_start_sector = mirage_disc_layout_get_start_sector(self->priv->disc);
    disc_length = mirage_disc_layout_get_length(self->priv->disc);

    /* Display message */
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
            ia_sector_analysis_window_append_text(self, NULL, "start: %d, length %d\n", track_start, track_length);

            for (gint address = track_start; address < track_start + track_length; address++) {
                /* Get sector */
                sector = mirage_track_get_sector(track, address, TRUE, NULL);
                if (sector) {
                    /* Verify L-EC */
                    if (!mirage_sector_verify_lec(sector)) {
                        ia_sector_analysis_window_append_text(self, "tag_section", "Sector %d (0x%X): ", address, address);
                        ia_sector_analysis_window_append_text(self, NULL, "L-EC error\n");
                    }
                    /* Verify subchannel CRC */
                    if (!mirage_sector_verify_subchannel_crc(sector)) {
                        ia_sector_analysis_window_append_text(self, "tag_section", "Sector %d (0x%X): ", address, address);
                        ia_sector_analysis_window_append_text(self, NULL, "Subchannel CRC error\n");
                    }

                    g_object_unref(sector);
                } else {
                    ia_sector_analysis_window_append_text(self, "tag_section", "Sector %d (0x%X): ", address, address);
                    ia_sector_analysis_window_append_text(self, NULL, "FAILED TO GET SECTOR!\n");
                }

                /* Update progess bar */
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(self->priv->progressbar), (float)(address - disc_start_sector)/(disc_length - disc_start_sector));

                /* Process events to keep GUI interactive */
                while (gtk_events_pending()) {
                    gtk_main_iteration();
                }

                /* Does user want to cancel operation? */
                if (g_cancellable_is_cancelled(self->priv->cancellable)) {
                    break;
                }
            }

            g_object_unref(track);

            /* Does user want to cancel operation? */
            if (g_cancellable_is_cancelled(self->priv->cancellable)) {
                break;
            }

            ia_sector_analysis_window_append_text(self, NULL, "\n");
        }

        g_object_unref(session);

        /* Does user want to cancel operation? */
        if (g_cancellable_is_cancelled(self->priv->cancellable)) {
            break;
        }
    }

    /* Finish; display message, hide progress bar & cancel button and
       show analyze button */
    if (g_cancellable_is_cancelled(self->priv->cancellable)) {
        ia_sector_analysis_window_append_text(self, NULL, "Sector analysis cancelled!\n");
    } else {
        ia_sector_analysis_window_append_text(self, NULL, "Sector analysis complete!\n");
    }

    gtk_widget_hide(self->priv->progressbar);
    gtk_widget_hide(self->priv->cancel_button);
    gtk_widget_show(self->priv->analyze_button);
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
    gtk_grid_attach(GTK_GRID(grid), scrolledwindow, 0, 0, 2, 1);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwindow), GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);

    /* Text */
    self->priv->text_view = gtk_text_view_new();
    gtk_container_add(GTK_CONTAINER(scrolledwindow), self->priv->text_view);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(self->priv->text_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(self->priv->text_view), FALSE);

    self->priv->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->priv->text_view));
    gtk_text_buffer_create_tag(self->priv->buffer, "tag_section", /*"foreground", "#000000",*/ "weight", PANGO_WEIGHT_BOLD, NULL);

    /* Analyze button */
    button = gtk_button_new_with_label("Analyze");
    g_signal_connect(button, "clicked", G_CALLBACK(ia_sector_analysis_window_ui_callback_analyze), self);
    gtk_grid_attach(GTK_GRID(grid), button, 0, 1, 2, 1);
    self->priv->analyze_button = button;

    /* Progress bar */
    self->priv->progressbar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(self->priv->progressbar), TRUE);
    gtk_widget_set_hexpand(self->priv->progressbar, TRUE);
    gtk_grid_attach(GTK_GRID(grid), self->priv->progressbar, 0, 2, 1, 1);

    /* Cancel button */
    button = gtk_button_new_with_label("Cancel");
    g_signal_connect_swapped(button, "clicked", G_CALLBACK(g_cancellable_cancel), self->priv->cancellable);
    gtk_grid_attach_next_to(GTK_GRID(grid), button, self->priv->progressbar, GTK_POS_RIGHT, 1, 1);
    self->priv->cancel_button = button;

    gtk_widget_show_all(grid);
    gtk_widget_hide(self->priv->progressbar);
    gtk_widget_hide(self->priv->cancel_button);
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

static void ia_sector_analysis_window_dispose (GObject *gobject)
{
    IaSectorAnalysisWindow *self = IA_SECTOR_ANALYSIS_WINDOW(gobject);

    if (self->priv->cancellable) {
        g_object_unref(self->priv->cancellable);
        self->priv->cancellable = NULL;
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(ia_sector_analysis_window_parent_class)->dispose(gobject);
}

static void ia_sector_analysis_window_init (IaSectorAnalysisWindow *self)
{
    self->priv = IA_SECTOR_ANALYSIS_WINDOW_GET_PRIVATE(self);

    self->priv->disc = NULL;
    self->priv->cancellable = g_cancellable_new();

    setup_gui(self);
}

static void ia_sector_analysis_window_class_init (IaSectorAnalysisWindowClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = ia_sector_analysis_window_dispose;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(IaSectorAnalysisWindowPrivate));
}
