/*
 *  libMirage: C2D image: parser
 *  Copyright (C) 2008-2014 Henrik Stokseth
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

#pragma once

G_BEGIN_DECLS


#define MIRAGE_TYPE_PARSER_C2D            (mirage_parser_c2d_get_type())
#define MIRAGE_PARSER_C2D(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_PARSER_C2D, MirageParserC2d))
#define MIRAGE_PARSER_C2D_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_PARSER_C2D, MirageParserC2dClass))
#define MIRAGE_IS_PARSER_C2D(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_PARSER_C2D))
#define MIRAGE_IS_PARSER_C2D_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_PARSER_C2D))
#define MIRAGE_PARSER_C2D_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_PARSER_C2D, MirageParserC2dClass))

typedef struct _MirageParserC2d        MirageParserC2d;
typedef struct _MirageParserC2dClass   MirageParserC2dClass;
typedef struct _MirageParserC2dPrivate MirageParserC2dPrivate;

struct _MirageParserC2d
{
    MirageParser parent_instance;

    /*< private >*/
    MirageParserC2dPrivate *priv;
};

struct _MirageParserC2dClass
{
    MirageParserClass parent_class;
};

/* Used by MIRAGE_TYPE_PARSER_C2D */
GType mirage_parser_c2d_get_type (void);
void mirage_parser_c2d_type_register (GTypeModule *type_module);


G_END_DECLS
