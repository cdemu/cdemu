EXTRA_DIST = \
	cdemu-daemon-dbus.conf \
	cdemu-daemon-system.sh \
	net.sf.cdemu.CDEmuDaemon.service.in

# Install only if requested
if SYSTEM_BUS_SERVICE

# Wrapper script
libexec_SCRIPTS = cdemu-daemon-system.sh

# D-Bus .service file
dbussystemservicedir = $(datadir)/dbus-1/system-services/
dbussystemservice_DATA = net.sf.cdemu.CDEmuDaemon.service
CLEANFILES = net.sf.cdemu.CDEmuDaemon.service

# D-Bus system bus policy
dbussystemconfdir = $(DBUS_SYS_DIR)
dbussystemconf_DATA = cdemu-daemon-dbus.conf

endif

# .service build rule; make sure it's rebuild every time
net.sf.cdemu.CDEmuDaemon.service: net.sf.cdemu.CDEmuDaemon.service.in
	$(AM_V_GEN)sed -e 's,@libexecdir\@,$(libexecdir),g' $< > $@

.PHONY: net.sf.cdemu.CDEmuDaemon.service
