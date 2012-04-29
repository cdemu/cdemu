INCLUDES = \
    -I$(top_srcdir)/src/libmirage \
    -I$(top_builddir)/src/libmirage \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS) \
    $(libsndfile_CFLAGS)

plugindir = $(MIRAGE_PLUGIN_DIR)

plugin_LTLIBRARIES = fragment-sndfile.la

noinst_HEADERS = \
    fragment-sndfile.h \
    fragment-sndfile-fragment.h

fragment_sndfile_la_SOURCES = \
   fragment-sndfile-fragment.c \
   fragment-sndfile-plugin.c
    
fragment_sndfile_la_LIBADD = \
    $(glib_LIBS) \
    $(gobject_LIBS) \
    $(sndfile_LIBS)

fragment_sndfile_la_LDFLAGS = \
    -module \
    -avoid-version
