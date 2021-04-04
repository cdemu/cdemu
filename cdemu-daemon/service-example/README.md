# CDEmu daemon as DBus-activatable systemd user service

This folder provides an example CDEmu daemon deployment as DBus-activatable
systemd user service.

To install the service:

 * copy `cdemu-daemon.service` to `/usr/lib/systemd/user`
 * copy `net.sf.cdemu.CDEmuDaemon.service` to `/usr/share/dbus-1/services`
 * run `systemctl --daemon-reload`

The service is auto-started when its DBus name (`net.sf.cdemu.CDEmuDaemon`)
is requested by a client for the first time, and can be controlled
using `systemctl --user` syntax and `cdemu-daemon` service name, e.g.:

```
systemctl --user status cdemu-daemon
systemctl --user stop cdemu-daemon
```

The service log can be obtained using `journalctl`:

```
journalctl --user-unit cdemu-daemon
```

The daemon can be configured via optional `~/.config/cdemu-daemon` config
file, which must ahere to CDEmu daemon's built-in config file syntax.
