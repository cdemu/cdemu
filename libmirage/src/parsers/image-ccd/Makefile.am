AM_YFLAGS = -d

BUILT_SOURCES = image-ccd-yacc.h

INCLUDES = \
    -I$(top_srcdir)/src/libmirage \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS)

plugindir = $(MIRAGE_PLUGIN_DIR)

plugin_LTLIBRARIES = image-ccd.la

noinst_HEADERS = \
    image-ccd.h \
    image-ccd-parser.h

image_ccd_la_SOURCES = \
   image-ccd-yacc.y \
   image-ccd-lex.l \
   image-ccd-parser.c \
   image-ccd-plugin.c
    
image_ccd_la_LIBADD = \
    $(glib_LIBS) \
    $(gobject_LIBS)

image_ccd_la_LDFLAGS = \
    -module \
    -avoid-version


mimedir = $(datadir)/mime/packages
mime_DATA = libmirage-image-ccd.xml

EXTRA_DIST = $(mime_DATA)
