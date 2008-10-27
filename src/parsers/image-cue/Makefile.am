AM_YFLAGS = -d

BUILT_SOURCES = image-cue-yacc.h

INCLUDES = \
    -I$(top_srcdir)/src/libmirage \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS)

plugindir = $(MIRAGE_PLUGIN_DIR)

plugin_LTLIBRARIES = image-cue.la

noinst_HEADERS = \
    image-cue.h \
    image-cue-parser.h
    
image_cue_la_SOURCES = \
   image-cue-yacc.y \
   image-cue-lex.l \
   image-cue-parser.c \
   image-cue-plugin.c
   
image_cue_la_LIBADD = \
    $(glib_LIBS) \
    $(gobject_LIBS)

image_cue_la_LDFLAGS = \
    -module \
    -avoid-version


mimedir = $(datadir)/mime/packages
mime_DATA = libmirage-image-cue.xml

EXTRA_DIST = $(mime_DATA)
