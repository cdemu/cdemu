/*
 *  libMirage: X-CD-Roast image: parser
 *  Copyright (C) 2009-2026 Rok Mandeljc
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


#define MIRAGE_TYPE_PARSER_XCDROAST            (mirage_parser_xcdroast_get_type())
#define MIRAGE_PARSER_XCDROAST(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_PARSER_XCDROAST, MirageParserXcdroast))
#define MIRAGE_PARSER_XCDROAST_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_PARSER_XCDROAST, MirageParserXcdroastClass))
#define MIRAGE_IS_PARSER_XCDROAST(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_PARSER_XCDROAST))
#define MIRAGE_IS_PARSER_XCDROAST_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_PARSER_XCDROAST))
#define MIRAGE_PARSER_XCDROAST_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_PARSER_XCDROAST, MirageParserXcdroastClass))

typedef struct _MirageParserXcdroast          MirageParserXcdroast;
typedef struct _MirageParserXcdroastClass     MirageParserXcdroastClass;
typedef struct _MirageParserXcdroastPrivate   MirageParserXcdroastPrivate;

struct _MirageParserXcdroast
{
    MirageParser parent_instance;

    /*< private >*/
    MirageParserXcdroastPrivate *priv;
};

struct _MirageParserXcdroastClass
{
    MirageParserClass parent_class;
};

/* Used by MIRAGE_TYPE_PARSER_XCDROAST */
GType mirage_parser_xcdroast_get_type (void);
void mirage_parser_xcdroast_type_register (GTypeModule *type_module);


G_END_DECLS
