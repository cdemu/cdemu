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
 * MIRAGE_FILE_FILTER_MAX_TYPES:
 *
 * Maximal number of MIME types that can be defined as supported by a file filter.
 */
#define MIRAGE_FILE_FILTER_MAX_TYPES 4

/**
 * MirageFileFilterInfo:
 * @id: file filter ID
 * @name: file filter name
 * @num_types: number of valid description and mime_types
 * @description: (array fixed-size=4): file type descriptions
 * @mime_type: (array fixed-size=4): file type MIMEs
 *
 * A structure containing file filter information. It can be obtained
 * with call to mirage_file_filter_get_info().
 */
typedef struct _MirageFileFilterInfo MirageFileFilterInfo;
struct _MirageFileFilterInfo
{
    gchar id[32];
    gchar name[32];
    gint num_types;
    gchar description[MIRAGE_FILE_FILTER_MAX_TYPES][64];
    gchar mime_type[MIRAGE_FILE_FILTER_MAX_TYPES][32];
};


/**********************************************************************\
 *                     MirageFileFilter object                        *
\**********************************************************************/
#define MIRAGE_TYPE_FILE_FILTER            (mirage_file_filter_get_type())
#define MIRAGE_FILE_FILTER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FILE_FILTER, MirageFileFilter))
#define MIRAGE_FILE_FILTER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_FILE_FILTER, MirageFileFilterClass))
#define MIRAGE_IS_FILE_FILTER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FILE_FILTER))
#define MIRAGE_IS_FILE_FILTER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_FILE_FILTER))
#define MIRAGE_FILE_FILTER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_FILE_FILTER, MirageFileFilterClass))

typedef struct _MirageFileFilter         MirageFileFilter;
typedef struct _MirageFileFilterClass    MirageFileFilterClass;
typedef struct _MirageFileFilterPrivate  MirageFileFilterPrivate;

/**
 * MirageFileFilter:
 *
 * All the fields in the <structname>MirageFileFilter</structname>
 * structure are private to the #MirageFileFilter implementation and
 * should never be accessed directly.
 */
struct _MirageFileFilter
{
    GFilterInputStream parent_instance;

    /*< private >*/
    MirageFileFilterPrivate *priv;
};

/**
 * MirageFileFilterClass:
 * @parent_class: the parent class
 * @can_handle_data_format: checks whether file filter can handle data stored in underyling stream
 * @read: reads data from stream
 * @tell: tells the current location within stream
 * @seek: seeks to a location within stream
 * @partial_read: reads a chunk of requested data from stream
 *
 * The class structure for the <structname>MirageFileFilter</structname> type.
 */
struct _MirageFileFilterClass
{
    GFilterInputStreamClass parent_class;

    /* Class members */
    gboolean (*can_handle_data_format) (MirageFileFilter *self, GError **error);


    /* Functions reimplemented from GInputStream: */
    gssize (*read) (MirageFileFilter *self, void *buffer, gsize count, GError **error);

    /* Functions reimplemented from GSeekable: */
    goffset (*tell) (MirageFileFilter *self);
    gboolean (*seek) (MirageFileFilter *self, goffset offset, GSeekType type, GError **error);

    /* Simplified interface */
    gssize (*partial_read) (MirageFileFilter *self, void *buffer, gsize count);
};

/* Used by MIRAGE_TYPE_FILE_FILTER */
GType mirage_file_filter_get_type (void);

void mirage_file_filter_generate_info (MirageFileFilter *self, const gchar *id, const gchar *name, gint num_types, ...);
const MirageFileFilterInfo *mirage_file_filter_get_info (MirageFileFilter *self);

gboolean mirage_file_filter_can_handle_data_format (MirageFileFilter *self, GError **error);

void mirage_file_filter_set_file_size (MirageFileFilter *self, gsize size);

goffset mirage_file_filter_get_position (MirageFileFilter *self);


G_END_DECLS

#endif /* __MIRAGE_FILE_FILTER_H__ */
