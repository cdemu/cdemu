# -*- coding: utf-8 -*-
# gCDEmu: Applet class
# Copyright (C) 2006-2009 Rok Mandeljc
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
import os
import traceback

import gconf
import gobject
import gtk, gtk.gdk
import gnome
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
from device import gCDEmu_Device

class gCDEmu_Applet (gnomeapplet.Applet):    
    def __init__(self):
        self.__gobject_init__()
        return

    def __cb_gconf_client_notify (self, client, id, entry, user_data):        
        if entry.get_key() == self.__gconf_key_path + "/icon_name":
            icon_name = entry.get_value().get_string()
            print "Icon name changed to: %s" % (icon_name)
                        
            try:
                self.__pixbuf_icon = gtk.gdk.pixbuf_new_from_file_at_size(config.image_dir + "/" + icon_name, 204, 204)
            except gobject.GError, e:
                message = gtk.MessageDialog(None, 0, gtk.MESSAGE_ERROR, gtk.BUTTONS_CLOSE, e)
                message.set_title(_("Error"))
                message.run()
                message.destroy()
            
            self.__refresh_icon()
            
        elif entry.get_key() == self.__gconf_key_path + "/notifications":
            value = entry.get_value().get_bool()
            print "Show notifications changed to: %i" % (value)
            
            # Does user want us to use notifications?
            self.__show_notifications = value
            
            # Update popup menu
            self.__popup.set_prop("/commands/notifications", "state", "%i" % (self.__show_notifications))
        elif entry.get_key() == self.__gconf_key_path + "/use_system_bus":
            value = entry.get_value().get_bool()
            print "Use system bus changed to: %i" % (value)
            
            # Should we use system bus?
            self.__use_system_bus = value
            
            # Update popup menu
            self.__popup.set_prop("/commands/system_bus", "state", "%i" % (self.__use_system_bus))

            # Switch bus and reconnect
            self.__switch_bus()
            self.__cleanup()
            self.__connect()
        else:
            print "Unhandled key: %s\n" % (entry.get_key())
        
    def __display_error (self, text):
         # Show error dialog
        message = gtk.MessageDialog(None, 0, gtk.MESSAGE_ERROR, gtk.BUTTONS_CLOSE, text)
        message.set_title(_("Error"))
        message.run()
        message.destroy()
        
        return
        
    def setup_applet (self):
        # Initialize variables
        self.__gconf_client = None
        self.__gconf_key_path = None

        self.__show_notifications = True
        self.__use_system_bus = True
        
        self.__about = None
        
        self.__pixbuf_logo = None
        self.__pixbuf_icon = None
        self.__image = None
    
        self.__panel_size = 24
        
        self.__dbus_bus = None
        self.__dbus_iface = None

        self.__connected = False
        self.__signal_started = None
    
        self.__menu = None
        self.__devices = []
        
        # Init applet
        self.set_applet_flags(gnomeapplet.EXPAND_MINOR)
        
        # Init G-conf
        self.add_preferences("/schemas/apps/gcdemu-applet/prefs");
        self.__gconf_client = gconf.client_get_default()
        self.__gconf_key_path = self.get_preferences_key()
        if self.__gconf_key_path == None:
            self.__gconf_client.add_dir('/apps/gcdemu-applet-standalone/prefs', gconf.CLIENT_PRELOAD_NONE)
            self.__gconf_key_path = "/apps/gcdemu-applet-standalone/prefs"
        
        # Watch over our config
        self.__gconf_client.notify_add(self.__gconf_key_path, self.__cb_gconf_client_notify)
        
        # Logo; load the SVG, scaled to 156x156
        self.__pixbuf_logo = gtk.gdk.pixbuf_new_from_file_at_size(config.image_dir + "/" + "gcdemu.svg", 156, 156)
        
        # Icon image
        self.__image = gtk.Image()
        self.add(self.__image)
        self.set_background_widget(self)
        
        # To load the icon, we pretend that icon path g-conf key has changed and
        # let the callback do the rest for us...
        self.__gconf_client.notify(self.__gconf_key_path + "/icon_name")
        
        # Set up About dialog
        self.__about = gtk.AboutDialog()
        self.__about.set_name("gCDEmu")
        self.__about.set_version(config.version)
        self.__about.set_copyright("Copyright (C) 2006-2008 Rok Mandeljc")
        self.__about.set_comments(_("gCDEmu is an applet for controlling CDEmu\ndevices via CDEmu daemon."))
        self.__about.set_website("http://cdemu.sf.net")
        self.__about.set_website_label(_("The CDEmu project website"))
        self.__about.set_authors([ "Rok Mandeljc <rok.mandeljc@gmail.com>" ])
        self.__about.set_documenters([ "Rok Mandeljc <rok.mandeljc@gmail.com>" ])
        self.__about.set_artists([ "Rômulo Fernandes <abra185@gmail.com>" ])
        self.__about.set_translator_credits(_("translator-credits"))
        self.__about.set_logo(self.__pixbuf_logo)
            
        # Set up popup menu
        popup_menu_xml = "<popup name='button3'>"
        popup_menu_xml += "<menuitem name='system_bus' verb='system_bus' label='%s' type='toggle'/>" % (_("Use _system bus"))
        if has_pynotify:
            popup_menu_xml += "<menuitem name='notifications' verb='notifications' label='%s' type='toggle'/>" % (_("Show _notifications"))
        popup_menu_xml += "<separator />"
        popup_menu_xml += "<menuitem name='about' verb='about' label='%s' pixtype='stock' pixname='gnome-stock-about' />" % (_("_About"))
        popup_menu_xml += "<menuitem name='help' verb='help' label='%s' pixtype='stock' pixname='gtk-help' />" % (_("_Help"))
        popup_menu_xml += "</popup>"
        
        # We will install custom UI handlers for other menu elements, so verbs
        # are not needed
        popup_menu_verbs = [
            ( "about", lambda w,d: self.__verb_about() ),
            ( "help",  lambda w,d: self.__verb_help()  ),
        ]
        
        popup = self.get_popup_component()
        popup.connect("ui-event", self.__cb_popup_ui_event)
        self.__popup = popup

        # Setup menu        
        self.setup_menu(popup_menu_xml, popup_menu_verbs, None)
                
        self.show_all()
        
        self.connect("size-allocate", self.__cb_applet_size_allocate) # The best way (TM) to monitor panel size change; "change-size" is useless...
        self.connect("button-press-event", self.__cb_applet_button_press_event)        
        
        # Init PyNotify
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
        self.__connect() # Connect
        
        return True
    

    def __signal_daemon_started (self):
        self.notification(_("Daemon started"), _("CDEmu daemon has been started."))
        # Clean up old connection and start new one
        self.__cleanup()
        self.__connect()
        
        return
        
    def __signal_dbus_daemon_stopped (self):
        self.notification(_("Daemon stopped"), _("CDEmu daemon has been stopped."))
        # Clean up old connection
        self.__cleanup()
        return


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
        value = bool(int(state_string))
        if verb == "notifications":
            if value != self.__show_notifications:
                print "Notifications flag change"
                # Store the settings
                self.__gconf_client.set_bool(self.__gconf_key_path + "/notifications", value)
        elif verb == "system_bus":
            if value != self.__use_system_bus:
                print "System bus flag change"
                # Store the settings
                self.__gconf_client.set_bool(self.__gconf_key_path + "/use_system_bus", value)
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
        # Run About dialog
        self.__about.run()
        self.__about.hide()
        
        return

    def __verb_help (self):
        gnome.help_display(config.name)
        return
    
    def notification (self, summary, body, icon_name=None):
        if self.__show_notifications:
            n = pynotify.Notification(summary, body, icon_name, self)
            n.show()
        return
        
    def check_connection (self):
        try:
            self.__dbus_iface.GetDaemonVersion()
        except:
            print "Daemon doesn't respond to GetDaemonVersion... trying to reconnect"
            # Connection failed, try to reconnect
            self.__cleanup()
            self.__connect()
        
        return
    

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
        
        # Don't let icon size go below 16...
        size = max(size, 16)
        
        scaled_pixbuf = self.__pixbuf_icon.scale_simple(size, size, gtk.gdk.INTERP_TILES)
        
        if self.__connected == False:
            # Desaturate (= greyscale) the image
            scaled_pixbuf.saturate_and_pixelate(scaled_pixbuf, 0.00, False)
                
        self.__image.set_from_pixbuf(scaled_pixbuf)
    
    def __cleanup (self):                
        # Some variables
        self.__connected = False
        
        # Destroy devices
        for device in self.__devices:
            device.destroy()
        self.__devices = []
        
        # Destroy menu
        if self.__menu:
            self.__menu.destroy()
        self.__menu = None
        
        # D-BUS proxy and interface
        self.__dbus_proxy_obj = None
        self.__dbus_iface = None
        
        self.__refresh_icon()
        
        return
    
    def __switch_bus (self):        
        # Close existing D-BUS connection
        if self.__dbus_bus:
            self.__signal_started.remove()
            self.__dbus_bus = None
        
        # Init D-BUS connection
        if self.__use_system_bus == True:
            print "Using system bus"
            self.__dbus_bus = dbus.SystemBus()
        else:
            print "Using session bus"
            self.__dbus_bus = dbus.SessionBus()
            
        # Set up signal handlers
        self.__signal_started = self.__dbus_bus.add_signal_receiver(self.__signal_daemon_started,
            'DaemonStarted',
            'net.sf.cdemu.CDEMUD_Daemon',
            None, # Don't request interface because we don't need it at this point
            '/CDEMUD_Daemon')
        
        return
    
    def __connect (self):        
        # Get D-BUS interface
        try:
            self.__dbus_proxy_obj = self.__dbus_bus.get_object('net.sf.cdemu.CDEMUD_Daemon', '/CDEMUD_Daemon')
            self.__dbus_iface = dbus.Interface(self.__dbus_proxy_obj, 'net.sf.cdemu.CDEMUD_Daemon')
        except dbus.DBusException, e:
            # Display dialog only for errors other than "Service not running"
            if e.get_dbus_name() != "org.freedesktop.DBus.Error.ServiceUnknown":
                self.__display_error(_("Failed to connect: %s") % e)
            else:
                print "Service not running"
            
            return False
        
        # Get daemon interface version
        try:
            interface_version = self.__dbus_iface.GetDaemonInterfaceVersion()
        except dbus.DBusException, e:
            self.__display_error(_("Failed to acquire daemon interface version (this most likely means your daemon is out-of-date): %s") % e)
            return False
        
        # Check daemon version; we should get away with direct string comparison
        # in Python...
        if (interface_version != config.daemon_interface_version):
            self.__display_error(_("CDEmu daemon interface version %i detected, but version %i is required!") % (interface_version, config.daemon_interface_version))
            return False
        
        # Signal receivers
        self.__dbus_iface.connect_to_signal("DaemonStopped", self.__signal_dbus_daemon_stopped)
                
        # Get number of devices
        try:
            num_devices = self.__dbus_iface.GetNumberOfDevices()
        except dbus.DBusException, e:
            self.__display_error(_("Failed to acquire number of devices: %s") % e)
            return False
        
        # Create menu
        self.__menu = gtk.Menu()
        
        for i in range(0, num_devices):
            # Create device
            self.__devices.append(gCDEmu_Device(i, self, self.__menu, self.__dbus_iface))
        
        # We're officially connected now
        self.__connected = True
        
        # Refresh icon
        self.__refresh_icon()
        
        return True

gobject.type_register(gCDEmu_Applet)
