/*
 *  Image Analyzer: Application object - XML load
 *  Copyright (C) 2007-2012 Rok Mandeljc
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <gtk/gtk.h>
#include <mirage.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "image-analyzer-application.h"
#include "image-analyzer-application-private.h"

#include "image-analyzer-dump.h"
#include "image-analyzer-parser-log.h"
#include "image-analyzer-xml-tags.h"


/**********************************************************************\
 *                        XML node helpers                            *
\**********************************************************************/
static inline gchar *xml_node_get_string (xmlNodePtr node)
{
    xmlChar *xml_str = xmlNodeGetContent(node);
    gchar *ret_str = g_strdup((gchar *)xml_str);
    xmlFree(xml_str);
    return ret_str;
}

static inline gdouble xml_node_get_attr_double (xmlNodePtr node, gchar *attr)
{
    xmlChar *xml_str = xmlGetProp(node, BAD_CAST attr);
    gdouble ret_double = g_strtod((gchar *)xml_str, NULL);
    xmlFree(xml_str);
    return ret_double;
}

static inline gdouble xml_node_get_double (xmlNodePtr node)
{
    xmlChar *xml_str = xmlNodeGetContent(node);
    gdouble ret_double = g_strtod((gchar *)xml_str, NULL);
    xmlFree(xml_str);
    return ret_double;
}

static inline guint64 xml_node_get_uint64 (xmlNodePtr node)
{
    xmlChar *xml_str = xmlNodeGetContent(node);
    guint64 ret_uint64 = g_ascii_strtoull((gchar *)xml_str, NULL, 0);
    xmlFree(xml_str);
    return ret_uint64;
}


/**********************************************************************\
 *                      Treestore data loading                         *
\**********************************************************************/
static void treestore_add_node (GtkTreeStore *treestore, GtkTreeIter *parent, GtkTreeIter *node, gchar *format, ...)
{
    GtkTreeIter tmp_node;

    va_list args;
    gchar content[255] = "";

    va_start(args, format);
    g_vsnprintf(content, sizeof(content), format, args);
    va_end(args);

    if (!node) node = &tmp_node;
    gtk_tree_store_append(treestore, node, parent);
    gtk_tree_store_set(treestore, node, 0, content, -1);
}

static void treestore_add_fragment (GtkTreeStore *treestore, GtkTreeIter *parent, xmlNodePtr xml_node)
{
    GtkTreeIter node;
    xmlNodePtr cur_node;

    treestore_add_node(treestore, parent, &node, "Fragment");

    /* Go over nodes */
    for (cur_node = xml_node->children; cur_node; cur_node = cur_node->next) {
        /* Skip non-element nodes */
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_FRAGMENT_ID)) {
            gchar *fragment_id = xml_node_get_string(cur_node);
            treestore_add_node(treestore, &node, NULL, "Fragment ID: %s", fragment_id);
            g_free(fragment_id);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_ADDRESS)) {
            gint address = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Address: %d (0x%X)", address, address);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_LENGTH)) {
            gint length = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Length: %d (0x%X)", length, length);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_TFILE_NAME)) {
            gchar *filename = xml_node_get_string(cur_node);
            treestore_add_node(treestore, &node, NULL, "Track file: Name: %s", filename);
            g_free(filename);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_TFILE_OFFSET)) {
            guint64 tfile_offset = xml_node_get_uint64(cur_node);
            treestore_add_node(treestore, &node, NULL, "Track file: Offset: %lli (0x%llX)", tfile_offset, tfile_offset);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_TFILE_SECTSIZE)) {
            gint tfile_sectsize  = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Track file: Sector size: %d (0x%X)", tfile_sectsize, tfile_sectsize);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_TFILE_FORMAT)) {
            gint tfile_format = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Track file: Format: 0x%X (%s)", tfile_format, dump_binary_fragment_tfile_format(tfile_format));
        }  else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_SFILE_NAME)) {
            gchar *filename = xml_node_get_string(cur_node);
            treestore_add_node(treestore, &node, NULL, "Subchannel file: Name: %s", filename);
            g_free(filename);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_SFILE_OFFSET)) {
            guint64 sfile_offset = xml_node_get_uint64(cur_node);
            treestore_add_node(treestore, &node, NULL, "Subchannel file: Offset: %lli (0x%llX)", sfile_offset, sfile_offset);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_SFILE_SECTSIZE)) {
            gint sfile_sectsize  = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Subchannel file: Sector size: %d (0x%X)", sfile_sectsize, sfile_sectsize);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_SFILE_FORMAT)) {
            gint sfile_format = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Subchannel file: Format: 0x%X (%s)", sfile_format, dump_binary_fragment_sfile_format(sfile_format));
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_FILENAME)) {
            gchar *filename = xml_node_get_string(cur_node);
            treestore_add_node(treestore, &node, NULL, "Filename: %s", filename);
            g_free(filename);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_OFFSET)) {
            gint offset = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Offset (sectors): %d (0x%X)", offset, offset);
        }
    }
}

static void treestore_add_index (GtkTreeStore *treestore, GtkTreeIter *parent, xmlNodePtr xml_node)
{
    GtkTreeIter node;
    xmlNodePtr cur_node;

    treestore_add_node(treestore, parent, &node, "Index");

    /* Go over nodes */
    for (cur_node = xml_node->children; cur_node; cur_node = cur_node->next) {
        /* Skip non-element nodes */
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_NUMBER)) {
            gint number = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Number: %d", number);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_ADDRESS)) {
            gint address = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Address: %d (0x%X)", address, address);
        }
    }
}

static void treestore_add_language (GtkTreeStore *treestore, GtkTreeIter *parent, xmlNodePtr xml_node)
{
    GtkTreeIter node;
    xmlNodePtr cur_node;

    static const struct {
        gchar *name;
        gchar *tag;
    } pack_types[] = {
        { "Title", TAG_TITLE },
        { "Performer", TAG_PERFORMER },
        { "Songwriter", TAG_SONGWRITER },
        { "Composer", TAG_COMPOSER },
        { "Arranger", TAG_ARRANGER },
        { "Message", TAG_MESSAGE },
        { "Disc ID", TAG_DISC_ID },
        { "Genre", TAG_GENRE },
        { "TOC", TAG_TOC },
        { "TOC2", TAG_TOC2 },
        { "Reserved 8A", TAG_RESERVED_8A },
        { "Reserved 8B", TAG_RESERVED_8B },
        { "Reserved 8C", TAG_RESERVED_8C },
        { "Closed info", TAG_CLOSED_INFO },
        { "UPC/ISRC", TAG_UPC_ISRC },
        { "Size", TAG_SIZE },
    };

    treestore_add_node(treestore, parent, &node, "Language");

    /* Go over nodes */
    for (cur_node = xml_node->children; cur_node; cur_node = cur_node->next) {
        /* Skip non-element nodes */
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_LANGUAGE_CODE)) {
            gint langcode = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Language code: %d", langcode);
        } else {
            gint i;

            for (i = 0; i < G_N_ELEMENTS(pack_types); i++) {
                if (!g_ascii_strcasecmp((gchar *)cur_node->name, pack_types[i].tag)) {
                    gchar *data = xml_node_get_string(cur_node);
                    gint len = xml_node_get_attr_double(cur_node, ATTR_LENGTH);
                    treestore_add_node(treestore, &node, NULL, "%s: %s (%i)", pack_types[i].name, data, len);
                    g_free(data);
                }
            }
        }
    }
}

static void treestore_add_track (GtkTreeStore *treestore, GtkTreeIter *parent, xmlNodePtr xml_node)
{
    GtkTreeIter node;
    xmlNodePtr cur_node;

    treestore_add_node(treestore, parent, &node, "Track");

    /* Go over nodes */
    for (cur_node = xml_node->children; cur_node; cur_node = cur_node->next) {
        /* Skip non-element nodes */
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_FLAGS)) {
            gint flags = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Flags: 0x%X (%s)", flags, dump_track_flags(flags));
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_MODE)) {
            gint mode = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Mode: 0x%X (%s)", mode, dump_track_mode(mode));
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_ADR)) {
            gint adr = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "ADR: %d", adr);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_CTL)) {
            gint ctl = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "CTL: %d", ctl);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_ISRC)) {
            gchar *isrc = xml_node_get_string(cur_node);
            treestore_add_node(treestore, &node, NULL, "ISRC: %s", isrc);
            g_free(isrc);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_SESSION_NUMBER)) {
            gint session_number = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Session number: %d", session_number);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_TRACK_NUMBER)) {
            gint track_number = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Track number: %d", track_number);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_START_SECTOR)) {
            gint start_sector = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Start sector: %d (0x%X)", start_sector, start_sector);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_LENGTH)) {
            gint length = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Length: %d (0x%X)", length, length);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_NUM_FRAGMENTS)) {
            gint num_fragments = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Number of fragments: %d", num_fragments);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_FRAGMENTS)) {
            xmlNodePtr child_node;
            GtkTreeIter fragments;

            treestore_add_node(treestore, &node, &fragments, "Fragments");
            for (child_node = cur_node->children; child_node; child_node = child_node->next) {
                if (child_node->type != XML_ELEMENT_NODE) {
                    continue;
                }

                if (!g_ascii_strcasecmp((gchar *)child_node->name, TAG_FRAGMENT)) {
                    treestore_add_fragment(treestore, &fragments, child_node);
                }
            }
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_TRACK_START)) {
            gint track_start = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Track start: %d (0x%X)", track_start, track_start);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_NUM_INDICES)) {
            gint num_indices = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Number of indices: %d", num_indices);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_INDICES)) {
            xmlNodePtr child_node;
            GtkTreeIter indices;

            treestore_add_node(treestore, &node, &indices, "Indices");
            for (child_node = cur_node->children; child_node; child_node = child_node->next) {
                if (child_node->type != XML_ELEMENT_NODE) {
                    continue;
                }

                if (!g_ascii_strcasecmp((gchar *)child_node->name, TAG_INDEX)) {
                    treestore_add_index(treestore, &indices, child_node);
                }
            }
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_NUM_LANGUAGES)) {
            gint num_languages = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Number of languages: %d", num_languages);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_LANGUAGES)) {
            xmlNodePtr child_node;
            GtkTreeIter languages;

            treestore_add_node(treestore, &node, &languages, "Languages");
            for (child_node = cur_node->children; child_node; child_node = child_node->next) {
                if (child_node->type != XML_ELEMENT_NODE) {
                    continue;
                }

                if (!g_ascii_strcasecmp((gchar *)child_node->name, TAG_LANGUAGE)) {
                    treestore_add_language(treestore, &languages, child_node);
                }
            }
        }
    }
}

static void treestore_add_session (GtkTreeStore *treestore, GtkTreeIter *parent, xmlNodePtr xml_node)
{
    GtkTreeIter node;
    xmlNodePtr cur_node;

    treestore_add_node(treestore, parent, &node, "Session");

    /* Go over nodes */
    for (cur_node = xml_node->children; cur_node; cur_node = cur_node->next) {
        /* Skip non-element nodes */
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_SESSION_TYPE)) {
            gint session_type = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Session type: 0x%X (%s)", session_type, dump_session_type(session_type));
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_SESSION_NUMBER)) {
            gint session_number = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Session number: %d", session_number);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_FIRST_TRACK)) {
            gint first_track = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Layout: First track: %d", first_track);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_START_SECTOR)) {
            gint start_sector = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Layout: Start sector: %d (0x%X)", start_sector);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_LENGTH)) {
            gint length = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Layout: Length: %d (0x%X)", length, length);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_LEADOUT_LENGTH)) {
            gint leadout_length = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Leadout length: %d (0x%X)", leadout_length, leadout_length);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_NUM_TRACKS)) {
            gint num_tracks = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Number of tracks: %d", num_tracks);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_TRACKS)) {
            xmlNodePtr child_node;
            GtkTreeIter tracks;

            treestore_add_node(treestore, &node, &tracks, "Tracks");
            for (child_node = cur_node->children; child_node; child_node = child_node->next) {
                if (child_node->type != XML_ELEMENT_NODE) {
                    continue;
                }

                if (!g_ascii_strcasecmp((gchar *)child_node->name, TAG_TRACK)) {
                    treestore_add_track(treestore, &tracks, child_node);
                }
            }
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_NUM_LANGUAGES)) {
            gint num_languages = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Number of languages: %d", num_languages);
        }  else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_LANGUAGES)) {
            xmlNodePtr child_node;
            GtkTreeIter languages;

            treestore_add_node(treestore, &node, &languages, "Languages");
            for (child_node = cur_node->children; child_node; child_node = child_node->next) {
                treestore_add_language(treestore, &languages, child_node);
            }
        }
    }
}

static void treestore_add_dpm (GtkTreeStore *treestore, GtkTreeIter *parent, xmlNodePtr xml_node)
{
    GtkTreeIter node;
    xmlNodePtr cur_node;

    treestore_add_node(treestore, parent, &node, "DPM");

    /* Go over nodes */
    for (cur_node = xml_node->children; cur_node; cur_node = cur_node->next) {
        /* Skip non-element nodes */
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_DPM_START)) {
            gint dpm_start = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Start sector: %d (0x%X)", dpm_start, dpm_start);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_DPM_RESOLUTION)) {
            gint dpm_resolution = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Resolution: %d (0x%X)", dpm_resolution, dpm_resolution);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_DPM_NUM_ENTRIES)) {
            gint dpm_entries = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Number of entries: %d (0x%X)", dpm_entries, dpm_entries);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_DPM_ENTRIES)) {
            xmlNodePtr child_node;
            GtkTreeIter dpm_entries;

            treestore_add_node(treestore, &node, &dpm_entries, "Data entries");
            for (child_node = cur_node->children; child_node; child_node = child_node->next) {
                if (child_node->type != XML_ELEMENT_NODE) {
                    continue;
                }

                if (!g_ascii_strcasecmp((gchar *)child_node->name, TAG_DPM_ENTRY)) {
                    guint32 dpm_entry = xml_node_get_double(child_node);
                    treestore_add_node(treestore, &dpm_entries, NULL, "0x%08X", dpm_entry);
                }
            }
        }
    }
}

static void treestore_add_disc (GtkTreeStore *treestore, GtkTreeIter *parent, xmlNodePtr xml_node)
{
    GtkTreeIter node;
    xmlNodePtr cur_node;

    treestore_add_node(treestore, parent, &node, "Disc");

    /* Go over nodes */
    for (cur_node = xml_node->children; cur_node; cur_node = cur_node->next) {
        /* Skip non-element nodes */
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_MEDIUM_TYPE)) {
            gint medium_type = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Medium type: 0x%X (%s)", medium_type, dump_medium_type(medium_type));
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_FILENAMES)) {
            xmlNodePtr child_node;
            GtkTreeIter files;

            treestore_add_node(treestore, &node, &files, "Filename(s)");

            for (child_node = cur_node->children; child_node; child_node = child_node->next) {
                if (child_node->type != XML_ELEMENT_NODE) {
                    continue;
                }

                if (!g_ascii_strcasecmp((gchar *)child_node->name, TAG_FILENAME)) {
                    gchar *filename = xml_node_get_string(child_node);
                    treestore_add_node(treestore, &files, NULL, "%s", filename);
                    g_free(filename);
                }
            }
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_MCN)) {
            gchar *mcn = xml_node_get_string(cur_node);
            treestore_add_node(treestore, &node, NULL, "MCN: %s", mcn);
            g_free(mcn);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_FIRST_SESSION)) {
            gint first_session = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Layout: First session: %d", first_session);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_FIRST_TRACK)) {
            gint first_track = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Layout: First track: %d", first_track);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_START_SECTOR)) {
            gint start_sector = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Layout: Start sector: %d (0x%X)", start_sector, start_sector);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_LENGTH)) {
            gint length = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Layout: Length: %d (0x%X)", length, length);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_NUM_SESSIONS)) {
            gint num_sessions = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Number of sessions: %d", num_sessions);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_NUM_TRACKS)) {
            gint num_tracks = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Number of tracks: %d", num_tracks);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_SESSIONS)) {
            xmlNodePtr child_node;
            GtkTreeIter sessions;

            treestore_add_node(treestore, &node, &sessions, "Sessions");
            for (child_node = cur_node->children; child_node; child_node = child_node->next) {
                if (child_node->type != XML_ELEMENT_NODE) {
                    continue;
                }

                if (!g_ascii_strcasecmp((gchar *)child_node->name, TAG_SESSION)) {
                    treestore_add_session(treestore, &sessions, child_node);
                }
            }
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_DPM)) {
            treestore_add_dpm(treestore, &node, cur_node);
        }
    }
}


/**********************************************************************\
 *                             XML Data Display                       *
\**********************************************************************/
gboolean image_analyzer_application_display_xml_data (IMAGE_ANALYZER_Application *self)
{
    xmlNodePtr root_node;
    xmlNodePtr cur_node;

    /* Make sure XML tree is valid */
    if (!self->priv->xml_doc) {
        return FALSE;
    }

    /* Get root element and verify it */
    root_node = xmlDocGetRootElement(self->priv->xml_doc);
    if (!root_node || root_node->type != XML_ELEMENT_NODE || g_ascii_strcasecmp((gchar *)root_node->name, TAG_IMAGE_ANALYZER_DUMP)) {
        g_warning("Invalid XML tree!\n");
        return FALSE;
    }

    /* Go over nodes */
    for (cur_node = root_node->children; cur_node; cur_node = cur_node->next) {
        /* Skip non-element nodes */
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_DISC)) {
            treestore_add_disc(self->priv->treestore, NULL, cur_node);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_PARSER_LOG)) {
            gchar *log = xml_node_get_string(cur_node);
            image_analyzer_parser_log_append_to_log(IMAGE_ANALYZER_PARSER_LOG(self->priv->dialog_parser), log);
            g_free(log);
        }
    }

    return TRUE;
}

