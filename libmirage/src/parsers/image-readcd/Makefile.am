AM_CPPFLAGS = \
    -I$(top_srcdir)/src/libmirage \
    -I$(top_builddir)/src/libmirage \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS)

plugindir = $(MIRAGE_PLUGIN_DIR)

plugin_LTLIBRARIES = image-readcd.la

noinst_HEADERS = \
    image-readcd.h \
    image-readcd-parser.h

image_readcd_la_SOURCES = \
    image-readcd-parser.c \
    image-readcd-plugin.c

image_readcd_la_LIBADD = \
    $(glib_LIBS) \
    $(gobject_LIBS)

image_readcd_la_LDFLAGS = \
    -module \
    -avoid-version
