lib_LTLIBRARIES = libmirage.la

libmirage_la_SOURCES = \
    mirage-mirage.c \
    mirage-cdtext-encdec.c \
    mirage-debug.c \
    mirage-disc.c \
    mirage-error.c \
    mirage-fragment.c \
    mirage-index.c \
    mirage-language.c \
    mirage-object.c \
    mirage-plugin.c \
    mirage-sector.c \
    mirage-session.c \
    mirage-track.c \
    mirage-utils.c
                        
INCLUDES = \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS) \
    $(gmodule_CFLAGS) \
    $(sndfile_CFLAGS) \
    -DMIRAGE_PLUGIN_DIR=\"$(MIRAGE_PLUGIN_DIR)\"

libmirage_la_LIBADD = \
    $(glib_LIBS) \
    $(gobject_LIBS) \
    $(gmodule_LIBS) \
    $(sndfile_LIBS) \
    -ldl

libmirage_la_LDFLAGS = \
	$(LT_VERSION_INFO)
    
libmirageincludedir=$(includedir)/libmirage-1.0
libmirageinclude_HEADERS = \
    mirage.h \
    mirage-cdtext-encdec.h \
    mirage-debug.h \
    mirage-disc.h \
    mirage-disc-structures.h \
    mirage-error.h \
    mirage-fragment.h \
    mirage-index.h \
    mirage-language.h \
    mirage-mirage.h \
    mirage-object.h \
    mirage-plugin.h \
    mirage-sector.h \
    mirage-session.h \
    mirage-track.h \
    mirage-utils.h
