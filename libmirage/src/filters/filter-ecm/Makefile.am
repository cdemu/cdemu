AM_CPPFLAGS = \
    -I$(top_srcdir)/src/libmirage \
    -I$(top_builddir)/src/libmirage \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS)

plugindir = $(MIRAGE_PLUGIN_DIR)

plugin_LTLIBRARIES = filter-ecm.la

noinst_HEADERS = \
    filter-ecm.h \
    filter-ecm-file-filter.h

filter_ecm_la_SOURCES = \
   filter-ecm-file-filter.c \
   filter-ecm-plugin.c

ffilter_ecm_la_LIBADD = \
    $(glib_LIBS) \
    $(gobject_LIBS)

filter_ecm_la_LDFLAGS = \
    -module \
    -avoid-version
