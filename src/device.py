# -*- coding: utf-8 -*-
# gCDEmu: Device class
# Copyright (C) 2008 Rok Mandeljc
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

import gtk
import os.path
import sys

from gettext import gettext as _

import dbus
from dbus.mainloop.glib import DBusGMainLoop
DBusGMainLoop(set_as_default=True)

def helper_combine_images_list (images_list, separator):
    new_list = []
    for filename in images_list:
        new_list.append(os.path.basename(filename))
    
    return separator.join(new_list)

def case_insensitive_ext_filter (filter_info, data):
    # Lowercase the filename
    filename = filter_info[0].lower()
    # Lowercase the extension
    ext = data.lower()
    
    # Check if lowercased filename ends with lowercased extension
    return filename.endswith(ext)
    
class gCDEmu_Device:
    def __setup_file_dialog (self):
        # Enumerate supported parsers and build up file selection dialog
        try:
            parsers = self.__dbus_iface.EnumSupportedParsers()
        except dbus.DBusException, e:
            print "Failed to enumerate supported parsers: %s" % e
        
        file_dialog = gtk.FileChooserDialog(
                        _("Open file"), 
                        None, 
                        gtk.FILE_CHOOSER_ACTION_OPEN, 
                        (gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL, gtk.STOCK_OPEN, gtk.RESPONSE_ACCEPT), 
                        None)
        
        file_dialog.set_select_multiple(True)
        
        filter = gtk.FileFilter()
        filter.set_name(_("All files"))
        filter.add_pattern("*")
        file_dialog.add_filter(filter)        
        
        all_images = gtk.FileFilter()
        all_images.set_name(_("All image files"))
        file_dialog.add_filter(all_images) 
        
        for parser in parsers:
            filter = gtk.FileFilter()
            filter.set_name(parser[5])
            for pattern in parser[6]:
                # Our custom filter function accepts pattern in form of .ext,
                # without * in front (contrary to add_pattern())
                filter.add_custom(gtk.FILE_FILTER_FILENAME, case_insensitive_ext_filter, pattern)
                all_images.add_custom(gtk.FILE_FILTER_FILENAME, case_insensitive_ext_filter, pattern)
            file_dialog.add_filter(filter)
        
        self.__file_dialog = file_dialog
        
        return
    
    def __setup_properties_dialog (self):
        dialog = gtk.Dialog(
            (_("Device %i properties") % (self.__number)),
            None,
            gtk.DIALOG_DESTROY_WITH_PARENT,
            (gtk.STOCK_CLOSE, gtk.RESPONSE_CLOSE)
        )
        
        dialog.vbox.set_border_width(5)
        
        label = gtk.Label("<b><big><u>" + (_("Device %i") % (self.__number)) + "</u></big></b>")
        label.show()
        label.set_use_markup(True)
        dialog.vbox.pack_start(label, expand=False)
        
        notebook = gtk.Notebook()
        notebook.show()
        notebook.set_tab_pos(gtk.POS_LEFT)
        notebook.set_border_width(5)
        dialog.vbox.pack_start(notebook, expand=True, fill=True)
        
        # Page: Status
        page = self.__create_page_status()
        notebook.append_page(page, gtk.Label(_("Status")))
        # Page: Options
        page = self.__create_page_options()
        notebook.append_page(page, gtk.Label(_("Options")))
        
        # Page: Daemon debug mask
        masks = self.__dbus_iface.EnumDaemonDebugMasks()
        self.__daemon_debug_masks = []
        for mask in masks:
            self.__daemon_debug_masks.append([ mask[0], mask[1] ])
        page = self.__create_page_debug_mask(_("Daemon debug mask"), self.__daemon_debug_masks, lambda b: self.__set_daemon_debug_mask())
        notebook.append_page(page, gtk.Label(_("Daemon")))
        
        # Page: Library debug mask
        masks = self.__dbus_iface.EnumLibraryDebugMasks()
        self.__library_debug_masks = []
        for mask in masks:
            self.__library_debug_masks.append([ mask[0], mask[1] ])
        page = self.__create_page_debug_mask(_("Library debug mask"), self.__library_debug_masks, lambda b: self.__set_library_debug_mask())
        notebook.append_page(page, gtk.Label(_("Library")))
        
        self.__properties_dialog = dialog
        
        # Connect now that everything's created
        notebook.connect("switch-page", self.__cb_notebook_switch_page)

        return
        
    def __create_page_status (self):
        vbox = gtk.VBox()
        vbox.show()
    
        # *** Frame: status ***
        frame = gtk.Frame(_("Status"))
        frame.set_label_align(0.50, 0.50)
        frame.show()
        frame.set_border_width(2)
        vbox.pack_start(frame, expand=False, fill=False)

        table = gtk.Table()
        table.show()
        frame.add(table)
        table.set_border_width(5)
        table.set_row_spacings(2)
        
        label = gtk.Label(_("Loaded: "))
        label.show()
        table.attach(label, 0, 1, 0, 1, xoptions=gtk.FILL|gtk.EXPAND, yoptions=0)
        
        label = gtk.Label()
        label.show()
        table.attach(label, 1, 2, 0, 1, xoptions=gtk.FILL|gtk.EXPAND, yoptions=0)
        self.__label_loaded = label 
                
        label = gtk.Label(_("File name: "))
        label.show()
        table.attach(label, 0, 1, 1, 2, xoptions=gtk.FILL|gtk.EXPAND, yoptions=0)
        
        label = gtk.Label()
        label.show()
        table.attach(label, 1, 2, 1, 2, xoptions=gtk.FILL|gtk.EXPAND, yoptions=0)
        self.__label_filename = label
        
        separator = gtk.HSeparator()
        separator.show()
        table.attach(separator, 0, 2, 2, 3, xoptions=gtk.FILL|gtk.EXPAND, yoptions=0)
        
        button = gtk.Button()
        button.show()
        table.attach(button, 0, 2, 3, 4, xoptions=gtk.EXPAND, yoptions=0)
        button.connect("clicked", lambda b: self.__device_load_unload())
        self.__button_load = button
        
        # *** Frame: mapping ***
        [dev_sr, dev_sg] = self.__dbus_iface.DeviceGetMapping(self.__number)
                
        frame = gtk.Frame(_("Device mapping"))
        frame.set_label_align(0.50, 0.50)
        frame.show()
        frame.set_border_width(2)
        vbox.pack_start(frame, expand=False, fill=False)
        
        table = gtk.Table()
        table.show()
        frame.add(table)
        table.set_border_width(5)
        table.set_row_spacings(2)
        
        label = gtk.Label(_("SCSI CD-ROM device: "))
        label.show()
        table.attach(label, 0, 1, 0, 1, xoptions=gtk.FILL|gtk.EXPAND, yoptions=0)
        
        label = gtk.Label(dev_sr)
        label.show()
        table.attach(label, 1, 2, 0, 1, xoptions=gtk.FILL|gtk.EXPAND, yoptions=0)
        self.__label_dev_sr = label
        
        label = gtk.Label(_("SCSI generic device: "))
        label.show()
        table.attach(label, 0, 1, 1, 2, xoptions=gtk.FILL|gtk.EXPAND, yoptions=0)
        
        label = gtk.Label(dev_sg)
        label.show()
        table.attach(label, 1, 2, 1, 2, xoptions=gtk.FILL|gtk.EXPAND, yoptions=0)
        self.__label_dev_sg = label
        
        return vbox
    
    def __create_page_options (self):
        vbox = gtk.VBox()
        vbox.show()
        
        # Device ID
        frame = gtk.Frame(_("Device ID"))
        frame.set_label_align(0.50, 0.50)
        frame.show()
        frame.set_border_width(2)
        vbox.pack_start(frame, expand=False, fill=False)
        
        table = gtk.Table()
        table.show()
        frame.add(table)
        table.set_border_width(5)
        table.set_row_spacings(2)
        
        label = gtk.Label(_("Vendor ID: "))
        label.show()
        table.attach(label, 0, 1, 0, 1, xoptions=0, yoptions=0)
        
        entry = gtk.Entry()
        entry.show()
        entry.set_max_length(8)
        table.attach(entry, 1, 2, 0, 1, xoptions=gtk.FILL|gtk.EXPAND, yoptions=0)          
        self.__entry_vendor_id = entry
        
        label = gtk.Label(_("Product ID: "))
        label.show()
        table.attach(label, 0, 1, 1, 2, xoptions=gtk.FILL|gtk.EXPAND, yoptions=0)
        
        entry = gtk.Entry()
        entry.show()
        entry.set_max_length(16)
        table.attach(entry, 1, 2, 1, 2, xoptions=gtk.FILL|gtk.EXPAND, yoptions=0)          
        self.__entry_product_id = entry
        
        label = gtk.Label(_("Revision: "))
        label.show()
        table.attach(label, 0, 1, 2, 3, xoptions=gtk.FILL|gtk.EXPAND, yoptions=0)
        
        entry = gtk.Entry()
        entry.show()
        entry.set_max_length(4)
        table.attach(entry, 1, 2, 2, 3, xoptions=gtk.FILL|gtk.EXPAND, yoptions=0)          
        self.__entry_revision = entry
        
        label = gtk.Label(_("Vendor-specific: "))
        label.show()
        table.attach(label, 0, 1, 3, 4, xoptions=gtk.FILL|gtk.EXPAND, yoptions=0)
        
        entry = gtk.Entry()
        entry.show()
        entry.set_max_length(20)
        table.attach(entry, 1, 2, 3, 4, xoptions=gtk.FILL|gtk.EXPAND, yoptions=0)          
        self.__entry_vendor_specific = entry
        
        separator = gtk.HSeparator()
        separator.show()
        table.attach(separator, 0, 2, 4, 5, xoptions=gtk.FILL|gtk.EXPAND, yoptions=0)
        
        button = gtk.Button(_("Set device ID"))
        button.show()
        table.attach(button, 0, 2, 5, 6, gtk.EXPAND, 0)
        button.connect("clicked", lambda b: self.__set_device_id())
        
        # DPM emulation
        checkbutton = gtk.CheckButton(_("DPM emulation"), False)
        checkbutton.show()
        checkbutton.connect("toggled", lambda c: self.__set_dpm_emulation())
        vbox.pack_start(checkbutton, expand=False, fill=False)
        self.__checkbutton_dpm = checkbutton
        
        # Transfer rate emulation
        checkbutton = gtk.CheckButton(_("Transfer rate emulation"), False)
        checkbutton.show()
        checkbutton.connect("toggled", lambda c: self.__set_tr_emulation())
        vbox.pack_start(checkbutton, expand=False, fill=False)
        self.__checkbutton_tr = checkbutton
        
        return vbox
    
    def __create_page_debug_mask (self, frame_name, masks_list, clicked_callback):
        # *** Frame: Debug mask ***
        frame = gtk.Frame(frame_name)
        frame.show()
        frame.set_border_width(5)
        frame.set_label_align(0.50, 0.50)
        
        vbox = gtk.VBox()
        vbox.show()
        vbox.set_border_width(5)
        vbox.set_spacing(2)
        frame.add(vbox)
                
        for debug_mask in masks_list:
            checkbutton = gtk.CheckButton(debug_mask[0], False)
            checkbutton.show()
            vbox.pack_start(checkbutton, expand=False, fill=False)
            
            debug_mask.append(checkbutton)
        
        separator = gtk.HSeparator()
        separator.show()
        vbox.pack_start(separator, expand=False, fill=False)
        
        button = gtk.Button(_("Set debug mask"))
        button.show()
        vbox.pack_start(button, expand=False, fill=False)
        button.connect("clicked", clicked_callback)
        
        return frame
    
    def __init__ (self, number, parent, menu, dbus_iface):        
        # Initialize all variables
        self.__dbus_iface = None
        self.__number = 0
        self.__parent = None
    
        self.__file_dialog = None
        self.__properties_dialog = None
    
        self.__menu_item = None
    
        self.__loaded = False
        self.__filenames = []
    
        self.__label_loaded = None
        self.__label_filename = None
        self.__button_load = None

        self.__daemon_debug = 0
        self.__library_debug = 0
        
        self.__device_id = [ "", "", "", ""]

        self.__dpm_emulation = False
        self.__tr_emulation = False
            
        self.__entry_vendor_id = None
        self.__entry_product_id = None
        self.__entry_revision = None
        self.__entry_vendor_specific = None
        self.__checkbutton_dpm = None
        self.__checkbutton_tr = None
        
        # Set variables
        self.__number = number
        self.__dbus_iface = dbus_iface
        self.__parent = parent
                
        # Create menu entry
        item = gtk.MenuItem("Device %i" % (number))
        item.connect("button-press-event", self.__cb_menu_item_button_press_event)
        item.child.set_use_underline(False) # Don't use underline on menu labels
        item.show()
        
        self.__menu_item = item
        
        menu.append(item)
        
        # File dialog
        self.__setup_file_dialog()
        
        # Properties dialog
        self.__setup_properties_dialog()
        
        # Setup signal receivers 
        self.__signals = []
        self.__signals.append(self.__dbus_iface.connect_to_signal("DeviceStatusChanged", self.__signal_status_changed))
        self.__signals.append(self.__dbus_iface.connect_to_signal("DeviceOptionChanged", self.__signal_option_changed))
        
        # Initial update
        self.__update_status(True)
        self.__update_device_id(True)
        self.__update_dpm_emulation(True)
        self.__update_tr_emulation(True)
        self.__update_daemon_debug_mask(True)
        self.__update_library_debug_mask(True)
        
        return
    
    
    def destroy (self):        
        # Remove references
        self.__parent = None
        self.__dbus_iface = None
        
        self.__label_loaded = None
        self.__label_filename = None
        self.__button_load = None
        
        self.__entry_vendor_id = None
        self.__entry_product_id = None
        self.__entry_revision = None
        self.__entry_vendor_specific = None
        self.__checkbutton_dpm = None
        self.__checkbutton_tr = None
        
        # Destroy widgets
        self.__properties_dialog.destroy()
        self.__file_dialog.destroy()
        self.__menu_item.destroy()
        
        # Disconnect signals
        for signal in self.__signals:
            signal.remove()
        self.__signals = []
        
        return
        
    def __signal_status_changed (self, device):
        # Skip signals not meant for us
        if device != self.__number:
            return
        
        # Update status
        self.__update_status(True)
        
        # Notify
        if self.__loaded:
            self.__parent.notification(_("Device change"), _("Device %i has been loaded.") % (self.__number))
        else:
            self.__parent.notification(_("Device change"), _("Device %i has been emptied.") % (self.__number))
        
        return
        
    def __signal_option_changed (self, device, option):
        # Skip signals not meant for us
        if device != self.__number:
            return
        
        # Option switch
        if option == "device-id":
            self.__update_device_id(True)
        elif option == "dpm-emulation":
            self.__update_dpm_emulation(True)
        elif option == "tr-emulation":
            self.__update_tr_emulation(True)
        elif option == "daemon-debug-mask":
            self.__update_daemon_debug_mask(True)
        elif option == "library-debug-mask":
            self.__update_library_debug_mask(True)
        else:
            print "Unhandled option: '%s'" % (option)
        
        self.__parent.notification(_("Device change"), _("Device %i has been changed its option:\n%s.") % (self.__number, option))
        
        return
    
    
    def __device_show_properties (self):
        self.__properties_dialog.run()
        self.__properties_dialog.hide()
    
    
    def __display_error (self, text):
         # Show error dialog
        message = gtk.MessageDialog(None, 0, gtk.MESSAGE_ERROR, gtk.BUTTONS_CLOSE, text)
        message.set_title(_("Error"))
        message.run()
        message.destroy()
        
        return

    
    def __get_debug_mask_value (self, masks_list):
        value = 0
        
        for debug_mask in masks_list:
            if debug_mask[2].get_active():
                value |= debug_mask[1]

        return value    
    
    def __set_debug_mask_value (self, masks_list, value):
        for debug_mask in masks_list:
            if value & debug_mask[1]:
                debug_mask[2].set_active(True)
            else:
                debug_mask[2].set_active(False)
        
        return
    
    
    def __cb_menu_item_button_press_event (self, widget, event):        
        if event.button == 1:
            # Button 1: Quick load/unload
            self.__device_load_unload()
        elif event.button == 3:
            # Button 3: Device properties
            self.__device_show_properties()
            # Reset certain tabs
            self.__update_device_id(False)
            self.__update_daemon_debug_mask(False)
            self.__update_library_debug_mask(False)
    
    def __cb_notebook_switch_page (self, notebook, page, page_num):
        # We clear device ID and debug mask tabs, because they have fields whose 
        # change doesn't trigger immediate setting change and therefore need to 
        # be reset on page switch
        self.__update_device_id(False)
        self.__update_daemon_debug_mask(False)
        self.__update_library_debug_mask(False)
    
    def __device_load_unload (self):
        if self.__loaded:
            # Unload
            try:
                self.__dbus_iface.DeviceUnload(self.__number)
            except dbus.DBusException, e:
                # Show error dialog
                self.__display_error(_("Failed to unload device %i:\n%s") % (self.__number, e))
                # Check the connection
                self.__parent.check_connection()
        else:
            # Run dialog, get filename and load the image
            if self.__file_dialog.run() == gtk.RESPONSE_ACCEPT:
                # Hide dialog while we do the loading...
                self.__file_dialog.hide()
                # Get filename
                filenames = self.__file_dialog.get_filenames()
                # Try loading
                try:
                    self.__dbus_iface.DeviceLoad(self.__number, filenames)
                except dbus.DBusException, e:
                    self.__display_error(_("Failed to load image %s to device %i:\n%s") % (";".join(filenames), self.__number, e))
                    # Check the connection
                    self.__parent.check_connection()
            else:
                self.__file_dialog.hide()
            
        return
    
    def __set_device_id (self):
        vendor_id = self.__entry_vendor_id.get_text()
        product_id = self.__entry_product_id.get_text()
        revision = self.__entry_revision.get_text()
        vendor_specific = self.__entry_vendor_specific.get_text()
        
        self.__device_id = [ vendor_id, product_id, revision, vendor_specific ]
        
        try:
            self.__dbus_iface.DeviceSetOption(self.__number, "device-id", self.__device_id)
        except dbus.DBusException, e:
            self.__display_error(_("Failed to set device ID for device %i to %s:\n%s") % (self.__number, self.__device_id, e))
            self.__parent.check_connection()
        
        return
        
    def __set_dpm_emulation (self):
        enabled = self.__checkbutton_dpm.get_active()
        
        # Filter out calls made by __update_dpm_emulation()
        if enabled == self.__dpm_emulation:
            return
        
        self.__dpm_emulation = enabled
        enabled = enabled and True or False # Because D-BUS API strictly requires boolean
        
        try:
            self.__dbus_iface.DeviceSetOption(self.__number, "dpm-emulation", [ enabled ])
        except dbus.DBusException, e:
            self.__display_error(_("Failed to set DPM emulation for device %i to %i:\n%s") % (self.__number, enabled, e))
            self.__parent.check_connection()
            
        return
        
    def __set_tr_emulation (self):
        enabled = self.__checkbutton_tr.get_active()
        
        # Filter out calls made by __update_tr_emulation()
        if enabled == self.__tr_emulation:
            return
        
        self.__tr_emulation = enabled
        enabled = enabled and True or False # Because D-BUS API strictly requires boolean
        
        try:
            self.__dbus_iface.DeviceSetOption(self.__number, "tr-emulation", [ enabled ])
        except dbus.DBusException, e:
            self.__display_error(_("Failed to set transfer rate emulation for device %i to %i:\n%s") % (self.__number, enabled, e))
            self.__parent.check_connection()
            
        return
    
    def __set_daemon_debug_mask (self):
        self.__daemon_debug = self.__get_debug_mask_value(self.__daemon_debug_masks)
        try:
            self.__dbus_iface.DeviceSetOption(self.__number, "daemon-debug-mask", [ self.__daemon_debug ])
        except dbus.DBusException, e:
            self.__display_error(_("Failed to set daemon debug mask for device %i to 0x%X:\n%s") % (self.__number, value, e))
            self.__parent.check_connection()
            
        return
        
    def __set_library_debug_mask (self):
        self.__library_debug = self.__get_debug_mask_value(self.__library_debug_masks)
        try:
            self.__dbus_iface.DeviceSetOption(self.__number, "library-debug-mask", [ self.__library_debug ])
        except dbus.DBusException, e:
            self.__display_error(_("Failed to set library debug mask for device %i to 0x%X:\n%s") % (self.__number, value, e))
            self.__parent.check_connection()
            
        return
    
    def __update_status (self, refresh):
        # Get status
        if refresh:
            try:
                [ self.__loaded, self.__filenames ] = self.__dbus_iface.DeviceGetStatus(self.__number)
            except dbus.DBusException, e:
                print "Failed to acquire device status: %s" % e
                self.__parent.check_connection()
            
        # Set label on menu item (label is its child...)
        if self.__loaded:
            images = os.path.basename(self.__filenames[0]) # Make it short
            if len(self.__filenames) > 1:
                images += ", ..." # Indicate there's more than one file
            str = "%s %02i: %s" % (_("Device"), self.__number, images)
        else:
            str = "%s %02i: %s" % (_("Device"), self.__number, _("Empty"))
        
        self.__menu_item.child.set_label(str)
        
        # Set appropriate fields in Device properties dialog
        if self.__loaded:
            self.__label_loaded.set_label(_("Yes"))
            self.__label_filename.set_label(helper_combine_images_list(self.__filenames, "\n"))
            self.__button_load.set_label(_("Unload"))
        else:
            self.__label_loaded.set_label(_("No"))
            self.__label_filename.set_label("")
            self.__button_load.set_label(_("Load"))
        
        return
    
    def __update_dpm_emulation (self, refresh):
        # Get DPM emulation flag
        if refresh:
            try:
                [ self.__dpm_emulation ] = self.__dbus_iface.DeviceGetOption(self.__number, "dpm-emulation")
            except dbus.DBusException, e:
                print "Failed to acquire DPM emulation flag: %s" % e
                self.__parent.check_connection()
        
        # Set the checkbutton
        self.__checkbutton_dpm.set_active(self.__dpm_emulation)
        
        return
        
    def __update_tr_emulation (self, refresh):
        # Get TR emulation flag
        if refresh:
            try:
                [ self.__tr_emulation ] = self.__dbus_iface.DeviceGetOption(self.__number, "tr-emulation")
            except dbus.DBusException, e:
                print "Failed to acquire TR emulation flag: %s" % e
                self.__parent.check_connection()
        
        # Set the checkbutton
        self.__checkbutton_tr.set_active(self.__tr_emulation)
        
        return
    
    def __update_device_id (self, refresh):
        # Get device ID
        if refresh:
            try:
                self.__device_id = self.__dbus_iface.DeviceGetOption(self.__number, "device-id")
            except dbus.DBusException, e:
                print "Failed to acquire device ID: %s" % e
                self.__parent.check_connection()
        
        # Set textboxes
        self.__entry_vendor_id.set_text(self.__device_id[0])
        self.__entry_product_id.set_text(self.__device_id[1])
        self.__entry_revision.set_text(self.__device_id[2])
        self.__entry_vendor_specific.set_text(str(self.__device_id[3]))
                
        return
    
    def __update_daemon_debug_mask (self, refresh):
        # Get daemon debug mask
        if refresh:
            try:
                [ self.__daemon_debug ] = self.__dbus_iface.DeviceGetOption(self.__number, "daemon-debug-mask")
            except dbus.DBusException, e:
                print "Failed to acquire daemon debug mask: %s" % e
                self.__parent.check_connection()
        
        self.__set_debug_mask_value(self.__daemon_debug_masks, self.__daemon_debug)
        
        return

    def __update_library_debug_mask (self, refresh):
        # Get library debug mask
        if refresh:
            try:
                [ self.__library_debug ] = self.__dbus_iface.DeviceGetOption(self.__number, "library-debug-mask")
            except dbus.DBusException, e:
                print "Failed to acquire library debug mask: %s" % e
                self.__parent.check_connection()
            
        self.__set_debug_mask_value(self.__library_debug_masks, self.__library_debug)
        
        return

