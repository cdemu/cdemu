libexec_SCRIPTS = gcdemu

gcdemudir = $(pythondir)/gcdemu

gcdemu_PYTHON = \
   __init__.py \
   applet.py \
   globals.py \
   ui.py

# GConf Preference Schemas
schemasdir = $(GCONF_SCHEMA_FILE_DIR)
schemas_in_files = gcdemu.schemas.in
schemas_DATA = $(schemas_in_files:.schemas.in=.schemas)

@INTLTOOL_SCHEMAS_RULE@

if GCONF_SCHEMAS_INSTALL
install-data-local: install-schemas
install-schemas:
	GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(schemas_DATA)
endif

CLEANFILES = \
    $(schemas_DATA)

EXTRA_DIST = \
    $(schemas_in_files) \
    $(libexec_SCRIPTS)
