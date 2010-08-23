AUTOMAKE_OPTIONS = gnu

SUBDIRS = pixmaps po servers src docs

DISTCHECK_CONFIGURE_FLAGS = \
    --disable-scrollkeeper

DISTCLEANFILES = \
    intltool-extract \
    intltool-merge \
    intltool-update
    
EXTRA_DIST = \
    intltool-update.in \
    intltool-extract.in \
    intltool-merge.in \
    omf.make \
    xmldocs.make \
    autogen.sh

dist-hook:
	mkdir $(distdir)/debian
	mkdir $(distdir)/debian/patches
	cp `find debian -maxdepth 1 -type f | grep -v ".svn"` $(distdir)/debian/
	cp `find debian/patches -maxdepth 1 -type f | grep -v ".svn"` $(distdir)/debian/patches/

