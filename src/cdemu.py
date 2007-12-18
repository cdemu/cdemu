# -*- coding: utf-8 -*-
# CDEmu: simple command line client
# Copyright (C) 2006-2007 Rok Mandeljc

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

import dbus
from dbus.mainloop.glib import DBusGMainLoop
DBusGMainLoop(set_as_default=True)

from gettext import gettext as _

import globals as config

class CDEmu (object):
    # D-BUS
    __dbus_bus = None
    __dbus_proxy = None
    __dbus_iface = None
    
    __use_system_bus = False
    
    def __init__ (self):
        return
    
    def process_command (self, argv):
        print_usage = False
        
        # Separate options from arguments
        try:
            options, arguments = getopt.gnu_getopt(argv, "hvb:", ["help", "version", "bus="])
        except getopt.GetoptError:
            self.__print_error(_("Unknown option"))
            self.__print_full_usage()
            return
                
        # Go through options    
        for opt, arg in options:
            if opt in ("-h", "--help"):
                print_usage = True
            elif opt in ("-v", "--version"):
                self.__print_version()
                return
            elif opt in ("-b", "--bus"):
                # Bus type
                if arg == "session":
                    self.__use_system_bus = False
                elif arg == "system":
                    self.__use_system_bus = True                    
                else:
                    self.__print_warning(_("Invalid bus parameter '%s', using session bus!") % (arg))
                    self.__use_system_bus = False
        
        if len(arguments) == 0:
            self.__print_full_usage()
            return
        
        # Connect        
        if self.__connect() != True:
            self.__print_error(_("Failed to connect to daemon!"))
            return
        
        # Command switch
        for command in self.__commands:
            if command[0] == arguments[0]:
                if print_usage:
                    self.__print_command_usage(command[0])
                else:
                    command[3](self, arguments[1:])
                return
                
        self.__print_error(_("Unknown command: %s") % (arguments[0]))
        self.__print_full_usage()
        
        return

    # Command handlers
    def __load_device (self, arguments):
        if len(arguments) < 2:
            self.__print_invalid_number_of_parameters("load")
            return
        
        # Load device
        device = int(arguments[0])
        filenames = arguments[1:]
        
        # We need to pass absolute filenames to daemon, because its working
        # directory is (99.9% of the time, anyway) different from ours...
        for i in range(0, len(filenames)):
            filenames[i] = os.path.abspath(filenames[i])
        
        try:
            self.__dbus_iface.DeviceLoad(device, filenames)
        except:
            self.__print_error(_("Failed to load image: %s") % (sys.exc_value))
            return
    
    def __unload_device (self, arguments):
        if len(arguments) != 1:
            self.__print_invalid_number_of_parameters("unload")
            return
        
        # Unload device
        device = int(arguments[0])
        
        try:
            self.__dbus_iface.DeviceUnload(device)
        except:
            self.__print_error(_("Failed to unload device %i: %s") % (device, sys.exc_value))
            return

    def __display_status (self, arguments):
        # Print status for all devices
        try:
            nr_devices = self.__dbus_iface.GetNumberOfDevices()
        except:
            self.__print_error(_("Failed to get number of devices: %s") % (sys.exc_value))
            return
        
        print _("Devices status:")
        print "%-5s %-10s %-10s %s" % (_("DEV"), _("LOADED"), _("TYPE"), _("FILENAME"))
        for device in range (0, nr_devices):
            try:
                [loaded, image_type, filenames] = self.__dbus_iface.DeviceGetStatus(device)
            except:
                self.__print_error(_("Failed to get status for device %i: %s") % (device, sys.exc_value))
                continue
            
            # First line is for all device's data, the rest are for additional filenames
            print "%-5s %-10s %-10s %s" % (device, loaded, image_type, filenames[0])
            for i in range(1, len(filenames)):
                print "%-5s %-10s %-10s %s" % ("", "", "", filenames[i])
                    
    def __device_debug (self, arguments):
        # Get debug mask
        if len(arguments) == 2:
            type = arguments[1]
            # Particular device vs. all devices
            if arguments[0] == "all":
                try:
                    nr_devices = self.__dbus_iface.GetNumberOfDevices()
                except:
                    self.__print_error(_("Failed to get number of devices: %s") % (sys.exc_value))
                    return
                
                print _("Debug masks for devices (type: '%s'):") % (type)
                print "%-5s %-10s" % (_("DEV"), _("DEBUG MASK"))
                
                for device in range(0, nr_devices):
                    try:
                        dbg_mask = self.__dbus_iface.DeviceGetDebugMask(device, type)
                    except:
                        self.__print_error(_("Failed to get debug mask for device %i: %s") % (device, sys.exc_value))
                        continue
                        
                    print "%-5s 0x%04X" % (device, dbg_mask)
            else:
                device = string.atoi(arguments[0])
                try:
                    dbg_mask = self.__dbus_iface.DeviceGetDebugMask(device, type)
                except:
                    self.__print_error(_("Failed to get debug mask for device %i: %s") % (device, sys.exc_value))
                    sys.exit(-1)
                
                print _("Debug mask for device %i: 0x%X (type: '%s')") % (device, dbg_mask, type)
        # Set debug mask
        elif len(arguments) == 3:
            type = arguments[1]
            dbg_mask = string.atoi(arguments[2], 0)
            if arguments[0] == "all":
                try:
                    nr_devices = self.__dbus_iface.GetNumberOfDevices()
                except:
                    self.__print_error(_("Failed to get number of devices: %s") % (sys.exc_value))
                    return
                
                print _("Setting debug mask for all devices to 0x%X (type: '%s').") % (dbg_mask, type)
                for device in range(0, nr_devices):
                    try:
                        self.__dbus_iface.DeviceSetDebugMask(device, type, dbg_mask)
                    except:
                        self.__print_error(_("Failed to set debug mask for device %i to 0x%X: %s") % (device, dbg_mask, sys.exc_value))
                        continue
            else:
                device = string.atoi(arguments[0], 0)
                print _("Setting debug mask for device %i to 0x%X (type: '%s').") % (device, dbg_mask, type)
                try:
                    self.__dbus_iface.DeviceSetDebugMask(device, type, dbg_mask)
                except:
                    self.__print_error(_("Failed to set debug mask for device %i to 0x%X: %s") % (device, dbg_mask, sys.exc_value))
                    return
        else:
            self.__print_invalid_number_of_parameters("device-debug")
            return
    
    def __supported_parsers (self, arguments):
        # Display supported parsers
        try:
            parsers = self.__dbus_iface.GetSupportedParsers()
        except:
            self.__print_error(_("Failed to get list of supported parsers: %s") % (sys.exc_value))
            return
        
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
                return
        else:
            self.__print_invalid_number_of_parameters("supported-parsers")
            return
    
    def __supported_fragments (self, arguments):
        # Display supported fragments
        try:
            fragments = self.__dbus_iface.GetSupportedFragments()
        except:
            self.__print_error(_("Failed to get list of supported fragments: %s") % (sys.exc_value))
            return
        
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
                return
        else:
            self.__print_invalid_number_of_parameters("supported-fragments")
            return
    
    def __device_debug_masks (self, arguments):
        if len(arguments) != 1:
            self.__print_invalid_number_of_parameters("device-debug-masks")
            return
        
        type = arguments[0]
        
        # Print module's debug masks
        try:
            debug_masks = self.__dbus_iface.GetDeviceDebugMasks(type)
        except:
            self.__print_error(_("Failed to get list of supported device debug masks: %s") % (sys.exc_value))
            return
        
        print _("Supported debug masks (type: '%s'):") % (type)
        for debug_mask in debug_masks:
            print "  %-25s: 0x%04X" % (debug_mask[0], debug_mask[1])
    
    def __version (self, arguments):
        # Print version information
        library_version = ""
        daemon_version  = ""
        try:
            library_version = self.__dbus_iface.GetVersion("library")
        except:
            self.__print_error(_("Failed to get library version: %s") % (sys.exc_value))
            sys.exit(-1)
        try:
            daemon_version = self.__dbus_iface.GetVersion("daemon")
        except:
            self.__print_error(_("Failed to get daemon version: %s") % (sys.exc_value))
            sys.exit(-1)
                
        print _("Library version: %s") % (str(library_version))
        print _("Daemon version: %s")  % (str(daemon_version))

    def __connect (self):
        try:
            if self.__use_system_bus:
                self.__dbus_bus = dbus.SystemBus()
            else:
                self.__dbus_bus = dbus.SessionBus()                
            self.__dbus_proxy = self.__dbus_bus.get_object("net.sf.cdemu.CDEMUD_Daemon", "/CDEMUD_Daemon")
            self.__dbus_iface = dbus.Interface(self.__dbus_proxy, "net.sf.cdemu.CDEMUD_Daemon")
        except:
            self.__print_error(_("Failed to connect to CDEmu daemon: %s") % (sys.exc_value))
            return False
            
        return True
        
    def __print_command_usage (self, command):
        for cur_command in self.__commands:
            if cur_command[0] == command:
                # We need to make sure we're not trying to 'translate' empty 
                # string for the third argument
                print _("Usage: %s %s %s") % ("cdemu", cur_command[0], cur_command[1] != "" and _(cur_command[1]) or "")
    
    def __print_full_usage (self):
        print _("Usage: %s [options] <command> <command parameters>") % ("cdemu")
        print ""
        print _("Commands:")
        for cur_command in self.__commands:
            print "  %-20s %s" % (cur_command[0], _(cur_command[2]))
        print ""
        print _("Options:")
        print "  %-20s %s" % ("-h, --help", _("displays help message"))
        print "  %-20s %s" % ("-v, --version", _("displays program version"))
        print "  %-20s %s" % ("-b, --bus", _("sets D-BUS bus type to use; valid values are 'session' and 'system'"))
    
    def __print_invalid_number_of_parameters (self, command):
        self.__print_error(_("Invalid number of parameters for command '%s'!") % (command))
        self.__print_command_usage(command)
    
    def __print_version (self):
        print "%s %s - (C) Rok Mandeljc" % (config.name, config.version)
    
    def __print_error (self, message):
        print _("ERROR: %s") % (message)
    
    def __print_warning (self, message):
        print _("WARNING: %s") % (message)
    
    # Commands
    __commands = [
        # Load
        [
            "load",
            _("<device> <image file> [...]"),    
            _("loads the device"),
            __load_device
        ],
        # Unload
        [
            "unload",
            _("<device>"),
            _("unloads the device"),
            __unload_device
        ],
        # Status
        [
            "status",
            "",
            _("displays the devices' status"),
            __display_status
        ],
        # Device-debug
        [
            "device-debug",
            _("<device> <type> [new value]"),
            _("displays/sets debug mask for the device"),
            __device_debug
        ],
        # Supported-parsers
        [
            "supported-parsers",
            "[parser_id]",
            _("displays the information about supported parsers"),
            __supported_parsers
        ],
        # Supported-fragments
        [
            "supported-fragments",
            "[fragment_id]",
            _("displays the information about supported fragments"),
            __supported_fragments
        ],
        # Device-debug-masks
        [
            "device-debug-masks",
            "<type>",
            _("displays the list of valid debug masks"),
            __device_debug_masks
        ],
        # Version
        [
            "version",
            "",
            _("displays version information"),
            __version
        ]
    ]
    
if __name__ == "__main__":
    cdemu = CDEmu()
    cdemu.process_command(sys.argv[1:])
