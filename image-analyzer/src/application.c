/*
 *  Image analyzer: application
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
#include <mirage/mirage.h>

#include "application.h"
#include "application-window.h"


/**********************************************************************\
 *                            Private structure                       *
\**********************************************************************/
#define IA_APPLICATION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), IA_TYPE_APPLICATION, IaApplicationPrivate))

struct _IaApplicationPrivate
{
    gpointer dummy;
};

G_DEFINE_TYPE(IaApplication, ia_application, GTK_TYPE_APPLICATION);


/**********************************************************************\
 *                      Application-wide actions                      *
\**********************************************************************/
static void new_window_activated (GSimpleAction *action G_GNUC_UNUSED, GVariant *parameter G_GNUC_UNUSED, gpointer user_data)
{
    g_application_activate(G_APPLICATION(user_data));
}

static void about_activated (GSimpleAction *action G_GNUC_UNUSED, GVariant *parameter G_GNUC_UNUSED, gpointer user_data G_GNUC_UNUSED)
{
    gchar *authors[] = { "Rok Mandeljc <rok.mandeljc@gmail.com>", NULL };

    gtk_show_about_dialog(NULL,
        "name", "Image Analyzer",
        "comments", "Utility for CD/DVD image analysis and manipulation.",
        "version", IMAGE_ANALYZER_VERSION,
        "authors", authors,
        "copyright", "Copyright (C) 2007-2014 Rok Mandeljc",
        NULL);

    return;
}

static void quit_activated (GSimpleAction *action G_GNUC_UNUSED, GVariant *parameter G_GNUC_UNUSED, gpointer user_data)
{
    g_application_quit(G_APPLICATION(user_data));
}


static IaApplicationWindow *ia_application_create_window (IaApplication *self)
{
    /* Create new application window */
    IaApplicationWindow *window = g_object_new(IA_TYPE_APPLICATION_WINDOW, NULL);
    gtk_application_add_window(GTK_APPLICATION(self), GTK_WINDOW(window));
    gtk_widget_show_all(GTK_WIDGET(window));

    return window;
}


/**********************************************************************\
 *                          Gtk/GApplication                          *
\**********************************************************************/
static void ia_application_activate (GApplication *self)
{
    ia_application_create_window(IA_APPLICATION(self));
}

static GActionEntry app_entries[] = {
    { "new-window", new_window_activated, NULL, NULL, NULL, {0} },
    { "about", about_activated, NULL, NULL, NULL, {0} },
    { "quit", quit_activated, NULL, NULL, NULL, {0} },
};

static void ia_application_startup (GApplication *self)
{
    GtkBuilder *builder;

    /* Chain up to parent */
    G_APPLICATION_CLASS(ia_application_parent_class)->startup(self);

    /* Initialize libMirage */
    mirage_initialize(NULL);

    /* Actions for application menu */
    g_action_map_add_action_entries(G_ACTION_MAP(self), app_entries, G_N_ELEMENTS(app_entries), self);

    /* Menu bar and application menu */
    builder = gtk_builder_new();
    gtk_builder_add_from_string(builder,
        "<interface>"
        "   <menu id='menubar'>"
        "    <submenu>"
        "      <attribute name='label' translatable='yes'>Image</attribute>"
        "      <section>"
        "        <item>"
        "          <attribute name='label' translatable='yes'>Open image</attribute>"
        "          <attribute name='action'>win.open_image</attribute>"
        "          <attribute name='accel'>&lt;Primary&gt;o</attribute>"
        "        </item>"
        "      </section>"
        "      <section>"
        "        <item>"
        "          <attribute name='label' translatable='yes'>Convert image</attribute>"
        "          <attribute name='action'>win.convert_image</attribute>"
        "          <attribute name='accel'>&lt;Primary&gt;c</attribute>"
        "        </item>"
        "      </section>"
        "      <section>"
        "        <item>"
        "          <attribute name='label' translatable='yes'>Open dump</attribute>"
        "          <attribute name='action'>win.open_dump</attribute>"
        "          <attribute name='accel'>&lt;Primary&gt;d</attribute>"
        "        </item>"
        "        <item>"
        "          <attribute name='label' translatable='yes'>Save dump</attribute>"
        "          <attribute name='action'>win.save_dump</attribute>"
        "          <attribute name='accel'>&lt;Primary&gt;s</attribute>"
        "        </item>"
        "      </section>"
        "      <section>"
        "        <item>"
        "          <attribute name='label' translatable='yes'>Close</attribute>"
        "          <attribute name='action'>win.close</attribute>"
        "          <attribute name='accel'>&lt;Primary&gt;w</attribute>"
        "        </item>"
        "      </section>"
        "    </submenu>"
        "    <submenu>"
        "      <attribute name='label' translatable='yes'>Tools</attribute>"
        "      <section>"
        "        <item>"
        "          <attribute name='label' translatable='yes'>Log</attribute>"
        "          <attribute name='action'>win.log_window</attribute>"
        "          <attribute name='accel'>&lt;Primary&gt;l</attribute>"
        "        </item>"
        "        <item>"
        "          <attribute name='label' translatable='yes'>Read sector</attribute>"
        "          <attribute name='action'>win.read_sector_window</attribute>"
        "          <attribute name='accel'>&lt;Primary&gt;r</attribute>"
        "        </item>"
        "        <item>"
        "          <attribute name='label' translatable='yes'>Sector analysis</attribute>"
        "          <attribute name='action'>win.sector_analysis_window</attribute>"
        "          <attribute name='accel'>&lt;Primary&gt;a</attribute>"
        "        </item>"
        "        <item>"
        "          <attribute name='label' translatable='yes'>Disc topology</attribute>"
        "          <attribute name='action'>win.disc_topology_window</attribute>"
        "          <attribute name='accel'>&lt;Primary&gt;t</attribute>"
        "        </item>"
        "        <item>"
        "          <attribute name='label' translatable='yes'>Disc structures</attribute>"
        "          <attribute name='action'>win.disc_structures_window</attribute>"
        "          <attribute name='accel'>&lt;Primary&gt;c</attribute>"
        "        </item>"
        "      </section>"
        "    </submenu>"
        "    <submenu>"
        "      <attribute name='label' translatable='yes'>_Help</attribute>"
        "      <section>"
        "        <item>"
        "          <attribute name='label' translatable='yes'>About</attribute>"
        "          <attribute name='action'>app.about</attribute>"
        "        </item>"
        "      </section>"
        "    </submenu>"
        "  </menu>"
        "  <menu id='appmenu'>"
        "    <section>"
        "      <item>"
        "        <attribute name='label' translatable='yes'>New window</attribute>"
        "        <attribute name='action'>app.new-window</attribute>"
        "      </item>"
        "    </section>"
        "    <section>"
        "      <item>"
        "        <attribute name='label' translatable='yes'>About</attribute>"
        "        <attribute name='action'>app.about</attribute>"
        "      </item>"
        "    </section>"
        "    <section>"
        "      <item>"
        "        <attribute name='label' translatable='yes'>Quit</attribute>"
        "        <attribute name='action'>app.quit</attribute>"
        "      </item>"
        "    </section>"
        "  </menu>"
        "</interface>", -1, NULL);

    gtk_application_set_app_menu(GTK_APPLICATION(self), G_MENU_MODEL(gtk_builder_get_object(builder, "appmenu")));
    gtk_application_set_menubar(GTK_APPLICATION(self), G_MENU_MODEL(gtk_builder_get_object(builder, "menubar")));

    g_object_unref(builder);
}

static void ia_application_shutdown (GApplication *self)
{
    /* Shutdown libMirage */
    mirage_shutdown(NULL);

    /* Chain up to parent */
    G_APPLICATION_CLASS(ia_application_parent_class)->shutdown(self);
}


static int ia_application_command_line (GApplication *self, GApplicationCommandLine *cmdline)
{
    gint status = 0;

    /* Get args from GApplicationCommandLine; we have to make extra copy
       of the array, because g_option_context_parse() assumes it can
       remove strings from the array without freeing them */
    gint argc;
    gchar **args = g_application_command_line_get_arguments (cmdline, &argc);

    gchar **argv = g_new(gchar *, argc + 1);
    for (gint i = 0; i <= argc; i++) {
        argv[i] = args[i];
    }

    /* Command-line parser */
    gboolean debug_to_stdout = FALSE;
    gint debug_mask = MIRAGE_DEBUG_PARSER;
    gchar **filenames = NULL;
    gboolean help = FALSE;

    GOptionEntry entries[] = {
        { "debug-to-stdout", 's', 0, G_OPTION_ARG_NONE, &debug_to_stdout, "Print libMirage debug to stdout.", NULL },
        { "debug-mask", 'd', 0, G_OPTION_ARG_INT, &debug_mask, "libMirage debug mask.", NULL },
        { "help", '?', 0, G_OPTION_ARG_NONE, &help, NULL, NULL },
        { NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL, NULL }
    };

    GOptionContext *context = context = g_option_context_new (NULL);
    GError *error = NULL;

    g_option_context_set_help_enabled(context, FALSE);
    g_option_context_add_main_entries(context, entries, NULL);

    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_application_command_line_printerr(cmdline, "%s\n", error->message);
        g_error_free(error);
        status = -1;
    } else if (help) {
        gchar *text = g_option_context_get_help(context, FALSE, NULL);
        g_application_command_line_print(cmdline, "%s",  text);
        g_free(text);
        status = 1;
    }

    /* Copy filenames */
    filenames = g_new0(gchar *, argc); /* App name + filenames = filenames + NULL */
    for (gint i = 0; i < argc; i++) {
        filenames[i] = g_strdup(argv[i+1]);
    }

    /* Cleanup */
    g_free(argv);
    g_strfreev(args);

    g_option_context_free(context);

    /* If we successfully parsed command-line, create application window */
    if (!status) {
        /* Create new application window */
        IaApplicationWindow *window = ia_application_create_window(IA_APPLICATION(self));

        /* Set command-line options */
        ia_application_window_apply_command_line_options(window, debug_to_stdout, debug_mask, filenames);
    }

    g_strfreev(filenames);

    /* Return exit code */
    g_application_command_line_set_exit_status(cmdline, status);

    return status;
}



/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
static void ia_application_init (IaApplication *self)
{
    self->priv = IA_APPLICATION_GET_PRIVATE(self);

    g_signal_connect(self, "command-line", G_CALLBACK(ia_application_command_line), NULL);
}

static void ia_application_class_init (IaApplicationClass *class)
{
    GApplicationClass *application_class = G_APPLICATION_CLASS(class);

    application_class->startup = ia_application_startup;
    application_class->shutdown = ia_application_shutdown;
    application_class->activate = ia_application_activate;

    /* Register private structure */
    g_type_class_add_private(class, sizeof(IaApplicationPrivate));
}
