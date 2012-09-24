INCLUDES = \
    -I$(top_builddir)/include \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS) \
    $(gthread_CFLAGS) \
    $(gio_CFLAGS) \
    $(libmirage_CFLAGS) \
    $(libao_CFLAGS)

noinst_HEADERS = \
	cdemu-audio.h \
	cdemu-audio-private.h \
	cdemu-daemon.h \
	cdemu-daemon-private.h \
	cdemu-debug.h \
	cdemu-device.h \
	cdemu-device-private.h \
	cdemu-error.h \
	cdemu.h \
	cdemu-mmc-features.h \
	cdemu-mode-pages.h \
	cdemu-packet-commands.h \
	cdemu-sense-constants.h

bin_PROGRAMS = cdemu-daemon

cdemu_daemon_SOURCES = \
	cdemu-audio.c \
	cdemu-daemon.c \
	cdemu-daemon-dbus.c \
	cdemu-device.c \
	cdemu-device-commands.c \
	cdemu-device-delay.c \
	cdemu-device-features.c \
	cdemu-device-kernel-io.c \
	cdemu-device-load.c \
	cdemu-device-mapping.c \
	cdemu-device-mode-pages.c \
	cdemu-error.c \
	main.c

cdemu_daemon_LDADD = \
    $(glib_LIBS) \
    $(gobject_LIBS) \
    $(gthread_LIBS) \
    $(gio_LIBS) \
    $(libmirage_LIBS) \
    $(libao_LIBS)
