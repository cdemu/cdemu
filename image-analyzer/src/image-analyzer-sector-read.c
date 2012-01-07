/*
 *  Image Analyzer: Sector read window
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
#include <mirage.h>

#include "image-analyzer-dump.h"
#include "image-analyzer-sector-read.h"
#include "image-analyzer-sector-read-private.h"


/**********************************************************************\
 *                           Dump functions                           *
\**********************************************************************/
static gchar *dump_sector_type (gint sector_type)
{
    static DUMP_Value values[] = {
        VAL(MIRAGE_MODE_MODE0),
        VAL(MIRAGE_MODE_AUDIO),
        VAL(MIRAGE_MODE_MODE1),
        VAL(MIRAGE_MODE_MODE2),
        VAL(MIRAGE_MODE_MODE2_FORM1),
        VAL(MIRAGE_MODE_MODE2_FORM2),
    };

    return dump_value(sector_type, values, G_N_ELEMENTS(values));
}

/**********************************************************************\
 *                      Text buffer manipulation                      *
\**********************************************************************/
static gboolean image_analyzer_read_sector_clear_text (IMAGE_ANALYZER_SectorRead *self)
{
    gtk_text_buffer_set_text(self->priv->buffer, "", -1);
    return TRUE;
}

static gboolean image_analyzer_read_sector_append_text (IMAGE_ANALYZER_SectorRead *self, const gchar *tag_name, const gchar *format, ...)
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

    return TRUE;
}

static gboolean image_analyzer_read_sector_append_sector_data (IMAGE_ANALYZER_SectorRead *self, const guint8 *data, gint data_len, const gchar *tag_name)
{
    gint i;
    for (i = 0; i < data_len; i++) {
        image_analyzer_read_sector_append_text(self, tag_name, "%02hhX ", data[i]);
    }
    return TRUE;
}


/**********************************************************************\
 *                             UI callbacks                           *
\**********************************************************************/
static void image_analyzer_sector_read_ui_callback_read (GtkWidget *button G_GNUC_UNUSED, IMAGE_ANALYZER_SectorRead *self)
{
    GObject *sector;
    GError *error = NULL;
    gint address, sector_type;
    gchar *address_msf;

    gdouble dpm_angle, dpm_density;

    const guint8 *tmp_buf;
    gint tmp_len;

    /* Read address from spin button */
    address = gtk_spin_button_get_value(GTK_SPIN_BUTTON(self->priv->spinbutton));

    /* Clear buffer */
    image_analyzer_read_sector_clear_text(self);

    /* Get image */
    if (!self->priv->disc) {
        image_analyzer_read_sector_append_text(self, NULL, "No image loaded!\n");
        return;
    }

    /* Get sector from disc */
    if (!mirage_disc_get_sector(MIRAGE_DISC(self->priv->disc), address, &sector, &error)) {
        image_analyzer_read_sector_append_text(self, NULL, "Failed to get sector: %s\n", error->message);
        g_error_free(error);
        return;
    }

    /* Sector address */
    image_analyzer_read_sector_append_text(self, "tag_section", "Sector address: ");
    image_analyzer_read_sector_append_text(self, NULL, "%X (%d)\n", address, address);

    /* Sector address MSF */
    address_msf = mirage_helper_lba2msf_str(address, TRUE);
    image_analyzer_read_sector_append_text(self, "tag_section", "Sector address MSF: ");
    image_analyzer_read_sector_append_text(self, NULL, "%s\n", address_msf);
    g_free(address_msf);

    /* Sector type */
    mirage_sector_get_sector_type(MIRAGE_SECTOR(sector), &sector_type, NULL);
    image_analyzer_read_sector_append_text(self, "tag_section", "Sector type: ");
    image_analyzer_read_sector_append_text(self, NULL, "0x%X (%s)\n", sector_type, dump_sector_type(sector_type));

    image_analyzer_read_sector_append_text(self, NULL, "\n");

    /* DPM */
    if (mirage_disc_get_dpm_data_for_sector(MIRAGE_DISC(self->priv->disc), address, &dpm_angle, &dpm_density, NULL)) {
        image_analyzer_read_sector_append_text(self, "tag_section", "Sector angle: ");
        image_analyzer_read_sector_append_text(self, NULL, "%f rotations\n", dpm_angle);

        image_analyzer_read_sector_append_text(self, "tag_section", "Sector density: ");
        image_analyzer_read_sector_append_text(self, NULL, "%f degrees per sector\n", dpm_density);

        image_analyzer_read_sector_append_text(self, NULL, "\n");
    }

     /* PQ subchannel */
    image_analyzer_read_sector_append_text(self, "tag_section", "PQ subchannel:\n");
    mirage_sector_get_subchannel(MIRAGE_SECTOR(sector), MIRAGE_SUBCHANNEL_PQ, &tmp_buf, &tmp_len, NULL);
    image_analyzer_read_sector_append_sector_data(self, tmp_buf, tmp_len, NULL);
    image_analyzer_read_sector_append_text(self, NULL, "\n");

    /* Subchannel CRC verification */
    image_analyzer_read_sector_append_text(self, "tag_section", "Subchannel CRC verification: ");
    if (mirage_sector_verify_subchannel_crc(MIRAGE_SECTOR(sector))) {
        image_analyzer_read_sector_append_text(self, NULL, "passed\n");
    } else {
        image_analyzer_read_sector_append_text(self, NULL, "bad CRC\n");
    }
    image_analyzer_read_sector_append_text(self, NULL, "\n");


    /* L-EC verification */
    image_analyzer_read_sector_append_text(self, "tag_section", "Sector data L-EC verification: ");
    if (mirage_sector_verify_lec(MIRAGE_SECTOR(sector))) {
        image_analyzer_read_sector_append_text(self, NULL, "passed\n");
    } else {
        image_analyzer_read_sector_append_text(self, NULL, "bad sector\n");
    }
    image_analyzer_read_sector_append_text(self, NULL, "\n");


    /* All sector data */
    image_analyzer_read_sector_append_text(self, "tag_section", "Sector data dump:\n", address);

    /* Sync */
    mirage_sector_get_sync(MIRAGE_SECTOR(sector), &tmp_buf, &tmp_len, NULL);
    image_analyzer_read_sector_append_sector_data(self, tmp_buf, tmp_len, "tag_sync");
    /* Header */
    mirage_sector_get_header(MIRAGE_SECTOR(sector), &tmp_buf, &tmp_len, NULL);
    image_analyzer_read_sector_append_sector_data(self, tmp_buf, tmp_len, "tag_header");
    /* Subheader */
    mirage_sector_get_subheader(MIRAGE_SECTOR(sector), &tmp_buf, &tmp_len, NULL);
    image_analyzer_read_sector_append_sector_data(self, tmp_buf, tmp_len, "tag_subheader");
    /* Data */
    mirage_sector_get_data(MIRAGE_SECTOR(sector), &tmp_buf, &tmp_len, NULL);
    image_analyzer_read_sector_append_sector_data(self, tmp_buf, tmp_len, "tag_data");
    /* EDC/ECC */
    mirage_sector_get_edc_ecc(MIRAGE_SECTOR(sector), &tmp_buf, &tmp_len, NULL);
    image_analyzer_read_sector_append_sector_data(self, tmp_buf, tmp_len, "tag_edc_ecc");
    /* Subchannel */
    mirage_sector_get_subchannel(MIRAGE_SECTOR(sector), MIRAGE_SUBCHANNEL_PW, &tmp_buf, &tmp_len, NULL);
    image_analyzer_read_sector_append_sector_data(self, tmp_buf, tmp_len, "tag_subchannel");

    g_object_unref(sector);

    return;
}


/**********************************************************************\
 *                              GUI setup                             * 
\**********************************************************************/
static void setup_gui (IMAGE_ANALYZER_SectorRead *self)
{
    GtkWidget *vbox, *scrolledwindow, *hbox, *button;
    GtkAdjustment *adjustment;

    /* Window */
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
    self->priv->text_view = gtk_text_view_new();
    gtk_container_add(GTK_CONTAINER(scrolledwindow), self->priv->text_view);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(self->priv->text_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(self->priv->text_view), FALSE);

    self->priv->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(self->priv->text_view));
    gtk_text_buffer_create_tag(self->priv->buffer, "tag_section", "foreground", "#000000", "weight", PANGO_WEIGHT_BOLD, NULL);

    gtk_text_buffer_create_tag(self->priv->buffer, "tag_sync", "foreground", "#CC0033", "font", "fixed", NULL); /* Red */
    gtk_text_buffer_create_tag(self->priv->buffer, "tag_header", "foreground", "#33CC33", "font", "fixed", NULL); /* Green */
    gtk_text_buffer_create_tag(self->priv->buffer, "tag_subheader", "foreground", "#990099", "font", "fixed", NULL); /* Purple */
    gtk_text_buffer_create_tag(self->priv->buffer, "tag_data", "foreground", "#000000", "font", "fixed", NULL); /* Black */
    gtk_text_buffer_create_tag(self->priv->buffer, "tag_edc_ecc", "foreground", "#FF9933", "font", "fixed", NULL); /* Orange */
    gtk_text_buffer_create_tag(self->priv->buffer, "tag_subchannel", "foreground", "#0033FF", "font", "fixed", NULL); /* Blue */

    /* HBox */
    hbox = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    /* Spin button */
    adjustment = gtk_adjustment_new(0, G_MININT64, G_MAXINT64, 1, 75, 0);
    self->priv->spinbutton = gtk_spin_button_new(GTK_ADJUSTMENT(adjustment), 1, 0);
    gtk_box_pack_start(GTK_BOX(hbox), self->priv->spinbutton, TRUE, TRUE, 0);

    /* Button */
    button = gtk_button_new_with_label("Read");
    g_signal_connect(button, "clicked", G_CALLBACK(image_analyzer_sector_read_ui_callback_read), self);
    gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
}


/**********************************************************************\
 *                              Disc set                              * 
\**********************************************************************/
void image_analyzer_sector_read_set_disc (IMAGE_ANALYZER_SectorRead *self, GObject *disc)
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


/**********************************************************************\
 *                             Object init                            * 
\**********************************************************************/
G_DEFINE_TYPE(IMAGE_ANALYZER_SectorRead, image_analyzer_sector_read, GTK_TYPE_WINDOW);

static void image_analyzer_sector_read_init (IMAGE_ANALYZER_SectorRead *self)
{
    self->priv = IMAGE_ANALYZER_SECTOR_READ_GET_PRIVATE(self);

    self->priv->disc = NULL;

    setup_gui(self);
}

static void image_analyzer_sector_read_dispose (GObject *gobject)
{
    IMAGE_ANALYZER_SectorRead *self = IMAGE_ANALYZER_SECTOR_READ(gobject);

    /* Unref disc */
    if (self->priv->disc) {
        g_object_unref(self->priv->disc);
        self->priv->disc = NULL;
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(image_analyzer_sector_read_parent_class)->dispose(gobject);
}

static void image_analyzer_sector_read_class_init (IMAGE_ANALYZER_SectorReadClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = image_analyzer_sector_read_dispose;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(IMAGE_ANALYZER_SectorReadPrivate));
}
