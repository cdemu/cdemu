/*
 *  libMirage: parser
 *  Copyright (C) 2008-2014 Rok Mandeljc
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

#ifndef __MIRAGE_PARSER_H__
#define __MIRAGE_PARSER_H__

#include "mirage-types.h"


G_BEGIN_DECLS


/**
 * MirageParserInfo:
 * @id: parser ID
 * @name: parser name
 * @description: (array zero-terminated=1): zero-terminated array of file type description strings
 * @mime_type: (array zero-terminated=1): zero-terminated array of file type MIME strings
 *
 * A structure containing parser information. It can be obtained with call to
 * mirage_parser_get_info().
 */
typedef struct _MirageParserInfo MirageParserInfo;
struct _MirageParserInfo
{
    gchar *id;
    gchar *name;
    gchar **description;
    gchar **mime_type;
};

void mirage_parser_info_copy (const MirageParserInfo *info, MirageParserInfo *dest);
void mirage_parser_info_free (MirageParserInfo *info);


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
 * All the fields in the <structname>MirageParser</structname>
 * structure are private to the #MirageParser implementation and
 * should never be accessed directly.
 */
struct _MirageParser
{
    MirageObject parent_instance;

    /*< private >*/
    MirageParserPrivate *priv;
} ;

/**
 * MirageParserClass:
 * @parent_class: the parent class
 * @load_image: loads image
 *
 * The class structure for the <structname>MirageParser</structname> type.
 */
struct _MirageParserClass
{
    MirageObjectClass parent_class;

    /* Class members */
    MirageDisc *(*load_image) (MirageParser *self, MirageStream **streams, GError **error);
};

/* Used by MIRAGE_TYPE_PARSER */
GType mirage_parser_get_type (void);

void mirage_parser_generate_info (MirageParser *self, const gchar *id, const gchar *name, gint num_types, ...);
const MirageParserInfo *mirage_parser_get_info (MirageParser *self);

MirageDisc *mirage_parser_load_image (MirageParser *self, MirageStream **streams, GError **error);

gint mirage_parser_guess_medium_type (MirageParser *self, MirageDisc *disc);
void mirage_parser_add_redbook_pregap (MirageParser *self, MirageDisc *disc);

GDataInputStream *mirage_parser_create_text_stream (MirageParser *self, MirageStream *stream, GError **error);

G_END_DECLS

#endif /* __MIRAGE_PARSER_H__ */
