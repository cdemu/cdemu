AM_YFLAGS = -d

BUILT_SOURCES = image-cue-parser.h

INCLUDES = \
    -I$(top_srcdir)/src/libmirage \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS) \
    $(gmodule_CFLAGS)

plugindir = $(MIRAGE_PLUGIN_DIR)

plugin_LTLIBRARIES = image-cue.la

noinst_HEADERS = \
    image-cue.h \
    image-cue-disc.h \
    image-cue-session.h

image_cue_la_SOURCES = \
   image-cue-parser.y \
   image-cue-lexer.l \
   image-cue-disc.c \
   image-cue-plugin.c \
   image-cue-session.c
    
image_cue_la_LIBADD = \
    $(glib_LIBS) \
    $(gobject_LIBS) \
    $(gmodule_LIBS)

image_cue_la_LDFLAGS = \
    -module \
    -avoid-version
