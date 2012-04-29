INCLUDES = \
    -I$(top_srcdir)/src/libmirage \
    -I$(top_builddir)/src/libmirage \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS)

plugindir = $(MIRAGE_PLUGIN_DIR)

plugin_LTLIBRARIES = image-toc.la

noinst_HEADERS = \
    image-toc.h \
    image-toc-parser.h

image_toc_la_SOURCES = \
    image-toc-parser.c \
    image-toc-plugin.c

image_toc_la_LIBADD = \
    $(glib_LIBS) \
    $(gobject_LIBS)

image_toc_la_LDFLAGS = \
    -module \
    -avoid-version
