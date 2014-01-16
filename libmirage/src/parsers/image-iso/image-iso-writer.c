/*
 *  libMirage: ISO image writer: Writer object
 *  Copyright (C) 2014 Rok Mandeljc
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "image-iso.h"

#define __debug__ "ISO-Writer"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_WRITER_ISO_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_WRITER_ISO, MirageWriterIsoPrivate))

struct _MirageWriterIsoPrivate
{
};


/**********************************************************************\
 *                MirageWriter methods implementation                 *
\**********************************************************************/
static MirageDisc *mirage_writer_iso_open_image (MirageWriter *self, const gchar *filename, GError **error)
{
    MirageDisc *disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    return NULL;
}

static MirageFragment *mirage_writer_iso_create_fragment (MirageWriter *self, gint session, gint track, MirageFragmentRole role, GError **error)
{
    MirageFragment *fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL);
    return fragment;
}

static gboolean mirage_writer_iso_finalize_image (MirageWriter *self)
{
    return TRUE;
}



/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE(MirageWriterIso, mirage_writer_iso, MIRAGE_TYPE_WRITER);

void mirage_writer_iso_type_register (GTypeModule *type_module)
{
    return mirage_writer_iso_register_type(type_module);
}


static void mirage_writer_iso_init (MirageWriterIso *self)
{
    self->priv = MIRAGE_WRITER_ISO_GET_PRIVATE(self);
}

static void mirage_writer_iso_class_init (MirageWriterIsoClass *klass)
{
    MirageWriterClass *writer_class = MIRAGE_WRITER_CLASS(klass);

    writer_class->open_image = mirage_writer_iso_open_image;
    writer_class->create_fragment = mirage_writer_iso_create_fragment;
    writer_class->finalize_image = mirage_writer_iso_finalize_image;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageWriterIsoPrivate));
}

static void mirage_writer_iso_class_finalize (MirageWriterIsoClass *klass G_GNUC_UNUSED)
{
}
