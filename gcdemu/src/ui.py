# -*- coding: utf-8 -*-
# gCDEmu: Auxiliary UI classes
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

import gtk
import os.path

from gettext import gettext as _

def helper_combine_images_list (images_list, separator):
    new_list = []
    for filename in images_list:
        new_list.append(os.path.basename(filename))
    
    return separator.join(new_list)

class gCDEmu_FileOpenDialog (gtk.FileChooserDialog):
    "File Open Dialog"
    
    # *************************************************************************
    # *                                 Init                                  *
    # *************************************************************************
    def __init__ (self):
        # Initialize gtk.FileChooserDialog
        gtk.FileChooserDialog.__init__(
            self,
            _("Open file"),
            None, 
            gtk.FILE_CHOOSER_ACTION_OPEN,
            (gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL, gtk.STOCK_OPEN, gtk.RESPONSE_ACCEPT),
            None
        )
        
        self.set_select_multiple(True)
    
    def add_filters (self, supported_images):
        # Add image file type filters to dialog
        all_files = gtk.FileFilter()
        all_files.set_name(_("All files"))
        all_files.add_pattern("*")
        self.add_filter(all_files)        
        
        all_images = gtk.FileFilter()
        all_images.set_name(_("All image files"))
        self.add_filter(all_images)        
        
        # Individual image type filters
        for image_type in supported_images:
            filter = gtk.FileFilter()
            filter.set_name(image_type[0])
            for pattern in image_type[1]:
                filter.add_pattern("*" + pattern);
                all_images.add_pattern("*" + pattern);
            self.add_filter(filter)
    
    def cleanup (self):
        # Remove all filters
        filters = self.list_filters()
        for filter in filters:
            self.remove_filter(filter)

            
class gCDEmu_DevicePropertiesDialog (gtk.Dialog):
    "Device Properties Dialog"
    
    # *************************************************************************
    # *                              Variables                                *
    # *************************************************************************
    __parent = None
    
    __debug_masks_l = []
    __debug_masks_d = []
    
    # *************************************************************************
    # *                                 Init                                  *
    # *************************************************************************
    def __init__ (self, parent, device_data = None):
        # Initialize gkt.Dialog
        gtk.Dialog.__init__(
            self,
            "Device Properties Dialog",
            None,
            gtk.DIALOG_DESTROY_WITH_PARENT,
            (gtk.STOCK_CLOSE, gtk.RESPONSE_CLOSE)
        )
    
        # Set parent
        self.__parent = parent
        
        # Debug masks... we need to convert tuple to list >.<
        (debug_masks_d, debug_masks_l) = self.__parent.get_debug_masks()
        self.__debug_masks_d = []
        for debug_mask in debug_masks_d:
            self.__debug_masks_d.append([debug_mask[0], debug_mask[1]])
        self.__debug_masks_l = []
        for debug_mask in debug_masks_l:
            self.__debug_masks_l.append([debug_mask[0], debug_mask[1]])
            
        # Build the rest of the dialog
        self.__init_ui()
        
        # If device data was given, set it
        if device_data:
            self.set_device_data(device_data)
    
    def __create_debug_mask_frame (self, frame_name, masks_list, type):
        # *** Frame: Debug mask ***
        frame = gtk.Frame(_("Debug mask: ") + frame_name)
        frame.show()
        frame.set_border_width(5)
        frame.set_label_align(0.50, 0.50)
        
        vbox = gtk.VBox()
        vbox.show()
        frame.add(vbox)
        vbox.set_border_width(5)
        vbox.set_spacing(2)
        
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
        button.connect("clicked", self.__set_debug_mask_button_callback, type)
        
        return frame
        
    def __init_ui (self):
        "Initializes UI."

        self.vbox.set_border_width(5)

        # *** Label: Device X ***
        self.__labelDevice = gtk.Label()
        self.__labelDevice.show()
        self.vbox.pack_start(self.__labelDevice)
        self.__labelDevice.set_use_markup(True)
        
        # *** Notebook ***
        notebook = gtk.Notebook()
        notebook.show()
        notebook.set_tab_pos(gtk.POS_LEFT)
        self.vbox.pack_start(notebook)
        notebook.set_border_width(5)

        # *** Frame: Status ***        
        frame = gtk.Frame(_("Status"))
        frame.set_label_align(0.50, 0.50)
        frame.show()
        notebook.append_page(frame, gtk.Label("Status"))
        frame.set_border_width(2)
        
        table = gtk.Table()
        table.show()
        frame.add(table)
        table.set_border_width(5)
        table.set_row_spacings(2)
        
        label = gtk.Label(_("Loaded: "))
        label.show()
        table.attach(label, 0, 1, 0, 1, xoptions=gtk.FILL|gtk.EXPAND, yoptions=0)
        
        self.__labelLoaded = gtk.Label()
        self.__labelLoaded.show()
        table.attach(self.__labelLoaded, 1, 2, 0, 1, xoptions=gtk.FILL|gtk.EXPAND, yoptions=0)
        
        label = gtk.Label(_("Image type: "))
        label.show()
        table.attach(label, 0, 1, 1, 2, xoptions=gtk.FILL|gtk.EXPAND, yoptions=0)
        
        self.__labelImageType = gtk.Label()
        self.__labelImageType.show()
        table.attach(self.__labelImageType, 1, 2, 1, 2, xoptions=gtk.FILL|gtk.EXPAND, yoptions=0)
        
        label = gtk.Label(_("File name: "))
        label.show()
        table.attach(label, 0, 1, 2, 3, xoptions=gtk.FILL|gtk.EXPAND, yoptions=0)
        
        self.__labelFileName = gtk.Label()
        self.__labelFileName.show()
        table.attach(self.__labelFileName, 1, 2, 2, 3, xoptions=gtk.FILL|gtk.EXPAND, yoptions=0)
        
        separator = gtk.HSeparator()
        separator.show()
        table.attach(separator, 0, 2, 3, 4, xoptions=gtk.FILL|gtk.EXPAND, yoptions=0)
        
        self.__buttonLoad = gtk.Button()
        self.__buttonLoad.show()
        table.attach(self.__buttonLoad, 0, 2, 4, 5, gtk.EXPAND, 0)
        self.__buttonLoad.connect("clicked", self.__cb_button_load_clicked)
        
        # Debug mask frames        
        frame = self.__create_debug_mask_frame(_("Daemon"), self.__debug_masks_d, "daemon")
        notebook.append_page(frame, gtk.Label("Daemon"))
        
        frame = self.__create_debug_mask_frame(_("Library"), self.__debug_masks_l, "library")
        notebook.append_page(frame, gtk.Label("Library"))
    
    # *************************************************************************    
    # *                             UI Callbacks                              *
    # *************************************************************************
    def __cb_button_load_clicked (self, button):
        self.__parent.device_load_unload(self.__device)
    
    def __set_debug_mask_button_callback (self, button, type):
        value = self.__get_debug_mask(type)
        self.__parent.device_set_debug_mask(self.__device, type, value)
        
    # *************************************************************************    
    # *                           Private functions                           *
    # *************************************************************************
    def __set_debug_mask (self, value, type):
        "Sets debug mask value on toggle buttons."
        
        if type == "daemon":
            mask_list = self.__debug_masks_d
        elif type == "library":
            mask_list = self.__debug_masks_l
        
        for debug_mask in mask_list:
            if value & debug_mask[1]:
                debug_mask[2].set_active(True)
            else:
                debug_mask[2].set_active(False)
    
    def __get_debug_mask (self, type):
        "Gets debug mask value from toggle buttons."
        
        if type == "daemon":
            mask_list = self.__debug_masks_d
        elif type == "library":
            mask_list = self.__debug_masks_l
        
        value = 0
        
        for debug_mask in mask_list:
            if debug_mask[2].get_active():
                value |= debug_mask[1]

        return value
    
    # *************************************************************************
    # *                           Public functions                            *
    # *************************************************************************
    def set_device_data (self, device):
        "Displays device data."
        
        self.__device = device[0]

        # Dialog title
        self.set_title(_("Device %i properties" % (device[0])))

        # Device X label
        self.__labelDevice.set_label("<b><big><u>" + _("Device %i" % (device[0])) + "</u></big></b>")
        
        # Set status
        if device[1]:
            self.__labelLoaded.set_label(_("Yes"))
            self.__labelFileName.set_label(helper_combine_images_list(device[3], "\n"))
            self.__labelImageType.set_label(device[2])
            self.__buttonLoad.set_label(_("Unload Device"))
        else:
            self.__labelLoaded.set_label(_("No"))
            self.__labelFileName.set_label(device[3][0])
            self.__labelImageType.set_label(device[2])
            self.__buttonLoad.set_label(_("Load Device"))
        
        # Set debug mask
        self.__set_debug_mask(device[4], "daemon")
        self.__set_debug_mask(device[5], "library")
                                

class gCDEmu_Menu (gtk.Menu):
    "Menu"
    
    # *************************************************************************
    # *                              Variables                                *
    # *************************************************************************
    __parent = None
    
    __device_entries = []
    
    # *************************************************************************
    # *                                 Init                                  *
    # *************************************************************************
    def __init__ (self, parent):
        gtk.Menu.__init__(self)
        
        self.__parent = parent
        self.__device_entries = []
        
        devices = self.__parent.get_number_of_devices()
        
        # Add devices
        for i in range (0, devices):
            item = gtk.MenuItem("")
            item.connect("button-press-event", self.__cb_device_entry_button_press_event, i)
            item.child.set_use_underline(False) # Don't use underline on menu labels
            item.show()
            
            self.__device_entries.append(item)
            self.append(item)
    
    # *************************************************************************    
    # *                             UI Callbacks                              *
    # *************************************************************************    
    def __cb_device_entry_button_press_event (self, widget, event, i):        
        if event.button == 1:
            # Button 1: Quick load/unload
            self.__parent.device_load_unload(i)
        elif event.button == 3:
            # Button 3: Device properties
            self.__parent.device_show_properties(i)
            
    # *************************************************************************
    # *                           Public functions                            *
    # *************************************************************************
    def set_device_data (self, data):
        "Displays device data."
        if data[1]:
            images = os.path.basename(data[3][0]) # Make it short
            if len(data[3]) > 1:
                images += ", ..." # Indicate there's more than one file
            str = "%s %02i: %s" % (_("Device"), data[0], images)
        else:
            str = "%s %02i: %s" % (_("Device"), data[0], _("Empty"))
        
        # Set label on MenuItem (label is its child...)
        self.__device_entries[data[0]].child.set_label(str)
