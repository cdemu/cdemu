/*
 *  libMirage: B6T image: parser
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

#ifndef __IMAGE_B6T_PARSER_H__
#define __IMAGE_B6T_PARSER_H__


G_BEGIN_DECLS

#define MIRAGE_TYPE_PARSER_B6T            (mirage_parser_b6t_get_type())
#define MIRAGE_PARSER_B6T(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_PARSER_B6T, MirageParserB6t))
#define MIRAGE_PARSER_B6T_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_PARSER_B6T, MirageParserB6tClass))
#define MIRAGE_IS_PARSER_B6T(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_PARSER_B6T))
#define MIRAGE_IS_PARSER_B6T_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_PARSER_B6T))
#define MIRAGE_PARSER_B6T_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_PARSER_B6T, MirageParserB6tClass))

typedef struct _MirageParserB6t           MirageParserB6t;
typedef struct _MirageParserB6tClass      MirageParserB6tClass;
typedef struct _MirageParserB6tPrivate    MirageParserB6tPrivate;

struct _MirageParserB6t
{
    MirageParser parent_instance;

    /*< private >*/
    MirageParserB6tPrivate *priv;
};

struct _MirageParserB6tClass
{
    MirageParserClass parent_class;
};

/* Used by MIRAGE_TYPE_PARSER_B6T */
GType mirage_parser_b6t_get_type (void);
void mirage_parser_b6t_type_register (GTypeModule *type_module);

G_END_DECLS

#endif /* __IMAGE_B6T_PARSER_H__ */
