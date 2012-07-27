# Icon
pixmapsdir = $(datadir)/pixmaps
pixmaps_DATA = gcdemu.svg gcdemu-icon.svg gcdemu-icon-gray.svg

# GSettings Schema
gschemas_in_files = apps.gcdemu.gschema.xml.in
@INTLTOOL_XML_NOMERGE_RULE@

gsettings_SCHEMAS = $(gschemas_in_files:.xml.in=.xml)
@GSETTINGS_RULES@


# Desktop application entry
desktopdir = $(datadir)/applications
desktop_in_files = gcdemu.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@


CLEANFILES = \
	$(desktop_DATA) \
    $(gschemas_in_files:.xml.in=.valid) \
    $(gsettings_SCHEMAS)

EXTRA_DIST = \
	$(desktop_in_files) \
    $(gschemas_in_files) \
    $(pixmaps_DATA)
