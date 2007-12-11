/*
 *  MIRAGE Image Analyzer: Parser log window
 *  Copyright (C) 2007 Rok Mandeljc
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

#ifndef __IMAGE_ANALYZER_PARSER_LOG_H__
#define __IMAGE_ANALYZER_PARSER_LOG_H__


G_BEGIN_DECLS


#define IMAGE_ANALYZER_TYPE_PARSER_LOG            (image_analyzer_parser_log_get_type())
#define IMAGE_ANALYZER_PARSER_LOG(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), IMAGE_ANALYZER_TYPE_PARSER_LOG, IMAGE_ANALYZER_ParserLog))
#define IMAGE_ANALYZER_PARSER_LOG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), IMAGE_ANALYZER_TYPE_PARSER_LOG, IMAGE_ANALYZER_ParserLogClass))
#define IMAGE_ANALYZER_IS_PARSER_LOG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), IMAGE_ANALYZER_TYPE_PARSER_LOG))
#define IMAGE_ANALYZER_IS_PARSER_LOG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), IMAGE_ANALYZER_TYPE_PARSER_LOG))
#define IMAGE_ANALYZER_PARSER_LOG_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), IMAGE_ANALYZER_TYPE_PARSER_LOG, IMAGE_ANALYZER_ParserLogClass))

typedef struct {
    GtkWindow parent;
} IMAGE_ANALYZER_ParserLog;

typedef struct {
    GtkWindowClass parent;
} IMAGE_ANALYZER_ParserLogClass;

/* Used by IMAGE_ANALYZER_TYPE_PARSER_LOG */
GType image_analyzer_parser_log_get_type (void);


/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
gboolean image_analyzer_parser_log_clear_log (IMAGE_ANALYZER_ParserLog *self, GError **error);
gboolean image_analyzer_parser_log_append_to_log (IMAGE_ANALYZER_ParserLog *self, gchar *message, GError **error);

G_END_DECLS

#endif /* __IMAGE_ANALYZER_PARSER_LOG_H__ */
