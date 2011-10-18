AUTOMAKE_OPTIONS = gnu

SUBDIRS = data man po src

DISTCLEANFILES = \
    intltool-extract \
    intltool-merge \
    intltool-update

EXTRA_DIST = \
    intltool-update.in \
    intltool-extract.in \
    intltool-merge.in \
    $(shell find debian -type f | grep -v .svn | grep -v CVS)

