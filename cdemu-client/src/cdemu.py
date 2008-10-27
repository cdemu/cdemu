# -*- coding: utf-8 -*-
# CDEmu: simple command line client
# Copyright (C) 2006-2008 Rok Mandeljc

# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

import getopt
import sys
import os.path
import string
import getpass
import ConfigParser

import dbus
from dbus.mainloop.glib import DBusGMainLoop
DBusGMainLoop(set_as_default=True)

from gettext import gettext as _

import globals as config

# This needs to be kept in sync with Daemon/libMirage
MIRAGE_E_NEEDSPASSWORD = "org.freedesktop.DBus.GLib.UnmappedError.MirageErrorQuark.Code233492496"

class CDEmu (object):
    # D-BUS
    __dbus_bus = None
    __dbus_proxy = None
    __dbus_iface = None
    
    __bus_type = "system" # Use system bus as hard-coded default
    
    def __init__ (self):
        # Load options; Try "~/.cdemu" first, then try "/etc/cdemu.conf" next.
        paths = (os.path.expanduser("~/.cdemu"), "/etc/cdemu.conf")
        path = "(not found)"
        for path_inst in paths:
            if os.path.exists(path_inst):
                path = path_inst
                break

        try:
            config = ConfigParser.ConfigParser()
            config.read([path])
        
            if config.has_section("defaults"):
                # Read default bus type
                if config.has_option("defaults","bus"):
                    self.__bus_type = config.get("defaults","bus")
        except:
            # No harm, just print a warning
            self.__print_error(_("Failed to load configuration from file '%s': %s") % (path, sys.exc_value))
            pass

        return
    
    def process_command (self, argv):
        print_usage = False
        
        # Separate options from arguments
        try:
            options, arguments = getopt.gnu_getopt(argv, "hvb:", ["help", "version", "bus="])
        except getopt.GetoptError:
            self.__print_error(_("Unknown option"))
            self.__print_full_usage()
            return False
                
        # Go through options    
        for opt, arg in options:
            if opt in ("-h", "--help"):
                print_usage = True
            elif opt in ("-v", "--version"):
                self.__print_version()
                return True
            elif opt in ("-b", "--bus"):
                # Bus type; don't check the value here, __connect() will do it
                # for us
                self.__bus_type = arg                    
        
        if len(arguments) == 0:
            self.__print_full_usage()
            # If we were called with print_usage set, then this technically succeeded
            return print_usage
                
        # Connect        
        if self.__connect() != True:
            self.__print_error(_("Failed to connect to daemon (bus: '%s')!") % (self.__bus_type))
            return False
        
        # Command switch
        for command in self.__commands:
            if command[0] == arguments[0]:
                if print_usage:
                    self.__print_command_usage(command[0])
                else:
                    return command[3](self, arguments[1:])
                
        self.__print_error(_("Unknown command: %s") % (arguments[0]))
        self.__print_full_usage()
        
        return False

    # Device loading with password query support
    def __load_device (self, device, filenames, params={}):
        # Try to load it
        try:
            self.__dbus_iface.DeviceLoad(device, filenames, params)
            return True
        except dbus.DBusException, e:
            if e.get_dbus_name() == MIRAGE_E_NEEDSPASSWORD:
                # We need password
                print _("The image you are trying to load is encrypted.");
                password = getpass.getpass(_("Password: "));
                return self.__load_device(device, filenames, {"password":password})
            else:
                self.__print_error(_("Failed to load image: %s") % (e.get_dbus_message()))
                return False
        
        
    # Command handlers
    def __cmd_load_device (self, arguments):
        if len(arguments) < 2:
            self.__print_invalid_number_of_parameters("load")
            return False
        
        filenames = arguments[1:]
        
        # We need to pass absolute filenames to daemon, because its working
        # directory is (99.9% of the time, anyway) different from ours...
        for i in range(0, len(filenames)):
            filenames[i] = os.path.abspath(filenames[i])
        
        # Particular device vs. any device
        if arguments[0] == "any":
            try:
                nr_devices = self.__dbus_iface.GetNumberOfDevices()
            except dbus.DBusException, e:
                self.__print_error(_("Failed to get number of devices: %s") % (e.get_dbus_message()))
                return False
            
            for device in range (0, nr_devices):
                # Device's status
                try:
                    status = self.__dbus_iface.DeviceGetStatus(device)
                except dbus.DBusException, e:
                    self.__print_error(_("Failed to get status of device %i: %s") % (device, e.get_dbus_message()))
                    continue
                
                # If device is already loaded, skip it
                if status[0]:
                    continue
                
                # Load device
                return self.__load_device(device, filenames)
            
            # If we're here, it means we didn't get an empty device
            self.__print_error(_("No empty device found"))
            return False
        else:
            device = string.atoi(arguments[0], 0)
            return self.__load_device(device, filenames)
        
        return True
        
    def __cmd_unload_device (self, arguments):
        if len(arguments) != 1:
            self.__print_invalid_number_of_parameters("unload")
            return False
        
        # Particular device vs. all devices
        if arguments[0] == "all":
            try:
                nr_devices = self.__dbus_iface.GetNumberOfDevices()
            except dbus.DBusException, e:
                self.__print_error(_("Failed to get number of devices: %s") % (e.get_dbus_message()))
                return False

            unload_fail = False
            for device in range(0, nr_devices):
                try:
                    self.__dbus_iface.DeviceUnload(device)
                except dbus.DBusException, e:
                    self.__print_error(_("Failed to unload device %i: %s") % (device, e.get_dbus_message()))
                    unload_fail = True
                    continue
            if unload_fail:
                return False

        else:
            device = string.atoi(arguments[0], 0)
            try:
                self.__dbus_iface.DeviceUnload(device)
            except dbus.DBusException, e:
                self.__print_error(_("Failed to unload device %i: %s") % (device, e.get_dbus_message()))
                return False
        
        return True
        
    def __cmd_display_status (self, arguments):
        # Print status for all devices
        try:
            nr_devices = self.__dbus_iface.GetNumberOfDevices()
        except dbus.DBusException, e:
            self.__print_error(_("Failed to get number of devices: %s") % (e.get_dbus_message()))
            return False
        
        print _("Devices' status:")
        print "%-5s %-10s %s" % (_("DEV"), _("LOADED"), _("FILENAME"))
        for device in range (0, nr_devices):
            try:
                [loaded, filenames] = self.__dbus_iface.DeviceGetStatus(device)
            except dbus.DBusException, e:
                self.__print_error(_("Failed to get status of device %i: %s") % (device, e.get_dbus_message()))
                continue
            
            # First line is for all device's data, the rest are for additional filenames
            print "%-5s %-10s %s" % (device, loaded, filenames[0])
            for i in range(1, len(filenames)):
                print "%-5s %-10s %s" % ("", "", filenames[i])
        
        return True
        
    def __cmd_device_mapping (self, arguments):
        # Print device mapping for all devices
        try:
            nr_devices = self.__dbus_iface.GetNumberOfDevices()
        except dbus.DBusException, e:
            self.__print_error(_("Failed to get number of devices: %s") % (e.get_dbus_message()))
            return False
        
        print _("Device mapping:")
        print "%-5s %-15s %-15s" % (_("DEV"), _("SCSI CD-ROM"), _("SCSI generic"))
        for device in range (0, nr_devices):
            try:
                [dev_sr, dev_sg] = self.__dbus_iface.DeviceGetMapping(device)
            except dbus.DBusException, e:
                self.__print_error(_("Failed to get device mapping of device %i: %s") % (device, e.get_dbus_message()))
                continue
            
            print "%-5s %-15s %-15s" % (device, dev_sr, dev_sg)
        
        return True
        
    def __cmd_daemon_debug_mask (self, arguments):
        # Get daemon debug mask
        if len(arguments) == 1:
            # Particular device vs. all devices
            if arguments[0] == "all":
                try:
                    nr_devices = self.__dbus_iface.GetNumberOfDevices()
                except dbus.DBusException, e:
                    self.__print_error(_("Failed to get number of devices: %s") % (e.get_dbus_message()))
                    return False
                
                print _("Devices' daemon debug masks:")
                print "%-5s %-10s" % (_("DEV"), _("DEBUG MASK"))
                
                for device in range(0, nr_devices):
                    try:
                        values = self.__dbus_iface.DeviceGetOption(device, "daemon-debug-mask")
                        dbg_mask = values[0]
                    except dbus.DBusException, e:
                        self.__print_error(_("Failed to get daemon debug mask of device %i: %s") % (device, e.get_dbus_message()))
                        continue
                        
                    print "%-5s 0x%08X" % (device, dbg_mask)
            else:
                device = string.atoi(arguments[0], 0)
                try:
                    values = self.__dbus_iface.DeviceGetOption(device, "daemon-debug-mask")
                    dbg_mask = values[0]
                except dbus.DBusException, e:
                    self.__print_error(_("Failed to get daemon debug mask of device %i: %s") % (device, e.get_dbus_message()))
                    return False
                
                print _("Daemon debug mask of device %i: 0x%X") % (device, dbg_mask)
                
        # Set daemon debug mask
        elif len(arguments) == 2:
            dbg_mask = string.atoi(arguments[1], 0)
            if arguments[0] == "all":
                try:
                    nr_devices = self.__dbus_iface.GetNumberOfDevices()
                except dbus.DBusException, e:
                    self.__print_error(_("Failed to get number of devices: %s") % (e.get_dbus_message()))
                    return False
                
                print _("Setting daemon debug mask of all devices to 0x%X.") % (dbg_mask)
                for device in range(0, nr_devices):
                    try:
                        self.__dbus_iface.DeviceSetOption(device, "daemon-debug-mask", [ dbg_mask ])
                    except dbus.DBusException, e:
                        self.__print_error(_("Failed to set daemon debug mask of device %i to 0x%X: %s") % (device, dbg_mask, e.get_dbus_message()))
                        continue
            else:
                device = string.atoi(arguments[0], 0)
                print _("Setting daemon debug mask of device %i to 0x%X.") % (device, dbg_mask)
                try:
                    self.__dbus_iface.DeviceSetOption(device, "daemon-debug-mask", [ dbg_mask ])
                except dbus.DBusException, e:
                    self.__print_error(_("Failed to set daemon debug mask of device %i to 0x%X: %s") % (device, dbg_mask, e.get_dbus_message()))
                    return False
        else:
            self.__print_invalid_number_of_parameters("daemon-debug-mask")
            return False
        
        return True
    
    def __cmd_library_debug_mask (self, arguments):
        # Get debug mask
        if len(arguments) == 1:
            # Particular device vs. all devices
            if arguments[0] == "all":
                try:
                    nr_devices = self.__dbus_iface.GetNumberOfDevices()
                except dbus.DBusException, e:
                    self.__print_error(_("Failed to get number of devices: %s") % (e.get_dbus_message()))
                    return False
                
                print _("Devices' library debug masks:")
                print "%-5s %-10s" % (_("DEV"), _("DEBUG MASK"))
                
                for device in range(0, nr_devices):
                    try:
                        values = self.__dbus_iface.DeviceGetOption(device, "library-debug-mask")
                        dbg_mask = values[0]
                    except dbus.DBusException, e:
                        self.__print_error(_("Failed to get library debug mask of device %i: %s") % (device, e.get_dbus_message()))
                        continue
                        
                    print "%-5s 0x%08X" % (device, dbg_mask)
            else:
                device = string.atoi(arguments[0], 0)
                try:
                    values = self.__dbus_iface.DeviceGetOption(device, "library-debug-mask")
                    dbg_mask = values[0]
                except dbus.DBusException, e:
                    self.__print_error(_("Failed to get library debug mask of device %i: %s") % (device, e.get_dbus_message()))
                    return False
                
                print _("Library debug mask of device %i: 0x%X") % (device, dbg_mask)
                
        # Set debug mask
        elif len(arguments) == 2:
            dbg_mask = string.atoi(arguments[1], 0)
            if arguments[0] == "all":
                try:
                    nr_devices = self.__dbus_iface.GetNumberOfDevices()
                except dbus.DBusException, e:
                    self.__print_error(_("Failed to get number of devices: %s") % (e.get_dbus_message()))
                    return False
                
                print _("Setting library debug mask of all devices to 0x%X.") % (dbg_mask)
                for device in range(0, nr_devices):
                    try:
                        self.__dbus_iface.DeviceSetOption(device, "library-debug-mask", [ dbg_mask ])
                    except dbus.DBusException, e:
                        self.__print_error(_("Failed to set library debug mask of device %i to 0x%X: %s") % (device, dbg_mask, e.get_dbus_message()))
                        continue
            else:
                device = string.atoi(arguments[0], 0)
                print _("Setting library debug mask of device %i to 0x%X.") % (device, dbg_mask)
                try:
                    self.__dbus_iface.DeviceSetOption(device, "library-debug-mask", [ dbg_mask ])
                except dbus.DBusException, e:
                    self.__print_error(_("Failed to set library debug mask of device %i to 0x%X: %s") % (device, dbg_mask, e.get_dbus_message()))
                    return False
        else:
            self.__print_invalid_number_of_parameters("library-debug-mask")
            return False
        
        return True
    
    def __cmd_dpm_emulation (self, arguments):
        # Get DPM emulation flag
        if len(arguments) == 1:
            # Particular device vs. all devices
            if arguments[0] == "all":
                try:
                    nr_devices = self.__dbus_iface.GetNumberOfDevices()
                except dbus.DBusException, e:
                    self.__print_error(_("Failed to get number of devices: %s") % (e.get_dbus_message()))
                    return False
                
                print _("Devices' DPM emulation flag:")
                print "%-5s %-10s" % (_("DEV"), _("ENABLED"))
                
                for device in range(0, nr_devices):
                    try:
                        [ enabled ] = self.__dbus_iface.DeviceGetOption(device, "dpm-emulation")
                    except dbus.DBusException, e:
                        self.__print_error(_("Failed to get DPM emulation flag of device %i: %s") % (device, e.get_dbus_message()))
                        continue
                        
                    print "%-5s %i" % (device, enabled)
            else:
                device = string.atoi(arguments[0], 0)
                try:
                    [ enabled ] = self.__dbus_iface.DeviceGetOption(device, "dpm-emulation")
                except dbus.DBusException, e:
                    self.__print_error(_("Failed to get DPM emulation flag of device %i: %s") % (device, e.get_dbus_message()))
                    return False
                
                print _("DPM emulation flag of device %i: %i") % (device, enabled)
                
        # Set DPM emulation flag
        elif len(arguments) == 2:
            enabled = string.atoi(arguments[1], 0) and True or False
            if arguments[0] == "all":
                try:
                    nr_devices = self.__dbus_iface.GetNumberOfDevices()
                except dbus.DBusException, e:
                    self.__print_error(_("Failed to get number of devices: %s") % (e.get_dbus_message()))
                    return False
                
                print _("Setting DPM emulation flag of all devices to %i.") % (enabled)
                for device in range(0, nr_devices):
                    try:
                        self.__dbus_iface.DeviceSetOption(device, "dpm-emulation", [ enabled ])
                    except dbus.DBusException, e:
                        self.__print_error(_("Failed to set DPM emulation flag of device %i to %i: %s") % (device, enabled, e.get_dbus_message()))
                        continue
            else:
                device = string.atoi(arguments[0], 0)
                print _("Setting DPM emulation flag of device %i to %i.") % (device, enabled)
                try:
                    self.__dbus_iface.DeviceSetOption(device, "dpm-emulation", [ enabled ])
                except dbus.DBusException, e:
                    self.__print_error(_("Failed to set DPM emulation flag of device %i to %i: %s") % (device, enabled, e.get_dbus_message()))
                    return False
        else:
            self.__print_invalid_number_of_parameters("dpm-emulation")
            return False
        
        return True
        
    def __cmd_tr_emulation (self, arguments):
        # Get TR emulation flag
        if len(arguments) == 1:
            # Particular device vs. all devices
            if arguments[0] == "all":
                try:
                    nr_devices = self.__dbus_iface.GetNumberOfDevices()
                except dbus.DBusException, e:
                    self.__print_error(_("Failed to get number of devices: %s") % (e.get_dbus_message()))
                    return False
                
                print _("Devices' transfer rate emulation flag:")
                print "%-5s %-10s" % (_("DEV"), _("ENABLED"))
                
                for device in range(0, nr_devices):
                    try:
                        [ enabled ] = self.__dbus_iface.DeviceGetOption(device, "tr-emulation")
                    except dbus.DBusException, e:
                        self.__print_error(_("Failed to get transfer rate emulation flag of device %i: %s") % (device, e.get_dbus_message()))
                        continue
                        
                    print "%-5s %i" % (device, enabled)
            else:
                device = string.atoi(arguments[0], 0)
                try:
                    [ enabled ] = self.__dbus_iface.DeviceGetOption(device, "tr-emulation")
                except dbus.DBusException, e:
                    self.__print_error(_("Failed to get transfer rate emulation flag of device %i: %s") % (device, e.get_dbus_message()))
                    return False
                
                print _("Transfer rate emulation flag of device %i: %i") % (device, enabled)
                
        # Set DPM emulation flag
        elif len(arguments) == 2:
            enabled = string.atoi(arguments[1], 0) and True or False
            if arguments[0] == "all":
                try:
                    nr_devices = self.__dbus_iface.GetNumberOfDevices()
                except dbus.DBusException, e:
                    self.__print_error(_("Failed to get number of devices: %s") % (e.get_dbus_message()))
                    return False
                
                print _("Setting transfer rate emulation flag of all devices to %i.") % (enabled)
                for device in range(0, nr_devices):
                    try:
                        self.__dbus_iface.DeviceSetOption(device, "tr-emulation", [ enabled ])
                    except dbus.DBusException, e:
                        self.__print_error(_("Failed to set transfer rate emulation flag of device %i to %i: %s") % (device, enabled, e.get_dbus_message()))
                        continue
            else:
                device = string.atoi(arguments[0], 0)
                print _("Setting transfer rate emulation flag of device %i to %i.") % (device, enabled)
                try:
                    self.__dbus_iface.DeviceSetOption(device, "tr-emulation", [ enabled ])
                except dbus.DBusException, e:
                    self.__print_error(_("Failed to set transfer rate emulation flag of device %i to %i: %s") % (device, enabled, e.get_dbus_message()))
                    return False
        else:
            self.__print_invalid_number_of_parameters("tr-emulation")
            return False
        
        return True
    
    def __cmd_device_id (self, arguments):
        # Get device ID
        if len(arguments) == 1:
            # Particular device vs. all devices
            if arguments[0] == "all":
                try:
                    nr_devices = self.__dbus_iface.GetNumberOfDevices()
                except dbus.DBusException, e:
                    self.__print_error(_("Failed to get number of devices: %s") % (e.get_dbus_message()))
                    return False
                
                print _("Devices' IDs:")
                print "%-5s %s" % (_("DEV"), _("DEVICE ID"))
                
                for device in range(0, nr_devices):
                    try:
                        values = self.__dbus_iface.DeviceGetOption(device, "device-id")
                        device_id = []
                        for value in values:
                            device_id.append(str(value))
                    except dbus.DBusException, e:
                        self.__print_error(_("Failed to get device ID of device %i: %s") % (device, e.get_dbus_message()))
                        continue
                        
                    print "%-5s %s" % (device, device_id)
            else:
                device = string.atoi(arguments[0], 0)
                try:
                    values = self.__dbus_iface.DeviceGetOption(device, "device-id")
                    device_id = []
                    for value in values:
                        device_id.append(str(value))
                except dbus.DBusException, e:
                    self.__print_error(_("Failed to get device ID of device %i: %s") % (device, e.get_dbus_message()))
                    return False
                
                print _("Device ID of device %i: %s") % (device, device_id)
                
        # Set device ID
        elif len(arguments) == 5:
            device_id = [ arguments[1], arguments[2], arguments[3], arguments[4] ]
            
            if arguments[0] == "all":
                try:
                    nr_devices = self.__dbus_iface.GetNumberOfDevices()
                except dbus.DBusException, e:
                    self.__print_error(_("Failed to get number of devices: %s") % (e.get_dbus_message()))
                    return False
                
                print _("Setting device ID of all devices to %s.") % (device_id)
                for device in range(0, nr_devices):
                    try:
                        self.__dbus_iface.DeviceSetOption(device, "device-id", device_id)
                    except dbus.DBusException, e:
                        self.__print_error(_("Failed to set device ID of device %i to %s: %s") % (device, device_id, e.get_dbus_message()))
                        continue
            else:
                device = string.atoi(arguments[0], 0)
                print _("Setting device ID of device %i to %s.") % (device, device_id)
                try:
                    self.__dbus_iface.DeviceSetOption(device, "device-id", device_id)
                except dbus.DBusException, e:
                    self.__print_error(_("Failed to set device ID of device %i to %s: %s") % (device, device_id, e.get_dbus_message()))
                    return False
        else:
            self.__print_invalid_number_of_parameters("device-id")
            return False
        
        return True
    
    def __cmd_enum_supported_parsers (self, arguments):
        # Display supported parsers
        try:
            parsers = self.__dbus_iface.EnumSupportedParsers()
        except dbus.DBusException, e:
            self.__print_error(_("Failed to enumerate supported parsers: %s") % (e.get_dbus_message()))
            return False
        
        if len(arguments) == 0:
            # Print all parsers
            print _("Supported parsers:")
            for parser in parsers:
                line = "  %s: %s:" % (parser[0], parser[5])
                for suffix in parser[6]:
                    line += " %s" % (suffix)
                print line
        elif len(arguments) == 1:
            # Print specific parser
            for parser in parsers:
                if parser[0] == arguments[0]:
                    print _("Parser information: '%s'") % (parser[0])
                    print _(" - Name: %s") % (parser[1])
                    print _(" - Version: %s") % (parser[2])
                    print _(" - Author: %s") % (parser[3])
                    print _(" - Multiple files support: %i") % (parser[4])
                    print _(" - Image files description: %s") % (parser[5])
                    line = _(" - Image files suffixes:")
                    for suffix in parser[6]:
                        line += " %s" % (suffix)
                    print line
                    
                    break
            else:
                print _("Parser with ID '%s' not found!") % (arguments[0])
                return False
        else:
            self.__print_invalid_number_of_parameters("enum-supported-parsers")
            return False
        
        return True
        
    def __cmd_enum_supported_fragments (self, arguments):
        # Display supported fragments
        try:
            fragments = self.__dbus_iface.EnumSupportedFragments()
        except dbus.DBusException, e:
            self.__print_error(_("Failed to enumerate supported fragments: %s") % (e.get_dbus_message()))
            return False
        
        if len(arguments) == 0:
            # Print all fragments
            print _("Supported fragments:")
            for fragment in fragments:
                line = "  %s: %s:" % (fragment[0], fragment[4])
                for suffix in fragment[5]:
                    line += " %s" % (suffix)
                print line
        elif len(arguments) == 1:
            # Print specific fragments
            for fragment in fragments:
                if fragment[0] == arguments[0]:
                    print _("Fragment information: '%s'") % (fragment[0])
                    print _(" - Name: %s") % (fragment[1])
                    print _(" - Version: %s") % (fragment[2])
                    print _(" - Author: %s") % (fragment[3])
                    print _(" - Interface: %s") % (fragment[4])
                    line = _(" - Data files suffixes:")
                    for suffix in fragment[5]:
                        line += " %s" % (suffix)
                    print line
                    
                    break
            else:
                print _("Fragment with ID '%s' not found!") % (arguments[0])
                return False
        else:
            self.__print_invalid_number_of_parameters("enum-supported-fragments")
            return False
        
        return True
        
    def __cmd_enum_daemon_debug_masks (self, arguments):
        if len(arguments) != 0:
            self.__print_invalid_number_of_parameters("enum-daemon-debug-masks")
            return False
                
        # Print module's debug masks
        try:
            debug_masks = self.__dbus_iface.EnumDaemonDebugMasks()
        except dbus.DBusException, e:
            self.__print_error(_("Failed to enumerate supported daemon debug masks: %s") % (e.get_dbus_message()))
            return False
        
        print _("Supported daemon debug masks:")
        for debug_mask in debug_masks:
            print "  %-25s: 0x%04X" % (debug_mask[0], debug_mask[1])
        
        return True
        
    def __cmd_enum_library_debug_masks (self, arguments):
        if len(arguments) != 0:
            self.__print_invalid_number_of_parameters("enum-library-debug-masks")
            return False
                
        # Print module's debug masks
        try:
            debug_masks = self.__dbus_iface.EnumLibraryDebugMasks()
        except dbus.DBusException, e:
            self.__print_error(_("Failed to enumerate supported library debug masks: %s") % (e.get_dbus_message()))
            return False
        
        print _("Supported library debug masks:")
        for debug_mask in debug_masks:
            print "  %-25s: 0x%04X" % (debug_mask[0], debug_mask[1])
        
        return True
    
    def __cmd_version (self, arguments):
        # Print version information
        library_version = ""
        daemon_version  = ""
        try:
            library_version = self.__dbus_iface.GetLibraryVersion()
        except dbus.DBusException, e:
            self.__print_error(_("Failed to get library version: %s") % (e.get_dbus_message()))
            return False
        try:
            daemon_version = self.__dbus_iface.GetDaemonVersion()
        except dbus.DBusException, e:
            self.__print_error(_("Failed to get daemon version: %s") % (e.get_dbus_message()))
            return False
                
        print _("Library version: %s") % (str(library_version))
        print _("Daemon version: %s")  % (str(daemon_version))
        
        return True
        
    def __connect (self):
        try:
            if self.__bus_type == "system":
                self.__dbus_bus = dbus.SystemBus()
            elif self.__bus_type == "session":
                self.__dbus_bus = dbus.SessionBus()
            else:
                self.__print_warning(_("Invalid bus parameter '%s', using default!") % (self.__bus_type))
                # Use system bus by default
                self.__bus_type = "system"
                self.__dbus_bus = dbus.SystemBus()
                
            self.__dbus_proxy = self.__dbus_bus.get_object("net.sf.cdemu.CDEMUD_Daemon", "/CDEMUD_Daemon")
            self.__dbus_iface = dbus.Interface(self.__dbus_proxy, "net.sf.cdemu.CDEMUD_Daemon")
        except dbus.DBusException, e:
            self.__print_error(_("Failed to connect to CDEmu daemon: %s") % (e.get_dbus_message()))
            return False
        
        daemon_version = self.__dbus_iface.GetDaemonVersion()
        if daemon_version < config.min_daemon_version:
            self.__print_error(_("CDEmu daemon version %s detected, but at least version %s is required!") % (daemon_version, config.min_daemon_version))
            return False
        
        return True
        
    def __print_command_usage (self, command):
        for cur_command in self.__commands:
            if cur_command[0] == command:
                # We need to make sure we're not trying to 'translate' empty 
                # string for the third argument
                print _("Usage: %s %s %s") % ("cdemu", cur_command[0], cur_command[1] != "" and _(cur_command[1]) or "")
        
        return
        
    def __print_full_usage (self):
        print _("Usage: %s [options] <command> <command parameters>") % ("cdemu")
        print ""
        print _("Commands:")
        for cur_command in self.__commands:
            print "  %-25s %s" % (cur_command[0], _(cur_command[2]))
        print ""
        print _("Options:")
        print "  %-25s %s" % ("-h, --help", _("displays help message"))
        print "  %-25s %s" % ("-v, --version", _("displays program version"))
        print "  %-25s %s" % ("-b, --bus", _("sets D-BUS bus type to use; valid values are 'session' and 'system'"))
        
        return
        
    def __print_invalid_number_of_parameters (self, command):
        self.__print_error(_("Invalid number of parameters for command '%s'!") % (command))
        self.__print_command_usage(command)
        return
        
    def __print_version (self):
        print "%s %s - (C) Rok Mandeljc" % (config.name, config.version)
        return
        
    def __print_error (self, message):
        print _("ERROR: %s") % (message)
        return
        
    def __print_warning (self, message):
        print _("WARNING: %s") % (message)
        return
    
    # Commands
    __commands = [
        # Load
        [
            "load",
            _("<device> <image file> [...]"),    
            _("loads the device"),
            __cmd_load_device
        ],
        # Unload
        [
            "unload",
            _("<device>"),
            _("unloads the device"),
            __cmd_unload_device
        ],
        # Status
        [
            "status",
            "",
            _("displays the devices' status"),
            __cmd_display_status
        ],
        # Device mapping
        [
            "device-mapping",
            "",
            _("displays the device mapping information"),
            __cmd_device_mapping
        ],
        # Daemon-debug-mask
        [
            "daemon-debug-mask",
            _("<device> [new value]"),
            _("displays/sets daemon debug mask"),
            __cmd_daemon_debug_mask
        ],
        # Library-debug-mask
        [
            "library-debug-mask",
            _("<device> [new value]"),
            _("displays/sets library debug mask"),
            __cmd_library_debug_mask
        ],
        # DPM-emulation
        [
            "dpm-emulation",
            _("<device> [new value]"),
            _("displays/sets DPM emulation flag"),
            __cmd_dpm_emulation
        ],
        # TR-emulation
        [
            "tr-emulation",
            _("<device> [new value]"),
            _("displays/sets transfer rate emulation flag"),
            __cmd_tr_emulation
        ],
        # Device-ID
        [
            "device-id",
            _("<device> [new vendor_id] [new product_id] [new revision] [new vendor_specific]"),
            _("displays/sets device ID"),
            __cmd_device_id
        ],
        # Enum-supported-parsers
        [
            "enum-supported-parsers",
            _("[parser_id]"),
            _("enumerates supported parsers"),
            __cmd_enum_supported_parsers
        ],
        # Enum-supported-fragments
        [
            "enum-supported-fragments",
            _("[fragment_id]"),
            _("enumerates supported fragments"),
            __cmd_enum_supported_fragments
        ],
        # Enum-daemon-debug-masks
        [
            "enum-daemon-debug-masks",
            "",
            _("enumerates valid daemon debug masks"),
            __cmd_enum_daemon_debug_masks
        ],
        # Enum-library-debug-masks
        [
            "enum-library-debug-masks",
            "",
            _("enumerates valid library debug masks"),
            __cmd_enum_library_debug_masks
        ],
        # Version
        [
            "version",
            "",
            _("displays version information"),
            __cmd_version
        ]
    ]
