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

#include <mirage.h>
#include "image-analyzer-dump.h"
#include "image-analyzer-application.h"
#include "image-analyzer-disc-topology.h"

#if HAVE_GTKEXTRA
#include <gtkextra/gtkplot.h>
#include <gtkextra/gtkplotdata.h>
#include <gtkextra/gtkplotcanvas.h>
#include <gtkextra/gtkplotcanvasplot.h>
#endif

/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define IMAGE_ANALYZER_DISC_TOPOLOGY_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), IMAGE_ANALYZER_TYPE_DISC_TOPOLOGY, IMAGE_ANALYZER_DiscTopologyPrivate))

typedef struct {
    /* Application */
    GObject *application;

#if HAVE_GTKEXTRA
    GtkWidget *canvas;
    GtkWidget *plot;
    GtkPlotData *plotdata;

    gdouble *data_x;
    gdouble *data_y;

    gint orig_width;
    gint orig_height;

    GSList *zoom_steps;
#endif
} IMAGE_ANALYZER_DiscTopologyPrivate;

typedef enum {
    PROPERTY_APPLICATION = 1,
} IMAGE_ANALYZER_DiscTopologyProperties;


/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
#if HAVE_GTKEXTRA
static void __resize_canvas (GtkWidget *widget, GtkAllocation *allocation, gpointer user_data) {
    IMAGE_ANALYZER_DiscTopology *self = user_data;
    IMAGE_ANALYZER_DiscTopologyPrivate *_priv = IMAGE_ANALYZER_DISC_TOPOLOGY_GET_PRIVATE(self);

    /* Scale ('magnify') */
    gdouble mag_x, mag_y, mag;

    mag_x = 1.0 * allocation->width/_priv->orig_width;
    mag_y = 1.0 * allocation->height/_priv->orig_height;

    mag = MAX(mag_x, mag_y);

    gtk_plot_canvas_set_magnification(GTK_PLOT_CANVAS(_priv->canvas), mag);

    /* Refresh */
    gtk_plot_canvas_paint(GTK_PLOT_CANVAS(_priv->canvas));
    gtk_plot_canvas_refresh(GTK_PLOT_CANVAS(_priv->canvas));

    return;
}

static void __zoom_push (IMAGE_ANALYZER_DiscTopology *self, gdouble *region) {
    IMAGE_ANALYZER_DiscTopologyPrivate *_priv = IMAGE_ANALYZER_DISC_TOPOLOGY_GET_PRIVATE(self);
    /* Add provided region to the top of the stack */
    _priv->zoom_steps = g_slist_prepend(_priv->zoom_steps, region);
    return;
}

static void __zoom_pop (IMAGE_ANALYZER_DiscTopology *self) {
    IMAGE_ANALYZER_DiscTopologyPrivate *_priv = IMAGE_ANALYZER_DISC_TOPOLOGY_GET_PRIVATE(self);
    /* Allow removing all but first element */
    if (g_slist_length(_priv->zoom_steps) > 1) {
        _priv->zoom_steps = g_slist_delete_link(_priv->zoom_steps, _priv->zoom_steps);
    }
    return;
}

static gboolean __image_analyzer_disc_topology_refresh_display_region (IMAGE_ANALYZER_DiscTopology *self, GError **error) {
    IMAGE_ANALYZER_DiscTopologyPrivate *_priv = IMAGE_ANALYZER_DISC_TOPOLOGY_GET_PRIVATE(self);

    gdouble xmin, xmax, ymin, ymax;
    gdouble xscale, yscale;

    /* Get display region; either use top of the zoom steps stack or fallback */
    if (_priv->zoom_steps) {
        gdouble *region = _priv->zoom_steps->data;
        xmin = region[0];
        xmax = region[1];
        ymin = region[2];
        ymax = region[3];
    } else {
        xmin = 0;
        xmax = 74*60*75; /* 74-min CD-ROM */
        ymin = 0;
        ymax = 50;
    }

    xscale = (xmax-xmin)/4.0;
    yscale = (ymax-ymin)/4.0;

    xscale = round(xscale/500)*500;
    yscale = round(yscale/0.25)*0.25;

    if (xscale <= 0) {
        //g_debug("%s: X-scale too small!\n", __func__);
        return FALSE;
    }

    if (yscale <= 0) {
        //g_debug("%s: Y-scale too small!\n", __func__);
        return FALSE;
    }

    /* Rearrange ticks */
    gtk_plot_axis_set_ticks(gtk_plot_get_axis(GTK_PLOT(_priv->plot), GTK_PLOT_AXIS_BOTTOM), xscale, 1);
    gtk_plot_axis_set_labels_numbers(gtk_plot_get_axis(GTK_PLOT(_priv->plot), GTK_PLOT_AXIS_BOTTOM), GTK_PLOT_LABEL_FLOAT, 0);
    gtk_plot_axis_set_ticks(gtk_plot_get_axis(GTK_PLOT(_priv->plot), GTK_PLOT_AXIS_TOP), xscale, 1);
    gtk_plot_axis_set_labels_numbers(gtk_plot_get_axis(GTK_PLOT(_priv->plot), GTK_PLOT_AXIS_TOP), GTK_PLOT_LABEL_FLOAT, 0);

    gtk_plot_axis_set_ticks(gtk_plot_get_axis(GTK_PLOT(_priv->plot), GTK_PLOT_AXIS_LEFT), yscale, 1);
    gtk_plot_axis_set_labels_numbers(gtk_plot_get_axis(GTK_PLOT(_priv->plot), GTK_PLOT_AXIS_LEFT), GTK_PLOT_LABEL_FLOAT, 2);
    gtk_plot_axis_set_ticks(gtk_plot_get_axis(GTK_PLOT(_priv->plot), GTK_PLOT_AXIS_RIGHT), yscale, 1);
    gtk_plot_axis_set_labels_numbers(gtk_plot_get_axis(GTK_PLOT(_priv->plot), GTK_PLOT_AXIS_RIGHT), GTK_PLOT_LABEL_FLOAT, 2);

    /* Set new range */
    gtk_plot_set_range(GTK_PLOT(_priv->plot), xmin, xmax, ymin, ymax);

    /* Refresh */
    gtk_plot_canvas_paint(GTK_PLOT_CANVAS(_priv->canvas));
    gtk_plot_canvas_refresh(GTK_PLOT_CANVAS(_priv->canvas));

    return TRUE;
}

static gboolean __cb_button_press_event (GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    IMAGE_ANALYZER_DiscTopology *self = user_data;

    /* Zoom out on right click; one step */
    if (event->button == 3) {
        __zoom_pop(self);
        __image_analyzer_disc_topology_refresh_display_region(self, NULL);
    }

    /* Let other handlers do their job, too */
    return FALSE;
}

static gboolean __cb_select_region (GtkPlotCanvas *canvas, gdouble x1, gdouble y1, gdouble x2, gdouble y2, gpointer user_data) {
    IMAGE_ANALYZER_DiscTopology *self = user_data;
    IMAGE_ANALYZER_DiscTopologyPrivate *_priv = IMAGE_ANALYZER_DISC_TOPOLOGY_GET_PRIVATE(self);

    gdouble xmin, ymin, xmax, ymax;
    gint px1, px2, py1, py2;

    gdouble *region;

    /* There must be plot... */
    if (!_priv->plot) {
        return FALSE;
    }

    xmin = MIN(x1, x2);
    ymin = MIN(y1, y2);
    xmax = MAX(x1, x2);
    ymax = MAX(y1, y2);

    gtk_plot_canvas_get_pixel(GTK_PLOT_CANVAS(_priv->canvas), xmin, ymin, &px1, &py1);
    gtk_plot_canvas_get_pixel(GTK_PLOT_CANVAS(_priv->canvas), xmax, ymax, &px2, &py2);
    gtk_plot_get_point(GTK_PLOT(_priv->plot), px1, py1, &xmin, &ymax);
    gtk_plot_get_point(GTK_PLOT(_priv->plot), px2, py2, &xmax, &ymin);

    xmax = round(xmax);
    xmin = round(xmin);

    region = g_new0(gdouble, 4);
    region[0] = xmin;
    region[1] = xmax;
    region[2] = ymin;
    region[3] = ymax;
    __zoom_push(self, region);

    if (!__image_analyzer_disc_topology_refresh_display_region(self, NULL)) {
        /* If zoom failed, pop the previously added region; this prevents failed
           zoom attempts to keep stacking on zoom stack (i.e. if requested region
           is too small) */
        __zoom_pop(self);
    }

    return TRUE;
}

gboolean image_analyzer_disc_topology_create (IMAGE_ANALYZER_DiscTopology *self, GObject *disc, GError **error) {
    IMAGE_ANALYZER_DiscTopologyPrivate *_priv = IMAGE_ANALYZER_DISC_TOPOLOGY_GET_PRIVATE(self);

    gint dpm_start, dpm_entries, dpm_resolution;

    /* X and Y data... sector addresses and corresponding sector densities */
    gdouble *data_x, *data_y;
    gdouble ymin, ymax;

    gint i;

    gdouble *region;

    /* Prepare data for plot */
    if (!mirage_disc_get_dpm_data(MIRAGE_DISC(disc), &dpm_start, &dpm_resolution, &dpm_entries, NULL, NULL)) {
        /*g_debug("%s: no DPM data...\n", __func__);*/
        return TRUE;
    }

    data_x = g_new0(gdouble, dpm_entries);
    data_y = g_new0(gdouble, dpm_entries);

    ymin = ymax = 25.0; /* In the middle by default */

    for (i = 0; i < dpm_entries; i++) {
        gint address = dpm_start + i*dpm_resolution;
        gdouble density = 0;

        if (!mirage_disc_get_dpm_data_for_sector(MIRAGE_DISC(disc), address, NULL, &density, NULL)) {
            /*g_debug("%s: failed to get DPM data for address 0x%X\n", __func__, address);*/
            continue;
        }

        data_x[i] = address;
        data_y[i] = density;

        ymin = MIN(ymin, data_y[i]);
        ymax = MAX(ymax, data_y[i]);
    }

    /* Do the plot */
    gtk_plot_data_set_points(_priv->plotdata, data_x, data_y, NULL, NULL, dpm_entries);

    /* Zoom: create default region */
    region = g_new0(gdouble, 4);
    region[0] = data_x[0];
    region[1] = data_x[dpm_entries-1];
    region[2] = ymin;
    region[3] = ymax;

    /* Zoom to default region */
    __zoom_push(self, region);
    __image_analyzer_disc_topology_refresh_display_region(self, NULL);

    /* Store data pointers so they can be freed on clear */
    _priv->data_x = data_x;
    _priv->data_y = data_y;

    /* Plot can be selected now */
    GTK_PLOT_CANVAS_SET_FLAGS(GTK_PLOT_CANVAS(_priv->canvas), GTK_PLOT_CANVAS_CAN_SELECT);

    return TRUE;
}

gboolean image_analyzer_disc_topology_clear (IMAGE_ANALYZER_DiscTopology *self, GError **error) {
    IMAGE_ANALYZER_DiscTopologyPrivate *_priv = IMAGE_ANALYZER_DISC_TOPOLOGY_GET_PRIVATE(self);

    /* Clear data */
    gtk_plot_data_set_points(_priv->plotdata, NULL, NULL, NULL, NULL, 0);

    g_free(_priv->data_x);
    _priv->data_x = NULL;
    g_free(_priv->data_y);
    _priv->data_y = NULL;

    /* Clear the zoom list */
    g_slist_free(_priv->zoom_steps);
    _priv->zoom_steps = NULL;
    __image_analyzer_disc_topology_refresh_display_region(self, NULL);

    /* Plot can't be selected anymore */
    GTK_PLOT_CANVAS_UNSET_FLAGS(GTK_PLOT_CANVAS(_priv->canvas), GTK_PLOT_CANVAS_CAN_SELECT);

    return TRUE;
}

#else

gboolean image_analyzer_disc_topology_create (IMAGE_ANALYZER_DiscTopology *self, GObject *disc, GError **error) {
    /* Nothing to do here */
    return TRUE;
}

gboolean image_analyzer_disc_topology_clear (IMAGE_ANALYZER_DiscTopology *self, GError **error) {
    /* Nothing to do here */
    return TRUE;
}

#endif

/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static GtkWindowClass *parent_class = NULL;

static void __image_analyzer_disc_topology_get_property (GObject *obj, guint param_id, GValue *value, GParamSpec *pspec) {
    IMAGE_ANALYZER_DiscTopology *self = IMAGE_ANALYZER_DISC_TOPOLOGY(obj);
    IMAGE_ANALYZER_DiscTopologyPrivate *_priv = IMAGE_ANALYZER_DISC_TOPOLOGY_GET_PRIVATE(self);

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

static void __image_analyzer_disc_topology_set_property (GObject *obj, guint param_id, const GValue *value, GParamSpec *pspec) {
    IMAGE_ANALYZER_DiscTopology *self = IMAGE_ANALYZER_DISC_TOPOLOGY(obj);
    IMAGE_ANALYZER_DiscTopologyPrivate *_priv = IMAGE_ANALYZER_DISC_TOPOLOGY_GET_PRIVATE(self);

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

#if HAVE_GTKEXTRA
static void __image_analyzer_disc_topology_instance_init (GTypeInstance *instance, gpointer g_class) {
    IMAGE_ANALYZER_DiscTopology *self = IMAGE_ANALYZER_DISC_TOPOLOGY(instance);
    IMAGE_ANALYZER_DiscTopologyPrivate *_priv = IMAGE_ANALYZER_DISC_TOPOLOGY_GET_PRIVATE(self);

    GtkWidget *vbox;

    GtkPlotCanvasChild *child;
    GtkPlotData *dataset;
    GdkColor color;

    gtk_window_set_title(GTK_WINDOW(self), "Disc topology");
    gtk_window_set_default_size(GTK_WINDOW(self), 800, 600);
    gtk_container_set_border_width(GTK_CONTAINER(self), 5);

    /* VBox */
    vbox = gtk_vbox_new(FALSE, 5);
    gtk_container_add(GTK_CONTAINER(self), vbox);

    /* Canvas */
    _priv->orig_width = 800;
    _priv->orig_height = 600;
    _priv->canvas = gtk_plot_canvas_new(_priv->orig_width, _priv->orig_height, 1.0);
    gtk_box_pack_start(GTK_BOX(vbox), _priv->canvas, TRUE, TRUE, 0);

    gtk_signal_connect(GTK_OBJECT(_priv->canvas), "select_region", GTK_SIGNAL_FUNC(__cb_select_region), self);
    gtk_signal_connect(GTK_OBJECT(_priv->canvas), "button-press-event", GTK_SIGNAL_FUNC(__cb_button_press_event), self);

    gdk_color_parse("#FFFF66", &color); /* Bright yellow */
    gdk_color_alloc(gdk_colormap_get_system(), &color);
    gtk_plot_canvas_set_background(GTK_PLOT_CANVAS(_priv->canvas), &color);

    /* Ugly hack because canvas doesn't automatically resize itself to its allocated space */
    gtk_signal_connect(GTK_OBJECT(_priv->canvas), "size-allocate", GTK_SIGNAL_FUNC(__resize_canvas), self);


    /* Plot */
    _priv->plot = gtk_plot_new(NULL);
    gtk_widget_show(_priv->plot);

    gtk_plot_clip_data(GTK_PLOT(_priv->plot), TRUE);

    gdk_color_parse("#FFFF99", &color); /* Bright yellow */
    gdk_color_alloc(gdk_colormap_get_system(), &color);
    gtk_plot_set_background(GTK_PLOT(_priv->plot), &color);

    gtk_plot_grids_set_visible(GTK_PLOT(_priv->plot), TRUE, TRUE, TRUE, TRUE);
    gdk_color_parse("gray", &color); /* Light grey */
    gdk_color_alloc(gdk_colormap_get_system(), &color);
    gtk_plot_major_vgrid_set_attributes(GTK_PLOT(_priv->plot), GTK_PLOT_LINE_DASHED, 1.0, &color);
    gtk_plot_minor_vgrid_set_attributes(GTK_PLOT(_priv->plot), GTK_PLOT_LINE_DOTTED, 1.0, &color);
    gtk_plot_major_hgrid_set_attributes(GTK_PLOT(_priv->plot), GTK_PLOT_LINE_DASHED, 1.0, &color);
    gtk_plot_minor_hgrid_set_attributes(GTK_PLOT(_priv->plot), GTK_PLOT_LINE_DOTTED, 1.0, &color);

    /* Put plot onto canvas */
    child = gtk_plot_canvas_plot_new(GTK_PLOT(_priv->plot));
    gtk_plot_canvas_put_child(GTK_PLOT_CANVAS(_priv->canvas), child, .1, .1, .8, .8);

    /* Dataset */
    dataset = GTK_PLOT_DATA(gtk_plot_data_new());
    gtk_plot_add_data(GTK_PLOT(_priv->plot), dataset);
    _priv->plotdata = dataset;
    gtk_widget_show(GTK_WIDGET(dataset));

    /* Edit line */
    gdk_color_parse("red", &color);
    gdk_color_alloc(gdk_colormap_get_system(), &color);
    gtk_plot_data_set_symbol(dataset, GTK_PLOT_SYMBOL_DOT, GTK_PLOT_SYMBOL_EMPTY, 2, 2, &color, &color);
    gtk_plot_data_set_line_attributes(dataset, GTK_PLOT_LINE_SOLID, 0, 0, 1.25, &color);
    gtk_plot_data_set_connector(dataset, GTK_PLOT_CONNECT_STRAIGHT);

    gtk_plot_data_set_legend(dataset, "Sector density");

    /* Some cosmetics */
    gtk_plot_hide_legends(GTK_PLOT(_priv->plot));

    gtk_plot_axis_set_title(gtk_plot_get_axis(GTK_PLOT(_priv->plot), GTK_PLOT_AXIS_LEFT), "Sector density (degrees per sector)");
    gtk_plot_axis_hide_title(gtk_plot_get_axis(GTK_PLOT(_priv->plot), GTK_PLOT_AXIS_LEFT));
    gtk_plot_axis_justify_title(gtk_plot_get_axis(GTK_PLOT(_priv->plot), GTK_PLOT_AXIS_LEFT), GTK_JUSTIFY_CENTER);

    gtk_plot_axis_set_title(gtk_plot_get_axis(GTK_PLOT(_priv->plot), GTK_PLOT_AXIS_RIGHT), "Sector density (degrees per sector)");
    gtk_plot_axis_show_title(gtk_plot_get_axis(GTK_PLOT(_priv->plot), GTK_PLOT_AXIS_RIGHT));
    gtk_plot_axis_justify_title(gtk_plot_get_axis(GTK_PLOT(_priv->plot), GTK_PLOT_AXIS_RIGHT), GTK_JUSTIFY_CENTER);

    gtk_plot_axis_set_title(gtk_plot_get_axis(GTK_PLOT(_priv->plot), GTK_PLOT_AXIS_TOP), "Address");
    gtk_plot_axis_hide_title(gtk_plot_get_axis(GTK_PLOT(_priv->plot), GTK_PLOT_AXIS_TOP));
    gtk_plot_axis_justify_title(gtk_plot_get_axis(GTK_PLOT(_priv->plot), GTK_PLOT_AXIS_TOP), GTK_JUSTIFY_CENTER);
    gtk_plot_axis_show_labels(gtk_plot_get_axis(GTK_PLOT(_priv->plot), GTK_PLOT_AXIS_TOP), 0);

    gtk_plot_axis_set_title(gtk_plot_get_axis(GTK_PLOT(_priv->plot), GTK_PLOT_AXIS_BOTTOM), "Address");
    gtk_plot_axis_show_title(gtk_plot_get_axis(GTK_PLOT(_priv->plot), GTK_PLOT_AXIS_BOTTOM));
    gtk_plot_axis_justify_title(gtk_plot_get_axis(GTK_PLOT(_priv->plot), GTK_PLOT_AXIS_BOTTOM), GTK_JUSTIFY_CENTER);

    /* Default zoom */
    __image_analyzer_disc_topology_refresh_display_region(self, NULL);

    return;
}
#else
static void __image_analyzer_disc_topology_instance_init (GTypeInstance *instance, gpointer g_class) {
    /* Nothing to do here */
}
#endif


static void __image_analyzer_disc_topology_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    IMAGE_ANALYZER_DiscTopologyClass *klass = IMAGE_ANALYZER_DISC_TOPOLOGY_CLASS(g_class);

    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(IMAGE_ANALYZER_DiscTopologyPrivate));

    /* Initialize GObject methods */
    class_gobject->get_property = __image_analyzer_disc_topology_get_property;
    class_gobject->set_property = __image_analyzer_disc_topology_set_property;

    /* Install properties */
    g_object_class_install_property(class_gobject, PROPERTY_APPLICATION, g_param_spec_object("application", "Application", "Parent application", IMAGE_ANALYZER_TYPE_APPLICATION, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

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
