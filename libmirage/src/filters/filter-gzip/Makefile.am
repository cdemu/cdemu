AM_CPPFLAGS = \
    -I$(top_srcdir)/src/libmirage \
    -I$(top_builddir)/src/libmirage \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS) \
    $(gmodule_CFLAGS) \
    $(gio_CFLAGS) \
    $(zlib_CFLAGS)

plugindir = $(MIRAGE_PLUGIN_DIR)

plugin_LTLIBRARIES = filter-gzip.la

noinst_HEADERS = \
    filter-gzip.h \
    filter-gzip-file-filter.h

filter_gzip_la_SOURCES = \
   filter-gzip-file-filter.c \
   filter-gzip-plugin.c

filter_gzip_la_LIBADD = \
    $(glib_LIBS) \
    $(gobject_LIBS) \
    $(gmodule_LIBS) \
    $(gio_LIBS) \
    $(zlib_LIBS)

filter_gzip_la_LDFLAGS = \
    -module \
    -avoid-version
