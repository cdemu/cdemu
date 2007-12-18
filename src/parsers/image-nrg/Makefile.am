INCLUDES = \
    -I$(top_srcdir)/src/libmirage \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS) \
    $(gmodule_CFLAGS)
    
plugindir = $(MIRAGE_PLUGIN_DIR)

plugin_LTLIBRARIES = image-nrg.la

noinst_HEADERS = \
    image-nrg.h \
    image-nrg-disc.h

image_nrg_la_SOURCES = \
    image-nrg-disc.c \
    image-nrg-plugin.c

image_nrg_la_LIBADD = \
    $(glib_LIBS) \
    $(gobject_LIBS) \
    $(gmodule_LIBS)

image_nrg_la_LDFLAGS = \
    -module \
    -avoid-version
