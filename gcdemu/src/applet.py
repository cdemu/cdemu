# -*- coding: utf-8 -*-
# gCDEmu: Applet class
# Copyright (C) 2006-2007 Rok Mandeljc
#
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

import sys
import traceback

import gconf
import gobject
import gtk, gtk.gdk
import gnome.ui
import gnomeapplet

from gettext import gettext as _

import dbus
from dbus.mainloop.glib import DBusGMainLoop
DBusGMainLoop(set_as_default=True)

try:
    import pynotify
    has_pynotify = True
except:
    print "You don't seem to have pynotify installed; notifications disabled."
    has_pynotify = False

# Internals
import globals as config
from ui import gCDEmu_FileOpenDialog
from ui import gCDEmu_Menu
from ui import gCDEmu_DevicePropertiesDialog

class gCDEmu_Applet (gnomeapplet.Applet):
    "gCDEmu applet"
    
    # *************************************************************************
    # *                              Variables                                *
    # *************************************************************************
    # *** Configuration ***
    __gconf_client = None
    __gconf_key_path = None

    __show_notifications = True
    __use_system_bus = False
    
    # *** UI ***
    __pixbuf_logo = None
    __pixbuf_icon = None
    __image = None
    
    __panel_size = 24
        
    # *** Dynamic facilities ***
    __dbus_bus = None
    __dbus_iface = None

    __connected = False

    __debug_masks = []
    __supported_parsers = []
    __supported_fragments = []
    __number_of_devices = 0
    __devices = []
    __device_properties = []
    
    __menu = None
    
    __open_file_dialog = None
    
    # *************************************************************************
    # *                                 Init                                  *
    # *************************************************************************
    def __init__(self):
        # GObject subclassing...
        self.__gobject_init__()
        #gnomeapplet.Applet.__init__(self)

    def setup_applet (self):
        # *** Init applet ***
        self.set_applet_flags(gnomeapplet.EXPAND_MINOR)
                                    
        # *** Init UI ***
        self.__pixbuf_logo = gtk.gdk.pixbuf_new_from_file(config.image_dir + "gcdemu-logo.png")
        self.__pixbuf_icon = gtk.gdk.pixbuf_new_from_file(config.image_dir + "gcdemu-icon.png")

        self.__image = gtk.Image()
        self.add(self.__image)
        
        self.__open_file_dialog = gCDEmu_FileOpenDialog()
        
        # *** Set up popup menu ***
        popup_menu_xml = "<popup name='button3'>"
        popup_menu_xml += "<menuitem name='system_bus' verb='system_bus' label='%s' type='toggle'/>" % (_("Use _system bus"))
        if has_pynotify:
            popup_menu_xml += "<menuitem name='notifications' verb='notifications' label='%s' type='toggle'/>" % (_("Show _Notifications"))
        popup_menu_xml += "<separator />"
        popup_menu_xml += "<menuitem name='about' verb='about' label='%s' pixtype='stock' pixname='gnome-stock-about' />" % (_("_About"))
        popup_menu_xml += "</popup>"
        
        # We will install custom UI handlers for other menu elements, so verbs
        # are not needed
        popup_menu_verbs = [
            ( "about", lambda w,d: self.__verb_about() ),
        ]
        
        popup = self.get_popup_component()
        popup.connect("ui-event", self.__cb_popup_ui_event)

        # Setup menu        
        self.setup_menu(popup_menu_xml, popup_menu_verbs, None)
                
        self.show_all()
        
        self.connect("size-allocate", self.__cb_applet_size_allocate) # The best way (TM) to monitor panel size change; "change-size" is useless...
        self.connect("button-press-event", self.__cb_applet_button_press_event)        
        
        # *** Init G-conf ***
        self.add_preferences("/schemas/apps/gcdemu-applet/prefs");
        self.__gconf_client = gconf.client_get_default()
        self.__gconf_key_path = self.get_preferences_key()
        if self.__gconf_key_path == None:
            self.__gconf_key_path = "/apps/gcdemu-applet-standalone"
        
        # *** Init PyNotify ***
        if has_pynotify:            
            if pynotify.init(config.name):
                # Load notifications setting
                self.__show_notifications = self.__gconf_client.get_bool(self.__gconf_key_path + "/notifications")
                print "Show notifications: ", self.__show_notifications
                # Set 'notifications' checkbox on popup menu
                popup.set_prop("/commands/notifications", "state", "%i" % (self.__show_notifications))
            else:
                print "There was a problem initializing the pynotify module."
                self.__show_notifications = False
        else:
            self.__show_notifications = False
        
        # Which bus to use...
        self.__use_system_bus = self.__gconf_client.get_bool(self.__gconf_key_path + "/use_system_bus")
        popup.set_prop("/commands/system_bus", "state", "%i" % (self.__use_system_bus))
                    
        # Try to connect
        print "Trying to connect..."
        self.__switch_bus() # Set up appropriate bus
        self.__connect_to_daemon() # Connect
        
        return True
    
    # *************************************************************************
    # *                            D-BUS Callbacks                            *
    # *************************************************************************
    def __cb_dbus_daemon_change (self, change_type):
        print "Daemon change callback... type = %i" % (change_type)
        if change_type == 1:
            # *** Daemon started ***

            # Use notification (if user didn't say she doesn't want us to)...
            if self.__show_notifications:
                n = pynotify.Notification(
                    _("Daemon started"), 
                    _("CDEmu daemon has been started."), 
                    None,
                    self
                )
                n.show()
        
            # Check if we happen to think we're connected (can happen if e.g. server
            # has crashed before; we need to clean dynamic facilities)
            if self.__connected:
                print "Got 'Daemon started' signal but we think we're connected already... :/"
                self.__cleanup_dynamic_facilities()
        
            # Connect
            self.__connect_to_daemon()
        elif change_type == 2:
            # *** Daemon stopped ***
            
            # Use notification (if user didn't say she doesn't want us to)...
            if self.__show_notifications:
                n = pynotify.Notification(
                    _("Daemon stopped"), 
                    _("CDEmu daemon has been stopped."), 
                    None,
                    self
                )
                n.show()
            
            # Clear the facilities
            self.__cleanup_dynamic_facilities()
        else:
            print "Unknown change type!"
            return
            
        # Refresh icon
        self.__refresh_icon()
            
    def __cb_dbus_device_change (self, i, change_type):
        device = self.__devices[i]
        
        if change_type == 1:
            # *** Status ***
            
            # Update device status
            (device[1], device[2], device[3]) = self.__dbus_iface.DeviceGetStatus(i)
                        
            # Use notification (if user didn't say she doesn't want us to)...
            if self.__show_notifications:
                if device[1]:
                    str = _("Device %i has been loaded.") % (i)
                else:
                    str = _("Device %i has been emptied.") % (i)
            
                n = pynotify.Notification(
                    _("Device change"), 
                    str, 
                    None,
                    self
                )
                n.show()
        elif change_type == 2:
            # *** Daemon debug mask ***
            
            # Update device debug mask
            device[4] = self.__dbus_iface.DeviceGetDebugMask(i, "daemon")
            
            # Use notification (if user didn't say she doesn't want us to)...
            if self.__show_notifications:
                n = pynotify.Notification(
                    _("Device change"), 
                    _("Device %i has been changed its daemon debug mask.") % (i), 
                    None,
                    self
                )
                n.show()
        elif change_type == 3:
            # *** Library debug mask ***
            # Update device debug mask
            device[5] = self.__dbus_iface.DeviceGetDebugMask(i, "library")
            
            # Use notification (if user didn't say she doesn't want us to)...
            if self.__show_notifications:
                n = pynotify.Notification(
                    _("Device change"), 
                    _("Device %i has been changed its library debug mask.") % (i), 
                    None,
                    self
                )
                n.show()
        else:
            print "Unknown change type!"
            return
        
        # Force update on menu and appropriate device properties dialog
        self.__menu.set_device_data(device)
        self.__device_properties[i].set_device_data(device)
    
    # *************************************************************************    
    # *                             UI Callbacks                              *
    # *************************************************************************    
    def __cb_applet_button_press_event (self, widget, event):
        if event.button == 1:
            # Button 1: Display menu    
            if self.__menu:
                self.__menu.popup(None, None, self.__calculate_popdown_menu_position, event.button, event.time)
                #self.__menu.popup(None, None, None, event.button, event.time, None)
    
    def __cb_popup_ui_event (self, bonobo_component, verb, event_type, state_string):
        # For some reason, it seems that callback is called every time menu is
        # dropped and one of the toggle buttons is set; we need to do some additional
        # filtering (i.e. compare state string with current flag setting)
        if verb == "notifications":
            if bool(int(state_string)) != self.__show_notifications:
                print "Notifications flag change"
                # Does user want us to use notifications?
                self.__show_notifications = bool(int(state_string))
                # Store the settings
                self.__gconf_client.set_bool(self.__gconf_key_path + "/notifications", self.__show_notifications)
        elif verb == "system_bus":
            if bool(int(state_string)) != self.__use_system_bus:
                print "System bus flag change"
                # Should we use system bus?
                self.__use_system_bus = bool(int(state_string))
                # Store the settings
                self.__gconf_client.set_bool(self.__gconf_key_path + "/use_system_bus", self.__use_system_bus)
                # Switch bus and reconnect
                self.__switch_bus()
                self.__cleanup_dynamic_facilities()
                self.__connect_to_daemon()
        else:
            print "Unknown verb: %s!" % (verb)
        
        return True
        
    def __cb_applet_size_allocate (self, applet, allocation):
        orient = applet.get_orient()
        
        if orient == gnomeapplet.ORIENT_UP or orient == gnomeapplet.ORIENT_DOWN:
            if applet.allocation.height == self.__panel_size:
                return
            else:
                self.__panel_size = applet.allocation.height
        else:
            if applet.allocation.width == self.__panel_size:
                return
            else:
                self.__panel_size = applet.allocation.width
        
        self.__refresh_icon()

        return
    
    def __verb_about (self):
        about = gnome.ui.About(
            "gCDEmu",
            config.version,
            "Copyright (C) 2006-2007 Rok Mandeljc",
            _("gCDEmu is an applet for controlling CDEmu devices via CDEmu daemon."),
            ["Rok Mandeljc <rok.mandeljc@email.si>"],
            None,
            _("translator-credits"),
            self.__pixbuf_logo
        )
        about.run()
        return
    
    # *************************************************************************    
    # *                           Private functions                           *
    # *************************************************************************
    def __calculate_popdown_menu_position (self, menu, data=None):    
        (x, y) = self.get_parent_window().get_origin()
        (menu_w, menu_h) = menu.size_request()
        (applet_w, applet_h) = (self.allocation.width, self.allocation.height)
        orient = self.get_orient()
        
        if orient == gnomeapplet.ORIENT_DOWN:
            y += applet_h
            x -= (menu_w - applet_w) / 2
        elif orient == gnomeapplet.ORIENT_UP:
            y -= menu_h
            x -= (menu_w - applet_w) / 2
        elif orient == gnomeapplet.ORIENT_RIGHT:
            y -= (menu_h - applet_h) / 2
            x += applet_w
        elif orient == gnomeapplet.ORIENT_LEFT:
            y -= (menu_h - applet_h) / 2
            x -= menu_w
            
        return (x, y, False)
    
    def __refresh_icon (self):
        size = self.__panel_size - 4
        
        scaled_pixbuf = self.__pixbuf_icon.scale_simple(size, size, gtk.gdk.INTERP_TILES)
        
        if self.__connected == False:
            # Desaturate (= greyscale) the image
            scaled_pixbuf.saturate_and_pixelate(scaled_pixbuf, 0.00, False)
                
        self.__image.set_from_pixbuf(scaled_pixbuf)
    
    def __conditional_reconnect (self):
        "Checks whether daemon responds to Hail, and if it doesn't, it attempts to reconnect"
        try:
            self.__dbus_iface.Hail()
        except:
            print "Daemon doesn't respond to Hail... trying to reconnect"
            # Connection failed, try to reconnect
            self.__cleanup_dynamic_facilities()
            self.__connect_to_daemon()
                
    def __cleanup_dynamic_facilities (self):
        "Clears dynamic facilities (ones that should be initialized every time daemon is started)"
                
        # Some variables
        self.__connected = False
        
        self.__debug_masks = []
        self.__supported_parsers = []
        self.__supported_fragments = []
        self.__number_of_devices = 0
        self.__devices = []
        
        # Cleanup the open file dialog
        self.__open_file_dialog.cleanup()
        
        # Menu (dynamic because number of devices could change)
        if self.__menu:
            self.__menu.destroy()
        self.__menu = None
        
        # Device properties dialogs (dynamic because number of devices could change)
        if self.__device_properties:
            for dialog in self.__device_properties:
                dialog.destroy()
        self.__device_properties = []
        
        # D-BUS proxy and interface
        self.__dbus_proxy_obj = None
        self.__dbus_iface = None
        
        return
    
    def __switch_bus (self):
        "Switches the D-BUS bus"
        
        # Close existing D-BUS connection
        if self.__dbus_bus:
            self.__dbus_bus.remove_signal_receiver(self.__cb_dbus_daemon_change,
                'DaemonChange',
                'net.sf.cdemu.CDEMUD_Daemon',
                None, # Don't request interface because we don't need it at this point
                '/CDEMUD_Daemon')
            self.__dbus_bus.remove_signal_receiver(self.__cb_dbus_device_change,
                'DeviceChange',
                'net.sf.cdemu.CDEMUD_Daemon',
                None, # Don't request interface because we don't need it at this point
                '/CDEMUD_Daemon')
            self.__dbus_bus = None
        
         # Init D-BUS connection
        if self.__use_system_bus == True:
            print "Using system bus"
            self.__dbus_bus = dbus.SystemBus()
        else:
            print "Using session bus"
            self.__dbus_bus = dbus.SessionBus()
            
        # Set up signal handlers
        self.__dbus_bus.add_signal_receiver(self.__cb_dbus_daemon_change,
            'DaemonChange',
            'net.sf.cdemu.CDEMUD_Daemon',
            None, # Don't request interface because we don't need it at this point
            '/CDEMUD_Daemon')
        self.__dbus_bus.add_signal_receiver(self.__cb_dbus_device_change,
            'DeviceChange',
            'net.sf.cdemu.CDEMUD_Daemon',
            None, # Don't request interface because we don't need it at this point
            '/CDEMUD_Daemon')
        
        return
    
    def __connect_to_daemon (self):
        "Tries to connect to daemon and sets up dynamic facilities if successful"
        
        try:
            # Get D-BUS interface
            self.__dbus_proxy_obj = self.__dbus_bus.get_object('net.sf.cdemu.CDEMUD_Daemon', '/CDEMUD_Daemon')
            self.__dbus_iface = dbus.Interface(self.__dbus_proxy_obj, 'net.sf.cdemu.CDEMUD_Daemon')

            # Retrieve list of supported parsers and fragments
            self.__supported_parsers = self.__dbus_iface.GetSupportedParsers()
            self.__supported_fragments = self.__dbus_iface.GetSupportedFragments()
            
            # Build list of supported images that can be fed to our open file 
            # as filter list
            supported_images = []
            for parser in self.__supported_parsers:
                supported_images.append([parser[5], parser[6]])
            self.__open_file_dialog.add_filters(supported_images)
            
            # Retrieive list of daemon debug masks for devices
            self.__debug_masks_d = self.__dbus_iface.GetDeviceDebugMasks("daemon")
            # Retrieve list of library debug masks for devices 
            self.__debug_masks_l = self.__dbus_iface.GetDeviceDebugMasks("library")
            
            # Retrieive number of devices
            self.__number_of_devices = self.__dbus_iface.GetNumberOfDevices()
            
            # Create menu (now that we have number of devices)
            self.__menu = gCDEmu_Menu(self)
            
            # Get information for each device
            for i in range (0, self.__number_of_devices):
                new_device = ["Device number", "Status", "Image type", "Filenames", "Module debug mask", "Daemon debug mask", "Library debug mask"]
                
                new_device[0] = i
                        
                (new_device[1], new_device[2], new_device[3]) = self.__dbus_iface.DeviceGetStatus(i)
                new_device[4] = self.__dbus_iface.DeviceGetDebugMask(i, "daemon")
                new_device[5] = self.__dbus_iface.DeviceGetDebugMask(i, "library")
                    
                # Set device data to menu
                self.__menu.set_device_data(new_device)
                
                # Create device properties dialog for device
                properties_dialog = gCDEmu_DevicePropertiesDialog(self, new_device)
                self.__device_properties.append(properties_dialog)
                
                # Add to list
                self.__devices.append(new_device)

            self.__connected = True
        except dbus.DBusException, e:
            print "Failed to connect: daemon most likely not running! %s" % e
            self.__cleanup_dynamic_facilities()
        except:
            print "Failed to connect:"
            traceback.print_exc()
            self.__cleanup_dynamic_facilities()
        
        # Refresh icon
        self.__refresh_icon()
            
    def __device_unload (self, i):
        try:
            self.__dbus_iface.DeviceUnload(i)
        except:
            # Show error dialog
            message = gtk.MessageDialog(
                None, 
                0, 
                gtk.MESSAGE_ERROR, 
                gtk.BUTTONS_CLOSE,
                _("Failed to unload device %i:\n%s") % (i, sys.exc_value))
            message.set_title(_("Error"))
            message.run()
            message.destroy()
            # Check the connection
            self.__conditional_reconnect()
        
    def __device_load (self, i):                
        # Run dialog, get filename and load the image
        if self.__open_file_dialog.run() == gtk.RESPONSE_ACCEPT:
            # Hide dialog while we do the loading...
            self.__open_file_dialog.hide()
            # Get filename
            filenames = self.__open_file_dialog.get_filenames()
            # Try loading
            try:
                self.__dbus_iface.DeviceLoad(i, filenames)
            except:
                # Show error dialog
                message = gtk.MessageDialog(
                    None, 
                    0, 
                    gtk.MESSAGE_ERROR, 
                    gtk.BUTTONS_CLOSE,
                    _("Failed to load image %s to device %i:\n%s") % (";".join(filenames), i, sys.exc_value))
                message.set_title(_("Error"))
                message.run()
                message.destroy()
                # Check the connection
                self.__conditional_reconnect()
                
        self.__open_file_dialog.hide()
        
    # *************************************************************************
    # *                           Public functions                            *
    # *************************************************************************
    def get_number_of_devices (self):
        return self.__number_of_devices
    
    def get_debug_masks (self):
        return (self.__debug_masks_d, self.__debug_masks_l)
    
    def get_supported_images (self):
        return self.__supported_images
    
    def device_load_unload (self, i):
        "Loads device if it's unloaded and unloads it if it's loaded."
        if self.__devices[i][1]:
            self.__device_unload(i)
        else:
            self.__device_load(i)
    
    def device_set_debug_mask (self, i, type, value):
        try:
            self.__dbus_iface.DeviceSetDebugMask(i, type, value)
        except:
            # Show error dialog
            message = gtk.MessageDialog(
                None, 
                0, 
                gtk.MESSAGE_ERROR, 
                gtk.BUTTONS_CLOSE,
                _("Failed to set debug mask (type: '%s') for device %i to 0x%X:\n%s") % (type, i, value, sys.exc_value))
            message.set_title(_("Error"))
            message.run()
            message.destroy()
            # Check the connection
            self.__conditional_reconnect()
    
    def device_show_properties (self, i):
        self.__device_properties[i].set_device_data(self.__devices[i])
        self.__device_properties[i].run()
        # It can happen that applet gets cleaned while dialog is being run,
        # and all this gets cleaned up...
        if self.__device_properties and self.__device_properties[i]:
            self.__device_properties[i].hide()

gobject.type_register(gCDEmu_Applet)
