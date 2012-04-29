INCLUDES = \
    -I$(top_srcdir)/src/libmirage \
    -I$(top_builddir)/src/libmirage \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS)

plugindir = $(MIRAGE_PLUGIN_DIR)

plugin_LTLIBRARIES = image-xcdroast.la

noinst_HEADERS = \
    image-xcdroast.h \
    image-xcdroast-parser.h

image_xcdroast_la_SOURCES = \
    image-xcdroast-parser.c \
    image-xcdroast-plugin.c
    
image_xcdroast_la_LIBADD = \
    $(glib_LIBS) \
    $(gobject_LIBS)

image_xcdroast_la_LDFLAGS = \
    -module \
    -avoid-version


mimedir = $(datadir)/mime/packages
mime_DATA = libmirage-image-xcdroast.xml

EXTRA_DIST = $(mime_DATA)
