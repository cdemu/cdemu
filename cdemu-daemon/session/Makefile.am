EXTRA_DIST = \
	net.sf.cdemu.CDEmuDaemon.service.in \
	cdemu-daemon-session.sh

# Install only if requested
if SESSION_BUS_SERVICE

# Wrapper script
libexec_SCRIPTS = cdemu-daemon-session.sh

# D-Bus .service file
dbussystemservicedir = $(datadir)/dbus-1/services/
dbussystemservice_DATA = net.sf.cdemu.CDEmuDaemon.service
CLEANFILES = net.sf.cdemu.CDEmuDaemon.service

endif

# .service build rule; make sure it's rebuild every time
net.sf.cdemu.CDEmuDaemon.service: net.sf.cdemu.CDEmuDaemon.service.in
	$(AM_V_GEN)sed -e 's,@libexecdir\@,$(libexecdir),g' $< > $@

.PHONY: net.sf.cdemu.CDEmuDaemon.service
