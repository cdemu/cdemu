INCLUDES = \
    -I$(top_srcdir)/src/libmirage \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS)
    
plugindir = $(MIRAGE_PLUGIN_DIR)

plugin_LTLIBRARIES = image-uif.la

noinst_HEADERS = \
    image-uif.h \
    image-uif-parser.h

image_uif_la_SOURCES = \
    image-uif-parser.c \
    image-uif-plugin.c

image_uif_la_LIBADD = \
     $(glib_LIBS) \
     $(gobject_LIBS) \
     $(zlib_LIBS)

image_uif_la_LDFLAGS = \
    -module \
    -avoid-version


mimedir = $(datadir)/mime/packages
mime_DATA = libmirage-image-uif.xml

EXTRA_DIST = $(mime_DATA)
