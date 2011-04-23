EXTRA_DIST = \
	net.sf.cdemu.CDEMUD_Daemon.service \
	cdemu-daemon-session.sh

# Install only if requested
if SESSION_BUS_SERVICE

# Wrapper script
bin_SCRIPTS = cdemu-daemon-session.sh

# D-Bus .service file
dbussystemservicedir = $(datadir)/dbus-1/services/
dbussystemservice_DATA = net.sf.cdemu.CDEMUD_Daemon.service

endif
