/*
 *  libMirage: Parser object
 *  Copyright (C) 2008-2010 Rok Mandeljc
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

#ifndef __MIRAGE_PARSER_H__
#define __MIRAGE_PARSER_H__


G_BEGIN_DECLS

/**
 * MIRAGE_ParserInfo:
 * @id: parser ID
 * @name: parser name
 * @description: image file description
 * @mime_type: image file MIME type
 *
 * <para>
 * A structure containing parser information. It can be obtained with call to
 * mirage_parser_get_parser_info().
 * </para>
 * 
 * <para>
 * @description is a string contraining image file description (e.g. "CloneCD 
 * images") and @mime_type is a string representing the image file MIME type
 * (e.g. "application/libmirage-mds"). Together, this information is intended 
 * to be used for building file type filters in GUI applications.
 * </para>
 **/
typedef struct {
    gchar *id;
    gchar *name;
    gchar *description;
    gchar *mime_type;
} MIRAGE_ParserInfo;


#define MIRAGE_TYPE_PARSER            (mirage_parser_get_type())
#define MIRAGE_PARSER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_PARSER, MIRAGE_Parser))
#define MIRAGE_PARSER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_PARSER, MIRAGE_ParserClass))
#define MIRAGE_IS_PARSER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_PARSER))
#define MIRAGE_IS_PARSER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_PARSER))
#define MIRAGE_PARSER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_PARSER, MIRAGE_ParserClass))

/**
 * MIRAGE_Parser:
 *
 * <para>
 * Contains private data only, and should be accessed using the functions below.
 * </para>
 **/
typedef struct {
    MIRAGE_Object parent;
} MIRAGE_Parser;

typedef struct {
    MIRAGE_ObjectClass parent;
    
    /* Class members */
    gboolean (*load_image) (MIRAGE_Parser *self, gchar **filenames, GObject **disc, GError **error);
} MIRAGE_ParserClass;

/* Used by MIRAGE_TYPE_PARSER */
GType mirage_parser_get_type (void);


/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
void mirage_parser_generate_parser_info (MIRAGE_Parser *self, const gchar *id, const gchar *name, const gchar *description, const gchar *mime_type);
gboolean mirage_parser_get_parser_info (MIRAGE_Parser *self, const MIRAGE_ParserInfo **parser_info, GError **error);

gboolean mirage_parser_load_image (MIRAGE_Parser *self, gchar **filenames, GObject **disc, GError **error);

gint mirage_parser_guess_medium_type (MIRAGE_Parser *self, GObject *disc);
gboolean mirage_parser_add_redbook_pregap (MIRAGE_Parser *self, GObject *disc, GError **error);

gboolean mirage_parser_set_params (MIRAGE_Parser *self, GHashTable *params, GError **error);
gboolean mirage_parser_get_param (MIRAGE_Parser *self, const gchar *name, const GVariantType *type, GVariant **ret_value, GError **error);
gboolean mirage_parser_get_param_string (MIRAGE_Parser *self, const gchar *name, const gchar **ret_value, GError **error);


G_END_DECLS

#endif /* __MIRAGE_PARSER_H__ */
