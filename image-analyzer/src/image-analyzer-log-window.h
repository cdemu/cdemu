/*
 *  Image Analyzer: Parser log window
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

#ifndef __IMAGE_ANALYZER_LOG_WINDOW_H__
#define __IMAGE_ANALYZER_LOG_WINDOW_H__


G_BEGIN_DECLS


#define IMAGE_ANALYZER_TYPE_LOG_WINDOW            (image_analyzer_log_window_get_type())
#define IMAGE_ANALYZER_LOG_WINDOW(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), IMAGE_ANALYZER_TYPE_LOG_WINDOW, IMAGE_ANALYZER_LogWindow))
#define IMAGE_ANALYZER_LOG_WINDOW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), IMAGE_ANALYZER_TYPE_LOG_WINDOW, IMAGE_ANALYZER_LogWindowClass))
#define IMAGE_ANALYZER_IS_LOG_WINDOW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), IMAGE_ANALYZER_TYPE_LOG_WINDOW))
#define IMAGE_ANALYZER_IS_LOG_WINDOW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), IMAGE_ANALYZER_TYPE_LOG_WINDOW))
#define IMAGE_ANALYZER_LOG_WINDOW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), IMAGE_ANALYZER_TYPE_LOG_WINDOW, IMAGE_ANALYZER_LogWindowClass))

typedef struct _IMAGE_ANALYZER_LogWindow        IMAGE_ANALYZER_LogWindow;
typedef struct _IMAGE_ANALYZER_LogWindowClass   IMAGE_ANALYZER_LogWindowClass;
typedef struct _IMAGE_ANALYZER_LogWindowPrivate IMAGE_ANALYZER_LogWindowPrivate;

struct _IMAGE_ANALYZER_LogWindow
{
    GtkWindow parent_instance;

    /*< private >*/
    IMAGE_ANALYZER_LogWindowPrivate *priv;
};

struct _IMAGE_ANALYZER_LogWindowClass
{
    GtkWindowClass parent_class;
};


/* Used by IMAGE_ANALYZER_TYPE_LOG_WINDOW */
GType image_analyzer_log_window_get_type (void);

/* Public API */
void image_analyzer_log_window_clear_log (IMAGE_ANALYZER_LogWindow *self);
void image_analyzer_log_window_append_to_log (IMAGE_ANALYZER_LogWindow *self, gchar *message);


G_END_DECLS

#endif /* __IMAGE_ANALYZER_LOG_WINDOW_H__ */
