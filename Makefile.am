SUBDIRS = src

dist-hook:
	mkdir $(distdir)/debian
	mkdir $(distdir)/debian/patches
	cp `find $(top_srcdir)/debian -maxdepth 1 -type f | grep -v ".svn"` $(distdir)/debian/
	cp `find $(top_srcdir)/debian/patches -maxdepth 1 -type f | grep -v ".svn"` $(distdir)/debian/patches/

