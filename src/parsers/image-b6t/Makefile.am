INCLUDES = \
    -I$(top_srcdir)/src/libmirage \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS)
    
plugindir = $(MIRAGE_PLUGIN_DIR)

plugin_LTLIBRARIES = image-b6t.la

noinst_HEADERS = \
    image-b6t.h \
    image-b6t-disc.h

image_b6t_la_SOURCES = \
    image-b6t-disc.c \
    image-b6t-plugin.c

image_b6t_la_LIBADD = \
    $(glib_LIBS) \
    $(gobject_LIBS)
    
image_b6t_la_LDFLAGS = \
    -module \
    -avoid-version


mimedir = $(datadir)/mime/packages
mime_DATA = libmirage-image-b6t.xml

EXTRA_DIST = $(mime_DATA)
