SUBDIRS = lzma-sdk

INCLUDES = \
    -I$(top_srcdir)/src/libmirage \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS)
    
plugindir = $(MIRAGE_PLUGIN_DIR)

plugin_LTLIBRARIES = image-daa.la

noinst_HEADERS = \
    image-daa.h \
    image-daa-fragment.h \
    image-daa-disc.h

image_daa_la_SOURCES = \
    image-daa-disc.c \
    image-daa-fragment.c \
    image-daa-plugin.c

image_daa_la_LIBADD = \
    $(glib_LIBS) \
    $(gobject_LIBS) \
    $(zlib_LIBS)

image_daa_la_LDFLAGS = \
    -module \
    -avoid-version


mimedir = $(datadir)/mime/packages
mime_DATA = libmirage-image-daa.xml

EXTRA_DIST = $(mime_DATA)
