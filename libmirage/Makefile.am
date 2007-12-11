AUTOMAKE_OPTIONS = gnu

SUBDIRS = src docs

EXTRA_DIST = \
    libmirage.pc.in

# Build documentation when doing distcheck
DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc

# pkg-config(1) related rules
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libmirage.pc 
