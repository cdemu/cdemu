/*
 *  libMirage: File filter object
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

#ifndef __MIRAGE_FILE_FILTER_H__
#define __MIRAGE_FILE_FILTER_H__


G_BEGIN_DECLS

/**
 * MIRAGE_FileFilterInfo:
 * @id: file filter ID
 * @name: file filter name
 *
 * <para>
 * A structure containing file filter information. It can be obtained
 * with call to mirage_file_filter_get_file_filter_info().
 * </para>
 **/
typedef struct _MIRAGE_FileFilterInfo MIRAGE_FileFilterInfo;
struct _MIRAGE_FileFilterInfo
{
    gchar *id;
    gchar *name;
};


/**********************************************************************\
 *                        File filter object                          *
\**********************************************************************/
#define MIRAGE_TYPE_FILE_FILTER            (mirage_file_filter_get_type())
#define MIRAGE_FILE_FILTER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FILE_FILTER, MIRAGE_FileFilter))
#define MIRAGE_FILE_FILTER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_FILE_FILTER, MIRAGE_FileFilterClass))
#define MIRAGE_IS_FILE_FILTER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FILE_FILTER))
#define MIRAGE_IS_FILE_FILTER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_FILE_FILTER))
#define MIRAGE_FILE_FILTER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_FILE_FILTER, MIRAGE_FileFilterClass))

typedef struct _MIRAGE_FileFilter         MIRAGE_FileFilter;
typedef struct _MIRAGE_FileFilterClass    MIRAGE_FileFilterClass;
typedef struct _MIRAGE_FileFilterPrivate  MIRAGE_FileFilterPrivate;

/**
 * MIRAGE_FileFilter:
 *
 * <para>
 * Contains private data only, and should be accessed using the functions below.
 * </para>
 **/
struct _MIRAGE_FileFilter
{
    GFilterInputStream parent_instance;

    /*< private >*/
    MIRAGE_FileFilterPrivate *priv;
};

struct _MIRAGE_FileFilterClass
{
    MIRAGE_ObjectClass parent_class;

    /* Class members */
    gboolean (*can_handle_data_format) (MIRAGE_FileFilter *self, GError **error);

    /* Functions reimplemented from GInputStream: */
    gssize (*read) (MIRAGE_FileFilter *self, void *buffer, gsize count, GError **error);

    /* Functions reimplemented from GSeekable: */
    goffset (*tell) (MIRAGE_FileFilter *self);
    gboolean (*seek) (MIRAGE_FileFilter *self, goffset offset, GSeekType type, GError **error);
};

/* Used by MIRAGE_TYPE_FILE_FILTER */
GType mirage_file_filter_get_type (void);


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
void mirage_file_filter_generate_file_filter_info (MIRAGE_FileFilter *self, const gchar *id, const gchar *name);
gboolean mirage_file_filter_get_file_filter_info (MIRAGE_FileFilter *self, const MIRAGE_FileFilterInfo **file_filter_info, GError **error);

gboolean mirage_file_filter_can_handle_data_format (MIRAGE_FileFilter *self, GError **error);

G_END_DECLS

#endif /* __MIRAGE_FILE_FILTER_H__ */
