/*
 *  libMirage: XZ file filter: File filter object
 *  Copyright (C) 2012 Rok Mandeljc
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

#ifndef __MIRAGE_FILTER_XZ_FILE_FILTER_H__
#define __MIRAGE_FILTER_XZ_FILE_FILTER_H__


G_BEGIN_DECLS

#define MIRAGE_TYPE_FILE_FILTER_XZ            (mirage_file_filter_xz_get_type())
#define MIRAGE_FILE_FILTER_XZ(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FILE_FILTER_XZ, MirageFileFilterXz))
#define MIRAGE_FILE_FILTER_XZ_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_FILE_FILTER_XZ, MirageFileFilterXzClass))
#define MIRAGE_IS_FILE_FILTER_XZ(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FILE_FILTER_XZ))
#define MIRAGE_IS_FILE_FILTER_XZ_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_FILE_FILTER_XZ))
#define MIRAGE_FILE_FILTER_XZ_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_FILE_FILTER_XZ, MirageFileFilterXzClass))

typedef struct _MirageFileFilterXz        MirageFileFilterXz;
typedef struct _MirageFileFilterXzClass   MirageFileFilterXzClass;
typedef struct _MirageFileFilterXzPrivate MirageFileFilterXzPrivate;

struct _MirageFileFilterXz
{
    MirageFileFilter parent_instance;

    /*< private >*/
    MirageFileFilterXzPrivate *priv;
};

struct _MirageFileFilterXzClass
{
    MirageFileFilterClass parent_class;
};

/* Used by MIRAGE_TYPE_FILE_FILTER_XZ */
GType mirage_file_filter_xz_get_type (void);
void mirage_file_filter_xz_type_register (GTypeModule *type_module);

G_END_DECLS

#endif /* __MIRAGE_FILTER_XZ_FILE_FILTER_H__ */
