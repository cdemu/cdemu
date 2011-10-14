AUTOMAKE_OPTIONS = gnu

SUBDIRS = src docs

EXTRA_DIST = \
    libmirage.pc.in \
    $(shell find debian -type f | grep -v .svn | grep -v CVS)

# Build documentation when doing distcheck
DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc

# pkg-config(1) related rules
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libmirage.pc
