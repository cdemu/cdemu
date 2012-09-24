/*
 *  Image Analyzer: Main
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
#include "mirage.h"
#include "image-analyzer-application.h"


/******************************************************************************\
 *                                Main function                               *
\******************************************************************************/
static gboolean debug_to_stdout = FALSE;
static gint debug_mask_initial = 0;

static GOptionEntry option_entries[] = {
    { "debug-to-stdout", 's', 0, G_OPTION_ARG_NONE, &debug_to_stdout, "Print libMirage debug to stdout as well.", NULL },
    { "debug-mask", 'd', 0, G_OPTION_ARG_INT, &debug_mask_initial, "Set libMirage debug mask.", NULL },
    { NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL, NULL }
};

int main (int argc, char **argv)
{
    GObject *application = NULL;
    GError *error = NULL;
    GOptionContext *option_context = NULL;
    gboolean succeeded = FALSE;

    gchar **open_image = NULL;

    /* Initialize GType */
    g_type_init();

    /* libMirage core object */
    if (!libmirage_init(&error)) {
        g_warning("Failed to initialize libMirage: %s!\n", error->message);
        g_error_free(error);
        return -1;
    }

    /* Parse command line */
    option_context = g_option_context_new("- Image Analyzer");
    g_option_context_add_main_entries(option_context, option_entries, NULL);
    g_option_context_add_group(option_context, gtk_get_option_group(TRUE));
    succeeded = g_option_context_parse(option_context, &argc, &argv, &error);
    g_option_context_free(option_context);

    if (!succeeded) {
        g_warning("Failed to parse options: %s\n", error->message);
        g_error_free(error);
        return -1;
    }

    /* Command-line parser has removed all options from argv; so it's just app
       name and image files now */
    open_image = g_new0(gchar *, argc); /* App name + filenames = filenames + NULL */
    for (gint i = 0; i < argc; i++) {
        open_image[i] = g_strdup(argv[i+1]);
    }

    /* Create application object */
    application = g_object_new(IMAGE_ANALYZER_TYPE_APPLICATION, NULL);

    /* Run application */
    if (!image_analyzer_application_run(IMAGE_ANALYZER_APPLICATION(application), open_image, debug_to_stdout, debug_mask_initial)) {
        g_warning("Failed to run application!\n");
    }

    g_object_unref(application);
    g_strfreev(open_image);

    libmirage_shutdown(NULL);

    return 0;
}
