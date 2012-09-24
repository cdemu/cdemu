/*
 *  libMirage: SNDFILE fragment plugin
 *  Copyright (C) 2007-2012 Rok Mandeljc
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

#ifndef __MIRAGE_FRAGMENT_SNDFILE_FRAGMENT_H__
#define __MIRAGE_FRAGMENT_SNDFILE_FRAGMENT_H__


G_BEGIN_DECLS

#define MIRAGE_TYPE_FRAGMENT_SNDFILE            (mirage_fragment_sndfile_get_type())
#define MIRAGE_FRAGMENT_SNDFILE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FRAGMENT_SNDFILE, MirageFragmentSndfile))
#define MIRAGE_FRAGMENT_SNDFILE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_FRAGMENT_SNDFILE, MirageFragmentSndfileClass))
#define MIRAGE_IS_FRAGMENT_SNDFILE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FRAGMENT_SNDFILE))
#define MIRAGE_IS_FRAGMENT_SNDFILE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_FRAGMENT_SNDFILE))
#define MIRAGE_FRAGMENT_SNDFILE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_FRAGMENT_SNDFILE, MirageFragmentSndfileClass))

typedef struct _MirageFragmentSndfile         MirageFragmentSndfile;
typedef struct _MirageFragmentSndfileClass    MirageFragmentSndfileClass;
typedef struct _MirageFragmentSndfilePrivate  MirageFragmentSndfilePrivate;

struct _MirageFragmentSndfile
{
    MirageFragment parent_instance;

    /*< private >*/
    MirageFragmentSndfilePrivate *priv;
};

struct _MirageFragmentSndfileClass
{
    MirageFragmentClass parent_class;
};

/* Used by MIRAGE_TYPE_FRAGMENT_SNDFILE */
GType mirage_fragment_sndfile_get_type (void);
void mirage_fragment_sndfile_type_register (GTypeModule *type_module);

G_END_DECLS

#endif /* __MIRAGE_FRAGMENT_SNDFILE_FRAGMENT_H__ */
