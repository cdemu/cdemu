/*
 *  Image analyzer: application window
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

#ifndef __IMAGE_ANALYZER_APPLICATION_WINDOW_H__
#define __IMAGE_ANALYZER_APPLICATION_WINDOW_H__


G_BEGIN_DECLS

#define IA_TYPE_APPLICATION_WINDOW            (ia_application_window_get_type())
#define IA_APPLICATION_WINDOW(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), IA_TYPE_APPLICATION_WINDOW, IaApplicationWindow))
#define IA_APPLICATION_WINDOW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), IA_TYPE_APPLICATION_WINDOW, IaApplicationWindowClass))
#define IA_IS_APPLICATION_WINDOW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), IA_TYPE_APPLICATION_WINDOW))
#define IA_IS_APPLICATION_WINDOW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), IA_TYPE_APPLICATION_WINDOW))
#define IA_APPLICATION_WINDOW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), IA_TYPE_APPLICATION_WINDOW, IaApplicationWindowClass))

typedef struct _IaApplicationWindow IaApplicationWindow;
typedef struct _IaApplicationWindowClass IaApplicationWindowClass;
typedef struct _IaApplicationWindowPrivate IaApplicationWindowPrivate;

struct _IaApplicationWindow
{
    GtkApplicationWindow parent_instance;

    /*< private >*/
    IaApplicationWindowPrivate *priv;
};

struct _IaApplicationWindowClass {
    GtkApplicationWindowClass parent_class;
};


/* Used by IA_TYPE_APPLICATION_WINDOW */
GType ia_application_window_get_type (void);

void ia_application_window_setup_logger (IaApplicationWindow *self);
void ia_application_window_apply_command_line_options (IaApplicationWindow *self, gboolean debug_to_stdout, gint debug_mask, gchar **filenames);

void ia_application_window_display_instance_id (IaApplicationWindow *self);
void ia_application_window_update_window_title (IaApplicationWindow *self);

G_END_DECLS

#endif /* __IMAGE_ANALYZER_APPLICATION_WINDOW_H__ */
