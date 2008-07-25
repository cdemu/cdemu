/*
 *  libMirage: CCD image parser: Disc object
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

#ifndef __IMAGE_CCD_DISC_H__
#define __IMAGE_CCD_DISC_H__


G_BEGIN_DECLS

#define MIRAGE_TYPE_DISC_CCD            (mirage_disc_ccd_get_type(global_module))
#define MIRAGE_DISC_CCD(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_DISC_CCD, MIRAGE_Disc_CCD))
#define MIRAGE_DISC_CCD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_DISC_CCD, MIRAGE_Disc_CCDClass))
#define MIRAGE_IS_DISC_CCD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_DISC_CCD))
#define MIRAGE_IS_DISC_CCD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_DISC_CCD))
#define MIRAGE_DISC_CCD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_DISC_CCD, MIRAGE_Disc_CCDClass))

typedef struct {
    MIRAGE_Disc parent;
} MIRAGE_Disc_CCD;

typedef struct {
    MIRAGE_DiscClass parent;
} MIRAGE_Disc_CCDClass;


/* Used by MIRAGE_TYPE_DISC_CCD */
GType mirage_disc_ccd_get_type (GTypeModule *module);


/* Helpers */
gboolean __mirage_disc_ccd_read_header (MIRAGE_Disc *self, CCD_CloneCD *header, GError **error);
gboolean __mirage_disc_ccd_read_disc (MIRAGE_Disc *self, CCD_Disc *disc, GError **error);
gboolean __mirage_disc_ccd_read_disc_catalog (MIRAGE_Disc *self, gchar *catalog, GError **error);
gboolean __mirage_disc_ccd_read_session (MIRAGE_Disc *self, CCD_Session *session, GError **error);
gboolean __mirage_disc_ccd_read_entry (MIRAGE_Disc *self, CCD_Entry *entry, GError **error);

gboolean __mirage_disc_ccd_read_track (MIRAGE_Disc *self, gint number, GError **error);
gboolean __mirage_disc_ccd_read_track_mode (MIRAGE_Disc *self, gint mode, GError **error);
gboolean __mirage_disc_ccd_read_track_index0 (MIRAGE_Disc *self, gint index0, GError **error);
gboolean __mirage_disc_ccd_read_track_index1 (MIRAGE_Disc *self, gint index1, GError **error);
gboolean __mirage_disc_ccd_read_track_isrc (MIRAGE_Disc *self, gchar *isrc, GError **error);

G_END_DECLS

#endif /* __IMAGE_CCD_DISC_H__ */
