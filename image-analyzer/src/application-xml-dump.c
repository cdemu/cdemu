/*
 *  Image Analyzer: Application object - XML dump
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <gtk/gtk.h>
#include <mirage.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "application.h"
#include "application-private.h"

#include "xml-tags.h"


/**********************************************************************\
 *                          XML node helpers                          *
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


/**********************************************************************\
 *                         XML dump functions                         *
\**********************************************************************/
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
        const gchar *data;
        gint len;
        if (mirage_language_get_pack_data(language, pack_types[i].code, &data, &len, NULL)) {
            xmlNodePtr pack_node = xml_add_node_with_content(parent, pack_types[i].tag, "%s", data);
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
    const gchar **filenames;
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
 *                          Public function                           *
\**********************************************************************/
void ia_application_create_xml_dump (IaApplication *self)
{
    xmlNodePtr root_node;
    xmlDocPtr doc;

    /* Create new XML tree */
    doc = xmlNewDoc(BAD_CAST "1.0");
    root_node = xmlNewNode(NULL, BAD_CAST TAG_IA_DUMP);
    xmlDocSetRootElement(doc, root_node);

    /* Dump disc to XML tree */
    xml_dump_disc(self->priv->disc, root_node);

    /* Note: libMirage log is added to XML tree upon actual storing of the dump */

    /* Store XML */
    self->priv->xml_doc = doc;
}
