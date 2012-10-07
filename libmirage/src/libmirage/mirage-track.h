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

/* Forward declarations */
typedef struct _MirageFragment MirageFragment;
typedef struct _MirageIndex MirageIndex;
typedef struct _MirageLanguage MirageLanguage;
typedef struct _MirageSector MirageSector;


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
 * MirageTrackFlags:
 * @MIRAGE_TRACK_FLAG_FOURCHANNEL: four channel audio
 * @MIRAGE_TRACK_FLAG_COPYPERMITTED: copy permitted
 * @MIRAGE_TRACK_FLAG_PREEMPHASIS: pre-emphasis
 *
 * <para>
 * Track flags.
 * </para>
 **/
typedef enum _MirageTrackFlags
{
    MIRAGE_TRACK_FLAG_FOURCHANNEL   = 0x01,
    MIRAGE_TRACK_FLAG_COPYPERMITTED = 0x02,
    MIRAGE_TRACK_FLAG_PREEMPHASIS   = 0x04,
} MirageTrackFlags;

/**
 * MirageTrackModes:
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
typedef enum _MirageTrackModes
{
    MIRAGE_MODE_MODE0       = 0x00,
    MIRAGE_MODE_AUDIO       = 0x01,
    MIRAGE_MODE_MODE1       = 0x02,
    MIRAGE_MODE_MODE2       = 0x03,
    MIRAGE_MODE_MODE2_FORM1 = 0x04,
    MIRAGE_MODE_MODE2_FORM2 = 0x05,
    MIRAGE_MODE_MODE2_MIXED = 0x06,
} MirageTrackModes;

/**
 * MirageTrackConstants:
 * @MIRAGE_TRACK_LEADIN: Lead-in track
 * @MIRAGE_TRACK_LEADOUT: Lead-out track
 *
 * <para>
 * Track constants.
 * </para>
 **/
typedef enum _MirageTrackConstants
{
    MIRAGE_TRACK_LEADIN  = 0x00,
    MIRAGE_TRACK_LEADOUT = 0xAA,
} MirageTrackConstants;


/**********************************************************************\
 *                          MirageTrack object                        *
\**********************************************************************/
#define MIRAGE_TYPE_TRACK            (mirage_track_get_type())
#define MIRAGE_TRACK(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_TRACK, MirageTrack))
#define MIRAGE_TRACK_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_TRACK, MirageTrackClass))
#define MIRAGE_IS_TRACK(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_TRACK))
#define MIRAGE_IS_TRACK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_TRACK))
#define MIRAGE_TRACK_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_TRACK, MirageTrackClass))

typedef struct _MirageTrack        MirageTrack;
typedef struct _MirageTrackClass   MirageTrackClass;
typedef struct _MirageTrackPrivate MirageTrackPrivate;

/**
 * MirageTrack:
 *
 * <para>
 * Contains private data only, and should be accessed using the functions below.
 * </para>
 **/
struct _MirageTrack
{
    MirageObject parent_instance;

    /*< private >*/
    MirageTrackPrivate *priv;
};

struct _MirageTrackClass
{
    MirageObjectClass parent_class;
};

/* Used by MIRAGE_TYPE_TRACK */
GType mirage_track_get_type (void);

/* Track flags */
void mirage_track_set_flags (MirageTrack *self, gint flags);
gint mirage_track_get_flags (MirageTrack *self);

/* Track mode */
void mirage_track_set_mode (MirageTrack *self, MirageTrackModes mode);
MirageTrackModes mirage_track_get_mode (MirageTrack *self);

/* Adr/Ctl */
gint mirage_track_get_adr (MirageTrack *self);

void mirage_track_set_ctl (MirageTrack *self, gint ctl);
gint mirage_track_get_ctl (MirageTrack *self);

/* ISRC */
void mirage_track_set_isrc (MirageTrack *self, const gchar *isrc);
const gchar * mirage_track_get_isrc (MirageTrack *self);

/* Read and get sector */
MirageSector *mirage_track_get_sector (MirageTrack *self, gint address, gboolean abs, GError **error);
gint mirage_track_read_sector (MirageTrack *self, gint address, gboolean abs, guint8 main_sel, guint8 subc_sel, guint8 *buffer, gint buffer_size, GError **error);

/* Layout */
gint mirage_track_layout_get_session_number (MirageTrack *self);
void mirage_track_layout_set_track_number (MirageTrack *self, gint track_number);
gint mirage_track_layout_get_track_number (MirageTrack *self);
void mirage_track_layout_set_start_sector (MirageTrack *self, gint start_sector);
gint mirage_track_layout_get_start_sector (MirageTrack *self);
gint mirage_track_layout_get_length (MirageTrack *self);

/* Data fragments handling */
gint mirage_track_get_number_of_fragments (MirageTrack *self);
void mirage_track_add_fragment (MirageTrack *self, gint index, MirageFragment *fragment);
gboolean mirage_track_remove_fragment_by_index (MirageTrack *self, gint index, GError **error);
void mirage_track_remove_fragment_by_object (MirageTrack *self, MirageFragment *fragment);
MirageFragment *mirage_track_get_fragment_by_index (MirageTrack *self, gint index, GError **error);
MirageFragment *mirage_track_get_fragment_by_address (MirageTrack *self, gint address, GError **error);
gboolean mirage_track_enumerate_fragments (MirageTrack *self, MirageCallbackFunction func, gpointer user_data);

MirageFragment *mirage_track_find_fragment_with_subchannel (MirageTrack *self, GError **error);

/* Track start */
void mirage_track_set_track_start (MirageTrack *self, gint track_start);
gint mirage_track_get_track_start (MirageTrack *self);

/* Indices handling */
gint mirage_track_get_number_of_indices (MirageTrack *self);
gboolean mirage_track_add_index (MirageTrack *self, gint address, GError **error);
gboolean mirage_track_remove_index_by_number (MirageTrack *self, gint number, GError **error);
void mirage_track_remove_index_by_object (MirageTrack *self, MirageIndex *index);
MirageIndex *mirage_track_get_index_by_number (MirageTrack *self, gint number, GError **error);
MirageIndex *mirage_track_get_index_by_address (MirageTrack *self, gint address, GError **error);
gboolean mirage_track_enumerate_indices (MirageTrack *self, MirageCallbackFunction func, gpointer user_data);

/* Languages (CD-Text) handling */
gint mirage_track_get_number_of_languages (MirageTrack *self);
gboolean mirage_track_add_language (MirageTrack *self, gint code, MirageLanguage *language, GError **error);
gboolean mirage_track_remove_language_by_index (MirageTrack *self, gint index, GError **error);
gboolean mirage_track_remove_language_by_code (MirageTrack *self, gint code, GError **error);
void mirage_track_remove_language_by_object (MirageTrack *self, MirageLanguage *language);
MirageLanguage *mirage_track_get_language_by_index (MirageTrack *self, gint index, GError **error);
MirageLanguage *mirage_track_get_language_by_code (MirageTrack *self, gint code, GError **error);
gboolean mirage_track_enumerate_languages (MirageTrack *self, MirageCallbackFunction func, gpointer user_data);

/* Two nice convenience functions */
MirageTrack *mirage_track_get_prev (MirageTrack *self, GError **error);
MirageTrack *mirage_track_get_next (MirageTrack *self, GError **error);

G_END_DECLS

#endif /* __MIRAGE_TRACK_H__ */
