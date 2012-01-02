/*
 *  MIRAGE Image Analyzer: Disc topology window
 *  Copyright (C) 2008-2010 Rok Mandeljc
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
#include <gtk/gtkx.h>

#include <mirage.h>
#include "image-analyzer-dump.h"
#include "image-analyzer-application.h"
#include "image-analyzer-disc-topology.h"


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define IMAGE_ANALYZER_DISC_TOPOLOGY_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), IMAGE_ANALYZER_TYPE_DISC_TOPOLOGY, IMAGE_ANALYZER_DiscTopologyPrivate))

typedef struct {
    /* GtkSocket */
    GtkWidget *socket;

    /* gnuplot */
    gboolean gnuplot_works;
        
    GPid pid;
    gint fd_in;
} IMAGE_ANALYZER_DiscTopologyPrivate;


/******************************************************************************\
 *                                   Helpers                                  *
\******************************************************************************/
static gboolean __image_analyzer_disc_topology_run_gnuplot (IMAGE_ANALYZER_DiscTopology *self, GError **error) {
    IMAGE_ANALYZER_DiscTopologyPrivate *_priv = IMAGE_ANALYZER_DISC_TOPOLOGY_GET_PRIVATE(self);

    gchar *argv[] = { "gnuplot", NULL };
    gboolean ret;
    gchar *cmd;

    /* Spawn gnuplot */
    ret = g_spawn_async_with_pipes(
            NULL, /* const gchar *working_directory, (NULL = inhearit parent) */
            argv, /* gchar **argv */
            NULL, /* gchar **envp */
            G_SPAWN_SEARCH_PATH, /* GSpawnFlags flags */
            NULL, /* GSpawnChildSetupFunc child_setup */
            NULL, /* gpointer user_data */
            &_priv->pid, /* GPid *child_pid */
            &_priv->fd_in, /* gint *standard_input */
            NULL, /* gint *standard_output */
            NULL, /* gint *standard_error */
            error /* GError **error */
        );
    if (!ret) {
        g_debug("Failed to spawn gnuplot process!\n");
        _priv->gnuplot_works = FALSE;
        return FALSE;
    } else {
        _priv->gnuplot_works = TRUE;
    }

    /* Redirect to socket */
    gtk_widget_show_all(GTK_WIDGET(self));

    cmd = g_strdup_printf("set term x11 window '%lX' ctrlq enhanced", gtk_socket_get_id(GTK_SOCKET(_priv->socket)));
    write(_priv->fd_in, cmd, strlen(cmd));
    write(_priv->fd_in, "\n", 1);

    gtk_widget_hide(GTK_WIDGET(self));
    
    return TRUE;
}


static gboolean __dump_dpm_data (GObject *disc, gchar **dump_file)
{
    GError *local_error = NULL;
    gchar *tmp_filename;
    gint fd;

    gint dpm_start, dpm_entries, dpm_resolution;
    gint i;

    gint address;
    gdouble density;
    gchar *line;
    gchar dbl_buffer[G_ASCII_DTOSTR_BUF_SIZE] = "";

    *dump_file = NULL;

    /* Prepare data for plot */
    if (!mirage_disc_get_dpm_data(MIRAGE_DISC(disc), &dpm_start, &dpm_resolution, &dpm_entries, NULL, NULL)) {
        return TRUE;
    }

    /* Open temporary file */
    fd = g_file_open_tmp("disc_topology_XXXXXX", &tmp_filename, &local_error);
    if (fd == -1) {
        g_warning("%s: failed to create temporary file: %s!\n", __func__, local_error->message);
        g_error_free(local_error);
        return FALSE;
    }

    /* Dump DPM data into temporary file */    
    for (i = 0; i < dpm_entries; i++) {
        address = dpm_start + i*dpm_resolution;
        density = 0;

        if (!mirage_disc_get_dpm_data_for_sector(MIRAGE_DISC(disc), address, NULL, &density, NULL)) {
            /*g_debug("%s: failed to get DPM data for address 0x%X\n", __func__, address);*/
            continue;
        }

        /* NOTE: we convert double to string using g_ascii_dtostr, because
           %g and %f are locale-dependent */
        line = g_strdup_printf("%d %s\n", address, g_ascii_dtostr(dbl_buffer, G_ASCII_DTOSTR_BUF_SIZE, density));
        write(fd, line, strlen(line));
        g_free(line);
    }

    /* Close temporary file */
    close(fd);

    *dump_file = tmp_filename;
    return TRUE;
}



/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
gboolean image_analyzer_disc_topology_refresh (IMAGE_ANALYZER_DiscTopology *self, GObject *disc, GError **error) {
    IMAGE_ANALYZER_DiscTopologyPrivate *_priv = IMAGE_ANALYZER_DISC_TOPOLOGY_GET_PRIVATE(self);
    gchar *command;
    
    /* No-op if gnuplot couldn't be started */
    if (!_priv->gnuplot_works) {
        return TRUE;
    }

    if (!disc) {
        /* No disc */
        command = g_strdup_printf(
            "clear; reset; "
            "unset xtics; unset ytics; "
            "unset border; unset key; "
            "set title 'No disc loaded'; "
            "plot [][0:1] 2 notitle; "
            "reset"
        );
    } else {
        gchar **filenames = NULL;
        gchar *basename;

        mirage_disc_get_filenames(MIRAGE_DISC(disc), &filenames, NULL);
        basename = g_path_get_basename(filenames[0]);
        
        if (!mirage_disc_get_dpm_data(MIRAGE_DISC(disc), NULL, NULL, NULL, NULL, NULL)) {
            /* No DPM data */
            command = g_strdup_printf(
                "clear; reset; "
                "unset xtics; unset ytics; "
                "unset border; unset key; "
                "set title '%s%s: no topology data'; "
                "plot [][0:1] 2 notitle; ",
                basename,
                filenames[1] ? "..." : ""
            );
        } else {
            gchar *tmp_filename;

            /* Dump DPM data to temporary file */
            if (!__dump_dpm_data(disc, &tmp_filename)) {
                g_free(basename);
                return FALSE;
            }

            /* Plot */
            command = g_strdup_printf(
                "clear; reset; "
                "set title '%s%s: disc topology'; "
                "set xlabel 'Sector address'; "
                "set ylabel 'Sector density [degrees/sector]'; "
                "set grid; "
                "plot '%s' notitle with lines; ",
                basename,
                filenames[1] ? "..." : "",
                tmp_filename
            );

            g_free(tmp_filename);
        }

        g_free(basename);
    }

    /* Write plot command */
    write(_priv->fd_in, command, strlen(command));
    write(_priv->fd_in, "\n", 1);

    g_free(command);
    
    return TRUE;
}


/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static GtkWindowClass *parent_class = NULL;

static void __image_analyzer_disc_topology_instance_init (GTypeInstance *instance, gpointer g_class) {
    IMAGE_ANALYZER_DiscTopology *self = IMAGE_ANALYZER_DISC_TOPOLOGY(instance);
    IMAGE_ANALYZER_DiscTopologyPrivate *_priv = IMAGE_ANALYZER_DISC_TOPOLOGY_GET_PRIVATE(self);

    /* Window */
    gtk_window_set_title(GTK_WINDOW(self), "Disc topology");
    gtk_window_set_default_size(GTK_WINDOW(self), 800, 600);
    gtk_container_set_border_width(GTK_CONTAINER(self), 5);

    /* Create socket for embedding gnuplot window */
    _priv->socket = gtk_socket_new();
    gtk_container_add(GTK_CONTAINER(self), _priv->socket);

    /* Run gnuplot */
    __image_analyzer_disc_topology_run_gnuplot(self, NULL);
    
    return;
}


static void __image_analyzer_disc_topology_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    IMAGE_ANALYZER_DiscTopologyClass *klass = IMAGE_ANALYZER_DISC_TOPOLOGY_CLASS(g_class);

    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(IMAGE_ANALYZER_DiscTopologyPrivate));

    return;
}

GType image_analyzer_disc_topology_get_type (void) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(IMAGE_ANALYZER_DiscTopologyClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __image_analyzer_disc_topology_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(IMAGE_ANALYZER_DiscTopology),
            0,      /* n_preallocs */
            __image_analyzer_disc_topology_instance_init,   /* instance_init */
            NULL,   /* value_table */
        };

        type = g_type_register_static(GTK_TYPE_WINDOW, "IMAGE_ANALYZER_DiscTopology", &info, 0);
    }

    return type;
}
