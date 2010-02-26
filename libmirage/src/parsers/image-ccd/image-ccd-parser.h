/*
 *  libMirage: CCD image parser: Parser object
 *  Copyright (C) 2006-2010 Rok Mandeljc
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

#ifndef __IMAGE_CCD_PARSER_H__
#define __IMAGE_CCD_PARSER_H__


G_BEGIN_DECLS

#define MIRAGE_TYPE_PARSER_CCD            (mirage_parser_ccd_get_type(global_module))
#define MIRAGE_PARSER_CCD(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_PARSER_CCD, MIRAGE_Parser_CCD))
#define MIRAGE_PARSER_CCD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_PARSER_CCD, MIRAGE_Parser_CCDClass))
#define MIRAGE_IS_PARSER_CCD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_PARSER_CCD))
#define MIRAGE_IS_PARSER_CCD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_PARSER_CCD))
#define MIRAGE_PARSER_CCD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_PARSER_CCD, MIRAGE_Parser_CCDClass))

typedef struct {
    MIRAGE_Parser parent;
} MIRAGE_Parser_CCD;

typedef struct {
    MIRAGE_ParserClass parent;
} MIRAGE_Parser_CCDClass;


/* Used by MIRAGE_TYPE_PARSER_CCD */
GType mirage_parser_ccd_get_type (GTypeModule *module);


/* Helpers */
gboolean __mirage_parser_ccd_read_header (MIRAGE_Parser *self, CCD_CloneCD *header, GError **error);
gboolean __mirage_parser_ccd_read_disc (MIRAGE_Parser *self, CCD_Disc *disc_data, GError **error);
gboolean __mirage_parser_ccd_read_disc_catalog (MIRAGE_Parser *self, gchar *catalog, GError **error);
gboolean __mirage_parser_ccd_read_session (MIRAGE_Parser *self, CCD_Session *session, GError **error);
gboolean __mirage_parser_ccd_read_entry (MIRAGE_Parser *self, CCD_Entry *entry, GError **error);

gboolean __mirage_parser_ccd_read_track (MIRAGE_Parser *self, gint number, GError **error);
gboolean __mirage_parser_ccd_read_track_mode (MIRAGE_Parser *self, gint mode, GError **error);
gboolean __mirage_parser_ccd_read_track_index0 (MIRAGE_Parser *self, gint index0, GError **error);
gboolean __mirage_parser_ccd_read_track_index1 (MIRAGE_Parser *self, gint index1, GError **error);
gboolean __mirage_parser_ccd_read_track_isrc (MIRAGE_Parser *self, gchar *isrc, GError **error);

G_END_DECLS

#endif /* __IMAGE_CCD_PARSER_H__ */
