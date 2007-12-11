bin_PROGRAMS = image-analyzer

image_analyzer_SOURCES = \
    image-analyzer-application.c \
    image-analyzer-dump.c \
    image-analyzer-parser-log.c \
    image-analyzer-sector-read.c \
    main.c

INCLUDES = \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS) \
    $(gtk_CFLAGS) \
    $(libmirage_CFLAGS)

image_analyzer_LDADD = \
    $(glib_LIBS) \
    $(gobject_LIBS) \
    $(gtk_LIBS) \
    $(libmirage_LIBS)

noinst_HEADERS = \
    image-analyzer-application.h \
    image-analyzer-dump.h \
    image-analyzer-parser-log.h \
    image-analyzer-sector-read.h
