INCLUDES = \
    -I$(top_srcdir)/src/libmirage \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS)
    
plugindir = $(MIRAGE_PLUGIN_DIR)

plugin_LTLIBRARIES = image-cdi.la

noinst_HEADERS = \
    image-cdi.h \
    image-cdi-parser.h

image_cdi_la_SOURCES = \
    image-cdi-parser.c \
    image-cdi-plugin.c

image_cdi_la_LIBADD = \
    $(glib_LIBS) \
    $(gobject_LIBS)

image_cdi_la_LDFLAGS = \
    -module \
    -avoid-version


mimedir = $(datadir)/mime/packages
mime_DATA = libmirage-image-cdi.xml

EXTRA_DIST = $(mime_DATA)
