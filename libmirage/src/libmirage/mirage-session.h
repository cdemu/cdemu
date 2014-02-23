/*
 *  libMirage: Session object
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

#ifndef __MIRAGE_SESSION_H__
#define __MIRAGE_SESSION_H__

#include "mirage-types.h"


G_BEGIN_DECLS

/**
 * MIRAGE_MCN_SIZE:
 *
 * Length of MCN string.
 */
#define MIRAGE_MCN_SIZE 13

/**
 * MirageSessionType:
 * @MIRAGE_SESSION_CDDA: CD AUDIO
 * @MIRAGE_SESSION_CDROM: CD-ROM
 * @MIRAGE_SESSION_CDI: CD-I
 * @MIRAGE_SESSION_CDROM_XA: CD-ROM XA
 *
 * Session type.
 */
typedef enum _MirageSessionType
{
    MIRAGE_SESSION_CDDA,
    MIRAGE_SESSION_CDROM,
    MIRAGE_SESSION_CDI,
    MIRAGE_SESSION_CDROM_XA,
} MirageSessionType;


/**
 * MirageEnumTrackCallback:
 * @track: (in): track
 * @user_data: (in) (closure): user data passed to enumeration function
 *
 * Callback function type used with mirage_session_enumerate_tracks().
 * A pointer to a track object is stored in @track, without incrementing
 * its reference counter. @user_data is user data passed to enumeration function.
 *
 * Returns: %TRUE on success, otherwise %FALSE
 */
typedef gboolean (*MirageEnumTrackCallback) (MirageTrack *track, gpointer user_data);

/**
 * MirageEnumLanguageCallback:
 * @language: (in): language
 * @user_data: (in) (closure): user data passed to enumeration function
 *
 * Callback function type used with mirage_session_enumerate_languages() and
 * mirage_track_enumerate_languages(). A pointer to a language object is
 * stored in @language, without incrementing its reference counter.
 * @user_data is user data passed to enumeration function.
 *
 * Returns: %TRUE on success, otherwise %FALSE
 */
typedef gboolean (*MirageEnumLanguageCallback) (MirageLanguage *language, gpointer user_data);


/**********************************************************************\
 *                         MirageSession object                       *
\**********************************************************************/
#define MIRAGE_TYPE_SESSION            (mirage_session_get_type())
#define MIRAGE_SESSION(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_SESSION, MirageSession))
#define MIRAGE_SESSION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_SESSION, MirageSessionClass))
#define MIRAGE_IS_SESSION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_SESSION))
#define MIRAGE_IS_SESSION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_SESSION))
#define MIRAGE_SESSION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_SESSION, MirageSessionClass))

typedef struct _MirageSessionClass     MirageSessionClass;
typedef struct _MirageSessionPrivate   MirageSessionPrivate;

/**
 * MirageSession:
 *
 * All the fields in the <structname>MirageSession</structname>
 * structure are private to the #MirageSession implementation and
 * should never be accessed directly.
 */
struct _MirageSession
{
    MirageObject parent_instance;

    /*< private >*/
    MirageSessionPrivate *priv;
};

/**
 * MirageSessionClass:
 * @parent_class: the parent class
 *
 * The class structure for the <structname>MirageSession</structname> type.
 */
struct _MirageSessionClass
{
    MirageObjectClass parent_class;
};

/* Used by MIRAGE_TYPE_SESSION */
GType mirage_session_get_type (void);

/* Session type */
void mirage_session_set_session_type (MirageSession *self, MirageSessionType type);
MirageSessionType mirage_session_get_session_type (MirageSession *self);

/* MCN */
void mirage_session_set_mcn (MirageSession *self, const gchar *mcn);
const gchar *mirage_session_get_mcn (MirageSession *self);

/* Layout */
void mirage_session_layout_set_session_number (MirageSession *self, gint number);
gint mirage_session_layout_get_session_number (MirageSession *self);
void mirage_session_layout_set_first_track (MirageSession *self, gint first_track);
gint mirage_session_layout_get_first_track (MirageSession *self);
void mirage_session_layout_set_start_sector (MirageSession *self, gint start_sector);
gint mirage_session_layout_get_start_sector (MirageSession *self);
gint mirage_session_layout_get_length (MirageSession *self);

gboolean mirage_session_layout_contains_address (MirageSession *self, gint address);

/* Convenience functions for setting/getting length of session's lead-out */
void mirage_session_set_leadout_length (MirageSession *self, gint length);
gint mirage_session_get_leadout_length (MirageSession *self);

/* Tracks handling */
gint mirage_session_get_number_of_tracks (MirageSession *self);
void mirage_session_add_track_by_index (MirageSession *self, gint index, MirageTrack *track);
gboolean mirage_session_add_track_by_number (MirageSession *self, gint number, MirageTrack *track, GError **error);
gboolean mirage_session_remove_track_by_index (MirageSession *self, gint index, GError **error);
gboolean mirage_session_remove_track_by_number (MirageSession *self, gint number, GError **error);
void mirage_session_remove_track_by_object (MirageSession *self, MirageTrack *track);
MirageTrack *mirage_session_get_track_by_index (MirageSession *self, gint index, GError **error);
MirageTrack *mirage_session_get_track_by_number (MirageSession *self, gint number, GError **error);
MirageTrack *mirage_session_get_track_by_address (MirageSession *self, gint address, GError **error);
gboolean mirage_session_enumerate_tracks (MirageSession *self, MirageEnumTrackCallback func, gpointer user_data);
MirageTrack *mirage_session_get_track_before (MirageSession *self, MirageTrack *track, GError **error);
MirageTrack *mirage_session_get_track_after (MirageSession *self, MirageTrack *track, GError **error);

/* Languages (CD-Text) handling */
gint mirage_session_get_number_of_languages (MirageSession *self);
gboolean mirage_session_add_language (MirageSession *self, gint code, MirageLanguage *language, GError **error);
gboolean mirage_session_remove_language_by_index (MirageSession *self, gint index, GError **error);
gboolean mirage_session_remove_language_by_code (MirageSession *self, gint code, GError **error);
void mirage_session_remove_language_by_object (MirageSession *self, MirageLanguage *language);
MirageLanguage *mirage_session_get_language_by_index (MirageSession *self, gint index, GError **error);
MirageLanguage *mirage_session_get_language_by_code (MirageSession *self, gint code, GError **error);
gboolean mirage_session_enumerate_languages (MirageSession *self, MirageEnumLanguageCallback func, gpointer user_data);

/* Direct CD-Text handling */
gboolean mirage_session_set_cdtext_data (MirageSession *self, guint8 *data, gint len, GError **error);
gboolean mirage_session_get_cdtext_data (MirageSession *self, guint8 **data, gint *len, GError **error);

/* Two nice convenience functions */
MirageSession *mirage_session_get_prev (MirageSession *self, GError **error);
MirageSession *mirage_session_get_next (MirageSession *self, GError **error);

G_END_DECLS

#endif /* __MIRAGE_SESSION_H__ */
