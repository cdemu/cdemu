serverdir = $(libdir)/bonobo/servers
server_in_files = GNOME_gCDEmuApplet.server.in
server_DATA = $(server_in_files:.server.in=.server)

$(server_in_files): $(server_in_files:.server.in=.server.in.in)
	sed -e "s|\@LIBEXECDIR\@|$(libexecdir)|" -e "s|\@DATADIR\@|$(datadir)|" $< > $@

@INTLTOOL_SERVER_RULE@

EXTRA_DIST = \
    GNOME_gCDEmuApplet.server.in.in

CLEANFILES = \
    $(server_in_files) \
    $(server_DATA)
