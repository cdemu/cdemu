/*
 *  Image Analyzer: Application object
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
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "image-analyzer-application.h"
#include "image-analyzer-application-private.h"

#include "image-analyzer-parser-log.h"
#include "image-analyzer-sector-analysis.h"
#include "image-analyzer-sector-read.h"
#include "image-analyzer-disc-topology.h"

#include "image-analyzer-dump.h"
#include "image-analyzer-xml-tags.h"


#define DEBUG_DOMAIN_PARSER "libMirage parser"


/**********************************************************************\
 *                           Logging redirection                      *
\**********************************************************************/
static void capture_parser_log (const gchar *log_domain G_GNUC_UNUSED, GLogLevelFlags log_level G_GNUC_UNUSED, const gchar *message, IMAGE_ANALYZER_Application *self)
{
    /* Append to our log string */
    self->priv->parser_log = g_string_append(self->priv->parser_log, message);
}


/**********************************************************************\
 *                              Status message                        *
\**********************************************************************/
static void image_analyzer_application_message (IMAGE_ANALYZER_Application *self, gchar *format, ...)
{
    gchar *message;
    va_list args;

    /* Pop message (so that anything set previously will be removed */
    gtk_statusbar_pop(GTK_STATUSBAR(self->priv->statusbar), self->priv->context_id);

    /* Push message */
    va_start(args, format);
    message = g_strdup_vprintf(format, args);
    va_end(args);

    gtk_statusbar_pop(GTK_STATUSBAR(self->priv->statusbar), self->priv->context_id);
    gtk_statusbar_push(GTK_STATUSBAR(self->priv->statusbar), self->priv->context_id, message);

    g_free(message);
}


/**********************************************************************\
 *                           Open/close image                         *
\**********************************************************************/
static gchar *image_analyzer_application_get_password (IMAGE_ANALYZER_Application *self)
{
    gchar *password;
    GtkDialog *dialog;
    GtkWidget *hbox, *entry, *label;
    gint result;

    dialog = GTK_DIALOG(gtk_dialog_new_with_buttons(
        "Enter password",
        GTK_WINDOW(self->priv->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_STOCK_OK, GTK_RESPONSE_OK,
        GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
        NULL));
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

    gtk_box_set_spacing(GTK_BOX(gtk_dialog_get_content_area(dialog)), 5);

    label = gtk_label_new("The image you are trying to load is encrypted.");
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(dialog)), label, TRUE, TRUE, 0);

    entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);

    hbox = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new("Password: "), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(dialog)), hbox, FALSE, FALSE, 0);

    /* Run dialog */
    gtk_widget_show_all(GTK_WIDGET(dialog));
    result = gtk_dialog_run(dialog);
    switch (result) {
        case GTK_RESPONSE_OK: {
            password = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
            break;
        }
        default: {
            password = NULL;
            break;
        }
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));

    return password;
}

static gboolean image_analyzer_application_close_image_or_dump (IMAGE_ANALYZER_Application *self)
{
    /* Clear log whether we're loaded or not... it doesn't really hurt to do it
       before the check, and it ensures the log is always cleared (i.e. if load
       call failed, we'd have error log but it wouldn't be cleared on subsequent
       load call... */
    image_analyzer_parser_log_clear_log(IMAGE_ANALYZER_PARSER_LOG(self->priv->dialog_parser));

    /* Clear disc reference in child windows */
    image_analyzer_disc_topology_set_disc(IMAGE_ANALYZER_DISC_TOPOLOGY(self->priv->dialog_topology), NULL);
    image_analyzer_sector_read_set_disc(IMAGE_ANALYZER_SECTOR_READ(self->priv->dialog_sector), NULL);
    image_analyzer_sector_analysis_set_disc(IMAGE_ANALYZER_SECTOR_ANALYSIS(self->priv->dialog_analysis), NULL);
    
    /* Clear TreeStore */
    gtk_tree_store_clear(self->priv->treestore);

    /* Free XML doc */
    if (self->priv->xml_doc) {
        xmlFreeDoc(self->priv->xml_doc);
        self->priv->xml_doc = NULL;
    }

    /* Release disc reference */
    if (self->priv->disc) {
        g_object_unref(self->priv->disc);
        self->priv->disc = NULL;
    }

    /* Print message only if something was loaded */
    if (self->priv->loaded) {
        image_analyzer_application_message(self, "Image/dump closed.");
    }

    self->priv->loaded = FALSE;

    return TRUE;
}

static gboolean image_analyzer_application_open_image (IMAGE_ANALYZER_Application *self, gchar **filenames)
{
    GObject *debug_context;
    guint log_handler;
    GError *error = NULL;

    /* Close any opened image or dump */
    image_analyzer_application_close_image_or_dump(self);

    /* Create debug context for disc */
    debug_context = g_object_new(MIRAGE_TYPE_DEBUG_CONTEXT, NULL);
    mirage_debug_context_set_domain(MIRAGE_DEBUG_CONTEXT(debug_context), DEBUG_DOMAIN_PARSER, NULL);
    mirage_debug_context_set_debug_mask(MIRAGE_DEBUG_CONTEXT(debug_context), MIRAGE_DEBUG_PARSER, NULL);

    /* Set log handler */
    self->priv->parser_log = g_string_new("");
    log_handler = g_log_set_handler(DEBUG_DOMAIN_PARSER, G_LOG_LEVEL_MASK, (GLogFunc)capture_parser_log, self);

    /* Create disc */
    self->priv->disc = libmirage_create_disc(filenames, debug_context, NULL, &error);
    if (!self->priv->disc) {
        g_warning("Failed to create disc: %s\n", error->message);
        image_analyzer_application_message(self, "Failed to open image: %s", error->message);
        g_error_free(error);

        /* Manually fill in the log */
        image_analyzer_parser_log_append_to_log(IMAGE_ANALYZER_PARSER_LOG(self->priv->dialog_parser), self->priv->parser_log->str);

        return FALSE;
    }

    /* Release reference to debug context; it's attached to disc now */
    g_object_unref(debug_context);

    /* Remove log handler */
    g_log_remove_handler(DEBUG_DOMAIN_PARSER, log_handler);

    /* Set disc reference in child windows */
    image_analyzer_disc_topology_set_disc(IMAGE_ANALYZER_DISC_TOPOLOGY(self->priv->dialog_topology), self->priv->disc);
    image_analyzer_sector_read_set_disc(IMAGE_ANALYZER_SECTOR_READ(self->priv->dialog_sector), self->priv->disc);
    image_analyzer_sector_analysis_set_disc(IMAGE_ANALYZER_SECTOR_ANALYSIS(self->priv->dialog_analysis), self->priv->disc);
    
    /* Make XML dump */
    image_analyzer_application_create_xml_dump(self);
    
    /* Free parser log string */
    g_string_free(self->priv->parser_log, TRUE);

    /* Display XML */
    image_analyzer_application_display_xml_data(self);

    self->priv->loaded = TRUE;

    image_analyzer_application_message(self, "Image successfully opened.");

    return TRUE;
}

static gboolean image_analyzer_application_open_dump (IMAGE_ANALYZER_Application *self, gchar *filename)
{
    /* Close any opened image or dump */
    image_analyzer_application_close_image_or_dump(self);

    /* Load XML */
    self->priv->xml_doc = xmlReadFile(filename, NULL, 0);
    if (!self->priv->xml_doc) {
        g_warning("Failed to dump disc!\n");
        image_analyzer_application_message(self, "Failed to load dump!");
        return FALSE;
    }

    /* Display XML */
    image_analyzer_application_display_xml_data(self);

    self->priv->loaded = TRUE;

    image_analyzer_application_message(self, "Dump successfully opened.");

    return TRUE;
}

static gboolean image_analyzer_application_save_dump (IMAGE_ANALYZER_Application *self, gchar *filename)
{
    /* Save the XML tree */
    xmlSaveFormatFileEnc(filename, self->priv->xml_doc, "UTF-8", 1);
    image_analyzer_application_message(self, "Dump successfully saved.");

    return TRUE;
}


/**********************************************************************\
 *                             UI callbacks                           *
\**********************************************************************/
static void ui_callback_open_image (GtkAction *action G_GNUC_UNUSED, IMAGE_ANALYZER_Application *self)
{
    /* Run the dialog */
    if (gtk_dialog_run(GTK_DIALOG(self->priv->dialog_open_image)) == GTK_RESPONSE_ACCEPT) {
        GSList *filenames_list;
        GSList *entry;
        gint num_filenames;
        gchar **filenames;
        gint i = 0;

        /* Get filenames from dialog */
        filenames_list = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(self->priv->dialog_open_image));

        /* Create strings array */
        num_filenames = g_slist_length(filenames_list);
        filenames = g_new0(gchar *, num_filenames + 1); /* NULL-terminated */

        /* GSList -> strings array conversion */
        entry = filenames_list;
        while (entry != NULL) {
            filenames[i++] = entry->data;
            entry = g_slist_next(entry);
        }

        /* Open image */
        image_analyzer_application_open_image(self, filenames);

        /* Free filenames list */
        g_slist_free(filenames_list);
        /* Free filenames array */
        g_strfreev(filenames);
    }

    gtk_widget_hide(GTK_WIDGET(self->priv->dialog_open_image));
}

static void ui_callback_open_dump (GtkAction *action G_GNUC_UNUSED, IMAGE_ANALYZER_Application *self)
{
    /* Run the dialog */
    if (gtk_dialog_run(GTK_DIALOG(self->priv->dialog_open_dump)) == GTK_RESPONSE_ACCEPT) {
        gchar *filename;

        /* Get filenames from dialog */
        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(self->priv->dialog_open_dump));

        /* Open dump */
        image_analyzer_application_open_dump(self, filename);

        /* Free filename */
        g_free(filename);
    }

    gtk_widget_hide(GTK_WIDGET(self->priv->dialog_open_dump));
}

static void ui_callback_save_dump (GtkAction *action G_GNUC_UNUSED, IMAGE_ANALYZER_Application *self)
{
    /* We need an opened image or dump for this */
    if (!self->priv->loaded) {
        return;
    }

    /* Run the dialog */
    if (gtk_dialog_run(GTK_DIALOG(self->priv->dialog_save_dump)) == GTK_RESPONSE_ACCEPT) {
        gchar *filename;

        /* Get filenames from dialog */
        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(self->priv->dialog_save_dump));

        /* Save */
        image_analyzer_application_save_dump(self, filename);

        /* Free filename */
        g_free(filename);
    }

    gtk_widget_hide(GTK_WIDGET(self->priv->dialog_save_dump));
}

static void ui_callback_close (GtkAction *action G_GNUC_UNUSED, IMAGE_ANALYZER_Application *self)
{
    image_analyzer_application_close_image_or_dump(self);
}

static void ui_callback_quit (GtkAction *action G_GNUC_UNUSED, gpointer user_data G_GNUC_UNUSED)
{
    gtk_main_quit();
}


static void ui_callback_parser_log (GtkAction *action G_GNUC_UNUSED, IMAGE_ANALYZER_Application *self)
{
    /* Make window (re)appear by first hiding, then showing it */
    gtk_widget_hide(self->priv->dialog_parser);
    gtk_widget_show_all(self->priv->dialog_parser);
}

static void ui_callback_sector (GtkAction *action G_GNUC_UNUSED, IMAGE_ANALYZER_Application *self)
{
    /* Make window (re)appear by first hiding, then showing it */
    gtk_widget_hide(self->priv->dialog_sector);
    gtk_widget_show_all(self->priv->dialog_sector);
}

static void ui_callback_analysis (GtkAction *action G_GNUC_UNUSED, IMAGE_ANALYZER_Application *self)
{
    /* Make window (re)appear by first hiding, then showing it */
    gtk_widget_hide(self->priv->dialog_analysis);
    gtk_widget_show_all(self->priv->dialog_analysis);
}

static void ui_callback_topology (GtkAction *action G_GNUC_UNUSED, IMAGE_ANALYZER_Application *self)
{
    /* Make window (re)appear by first hiding, then showing it */
    gtk_widget_hide(self->priv->dialog_topology);
    gtk_widget_show_all(self->priv->dialog_topology);
}

static void ui_callback_about (GtkAction *action G_GNUC_UNUSED, IMAGE_ANALYZER_Application *self)
{
    gchar *authors[] = { "Rok Mandeljc <rok.mandeljc@gmail.com>", NULL };

    gtk_show_about_dialog(
        GTK_WINDOW(self->priv->window),
        "name", "Image Analyzer",
        "comments", "Image Analyzer displays tree structure of disc image created by libMirage.",
        "version", PACKAGE_VERSION,
        "authors", authors,
        "copyright", "Copyright (C) 2007-2012 Rok Mandeljc",
        NULL);

    return;
}


static gboolean cb_window_delete_event (GtkWidget *widget G_GNUC_UNUSED, GdkEvent *event G_GNUC_UNUSED, gpointer user_data G_GNUC_UNUSED)
{
    /* Quit the app */
    gtk_main_quit();
    /* Don't invoke other handlers, we'll cleanup stuff ourselves */
    return TRUE;
}


/**********************************************************************\
 *                           GUI build helpers                        *
\**********************************************************************/
typedef struct
{
    GtkWidget *dialog;
    GtkFileFilter *all_images;
} IMAGE_ANALYZER_FilterContext;

static gboolean append_file_filter (MIRAGE_ParserInfo *parser_info, IMAGE_ANALYZER_FilterContext *context)
{
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, parser_info->description);

    /* Per-parser filter */
    gtk_file_filter_add_mime_type(filter, parser_info->mime_type);
    /* "All images" filter */
    gtk_file_filter_add_mime_type(context->all_images, parser_info->mime_type);

    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(context->dialog), filter);

    return TRUE;
}

static GtkWidget *build_dialog_open_image (IMAGE_ANALYZER_Application *self)
{
    IMAGE_ANALYZER_FilterContext context;

    GtkWidget *dialog;
    GtkFileFilter *filter;

    dialog = gtk_file_chooser_dialog_new(
        "Open File",
        GTK_WINDOW(self->priv->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
        NULL);

    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);

    /* "All files" filter */
    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "All files");
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    /* "All images" filter */
    filter= gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "All images");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    /* Per-parser filters */
    context.dialog = dialog;
    context.all_images = filter;
    libmirage_for_each_parser((MIRAGE_CallbackFunction)append_file_filter, &context, NULL);

    return dialog;
}

static GtkWidget *build_dialog_open_dump (IMAGE_ANALYZER_Application *self)
{
    GtkWidget *dialog;
    GtkFileFilter *filter;

    dialog = gtk_file_chooser_dialog_new(
        "Open File",
        GTK_WINDOW(self->priv->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
        NULL);

    /* "XML files" filter */
    filter= gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "XML files");
    gtk_file_filter_add_pattern(filter, "*.xml");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    /* "All files" filter */
    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "All files");
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    return dialog;
}

static GtkWidget *build_dialog_save_dump (IMAGE_ANALYZER_Application *self)
{
    GtkWidget *dialog;
    GtkFileFilter *filter;

    dialog = gtk_file_chooser_dialog_new(
        "Save File",
        GTK_WINDOW(self->priv->window),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
        NULL);

    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);

    /* "XML files" filter */
    filter= gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "XML files");
    gtk_file_filter_add_pattern(filter, "*.xml");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    /* "All files" filter */
    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "All files");
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    return dialog;
}

static GtkWidget *build_dialog_parser ()
{
    GtkWidget *dialog = g_object_new(IMAGE_ANALYZER_TYPE_PARSER_LOG, NULL);
    g_signal_connect(dialog, "delete_event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    return dialog;
}

static GtkWidget *build_dialog_sector ()
{
    GtkWidget *dialog = g_object_new(IMAGE_ANALYZER_TYPE_SECTOR_READ, NULL);
    g_signal_connect(dialog, "delete_event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    return dialog;
}

static GtkWidget *build_dialog_analysis ()
{
    GtkWidget *dialog = g_object_new(IMAGE_ANALYZER_TYPE_SECTOR_ANALYSIS, NULL);
    g_signal_connect(dialog, "delete_event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    return dialog;
}

static GtkWidget *build_dialog_topology ()
{
    GtkWidget *dialog = g_object_new(IMAGE_ANALYZER_TYPE_DISC_TOPOLOGY, NULL);
    g_signal_connect(dialog, "delete_event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    image_analyzer_disc_topology_set_disc(IMAGE_ANALYZER_DISC_TOPOLOGY(dialog), NULL);
    return dialog;
}

static GtkWidget *build_menu (IMAGE_ANALYZER_Application *self)
{
    static GtkActionEntry entries[] = {
        { "FileMenuAction", NULL, "_File", NULL, NULL, NULL },
        { "ImageMenuAction", NULL, "_Image", NULL, NULL, NULL },
        { "HelpMenuAction", NULL, "_Help", NULL, NULL, NULL },

        { "OpenImageAction", GTK_STOCK_OPEN, "_Open image", "<control>O", "Open image", G_CALLBACK(ui_callback_open_image) },
        { "OpenDumpAction", GTK_STOCK_OPEN, "Open _dump", "<control>D", "Open dump", G_CALLBACK(ui_callback_open_dump) },
        { "SaveDumpAction", GTK_STOCK_SAVE, "_Save dump", "<control>S", "Save dump", G_CALLBACK(ui_callback_save_dump) },
        { "CloseAction", GTK_STOCK_CLOSE, "_Close", "<control>W", "Close", G_CALLBACK(ui_callback_close) },
        { "QuitAction", GTK_STOCK_QUIT, "_Quit", "<control>Q", "Quit", G_CALLBACK(ui_callback_quit) },

        { "ParserLogAction", GTK_STOCK_DIALOG_INFO, "_Parser log", "<control>P", "Parser log", G_CALLBACK(ui_callback_parser_log) },
        { "SectorAction", GTK_STOCK_EXECUTE, "_Read sector", "<control>R", "Read sector", G_CALLBACK(ui_callback_sector) },
        { "AnalysisAction", GTK_STOCK_EXECUTE, "Sector _Analysis", "<control>A", "Sector analysis", G_CALLBACK(ui_callback_analysis) },
        { "TopologyAction", GTK_STOCK_EXECUTE, "Disc _topology", "<control>T", "Disc topology", G_CALLBACK(ui_callback_topology) },

        { "AboutAction", GTK_STOCK_ABOUT, "_About", NULL, "About", G_CALLBACK(ui_callback_about) },
    };

    static guint n_entries = G_N_ELEMENTS(entries);

    static gchar *ui_xml = "\
        <ui> \
            <menubar name='MenuBar'> \
                <menu name='FileMenu' action='FileMenuAction'> \
                    <menuitem name='Open image' action='OpenImageAction' /> \
                    <separator/> \
                    <menuitem name='Open dump' action='OpenDumpAction' /> \
                    <menuitem name='Save dump' action='SaveDumpAction' /> \
                    <separator/> \
                    <menuitem name='Close' action='CloseAction' /> \
                    <menuitem name='Quit' action='QuitAction' /> \
                </menu> \
                <menu name='Image' action='ImageMenuAction'> \
                    <menuitem name='Parser log' action='ParserLogAction' /> \
                    <menuitem name='Read sector' action='SectorAction' /> \
                    <menuitem name='Sector analysis' action='AnalysisAction' /> \
                    <menuitem name='Disc topology' action='TopologyAction' /> \
                </menu> \
                <menu name='HelpMenu' action='HelpMenuAction'> \
                    <menuitem name='About' action='AboutAction' /> \
                </menu> \
            </menubar> \
        </ui> \
        ";

    GtkActionGroup *actiongroup;
    GError *error = NULL;

    /* Action group */
    actiongroup = gtk_action_group_new("Image Analyzer");
    gtk_action_group_add_actions(actiongroup, entries, n_entries, self);
    gtk_ui_manager_insert_action_group(self->priv->ui_manager, actiongroup, 0);

    gtk_ui_manager_add_ui_from_string(self->priv->ui_manager, ui_xml, strlen(ui_xml), &error);
    if (error) {
        g_warning("Building menus failed: %s", error->message);
        g_error_free(error);
    }

    return gtk_ui_manager_get_widget(self->priv->ui_manager, "/MenuBar");
}

static GtkWidget *build_treeview (IMAGE_ANALYZER_Application *self)
{
    GtkWidget *treeview;

    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;

    /* GtkTreeView */
    treeview = gtk_tree_view_new();

    /* GktTreeViewColumn */
    column = gtk_tree_view_column_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    gtk_tree_view_column_add_attribute(column, renderer, "text", 0);

    /* GtkTreeStore */
    self->priv->treestore = gtk_tree_store_new(1, G_TYPE_STRING);
    gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), GTK_TREE_MODEL(self->priv->treestore));

    return treeview;
}


/**********************************************************************\
 *                              GUI setup                             * 
\**********************************************************************/
static void setup_gui (IMAGE_ANALYZER_Application *self)
{
    GtkWidget *vbox, *menubar, *scrolledwindow, *treeview;
    GtkAccelGroup *accel_group;
    
    /* UI manager */
    self->priv->ui_manager = gtk_ui_manager_new();

    /* Window */
    self->priv->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_signal_connect(self->priv->window, "delete_event", G_CALLBACK(cb_window_delete_event), self);
    gtk_window_set_title(GTK_WINDOW(self->priv->window), "Image analyzer");
    gtk_window_set_default_size(GTK_WINDOW(self->priv->window), 300, 400);
    gtk_container_set_border_width(GTK_CONTAINER(self->priv->window), 5);

    /* VBox */
    vbox = gtk_vbox_new(FALSE, 5);
    gtk_container_add(GTK_CONTAINER(self->priv->window), vbox);

    /* Menu */
    menubar = build_menu(self);
    gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 0);

    /* Scrolled window */
    scrolledwindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(vbox), scrolledwindow, TRUE, TRUE, 0);

    /* Tree view widget */
    treeview = build_treeview(self);
    gtk_container_add(GTK_CONTAINER(scrolledwindow), treeview);

    /* Status bar */
    self->priv->statusbar = gtk_statusbar_new();
    gtk_box_pack_start(GTK_BOX(vbox), self->priv->statusbar, FALSE, FALSE, 0);
    self->priv->context_id = gtk_statusbar_get_context_id(GTK_STATUSBAR(self->priv->statusbar), "Message");

    /* Dialogs */
    self->priv->dialog_open_image = build_dialog_open_image(self);
    self->priv->dialog_open_dump = build_dialog_open_dump(self);
    self->priv->dialog_save_dump = build_dialog_save_dump(self);
    self->priv->dialog_parser = build_dialog_parser();
    self->priv->dialog_sector = build_dialog_sector();
    self->priv->dialog_analysis = build_dialog_analysis();
    self->priv->dialog_topology = build_dialog_topology();

    /* Accelerator group */
    accel_group = gtk_ui_manager_get_accel_group(self->priv->ui_manager);
    gtk_window_add_accel_group(GTK_WINDOW(self->priv->window), accel_group);

    /* Set libMirage password function */
    libmirage_set_password_function((MIRAGE_PasswordFunction)image_analyzer_application_get_password, self, NULL);
}


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
gboolean image_analyzer_application_run (IMAGE_ANALYZER_Application *self, gchar **open_image)
{
    /* Open image, if provided */
    if (g_strv_length(open_image)) {
        /* If it ends with .xml, we treat it as a dump file */
        if (mirage_helper_has_suffix(open_image[0], ".xml")) {
            image_analyzer_application_open_dump(self, open_image[0]);
        } else {
            image_analyzer_application_open_image(self, open_image);
        }
    }

    /* Show window */
    gtk_widget_show_all(self->priv->window);

    /* GtkMain() */
    gtk_main();

    return TRUE;
}

gboolean image_analyzer_application_get_loaded_image (IMAGE_ANALYZER_Application *self, GObject **disc)
{
    if (!self->priv->loaded || !self->priv->disc) {
        return FALSE;
    }

    if (disc) {
        g_object_ref(self->priv->disc);
        *disc = self->priv->disc;
    }

    return TRUE;
}


/**********************************************************************\
 *                             Object init                            * 
\**********************************************************************/
G_DEFINE_TYPE(IMAGE_ANALYZER_Application, image_analyzer_application, G_TYPE_OBJECT);

static void image_analyzer_application_init (IMAGE_ANALYZER_Application *self)
{
    self->priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);

    self->priv->disc = NULL;

    setup_gui(self);
}

static void image_analyzer_application_finalize (GObject *gobject)
{
    IMAGE_ANALYZER_Application *self = IMAGE_ANALYZER_APPLICATION(gobject);

    /* Close image */
    image_analyzer_application_close_image_or_dump(self);

    gtk_widget_destroy(self->priv->window);

    gtk_widget_destroy(self->priv->dialog_open_image);
    gtk_widget_destroy(self->priv->dialog_open_dump);
    gtk_widget_destroy(self->priv->dialog_save_dump);
    gtk_widget_destroy(self->priv->dialog_parser);
    gtk_widget_destroy(self->priv->dialog_sector);
    gtk_widget_destroy(self->priv->dialog_analysis);
    gtk_widget_destroy(self->priv->dialog_topology);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(image_analyzer_application_parent_class)->finalize(gobject);
}

static void image_analyzer_application_class_init (IMAGE_ANALYZER_ApplicationClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = image_analyzer_application_finalize;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(IMAGE_ANALYZER_ApplicationPrivate));
}
