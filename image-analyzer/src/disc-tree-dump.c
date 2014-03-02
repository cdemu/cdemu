/*
 *  Image analyzer: disc tree dump
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <mirage.h>

#include "disc-tree-dump.h"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define IA_DISC_TREE_DUMP_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), IA_TYPE_DISC_TREE_DUMP, IaDiscTreeDumpPrivate))

struct _IaDiscTreeDumpPrivate
{
    GtkTreeStore *treestore;

    xmlDocPtr xml_doc;
    gchar *log;

    gchar *filename;
};


/**********************************************************************\
 *                        Generic dump functions                      *
\**********************************************************************/
gchar *dump_value (gint val, const DumpValue *values, gint num_values)
{
    for (gint i = 0; i < num_values; i++) {
        if (values[i].value == val) {
            return values[i].name;
        }
    }

    return "<Unknown>";
}

gchar *dump_flags (gint val, const DumpValue *values, gint num_values)
{
    static gchar tmp_string[256] = "";
    gchar *ptr = tmp_string;

    memset(tmp_string, 0, sizeof(tmp_string));

    for (gint i = 0; i < num_values; i++) {
        if ((val & values[i].value) == values[i].value) {
            if (strlen(tmp_string) > 0) {
                g_assert(strlen(tmp_string)+3 <= sizeof(tmp_string));
                ptr += g_snprintf(ptr, 3, "; ");
            }
            g_assert(strlen(values[i].name)+1 <= sizeof(tmp_string));
            ptr += g_snprintf(ptr, strlen(values[i].name)+1, "%s", values[i].name);
        }
    }

    return tmp_string;
}


/**********************************************************************\
 *                        Specific dump functions                     *
\**********************************************************************/
gchar *dump_track_flags (gint track_flags)
{
    static DumpValue values[] = {
        { MIRAGE_TRACK_FLAG_FOURCHANNEL, "four channel audio" },
        { MIRAGE_TRACK_FLAG_COPYPERMITTED, "copy permitted" },
        { MIRAGE_TRACK_FLAG_PREEMPHASIS, "pre-emphasis" },
    };

    return dump_flags(track_flags, values, G_N_ELEMENTS(values));
}

gchar *dump_track_sector_type (gint track_mode)
{
    static DumpValue values[] = {
        { MIRAGE_SECTOR_MODE0, "Mode 0" },
        { MIRAGE_SECTOR_AUDIO, "Audio" },
        { MIRAGE_SECTOR_MODE1, "Mode 1" },
        { MIRAGE_SECTOR_MODE2, "Mode 2 Formless" },
        { MIRAGE_SECTOR_MODE2_FORM1, "Mode 2 Form 1" },
        { MIRAGE_SECTOR_MODE2_FORM2, "Mode 2 Form 2" },
        { MIRAGE_SECTOR_MODE2_MIXED, "Mode 2 Mixed" },
    };

    return dump_value(track_mode, values, G_N_ELEMENTS(values));
}

gchar *dump_session_type (gint session_type)
{
    static DumpValue values[] = {
        { MIRAGE_SESSION_CDROM, "CD-DA/CD-ROM" },
        { MIRAGE_SESSION_CDI, "CD-I" },
        { MIRAGE_SESSION_CDROM_XA, "CD-ROM XA" },
    };

    return dump_value(session_type, values, G_N_ELEMENTS(values));
}

gchar *dump_medium_type (gint medium_type)
{
    static DumpValue values[] = {
        { MIRAGE_MEDIUM_CD, "CD-ROM" },
        { MIRAGE_MEDIUM_DVD, "DVD-ROM" },
        { MIRAGE_MEDIUM_BD, "BlueRay Disc" },
        { MIRAGE_MEDIUM_HD, "HD-DVD Disc" },
        { MIRAGE_MEDIUM_HDD, "Hard-disk" }
    };

    return dump_value(medium_type, values, G_N_ELEMENTS(values));
}


gchar *dump_binary_fragment_main_format (gint format)
{
    static DumpValue values[] = {
        { MIRAGE_MAIN_DATA_FORMAT_DATA, "Binary data" },
        { MIRAGE_MAIN_DATA_FORMAT_AUDIO, "Audio data" },
        { MIRAGE_MAIN_DATA_FORMAT_AUDIO_SWAP, "Audio data (swapped)" },
    };

    return dump_flags(format, values, G_N_ELEMENTS(values));
}

gchar *dump_binary_fragment_subchannel_format (gint format)
{
    static DumpValue values[] = {
        { MIRAGE_SUBCHANNEL_DATA_FORMAT_INTERNAL, "internal" },
        { MIRAGE_SUBCHANNEL_DATA_FORMAT_EXTERNAL, "external" },

        { MIRAGE_SUBCHANNEL_DATA_FORMAT_PW96_INTERLEAVED, "PW96 interleaved" },
        { MIRAGE_SUBCHANNEL_DATA_FORMAT_PW96_LINEAR, "PW96 linear" },
        { MIRAGE_SUBCHANNEL_DATA_FORMAT_RW96, "RW96" },
        { MIRAGE_SUBCHANNEL_DATA_FORMAT_Q16, "Q16" },
    };

    return dump_flags(format, values, G_N_ELEMENTS(values));
}


/**********************************************************************\
 *                            Disc -> XML                             *
\**********************************************************************/
static xmlNodePtr xml_add_node (xmlNodePtr parent, gchar *name)
{
    return xmlNewChild(parent, NULL, BAD_CAST name, NULL);
}

static xmlAttrPtr xml_add_attribute (xmlNodePtr node, gchar *name, gchar *format, ...)
{
    va_list args;
    gchar content[255] = "";

    /* Create content string */
    va_start(args, format);
    g_vsnprintf(content, sizeof(content), format, args);
    va_end(args);

    /* Create the node */
    return xmlNewProp(node, BAD_CAST name, BAD_CAST content);
}

static xmlNodePtr xml_add_node_with_content (xmlNodePtr parent, gchar *name, gchar *format, ...)
{
    va_list args;
    gchar content[255] = "";

    /* Create content string */
    va_start(args, format);
    g_vsnprintf(content, sizeof(content), format, args);
    va_end(args);

    /* Create the node */
    return xmlNewTextChild(parent, NULL, BAD_CAST name, BAD_CAST content);
}

static gboolean xml_dump_fragment (MirageFragment *fragment, xmlNodePtr parent)
{
    gint address, length;

    const gchar *main_name, *subchannel_name;
    guint64 main_offset, subchannel_offset;
    gint main_size, subchannel_size;
    gint main_format, subchannel_format;

    /* Make fragment node parent */
    parent = xml_add_node(parent, TAG_FRAGMENT);

    address = mirage_fragment_get_address(fragment);
    xml_add_node_with_content(parent, TAG_ADDRESS, "%d", address);

    length = mirage_fragment_get_length(fragment);
    xml_add_node_with_content(parent, TAG_LENGTH, "%d", length);

    /* Main data */
    main_name = mirage_fragment_main_data_get_filename(fragment);
    xml_add_node_with_content(parent, TAG_MAIN_NAME, "%s", main_name);

    main_offset = mirage_fragment_main_data_get_offset(fragment);
    xml_add_node_with_content(parent, TAG_MAIN_OFFSET, "%lld", main_offset);

    main_size = mirage_fragment_main_data_get_size(fragment);
    xml_add_node_with_content(parent, TAG_MAIN_SIZE, "%d", main_size);

    main_format = mirage_fragment_main_data_get_format(fragment);
    xml_add_node_with_content(parent, TAG_MAIN_FORMAT, "0x%X", main_format);

    /* Subchannel data */
    subchannel_name = mirage_fragment_subchannel_data_get_filename(fragment);
    xml_add_node_with_content(parent, TAG_SUBCHANNEL_NAME, "%s", subchannel_name);

    subchannel_offset = mirage_fragment_subchannel_data_get_offset(fragment);
    xml_add_node_with_content(parent, TAG_SUBCHANNEL_OFFSET, "%lld", subchannel_offset);

    subchannel_size = mirage_fragment_subchannel_data_get_size(fragment);
    xml_add_node_with_content(parent, TAG_SUBCHANNEL_SIZE, "%d", subchannel_size);

    subchannel_format = mirage_fragment_subchannel_data_get_format(fragment);
    xml_add_node_with_content(parent, TAG_SUBCHANNEL_FORMAT, "0x%X", subchannel_format);

    return TRUE;
}

static gboolean xml_dump_index (MirageIndex *index, xmlNodePtr parent)
{
    gint number, address;

    /* Make index node parent */
    parent = xml_add_node(parent, TAG_INDEX);

    number = mirage_index_get_number(index);
    xml_add_node_with_content(parent, TAG_NUMBER, "%d", number);

    address = mirage_index_get_address(index);
    xml_add_node_with_content(parent, TAG_ADDRESS, "%d", address);

    return TRUE;
}

static gboolean xml_dump_language (MirageLanguage *language, xmlNodePtr parent)
{
    static const struct {
        gchar *tag;
        gint code;
    } pack_types[] = {
        { TAG_TITLE, MIRAGE_LANGUAGE_PACK_TITLE },
        { TAG_PERFORMER, MIRAGE_LANGUAGE_PACK_PERFORMER },
        { TAG_SONGWRITER, MIRAGE_LANGUAGE_PACK_SONGWRITER },
        { TAG_COMPOSER, MIRAGE_LANGUAGE_PACK_COMPOSER },
        { TAG_ARRANGER, MIRAGE_LANGUAGE_PACK_ARRANGER },
        { TAG_MESSAGE, MIRAGE_LANGUAGE_PACK_MESSAGE },
        { TAG_DISC_ID, MIRAGE_LANGUAGE_PACK_DISC_ID },
        { TAG_GENRE, MIRAGE_LANGUAGE_PACK_GENRE },
        { TAG_TOC, MIRAGE_LANGUAGE_PACK_TOC },
        { TAG_TOC2, MIRAGE_LANGUAGE_PACK_TOC2 },
        { TAG_RESERVED_8A, MIRAGE_LANGUAGE_PACK_RES_8A },
        { TAG_RESERVED_8B, MIRAGE_LANGUAGE_PACK_RES_8B },
        { TAG_RESERVED_8C, MIRAGE_LANGUAGE_PACK_RES_8C },
        { TAG_CLOSED_INFO, MIRAGE_LANGUAGE_PACK_CLOSED_INFO },
        { TAG_UPC_ISRC, MIRAGE_LANGUAGE_PACK_UPC_ISRC },
        { TAG_SIZE, MIRAGE_LANGUAGE_PACK_SIZE },
    };

    gint code;

    /* Make language node parent */
    parent = xml_add_node(parent, TAG_LANGUAGE);

    code = mirage_language_get_code(language);
    xml_add_node_with_content(parent, TAG_LANGUAGE_CODE, "%d", code);

    for (gint i = 0; i < G_N_ELEMENTS(pack_types); i++) {
        const guint8 *data;
        gint len;
        if (mirage_language_get_pack_data(language, pack_types[i].code, &data, &len, NULL)) {
            xmlNodePtr pack_node = xml_add_node_with_content(parent, pack_types[i].tag, "%s", (const gchar *)data);
            xml_add_attribute(pack_node, ATTR_LENGTH, "%d", len);
        }
    }

    return TRUE;
}

static gboolean xml_dump_track (MirageTrack *track, xmlNodePtr parent)
{
    gint flags, sector_type;
    gint adr, ctl;
    const gchar *isrc;
    gint session_number, track_number;
    gint start_sector, length;
    gint num_fragments;
    gint track_start;
    gint num_indices, num_languages;

    /* Make track node parent */
    parent = xml_add_node(parent, TAG_TRACK);

    flags = mirage_track_get_flags(track);
    xml_add_node_with_content(parent, TAG_FLAGS, "0x%X", flags);

    sector_type = mirage_track_get_sector_type(track);
    xml_add_node_with_content(parent, TAG_SECTOR_TYPE, "0x%X", sector_type);

    adr = mirage_track_get_adr(track);
    xml_add_node_with_content(parent, TAG_ADR, "%d", adr);

    ctl = mirage_track_get_ctl(track);
    xml_add_node_with_content(parent, TAG_CTL, "%d", ctl);

    isrc = mirage_track_get_isrc(track);
    if (isrc) xml_add_node_with_content(parent, TAG_ISRC, "%s", isrc);

    session_number = mirage_track_layout_get_session_number(track);
    xml_add_node_with_content(parent, TAG_SESSION_NUMBER, "%d", session_number);

    track_number = mirage_track_layout_get_track_number(track);
    xml_add_node_with_content(parent, TAG_TRACK_NUMBER, "%d", track_number);

    start_sector = mirage_track_layout_get_start_sector(track);
    xml_add_node_with_content(parent, TAG_START_SECTOR, "%d", start_sector);

    length = mirage_track_layout_get_length(track);
    xml_add_node_with_content(parent, TAG_LENGTH, "%d", length);

    num_fragments = mirage_track_get_number_of_fragments(track);
    xml_add_node_with_content(parent, TAG_NUM_FRAGMENTS, "%d", num_fragments);

    if (num_fragments) {
        xmlNodePtr node = xml_add_node(parent, TAG_FRAGMENTS);
        mirage_track_enumerate_fragments(track, (MirageEnumFragmentCallback)xml_dump_fragment, node);
    }

    track_start = mirage_track_get_track_start(track);
    xml_add_node_with_content(parent, TAG_TRACK_START, "%d", track_start);

    num_indices = mirage_track_get_number_of_indices(track);
    xml_add_node_with_content(parent, TAG_NUM_INDICES, "%d", num_indices);

    if (num_indices) {
        xmlNodePtr node = xml_add_node(parent, TAG_INDICES);
        mirage_track_enumerate_indices(track, (MirageEnumIndexCallback)xml_dump_index, node);
    }

    num_languages = mirage_track_get_number_of_languages(track);
    xml_add_node_with_content(parent, TAG_NUM_LANGUAGES, "%d", num_languages);

    if (num_languages) {
        xmlNodePtr node = xml_add_node(parent, TAG_LANGUAGES);
        mirage_track_enumerate_languages(track, (MirageEnumLanguageCallback)xml_dump_language, node);
    }

    return TRUE;
}

static gboolean xml_dump_session (MirageSession *session, xmlNodePtr parent)
{
    gint session_type, session_number;
    const gchar *mcn;
    gint first_track;
    gint start_sector, length;
    gint leadout_length;
    gint num_tracks, num_languages;

    /* Make session node parent */
    parent = xml_add_node(parent, TAG_SESSION);

    session_type = mirage_session_get_session_type(session);
    xml_add_node_with_content(parent, TAG_SESSION_TYPE, "0x%X", session_type);

    mcn = mirage_session_get_mcn(session);
    if (mcn) xml_add_node_with_content(parent, TAG_MCN, "%s", mcn);

    session_number = mirage_session_layout_get_session_number(session);
    xml_add_node_with_content(parent, TAG_SESSION_NUMBER, "%d", session_number);

    first_track = mirage_session_layout_get_first_track(session);
    xml_add_node_with_content(parent, TAG_FIRST_TRACK, "%d", first_track);

    start_sector = mirage_session_layout_get_start_sector(session);
    xml_add_node_with_content(parent, TAG_START_SECTOR, "%d", start_sector);

    length = mirage_session_layout_get_length(session);
    xml_add_node_with_content(parent, TAG_LENGTH, "%d", length);

    leadout_length = mirage_session_get_leadout_length(session);
    xml_add_node_with_content(parent, TAG_LEADOUT_LENGTH, "%d", leadout_length);

    num_tracks = mirage_session_get_number_of_tracks(session);
    xml_add_node_with_content(parent, TAG_NUM_TRACKS, "%d", num_tracks);

    if (num_tracks) {
        xmlNodePtr node = xml_add_node(parent, TAG_TRACKS);
        mirage_session_enumerate_tracks(session, (MirageEnumTrackCallback)xml_dump_track, node);
    }

    num_languages = mirage_session_get_number_of_languages(session);
    xml_add_node_with_content(parent, TAG_NUM_LANGUAGES, "%d", num_languages);

    if (num_languages) {
        xmlNodePtr node = xml_add_node(parent, TAG_LANGUAGES);
        mirage_session_enumerate_languages(session, (MirageEnumLanguageCallback)xml_dump_language, node);
    }

    //mirage_session_get_cdtext_data(session, guint8 **data, gint *len, NULL);

    return TRUE;
}

static gboolean xml_dump_disc (MirageDisc *disc, xmlNodePtr parent)
{
    gint medium_type;
    gchar **filenames;
    gint first_session, first_track;
    gint start_sector, length;
    gint num_sessions, num_tracks;

    gint dpm_start, dpm_resolution, dpm_entries;
    const guint32 *dpm_data;

    xmlNodePtr node;

    /* Make disc node parent */
    parent = xml_add_node(parent, TAG_DISC);

    medium_type = mirage_disc_get_medium_type(disc);
    xml_add_node_with_content(parent, TAG_MEDIUM_TYPE, "0x%X", medium_type);

    filenames = mirage_disc_get_filenames(disc);
    node = xml_add_node(parent, TAG_FILENAMES);
    for (gint i = 0; filenames[i]; i++) {
        xml_add_node_with_content(node, TAG_FILENAME, "%s", filenames[i]);
    }

    first_session = mirage_disc_layout_get_first_session(disc);
    xml_add_node_with_content(parent, TAG_FIRST_SESSION, "%d", first_session);

    first_track = mirage_disc_layout_get_first_track(disc);
    xml_add_node_with_content(parent, TAG_FIRST_TRACK, "%d", first_track);

    start_sector = mirage_disc_layout_get_start_sector(disc);
    xml_add_node_with_content(parent, TAG_START_SECTOR, "%d", start_sector);

    length = mirage_disc_layout_get_length(disc);
    xml_add_node_with_content(parent, TAG_LENGTH, "%d", length);

    num_sessions = mirage_disc_get_number_of_sessions(disc);
    xml_add_node_with_content(parent, TAG_NUM_SESSIONS, "%d", num_sessions);

    num_tracks = mirage_disc_get_number_of_tracks(disc);
    xml_add_node_with_content(parent, TAG_NUM_TRACKS, "%d", num_tracks);

    if (num_sessions) {
        xmlNodePtr session_node = xml_add_node(parent, TAG_SESSIONS);
        mirage_disc_enumerate_sessions(disc, (MirageEnumSessionCallback)xml_dump_session, session_node);
    }

    mirage_disc_get_dpm_data(disc, &dpm_start, &dpm_resolution, &dpm_entries, &dpm_data);
    if (dpm_entries) {
        node = xml_add_node(parent, TAG_DPM);

        /* DPM */
        xml_add_node_with_content(node, TAG_DPM_START, "%d", dpm_start);
        xml_add_node_with_content(node, TAG_DPM_RESOLUTION, "%d", dpm_resolution);
        xml_add_node_with_content(node, TAG_DPM_NUM_ENTRIES, "%d", dpm_entries);

        node = xml_add_node(node, TAG_DPM_ENTRIES);
        for (gint i = 0; i < dpm_entries; i++) {
            xml_add_node_with_content(node, TAG_DPM_ENTRY, "%d", dpm_data[i]);
        }
    }

    return TRUE;
}


/**********************************************************************\
 *                       XML -> GtkTreeStore                          *
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

    treestore_add_node(treestore, parent, &node, "Fragment");

    /* Go over nodes */
    for (xmlNodePtr cur_node = xml_node->children; cur_node; cur_node = cur_node->next) {
        /* Skip non-element nodes */
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_ADDRESS)) {
            gint address = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Address: %d (0x%X)", address, address);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_LENGTH)) {
            gint length = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Length: %d (0x%X)", length, length);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_MAIN_NAME)) {
            gchar *filename = xml_node_get_string(cur_node);
            treestore_add_node(treestore, &node, NULL, "Main data: File name: %s", filename);
            g_free(filename);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_MAIN_OFFSET)) {
            guint64 main_offset = xml_node_get_uint64(cur_node);
            treestore_add_node(treestore, &node, NULL, "Main data: Offset: %lli (0x%llX)", main_offset, main_offset);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_MAIN_SIZE)) {
            gint main_size  = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Main data: Size: %d (0x%X)", main_size, main_size);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_MAIN_FORMAT)) {
            gint main_format = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Main data: Format: 0x%X (%s)", main_format, dump_binary_fragment_main_format(main_format));
        }  else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_SUBCHANNEL_NAME)) {
            gchar *filename = xml_node_get_string(cur_node);
            treestore_add_node(treestore, &node, NULL, "Subchannel data: File name: %s", filename);
            g_free(filename);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_SUBCHANNEL_OFFSET)) {
            guint64 subchannel_offset = xml_node_get_uint64(cur_node);
            treestore_add_node(treestore, &node, NULL, "Subchannel data: Offset: %lli (0x%llX)", subchannel_offset, subchannel_offset);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_SUBCHANNEL_SIZE)) {
            gint subchannel_size  = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Subchannel data: Size: %d (0x%X)", subchannel_size, subchannel_size);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_SUBCHANNEL_FORMAT)) {
            gint subchannel_format = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Subchannel data: Format: 0x%X (%s)", subchannel_format, dump_binary_fragment_subchannel_format(subchannel_format));
        }
    }
}

static void treestore_add_index (GtkTreeStore *treestore, GtkTreeIter *parent, xmlNodePtr xml_node)
{
    GtkTreeIter node;

    treestore_add_node(treestore, parent, &node, "Index");

    /* Go over nodes */
    for (xmlNodePtr cur_node = xml_node->children; cur_node; cur_node = cur_node->next) {
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
    for (xmlNodePtr cur_node = xml_node->children; cur_node; cur_node = cur_node->next) {
        /* Skip non-element nodes */
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_LANGUAGE_CODE)) {
            gint langcode = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Language code: %d", langcode);
        } else {
            for (gint i = 0; i < G_N_ELEMENTS(pack_types); i++) {
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

    treestore_add_node(treestore, parent, &node, "Track");

    /* Go over nodes */
    for (xmlNodePtr cur_node = xml_node->children; cur_node; cur_node = cur_node->next) {
        /* Skip non-element nodes */
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_FLAGS)) {
            gint flags = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Flags: 0x%X (%s)", flags, dump_track_flags(flags));
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_SECTOR_TYPE)) {
            gint sector_type = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Sector type: 0x%X (%s)", sector_type, dump_track_sector_type(sector_type));
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
            GtkTreeIter fragments;

            treestore_add_node(treestore, &node, &fragments, "Fragments");
            for (xmlNodePtr child_node = cur_node->children; child_node; child_node = child_node->next) {
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
            GtkTreeIter indices;

            treestore_add_node(treestore, &node, &indices, "Indices");
            for (xmlNodePtr child_node = cur_node->children; child_node; child_node = child_node->next) {
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
            GtkTreeIter languages;

            treestore_add_node(treestore, &node, &languages, "Languages");
            for (xmlNodePtr child_node = cur_node->children; child_node; child_node = child_node->next) {
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

    treestore_add_node(treestore, parent, &node, "Session");

    /* Go over nodes */
    for (xmlNodePtr cur_node = xml_node->children; cur_node; cur_node = cur_node->next) {
        /* Skip non-element nodes */
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_SESSION_TYPE)) {
            gint session_type = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Session type: 0x%X (%s)", session_type, dump_session_type(session_type));
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_MCN)) {
            gchar *mcn = xml_node_get_string(cur_node);
            treestore_add_node(treestore, &node, NULL, "MCN: %s", mcn);
            g_free(mcn);
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
            GtkTreeIter tracks;

            treestore_add_node(treestore, &node, &tracks, "Tracks");
            for (xmlNodePtr child_node = cur_node->children; child_node; child_node = child_node->next) {
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
            GtkTreeIter languages;

            treestore_add_node(treestore, &node, &languages, "Languages");
            for (xmlNodePtr child_node = cur_node->children; child_node; child_node = child_node->next) {
                treestore_add_language(treestore, &languages, child_node);
            }
        }
    }
}

static void treestore_add_dpm (GtkTreeStore *treestore, GtkTreeIter *parent, xmlNodePtr xml_node)
{
    GtkTreeIter node;

    treestore_add_node(treestore, parent, &node, "DPM");

    /* Go over nodes */
    for (xmlNodePtr cur_node = xml_node->children; cur_node; cur_node = cur_node->next) {
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
            GtkTreeIter dpm_entries;

            treestore_add_node(treestore, &node, &dpm_entries, "Data entries");
            for (xmlNodePtr child_node = cur_node->children; child_node; child_node = child_node->next) {
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

    treestore_add_node(treestore, parent, &node, "Disc");

    /* Go over nodes */
    for (xmlNodePtr cur_node = xml_node->children; cur_node; cur_node = cur_node->next) {
        /* Skip non-element nodes */
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_MEDIUM_TYPE)) {
            gint medium_type = xml_node_get_double(cur_node);
            treestore_add_node(treestore, &node, NULL, "Medium type: 0x%X (%s)", medium_type, dump_medium_type(medium_type));
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_FILENAMES)) {
            GtkTreeIter files;

            treestore_add_node(treestore, &node, &files, "Filename(s)");

            for (xmlNodePtr child_node = cur_node->children; child_node; child_node = child_node->next) {
                if (child_node->type != XML_ELEMENT_NODE) {
                    continue;
                }

                if (!g_ascii_strcasecmp((gchar *)child_node->name, TAG_FILENAME)) {
                    gchar *filename = xml_node_get_string(child_node);
                    treestore_add_node(treestore, &files, NULL, "%s", filename);
                    g_free(filename);
                }
            }
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
            GtkTreeIter sessions;

            treestore_add_node(treestore, &node, &sessions, "Sessions");
            for (xmlNodePtr child_node = cur_node->children; child_node; child_node = child_node->next) {
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


static void ia_disc_tree_dump_treestore_from_xml (IaDiscTreeDump *self)
{
    xmlNodePtr root_node = xmlDocGetRootElement(self->priv->xml_doc);
    for (xmlNodePtr cur_node = root_node->children; cur_node; cur_node = cur_node->next) {
        /* Skip non-element nodes */
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_DISC)) {
            treestore_add_disc(self->priv->treestore, NULL, cur_node);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_PARSER_LOG)) {
            self->priv->log = xml_node_get_string(cur_node);
        }
    }
}


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
GtkTreeStore *ia_disc_tree_dump_get_treestore (IaDiscTreeDump *self)
{
    return self->priv->treestore;
}

const gchar *ia_disc_tree_dump_get_log (IaDiscTreeDump *self)
{
    return self->priv->log;
}

const gchar *ia_disc_tree_dump_get_filename (IaDiscTreeDump *self)
{
    return self->priv->filename;
}


void ia_disc_tree_dump_clear (IaDiscTreeDump *self)
{
    if (self->priv->xml_doc) {
        xmlFreeDoc(self->priv->xml_doc);
        self->priv->xml_doc = NULL;
    }

    gtk_tree_store_clear(self->priv->treestore);

    g_free(self->priv->log);
    self->priv->log = NULL;

    g_free(self->priv->filename);
    self->priv->filename = NULL;
}

gboolean ia_disc_tree_dump_save_xml_dump (IaDiscTreeDump *self, const gchar *filename)
{
    /* Save the XML tree */
    if (xmlSaveFormatFileEnc(filename, self->priv->xml_doc, "UTF-8", 1) == -1) {
        return FALSE;
    }

    return TRUE;
}

gboolean ia_disc_tree_dump_load_xml_dump (IaDiscTreeDump *self, const gchar *filename)
{
    /* Make sure tree is clear */
    ia_disc_tree_dump_clear(self);

    /* Load XML */
    self->priv->xml_doc = xmlReadFile(filename, NULL, 0);
    if (!self->priv->xml_doc) {
        return FALSE;
    }

    /* Store filename */
    self->priv->filename = g_strdup(filename);

    /* Fill tree store */
    ia_disc_tree_dump_treestore_from_xml(self);

    return TRUE;
}

void ia_disc_tree_dump_create_from_disc (IaDiscTreeDump *self, MirageDisc *disc, const gchar *log)
{
    xmlNodePtr root_node;

    /* Make sure tree is clear */
    ia_disc_tree_dump_clear(self);

    /* Create new XML tree */
    self->priv->xml_doc = xmlNewDoc(BAD_CAST "1.0");
    root_node = xmlNewNode(NULL, BAD_CAST TAG_IMAGE_ANALYZER_DUMP);
    xmlDocSetRootElement(self->priv->xml_doc, root_node);

    /* Dump disc to XML tree */
    xml_dump_disc(disc, root_node);

    /* Append log */
    xmlNewTextChild(root_node, NULL, BAD_CAST TAG_PARSER_LOG, BAD_CAST log);

    /* Fill tree store */
    ia_disc_tree_dump_treestore_from_xml(self);
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE(IaDiscTreeDump, ia_disc_tree_dump, G_TYPE_OBJECT);


static void ia_disc_tree_dump_finalize (GObject *gobject)
{
    IaDiscTreeDump *self = IA_DISC_TREE_DUMP(gobject);

    /* Clear, freeing all resources */
    ia_disc_tree_dump_clear(self);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(ia_disc_tree_dump_parent_class)->finalize(gobject);
}

static void ia_disc_tree_dump_init (IaDiscTreeDump *self)
{
    self->priv = IA_DISC_TREE_DUMP_GET_PRIVATE(self);

    self->priv->xml_doc = NULL;
    self->priv->log = NULL;
    self->priv->filename = NULL;

    self->priv->treestore = gtk_tree_store_new(1, G_TYPE_STRING);
}

static void ia_disc_tree_dump_class_init (IaDiscTreeDumpClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = ia_disc_tree_dump_finalize;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(IaDiscTreeDumpPrivate));
}
