INCLUDES = \
    -I$(top_srcdir)/src/libmirage \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS)
    
plugindir = $(MIRAGE_PLUGIN_DIR)

plugin_LTLIBRARIES = image-c2d.la

noinst_HEADERS = \
    image-c2d.h \
    image-c2d-disc.h

image_c2d_la_SOURCES = \
    image-c2d-disc.c \
    image-c2d-plugin.c

image_c2d_la_LIBADD = \
     $(glib_LIBS) \
     $(gobject_LIBS)

image_c2d_la_LDFLAGS = \
    -module \
    -avoid-version


mimedir = $(datadir)/mime/packages
mime_DATA = libmirage-image-c2d.xml

EXTRA_DIST = $(mime_DATA)
