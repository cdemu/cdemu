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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __IMAGE_ANALYZER_LOG_WINDOW_H__
#define __IMAGE_ANALYZER_LOG_WINDOW_H__


G_BEGIN_DECLS


#define IA_TYPE_LOG_WINDOW            (ia_log_window_get_type())
#define IA_LOG_WINDOW(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), IA_TYPE_LOG_WINDOW, IaLogWindow))
#define IA_LOG_WINDOW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), IA_TYPE_LOG_WINDOW, IaLogWindowClass))
#define IA_IS_LOG_WINDOW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), IA_TYPE_LOG_WINDOW))
#define IA_IS_LOG_WINDOW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), IA_TYPE_LOG_WINDOW))
#define IA_LOG_WINDOW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), IA_TYPE_LOG_WINDOW, IaLogWindowClass))

typedef struct _IaLogWindow        IaLogWindow;
typedef struct _IaLogWindowClass   IaLogWindowClass;
typedef struct _IaLogWindowPrivate IaLogWindowPrivate;

struct _IaLogWindow
{
    GtkWindow parent_instance;

    /*< private >*/
    IaLogWindowPrivate *priv;
};

struct _IaLogWindowClass
{
    GtkWindowClass parent_class;
};


/* Used by IA_TYPE_LOG_WINDOW */
GType ia_log_window_get_type (void);

/* Public API */
void ia_log_window_clear_log (IaLogWindow *self);
void ia_log_window_append_to_log (IaLogWindow *self, const gchar *message);

gchar *ia_log_window_get_log_text (IaLogWindow *self);

void ia_log_window_set_debug_to_stdout (IaLogWindow *self, gboolean enabled);

G_END_DECLS

#endif /* __IMAGE_ANALYZER_LOG_WINDOW_H__ */
