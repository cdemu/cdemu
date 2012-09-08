/*
 *  libMirage: GZIP file filter: File filter object
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

#ifndef __MIRAGE_FILTER_GZIP_FILE_FILTER_H__
#define __MIRAGE_FILTER_GZIP_FILE_FILTER_H__


G_BEGIN_DECLS

#define MIRAGE_TYPE_FILE_FILTER_GZIP            (mirage_file_filter_gzip_get_type())
#define MIRAGE_FILE_FILTER_GZIP(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FILE_FILTER_GZIP, MIRAGE_FileFilter_GZIP))
#define MIRAGE_FILE_FILTER_GZIP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_FILE_FILTER_GZIP, MIRAGE_FileFilter_GZIPClass))
#define MIRAGE_IS_FILE_FILTER_GZIP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FILE_FILTER_GZIP))
#define MIRAGE_IS_FILE_FILTER_GZIP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_FILE_FILTER_GZIP))
#define MIRAGE_FILE_FILTER_GZIP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_FILE_FILTER_GZIP, MIRAGE_FileFilter_GZIPClass))

typedef struct _MIRAGE_FileFilter_GZIP        MIRAGE_FileFilter_GZIP;
typedef struct _MIRAGE_FileFilter_GZIPClass   MIRAGE_FileFilter_GZIPClass;
typedef struct _MIRAGE_FileFilter_GZIPPrivate MIRAGE_FileFilter_GZIPPrivate;

struct _MIRAGE_FileFilter_GZIP
{
    MIRAGE_FileFilter parent_instance;

    /*< private >*/
    MIRAGE_FileFilter_GZIPPrivate *priv;
};

struct _MIRAGE_FileFilter_GZIPClass
{
    MIRAGE_FileFilterClass parent_class;
};

/* Used by MIRAGE_TYPE_FILE_FILTER_GZIP */
GType mirage_file_filter_gzip_get_type (void);
void mirage_file_filter_gzip_type_register (GTypeModule *type_module);

G_END_DECLS

#endif /* __MIRAGE_FILTER_GZIP_FILE_FILTER_H__ */
