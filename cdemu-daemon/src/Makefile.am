INCLUDES = \
    -I$(top_builddir)/include \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS) \
    $(gthread_CFLAGS) \
    $(gio_CFLAGS) \
    $(libmirage_CFLAGS) \
    $(libao_CFLAGS)

noinst_HEADERS = \
    cdemud.h \
    cdemud-audio.h \
    cdemud-daemon.h \
    cdemud-debug.h \
    cdemud-device.h \
    cdemud-error.h \
    cdemud-mmc-features.h \
    cdemud-mode-pages.h \
    cdemud-packet-commands.h \
    cdemud-sense-constants.h
    
bin_PROGRAMS = cdemud

cdemud_SOURCES = \
    cdemud-audio.c \
    cdemud-daemon.c \
    cdemud-device.c \
    cdemud-error.c \
    main.c

cdemud_LDADD = \
    $(glib_LIBS) \
    $(gobject_LIBS) \
    $(gthread_LIBS) \
    $(gio_LIBS) \
    $(libmirage_LIBS) \
    $(libao_LIBS)
