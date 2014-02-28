/*
 *  Image Analyzer: Application
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
 *                      Debug and logging redirection                 *
\**********************************************************************/
#if 0
static void capture_log (const gchar *log_domain G_GNUC_UNUSED, GLogLevelFlags log_level, const gchar *message, IaApplication *self)
{
    /* Append to log */
    ia_log_window_append_to_log(IA_LOG_WINDOW(self->priv->dialog_log), message);

    /* Errors, critical errors and warnings are always printed to stdout */
    if (log_level & G_LOG_LEVEL_ERROR) {
        g_print("ERROR: %s", message);
        return;
    }
    if (log_level & G_LOG_LEVEL_CRITICAL) {
        g_print("CRITICAL: %s", message);
        return;
    }
    if (log_level & G_LOG_LEVEL_WARNING) {
        g_print("WARNING: %s", message);
        return;
    }

    /* Debug messages are printed to stdout only if user requested so */
    if (self->priv->debug_to_stdout) {
        g_print("%s", message);
    }
}
#endif

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


/**********************************************************************\
 *                          Gtk/GApplication                          *
\**********************************************************************/
static void ia_application_activate (GApplication *self)
{
    /* Create new application window */
    IaApplicationWindow *window = g_object_new(IA_TYPE_APPLICATION_WINDOW, NULL);
    gtk_application_add_window(GTK_APPLICATION(self), GTK_WINDOW(window));
    gtk_widget_show_all(GTK_WIDGET(window));
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
        "          <attribute name='action'>win.disc_structure_window</attribute>"
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



/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
static void ia_application_init (IaApplication *self)
{
    self->priv = IA_APPLICATION_GET_PRIVATE(self);
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
