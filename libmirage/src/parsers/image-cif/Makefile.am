AM_CPPFLAGS = \
    -I$(top_srcdir)/src/libmirage \
    -I$(top_builddir)/src/libmirage \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS)
    
plugindir = $(MIRAGE_PLUGIN_DIR)

plugin_LTLIBRARIES = image-cif.la

noinst_HEADERS = \
    image-cif.h \
    image-cif-parser.h

image_cif_la_SOURCES = \
    image-cif-parser.c \
    image-cif-plugin.c

image_cif_la_LIBADD = \
     $(glib_LIBS) \
     $(gobject_LIBS)

image_cif_la_LDFLAGS = \
    -module \
    -avoid-version


mimedir = $(datadir)/mime/packages
mime_DATA = libmirage-image-cif.xml

EXTRA_DIST = $(mime_DATA)
