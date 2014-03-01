/*
 *  Image Analyzer: Read sector window
 *  Copyright (C) 2007-2014 Rok Mandeljc
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
#include "read-sector-window.h"


/**********************************************************************\
 *                            Private structure                       *
\**********************************************************************/
#define IA_READ_SECTOR_WINDOW_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), IA_TYPE_READ_SECTOR_WINDOW, IaReadSectorWindowPrivate))

struct _IaReadSectorWindowPrivate
{
    /* Text entry */
    GtkWidget *text_view;
    GtkTextBuffer *buffer;

    GtkWidget *spinbutton;

    /* Disc */
    MirageDisc *disc;
};


/**********************************************************************\
 *                           Dump functions                           *
\**********************************************************************/
static gchar *dump_sector_type (gint sector_type)
{
    static DumpValue values[] = {
        VAL(MIRAGE_SECTOR_MODE0),
        VAL(MIRAGE_SECTOR_AUDIO),
        VAL(MIRAGE_SECTOR_MODE1),
        VAL(MIRAGE_SECTOR_MODE2),
        VAL(MIRAGE_SECTOR_MODE2_FORM1),
        VAL(MIRAGE_SECTOR_MODE2_FORM2),
    };

    return dump_value(sector_type, values, G_N_ELEMENTS(values));
}

/**********************************************************************\
 *                      Text buffer manipulation                      *
\**********************************************************************/
static gboolean ia_read_sector_clear_text (IaReadSectorWindow *self)
{
    gtk_text_buffer_set_text(self->priv->buffer, "", -1);
    return TRUE;
}

static gboolean ia_read_sector_append_text (IaReadSectorWindow *self, const gchar *tag_name, const gchar *format, ...)
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

static gboolean ia_read_sector_append_sector_data (IaReadSectorWindow *self, const guint8 *data, gint data_len, const gchar *tag_name)
{
    for (gint i = 0; i < data_len; i++) {
        ia_read_sector_append_text(self, tag_name, "%02hhX ", data[i]);
    }
    return TRUE;
}


/**********************************************************************\
 *                             UI callbacks                           *
\**********************************************************************/
static void ia_read_sector_window_ui_callback_read (GtkWidget *button G_GNUC_UNUSED, IaReadSectorWindow *self)
{
    MirageSector *sector;
    GError *error = NULL;
    gint address, sector_type;
    gchar *address_msf;

    gdouble dpm_angle, dpm_density;

    const guint8 *tmp_buf;
    gint tmp_len;

    /* Read address from spin button */
    address = gtk_spin_button_get_value(GTK_SPIN_BUTTON(self->priv->spinbutton));

    /* Clear buffer */
    ia_read_sector_clear_text(self);

    /* Get image */
    if (!self->priv->disc) {
        ia_read_sector_append_text(self, NULL, "No image loaded!\n");
        return;
    }

    /* Get sector from disc */
    sector = mirage_disc_get_sector(self->priv->disc, address, &error);
    if (!sector) {
        ia_read_sector_append_text(self, NULL, "Failed to get sector: %s\n", error->message);
        g_error_free(error);
        return;
    }

    /* Sector address */
    ia_read_sector_append_text(self, "tag_section", "Sector address: ");
    ia_read_sector_append_text(self, NULL, "0x%X (%d)\n", address, address);

    /* Sector address MSF */
    address_msf = mirage_helper_lba2msf_str(address, TRUE);
    ia_read_sector_append_text(self, "tag_section", "Sector address MSF: ");
    ia_read_sector_append_text(self, NULL, "%s\n", address_msf);
    g_free(address_msf);

    /* Sector type */
    sector_type = mirage_sector_get_sector_type(sector);
    ia_read_sector_append_text(self, "tag_section", "Sector type: ");
    ia_read_sector_append_text(self, NULL, "0x%X (%s)\n", sector_type, dump_sector_type(sector_type));

    ia_read_sector_append_text(self, NULL, "\n");

    /* DPM */
    if (mirage_disc_get_dpm_data_for_sector(self->priv->disc, address, &dpm_angle, &dpm_density, NULL)) {
        ia_read_sector_append_text(self, "tag_section", "Sector angle: ");
        ia_read_sector_append_text(self, NULL, "%f rotations\n", dpm_angle);

        ia_read_sector_append_text(self, "tag_section", "Sector density: ");
        ia_read_sector_append_text(self, NULL, "%f degrees per sector\n", dpm_density);

        ia_read_sector_append_text(self, NULL, "\n");
    }

     /* Q subchannel */
    ia_read_sector_append_text(self, "tag_section", "Q subchannel:\n");
    mirage_sector_get_subchannel(sector, MIRAGE_SUBCHANNEL_Q, &tmp_buf, &tmp_len, NULL);
    ia_read_sector_append_sector_data(self, tmp_buf, tmp_len, NULL);
    ia_read_sector_append_text(self, NULL, "\n");

    /* Subchannel CRC verification */
    ia_read_sector_append_text(self, "tag_section", "Subchannel CRC verification: ");
    if (mirage_sector_verify_subchannel_crc(sector)) {
        ia_read_sector_append_text(self, NULL, "passed\n");
    } else {
        ia_read_sector_append_text(self, NULL, "bad CRC\n");
    }
    ia_read_sector_append_text(self, NULL, "\n");


    /* L-EC verification */
    ia_read_sector_append_text(self, "tag_section", "Sector data L-EC verification: ");
    if (mirage_sector_verify_lec(sector)) {
        ia_read_sector_append_text(self, NULL, "passed\n");
    } else {
        ia_read_sector_append_text(self, NULL, "bad sector\n");
    }
    ia_read_sector_append_text(self, NULL, "\n");


    /* All sector data */
    ia_read_sector_append_text(self, "tag_section", "Sector data dump:\n", address);

    /* Sync */
    mirage_sector_get_sync(sector, &tmp_buf, &tmp_len, NULL);
    ia_read_sector_append_sector_data(self, tmp_buf, tmp_len, "tag_sync");
    /* Header */
    mirage_sector_get_header(sector, &tmp_buf, &tmp_len, NULL);
    ia_read_sector_append_sector_data(self, tmp_buf, tmp_len, "tag_header");
    /* Subheader */
    mirage_sector_get_subheader(sector, &tmp_buf, &tmp_len, NULL);
    ia_read_sector_append_sector_data(self, tmp_buf, tmp_len, "tag_subheader");
    /* Data */
    mirage_sector_get_data(sector, &tmp_buf, &tmp_len, NULL);
    ia_read_sector_append_sector_data(self, tmp_buf, tmp_len, "tag_data");
    /* EDC/ECC */
    mirage_sector_get_edc_ecc(sector, &tmp_buf, &tmp_len, NULL);
    ia_read_sector_append_sector_data(self, tmp_buf, tmp_len, "tag_edc_ecc");
    /* Subchannel */
    mirage_sector_get_subchannel(sector, MIRAGE_SUBCHANNEL_PW, &tmp_buf, &tmp_len, NULL);
    ia_read_sector_append_sector_data(self, tmp_buf, tmp_len, "tag_subchannel");

    g_object_unref(sector);

    return;
}


/**********************************************************************\
 *                              GUI setup                             *
\**********************************************************************/
static void setup_gui (IaReadSectorWindow *self)
{
    GtkWidget *grid, *scrolledwindow, *button;
    GtkAdjustment *adjustment;

    /* Window */
    gtk_window_set_title(GTK_WINDOW(self), "Read sector");
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

    gtk_text_buffer_create_tag(self->priv->buffer, "tag_sync", "foreground", "#CC0033", "family", "monospace", NULL); /* Red */
    gtk_text_buffer_create_tag(self->priv->buffer, "tag_header", "foreground", "#33CC33", "family", "monospace", NULL); /* Green */
    gtk_text_buffer_create_tag(self->priv->buffer, "tag_subheader", "foreground", "#990099", "family", "monospace", NULL); /* Purple */
    gtk_text_buffer_create_tag(self->priv->buffer, "tag_data", /*"foreground", "#000000",*/ "family", "monospace", NULL); /* Black */
    gtk_text_buffer_create_tag(self->priv->buffer, "tag_edc_ecc", "foreground", "#FF9933", "family", "monospace", NULL); /* Orange */
    gtk_text_buffer_create_tag(self->priv->buffer, "tag_subchannel", "foreground", "#0033FF", "family", "monospace", NULL); /* Blue */

    /* Spin button */
    adjustment = gtk_adjustment_new(0, G_MININT64, G_MAXINT64, 1, 75, 0);
    self->priv->spinbutton = gtk_spin_button_new(GTK_ADJUSTMENT(adjustment), 1, 0);
    gtk_widget_set_hexpand(self->priv->spinbutton, TRUE);
    gtk_grid_attach(GTK_GRID(grid), self->priv->spinbutton, 0, 1, 1, 1);

    /* Button */
    button = gtk_button_new_with_label("Read");
    g_signal_connect(button, "clicked", G_CALLBACK(ia_read_sector_window_ui_callback_read), self);
    gtk_grid_attach_next_to(GTK_GRID(grid), button, self->priv->spinbutton, GTK_POS_RIGHT, 1, 1);
}


/**********************************************************************\
 *                              Disc set                              *
\**********************************************************************/
void ia_read_sector_window_set_disc (IaReadSectorWindow *self, MirageDisc *disc)
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


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE(IaReadSectorWindow, ia_read_sector_window, GTK_TYPE_WINDOW);

static void ia_read_sector_window_init (IaReadSectorWindow *self)
{
    self->priv = IA_READ_SECTOR_WINDOW_GET_PRIVATE(self);

    self->priv->disc = NULL;

    setup_gui(self);
}

static void ia_read_sector_window_dispose (GObject *object)
{
    IaReadSectorWindow *self = IA_READ_SECTOR_WINDOW(object);

    /* Unref disc */
    if (self->priv->disc) {
        g_object_unref(self->priv->disc);
        self->priv->disc = NULL;
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(ia_read_sector_window_parent_class)->dispose(object);
}

static void ia_read_sector_window_class_init (IaReadSectorWindowClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = ia_read_sector_window_dispose;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(IaReadSectorWindowPrivate));
}
