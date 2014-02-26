/*
 *  Image Analyzer: Disc topology window
 *  Copyright (C) 2008-2012 Rok Mandeljc
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

#ifndef __IMAGE_ANALYZER_DISC_TOPOLOGY_H__
#define __IMAGE_ANALYZER_DISC_TOPOLOGY_H__


G_BEGIN_DECLS


#define IA_TYPE_DISC_TOPOLOGY            (ia_disc_topology_get_type())
#define IA_DISC_TOPOLOGY(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), IA_TYPE_DISC_TOPOLOGY, IaDiscTopology))
#define IA_DISC_TOPOLOGY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), IA_TYPE_DISC_TOPOLOGY, IaDiscTopologyClass))
#define IA_IS_DISC_TOPOLOGY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), IA_TYPE_DISC_TOPOLOGY))
#define IA_IS_DISC_TOPOLOGY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), IA_TYPE_DISC_TOPOLOGY))
#define IA_DISC_TOPOLOGY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), IA_TYPE_DISC_TOPOLOGY, IaDiscTopologyClass))

typedef struct _IaDiscTopology         IaDiscTopology;
typedef struct _IaDiscTopologyClass    IaDiscTopologyClass;
typedef struct _IaDiscTopologyPrivate  IaDiscTopologyPrivate;

struct _IaDiscTopology
{
    GtkWindow parent_instance;

    /*< private >*/
    IaDiscTopologyPrivate *priv;
};

struct _IaDiscTopologyClass
{
    GtkWindowClass parent_class;
};

/* Used by IA_TYPE_DISC_TOPOLOGY */
GType ia_disc_topology_get_type (void);


/* Public API */
void ia_disc_topology_set_disc (IaDiscTopology *self, MirageDisc *disc);


G_END_DECLS

#endif /* __IMAGE_ANALYZER_DISC_TOPOLOGY_H__ */
