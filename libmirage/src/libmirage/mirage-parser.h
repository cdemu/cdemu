/*
 *  libMirage: Parser object
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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __MIRAGE_PARSER_H__
#define __MIRAGE_PARSER_H__

/* Forward declarations */
typedef struct _MirageDisc MirageDisc;


G_BEGIN_DECLS

/**
 * MirageParserInfo:
 * @id: parser ID
 * @name: parser name
 * @description: image file description
 * @mime_type: image file MIME type
 *
 * <para>
 * A structure containing parser information. It can be obtained with call to
 * mirage_parser_get_info().
 * </para>
 *
 * <para>
 * @description is a string contraining image file description (e.g. "CloneCD
 * images") and @mime_type is a string representing the image file MIME type
 * (e.g. "application/libmirage-mds"). Together, this information is intended
 * to be used for building file type filters in GUI applications.
 * </para>
 **/
typedef struct _MirageParserInfo MirageParserInfo;
struct _MirageParserInfo
{
    gchar id[32];
    gchar name[32];
    gchar description[64];
    gchar mime_type[32];
};


/**********************************************************************\
 *                         MirageParser object                        *
\**********************************************************************/
#define MIRAGE_TYPE_PARSER            (mirage_parser_get_type())
#define MIRAGE_PARSER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_PARSER, MirageParser))
#define MIRAGE_PARSER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_PARSER, MirageParserClass))
#define MIRAGE_IS_PARSER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_PARSER))
#define MIRAGE_IS_PARSER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_PARSER))
#define MIRAGE_PARSER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_PARSER, MirageParserClass))

typedef struct _MirageParser           MirageParser;
typedef struct _MirageParserClass      MirageParserClass;
typedef struct _MirageParserPrivate    MirageParserPrivate;

/**
 * MirageParser:
 *
 * <para>
 * Contains private data only, and should be accessed using the functions below.
 * </para>
 **/
struct _MirageParser
{
    MirageObject parent_instance;

    /*< private >*/
    MirageParserPrivate *priv;
} ;

struct _MirageParserClass
{
    MirageObjectClass parent_class;

    /* Class members */
    MirageDisc *(*load_image) (MirageParser *self, GInputStream **streams, GError **error);
};

/* Used by MIRAGE_TYPE_PARSER */
GType mirage_parser_get_type (void);

void mirage_parser_generate_info (MirageParser *self, const gchar *id, const gchar *name, const gchar *description, const gchar *mime_type);
const MirageParserInfo *mirage_parser_get_info (MirageParser *self);

MirageDisc *mirage_parser_load_image (MirageParser *self, GInputStream **streams, GError **error);

gint mirage_parser_guess_medium_type (MirageParser *self, MirageDisc *disc);
void mirage_parser_add_redbook_pregap (MirageParser *self, MirageDisc *disc);

GDataInputStream *mirage_parser_create_text_stream (MirageParser *self, GInputStream *stream, GError **error);

G_END_DECLS

#endif /* __MIRAGE_PARSER_H__ */
