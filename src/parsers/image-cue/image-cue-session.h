/*
 *  libMirage: CUE image parser: Session object
 *  Copyright (C) 2006-2007 Rok Mandeljc
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

#ifndef __IMAGE_CUE_SESSION_H__
#define __IMAGE_CUE_SESSION_H__


G_BEGIN_DECLS

#define MIRAGE_TYPE_SESSION_CUE            (mirage_session_cue_get_type(global_module))
#define MIRAGE_SESSION_CUE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_SESSION_CUE, MIRAGE_Session_CUE))
#define MIRAGE_SESSION_CUE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_SESSION_CUE, MIRAGE_Session_CUEClass))
#define MIRAGE_IS_SESSION_CUE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_SESSION_CUE))
#define MIRAGE_IS_SESSION_CUE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_SESSION_CUE))
#define MIRAGE_SESSION_CUE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_SESSION_CUE, MIRAGE_Session_CUEClass))

typedef struct {
    MIRAGE_Session parent;
} MIRAGE_Session_CUE;

typedef struct {
    MIRAGE_SessionClass parent;
} MIRAGE_Session_CUEClass;

/* Used by MIRAGE_TYPE_SESSION_CUE */
GType mirage_session_cue_get_type (GTypeModule *module);

/* Helpers */
gboolean __mirage_session_cue_finish_last_track (MIRAGE_Session *self, GError **error);
gboolean __mirage_session_cue_set_cue_filename (MIRAGE_Session *self, gchar *filename, GError **error);
gboolean __mirage_session_cue_set_mcn (MIRAGE_Session *self, gchar *mcn, GError **error);
gboolean __mirage_session_cue_set_new_file (MIRAGE_Session *self, gchar *filename_string, gchar *file_type, GError **error);
gboolean __mirage_session_cue_add_track (MIRAGE_Session *self, gint number, gchar *mode_string, GError **error);
gboolean __mirage_session_cue_add_index (MIRAGE_Session *self, gint number, gint address, GError **error);
gboolean __mirage_session_cue_set_flag (MIRAGE_Session *self, gint flag, GError **error);
gboolean __mirage_session_cue_set_isrc (MIRAGE_Session *self, gchar *isrc, GError **error);
gboolean __mirage_session_cue_add_empty_part (MIRAGE_Session *self, gint length, GError **error);

G_END_DECLS

#endif /* __IMAGE_CUE_H__ */
