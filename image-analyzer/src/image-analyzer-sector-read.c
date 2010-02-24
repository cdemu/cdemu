/*
 *  MIRAGE Image Analyzer: Sector read window
 *  Copyright (C) 2007-2009 Rok Mandeljc
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
#include "image-analyzer-sector-read.h"


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define IMAGE_ANALYZER_SECTOR_READ_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), IMAGE_ANALYZER_TYPE_SECTOR_READ, IMAGE_ANALYZER_SectorReadPrivate))

typedef struct {
    /* Application */
    GObject *application;
    
    /* Text entry */
    GtkWidget *text_view;
    GtkTextBuffer *buffer;
    
    GtkWidget *spinbutton;
} IMAGE_ANALYZER_SectorReadPrivate;

typedef enum {
    PROPERTY_APPLICATION = 1,
} IMAGE_ANALYZER_SectorReadProperties;


/******************************************************************************\
 *                               Dump functions                               *
\******************************************************************************/
static gchar *__dump_sector_type (gint sector_type) {
    static DUMP_Value values[] = {
        VAL(MIRAGE_MODE_MODE0),
        VAL(MIRAGE_MODE_AUDIO),
        VAL(MIRAGE_MODE_MODE1),
        VAL(MIRAGE_MODE_MODE2),
        VAL(MIRAGE_MODE_MODE2_FORM1),
        VAL(MIRAGE_MODE_MODE2_FORM2),
    };
    
    return __dump_value(sector_type, values, G_N_ELEMENTS(values));
}

/******************************************************************************\
 *                          Text buffer manipulation                          *
\******************************************************************************/
static gboolean __image_analyzer_read_sector_clear_text (IMAGE_ANALYZER_SectorRead *self) {
    IMAGE_ANALYZER_SectorReadPrivate *_priv = IMAGE_ANALYZER_SECTOR_READ_GET_PRIVATE(self);
    gtk_text_buffer_set_text(_priv->buffer, "", -1);    
    return TRUE;
}

static gboolean __image_analyzer_read_sector_append_text (IMAGE_ANALYZER_SectorRead *self, const gchar *tag_name, const gchar *format, ...) {
    IMAGE_ANALYZER_SectorReadPrivate *_priv = IMAGE_ANALYZER_SECTOR_READ_GET_PRIVATE(self);
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
    
    return TRUE;
}

static gboolean __image_analyzer_read_sector_append_sector_data (IMAGE_ANALYZER_SectorRead *self, const guint8 *data, gint data_len, const gchar *tag_name) {
    gint i;
    
    for (i = 0; i < data_len; i++) {
        __image_analyzer_read_sector_append_text(self, tag_name, "%02hhX ", data[i]);
    }
    
    return TRUE;
}

/******************************************************************************\
 *                                 UI callbacks                               *
\******************************************************************************/
static void __image_analyzer_sector_read_ui_callback_read (GtkWidget *button, gpointer user_data) {
    IMAGE_ANALYZER_SectorRead *self = IMAGE_ANALYZER_SECTOR_READ(user_data);
    IMAGE_ANALYZER_SectorReadPrivate *_priv = IMAGE_ANALYZER_SECTOR_READ_GET_PRIVATE(self);
    GObject *disc, *sector;
    GError *error = NULL;
    gint address, sector_type;
    gchar *address_msf;
    
    gdouble dpm_angle, dpm_density;
    
    const guint8 *tmp_buf;
    gint tmp_len;
    
    /* Read address from spin button */
    address = gtk_spin_button_get_value(GTK_SPIN_BUTTON(_priv->spinbutton));
    
    /* Clear buffer */
    __image_analyzer_read_sector_clear_text(self);
    
    /* Get image */
    if (!image_analyzer_application_get_loaded_image(IMAGE_ANALYZER_APPLICATION(_priv->application), &disc)) {
        __image_analyzer_read_sector_append_text(self, NULL, "No image loaded!\n");
        return;
    }
    
    /* Get sector from disc */
    if (!mirage_disc_get_sector(MIRAGE_DISC(disc), address, &sector, &error)) {
        __image_analyzer_read_sector_append_text(self, NULL, "Failed to get sector: %s\n", error->message);
        g_error_free(error);
        return;
    }
    
    /* Sector address */
    __image_analyzer_read_sector_append_text(self, "tag_section", "Sector address: ");
    __image_analyzer_read_sector_append_text(self, NULL, "%X (%d)\n", address, address);
    
    /* Sector address MSF */
    address_msf = mirage_helper_lba2msf_str(address, TRUE);
    __image_analyzer_read_sector_append_text(self, "tag_section", "Sector address MSF: ");
    __image_analyzer_read_sector_append_text(self, NULL, "%s\n", address_msf);
    g_free(address_msf);
    
    /* Sector type */
    mirage_sector_get_sector_type(MIRAGE_SECTOR(sector), &sector_type, NULL);
    __image_analyzer_read_sector_append_text(self, "tag_section", "Sector type: ");
    __image_analyzer_read_sector_append_text(self, NULL, "0x%X (%s)\n", sector_type, __dump_sector_type(sector_type));
    
    __image_analyzer_read_sector_append_text(self, NULL, "\n");
    
    /* DPM */
    if (mirage_disc_get_dpm_data_for_sector(MIRAGE_DISC(disc), address, &dpm_angle, &dpm_density, NULL)) {
        __image_analyzer_read_sector_append_text(self, "tag_section", "Sector angle: ");
        __image_analyzer_read_sector_append_text(self, NULL, "%f rotations\n", dpm_angle);
        
        __image_analyzer_read_sector_append_text(self, "tag_section", "Sector density: ");
        __image_analyzer_read_sector_append_text(self, NULL, "%f degrees per sector\n", dpm_density);
    }
    __image_analyzer_read_sector_append_text(self, NULL, "\n");
    
     /* PQ subchannel */
    __image_analyzer_read_sector_append_text(self, "tag_section", "PQ subchannel:\n");
    mirage_sector_get_subchannel(MIRAGE_SECTOR(sector), MIRAGE_SUBCHANNEL_PQ, &tmp_buf, &tmp_len, NULL);
    __image_analyzer_read_sector_append_sector_data(self, tmp_buf, tmp_len, NULL);
    __image_analyzer_read_sector_append_text(self, NULL, "\n\n");
    
    
    /* All sector data */
    __image_analyzer_read_sector_append_text(self, "tag_section", "Sector data dump:\n", address);
    
    /* Sync */
    mirage_sector_get_sync(MIRAGE_SECTOR(sector), &tmp_buf, &tmp_len, NULL);
    __image_analyzer_read_sector_append_sector_data(self, tmp_buf, tmp_len, "tag_sync");
    /* Header */
    mirage_sector_get_header(MIRAGE_SECTOR(sector), &tmp_buf, &tmp_len, NULL);
    __image_analyzer_read_sector_append_sector_data(self, tmp_buf, tmp_len, "tag_header");
    /* Subheader */
    mirage_sector_get_subheader(MIRAGE_SECTOR(sector), &tmp_buf, &tmp_len, NULL);
    __image_analyzer_read_sector_append_sector_data(self, tmp_buf, tmp_len, "tag_subheader");
    /* Data */
    mirage_sector_get_data(MIRAGE_SECTOR(sector), &tmp_buf, &tmp_len, NULL);
    __image_analyzer_read_sector_append_sector_data(self, tmp_buf, tmp_len, "tag_data");
    /* EDC/ECC */
    mirage_sector_get_edc_ecc(MIRAGE_SECTOR(sector), &tmp_buf, &tmp_len, NULL);
    __image_analyzer_read_sector_append_sector_data(self, tmp_buf, tmp_len, "tag_edc_ecc");
    /* Subchannel */
    mirage_sector_get_subchannel(MIRAGE_SECTOR(sector), MIRAGE_SUBCHANNEL_PW, &tmp_buf, &tmp_len, NULL);
    __image_analyzer_read_sector_append_sector_data(self, tmp_buf, tmp_len, "tag_subchannel");
    
    g_object_unref(sector);
    g_object_unref(disc);
    
    return;
}

/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static GtkWindowClass *parent_class = NULL;

static void __image_analyzer_sector_read_get_property (GObject *obj, guint param_id, GValue *value, GParamSpec *pspec) {
    IMAGE_ANALYZER_SectorRead *self = IMAGE_ANALYZER_SECTOR_READ(obj);
    IMAGE_ANALYZER_SectorReadPrivate *_priv = IMAGE_ANALYZER_SECTOR_READ_GET_PRIVATE(self);
        
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

static void __image_analyzer_sector_read_set_property (GObject *obj, guint param_id, const GValue *value, GParamSpec *pspec) {
    IMAGE_ANALYZER_SectorRead *self = IMAGE_ANALYZER_SECTOR_READ(obj);
    IMAGE_ANALYZER_SectorReadPrivate *_priv = IMAGE_ANALYZER_SECTOR_READ_GET_PRIVATE(self);
    
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

static void __image_analyzer_sector_read_instance_init (GTypeInstance *instance, gpointer g_class) {
    IMAGE_ANALYZER_SectorRead *self = IMAGE_ANALYZER_SECTOR_READ(instance);
    IMAGE_ANALYZER_SectorReadPrivate *_priv = IMAGE_ANALYZER_SECTOR_READ_GET_PRIVATE(self);
    
    GtkWidget *vbox, *scrolledwindow, *hbox, *button;
    GtkObject *adjustment;
    
    gtk_window_set_title(GTK_WINDOW(self), "Read sector");
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
    
    gtk_text_buffer_create_tag(_priv->buffer, "tag_sync", "foreground", "#CC0033", "font", "fixed", NULL); /* Red */
    gtk_text_buffer_create_tag(_priv->buffer, "tag_header", "foreground", "#33CC33", "font", "fixed", NULL); /* Green */
    gtk_text_buffer_create_tag(_priv->buffer, "tag_subheader", "foreground", "#990099", "font", "fixed", NULL); /* Purple */
    gtk_text_buffer_create_tag(_priv->buffer, "tag_data", "foreground", "#000000", "font", "fixed", NULL); /* Black */
    gtk_text_buffer_create_tag(_priv->buffer, "tag_edc_ecc", "foreground", "#FF9933", "font", "fixed", NULL); /* Orange */
    gtk_text_buffer_create_tag(_priv->buffer, "tag_subchannel", "foreground", "#0033FF", "font", "fixed", NULL); /* Blue */
        
    /* HBox */
    hbox = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    
    /* Spin button */
    adjustment = gtk_adjustment_new(0, G_MININT64, G_MAXINT64, 1, 75, 0);
    _priv->spinbutton = gtk_spin_button_new(GTK_ADJUSTMENT(adjustment), 1, 0);
    gtk_box_pack_start(GTK_BOX(hbox), _priv->spinbutton, TRUE, TRUE, 0);
    
    /* Button */
    button = gtk_button_new_with_label("Read");
    g_signal_connect(button, "clicked", G_CALLBACK(__image_analyzer_sector_read_ui_callback_read), self);
    gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);   
    
    return;
}

static void __image_analyzer_sector_read_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    IMAGE_ANALYZER_SectorReadClass *klass = IMAGE_ANALYZER_SECTOR_READ_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(IMAGE_ANALYZER_SectorReadPrivate));
    
    /* Initialize GObject methods */
    class_gobject->get_property = __image_analyzer_sector_read_get_property;
    class_gobject->set_property = __image_analyzer_sector_read_set_property;
    
    /* Install properties */
    g_object_class_install_property(class_gobject, PROPERTY_APPLICATION, g_param_spec_object("application", "Application", "Parent application", IMAGE_ANALYZER_TYPE_APPLICATION, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
    
    return;
}

GType image_analyzer_sector_read_get_type (void) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(IMAGE_ANALYZER_SectorReadClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __image_analyzer_sector_read_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(IMAGE_ANALYZER_SectorRead),
            0,      /* n_preallocs */
            __image_analyzer_sector_read_instance_init    /* instance_init */
        };
        
        type = g_type_register_static(GTK_TYPE_WINDOW, "IMAGE_ANALYZER_SectorRead", &info, 0);
    }
    
    return type;
}
