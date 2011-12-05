EXTRA_DIST = \
	net.sf.cdemu.CDEMUD_Daemon.service.in \
	cdemu-daemon-system.sh \
	cdemud-dbus.conf

# Install only if requested
if SYSTEM_BUS_SERVICE

# Wrapper script
libexec_SCRIPTS = cdemu-daemon-system.sh

# D-Bus .service file
dbussystemservicedir = $(datadir)/dbus-1/system-services/
dbussystemservice_DATA = net.sf.cdemu.CDEMUD_Daemon.service
CLEANFILES = net.sf.cdemu.CDEMUD_Daemon.service

# D-Bus system bus policy
dbussystemconfdir = $(DBUS_SYS_DIR)
dbussystemconf_DATA = cdemud-dbus.conf

endif

# .service build rule; make sure it's rebuild every time
net.sf.cdemu.CDEMUD_Daemon.service: net.sf.cdemu.CDEMUD_Daemon.service.in
	$(AM_V_GEN)sed -e 's,@libexecdir\@,$(libexecdir),g' $< > $@

.PHONY: net.sf.cdemu.CDEMUD_Daemon.service
