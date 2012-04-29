INCLUDES = \
    -I$(top_srcdir)/src/libmirage \
    -I$(top_builddir)/src/libmirage \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS)
    
plugindir = $(MIRAGE_PLUGIN_DIR)

plugin_LTLIBRARIES = image-mds.la

noinst_HEADERS = \
    image-mds.h \
    image-mds-parser.h

image_mds_la_SOURCES = \
    image-mds-parser.c \
    image-mds-plugin.c

image_mds_la_LIBADD = \
     $(glib_LIBS) \
     $(gobject_LIBS)

image_mds_la_LDFLAGS = \
    -module \
    -avoid-version


mimedir = $(datadir)/mime/packages
mime_DATA = libmirage-image-mds.xml

EXTRA_DIST = $(mime_DATA)
