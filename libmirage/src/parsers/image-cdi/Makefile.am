INCLUDES = \
    -I$(top_srcdir)/src/libmirage \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS) \
    $(gmodule_CFLAGS)
    
plugindir = $(MIRAGE_PLUGIN_DIR)

plugin_LTLIBRARIES = image-cdi.la

noinst_HEADERS = \
    image-cdi.h \
    image-cdi-disc.h

image_cdi_la_SOURCES = \
    image-cdi-disc.c \
    image-cdi-plugin.c

image_cdi_la_LIBADD = \
    $(glib_LIBS) \
    $(gobject_LIBS) \
    $(gmodule_LIBS)

image_cdi_la_LDFLAGS = \
    -module \
    -avoid-version
