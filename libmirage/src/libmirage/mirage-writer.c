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
    MirageWriterInfo info;
};


/**********************************************************************\
 *                           Writer info API                          *
\**********************************************************************/
static void mirage_writer_info_generate (MirageWriterInfo *info, const gchar *id, const gchar *name, const gchar *parameter_sheet)
{
    /* Free old fields */
    mirage_writer_info_free(info);

    /* Copy ID and name */
    info->id = g_strdup(id);
    info->name = g_strdup(name);

    /* Copy parameter sheet */
    info->parameter_sheet = g_strdup(parameter_sheet);
}

/**
 * mirage_writer_info_copy:
 * @info: (in): a #MirageWriterInfo to copy data from
 * @dest: (in): a #MirageWriterInfo to copy data to
 *
 * Copies parser information from @info to @dest.
 */
void mirage_writer_info_copy (const MirageWriterInfo *info, MirageWriterInfo *dest)
{
    dest->id = g_strdup(info->id);
    dest->name = g_strdup(info->name);
    dest->parameter_sheet = g_strdup(info->parameter_sheet);
}

/**
 * mirage_writer_info_free:
 * @info: (in): a #MirageWriterInfo to free
 *
 * Frees the allocated fields in @info (but not the structure itself!).
 */
void mirage_writer_info_free (MirageWriterInfo *info)
{
    g_free(info->id);
    g_free(info->name);
    g_free(info->parameter_sheet);
}


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
/**
 * mirage_writer_generate_info:
 * @self: a #MirageWriter
 * @id: (in): writer ID
 * @name: (in): writer name
 * @parameter_sheet: (in): XML parameter sheet, describing writer's parameters
 *
 * Generates writer information from the input fields. It is intended as a function
 * for creating writer information in writer implementations.
 */
void mirage_writer_generate_info (MirageWriter *self, const gchar *id, const gchar *name, const gchar *parameter_sheet)
{
    mirage_writer_info_generate(&self->priv->info, id, name, parameter_sheet);
}

/**
 * mirage_writer_get_info:
 * @self: a #MirageWriter
 *
 * Retrieves writer information.
 *
 * Returns: (transfer none): a pointer to writer information structure.  The
 * structure belongs to object and should not be modified.
 */
const MirageWriterInfo *mirage_writer_get_info (MirageWriter *self)
{
    return &self->priv->info;
}


gboolean mirage_writer_open_image (MirageWriter *self, MirageDisc *disc, GHashTable *options, GError **error)
{
    return MIRAGE_WRITER_GET_CLASS(self)->open_image(self, disc, options, error);
}

MirageFragment *mirage_writer_create_fragment (MirageWriter *self, MirageTrack *track, MirageFragmentRole role, GError **error)
{
    return MIRAGE_WRITER_GET_CLASS(self)->create_fragment(self, track, role, error);
}

gboolean mirage_writer_finalize_image (MirageWriter *self, MirageDisc *disc, GError **error)
{
    return MIRAGE_WRITER_GET_CLASS(self)->finalize_image(self, disc, error);
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_ABSTRACT_TYPE(MirageWriter, mirage_writer, MIRAGE_TYPE_OBJECT);


static void mirage_writer_init (MirageWriter *self)
{
    self->priv = MIRAGE_WRITER_GET_PRIVATE(self);

    /* Make sure all fields are empty */
    memset(&self->priv->info, 0, sizeof(self->priv->info));
}

static void mirage_writer_finalize (GObject *gobject)
{
    MirageWriter *self = MIRAGE_WRITER(gobject);

    /* Free info structure */
    mirage_writer_info_free(&self->priv->info);

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
