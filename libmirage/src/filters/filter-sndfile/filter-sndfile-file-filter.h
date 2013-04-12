/*
 *  libMirage: SNDFILE file filter: File filter object
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __FILTER_SNDFILE_FILE_FILTER_H__
#define __FILTER_SNDFILE_FILE_FILTER_H__


G_BEGIN_DECLS

#define MIRAGE_TYPE_FILE_FILTER_SNDFILE            (mirage_file_filter_sndfile_get_type())
#define MIRAGE_FILE_FILTER_SNDFILE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FILE_FILTER_SNDFILE, MirageFileFilterSndfile))
#define MIRAGE_FILE_FILTER_SNDFILE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_FILE_FILTER_SNDFILE, MirageFileFilterSndfileClass))
#define MIRAGE_IS_FILE_FILTER_SNDFILE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FILE_FILTER_SNDFILE))
#define MIRAGE_IS_FILE_FILTER_SNDFILE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_FILE_FILTER_SNDFILE))
#define MIRAGE_FILE_FILTER_SNDFILE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_FILE_FILTER_SNDFILE, MirageFileFilterSndfileClass))

typedef struct _MirageFileFilterSndfile        MirageFileFilterSndfile;
typedef struct _MirageFileFilterSndfileClass   MirageFileFilterSndfileClass;
typedef struct _MirageFileFilterSndfilePrivate MirageFileFilterSndfilePrivate;

struct _MirageFileFilterSndfile
{
    MirageFileFilter parent_instance;

    /*< private >*/
    MirageFileFilterSndfilePrivate *priv;
};

struct _MirageFileFilterSndfileClass
{
    MirageFileFilterClass parent_class;
};

/* Used by MIRAGE_TYPE_FILE_FILTER_SNDFILE */
GType mirage_file_filter_sndfile_get_type (void);
void mirage_file_filter_sndfile_type_register (GTypeModule *type_module);

G_END_DECLS

#endif /* __FILTER_SNDFILE_FILE_FILTER_H__ */
