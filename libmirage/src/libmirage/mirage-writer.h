/*
 *  libMirage: Writer object
 *  Copyright (C) 2014 Rok Mandeljc
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

#ifndef __MIRAGE_WRITER_H__
#define __MIRAGE_WRITER_H__

#include "mirage-types.h"


G_BEGIN_DECLS


/**
 * MirageFragmentRole:
 * @MIRAGE_FRAGMENT_PREGAP: pregap fragment
 * @MIRAGE_FRAGMENT_DATA: data fragment
 *
 * Fragment roles.
 */
typedef enum _MirageFragmentRole
{
    MIRAGE_FRAGMENT_PREGAP,
    MIRAGE_FRAGMENT_DATA,
} MirageFragmentRole;


/**
 * MirageWriterInfo:
 * @id: writer ID
 * @name: writer name
 *
 * A structure containing writer information. It can be obtained with call to
 * mirage_writer_get_info().
 */
typedef struct _MirageWriterInfo MirageWriterInfo;
struct _MirageWriterInfo
{
    gchar *id;
    gchar *name;
};

void mirage_writer_info_copy (const MirageWriterInfo *info, MirageWriterInfo *dest);
void mirage_writer_info_free (MirageWriterInfo *info);


/**
 * MirageWriterParameter:
 * @name: parameter name
 * @description: description of parameter
 * @default_value: default value for parameter. Also determines parameter type.
 * @enum_values: if parameter is an enum, this field contains all possible values. The variant has a signature "as".
 *
 * A structure encapsulating information about image writer parameters,
 * using in writer's parameter sheet.
 */
typedef struct _MirageWriterParameter MirageWriterParameter;
struct _MirageWriterParameter
{
    gchar *name;
    gchar *description;

    GVariant *default_value;

    GVariant *enum_values;
};


/**********************************************************************\
 *                         MirageWriter object                        *
\**********************************************************************/
#define MIRAGE_TYPE_WRITER            (mirage_writer_get_type())
#define MIRAGE_WRITER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_WRITER, MirageWriter))
#define MIRAGE_WRITER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_WRITER, MirageWriterClass))
#define MIRAGE_IS_WRITER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_WRITER))
#define MIRAGE_IS_WRITER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_WRITER))
#define MIRAGE_WRITER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_WRITER, MirageWriterClass))

typedef struct _MirageWriter           MirageWriter;
typedef struct _MirageWriterClass      MirageWriterClass;
typedef struct _MirageWriterPrivate    MirageWriterPrivate;

/**
 * MirageWriter:
 *
 * All the fields in the <structname>MirageWriter</structname>
 * structure are private to the #MirageWriter implementation and
 * should never be accessed directly.
 */
struct _MirageWriter
{
    MirageObject parent_instance;

    /*< private >*/
    MirageWriterPrivate *priv;
} ;

/**
 * MirageWriterClass:
 * @parent_class: the parent class
 * @open_image: initializes writer on specified image
 * @create_fragment: creates a fragment of specified role for the given track
 * @finalize_image: finalizes image
 *
 * The class structure for the <structname>MirageWriter</structname> type.
 */
struct _MirageWriterClass
{
    MirageObjectClass parent_class;

    /* Class members */
    gboolean (*open_image) (MirageWriter *self, MirageDisc *disc, GError **error);
    MirageFragment *(*create_fragment) (MirageWriter *self, MirageTrack *track, MirageFragmentRole role, GError **error);
    gboolean (*finalize_image) (MirageWriter *self, MirageDisc *disc, GError **error);
};

/* Used by MIRAGE_TYPE_WRITER */
GType mirage_writer_get_type (void);

void mirage_writer_generate_info (MirageWriter *self, const gchar *id, const gchar *name);
const MirageWriterInfo *mirage_writer_get_info (MirageWriter *self);

void mirage_writer_add_parameter_boolean (MirageWriter *self, const gchar *id, const gchar *name, const gchar *description, gboolean default_value);
void mirage_writer_add_parameter_string (MirageWriter *self, const gchar *id, const gchar *name, const gchar *description, const gchar *default_value);
void mirage_writer_add_parameter_int (MirageWriter *self, const gchar *id, const gchar *name, const gchar *description, gint default_value);
void mirage_writer_add_parameter_enum (MirageWriter *self, const gchar *id, const gchar *name, const gchar *description, const gchar *default_value, ...);

GList *mirage_writer_lookup_parameter_ids (MirageWriter *self);
const MirageWriterParameter *mirage_writer_lookup_parameter_info (MirageWriter *self, const gchar *id);

gboolean mirage_writer_get_parameter_boolean (MirageWriter *self, const gchar *id);
const gchar *mirage_writer_get_parameter_string (MirageWriter *self, const gchar *id);
gint mirage_writer_get_parameter_int (MirageWriter *self, const gchar *id);
const gchar *mirage_writer_get_parameter_enum (MirageWriter *self, const gchar *id);

gboolean mirage_writer_open_image (MirageWriter *self, MirageDisc *disc, GHashTable *parameters, GError **error);
MirageFragment *mirage_writer_create_fragment (MirageWriter *self, MirageTrack *track, MirageFragmentRole role, GError **error);
gboolean mirage_writer_finalize_image (MirageWriter *self, MirageDisc *disc, GError **error);

guint mirage_writer_get_conversion_progress_step (MirageWriter *self);
void mirage_writer_set_conversion_progress_step (MirageWriter *self, guint step);
gboolean mirage_writer_convert_image (MirageWriter *self, const gchar *filename, MirageDisc *original_disc, GHashTable *parameters, GCancellable *cancellable, GError **error);

G_END_DECLS

#endif /* __MIRAGE_WRITER_H__ */
