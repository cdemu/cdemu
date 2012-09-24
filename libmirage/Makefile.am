
SUBDIRS = src docs

EXTRA_DIST = \
    libmirage.pc.in \
    debian

# Build documentation when doing distcheck
DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc

# Enable introspection during distcheck
DISTCHECK_CONFIGURE_FLAGS += --enable-introspection

# pkg-config(1) related rules
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libmirage.pc
