/*
 *  libMirage: ISO image: writer
 *  Copyright (C) 2014-2026 Rok Mandeljc
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

#pragma once

G_BEGIN_DECLS


#define MIRAGE_TYPE_WRITER_ISO            (mirage_writer_iso_get_type())
#define MIRAGE_WRITER_ISO(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_WRITER_ISO, MirageWriterIso))
#define MIRAGE_WRITER_ISO_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_WRITER_ISO, MirageWriterIsoClass))
#define MIRAGE_IS_WRITER_ISO(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_WRITER_ISO))
#define MIRAGE_IS_WRITER_ISO_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_WRITER_ISO))
#define MIRAGE_WRITER_ISO_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_WRITER_ISO, MirageWriterIsoClass))

typedef struct _MirageWriterIso           MirageWriterIso;
typedef struct _MirageWriterIsoClass      MirageWriterIsoClass;
typedef struct _MirageWriterIsoPrivate    MirageWriterIsoPrivate;

struct _MirageWriterIso
{
    MirageWriter parent_instance;

    /*< private >*/
    MirageWriterIsoPrivate *priv;
};

struct _MirageWriterIsoClass
{
    MirageWriterClass parent_class;
};

/* Used by MIRAGE_TYPE_WRITER_ISO */
GType mirage_writer_iso_get_type (void);
void mirage_writer_iso_type_register (GTypeModule *type_module);


G_END_DECLS
