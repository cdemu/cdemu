bin_PROGRAMS = image-analyzer

image_analyzer_SOURCES = \
	image-analyzer-application.c \
	image-analyzer-application-xml-dump.c \
	image-analyzer-application-xml-load.c \
	image-analyzer-disc-topology.c \
	image-analyzer-dump.c \
	image-analyzer-parser-log.c \
	image-analyzer-sector-analysis.c \
	image-analyzer-sector-read.c \
	main.c

INCLUDES = \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS) \
    $(gtk_CFLAGS) \
    $(gtkextra_CFLAGS) \
    $(libmirage_CFLAGS) \
    $(libxml_CFLAGS)

image_analyzer_LDADD = \
    $(glib_LIBS) \
    $(gobject_LIBS) \
    $(gtk_LIBS) \
    $(gtkextra_LIBS) \
    $(libmirage_LIBS) \
    $(libxml_LIBS) \
    -lm

noinst_HEADERS = \
	image-analyzer-application.h \
	image-analyzer-application-private.h \
	image-analyzer-disc-topology.h \
	image-analyzer-disc-topology-private.h \
	image-analyzer-dump.h \
	image-analyzer-parser-log.h \
	image-analyzer-parser-log-private.h \
	image-analyzer-sector-analysis.h \
	image-analyzer-sector-analysis-private.h \
	image-analyzer-sector-read.h \
	image-analyzer-sector-read-private.h \
	image-analyzer-xml-tags.h
