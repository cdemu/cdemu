/*
 *  libMirage: DAA image parser: Fragment object
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
 
#ifndef __IMAGE_DAA_FRAGMENT_H__
#define __IMAGE_DAA_FRAGMENT_H__


G_BEGIN_DECLS

#define MIRAGE_TYPE_FRAGMENT_DAA            (mirage_fragment_daa_get_type())
#define MIRAGE_FRAGMENT_DAA(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FRAGMENT_DAA, MIRAGE_Fragment_DAA))
#define MIRAGE_FRAGMENT_DAA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_FRAGMENT_DAA, MIRAGE_Fragment_DAAClass))
#define MIRAGE_IS_FRAGMENT_DAA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FRAGMENT_DAA))
#define MIRAGE_IS_FRAGMENT_DAA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_FRAGMENT_DAA))
#define MIRAGE_FRAGMENT_DAA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_FRAGMENT_DAA, MIRAGE_Fragment_DAAClass))

typedef struct _MIRAGE_Fragment_DAA         MIRAGE_Fragment_DAA;
typedef struct _MIRAGE_Fragment_DAAClass    MIRAGE_Fragment_DAAClass;
typedef struct _MIRAGE_Fragment_DAAPrivate  MIRAGE_Fragment_DAAPrivate;

struct _MIRAGE_Fragment_DAA
{
    MIRAGE_Fragment parent_instance;

    /*< private >*/
    MIRAGE_Fragment_DAAPrivate *priv;
};

struct _MIRAGE_Fragment_DAAClass
{
    MIRAGE_FragmentClass parent_class;
};

/* Used by MIRAGE_TYPE_FRAGMENT_DAA */
GType mirage_fragment_daa_get_type (void);
void mirage_fragment_daa_type_register (GTypeModule *type_module);

gboolean mirage_fragment_daa_set_file (MIRAGE_Fragment_DAA *self, gchar *file, gchar *password, GError **error);

G_END_DECLS

#endif /* __IMAGE_DAA_FRAGMENT_H__ */
