/*
 *  CDEmuD: main
 *  Copyright (C) 2006-2007 Rok Mandeljc
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

#include <libdaemon/daemon.h>
#include "cdemud.h"

/* Log handler for daemon */
static void __cdemud_daemon_daemon_log_handler (const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer unused_data) {
    daemon_log(LOG_INFO, message);
}

/* Log handler for non-daemon */
static void __cdemud_daemon_local_log_handler (const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer unused_data) {
    g_print(message);
}

/* Signal handler */
static gboolean __cdemud_daemon_signal_handler (GIOChannel *source, GIOCondition condition, gpointer user_data) {
    CDEMUD_Daemon *self = CDEMUD_DAEMON(user_data);
    gint sig;
            
    /* Get signal */
    if ((sig = daemon_signal_next()) <= 0) {
        cdemud_daemon_stop_daemon(self, NULL);
    }
            
    /* Dispatch signal */
    switch (sig) {
        case SIGINT:
        case SIGQUIT: 
        case SIGTERM: {
            cdemud_daemon_stop_daemon(self, NULL);
            break;
        }
        case SIGHUP: {
            /* FIXME: nothing here yet */
            break;
        }
        default: {
            break;
        }
    }
    
    return TRUE;
}

/******************************************************************************\
 *                                Main function                               *
\******************************************************************************/
static gboolean daemon_kill = FALSE;
static gboolean daemonize = FALSE;
static gint num_devices = 1;
static gchar *ctl_device = "/dev/vhba_ctl";
static gchar *audio_backend = NULL;
static gchar *audio_device = NULL;
static gchar *bus = "system";

static GOptionEntry option_entries[] = {
    { "kill",         'k', 0, G_OPTION_ARG_NONE,   &daemon_kill,   "Kill daemon",        NULL },
    { "daemonize",    'd', 0, G_OPTION_ARG_NONE,   &daemonize,     "Daemonize",          NULL },
    { "num-devices",  'n', 0, G_OPTION_ARG_INT,    &num_devices,   "Number of devices",  "N" },
    { "ctl-device",   'c', 0, G_OPTION_ARG_STRING, &ctl_device,    "Control device",     "path" },
    { "audio",        'a', 0, G_OPTION_ARG_STRING, &audio_backend, "Audio play backend", "backend" },
    { "audio-device", 'o', 0, G_OPTION_ARG_STRING, &audio_device,  "Audio play device",  "device" },
    { "bus",          'b', 0, G_OPTION_ARG_STRING, &bus,           "Bus type to use",     "bus_type" },
    { NULL }
};

int main (int argc, char *argv[]) {    
    pid_t pid;
    
    /* Glib's commandline parser */
    GError *error = NULL;
    GOptionContext *option_context = NULL;
    gboolean succeeded = FALSE;
    
    option_context = g_option_context_new("- CDEmu Daemon");
    g_option_context_add_main_entries(option_context, option_entries, NULL);
    succeeded = g_option_context_parse(option_context, &argc, &argv, &error);
    g_option_context_free(option_context);
    
    if (!succeeded) {
        g_print("Failed to parse options: %s\n", error->message);
        g_error_free(error);
        return 1;
    }
    
    /* Set indetification string for the daemon for both syslog and PID file */
    daemon_pid_file_ident = daemon_log_ident = daemon_ident_from_argv0(argv[0]);
    
    /* Log handler */
    g_log_set_default_handler(__cdemud_daemon_local_log_handler, NULL);
    
    /* Check if we are called with -k parameter */
    if (daemon_kill) {
        gint ret;
        /* Kill daemon with SIGINT */
        if ((ret = daemon_pid_file_kill_wait(SIGINT, 5)) < 0) {
            g_print("Failed to kill daemon.\n");
        }
        
        return ret < 0 ? 1 : 0;
    }
    
    g_type_init();
    
    /* Now, either we're called in non-daemon/local or in daemon mode */
    if (daemonize) {
        /* *** Daemon mode *** */
        /* Basically, we hook libdaemon's functionality in here; as cdemud-daemon
           and cdemud-device part are free of anything specific to libdaemon, we
           need to install pid file, signal handler, appropriate I/O channel, and
           notification via daemon_log/syslog */
        gint signal_fd = 0;
        GIOChannel *signal_channel = NULL;
        
        /* Log handler */
        g_log_set_default_handler(__cdemud_daemon_daemon_log_handler, NULL);
                
        /* Check that the daemon is not rung twice a the same time */
        if ((pid = daemon_pid_file_is_running()) >= 0) {
            g_warning("Daemon already running on PID file %u\n", pid);
            return 1;
        }
        
        /* Prepare for return value passing from the initialization procedure of 
           the daemon process */
        daemon_retval_init();
        
        /* Do the fork */
        if ((pid = daemon_fork()) < 0) {
            /* Exit on error */
            daemon_retval_done();
            return 1;
        } else if (pid) { /* The parent */
            gint ret;
        
            /* Wait for 20 seconds for the return value passed from the daemon process */
            if ((ret = daemon_retval_wait(20)) < 0) {
                g_warning("Could not recieve return value from daemon process.\n");
                return 255;
            }
            
            if (ret) {
                g_warning("Daemon returned %i.", ret);
            } else {
                g_debug("Daemon returned %i.", ret);
            }

            return ret;
        } else {
            /* Create the PID file */
            if (daemon_pid_file_create() < 0) {
                g_warning("%s: Could not create PID file: %s.", __func__, strerror(errno));
                goto return_err;
            }
    
            /* Initialize signal handling */
            if (daemon_signal_init(SIGINT, SIGQUIT, SIGHUP, SIGTERM, 0) < 0) {
                g_warning("%s: Could not register signal handlers: %s.", __func__, strerror(errno));
                goto remove_pid;
            }
            
            /* Create daemon and initialize it */
            GError *error = NULL;
            GObject *obj = NULL;
            obj = g_object_new(CDEMUD_TYPE_DAEMON, NULL);
            
            /* Initialize daemon; when running in daemon mode, we -always- use system bus */
            if (!cdemud_daemon_initialize(CDEMUD_DAEMON(obj), num_devices, ctl_device, audio_backend, audio_device, TRUE, &error) < 0) {
                g_warning("Daemon initialization failed: %s\n", error->message);
                goto signal_done;
            }
            
            /* Create signal handle and put I/O channel on top of it */
            signal_fd = daemon_signal_fd();
            signal_channel = g_io_channel_unix_new(signal_fd);
            g_io_add_watch(signal_channel, G_IO_IN, __cdemud_daemon_signal_handler, CDEMUD_DAEMON(obj));
            
            /* Send OK to parent */
            daemon_retval_send(0);
            
            /* Start the daemon */
            if (!cdemud_daemon_start_daemon(CDEMUD_DAEMON(obj), &error)) {
                g_warning("Failed to start daemon: %s\n", error->message);
                goto signal_done;
            }
            /* *** */
            
            /* Close signal handle */
            daemon_signal_done();
            
            /* Remove PID file */
            daemon_pid_file_remove();
            
            g_debug("Goodbye, galaxy...\n");
            return 0;
            
            /* Error bail path */
signal_done:
            daemon_signal_done();
//destroy_obj:
            g_object_unref(obj);
remove_pid:
            daemon_pid_file_remove();
return_err:
            daemon_retval_send(1);
            return -1;
        }
    } else {
        /* *** Local service mode *** */
        GError *error = NULL;        
        GObject *obj = NULL;
        gboolean use_system_bus = TRUE;
        
        g_debug("Starting daemon locally with following parameters:\n"
                " - num_devices: %i\n"
                " - ctl_device: %s\n"
                " - audio_backend: %s\n"
                " - audio_device: %s\n"
                " - bus type: %s\n", num_devices, ctl_device, audio_backend, audio_device, bus);
        
        if (!mirage_helper_strcasecmp(bus, "system")) {
            use_system_bus = TRUE;
        } else if (!mirage_helper_strcasecmp(bus, "session")) {
            use_system_bus = FALSE;
        } else {
            g_warning("Invalid bus argument '%s', using default bus!\n", bus);
            use_system_bus = TRUE;
        }
        
        /* Create daemon */
        obj = g_object_new(CDEMUD_TYPE_DAEMON, NULL);

        /* Initialize daemon, passing commandline options to initialization */
        if (cdemud_daemon_initialize(CDEMUD_DAEMON(obj), num_devices, ctl_device, audio_backend, audio_device, use_system_bus, &error)) {
            /* Start the daemon */
            if (!cdemud_daemon_start_daemon(CDEMUD_DAEMON(obj), &error)) {
                g_warning("Failed to start daemon: %s\n", error->message);
                g_error_free(error);
            }
        } else {
            g_warning("Daemon initialization failed: %s\n", error->message);
            g_error_free(error);
        }
        g_object_unref(obj);
        return 0;
    }
    
    return 0;
}
