/*
 *  Image Analyzer: Application
 *  Copyright (C) 2007-2014 Rok Mandeljc
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

#ifndef __IMAGE_ANALYZER_APPLICATION_H__
#define __IMAGE_ANALYZER_APPLICATION_H__


G_BEGIN_DECLS

#define IA_TYPE_APPLICATION            (ia_application_get_type())
#define IA_APPLICATION(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), IA_TYPE_APPLICATION, IaApplication))
#define IA_APPLICATION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), IA_TYPE_APPLICATION, IaApplicationClass))
#define IA_IS_APPLICATION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), IA_TYPE_APPLICATION))
#define IA_IS_APPLICATION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), IA_TYPE_APPLICATION))
#define IA_APPLICATION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), IA_TYPE_APPLICATION, IaApplicationClass))

typedef struct _IaApplication IaApplication;
typedef struct _IaApplicationClass IaApplicationClass;
typedef struct _IaApplicationPrivate IaApplicationPrivate;

struct _IaApplication
{
    GtkApplication parent_instance;

    /*< private >*/
    IaApplicationPrivate *priv;
};

struct _IaApplicationClass {
    GtkApplicationClass parent_class;
};


/* Used by IA_TYPE_APPLICATION */
GType ia_application_get_type (void);

G_END_DECLS

#endif /* __IMAGE_ANALYZER_APPLICATION_H__ */
