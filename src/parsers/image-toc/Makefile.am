AM_YFLAGS = -d

BUILT_SOURCES = image-toc-parser.h

INCLUDES = \
    -I$(top_srcdir)/src/libmirage \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS)

plugindir = $(MIRAGE_PLUGIN_DIR)

plugin_LTLIBRARIES = image-toc.la

noinst_HEADERS = \
    image-toc.h \
    image-toc-disc.h \
    image-toc-session.h

image_toc_la_SOURCES = \
    image-toc-parser.y \
    image-toc-lexer.l \
    image-toc-disc.c \
    image-toc-plugin.c \
    image-toc-session.c
    
image_toc_la_LIBADD = \
    $(glib_LIBS) \
    $(gobject_LIBS)

image_toc_la_LDFLAGS = \
    -module \
    -avoid-version


mimedir = $(datadir)/mime/packages
mime_DATA = libmirage-image-toc.xml

EXTRA_DIST = $(mime_DATA)
