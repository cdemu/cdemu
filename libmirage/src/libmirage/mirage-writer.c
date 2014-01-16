/*
 *  libMirage: Writer object
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"

#define __debug__ "Writer"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_WRITER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_WRITER, MirageWriterPrivate))

struct _MirageWriterPrivate
{
};


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
MirageDisc *mirage_writer_open_image (MirageWriter *self, const gchar *filename, GError **error)
{
    return MIRAGE_WRITER_GET_CLASS(self)->open_image(self, filename, error);
}

MirageFragment *mirage_writer_create_fragment (MirageWriter *self, gint session, gint track, MirageFragmentRole role, GError **error)
{
    return MIRAGE_WRITER_GET_CLASS(self)->create_fragment(self, session, track, role, error);
}

gboolean mirage_writer_finalize_image (MirageWriter *self)
{
    return MIRAGE_WRITER_GET_CLASS(self)->finalize_image(self);
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_ABSTRACT_TYPE(MirageWriter, mirage_writer, MIRAGE_TYPE_OBJECT);


static void mirage_writer_init (MirageWriter *self)
{
    self->priv = MIRAGE_WRITER_GET_PRIVATE(self);
}

static void mirage_writer_finalize (GObject *gobject)
{
    /*MirageWriter *self = MIRAGE_WRITER(gobject);*/

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_writer_parent_class)->finalize(gobject);
}

static void mirage_writer_class_init (MirageWriterClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = mirage_writer_finalize;

    klass->open_image = NULL;
    klass->create_fragment = NULL;
    klass->finalize_image = NULL;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageWriterPrivate));
}
