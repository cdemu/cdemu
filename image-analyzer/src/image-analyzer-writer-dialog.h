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


#define IMAGE_ANALYZER_TYPE_WRITER_DIALOG            (image_analyzer_writer_dialog_get_type())
#define IMAGE_ANALYZER_WRITER_DIALOG(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), IMAGE_ANALYZER_TYPE_WRITER_DIALOG, ImageAnalyzerWriterDialog))
#define IMAGE_ANALYZER_WRITER_DIALOG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), IMAGE_ANALYZER_TYPE_WRITER_DIALOG, ImageAnalyzerWriterDialogClass))
#define IMAGE_ANALYZER_IS_WRITER_DIALOG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), IMAGE_ANALYZER_TYPE_WRITER_DIALOG))
#define IMAGE_ANALYZER_IS_WRITER_DIALOG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), IMAGE_ANALYZER_TYPE_WRITER_DIALOG))
#define IMAGE_ANALYZER_WRITER_DIALOG_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), IMAGE_ANALYZER_TYPE_WRITER_DIALOG, ImageAnalyzerWriterDialogClass))

typedef struct _ImageAnalyzerWriterDialog           ImageAnalyzerWriterDialog;
typedef struct _ImageAnalyzerWriterDialogClass      ImageAnalyzerWriterDialogClass;
typedef struct _ImageAnalyzerWriterDialogPrivate    ImageAnalyzerWriterDialogPrivate;

struct _ImageAnalyzerWriterDialog {
    GtkDialog parent_instance;

    /*< private >*/
    ImageAnalyzerWriterDialogPrivate *priv;
};

struct _ImageAnalyzerWriterDialogClass {
    GtkDialogClass parent_class;
};


/* Used by IMAGE_ANALYZER_TYPE_WRITER_DIALOG */
GType image_analyzer_writer_dialog_get_type (void);

const gchar *image_analyzer_writer_dialog_get_filename (ImageAnalyzerWriterDialog *self);
MirageWriter *image_analyzer_writer_dialog_get_writer (ImageAnalyzerWriterDialog *self);
GHashTable *image_analyzer_writer_dialog_get_writer_parameters (ImageAnalyzerWriterDialog *self);

G_END_DECLS

#endif /* __IMAGE_ANALYZER_WRITER_DIALOG_H__ */
