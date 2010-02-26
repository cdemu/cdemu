/*
 *  libMirage: NULL fragment: Fragment object
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

#include "fragment-null.h"

#define __debug__ "NULL-Fragment"


/******************************************************************************\
 *                   MIRAGE_Fragment methods implementations                  *
\******************************************************************************/
static gboolean __mirage_fragment_null_can_handle_data_format (MIRAGE_Fragment *self, const gchar *filename, GError **error) {
    /* NULL doesn't need any data file checks; what's important is interface type,
       which is filtered out elsewhere */
    return TRUE;
}

static gboolean __mirage_fragment_null_use_the_rest_of_file (MIRAGE_Fragment *self, GError **error) {
    /* No file, nothing to use */
    return TRUE;
}

static gboolean __mirage_fragment_null_read_main_data (MIRAGE_Fragment *self, gint address, guint8 *buf, gint *length, GError **error) {
    /* Nothing to read */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: no data in NULL fragment\n", __debug__);
    if (length) {
        *length = 0;
    }    
    return TRUE;
}

static gboolean __mirage_fragment_null_read_subchannel_data (MIRAGE_Fragment *self, gint address, guint8 *buf, gint *length, GError **error) {
    /* Nothing to read */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: no data in NULL fragment\n", __debug__);
    if (length) {
        *length = 0;
    }
    return TRUE;
}


/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static MIRAGE_FragmentClass *parent_class = NULL;

static void __mirage_fragment_null_instance_init (GTypeInstance *instance, gpointer g_class) {   
    /* Create fragment info */
    mirage_fragment_generate_fragment_info(MIRAGE_FRAGMENT(instance),
        "FRAGMENT-NULL",
        "NULL Fragment"
    );
    
    return;
}

static void __mirage_fragment_null_class_init (gpointer g_class, gpointer g_class_data) {
    MIRAGE_FragmentClass *class_fragment = MIRAGE_FRAGMENT_CLASS(g_class);
    MIRAGE_Fragment_NULLClass *klass = MIRAGE_FRAGMENT_NULL_CLASS(g_class);

    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Initialize MIRAGE_Fragment methods */
    class_fragment->can_handle_data_format = __mirage_fragment_null_can_handle_data_format;
    class_fragment->use_the_rest_of_file = __mirage_fragment_null_use_the_rest_of_file;
    class_fragment->read_main_data = __mirage_fragment_null_read_main_data;
    class_fragment->read_subchannel_data = __mirage_fragment_null_read_subchannel_data;
        
    return;
}

GType mirage_fragment_null_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Fragment_NULLClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_fragment_null_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Fragment_NULL),
            0,      /* n_preallocs */
            __mirage_fragment_null_instance_init    /* instance_init */
        };
        
        static const GInterfaceInfo interface_info = {
            NULL,   /* interface_init */
            NULL,   /* interface_finalize */
            NULL    /* interface_data */
        };
                
        type = g_type_module_register_type(module, MIRAGE_TYPE_FRAGMENT, "MIRAGE_Fragment_NULL", &info, 0);

        g_type_module_add_interface(module, type, MIRAGE_TYPE_FINTERFACE_NULL, &interface_info);
    }
    
    return type;
}
