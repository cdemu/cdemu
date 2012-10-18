/*
 *  libMirage: Fragment object
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

#ifndef __MIRAGE_FRAGMENT_H__
#define __MIRAGE_FRAGMENT_H__


G_BEGIN_DECLS

/**
 * MirageFragmentInfo:
 * @id: fragment ID
 * @name: fragment name
 *
 * A structure containing fragment information. It can be obtained with call to
 * mirage_fragment_get_info().
 */
typedef struct _MirageFragmentInfo MirageFragmentInfo;
struct _MirageFragmentInfo
{
    gchar id[32];
    gchar name[32];
};


/**********************************************************************\
 *                        MirageFragment object                       *
\**********************************************************************/
#define MIRAGE_TYPE_FRAGMENT            (mirage_fragment_get_type())
#define MIRAGE_FRAGMENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FRAGMENT, MirageFragment))
#define MIRAGE_FRAGMENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_FRAGMENT, MirageFragmentClass))
#define MIRAGE_IS_FRAGMENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FRAGMENT))
#define MIRAGE_IS_FRAGMENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_FRAGMENT))
#define MIRAGE_FRAGMENT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_FRAGMENT, MirageFragmentClass))

typedef struct _MirageFragment         MirageFragment;
typedef struct _MirageFragmentClass    MirageFragmentClass;
typedef struct _MirageFragmentPrivate  MirageFragmentPrivate;

/**
 * MirageFragment:
 *
 * All the fields in the <structname>MirageFragment</structname>
 * structure are private to the #MirageFragment implementation and
 * should never be accessed directly.
 */
struct _MirageFragment
{
    MirageObject parent_instance;

    /*< private >*/
    MirageFragmentPrivate *priv;
};

/**
 * MirageFragmentClass:
 *
 * @parent_class: the parent class
 * @can_handle_data_format: checks whether fragment can handle data stored in stream
 * @use_the_rest_of_file: uses the rest of data file
 * @read_main_data: reads main channel data for sector at specified address
 * @read_subchannel_data: reads subchannel data for sector at specified address
 * @signal_layout_changed: "layout-changed" signal identifier
 *
 * The class structure for the <structname>MirageFragment</structname> type.
 */
struct _MirageFragmentClass
{
    MirageObjectClass parent_class;

    /* Class members */
    gboolean (*can_handle_data_format) (MirageFragment *self, GInputStream *stream, GError **error);

    gboolean (*use_the_rest_of_file) (MirageFragment *self, GError **error);

    gboolean (*read_main_data) (MirageFragment *self, gint address, guint8 **buffer, gint *length, GError **error);
    gboolean (*read_subchannel_data) (MirageFragment *self, gint address, guint8 **buffer, gint *length, GError **error);

    /* Signals */
    gint signal_layout_changed;
};

/* Used by MIRAGE_TYPE_FRAGMENT */
GType mirage_fragment_get_type (void);


void mirage_fragment_generate_info (MirageFragment *self, const gchar *id, const gchar *name);
const MirageFragmentInfo *mirage_fragment_get_info (MirageFragment *self);

gboolean mirage_fragment_can_handle_data_format (MirageFragment *self, GInputStream *stream, GError **error);

void mirage_fragment_set_address (MirageFragment *self, gint address);
gint mirage_fragment_get_address (MirageFragment *self);

void mirage_fragment_set_length (MirageFragment *self, gint length);
gint mirage_fragment_get_length (MirageFragment *self);

gboolean mirage_fragment_use_the_rest_of_file (MirageFragment *self, GError **error);

gboolean mirage_fragment_read_main_data (MirageFragment *self, gint address, guint8 **buffer, gint *length, GError **error);
gboolean mirage_fragment_read_subchannel_data (MirageFragment *self, gint address, guint8 **buffer, gint *length, GError **error);

G_END_DECLS

#endif /* __MIRAGE_FRAGMENT_H__ */
