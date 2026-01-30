/*
 *  CDEmu daemon: main
 *  Copyright (C) 2006-2026 Rok Mandeljc
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

#include <glib-unix.h>
#include "cdemu.h"


/* Log handler: writing to stdout */
static void _log_handler_stdout (const gchar *log_domain G_GNUC_UNUSED, GLogLevelFlags log_level G_GNUC_UNUSED, const gchar *message, gpointer unused_data G_GNUC_UNUSED)
{
    g_print("%s", message);
}

static void _log_handler_logfile (const gchar *log_domain G_GNUC_UNUSED, GLogLevelFlags log_level G_GNUC_UNUSED, const gchar *message, gpointer user_data)
{
    FILE *logfile = (FILE *)user_data;
    g_fprintf(logfile, "%s", message);
    fflush(logfile);
}


/* Signal handler */
static gboolean _signal_handler (gpointer user_data)
{
    CdemuDaemon *self = (CdemuDaemon*)user_data;
    cdemu_daemon_stop_daemon(self);
    return G_SOURCE_CONTINUE;
}


/* Program options */
static gint _get_config_int (GKeyFile *config, const gchar *group, const gchar *key)
{
    if (config) {
        GError *error = NULL;
        gint value = g_key_file_get_integer(config, group, key, &error);
        if (!error) {
            return value;
        }

        if (!g_error_matches(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
            g_warning(Q_("Failed to read integer (%s:%s) from config: %s\n"), group, key, error->message);
        }
        g_error_free(error);
    }
    return -1; /* Not available */
}

static gchar *_get_config_str (GKeyFile *config, const gchar *group, const gchar *key)
{
    if (config) {
        GError *error = NULL;
        gchar *value = g_key_file_get_string(config, group, key, &error);
        if (!error) {
            return value;
        }

        if (!g_error_matches(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
            g_warning(Q_("Failed to read string (%s:%s) from config: %s\n"), group, key, error->message);
        }
        g_error_free(error);
    }
    return NULL; /* Not available */
}

struct _ProgramOptions
{
    gchar *config_filename;
    gboolean config_file_exists;

    gchar *log_filename;

    gint num_devices;
    gchar *ctl_device;
    gchar *audio_driver;
    gchar *bus;
    gint cdemu_debug_mask;
    gint mirage_debug_mask;

    gint use_system_sleep_handler;
};


static gboolean _collect_program_options (int argc, char **argv, struct _ProgramOptions *options)
{
    GKeyFile *config_file = NULL;
    GError *error = NULL;
    GOptionContext *option_context;
    gboolean succeeded;

    const GOptionEntry option_entries[] = {
        {"config-file", 0, 0, G_OPTION_ARG_FILENAME, &options->config_filename, N_("Config file"), N_("filename")},
        {"num-devices", 'n', 0, G_OPTION_ARG_INT, &options->num_devices, N_("Number of devices"), N_("N")},
        {"ctl-device", 'c', 0, G_OPTION_ARG_STRING, &options->ctl_device, N_("Control device"), N_("path")},
        {"audio-driver", 'a', 0, G_OPTION_ARG_STRING, &options->audio_driver, N_("Audio driver"), N_("driver")},
        {"bus", 'b', 0, G_OPTION_ARG_STRING, &options->bus, N_("Bus type to use"), N_("bus_type")},
        {"logfile", 'l', 0, G_OPTION_ARG_FILENAME, &options->log_filename, N_("Logfile"), N_("logfile")},
        {"default-cdemu-debug-mask", 0, 0, G_OPTION_ARG_INT, &options->cdemu_debug_mask, N_("Default debug mask for CDEmu devices"), N_("mask")},
        {"default-mirage-debug-mask", 0, 0, G_OPTION_ARG_INT, &options->mirage_debug_mask, N_("Default debug mask for underlying libMirage"), N_("mask")},
        {"system-sleep-handler", 0, 0, G_OPTION_ARG_INT, &options->use_system_sleep_handler, N_("Enable system sleep handler to stop devices before system enters suspend/hibernation (0=disable, 1=enable)"), "0|1"},
        {NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL, NULL}
    };

    /* Initialize config struct; NULL for pointers and -1 for numeric
     * values means un-specified */
    options->config_filename = NULL;
    options->config_file_exists = FALSE;

    options->log_filename = NULL;

    options->num_devices = -1;
    options->ctl_device = NULL;
    options->audio_driver = NULL;
    options->bus = NULL;
    options->cdemu_debug_mask = -1;
    options->mirage_debug_mask = -1;
    options->use_system_sleep_handler = -1;

    /* Parse command-line */
    option_context = g_option_context_new(NULL);
    g_option_context_set_translation_domain(option_context, GETTEXT_PACKAGE);
    g_option_context_add_main_entries(option_context, option_entries, GETTEXT_PACKAGE);
    succeeded = g_option_context_parse(option_context, &argc, &argv, &error);
    g_option_context_free(option_context);

    if (!succeeded) {
        g_warning(Q_("Failed to parse options: %s\n"), error->message);
        g_error_free(error);
        return FALSE;
    }

    /* For options that can be specified both in config file and on
     * command-line, the latter takes precedence. If the option is not
     * specified in either way, fall back to default value. */

    /* Try loading the key/value file */
    if (options->config_filename) {
        options->config_file_exists = g_file_test(options->config_filename, G_FILE_TEST_IS_REGULAR);
        if (options->config_file_exists) {
            /* Create empty GKeyFile and load it from given file */
            config_file = g_key_file_new();
            succeeded = g_key_file_load_from_file(config_file, options->config_filename, G_KEY_FILE_NONE, &error);
            if (!succeeded) {
                g_warning(Q_("Failed to load config file '%s': %s\n"), options->config_filename, error->message);
                g_key_file_free(config_file);
                g_error_free(error);
                return FALSE;
            }
        }
    }

    /* Number of devices */
    if (options->num_devices < 0) {
        gint value = _get_config_int(config_file, "settings", "num-devices");
        if (value < 0) {
            options->num_devices = 1; /* Default */
        } else {
            options->num_devices = value;
        }
    }

    /* Control device */
    if (options->ctl_device == NULL) {
        gchar *value = _get_config_str(config_file, "settings", "ctl-device");
        if (value == NULL) {
            options->ctl_device = g_strdup("/dev/vhba_ctl"); /* Default */
        } else {
            options->ctl_device = value;
        }
    }

    /* Audio driver */
    if (options->audio_driver == NULL) {
        gchar *value = _get_config_str(config_file, "settings", "audio-driver");
        if (value == NULL) {
            options->audio_driver = g_strdup("null"); /* Default */
        } else {
            options->audio_driver = value;
        }
    }

    /* Bus */
    if (options->bus == NULL) {
        gchar *value = _get_config_str(config_file, "settings", "bus");
        if (value == NULL) {
            options->bus = g_strdup("session"); /* Default */
        } else {
            options->bus = value;
        }
    }

    /* Log filename */
    if (options->log_filename == NULL) {
        gchar *value = _get_config_str(config_file, "settings", "logfile");
        if (value == NULL) {
            options->log_filename = NULL; /* Default */
        } else {
            options->log_filename = value;
        }
    }

    /* CDEmu debug mask */
    if (options->cdemu_debug_mask == -1) {
        gint value = _get_config_int(config_file, "settings", "default-cdemu-debug-mask");
        if (value < 0) {
            options->cdemu_debug_mask = 0; /* Default */
        } else {
            options->cdemu_debug_mask = value;
        }
    }

    /* libMirage debug mask */
    if (options->mirage_debug_mask == -1) {
        gint value = _get_config_int(config_file, "settings", "default-mirage-debug-mask");
        if (value < 0) {
            options->mirage_debug_mask = 0; /* Default */
        } else {
            options->mirage_debug_mask = value;
        }
    }

    /* Enable system sleep handler */
    if (options->use_system_sleep_handler == -1) {
        gint value = _get_config_int(config_file, "settings", "system-sleep-handler");
        if (value < 0) {
            options->use_system_sleep_handler = 1; /* Default: 1 (= enable) */
        } else {
            options->use_system_sleep_handler = value;
        }
    }

    /* Free the config file */
    if (config_file) {
        g_key_file_free(config_file);
    }

    return TRUE;
}


/******************************************************************************\
 *                                Main function                               *
\******************************************************************************/
int main (int argc, char **argv)
{
    GError *error = NULL;
    gboolean succeeded = TRUE;

    CdemuDaemon *daemon_obj = NULL;
    FILE *logfile = NULL;

    struct _ProgramOptions program_options;
    CdemuDaemonSettings daemon_settings;

    /* Localization support */
    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);

    /* Default log handler is local */
    g_log_set_default_handler(_log_handler_stdout, NULL);

    /* Collect program options from command-line and config file */
    if (!_collect_program_options(argc, argv, &program_options)) {
        return -1;
    }

    /* Set up logfile handler, if necessary */
    if (program_options.log_filename) {
        logfile = g_fopen(program_options.log_filename, "w"); /* Overwrite log file */
        if (!logfile) {
            g_warning(Q_("Failed to open log file %s for writing!\n"), program_options.log_filename);
            return -1;
        }
        g_log_set_default_handler(_log_handler_logfile, logfile);
    }

    /* Initialize libMirage */
    if (!mirage_initialize(&error)) {
        g_warning(Q_("Failed to initialize libMirage: %s!\n"), error->message);
        g_error_free(error);
        return -1;
    }

    /* Display status */
    g_message(Q_("Starting CDEmu daemon with following parameters:\n"));
    if (program_options.config_filename) {
        g_message(Q_(" - config file: %s (exists: %d)\n"), program_options.config_filename, program_options.config_file_exists);
    } else {
        g_message(Q_(" - config file: N/A\n"));
    }
    g_message(Q_(" - num devices: %i\n"), program_options.num_devices);
    g_message(Q_(" - control device: %s\n"), program_options.ctl_device);
    g_message(Q_(" - audio driver: %s\n"), program_options.audio_driver);
    g_message(Q_(" - bus type: %s\n"), program_options.bus);
    g_message(Q_(" - default CDEmu debug mask: 0x%X\n"), program_options.cdemu_debug_mask);
    g_message(Q_(" - default libMirage debug mask: 0x%X\n"), program_options.mirage_debug_mask);
    g_message(Q_(" - enable system sleep handler: %d\n"), program_options.use_system_sleep_handler);
    g_message("\n");

    /* Prepare daemon settings structure */
    if (!mirage_helper_strcasecmp(program_options.bus, "system")) {
        daemon_settings.bus_type = G_BUS_TYPE_SYSTEM;
    } else if (!mirage_helper_strcasecmp(program_options.bus, "session")) {
        daemon_settings.bus_type = G_BUS_TYPE_SESSION;
    } else {
        g_warning(Q_("Invalid bus argument '%s', using default bus!\n"), program_options.bus);
        daemon_settings.bus_type = G_BUS_TYPE_SESSION;
    }

    daemon_settings.ctl_device = program_options.ctl_device; /* no ownership transfer! */
    daemon_settings.audio_driver = program_options.audio_driver; /* no ownership transfer! */

    daemon_settings.num_devices = program_options.num_devices;

    daemon_settings.cdemu_debug_mask = program_options.cdemu_debug_mask;
    daemon_settings.mirage_debug_mask = program_options.mirage_debug_mask;

    daemon_settings.use_system_sleep_handler = program_options.use_system_sleep_handler != 0;

    /* Discourage the use of system bus */
    if (daemon_settings.bus_type == G_BUS_TYPE_SYSTEM) {
        g_message(Q_("WARNING: using CDEmu on system bus is deprecated and might lead to security issues on multi-user systems! Consult the README file for more details.\n\n"));
    }

    /* Create daemon */
    daemon_obj = g_object_new(CDEMU_TYPE_DAEMON, NULL);

    /* Signal trapping */
    if (g_unix_signal_add(SIGTERM, _signal_handler, daemon_obj) <= 0) {
        g_warning(Q_("Failed to add signal handler for SIGTERM!\n"));
    }
    if (g_unix_signal_add(SIGINT, _signal_handler, daemon_obj) <= 0) {
        g_warning(Q_("Failed to add signal handler for SIGINT!\n"));
    }
    /* SIGQUIT not supported by g_unix_signal_source_new */
    if (g_unix_signal_add(SIGHUP, _signal_handler, daemon_obj) <= 0) {
        g_warning(Q_("Failed to add signal handler for SIGHUP!\n"));
    }

    /* Initialize and start daemon */
    if (cdemu_daemon_initialize_and_start(daemon_obj, &daemon_settings)) {
        /* Printed when daemon stops */
        g_message(Q_("Stopping daemon.\n"));
    } else {
        g_warning(Q_("Daemon initialization and start failed!\n"));
        succeeded = FALSE;
    }

    /* Release daemon object */
    g_object_unref(daemon_obj);

    /* Shutdown libMirage */
    mirage_shutdown(NULL);

    /* Close log file, if necessary */
    if (logfile) {
        fclose(logfile);
    }

    /* Clean-up string config variables */
    g_free(program_options.config_filename);
    g_free(program_options.ctl_device);
    g_free(program_options.audio_driver);
    g_free(program_options.bus);
    g_free(program_options.log_filename);

    return succeeded ? 0 : -1;
}
