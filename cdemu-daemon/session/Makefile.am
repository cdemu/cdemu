EXTRA_DIST = \
	net.sf.cdemu.CDEMUD_Daemon.service.in \
	cdemu-daemon-session.sh

# Install only if requested
if SESSION_BUS_SERVICE

# Wrapper script
bin_SCRIPTS = cdemu-daemon-session.sh

# D-Bus .service file
dbussystemservicedir = $(datadir)/dbus-1/services/
dbussystemservice_DATA = net.sf.cdemu.CDEMUD_Daemon.service
CLEANFILES = net.sf.cdemu.CDEMUD_Daemon.service

endif

# .service build rule; make sure it's rebuild every time
net.sf.cdemu.CDEMUD_Daemon.service: net.sf.cdemu.CDEMUD_Daemon.service.in
	$(AM_V_GEN)sed -e 's,@bindir\@,$(bindir),g' $< > $@

.PHONY: net.sf.cdemu.CDEMUD_Daemon.service
