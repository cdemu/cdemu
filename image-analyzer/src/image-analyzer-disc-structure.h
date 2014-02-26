/*
 *  Image Analyzer: Disc structure window
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

#ifndef __IMAGE_ANALYZER_DISC_STRUCTURE_H__
#define __IMAGE_ANALYZER_DISC_STRUCTURE_H__


G_BEGIN_DECLS


#define IA_TYPE_DISC_STRUCTURE            (ia_disc_structure_get_type())
#define IA_DISC_STRUCTURE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), IA_TYPE_DISC_STRUCTURE, IaDiscStructure))
#define IA_DISC_STRUCTURE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), IA_TYPE_DISC_STRUCTURE, IaDiscStructureClass))
#define IA_IS_DISC_STRUCTURE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), IA_TYPE_DISC_STRUCTURE))
#define IA_IS_DISC_STRUCTURE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), IA_TYPE_DISC_STRUCTURE))
#define IA_DISC_STRUCTURE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), IA_TYPE_DISC_STRUCTURE, IaDiscStructureClass))

typedef struct _IaDiscStructure           IaDiscStructure;
typedef struct _IaDiscStructureClass      IaDiscStructureClass;
typedef struct _IaDiscStructurePrivate    IaDiscStructurePrivate;

struct _IaDiscStructure {
    GtkWindow parent_instance;

    /*< private >*/
    IaDiscStructurePrivate *priv;
};

struct _IaDiscStructureClass {
    GtkWindowClass parent_class;
};


/* Used by IA_TYPE_DISC_STRUCTURE */
GType ia_disc_structure_get_type (void);

/* Public API */
void ia_disc_structure_set_disc (IaDiscStructure *self, MirageDisc *disc);


G_END_DECLS

#endif /* __IMAGE_ANALYZER_DISC_STRUCTURE_H__ */
