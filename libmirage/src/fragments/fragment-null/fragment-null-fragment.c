/*
 *  libMirage: NULL fragment: Fragment object
 *  Copyright (C) 2007-2012 Rok Mandeljc
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


/**********************************************************************\
 *               MIRAGE_Fragment methods implementations              *
\**********************************************************************/
static gboolean mirage_fragment_null_can_handle_data_format (MIRAGE_Fragment *_self G_GNUC_UNUSED, const gchar *filename G_GNUC_UNUSED, GError **error G_GNUC_UNUSED)
{
    /* NULL doesn't need any data file checks; what's important is interface type,
       which is filtered out elsewhere */
    return TRUE;
}

static gboolean mirage_fragment_null_use_the_rest_of_file (MIRAGE_Fragment *_self G_GNUC_UNUSED, GError **error G_GNUC_UNUSED)
{
    /* No file, nothing to use */
    return TRUE;
}

static gboolean mirage_fragment_null_read_main_data (MIRAGE_Fragment *_self, gint address G_GNUC_UNUSED, guint8 *buf G_GNUC_UNUSED, gint *length, GError **error G_GNUC_UNUSED)
{
    MIRAGE_Fragment_NULL *self = MIRAGE_FRAGMENT_NULL(_self);
    /* Nothing to read */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: no data in NULL fragment\n", __debug__);
    if (length) {
        *length = 0;
    }    
    return TRUE;
}

static gboolean mirage_fragment_null_read_subchannel_data (MIRAGE_Fragment *_self, gint address G_GNUC_UNUSED, guint8 *buf G_GNUC_UNUSED, gint *length, GError **error G_GNUC_UNUSED)
{
    MIRAGE_Fragment_NULL *self = MIRAGE_FRAGMENT_NULL(_self);
    /* Nothing to read */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: no data in NULL fragment\n", __debug__);
    if (length) {
        *length = 0;
    }
    return TRUE;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE_EXTENDED(MIRAGE_Fragment_NULL,
                               mirage_fragment_null,
                               MIRAGE_TYPE_FRAGMENT,
                               0,
                               G_IMPLEMENT_INTERFACE_DYNAMIC(MIRAGE_TYPE_FRAG_IFACE_NULL, NULL));

void mirage_fragment_null_type_register (GTypeModule *type_module)
{
    return mirage_fragment_null_register_type(type_module);
}


static void mirage_fragment_null_init (MIRAGE_Fragment_NULL *self)
{
    /*self->priv = MIRAGE_FRAGMENT_NULL_GET_PRIVATE(self);*/

    mirage_fragment_generate_fragment_info(MIRAGE_FRAGMENT(self),
        "FRAGMENT-NULL",
        "NULL Fragment"
    );
}

static void mirage_fragment_null_class_init (MIRAGE_Fragment_NULLClass *klass)
{
    MIRAGE_FragmentClass *fragment_class = MIRAGE_FRAGMENT_CLASS(klass);

    fragment_class->can_handle_data_format = mirage_fragment_null_can_handle_data_format;
    fragment_class->use_the_rest_of_file = mirage_fragment_null_use_the_rest_of_file;
    fragment_class->read_main_data = mirage_fragment_null_read_main_data;
    fragment_class->read_subchannel_data = mirage_fragment_null_read_subchannel_data;
}

static void mirage_fragment_null_class_finalize (MIRAGE_Fragment_NULLClass *klass G_GNUC_UNUSED)
{
}
