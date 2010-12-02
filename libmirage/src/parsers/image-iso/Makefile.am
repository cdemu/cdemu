INCLUDES = \
    -I$(top_srcdir)/src/libmirage \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS)

plugindir = $(MIRAGE_PLUGIN_DIR)

plugin_LTLIBRARIES = image-iso.la

noinst_HEADERS = \
    image-iso.h \
    image-iso-parser.h

image_iso_la_SOURCES = \
    image-iso-parser.c \
    image-iso-plugin.c

image_iso_la_LIBADD = \
    $(glib_LIBS) \
    $(gobject_LIBS)

image_iso_la_LDFLAGS = \
    -module \
    -avoid-version
