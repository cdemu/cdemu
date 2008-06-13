INCLUDES = \
    -I$(top_srcdir)/src/libmirage \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS)

plugindir = $(MIRAGE_PLUGIN_DIR)

plugin_LTLIBRARIES = fragment-binary.la

noinst_HEADERS = \
    fragment-binary.h \
    fragment-binary-fragment.h

fragment_binary_la_SOURCES = \
   fragment-binary-fragment.c \
   fragment-binary-plugin.c
    
fragment_binary_la_LIBADD = \
    $(glib_LIBS) \
    $(gobject_LIBS)

fragment_binary_la_LDFLAGS = \
    -module \
    -avoid-version
