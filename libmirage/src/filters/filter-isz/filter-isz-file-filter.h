/*
 *  libMirage: ISZ file filter: File filter object
 *  Copyright (C) 2012 Henrik Stokseth
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

#ifndef __FILTER_ISZ_FILE_FILTER_H__
#define __FILTER_ISZ_FILE_FILTER_H__


G_BEGIN_DECLS

#define MIRAGE_TYPE_FILE_FILTER_ISZ            (mirage_file_filter_isz_get_type())
#define MIRAGE_FILE_FILTER_ISZ(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FILE_FILTER_ISZ, MirageFileFilterIsz))
#define MIRAGE_FILE_FILTER_ISZ_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_FILE_FILTER_ISZ, MirageFileFilterIszClass))
#define MIRAGE_IS_FILE_FILTER_ISZ(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FILE_FILTER_ISZ))
#define MIRAGE_IS_FILE_FILTER_ISZ_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_FILE_FILTER_ISZ))
#define MIRAGE_FILE_FILTER_ISZ_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_FILE_FILTER_ISZ, MirageFileFilterIszClass))

typedef struct _MirageFileFilterIsz        MirageFileFilterIsz;
typedef struct _MirageFileFilterIszClass   MirageFileFilterIszClass;
typedef struct _MirageFileFilterIszPrivate MirageFileFilterIszPrivate;

struct _MirageFileFilterIsz
{
    MirageFileFilter parent_instance;

    /*< private >*/
    MirageFileFilterIszPrivate *priv;
};

struct _MirageFileFilterIszClass
{
    MirageFileFilterClass parent_class;
};

/* Used by MIRAGE_TYPE_FILE_FILTER_ISZ */
GType mirage_file_filter_isz_get_type (void);
void mirage_file_filter_isz_type_register (GTypeModule *type_module);

G_END_DECLS

#endif /* __FILTER_ISZ_FILE_FILTER_H__ */
