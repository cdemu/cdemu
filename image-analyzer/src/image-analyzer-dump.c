/*
 *  MIRAGE Image Analyzer: Generic dump functions
 *  Copyright (C) 2007-2010 Rok Mandeljc
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <gtk/gtk.h>
#include <mirage.h>
#include "image-analyzer-dump.h"


gchar *__dump_value (gint val, DUMP_Value *values, gint num_values) {
    gint i;
    
    for (i = 0; i < num_values; i++) {
        if (values[i].value == val) {
            return values[i].name;
        }
    }
    
    return "<Unknown>";
}

gchar *__dump_flags (gint val, DUMP_Value *values, gint num_values) {
    static gchar tmp_string[255] = "";
    gchar *ptr = tmp_string;
    gint i;

    memset(tmp_string, 0, sizeof(tmp_string));
        
    for (i = 0; i < num_values; i++) {
        if ((val & values[i].value) == values[i].value) {
            if (strlen(tmp_string)) {
                ptr += sprintf(ptr, "; ");
            }
            ptr += sprintf(ptr, "%s", values[i].name);
        }
    }
    
    return tmp_string;
}
