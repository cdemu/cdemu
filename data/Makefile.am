# Icon
pixmapsdir = $(datadir)/pixmaps
pixmaps_DATA = gcdemu.svg

# GConf Preference Schema
schemasdir = $(GCONF_SCHEMA_FILE_DIR)
schemas_in_files = gcdemu.schemas.in
schemas_DATA = $(schemas_in_files:.schemas.in=.schemas)
@INTLTOOL_SCHEMAS_RULE@

if GCONF_SCHEMAS_INSTALL
install-data-local: install-schemas
install-schemas:
	GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(schemas_DATA)
endif


# Desktop application entry
desktopdir = $(datadir)/applications
desktop_in_files = gcdemu.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@



CLEANFILES = \
	$(desktop_DATA) \
    $(schemas_DATA)

EXTRA_DIST = \
	$(desktop_in_files) \
    $(schemas_in_files) \
    $(pixmaps_DATA)
