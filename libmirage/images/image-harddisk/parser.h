/*
 *  libMirage: hard-disk image: parser
 *  Copyright (C) 2013-2014 Henrik Stokseth
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

#ifndef __IMAGE_HD_PARSER_H__
#define __IMAGE_HD_PARSER_H__


G_BEGIN_DECLS

#define MIRAGE_TYPE_PARSER_HD            (mirage_parser_hd_get_type())
#define MIRAGE_PARSER_HD(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_PARSER_HD, MirageParserHd))
#define MIRAGE_PARSER_HD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_PARSER_HD, MirageParserHdClass))
#define MIRAGE_IS_PARSER_HD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_PARSER_HD))
#define MIRAGE_IS_PARSER_HD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_PARSER_HD))
#define MIRAGE_PARSER_HD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_PARSER_HD, MirageParserHdClass))

typedef struct _MirageParserHd           MirageParserHd;
typedef struct _MirageParserHdClass      MirageParserHdClass;
typedef struct _MirageParserHdPrivate    MirageParserHdPrivate;

struct _MirageParserHd
{
    MirageParser parent_instance;

    /*< private >*/
    MirageParserHdPrivate *priv;
};

struct _MirageParserHdClass
{
    MirageParserClass parent_class;
};

/* Used by MIRAGE_TYPE_PARSER_HD */
GType mirage_parser_hd_get_type (void);
void mirage_parser_hd_type_register (GTypeModule *type_module);

G_END_DECLS

#endif /* __IMAGE_HD_PARSER_H__ */
