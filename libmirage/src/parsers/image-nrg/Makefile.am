INCLUDES = \
    -I$(top_srcdir)/src/libmirage \
    -I$(top_builddir)/src/libmirage \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS)
    
plugindir = $(MIRAGE_PLUGIN_DIR)

plugin_LTLIBRARIES = image-nrg.la

noinst_HEADERS = \
    image-nrg.h \
    image-nrg-parser.h

image_nrg_la_SOURCES = \
    image-nrg-parser.c \
    image-nrg-plugin.c

image_nrg_la_LIBADD = \
    $(glib_LIBS) \
    $(gobject_LIBS)
    
image_nrg_la_LDFLAGS = \
    -module \
    -avoid-version


mimedir = $(datadir)/mime/packages
mime_DATA = libmirage-image-nrg.xml

EXTRA_DIST = $(mime_DATA)
