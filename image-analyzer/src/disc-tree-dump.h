/*
 *  Image Analyzer: Disc tree dump
 *  Copyright (C) 2007-2014 Rok Mandeljc
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

#ifndef __IMAGE_ANALYZER_DISC_TREE_DUMP_H__
#define __IMAGE_ANALYZER_DISC_TREE_DUMP_H__


G_BEGIN_DECLS


#define IA_TYPE_DISC_TREE_DUMP            (ia_disc_tree_dump_get_type())
#define IA_DISC_TREE_DUMP(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), IA_TYPE_DISC_TREE_DUMP, IaDiscTreeDump))
#define IA_DISC_TREE_DUMP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), IA_TYPE_DISC_TREE_DUMP, IaDiscTreeDumpClass))
#define IA_IS_DISC_TREE_DUMP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), IA_TYPE_DISC_TREE_DUMP))
#define IA_IS_DISC_TREE_DUMP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), IA_TYPE_DISC_TREE_DUMP))
#define IA_DISC_TREE_DUMP_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), IA_TYPE_DISC_TREE_DUMP, IaDiscTreeDumpClass))

typedef struct _IaDiscTreeDump         IaDiscTreeDump;
typedef struct _IaDiscTreeDumpClass    IaDiscTreeDumpClass;
typedef struct _IaDiscTreeDumpPrivate  IaDiscTreeDumpPrivate;

struct _IaDiscTreeDump
{
    GObject parent_instance;

    /*< private >*/
    IaDiscTreeDumpPrivate *priv;
};

struct _IaDiscTreeDumpClass
{
    GObjectClass parent_class;
};

/* Used by IA_TYPE_DISC_TREE_DUMP */
GType ia_disc_tree_dump_get_type (void);


/* Public API */
GtkTreeStore *ia_disc_tree_dump_get_treestore (IaDiscTreeDump *self);
const gchar *ia_disc_tree_dump_get_log (IaDiscTreeDump *self);

void ia_disc_tree_dump_clear (IaDiscTreeDump *self);
gboolean ia_disc_tree_dump_save_xml_dump (IaDiscTreeDump *self, const gchar *filename, GError **error);
gboolean ia_disc_tree_dump_load_xml_dump (IaDiscTreeDump *self, const gchar *filename, GError **error);
void ia_disc_tree_dump_create_from_disc (IaDiscTreeDump *self, MirageDisc *disc);


/* XML dump tags */
#define TAG_IMAGE_ANALYZER_DUMP "image-analyzer-dump"
#define TAG_LIBMIRAGE_LOG "libmirage-log"

#define TAG_DISC "disc"
#define TAG_MEDIUM_TYPE "medium-type"
#define TAG_FILENAMES "filenames"
#define TAG_FILENAME "filename"
#define TAG_FIRST_SESSION "first-session"
#define TAG_FIRST_TRACK "first-track"
#define TAG_START_SECTOR "start-sector"
#define TAG_LENGTH "length"
#define TAG_NUM_SESSIONS "num-sessions"
#define TAG_NUM_TRACKS "num-tracks"
#define TAG_SESSIONS "sessions"
#define TAG_DPM "dpm"
#define TAG_DPM_START "dpm-start"
#define TAG_DPM_RESOLUTION "dpm-resolution"
#define TAG_DPM_NUM_ENTRIES "dpm-num-entries"
#define TAG_DPM_ENTRIES "dpm-entries"
#define TAG_DPM_ENTRY "dpm-entry"

#define TAG_SESSION "session"
#define TAG_SESSION_TYPE "session-type"
#define TAG_MCN "mcn"
#define TAG_SESSION_NUMBER "session-number"
#define TAG_FIRST_TRACK "first-track"
#define TAG_START_SECTOR "start-sector"
#define TAG_LENGTH "length"
#define TAG_LEADOUT_LENGTH "leadout-length"
#define TAG_NUM_TRACKS "num-tracks"
#define TAG_TRACKS "tracks"
#define TAG_NUM_LANGUAGES "num-languages"
#define TAG_LANGUAGES "languages"

#define TAG_TRACK "track"
#define TAG_FLAGS "flags"
#define TAG_SECTOR_TYPE "sector-type"
#define TAG_ADR "adr"
#define TAG_CTL "ctl"
#define TAG_ISRC "isrc"
#define TAG_SESSION_NUMBER "session-number"
#define TAG_TRACK_NUMBER "track-number"
#define TAG_START_SECTOR "start-sector"
#define TAG_LENGTH "length"
#define TAG_NUM_FRAGMENTS "num-fragments"
#define TAG_FRAGMENTS "fragments"
#define TAG_TRACK_START "track-start"
#define TAG_NUM_INDICES "num-indices"
#define TAG_INDICES "indices"
#define TAG_NUM_LANGUAGES "num-languages"
#define TAG_LANGUAGES "languages"

#define TAG_LANGUAGE "language"
#define TAG_LANGUAGE_CODE "language-code"
#define TAG_CONTENT "content"
#define TAG_LENGTH "length"
#define TAG_TITLE "title"
#define TAG_PERFORMER "performer"
#define TAG_SONGWRITER "songwriter"
#define TAG_COMPOSER "composer"
#define TAG_ARRANGER "arranger"
#define TAG_MESSAGE "message"
#define TAG_DISC_ID "disc-id"
#define TAG_GENRE "genre"
#define TAG_TOC "toc"
#define TAG_TOC2 "toc2"
#define TAG_RESERVED_8A "reserved-8a"
#define TAG_RESERVED_8B "reserved-8b"
#define TAG_RESERVED_8C "reserved-8c"
#define TAG_CLOSED_INFO "closed-info"
#define TAG_UPC_ISRC "upc-isrc"
#define TAG_SIZE "size"

#define ATTR_LENGTH "length"

#define TAG_INDEX "index"
#define TAG_NUMBER "number"
#define TAG_ADDRESS "address"

#define TAG_FRAGMENT "fragment"
#define TAG_ADDRESS "address"
#define TAG_LENGTH "length"
#define TAG_MAIN_NAME "main-name"
#define TAG_MAIN_OFFSET "main-offset"
#define TAG_MAIN_SIZE "main-size"
#define TAG_MAIN_FORMAT "main-format"
#define TAG_SUBCHANNEL_NAME "subchannel-name"
#define TAG_SUBCHANNEL_OFFSET "subchannel-offset"
#define TAG_SUBCHANNEL_SIZE "subchannel-size"
#define TAG_SUBCHANNEL_FORMAT "subchannel-format"


/* Generic dump functions */
typedef struct _DumpValue
{
    gint value;
    gchar *name;
} DumpValue;


#define VAL(x) { x, #x }

gchar *dump_value (gint val, const DumpValue *values, gint num_values);
gchar *dump_flags (gint val, const DumpValue *values, gint num_values);

gchar *dump_track_flags (gint track_flags);
gchar *dump_track_sector_type (gint sector_type);
gchar *dump_session_type (gint session_type);
gchar *dump_medium_type (gint medium_type);
gchar *dump_binary_fragment_main_format (gint format);
gchar *dump_binary_fragment_subchannel_format (gint format);


G_END_DECLS

#endif /* __IMAGE_ANALYZER_DISC_TREE_DUMP_H__ */
