EXTRA_DIST = \
	cdemu-daemon-system.service \
	cdemu-daemon-system.sh \
	cdemud-dbus.conf

# Install only if requested
if SYSTEM_BUS_SERVICE

# Wrapper script
bin_SCRIPTS = cdemu-daemon-system.sh

# D-Bus .service file
dbussystemservicedir = $(datadir)/dbus-1/system-services/
dbussystemservice_DATA = cdemu-daemon-system.service

# D-Bus system bus policy
dbussystemconfdir = $(DBUS_SYS_DIR)
dbussystemconf_DATA = cdemud-dbus.conf

endif
