AM_CPPFLAGS = \
    -I$(top_srcdir)/src/libmirage \
    -I$(top_builddir)/src/libmirage \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS) \
    $(gmodule_CFLAGS) \
    $(gio_CFLAGS) \
    $(liblzma_CFLAGS)

plugindir = $(MIRAGE_PLUGIN_DIR)

plugin_LTLIBRARIES = filter-xz.la

noinst_HEADERS = \
    filter-xz.h \
    filter-xz-file-filter.h

filter_xz_la_SOURCES = \
   filter-xz-file-filter.c \
   filter-xz-plugin.c

filter_xz_la_LIBADD = \
    $(glib_LIBS) \
    $(gobject_LIBS) \
    $(gmodule_LIBS) \
    $(gio_LIBS) \
    $(liblzma_LIBS)

filter_xz_la_LDFLAGS = \
    -module \
    -avoid-version
