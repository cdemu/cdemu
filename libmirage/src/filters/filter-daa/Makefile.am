SUBDIRS = lzma-sdk

AM_CPPFLAGS = \
    -I$(top_srcdir)/src/libmirage \
    -I$(top_builddir)/src/libmirage \
    -I$(srcdir)/lzma-sdk \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS) \
    $(gmodule_CFLAGS) \
    $(gio_CFLAGS)

plugindir = $(MIRAGE_PLUGIN_DIR)

plugin_LTLIBRARIES = filter-daa.la

noinst_HEADERS = \
    filter-daa.h \
    filter-daa-file-filter.h

filter_daa_la_SOURCES = \
    filter-daa-file-filter.c \
    filter-daa-plugin.c

filter_daa_la_LIBADD = \
    $(glib_LIBS) \
    $(gobject_LIBS) \
    $(gmodule_LIBS) \
    $(gio_LIBS) \
    $(zlib_LIBS) \
    $(builddir)/lzma-sdk/liblzma-sdk.la

filter_daa_la_LDFLAGS = \
    -module \
    -avoid-version


mimedir = $(datadir)/mime/packages
mime_DATA = libmirage-filter-daa.xml

EXTRA_DIST = $(mime_DATA)
