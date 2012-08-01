/*
 *  Image Analyzer: Application object
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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __IMAGE_ANALYZER_APPLICATION_H__
#define __IMAGE_ANALYZER_APPLICATION_H__


G_BEGIN_DECLS

#define IMAGE_ANALYZER_TYPE_APPLICATION            (image_analyzer_application_get_type())
#define IMAGE_ANALYZER_APPLICATION(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), IMAGE_ANALYZER_TYPE_APPLICATION, IMAGE_ANALYZER_Application))
#define IMAGE_ANALYZER_APPLICATION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), IMAGE_ANALYZER_TYPE_APPLICATION, IMAGE_ANALYZER_ApplicationClass))
#define IMAGE_ANALYZER_IS_APPLICATION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), IMAGE_ANALYZER_TYPE_APPLICATION))
#define IMAGE_ANALYZER_IS_APPLICATION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), IMAGE_ANALYZER_TYPE_APPLICATION))
#define IMAGE_ANALYZER_APPLICATION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), IMAGE_ANALYZER_TYPE_APPLICATION, IMAGE_ANALYZER_ApplicationClass))

typedef struct _IMAGE_ANALYZER_Application IMAGE_ANALYZER_Application;
typedef struct _IMAGE_ANALYZER_ApplicationClass IMAGE_ANALYZER_ApplicationClass;
typedef struct _IMAGE_ANALYZER_ApplicationPrivate IMAGE_ANALYZER_ApplicationPrivate;

struct _IMAGE_ANALYZER_Application
{
    GObject parent_instance;

    /*< private >*/
    IMAGE_ANALYZER_ApplicationPrivate *priv;
};

struct _IMAGE_ANALYZER_ApplicationClass {
    GObjectClass parent_class;
};


/* Used by IMAGE_ANALYZER_TYPE_APPLICATION */
GType image_analyzer_application_get_type (void);


/* Public API */
gboolean image_analyzer_application_run (IMAGE_ANALYZER_Application *self, gchar **open_image, gboolean debug_stdout);
gboolean image_analyzer_application_get_loaded_image (IMAGE_ANALYZER_Application *self, GObject **disc);

G_END_DECLS

#endif /* __IMAGE_ANALYZER_APPLICATION_H__ */
