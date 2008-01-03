/*
 *  MIRAGE Image Analyzer: Main
 *  Copyright (C) 2007 Rok Mandeljc
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
#include "image-analyzer-application.h"


/******************************************************************************\
 *                                Main function                               *
\******************************************************************************/
int main (int argc, char **argv) {
    GObject *application = NULL;
    GError *error = NULL;
    
    /* Initialize GType */
    g_type_init();
    
    /* Initialize Gtk */
    gtk_set_locale();
    gtk_init(&argc, &argv);
    
    /* Create application object */
    application = g_object_new(IMAGE_ANALYZER_TYPE_APPLICATION, NULL);
    
    /* Run application */
    if (!image_analyzer_application_run(IMAGE_ANALYZER_APPLICATION(application), &error)) {
        g_warning("Failed to run application: %s\n", error->message);
        g_error_free(error);
    }
        
    g_object_unref(application);
    
    return 0;
}
