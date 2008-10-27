/*
 *  libMirage: TOC image parser: Parser object
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
 
#ifndef __IMAGE_TOC_PARSER_H__
#define __IMAGE_TOC_PARSER_H__


G_BEGIN_DECLS

#define MIRAGE_TYPE_PARSER_TOC            (mirage_parser_toc_get_type(global_module))
#define MIRAGE_PARSER_TOC(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_PARSER_TOC, MIRAGE_Parser_TOC))
#define MIRAGE_PARSER_TOC_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_PARSER_TOC, MIRAGE_Parser_TOCClass))
#define MIRAGE_IS_PARSER_TOC(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_PARSER_TOC))
#define MIRAGE_IS_PARSER_TOC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_PARSER_TOC))
#define MIRAGE_PARSER_TOC_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_PARSER_TOC, MIRAGE_Parser_TOCClass))

typedef struct {
    MIRAGE_Parser parent;
} MIRAGE_Parser_TOC;

typedef struct {
    MIRAGE_ParserClass parent;
} MIRAGE_Parser_TOCClass;

/* Used by MIRAGE_TYPE_PARSER_TOC */
GType mirage_parser_toc_get_type (GTypeModule *module);

/* Helpers */
enum {
    TOC_DATA_TYPE_NONE,
    TOC_DATA_TYPE_AUDIO,
    TOC_DATA_TYPE_DATA,
};

gboolean __mirage_parser_toc_set_toc_filename (MIRAGE_Parser *self, gchar *filename, GError **error);
gboolean __mirage_parser_toc_set_mcn (MIRAGE_Parser *self, gchar *mcn, GError **error);
gboolean __mirage_parser_toc_set_session_type (MIRAGE_Parser *self, gchar *type_string, GError **error);
gboolean __mirage_parser_toc_add_track (MIRAGE_Parser *self, gchar *mode_string, gchar *subchan_string, GError **error);
gboolean __mirage_parser_toc_add_track_fragment (MIRAGE_Parser *self, gint type, gchar *filename_string, gint base_offset, gint start, gint length, GError **error);
gboolean __mirage_parser_toc_set_track_start (MIRAGE_Parser *self, gint start, GError **error);
gboolean __mirage_parser_toc_add_index (MIRAGE_Parser *self, gint address, GError **error);
gboolean __mirage_parser_toc_set_flag (MIRAGE_Parser *self, gint flag, gboolean set, GError **error);
gboolean __mirage_parser_toc_set_isrc (MIRAGE_Parser *self, gchar *isrc, GError **error);

gboolean __mirage_parser_toc_add_language_mapping (MIRAGE_Parser *self, gint index, gint langcode, GError **error);
gboolean __mirage_parser_toc_add_g_laguage (MIRAGE_Parser *self, gint index, GError **error);
gboolean __mirage_parser_toc_add_t_laguage (MIRAGE_Parser *self, gint index, GError **error);
gboolean __mirage_parser_toc_set_g_cdtext_data (MIRAGE_Parser *self, gint pack_type, gchar *data, GError **error);
gboolean __mirage_parser_toc_set_t_cdtext_data (MIRAGE_Parser *self, gint pack_type, gchar *data, GError **error);


G_END_DECLS

#endif /* __IMAGE_TOC_H__ */
