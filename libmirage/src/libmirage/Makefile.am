lib_LTLIBRARIES = libmirage.la

libmirage_la_SOURCES = \
    mirage.c \
    mirage-cdtext-coder.c \
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
    mirage-cdtext-coder.h \
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

CLEANFILES =

# GObject introspection
-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir) --warn-all --symbol-prefix=mirage --identifier-prefix=Mirage
INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir)

if HAVE_INTROSPECTION
introspection_sources = $(libmirage_la_SOURCES) $(libmirageinclude_HEADERS)

libMirage-1.0.gir: libmirage.la
libMirage_1_0_gir_INCLUDES = GObject-2.0 Gio-2.0 GLib-2.0 GModule-2.0
libMirage_1_0_gir_CFLAGS = $(INCLUDES)
libMirage_1_0_gir_LIBS = libmirage.la
libMirage_1_0_gir_FILES = $(introspection_sources)
libMirage_1_0_gir_VERSION = $(MIRAGE_MAJOR_VERSION).$(MIRAGE_MINOR_VERSION)
INTROSPECTION_GIRS += libMirage-1.0.gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(gir_DATA) $(typelib_DATA)
endif
