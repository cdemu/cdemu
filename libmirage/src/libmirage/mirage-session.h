/*
 *  libMirage: Session object
 *  Copyright (C) 2006-2008 Rok Mandeljc
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
 
#ifndef __MIRAGE_SESSION_H__
#define __MIRAGE_SESSION_H__


G_BEGIN_DECLS

/**
 * MIRAGE_SessionTypes:
 * @MIRAGE_SESSION_CD_DA: CD AUDIO
 * @MIRAGE_SESSION_CD_ROM: CD-ROM
 * @MIRAGE_SESSION_CD_I: CD-I
 * @MIRAGE_SESSION_CD_ROM_XA: CD-ROM XA
 *
 * <para>
 * Session types
 * </para>
 **/
typedef enum {
    MIRAGE_SESSION_CD_DA     = 0x00,
    MIRAGE_SESSION_CD_ROM    = 0x00,
    MIRAGE_SESSION_CD_I      = 0x10,
    MIRAGE_SESSION_CD_ROM_XA = 0x20,
} MIRAGE_SessionTypes;


#define MIRAGE_TYPE_SESSION            (mirage_session_get_type())
#define MIRAGE_SESSION(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_SESSION, MIRAGE_Session))
#define MIRAGE_SESSION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_SESSION, MIRAGE_SessionClass))
#define MIRAGE_IS_SESSION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_SESSION))
#define MIRAGE_IS_SESSION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_SESSION))
#define MIRAGE_SESSION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_SESSION, MIRAGE_SessionClass))

/**
 * MIRAGE_Session:
 *
 * <para>
 * Contains private data only, and should be accessed using the functions below.
 * </para>
 **/
typedef struct {
    MIRAGE_Object parent;
} MIRAGE_Session;

typedef struct {
    MIRAGE_ObjectClass parent;
} MIRAGE_SessionClass;

/* Used by MIRAGE_TYPE_SESSION */
GType mirage_session_get_type (void);


/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
/* Session type */
gboolean mirage_session_set_session_type (MIRAGE_Session *self, gint type, GError **error);
gboolean mirage_session_get_session_type (MIRAGE_Session *self, gint *type, GError **error);

/* Layout */
gboolean mirage_session_layout_set_session_number (MIRAGE_Session *self, gint number, GError **error);
gboolean mirage_session_layout_get_session_number (MIRAGE_Session *self, gint *number, GError **error);
gboolean mirage_session_layout_set_first_track (MIRAGE_Session *self, gint first_track, GError **error);
gboolean mirage_session_layout_get_first_track (MIRAGE_Session *self, gint *first_track, GError **error);
gboolean mirage_session_layout_set_start_sector (MIRAGE_Session *self, gint start_sector, GError **error);
gboolean mirage_session_layout_get_start_sector (MIRAGE_Session *self, gint *start_sector, GError **error);
gboolean mirage_session_layout_get_length (MIRAGE_Session *self, gint *length, GError **error);

/* Convenience functions for setting/getting length of session's lead-out */
gboolean mirage_session_set_leadout_length (MIRAGE_Session *self, gint length, GError **error);
gboolean mirage_session_get_leadout_length (MIRAGE_Session *self, gint *length, GError **error);

/* Tracks handling */
gboolean mirage_session_get_number_of_tracks (MIRAGE_Session *self, gint *number_of_tracks, GError **error);
gboolean mirage_session_add_track_by_index (MIRAGE_Session *self, gint index, GObject **track, GError **error);
gboolean mirage_session_add_track_by_number (MIRAGE_Session *self, gint number, GObject **track, GError **error);
gboolean mirage_session_remove_track_by_index (MIRAGE_Session *self, gint index, GError **error);
gboolean mirage_session_remove_track_by_number (MIRAGE_Session *self, gint number, GError **error);
gboolean mirage_session_remove_track_by_object (MIRAGE_Session *self, GObject *track, GError **error);
gboolean mirage_session_get_track_by_index (MIRAGE_Session *self, gint index, GObject **track, GError **error);
gboolean mirage_session_get_track_by_number (MIRAGE_Session *self, gint number, GObject **track, GError **error);
gboolean mirage_session_get_track_by_address (MIRAGE_Session *self, gint address, GObject **track, GError **error);
gboolean mirage_session_for_each_track (MIRAGE_Session *self, MIRAGE_CallbackFunction func, gpointer user_data, GError **error);
gboolean mirage_session_get_track_before (MIRAGE_Session *self, GObject *cur_track, GObject **prev_track, GError **error);
gboolean mirage_session_get_track_after (MIRAGE_Session *self, GObject *cur_track, GObject **next_track, GError **error);

/* Languages (CD-Text) handling */
gboolean mirage_session_get_number_of_languages (MIRAGE_Session *self, gint *number_of_languages, GError **error);
gboolean mirage_session_add_language (MIRAGE_Session *self, gint langcode, GObject **language, GError **error);
gboolean mirage_session_remove_language_by_index (MIRAGE_Session *self, gint index, GError **error);
gboolean mirage_session_remove_language_by_code (MIRAGE_Session *self, gint langcode, GError **error);
gboolean mirage_session_remove_language_by_object (MIRAGE_Session *self, GObject *language, GError **error);
gboolean mirage_session_get_language_by_index (MIRAGE_Session *self, gint index, GObject **language, GError **error);
gboolean mirage_session_get_language_by_code (MIRAGE_Session *self, gint langcode, GObject **language, GError **error);
gboolean mirage_session_for_each_language (MIRAGE_Session *self, MIRAGE_CallbackFunction func, gpointer user_data, GError **error);

/* Direct CD-Text handling */
gboolean mirage_session_set_cdtext_data (MIRAGE_Session *self, guint8 *data, gint len, GError **error);
gboolean mirage_session_get_cdtext_data (MIRAGE_Session *self, guint8 **data, gint *len, GError **error);

/* Two nice convenience functions */
gboolean mirage_session_get_prev (MIRAGE_Session *self, GObject **prev_session, GError **error);
gboolean mirage_session_get_next (MIRAGE_Session *self, GObject **next_session, GError **error);

G_END_DECLS

#endif /* __MIRAGE_SESSION_H__ */
