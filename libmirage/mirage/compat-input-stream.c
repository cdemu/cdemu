/*
 *  libMirage: compatibility input stream
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

#include "mirage/config.h"
#include "mirage/compat-input-stream.h"
#include "mirage/stream.h"

#include <glib/gi18n-lib.h>


/**********************************************************************\
 *                  Object and its private structure                  *
\**********************************************************************/
struct _MirageCompatInputStreamPrivate
{
    MirageStream *stream;
};


G_DEFINE_TYPE_WITH_PRIVATE(MirageCompatInputStream, mirage_compat_input_stream, G_TYPE_INPUT_STREAM)


/**********************************************************************\
 *                        GInputStream methods                        *
\**********************************************************************/
static gssize mirage_compat_input_stream_read (GInputStream *_self, void *buffer, gsize count, GCancellable *cancellable G_GNUC_UNUSED, GError **error)
{
    MirageCompatInputStream *self = MIRAGE_COMPAT_INPUT_STREAM(_self);
    return mirage_stream_read(self->priv->stream, buffer, count, error);
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
enum {
    PROP_0,

    PROP_STREAM,

    N_PROPERTIES
};

static GParamSpec *mirage_compat_input_stream_properties[N_PROPERTIES] = {
    NULL,
};

static void mirage_compat_input_stream_init (MirageCompatInputStream *self)
{
    self->priv = mirage_compat_input_stream_get_instance_private(self);

    /* Make sure all fields are empty */
    self->priv->stream = NULL;
}

static void mirage_compat_input_stream_dispose (GObject *gobject)
{
    MirageCompatInputStream *self = MIRAGE_COMPAT_INPUT_STREAM(gobject);

    /* Unref stream */
    if (self->priv->stream) {
        g_object_unref(self->priv->stream);
        self->priv->stream = NULL;
    }

    /* Chain up to the parent class */
    G_OBJECT_CLASS(mirage_compat_input_stream_parent_class)->dispose(gobject);
}

static void mirage_compat_input_stream_set_property (GObject *gobject, guint property_id, const GValue *value, GParamSpec *pspec)
{
    MirageCompatInputStream *self = MIRAGE_COMPAT_INPUT_STREAM(gobject);

    switch (property_id) {
        case PROP_STREAM: {
            self->priv->stream = g_object_ref(g_value_get_pointer(value));
            return;
        }
    }

    /* Chain up to the parent class */
    G_OBJECT_CLASS(mirage_compat_input_stream_parent_class)->set_property(gobject, property_id, value, pspec);
}


static void mirage_compat_input_stream_class_init (MirageCompatInputStreamClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GInputStreamClass *ginputstream_class = G_INPUT_STREAM_CLASS(klass);

    gobject_class->dispose = mirage_compat_input_stream_dispose;
    gobject_class->set_property = mirage_compat_input_stream_set_property;

    ginputstream_class->read_fn = mirage_compat_input_stream_read;

    /* Register properties */
    mirage_compat_input_stream_properties[PROP_STREAM] = g_param_spec_pointer(
        "stream",
        "mirage-stream",
        "Base MirageStream object.",
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE);

    g_object_class_install_properties(gobject_class, N_PROPERTIES, mirage_compat_input_stream_properties);
}
