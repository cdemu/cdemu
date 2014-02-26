/*
 *  Image Analyzer: Application object - private
 *  Copyright (C) 2012 Rok Mandeljc
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

#ifndef __IMAGE_ANALYZER_APPLICATION_PRIVATE_H__
#define __IMAGE_ANALYZER_APPLICATION_PRIVATE_H__

#define IA_APPLICATION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), IA_TYPE_APPLICATION, IaApplicationPrivate))

struct _IaApplicationPrivate
{
    /* Disc */
    gboolean loaded;
    MirageDisc *disc; /* Disc */

    /* Dialogs */
    GtkWidget *dialog_open_image;
    GtkWidget *dialog_image_writer;
    GtkWidget *dialog_open_dump;
    GtkWidget *dialog_save_dump;
    GtkWidget *dialog_log;
    GtkWidget *dialog_sector;
    GtkWidget *dialog_analysis;
    GtkWidget *dialog_topology;
    GtkWidget *dialog_structure;

    /* Window */
    GtkWidget *window;

    /* Status bar */
    GtkWidget *statusbar;
    guint context_id;

    /* Model */
    GtkTreeStore *treestore;
    xmlDocPtr xml_doc;

    /* Debug */
    MirageContext *mirage_context;
    gboolean debug_to_stdout;
};

void ia_application_create_xml_dump (IaApplication *self);
gboolean ia_application_display_xml_data (IaApplication *self);


#endif /* __IMAGE_ANALYZER_APPLICATION_PRIVATE_H__ */

