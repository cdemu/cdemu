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
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __MIRAGE_DISC_H__
#define __MIRAGE_DISC_H__


G_BEGIN_DECLS

/**
 * MIRAGE_MCN_SIZE:
 *
 * <para>
 * Length of MCN string.
 * </para>
 **/
#define MIRAGE_MCN_SIZE 13

/**
 * MIRAGE_MediumTypes:
 * @MIRAGE_MEDIUM_CD: CD disc
 * @MIRAGE_MEDIUM_DVD: DVD disc
 * @MIRAGE_MEDIUM_BD: BD (Blue-Ray) disc
 * @MIRAGE_MEDIUM_HD: HD-DVD disc
 *
 * <para>
 * Medium types.
 * </para>
 **/
typedef enum
{
    MIRAGE_MEDIUM_CD  = 0x01,
    MIRAGE_MEDIUM_DVD = 0x02,
    MIRAGE_MEDIUM_BD  = 0x03,
    MIRAGE_MEDIUM_HD  = 0x04,
} MIRAGE_MediumTypes;


enum
{
    PROP_MIRAGE_DISC_DVD_REPORT_CSS = 1,
};


#define MIRAGE_TYPE_DISC            (mirage_disc_get_type())
#define MIRAGE_DISC(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_DISC, MIRAGE_Disc))
#define MIRAGE_DISC_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_DISC, MIRAGE_DiscClass))
#define MIRAGE_IS_DISC(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_DISC))
#define MIRAGE_IS_DISC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_DISC))
#define MIRAGE_DISC_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_DISC, MIRAGE_DiscClass))

typedef struct _MIRAGE_Disc         MIRAGE_Disc;
typedef struct _MIRAGE_DiscClass    MIRAGE_DiscClass;
typedef struct _MIRAGE_DiscPrivate  MIRAGE_DiscPrivate;

/**
 * MIRAGE_Disc:
 *
 * <para>
 * Contains private data only, and should be accessed using the functions below.
 * </para>
 **/
struct _MIRAGE_Disc
{
    MIRAGE_Object parent_instance;

    /*< private >*/
    MIRAGE_DiscPrivate *priv;
};

struct _MIRAGE_DiscClass
{
    MIRAGE_ObjectClass parent_class;
};

/* Used by MIRAGE_TYPE_DISC */
GType mirage_disc_get_type (void);


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
/* Medium type */
void mirage_disc_set_medium_type (MIRAGE_Disc *self, gint medium_type);
gint mirage_disc_get_medium_type (MIRAGE_Disc *self);

/* Filename */
void mirage_disc_set_filenames (MIRAGE_Disc *self, gchar **filenames);
void mirage_disc_set_filename (MIRAGE_Disc *self, const gchar *filename);
gchar **mirage_disc_get_filenames (MIRAGE_Disc *self);

/* MCN */
void mirage_disc_set_mcn (MIRAGE_Disc *self, const gchar *mcn);
const gchar *mirage_disc_get_mcn (MIRAGE_Disc *self);

/* Layout */
void mirage_disc_layout_set_first_session (MIRAGE_Disc *self, gint first_session);
gint mirage_disc_layout_get_first_session (MIRAGE_Disc *self);
void mirage_disc_layout_set_first_track (MIRAGE_Disc *self, gint first_track);
gint mirage_disc_layout_get_first_track (MIRAGE_Disc *self);
void mirage_disc_layout_set_start_sector (MIRAGE_Disc *self, gint start_sector);
gint mirage_disc_layout_get_start_sector (MIRAGE_Disc *self);
gint mirage_disc_layout_get_length (MIRAGE_Disc *self);

/* Session handling */
gint mirage_disc_get_number_of_sessions (MIRAGE_Disc *self);
gboolean mirage_disc_add_session_by_index (MIRAGE_Disc *self, gint index, GObject **session, GError **error);
gboolean mirage_disc_add_session_by_number (MIRAGE_Disc *self, gint number, GObject **session, GError **error);
gboolean mirage_disc_remove_session_by_index (MIRAGE_Disc *self, gint index, GError **error);
gboolean mirage_disc_remove_session_by_number (MIRAGE_Disc *self, gint number, GError **error);
void mirage_disc_remove_session_by_object (MIRAGE_Disc *self, GObject *session);
gboolean mirage_disc_get_session_by_index (MIRAGE_Disc *self, gint index, GObject **session, GError **error);
gboolean mirage_disc_get_session_by_number (MIRAGE_Disc *self, gint number, GObject **session, GError **error);
gboolean mirage_disc_get_session_by_address (MIRAGE_Disc *self, gint address, GObject **session, GError **error);
gboolean mirage_disc_get_session_by_track (MIRAGE_Disc *self, gint track, GObject **session, GError **error);
gboolean mirage_disc_for_each_session (MIRAGE_Disc *self, MIRAGE_CallbackFunction func, gpointer user_data, GError **error);
gboolean mirage_disc_get_session_before (MIRAGE_Disc *self, GObject *cur_session, GObject **prev_session, GError **error);
gboolean mirage_disc_get_session_after (MIRAGE_Disc *self, GObject *cur_session, GObject **next_session, GError **error);

/* Track handling */
gint mirage_disc_get_number_of_tracks (MIRAGE_Disc *self);
gboolean mirage_disc_add_track_by_index (MIRAGE_Disc *self, gint index, GObject **track, GError **error);
gboolean mirage_disc_add_track_by_number (MIRAGE_Disc *self, gint number, GObject **track, GError **error);
gboolean mirage_disc_remove_track_by_index (MIRAGE_Disc *self, gint index, GError **error);
gboolean mirage_disc_remove_track_by_number (MIRAGE_Disc *self, gint number, GError **error);
gboolean mirage_disc_get_track_by_index (MIRAGE_Disc *self, gint index, GObject **track, GError **error);
gboolean mirage_disc_get_track_by_number (MIRAGE_Disc *self, gint number, GObject **track, GError **error);
gboolean mirage_disc_get_track_by_address (MIRAGE_Disc *self, gint address, GObject **track, GError **error);

/* Disc structures */
void mirage_disc_set_disc_structure (MIRAGE_Disc *self, gint layer, gint type, const guint8 *data, gint len);
gboolean mirage_disc_get_disc_structure (MIRAGE_Disc *self, gint layer, gint type, const guint8 **data, gint *len, GError **error);

/* Direct sector access */
GObject *mirage_disc_get_sector (MIRAGE_Disc *self, gint address, GError **error);
gboolean mirage_disc_read_sector (MIRAGE_Disc *self, gint address, guint8 main_sel, guint8 subc_sel, guint8 *ret_buf, gint *ret_len, GError **error);

/* DPM */
void mirage_disc_set_dpm_data (MIRAGE_Disc *self, gint start, gint resolution, gint num_entries, const guint32 *data);
void mirage_disc_get_dpm_data (MIRAGE_Disc *self, gint *start, gint *resolution, gint *num_entries, const guint32 **data);
gboolean mirage_disc_get_dpm_data_for_sector (MIRAGE_Disc *self, gint address, gdouble *angle, gdouble *density, GError **error);

G_END_DECLS

#endif /* __MIRAGE_DISC_H__ */
