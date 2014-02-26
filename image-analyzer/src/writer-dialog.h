/*
 *  Image Analyzer: Writer dialog
 *  Copyright (C) 2014 Rok Mandeljc
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

#ifndef __IMAGE_ANALYZER_WRITER_DIALOG_H__
#define __IMAGE_ANALYZER_WRITER_DIALOG_H__


G_BEGIN_DECLS


#define IA_TYPE_WRITER_DIALOG            (ia_writer_dialog_get_type())
#define IA_WRITER_DIALOG(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), IA_TYPE_WRITER_DIALOG, IaWriterDialog))
#define IA_WRITER_DIALOG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), IA_TYPE_WRITER_DIALOG, IaWriterDialogClass))
#define IA_IS_WRITER_DIALOG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), IA_TYPE_WRITER_DIALOG))
#define IA_IS_WRITER_DIALOG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), IA_TYPE_WRITER_DIALOG))
#define IA_WRITER_DIALOG_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), IA_TYPE_WRITER_DIALOG, IaWriterDialogClass))

typedef struct _IaWriterDialog           IaWriterDialog;
typedef struct _IaWriterDialogClass      IaWriterDialogClass;
typedef struct _IaWriterDialogPrivate    IaWriterDialogPrivate;

struct _IaWriterDialog {
    GtkDialog parent_instance;

    /*< private >*/
    IaWriterDialogPrivate *priv;
};

struct _IaWriterDialogClass {
    GtkDialogClass parent_class;
};


/* Used by IA_TYPE_WRITER_DIALOG */
GType ia_writer_dialog_get_type (void);

const gchar *ia_writer_dialog_get_filename (IaWriterDialog *self);
MirageWriter *ia_writer_dialog_get_writer (IaWriterDialog *self);
GHashTable *ia_writer_dialog_get_writer_parameters (IaWriterDialog *self);

G_END_DECLS

#endif /* __IMAGE_ANALYZER_WRITER_DIALOG_H__ */
