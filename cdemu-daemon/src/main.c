/*
 *  CDEmu daemon: main
 *  Copyright (C) 2006-2012 Rok Mandeljc
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

#include "cdemu.h"

CdemuDaemon *daemon_obj;
FILE *logfile;

static gint num_devices = 1;
static gchar *ctl_device = "/dev/vhba_ctl";
static gchar *audio_driver = "null";
static gchar *bus = "session";
static gchar *log_filename = NULL;

static GOptionEntry option_entries[] = {
    { "num-devices", 'n', 0, G_OPTION_ARG_INT, &num_devices, "Number of devices", "N" },
    { "ctl-device", 'c', 0, G_OPTION_ARG_STRING, &ctl_device, "Control device", "path" },
    { "audio-driver", 'a', 0, G_OPTION_ARG_STRING, &audio_driver, "Audio driver", "driver" },
    { "bus", 'b', 0, G_OPTION_ARG_STRING, &bus, "Bus type to use", "bus_type" },
    { "logfile", 'l', 0, G_OPTION_ARG_STRING, &log_filename, "Logfile", "logfile" },
    { NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL, NULL }
};


/* Log handler: writing to stdout */
static void log_handler_stdout (const gchar *log_domain G_GNUC_UNUSED, GLogLevelFlags log_level G_GNUC_UNUSED, const gchar *message, gpointer unused_data G_GNUC_UNUSED)
{
    g_print("%s", message);
}

static void log_handler_logfile (const gchar *log_domain G_GNUC_UNUSED, GLogLevelFlags log_level G_GNUC_UNUSED, const gchar *message, gpointer unused_data G_GNUC_UNUSED)
{
    g_fprintf(logfile, "%s", message);
    fflush(logfile);
}


/* Signal handler */
static void __unix_signal_handler (int signal)
{
    g_message("Received signal - %s\n", g_strsignal(signal));
    switch (signal) {
        case SIGINT:
        case SIGQUIT:
        case SIGTERM:
        case SIGHUP: {
            cdemu_daemon_stop_daemon(daemon_obj);
            break;
        }
        default: {
            break;
        }
    }
}

static void setup_signal_trap ()
{
    struct sigaction action;

    action.sa_handler = __unix_signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    if (sigaction(SIGTERM, &action, 0) > 0) {
        g_warning("Failed to setup unix signal sigaction for SIGTERM!");
    }
    if (sigaction(SIGINT, &action, 0) > 0) {
        g_warning("Failed to setup unix signal sigaction for SIGINT!");
    }
    if (sigaction(SIGQUIT, &action, 0) > 0) {
        g_warning("Failed to setup unix signal sigaction for SIGQUIT!");
    }
    if (sigaction(SIGHUP, &action, 0) > 0) {
        g_warning("Failed to setup unix signal sigaction for SIGHUP!");
    }
}


/******************************************************************************\
 *                                Main function                               *
\******************************************************************************/
int main (int argc, char **argv)
{
    /* Glib type system and threading system initialization; needed 
       only in older glib versions */
#if !GLIB_CHECK_VERSION(2, 36, 0)
    g_type_init();
#endif
#if !GLIB_CHECK_VERSION(2, 32, 0)
    g_thread_init(NULL);
#endif

    /* Default log handler is local */
    g_log_set_default_handler(log_handler_stdout, NULL);

    /* Glib's commandline parser */
    GError *error = NULL;
    GOptionContext *option_context;
    gboolean succeeded;

    option_context = g_option_context_new("- CDEmu Daemon");
    g_option_context_add_main_entries(option_context, option_entries, NULL);
    succeeded = g_option_context_parse(option_context, &argc, &argv, &error);
    g_option_context_free(option_context);

    if (!succeeded) {
        g_warning("Failed to parse options: %s\n", error->message);
        g_error_free(error);
        return -1;
    }

    /* Set up logfile handler, if necessary */
    if (log_filename) {
        logfile = fopen(log_filename, "w"); /* Overwrite log file */
        if (!logfile) {
            g_warning("Failed to open log file %s for writing!\n", log_filename);
            return -1;
        }
        g_log_set_default_handler(log_handler_logfile, NULL);
    }

    /* Initialize libMirage */
    if (!mirage_initialize(&error)) {
        g_warning("Failed to initialize libMirage: %s!\n", error->message);
        g_error_free(error);
        return -1;
    }

    g_message("Starting CDEmu daemon with following parameters:\n");
    g_message(" - num devices: %i\n", num_devices);
    g_message(" - ctl device: %s\n", ctl_device);
    g_message(" - audio driver: %s\n", audio_driver);
    g_message(" - bus type: %s\n\n", bus);

    /* Decipher bus type */
    gboolean use_system_bus = FALSE;
    if (!mirage_helper_strcasecmp(bus, "system")) {
        use_system_bus = TRUE;
    } else if (!mirage_helper_strcasecmp(bus, "session")) {
        use_system_bus = FALSE;
    } else {
        g_warning("Invalid bus argument '%s', using default bus!\n", bus);
        use_system_bus = FALSE;
    }

    /* Discourage the use of system bus */
    if (use_system_bus) {
        g_message("WARNING: using CDEmu on system bus is deprecated and might lead to security issues on multi-user systems! Consult the README file for more details.\n\n");
    }

    /* Create daemon */
    daemon_obj = g_object_new(CDEMU_TYPE_DAEMON, NULL);

    /* Signal trapping */
    setup_signal_trap();

    /* Initialize and start daemon */
    if (cdemu_daemon_initialize_and_start(daemon_obj, num_devices, ctl_device, audio_driver, use_system_bus)) {
        /* Printed when daemon stops */
        g_message("Stopping daemon.\n");
    } else {
        g_warning("Daemon initialization and start failed!\n");
        succeeded = FALSE;
    }

    /* Release daemon object */
    g_object_unref(daemon_obj);

    /* Shutdown libMirage */
    mirage_shutdown(NULL);

    /* Close log file, if necessary */
    if (log_filename) {
        fclose(logfile);
    }

    return succeeded ? 0 : -1;
}
