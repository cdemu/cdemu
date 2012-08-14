/*
 *  libMirage: Track object
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

#ifndef __MIRAGE_TRACK_H__
#define __MIRAGE_TRACK_H__


G_BEGIN_DECLS

/**
 * MIRAGE_ISRC_SIZE:
 *
 * <para>
 * Length of ISRC string.
 * </para>
 **/
#define MIRAGE_ISRC_SIZE 12

/**
 * MIRAGE_TrackFlags:
 * @MIRAGE_TRACKF_FOURCHANNEL: four channel audio
 * @MIRAGE_TRACKF_COPYPERMITTED: copy permitted
 * @MIRAGE_TRACKF_PREEMPHASIS: pre-emphasis
 *
 * <para>
 * Track flags.
 * </para>
 **/
typedef enum
{
    MIRAGE_TRACKF_FOURCHANNEL   = 0x01,
    MIRAGE_TRACKF_COPYPERMITTED = 0x02,
    MIRAGE_TRACKF_PREEMPHASIS   = 0x04,
} MIRAGE_TrackFlags;

/**
 * MIRAGE_TrackModes:
 * @MIRAGE_MODE_MODE0: Mode 0
 * @MIRAGE_MODE_AUDIO: Audio
 * @MIRAGE_MODE_MODE1: Mode 1
 * @MIRAGE_MODE_MODE2: Mode 2 Formless
 * @MIRAGE_MODE_MODE2_FORM1: Mode 2 Form 1
 * @MIRAGE_MODE_MODE2_FORM2: Mode 2 Form 2
 * @MIRAGE_MODE_MODE2_MIXED: Mode 2 Mixed
 *
 * <para>
 * Track modes.
 * </para>
 **/
typedef enum
{
    MIRAGE_MODE_MODE0       = 0x00,
    MIRAGE_MODE_AUDIO       = 0x01,
    MIRAGE_MODE_MODE1       = 0x02,
    MIRAGE_MODE_MODE2       = 0x03,
    MIRAGE_MODE_MODE2_FORM1 = 0x04,
    MIRAGE_MODE_MODE2_FORM2 = 0x05,
    MIRAGE_MODE_MODE2_MIXED = 0x06,
} MIRAGE_TrackModes;

/**
 * MIRAGE_TrackConstants:
 * @MIRAGE_TRACK_LEADIN: Lead-in track
 * @MIRAGE_TRACK_LEADOUT: Lead-out track
 *
 * <para>
 * Track constants.
 * </para>
 **/
typedef enum
{
    MIRAGE_TRACK_LEADIN  = 0x00,
    MIRAGE_TRACK_LEADOUT = 0xAA,
} MIRAGE_TrackConstants;


#define MIRAGE_TYPE_TRACK            (mirage_track_get_type())
#define MIRAGE_TRACK(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_TRACK, MIRAGE_Track))
#define MIRAGE_TRACK_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_TRACK, MIRAGE_TrackClass))
#define MIRAGE_IS_TRACK(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_TRACK))
#define MIRAGE_IS_TRACK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_TRACK))
#define MIRAGE_TRACK_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_TRACK, MIRAGE_TrackClass))

typedef struct _MIRAGE_Track        MIRAGE_Track;
typedef struct _MIRAGE_TrackClass   MIRAGE_TrackClass;
typedef struct _MIRAGE_TrackPrivate MIRAGE_TrackPrivate;

/**
 * MIRAGE_Track:
 *
 * <para>
 * Contains private data only, and should be accessed using the functions below.
 * </para>
 **/
struct _MIRAGE_Track
{
    MIRAGE_Object parent_instance;

    /*< private >*/
    MIRAGE_TrackPrivate *priv;
};

struct _MIRAGE_TrackClass
{
    MIRAGE_ObjectClass parent_class;
};

/* Used by MIRAGE_TYPE_TRACK */
GType mirage_track_get_type (void);


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
/* Track flags */
void mirage_track_set_flags (MIRAGE_Track *self, gint flags);
gint mirage_track_get_flags (MIRAGE_Track *self);

/* Track mode */
void mirage_track_set_mode (MIRAGE_Track *self, gint mode);
gint mirage_track_get_mode (MIRAGE_Track *self);

/* Adr/Ctl */
gint mirage_track_get_adr (MIRAGE_Track *self);

void mirage_track_set_ctl (MIRAGE_Track *self, gint ctl);
gint mirage_track_get_ctl (MIRAGE_Track *self);

/* ISRC */
void mirage_track_set_isrc (MIRAGE_Track *self, const gchar *isrc);
const gchar * mirage_track_get_isrc (MIRAGE_Track *self);

/* Read and get sector */
GObject *mirage_track_get_sector (MIRAGE_Track *self, gint address, gboolean abs, GError **error);
gboolean mirage_track_read_sector (MIRAGE_Track *self, gint address, gboolean abs, guint8 main_sel, guint8 subc_sel, guint8 *ret_buf, gint *ret_len, GError **error);

/* Layout */
gint mirage_track_layout_get_session_number (MIRAGE_Track *self);
void mirage_track_layout_set_track_number (MIRAGE_Track *self, gint track_number);
gint mirage_track_layout_get_track_number (MIRAGE_Track *self);
void mirage_track_layout_set_start_sector (MIRAGE_Track *self, gint start_sector);
gint mirage_track_layout_get_start_sector (MIRAGE_Track *self);
gint mirage_track_layout_get_length (MIRAGE_Track *self);

/* Data fragments handling */
gint mirage_track_get_number_of_fragments (MIRAGE_Track *self);
void mirage_track_add_fragment (MIRAGE_Track *self, gint index, GObject *fragment);
gboolean mirage_track_remove_fragment_by_index (MIRAGE_Track *self, gint index, GError **error);
void mirage_track_remove_fragment_by_object (MIRAGE_Track *self, GObject *fragment);
gboolean mirage_track_get_fragment_by_index (MIRAGE_Track *self, gint index, GObject **fragment, GError **error);
gboolean mirage_track_get_fragment_by_address (MIRAGE_Track *self, gint address, GObject **fragment, GError **error);
gboolean mirage_track_for_each_fragment (MIRAGE_Track *self, MIRAGE_CallbackFunction func, gpointer user_data, GError **error);

gboolean mirage_track_find_fragment_with_subchannel (MIRAGE_Track *self, GObject **fragment, GError **error);

/* Track start */
void mirage_track_set_track_start (MIRAGE_Track *self, gint track_start);
gint mirage_track_get_track_start (MIRAGE_Track *self);

/* Indices handling */
gint mirage_track_get_number_of_indices (MIRAGE_Track *self);
gboolean mirage_track_add_index (MIRAGE_Track *self, gint address, GObject **index, GError **error);
gboolean mirage_track_remove_index_by_number (MIRAGE_Track *self, gint number, GError **error);
void mirage_track_remove_index_by_object (MIRAGE_Track *self, GObject *index);
gboolean mirage_track_get_index_by_number (MIRAGE_Track *self, gint number, GObject **index, GError **error);
gboolean mirage_track_get_index_by_address (MIRAGE_Track *self, gint address, GObject **index, GError **error);
gboolean mirage_track_for_each_index (MIRAGE_Track *self, MIRAGE_CallbackFunction func, gpointer user_data, GError **error);

/* Languages (CD-Text) handling */
gint mirage_track_get_number_of_languages (MIRAGE_Track *self);
gboolean mirage_track_add_language (MIRAGE_Track *self, gint langcode, GObject **language, GError **error);
gboolean mirage_track_remove_language_by_index (MIRAGE_Track *self, gint index, GError **error);
gboolean mirage_track_remove_language_by_code (MIRAGE_Track *self, gint langcode, GError **error);
void mirage_track_remove_language_by_object (MIRAGE_Track *self, GObject *language);
gboolean mirage_track_get_language_by_index (MIRAGE_Track *self, gint index, GObject **language, GError **error);
gboolean mirage_track_get_language_by_code (MIRAGE_Track *self, gint langcode, GObject **language, GError **error);
gboolean mirage_track_for_each_language (MIRAGE_Track *self, MIRAGE_CallbackFunction func, gpointer user_data, GError **error);

/* Two nice convenience functions */
gboolean mirage_track_get_prev (MIRAGE_Track *self, GObject **prev_track, GError **error);
gboolean mirage_track_get_next (MIRAGE_Track *self, GObject **next_track, GError **error);

G_END_DECLS

#endif /* __MIRAGE_TRACK_H__ */
