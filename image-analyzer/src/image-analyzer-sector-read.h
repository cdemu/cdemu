/*
 *  Image Analyzer: Sector read window
 *  Copyright (C) 2007-2012 Rok Mandeljc
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

#ifndef __IMAGE_ANALYZER_SECTOR_READ_H__
#define __IMAGE_ANALYZER_SECTOR_READ_H__


G_BEGIN_DECLS


#define IA_TYPE_SECTOR_READ            (ia_sector_read_get_type())
#define IA_SECTOR_READ(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), IA_TYPE_SECTOR_READ, IaSectorRead))
#define IA_SECTOR_READ_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), IA_TYPE_SECTOR_READ, IaSectorReadClass))
#define IA_IS_SECTOR_READ(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), IA_TYPE_SECTOR_READ))
#define IA_IS_SECTOR_READ_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), IA_TYPE_SECTOR_READ))
#define IA_SECTOR_READ_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), IA_TYPE_SECTOR_READ, IaSectorReadClass))

typedef struct _IaSectorRead           IaSectorRead;
typedef struct _IaSectorReadClass      IaSectorReadClass;
typedef struct _IaSectorReadPrivate    IaSectorReadPrivate;

struct _IaSectorRead {
    GtkWindow parent_instance;

    /*< private >*/
    IaSectorReadPrivate *priv;
};

struct _IaSectorReadClass {
    GtkWindowClass parent_class;
};


/* Used by IA_TYPE_SECTOR_READ */
GType ia_sector_read_get_type (void);

/* Public API */
void ia_sector_read_set_disc (IaSectorRead *self, MirageDisc *disc);


G_END_DECLS

#endif /* __IMAGE_ANALYZER_SECTOR_READ_H__ */
