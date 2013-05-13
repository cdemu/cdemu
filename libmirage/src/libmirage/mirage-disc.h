/*
 *  libMirage: Disc object
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __MIRAGE_DISC_H__
#define __MIRAGE_DISC_H__

#include "mirage-types.h"


G_BEGIN_DECLS

/**
 * MIRAGE_MCN_SIZE:
 *
 * Length of MCN string.
 */
#define MIRAGE_MCN_SIZE 13

/**
 * MirageMediumTypes:
 * @MIRAGE_MEDIUM_CD: CD disc
 * @MIRAGE_MEDIUM_DVD: DVD disc
 * @MIRAGE_MEDIUM_BD: BD (Blue-Ray) disc
 * @MIRAGE_MEDIUM_HD: HD-DVD disc
 * @MIRAGE_MEDIUM_HDD: Hard-disk
 *
 * Medium types.
 */
typedef enum _MirageMediumTypes
{
    MIRAGE_MEDIUM_CD  = 0x01,
    MIRAGE_MEDIUM_DVD = 0x02,
    MIRAGE_MEDIUM_BD  = 0x03,
    MIRAGE_MEDIUM_HD  = 0x04,
    MIRAGE_MEDIUM_HDD = 0x05
} MirageMediumTypes;


/**
 * MirageEnumSessionCallback:
 * @session: (in): session
 * @user_data: (in) (closure): user data passed to enumeration function
 *
 * Callback function type used with mirage_disc_enumerate_sessions().
 * A pointer to a session object is stored in @session, without incrementing
 * its reference counter. @user_data is user data passed to enumeration function.
 *
 * Returns: %TRUE on success, otherwise %FALSE
 */
typedef gboolean (*MirageEnumSessionCallback) (MirageSession *session, gpointer user_data);


/**********************************************************************\
 *                          MirageDisc object                         *
\**********************************************************************/
#define MIRAGE_TYPE_DISC            (mirage_disc_get_type())
#define MIRAGE_DISC(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_DISC, MirageDisc))
#define MIRAGE_DISC_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_DISC, MirageDiscClass))
#define MIRAGE_IS_DISC(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_DISC))
#define MIRAGE_IS_DISC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_DISC))
#define MIRAGE_DISC_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_DISC, MirageDiscClass))

typedef struct _MirageDiscClass    MirageDiscClass;
typedef struct _MirageDiscPrivate  MirageDiscPrivate;


/**
 * MirageDisc:
 *
 * All the fields in the <structname>MirageDisc</structname>
 * structure are private to the #MirageDisc implementation and
 * should never be accessed directly.
 */
struct _MirageDisc
{
    MirageObject parent_instance;

    /*< private >*/
    MirageDiscPrivate *priv;
};

/**
 * MirageDiscClass:
 * @parent_class: the parent class
 *
 * The class structure for the <structname>MirageDisc</structname> type.
 */
struct _MirageDiscClass
{
    MirageObjectClass parent_class;
};

/* Used by MIRAGE_TYPE_DISC */
GType mirage_disc_get_type (void);

/* Medium type */
void mirage_disc_set_medium_type (MirageDisc *self, MirageMediumTypes medium_type);
MirageMediumTypes mirage_disc_get_medium_type (MirageDisc *self);

/* Filename */
void mirage_disc_set_filenames (MirageDisc *self, const gchar **filenames);
void mirage_disc_set_filename (MirageDisc *self, const gchar *filename);
const gchar **mirage_disc_get_filenames (MirageDisc *self);

/* MCN */
void mirage_disc_set_mcn (MirageDisc *self, const gchar *mcn);
const gchar *mirage_disc_get_mcn (MirageDisc *self);

/* Layout */
void mirage_disc_layout_set_first_session (MirageDisc *self, gint first_session);
gint mirage_disc_layout_get_first_session (MirageDisc *self);
void mirage_disc_layout_set_first_track (MirageDisc *self, gint first_track);
gint mirage_disc_layout_get_first_track (MirageDisc *self);
void mirage_disc_layout_set_start_sector (MirageDisc *self, gint start_sector);
gint mirage_disc_layout_get_start_sector (MirageDisc *self);
gint mirage_disc_layout_get_length (MirageDisc *self);

/* Session handling */
gint mirage_disc_get_number_of_sessions (MirageDisc *self);
void mirage_disc_add_session_by_index (MirageDisc *self, gint index, MirageSession *session);
gboolean mirage_disc_add_session_by_number (MirageDisc *self, gint number, MirageSession *session, GError **error);
gboolean mirage_disc_remove_session_by_index (MirageDisc *self, gint index, GError **error);
gboolean mirage_disc_remove_session_by_number (MirageDisc *self, gint number, GError **error);
void mirage_disc_remove_session_by_object (MirageDisc *self, MirageSession *session);
MirageSession *mirage_disc_get_session_by_index (MirageDisc *self, gint index, GError **error);
MirageSession *mirage_disc_get_session_by_number (MirageDisc *self, gint number, GError **error);
MirageSession *mirage_disc_get_session_by_address (MirageDisc *self, gint address, GError **error);
MirageSession *mirage_disc_get_session_by_track (MirageDisc *self, gint track, GError **error);
gboolean mirage_disc_enumerate_sessions (MirageDisc *self, MirageEnumSessionCallback func, gpointer user_data);
MirageSession *mirage_disc_get_session_before (MirageDisc *self, MirageSession *session, GError **error);
MirageSession *mirage_disc_get_session_after (MirageDisc *self, MirageSession *session, GError **error);

/* Track handling */
gint mirage_disc_get_number_of_tracks (MirageDisc *self);
gboolean mirage_disc_add_track_by_index (MirageDisc *self, gint index, MirageTrack *track, GError **error);
gboolean mirage_disc_add_track_by_number (MirageDisc *self, gint number, MirageTrack *track, GError **error);
gboolean mirage_disc_remove_track_by_index (MirageDisc *self, gint index, GError **error);
gboolean mirage_disc_remove_track_by_number (MirageDisc *self, gint number, GError **error);
MirageTrack *mirage_disc_get_track_by_index (MirageDisc *self, gint index, GError **error);
MirageTrack *mirage_disc_get_track_by_number (MirageDisc *self, gint number, GError **error);
MirageTrack *mirage_disc_get_track_by_address (MirageDisc *self, gint address, GError **error);

/* Disc structures */
void mirage_disc_set_disc_structure (MirageDisc *self, gint layer, gint type, const guint8 *data, gint len);
gboolean mirage_disc_get_disc_structure (MirageDisc *self, gint layer, gint type, const guint8 **data, gint *len, GError **error);

/* Direct sector access */
MirageSector *mirage_disc_get_sector (MirageDisc *self, gint address, GError **error);

/* DPM */
void mirage_disc_set_dpm_data (MirageDisc *self, gint start, gint resolution, gint num_entries, const guint32 *data);
void mirage_disc_get_dpm_data (MirageDisc *self, gint *start, gint *resolution, gint *num_entries, const guint32 **data);
gboolean mirage_disc_get_dpm_data_for_sector (MirageDisc *self, gint address, gdouble *angle, gdouble *density, GError **error);

G_END_DECLS

#endif /* __MIRAGE_DISC_H__ */
