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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __IMAGE_ANALYZER_APPLICATION_PRIVATE_H__
#define __IMAGE_ANALYZER_APPLICATION_PRIVATE_H__

#define IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), IMAGE_ANALYZER_TYPE_APPLICATION, IMAGE_ANALYZER_ApplicationPrivate))

struct _IMAGE_ANALYZER_ApplicationPrivate
{
    /* Disc */
    gboolean loaded;
    GObject *disc; /* Disc */

    /* Dialogs */
    GtkWidget *dialog_open_image;
    GtkWidget *dialog_open_dump;
    GtkWidget *dialog_save_dump;
    GtkWidget *dialog_parser;
    GtkWidget *dialog_sector;
    GtkWidget *dialog_analysis;
    GtkWidget *dialog_topology;

    /* Window */
    GtkWidget *window;

    /* UI Manager */
    GtkUIManager *ui_manager;

    /* Status bar */
    GtkWidget *statusbar;
    guint context_id;

    /* Model */
    GString *parser_log;
    GtkTreeStore *treestore;
    xmlDocPtr xml_doc;

    /* Debug */
    gboolean debug_stdout;
};

void image_analyzer_application_create_xml_dump (IMAGE_ANALYZER_Application *self);
gboolean image_analyzer_application_display_xml_data (IMAGE_ANALYZER_Application *self);


#endif /* __IMAGE_ANALYZER_APPLICATION_PRIVATE_H__ */

