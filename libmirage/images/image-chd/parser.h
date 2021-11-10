/*
 *  libMirage: chd image: parser
 *  Copyright (C) 2011-2014 Rok Mandeljc
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

#ifndef __IMAGE_CHD_PARSER_H__
#define __IMAGE_CHD_PARSER_H__


G_BEGIN_DECLS

#define MIRAGE_TYPE_PARSER_CHD            (mirage_parser_chd_get_type())
#define MIRAGE_PARSER_CHD(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_PARSER_CHD, MirageParserChd))
#define MIRAGE_PARSER_CHD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_PARSER_CHD, MirageParserChdClass))
#define MIRAGE_IS_PARSER_CHD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_PARSER_CHD))
#define MIRAGE_IS_PARSER_CHD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_PARSER_CHD))
#define MIRAGE_PARSER_CHD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_PARSER_CHD, MirageParserChdClass))

typedef struct _MirageParserChd        MirageParserChd;
typedef struct _MirageParserChdClass   MirageParserChdClass;
typedef struct _MirageParserChdPrivate MirageParserChdPrivate;

struct _MirageParserChd
{
    MirageParser parent_instance;

    /*< private >*/
    MirageParserChdPrivate *priv;
};

struct _MirageParserChdClass
{
    MirageParserClass parent_class;
};

/* Used by MIRAGE_TYPE_PARSER_CHD */
GType mirage_parser_chd_get_type (void);
void mirage_parser_chd_type_register (GTypeModule *type_module);
const char *_byte_array_to_hex_string(const UINT8 *pin, const UINT8 size);

G_END_DECLS

#endif /* __IMAGE_CHD_PARSER_H__ */
