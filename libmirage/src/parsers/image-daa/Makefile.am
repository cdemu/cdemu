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

plugin_LTLIBRARIES = image-daa.la

noinst_HEADERS = \
    image-daa.h \
    image-daa-fragment.h \
    image-daa-parser.h

image_daa_la_SOURCES = \
    image-daa-fragment.c \
    image-daa-parser.c \
    image-daa-plugin.c

image_daa_la_LIBADD = \
    $(glib_LIBS) \
    $(gobject_LIBS) \
    $(gmodule_LIBS) \
    $(gio_LIBS) \
    $(zlib_LIBS) \
    $(builddir)/lzma-sdk/liblzma-sdk.la

image_daa_la_LDFLAGS = \
    -module \
    -avoid-version


mimedir = $(datadir)/mime/packages
mime_DATA = libmirage-image-daa.xml

EXTRA_DIST = $(mime_DATA)
