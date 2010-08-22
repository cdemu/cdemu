AUTOMAKE_OPTIONS = gnu

SUBDIRS = src docs

EXTRA_DIST = \
    libmirage.pc.in

dist-hook:
	mkdir $(distdir)/debian
	mkdir $(distdir)/debian/patches
	cp `find debian -maxdepth 1 -type f | grep -v ".svn"` $(distdir)/debian/
	cp `find debian/patches -maxdepth 1 -type f | grep -v ".svn"` $(distdir)/debian/patches/

# Build documentation when doing distcheck
DISTCHECK_CONFIGURE_FLAGS = --enable-gtk-doc

# pkg-config(1) related rules
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libmirage.pc 
