AUTOMAKE_OPTIONS = gnu

SUBDIRS = data po src

DISTCLEANFILES = \
    intltool-extract \
    intltool-merge \
    intltool-update

EXTRA_DIST = \
    intltool-update.in \
    intltool-extract.in \
    intltool-merge.in

dist-hook:
	mkdir $(distdir)/debian
	mkdir $(distdir)/debian/patches
	cp `find $(top_srcdir)/debian -maxdepth 1 -type f | grep -v ".svn"` $(distdir)/debian/
	cp `find $(top_srcdir)/debian/patches -maxdepth 1 -type f | grep -v ".svn"` $(distdir)/debian/patches/

