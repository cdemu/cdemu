INCLUDES = \
    -I$(top_builddir)/include \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS) \
    $(gthread_CFLAGS) \
    $(gio_CFLAGS) \
    $(libmirage_CFLAGS) \
    $(libao_CFLAGS)

noinst_HEADERS = \
	cdemud-audio.h \
	cdemud-audio-private.h \
	cdemud-daemon.h \
	cdemud-daemon-private.h \
	cdemud-debug.h \
	cdemud-device.h \
	cdemud-device-private.h \
	cdemud-error.h \
	cdemud.h \
	cdemud-mmc-features.h \
	cdemud-mode-pages.h \
	cdemud-packet-commands.h \
	cdemud-sense-constants.h
    
bin_PROGRAMS = cdemud

cdemud_SOURCES = \
	cdemud-audio.c \
	cdemud-daemon.c \
	cdemud-daemon-dbus.c \
	cdemud-device.c \
	cdemud-device-commands.c \
	cdemud-device-delay.c \
	cdemud-device-features.c \
	cdemud-device-kernel-io.c \
	cdemud-device-load.c \
	cdemud-device-mapping.c \
	cdemud-device-mode-pages.c \
	cdemud-error.c \
	main.c

cdemud_LDADD = \
    $(glib_LIBS) \
    $(gobject_LIBS) \
    $(gthread_LIBS) \
    $(gio_LIBS) \
    $(libmirage_LIBS) \
    $(libao_LIBS)
