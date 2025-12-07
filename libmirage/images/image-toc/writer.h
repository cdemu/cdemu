/*
 *  libMirage: TOC image: writer
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

#pragma once

G_BEGIN_DECLS


#define MIRAGE_TYPE_WRITER_TOC            (mirage_writer_toc_get_type())
#define MIRAGE_WRITER_TOC(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_WRITER_TOC, MirageWriterToc))
#define MIRAGE_WRITER_TOC_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_WRITER_TOC, MirageWriterTocClass))
#define MIRAGE_IS_WRITER_TOC(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_WRITER_TOC))
#define MIRAGE_IS_WRITER_TOC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_WRITER_TOC))
#define MIRAGE_WRITER_TOC_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_WRITER_TOC, MirageWriterTocClass))

typedef struct _MirageWriterToc           MirageWriterToc;
typedef struct _MirageWriterTocClass      MirageWriterTocClass;
typedef struct _MirageWriterTocPrivate    MirageWriterTocPrivate;

struct _MirageWriterToc
{
    MirageWriter parent_instance;

    /*< private >*/
    MirageWriterTocPrivate *priv;
};

struct _MirageWriterTocClass
{
    MirageWriterClass parent_class;
};

/* Used by MIRAGE_TYPE_WRITER_TOC */
GType mirage_writer_toc_get_type (void);
void mirage_writer_toc_type_register (GTypeModule *type_module);


G_END_DECLS
