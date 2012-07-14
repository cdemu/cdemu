SUBDIRS = \
   image-b6t \
   image-c2d \
   image-ccd \
   image-cdi \
   image-cif \
   image-cue \
   image-daa \
   image-iso \
   image-mds \
   image-mdx \
   image-nrg \
   image-readcd \
   image-toc \
   image-xcdroast


mimedir = $(datadir)/mime/packages

install-data-hook: update-mime-database
uninstall-hook: update-mime-database

# Update mime only when system-wide installation takes place
update-mime-database:
	if [ -f $(DESTDIR)$(mimedir)/freedesktop.org.xml ]; then \
		if which update-mime-database>/dev/null 2>&1; then \
			update-mime-database $(DESTDIR)$(datadir)/mime; \
		fi \
	fi
