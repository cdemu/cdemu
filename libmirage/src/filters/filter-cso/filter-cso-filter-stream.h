/*
 *  libMirage: CSO filter: Filter stream object
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __FILTER_CSO_FILTER_STREAM_H__
#define __FILTER_CSO_FILTER_STREAM_H__


G_BEGIN_DECLS

#define MIRAGE_TYPE_FILTER_STREAM_CSO            (mirage_filter_stream_cso_get_type())
#define MIRAGE_FILTER_STREAM_CSO(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FILTER_STREAM_CSO, MirageFilterStreamCso))
#define MIRAGE_FILTER_STREAM_CSO_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_FILTER_STREAM_CSO, MirageFilterStreamCsoClass))
#define MIRAGE_IS_FILTER_STREAM_CSO(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FILTER_STREAM_CSO))
#define MIRAGE_IS_FILTER_STREAM_CSO_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_FILTER_STREAM_CSO))
#define MIRAGE_FILTER_STREAM_CSO_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_FILTER_STREAM_CSO, MirageFilterStreamCsoClass))

typedef struct _MirageFilterStreamCso        MirageFilterStreamCso;
typedef struct _MirageFilterStreamCsoClass   MirageFilterStreamCsoClass;
typedef struct _MirageFilterStreamCsoPrivate MirageFilterStreamCsoPrivate;

struct _MirageFilterStreamCso
{
    MirageFilterStream parent_instance;

    /*< private >*/
    MirageFilterStreamCsoPrivate *priv;
};

struct _MirageFilterStreamCsoClass
{
    MirageFilterStreamClass parent_class;
};

/* Used by MIRAGE_TYPE_FILTER_STREAM_CSO */
GType mirage_filter_stream_cso_get_type (void);
void mirage_filter_stream_cso_type_register (GTypeModule *type_module);

G_END_DECLS

#endif /* __FILTER_CSO_FILTER_STREAM_H__ */
