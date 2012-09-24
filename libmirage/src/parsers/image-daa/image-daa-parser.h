/*
 *  libMirage: DAA image parser: Parser object
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

#ifndef __IMAGE_DAA_PARSER_H__
#define __IMAGE_DAA_PARSER_H__


G_BEGIN_DECLS

#define MIRAGE_TYPE_PARSER_DAA            (mirage_parser_daa_get_type())
#define MIRAGE_PARSER_DAA(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_PARSER_DAA, MirageParser_DAA))
#define MIRAGE_PARSER_DAA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_PARSER_DAA, MirageParser_DAAClass))
#define MIRAGE_IS_PARSER_DAA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_PARSER_DAA))
#define MIRAGE_IS_PARSER_DAA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_PARSER_DAA))
#define MIRAGE_PARSER_DAA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_PARSER_DAA, MirageParser_DAAClass))

typedef struct _MirageParser_DAA MirageParser_DAA;
typedef struct _MirageParser_DAAClass MirageParser_DAAClass;
typedef struct _MirageParser_DAAPrivate MirageParser_DAAPrivate;

struct _MirageParser_DAA
{
    MirageParser parent_instance;

    /*< private >*/
    MirageParser_DAAPrivate *priv;
};

struct _MirageParser_DAAClass
{
    MirageParserClass parent_class;
};

/* Used by MIRAGE_TYPE_PARSER_DAA */
GType mirage_parser_daa_get_type (void);
void mirage_parser_daa_type_register (GTypeModule *type_module);

G_END_DECLS

#endif /* __IMAGE_DAA_PARSER_H__ */
