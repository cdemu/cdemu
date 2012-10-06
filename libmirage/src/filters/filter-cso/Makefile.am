AM_CPPFLAGS = \
    -I$(top_srcdir)/src/libmirage \
    -I$(top_builddir)/src/libmirage \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS) \
    $(gmodule_CFLAGS) \
    $(gio_CFLAGS) \
    $(zlib_CFLAGS)

plugindir = $(MIRAGE_PLUGIN_DIR)

plugin_LTLIBRARIES = filter-cso.la

noinst_HEADERS = \
    filter-cso.h \
    filter-cso-file-filter.h

filter_cso_la_SOURCES = \
   filter-cso-file-filter.c \
   filter-cso-plugin.c

filter_cso_la_LIBADD = \
    $(glib_LIBS) \
    $(gobject_LIBS) \
    $(gmodule_LIBS) \
    $(gio_LIBS) \
    $(zlib_LIBS)

filter_cso_la_LDFLAGS = \
    -module \
    -avoid-version

mimedir = $(datadir)/mime/packages
mime_DATA = libmirage-filter-cso.xml

EXTRA_DIST = $(mime_DATA)
