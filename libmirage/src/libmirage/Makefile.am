lib_LTLIBRARIES = libmirage.la

libmirage_la_SOURCES = \
    mirage.c \
    mirage-cdtext-encdec.c \
    mirage-debug.c \
    mirage-disc.c \
    mirage-error.c \
    mirage-file-filter.c \
    mirage-fragment.c \
    mirage-index.c \
    mirage-language.c \
    mirage-object.c \
    mirage-parser.c \
    mirage-plugin.c \
    mirage-sector.c \
    mirage-session.c \
    mirage-track.c \
    mirage-utils.c \
    mirage-version.c

AM_CPPFLAGS = \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS) \
    $(gmodule_CFLAGS) \
    $(gio_CFLAGS) \
    -DMIRAGE_PLUGIN_DIR=\"$(MIRAGE_PLUGIN_DIR)\"

libmirage_la_LIBADD = \
    $(glib_LIBS) \
    $(gobject_LIBS) \
    $(gmodule_LIBS) \
    $(gio_LIBS)

libmirage_la_LDFLAGS = \
	$(LT_VERSION_INFO)

libmirageincludedir=$(includedir)/libmirage
libmirageinclude_HEADERS = \
    mirage.h \
    mirage-cdtext-encdec.h \
    mirage-debug.h \
    mirage-disc.h \
    mirage-disc-structures.h \
    mirage-error.h \
    mirage-file-filter.h \
    mirage-fragment.h \
    mirage-index.h \
    mirage-language.h \
    mirage-object.h \
    mirage-parser.h \
    mirage-plugin.h \
    mirage-sector.h \
    mirage-session.h \
    mirage-track.h \
    mirage-utils.h \
    mirage-version.h

EXTRA_DIST = \
    mirage-version.h.in
