AM_CPPFLAGS = \
    -I$(top_srcdir)/src/libmirage \
    -I$(top_builddir)/src/libmirage \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS) \
    $(gmodule_CFLAGS) \
    $(gio_CFLAGS)

plugindir = $(MIRAGE_PLUGIN_DIR)

plugin_LTLIBRARIES = image-mdx.la

noinst_HEADERS = \
    image-mdx.h \
    image-mdx-parser.h

image_mdx_la_SOURCES = \
    image-mdx-parser.c \
    image-mdx-plugin.c

image_mdx_la_LIBADD = \
    $(glib_LIBS) \
    $(gobject_LIBS) \
    $(gmodule_LIBS) \
    $(gio_LIBS)

image_mdx_la_LDFLAGS = \
    -module \
    -avoid-version


mimedir = $(datadir)/mime/packages
mime_DATA = libmirage-image-mdx.xml

EXTRA_DIST = $(mime_DATA)
