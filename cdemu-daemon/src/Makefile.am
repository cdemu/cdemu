INCLUDES = \
    -I$(top_builddir)/include \
    $(glib_CFLAGS) \
    $(gobject_CFLAGS) \
    $(gthread_CFLAGS) \
    $(dbus_CFLAGS) \
    $(libmirage_CFLAGS) \
    $(libao_CFLAGS)

BUILT_SOURCES = \
    cdemud-marshallers.h \
    cdemud-marshallers.c \
    cdemud-service-glue.h

EXTRA_DIST = \
    cdemud-marshallers.list \
    cdemud-service.xml

noinst_HEADERS = \
    cdemud.h \
    cdemud-audio.h \
    cdemud-daemon.h \
    cdemud-debug.h \
    cdemud-device.h \
    cdemud-error.h \
    cdemud-mmc-features.h \
    cdemud-mode-pages.h \
    cdemud-packet-commands.h \
    cdemud-sense-constants.h \
    cdemud-service-glue.h

bin_PROGRAMS = cdemud

cdemud_SOURCES = \
    cdemud-audio.c \
    cdemud-daemon.c \
    cdemud-device.c \
    cdemud-error.c \
    cdemud-marshallers.c \
    main.c

cdemud_LDADD = \
    $(glib_LIBS) \
    $(gobject_LIBS) \
    $(gthread_LIBS) \
    $(dbus_LIBS) \
    $(libmirage_LIBS) \
    $(libao_LIBS) \
    $(sysfs_LIBS)

# Some ugly hacks for building built sources out of source tree... until I
# figure how to do this properly (mainly needed to for 'make distcheck' pass)
SERVICE_GLUE_IN = $(top_srcdir)/src/cdemud-service.xml
SERVICE_GLUE_OUT = $(top_builddir)/src/cdemud-service-glue.h
cdemud-service-glue.h: $(SERVICE_GLUE_IN)
	dbus-binding-tool --prefix=cdemud_daemon --mode=glib-server $(SERVICE_GLUE_IN) > $(SERVICE_GLUE_OUT)

MARSHALLERS_IN = $(top_srcdir)/src/cdemud-marshallers.list
MARSHALLERS_H_OUT = $(top_builddir)/src/cdemud-marshallers.h
MARSHALLERS_C_OUT = $(top_builddir)/src/cdemud-marshallers.c
cdemud-marshallers.h: $(MARSHALLERS_IN)
	glib-genmarshal --header $(MARSHALLERS_IN) > $(MARSHALLERS_H_OUT)

cdemud-marshallers.c: $(MARSHALLERS_IN)
	glib-genmarshal --header --body $(MARSHALLERS_IN) > $(MARSHALLERS_C_OUT)

DISTCLEANFILES = \
    $(SERVICE_GLUE_OUT) \
    $(MARSHALLERS_H_OUT) \
    $(MARSHALLERS_C_OUT)
