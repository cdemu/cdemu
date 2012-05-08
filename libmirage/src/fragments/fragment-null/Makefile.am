AM_CPPFLAGS = \
    -I$(top_srcdir)/src/libmirage \
    -I$(top_builddir)/src/libmirage \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS)

plugindir = $(MIRAGE_PLUGIN_DIR)

plugin_LTLIBRARIES = fragment-null.la

noinst_HEADERS = \
    fragment-null.h \
    fragment-null-fragment.h

fragment_null_la_SOURCES = \
   fragment-null-fragment.c \
   fragment-null-plugin.c
    
fragment_null_la_LIBADD = \
    $(glib_LIBS) \
    $(gobject_LIBS)

fragment_null_la_LDFLAGS = \
    -module \
    -avoid-version
