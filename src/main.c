/*
 *  CDEmuD: main
 *  Copyright (C) 2006-2008 Rok Mandeljc
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
        case SIGTERM:
        case SIGHUP: {
            cdemud_daemon_stop_daemon(self, NULL);
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
static gchar *audio_driver = "null";
static gchar *bus = "system";
static gchar *pid_file = NULL;

static GOptionEntry option_entries[] = {
    { "kill",         'k', 0, G_OPTION_ARG_NONE,   &daemon_kill,   "Kill daemon",        NULL },
    { "daemonize",    'd', 0, G_OPTION_ARG_NONE,   &daemonize,     "Daemonize",          NULL },
    { "num-devices",  'n', 0, G_OPTION_ARG_INT,    &num_devices,   "Number of devices",  "N" },
    { "ctl-device",   'c', 0, G_OPTION_ARG_STRING, &ctl_device,    "Control device",     "path" },
    { "audio-driver", 'a', 0, G_OPTION_ARG_STRING, &audio_driver,  "Audio driver",       "driver" },
    { "bus",          'b', 0, G_OPTION_ARG_STRING, &bus,           "Bus type to use",    "bus_type" },
    { "pidfile",      'p', 0, G_OPTION_ARG_STRING, &pid_file,      "PID file",           "path" },
    { NULL }
};

static const gchar *__cdemud_custom_pid_file (void) {
    return pid_file;
}

static gboolean __run_daemon () {
    gboolean succeeded = TRUE;
    
    GError *error = NULL;        
    GObject *obj = NULL;

    gboolean use_system_bus = TRUE;
    
    /* If ran in daemon mode, we always use system bus */
    if (daemonize) {
        bus = "system";
    }
    
    g_debug("Starting daemon in %s mode with following parameters:\n"
            " - num devices: %i\n"
            " - ctl device: %s\n"
            " - audio driver: %s\n"
            " - bus type: %s\n",
            daemonize ? "daemon" : "local", 
            num_devices, 
            ctl_device, 
            audio_driver, 
            bus);
    
    /* Decipher bus type */
    if (!mirage_helper_strcasecmp(bus, "system")) {
        use_system_bus = TRUE;
    } else if (!mirage_helper_strcasecmp(bus, "session")) {
        use_system_bus = FALSE;
    } else {
        g_warning("Invalid bus argument '%s', using default bus!\n", bus);
        use_system_bus = TRUE;
    }
    
    /* If ran in daemon mode, create PID file */
    if (daemonize) {
        if (daemon_pid_file_create() < 0) {
            g_warning("Could not create PID file: %s.", strerror(errno));
            daemon_retval_send(-1);
            return FALSE;
        }
    }
    
    /* Create daemon */
    obj = g_object_new(CDEMUD_TYPE_DAEMON, NULL);
    
    /* Initialize daemon */
    if (cdemud_daemon_initialize(CDEMUD_DAEMON(obj), num_devices, ctl_device, audio_driver, use_system_bus, &error)) {
        /* Initialize signal handling */
        if (daemon_signal_init(SIGINT, SIGQUIT, SIGHUP, SIGTERM, 0) == 0) {
            gint signal_fd = daemon_signal_fd();
            GIOChannel *signal_channel = g_io_channel_unix_new(signal_fd);
            
            g_io_add_watch(signal_channel, G_IO_IN, __cdemud_daemon_signal_handler, CDEMUD_DAEMON(obj));
            
            /* If ran in daemon mode, send signal to parent */
            if (daemonize) {
                daemon_retval_send(0);
            }
        
            /* Start the daemon */
            if (!cdemud_daemon_start_daemon(CDEMUD_DAEMON(obj), &error)) {
                g_warning("Failed to start daemon: %s\n", error->message);
                g_error_free(error);
                succeeded = FALSE;
            }
        
            /* Close signal handling */
            daemon_signal_done();
            g_io_channel_unref(signal_channel);
        } else {
            g_warning("Could not initialize signal handling: %s.", strerror(errno));
            daemon_retval_send(1);
            succeeded = FALSE;
        }
    } else {
        g_warning("Daemon initialization failed: %s\n", error->message);
        g_error_free(error);
        daemon_retval_send(1);
        succeeded = FALSE;
    }
    
    g_object_unref(obj);

    /* If ran in daemon mode, remove PID file */
    if (daemonize) {
        daemon_pid_file_remove();
    }
    
    return succeeded;
}

int main (int argc, char *argv[]) {    
    gint retval;
    pid_t pid;
    
    /* Glib's commandline parser */
    GError *error = NULL;
    GOptionContext *option_context = NULL;
    gboolean succeeded = FALSE;
        
    g_thread_init(NULL);
    
    option_context = g_option_context_new("- CDEmu Daemon");
    g_option_context_add_main_entries(option_context, option_entries, NULL);
    succeeded = g_option_context_parse(option_context, &argc, &argv, &error);
    g_option_context_free(option_context);
    
    if (!succeeded) {
        g_print("Failed to parse options: %s\n", error->message);
        g_error_free(error);
        return -1;
    }
    
    /* Set identification string for the daemon for both syslog and PID file */
    daemon_pid_file_ident = daemon_log_ident = daemon_ident_from_argv0(argv[0]);
    
    /* Override default PID file callback if requested */
    if (pid_file)
        daemon_pid_file_proc = __cdemud_custom_pid_file;
    
    /* Log handler */
    g_log_set_default_handler(__cdemud_daemon_local_log_handler, NULL);
    
    /* Check if we are called with -k parameter */
    if (daemon_kill) {
        /* Kill daemon with SIGINT */
        if ((retval = daemon_pid_file_kill_wait(SIGINT, 5)) < 0) {
            g_print("Failed to kill daemon.\n");
        }
        
        return retval;
    }
    
    g_type_init();
    if (!libmirage_init(&error)) {
        g_print("Failed to initialize libMirage: %s!\n", error->message);
        g_error_free(error);
        return -1;
    }
    
    /* Now, either we're called in non-daemon/local or in daemon mode */
    if (daemonize) {
        /* *** Daemon mode *** */
        
        /* Log handler */
        g_log_set_default_handler(__cdemud_daemon_daemon_log_handler, NULL);
                
        /* Check that the daemon is not rung twice a the same time */
        if ((pid = daemon_pid_file_is_running()) >= 0) {
            g_warning("Daemon already running on PID file %u\n", pid);
            return -1;
        }
        
        /* Prepare for return value passing from the initialization procedure of 
           the daemon process */
        daemon_retval_init();
        
        /* Do the fork */
        if ((pid = daemon_fork()) < 0) {
            /* Exit on error */
            daemon_retval_done();
            return -1;
        } else if (pid) { /* The parent */        
            /* Wait for 20 seconds for the return value passed from the daemon process */
            if ((retval = daemon_retval_wait(20)) < 0) {
                g_warning("Could not recieve return value from daemon process.\nCheck the system log for error messages.\n");
                return retval;
            }
            
            if (retval) {
                g_warning("Daemon returned %i.", retval);
            } else {
                g_debug("Daemon returned %i.", retval);
            }

            return retval;
        } else {           
            retval = __run_daemon(); 
            retval -= 1; /* True/False -> 0/-1 */
        }
    } else {
        /* *** Local mode *** */
        
        retval = __run_daemon(); 
        retval -= 1; /* True/False -> 0/-1 */
    }
    
    libmirage_shutdown(NULL);
    
    return retval;
}
