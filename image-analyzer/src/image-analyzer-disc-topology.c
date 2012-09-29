/*
 *  Image Analyzer: Disc topology window
 *  Copyright (C) 2008-2012 Rok Mandeljc
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

#include <math.h>
#include <glib.h>
#include <gtk/gtk.h>
#ifdef GTK3_ENABLED
#include <gtk/gtkx.h>
#endif
#include <mirage.h>

#include "image-analyzer-disc-topology.h"
#include "image-analyzer-disc-topology-private.h"


/**********************************************************************\
 *                               Helpers                              *
\**********************************************************************/
static gboolean image_analyzer_disc_topology_run_gnuplot (ImageAnalyzerDiscTopology *self, GError **error)
{
    gchar *argv[] = { "gnuplot", NULL };
    gboolean ret;
    ssize_t cmdlen, written;
    gchar *cmd;

    /* Spawn gnuplot */
    ret = g_spawn_async_with_pipes(
            NULL, /* const gchar *working_directory, (NULL = inhearit parent) */
            argv, /* gchar **argv */
            NULL, /* gchar **envp */
            G_SPAWN_SEARCH_PATH, /* GSpawnFlags flags */
            NULL, /* GSpawnChildSetupFunc child_setup */
            NULL, /* gpointer user_data */
            &self->priv->pid, /* GPid *child_pid */
            &self->priv->fd_in, /* gint *standard_input */
            NULL, /* gint *standard_output */
            NULL, /* gint *standard_error */
            error /* GError **error */
        );
    if (!ret) {
        g_debug("Failed to spawn gnuplot process!\n");
        self->priv->gnuplot_works = FALSE;
        return FALSE;
    } else {
        self->priv->gnuplot_works = TRUE;
    }

    /* Redirect to socket */
    gtk_widget_show_all(GTK_WIDGET(self));

    cmd = g_strdup_printf("set term x11 window '%lX' ctrlq\n", gtk_socket_get_id(GTK_SOCKET(self->priv->socket)));
    cmdlen = strlen(cmd);

    written = write(self->priv->fd_in, cmd, cmdlen);
    g_free(cmd);
    if (written != cmdlen) {
        return FALSE;
    }

    gtk_widget_hide(GTK_WIDGET(self));

    return TRUE;
}

static gboolean image_analyzer_disc_topology_refresh (ImageAnalyzerDiscTopology *self, GObject *disc)
{
    gboolean dpm_valid = FALSE;
    gint dpm_start, dpm_entries, dpm_resolution;
    ssize_t cmdlen, written;
    gchar *cmd;

    /* No-op if gnuplot couldn't be started */
    if (!self->priv->gnuplot_works) {
        return TRUE;
    }

    if (!disc) {
        /* No disc */
        cmd = g_strdup_printf(
            "clear; reset; "
            "unset xtics; unset ytics; "
            "unset border; unset key; "
            "set title 'No disc loaded'; "
            "plot [][0:1] 2 notitle; "
            "reset"
        );
    } else {
        const gchar **filenames = mirage_disc_get_filenames(MIRAGE_DISC(disc));
        gchar *basename = g_path_get_basename(filenames[0]);

        mirage_disc_get_dpm_data(MIRAGE_DISC(disc), &dpm_start, &dpm_entries, &dpm_resolution, NULL);

        if (!dpm_entries) {
            /* No DPM data */
            cmd = g_strdup_printf(
                "clear; reset; "
                "unset xtics; unset ytics; "
                "unset border; unset key; "
                "set title '%s%s: no topology data'; "
                "plot [][0:1] 2 notitle; ",
                basename,
                filenames[1] ? "..." : ""
            );
        } else {
            /* Plot with DPM data fed via stdin */
            cmd = g_strdup_printf(
                "clear; reset; "
                "set title '%s%s: disc topology'; "
                "set xlabel 'Sector address'; "
                "set ylabel 'Sector density [degrees/sector]'; "
                "set grid; "
                "plot '-' notitle with lines; \n",
                basename,
                filenames[1] ? "..." : ""
            );
            dpm_valid = TRUE;
        }

        g_free(basename);
    }
    cmdlen = strlen(cmd);

    /* Write plot command */
    written = write(self->priv->fd_in, cmd, cmdlen);
    g_free(cmd);
    if (written != cmdlen) {
        return FALSE;
    }

    /* Feed DPM data */
    if (dpm_valid) {
        gint address;
        gdouble density;

        gchar dbl_buffer[G_ASCII_DTOSTR_BUF_SIZE] = "";

        for (gint i = 0; i < dpm_entries; i++) {
            address = dpm_start + i*dpm_resolution;
            density = 0;

            if (!mirage_disc_get_dpm_data_for_sector(MIRAGE_DISC(disc), address, NULL, &density, NULL)) {
                /*g_debug("%s: failed to get DPM data for address 0x%X\n", __func__, address);*/
                continue;
            }

            /* NOTE: we convert double to string using g_ascii_dtostr, because
               %g and %f are locale-dependent */
            cmd = g_strdup_printf("%d %s\n", address, g_ascii_dtostr(dbl_buffer, G_ASCII_DTOSTR_BUF_SIZE, density));
            cmdlen = strlen(cmd);

            written = write(self->priv->fd_in, cmd, cmdlen);
            g_free(cmd);
            if (written != cmdlen) {
                return FALSE;
            }
        }

        /* Write EOF */
        written = write(self->priv->fd_in, "e\n", 2);
        if (written != 2) {
            return FALSE;
        }
    }

    return TRUE;
}


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
void image_analyzer_disc_topology_set_disc (ImageAnalyzerDiscTopology *self, GObject *disc)
{
    /* Just refresh; we don't need disc reference */
    image_analyzer_disc_topology_refresh(self, disc);
}


/**********************************************************************\
 *                              GUI setup                             *
\**********************************************************************/
static void setup_gui (ImageAnalyzerDiscTopology *self)
{
    /* Window */
    gtk_window_set_title(GTK_WINDOW(self), "Disc topology");
    gtk_window_set_default_size(GTK_WINDOW(self), 800, 600);
    gtk_container_set_border_width(GTK_CONTAINER(self), 5);

    /* Create socket for embedding gnuplot window */
    self->priv->socket = gtk_socket_new();
    gtk_container_add(GTK_CONTAINER(self), self->priv->socket);

    /* Run gnuplot */
    image_analyzer_disc_topology_run_gnuplot(self, NULL);
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE(ImageAnalyzerDiscTopology, image_analyzer_disc_topology, GTK_TYPE_WINDOW);

static void image_analyzer_disc_topology_init (ImageAnalyzerDiscTopology *self)
{
    self->priv = IMAGE_ANALYZER_DISC_TOPOLOGY_GET_PRIVATE(self);

    setup_gui(self);
}

static void image_analyzer_disc_topology_class_init (ImageAnalyzerDiscTopologyClass *klass)
{
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(ImageAnalyzerDiscTopologyPrivate));
}
