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

#ifndef __MIRAGE_WRITER_H__
#define __MIRAGE_WRITER_H__

#include "mirage-types.h"


G_BEGIN_DECLS


typedef enum _MirageFragmentRole
{
    MIRAGE_FRAGMENT_PREGAP,
    MIRAGE_FRAGMENT_DATA,
} MirageFragmentRole;


/**********************************************************************\
 *                         MirageWriter object                        *
\**********************************************************************/
#define MIRAGE_TYPE_WRITER            (mirage_writer_get_type())
#define MIRAGE_WRITER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_WRITER, MirageWriter))
#define MIRAGE_WRITER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_WRITER, MirageWriterClass))
#define MIRAGE_IS_WRITER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_WRITER))
#define MIRAGE_IS_WRITER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_WRITER))
#define MIRAGE_WRITER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_WRITER, MirageWriterClass))

typedef struct _MirageWriter           MirageWriter;
typedef struct _MirageWriterClass      MirageWriterClass;
typedef struct _MirageWriterPrivate    MirageWriterPrivate;

/**
 * MirageWriter:
 *
 * All the fields in the <structname>MirageWriter</structname>
 * structure are private to the #MirageWriter implementation and
 * should never be accessed directly.
 */
struct _MirageWriter
{
    MirageObject parent_instance;

    /*< private >*/
    MirageWriterPrivate *priv;
} ;

/**
 * MirageWriterClass:
 * @parent_class: the parent class
 * @load_image: loads image
 *
 * The class structure for the <structname>MirageWriter</structname> type.
 */
struct _MirageWriterClass
{
    MirageObjectClass parent_class;

    /* Class members */
    gboolean (*open_image) (MirageWriter *self, MirageDisc *disc, GError **error);
    MirageFragment *(*create_fragment) (MirageWriter *self, MirageTrack *track, MirageFragmentRole role, GError **error);
    gboolean (*finalize_image) (MirageWriter *self);
};

/* Used by MIRAGE_TYPE_WRITER */
GType mirage_writer_get_type (void);

gboolean mirage_writer_open_image (MirageWriter *self, MirageDisc *disc, GError **error);
MirageFragment *mirage_writer_create_fragment (MirageWriter *self, MirageTrack *track, MirageFragmentRole role, GError **error);
gboolean mirage_writer_finalize_image (MirageWriter *self);

MirageDisc *mirage_writer_get_disc (MirageWriter *self);

G_END_DECLS

#endif /* __MIRAGE_WRITER_H__ */
