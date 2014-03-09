/*
 *  Image analyzer: main
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

#include <gtk/gtk.h>

#include "application.h"

/**********************************************************************\
 *                            Main function                           *
\**********************************************************************/
int main (int argc, char **argv)
{
    IaApplication *application;
    int status;

#if !GLIB_CHECK_VERSION(2,36,0)
    g_type_init();
#endif

    g_set_application_name("Image analyzer");

    application = g_object_new(IA_TYPE_APPLICATION,
        //"application-id", "net.sf.cdemu.ImageAnalyzer",
        "flags", G_APPLICATION_HANDLES_COMMAND_LINE,
        NULL);

    status = g_application_run(G_APPLICATION(application), argc, argv);

    g_object_unref(application);

    return status;
}
