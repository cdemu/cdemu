/*
 *  libMirage: ISO image parser: Parser object
 *  Copyright (C) 2006-2012 Rok Mandeljc
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

#ifndef __IMAGE_ISO_PARSER_H__
#define __IMAGE_ISO_PARSER_H__


G_BEGIN_DECLS

#define MIRAGE_TYPE_PARSER_ISO            (mirage_parser_iso_get_type())
#define MIRAGE_PARSER_ISO(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_PARSER_ISO, MIRAGE_Parser_ISO))
#define MIRAGE_PARSER_ISO_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_PARSER_ISO, MIRAGE_Parser_ISOClass))
#define MIRAGE_IS_PARSER_ISO(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_PARSER_ISO))
#define MIRAGE_IS_PARSER_ISO_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_PARSER_ISO))
#define MIRAGE_PARSER_ISO_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_PARSER_ISO, MIRAGE_Parser_ISOClass))

typedef struct _MIRAGE_Parser_ISO           MIRAGE_Parser_ISO;
typedef struct _MIRAGE_Parser_ISOClass      MIRAGE_Parser_ISOClass;
typedef struct _MIRAGE_Parser_ISOPrivate    MIRAGE_Parser_ISOPrivate;

struct _MIRAGE_Parser_ISO
{
    MIRAGE_Parser parent_instance;

    /*< private >*/
    MIRAGE_Parser_ISOPrivate *priv;
};

struct _MIRAGE_Parser_ISOClass
{
    MIRAGE_ParserClass parent_class;
};

/* Used by MIRAGE_TYPE_PARSER_ISO */
GType mirage_parser_iso_get_type (void);
void mirage_parser_iso_type_register (GTypeModule *type_module);

G_END_DECLS

#endif /* __IMAGE_ISO_PARSER_H__ */
