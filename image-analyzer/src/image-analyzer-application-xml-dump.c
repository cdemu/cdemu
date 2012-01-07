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

#include "image-analyzer-xml-tags.h"


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
static gboolean xml_dump_fragment (GObject *fragment, xmlNodePtr parent)
{
    const MIRAGE_FragmentInfo *fragment_info;
    gint address, length;

    /* Make fragment node parent */
    parent = xml_add_node(parent, TAG_FRAGMENT);

    if (mirage_fragment_get_fragment_info(MIRAGE_FRAGMENT(fragment), &fragment_info, NULL)) {
        xml_add_node_with_content(parent, TAG_FRAGMENT_ID, "%s", fragment_info->id);
    }

    if (mirage_fragment_get_address(MIRAGE_FRAGMENT(fragment), &address, NULL)) {
        xml_add_node_with_content(parent, TAG_ADDRESS, "%d", address);
    }

    if (mirage_fragment_get_length(MIRAGE_FRAGMENT(fragment), &length, NULL)) {
        xml_add_node_with_content(parent, TAG_LENGTH, "%d", length);
    }

    if (MIRAGE_IS_FRAG_IFACE_NULL(fragment)) {
        /* Nothing to do here*/
    } else if (MIRAGE_IS_FRAG_IFACE_BINARY(fragment)) {
        FILE *tfile_handle, *sfile_handle;
        guint64 tfile_offset, sfile_offset;
        gint tfile_sectsize, sfile_sectsize;
        gint tfile_format, sfile_format;

        if (mirage_frag_iface_binary_track_file_get_handle(MIRAGE_FRAG_IFACE_BINARY(fragment), &tfile_handle, NULL)) {
            xml_add_node_with_content(parent, TAG_TFILE_HANDLE, "%p", tfile_handle);
        }

        if (mirage_frag_iface_binary_track_file_get_offset(MIRAGE_FRAG_IFACE_BINARY(fragment), &tfile_offset, NULL)) {
            xml_add_node_with_content(parent, TAG_TFILE_OFFSET, "%lld", tfile_offset);
        }

        if (mirage_frag_iface_binary_track_file_get_sectsize(MIRAGE_FRAG_IFACE_BINARY(fragment), &tfile_sectsize, NULL)) {
            xml_add_node_with_content(parent, TAG_TFILE_SECTSIZE, "%d", tfile_sectsize);
        }

        if (mirage_frag_iface_binary_track_file_get_format(MIRAGE_FRAG_IFACE_BINARY(fragment), &tfile_format, NULL)) {
            xml_add_node_with_content(parent, TAG_TFILE_FORMAT, "0x%X", tfile_format);
        }

        if (mirage_frag_iface_binary_subchannel_file_get_handle(MIRAGE_FRAG_IFACE_BINARY(fragment), &sfile_handle, NULL)) {
            xml_add_node_with_content(parent, TAG_SFILE_HANDLE, "%p", sfile_handle);
        }

        if (mirage_frag_iface_binary_subchannel_file_get_offset(MIRAGE_FRAG_IFACE_BINARY(fragment), &sfile_offset, NULL)) {
            xml_add_node_with_content(parent, TAG_SFILE_OFFSET, "%lld", sfile_offset);
        }

        if (mirage_frag_iface_binary_subchannel_file_get_sectsize(MIRAGE_FRAG_IFACE_BINARY(fragment), &sfile_sectsize, NULL)) {
            xml_add_node_with_content(parent, TAG_SFILE_SECTSIZE, "%d", sfile_sectsize);
        }

        if (mirage_frag_iface_binary_subchannel_file_get_format(MIRAGE_FRAG_IFACE_BINARY(fragment), &sfile_format, NULL)) {
            xml_add_node_with_content(parent, TAG_SFILE_FORMAT, "0x%X", sfile_format);
        }

    } else if (MIRAGE_IS_FRAG_IFACE_AUDIO(fragment)) {
        const gchar *filename;
        gint offset;

        if (mirage_frag_iface_audio_get_file(MIRAGE_FRAG_IFACE_AUDIO(fragment), &filename, NULL)) {
            xml_add_node_with_content(parent, TAG_FILENAME, "%s", filename);
        }

        if (mirage_frag_iface_audio_get_offset(MIRAGE_FRAG_IFACE_AUDIO(fragment), &offset, NULL)) {
            xml_add_node_with_content(parent, TAG_OFFSET, "%d", offset);
        }
    }

    return TRUE;
}

static gboolean xml_dump_index (GObject *index, xmlNodePtr parent)
{
    gint number, address;

    /* Make index node parent */
    parent = xml_add_node(parent, TAG_INDEX);

    if (mirage_index_get_number(MIRAGE_INDEX(index), &number, NULL)) {
        xml_add_node_with_content(parent, TAG_NUMBER, "%d", number);
    }

    if (mirage_index_get_address(MIRAGE_INDEX(index), &address, NULL)) {
        xml_add_node_with_content(parent, TAG_ADDRESS, "%d", address);
    }

    return TRUE;
}

static gboolean xml_dump_language (GObject *language, xmlNodePtr parent)
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

    gint i;
    gint langcode;

    /* Make language node parent */
    parent = xml_add_node(parent, TAG_LANGUAGE);

    if (mirage_language_get_langcode(MIRAGE_LANGUAGE(language), &langcode, NULL)) {
        xml_add_node_with_content(parent, TAG_LANGUAGE_CODE, "%d", langcode);
    }

    for (i = 0; i < G_N_ELEMENTS(pack_types); i++) {
        const gchar *data;
        gint len;
        if (mirage_language_get_pack_data(MIRAGE_LANGUAGE(language), pack_types[i].code, &data, &len, NULL)) {
            xmlNodePtr pack_node = xml_add_node_with_content(parent, pack_types[i].tag, "%s", data);
            xml_add_attribute(pack_node, ATTR_LENGTH, "%d", len);
        }
    }


    return TRUE;
}

static gboolean xml_dump_track (GObject *track, xmlNodePtr parent)
{
    gint flags, mode;
    gint adr, ctl;
    const gchar *isrc;
    gint session_number, track_number;
    gint start_sector, length;
    gint num_fragments;
    gint track_start;
    gint num_indices, num_languages;

    /* Make track node parent */
    parent = xml_add_node(parent, TAG_TRACK);

    if (mirage_track_get_flags(MIRAGE_TRACK(track), &flags, NULL)) {
        xml_add_node_with_content(parent, TAG_FLAGS, "0x%X", flags);
    }

    if (mirage_track_get_mode(MIRAGE_TRACK(track), &mode, NULL)) {
        xml_add_node_with_content(parent, TAG_MODE, "0x%X", mode);
    }

    if (mirage_track_get_adr(MIRAGE_TRACK(track), &adr, NULL)) {
        xml_add_node_with_content(parent, TAG_ADR, "%d", adr);
    }

    if (mirage_track_get_ctl(MIRAGE_TRACK(track), &ctl, NULL)) {
        xml_add_node_with_content(parent, TAG_CTL, "%d", ctl);
    }

    if (mirage_track_get_isrc(MIRAGE_TRACK(track), &isrc, NULL)) {
        xml_add_node_with_content(parent, TAG_ISRC, "%s", isrc);
    }

    if (mirage_track_layout_get_session_number(MIRAGE_TRACK(track), &session_number, NULL)) {
        xml_add_node_with_content(parent, TAG_SESSION_NUMBER, "%d", session_number);
    }

    if (mirage_track_layout_get_track_number(MIRAGE_TRACK(track), &track_number, NULL)) {
        xml_add_node_with_content(parent, TAG_TRACK_NUMBER, "%d", track_number);
    }

    if (mirage_track_layout_get_start_sector(MIRAGE_TRACK(track), &start_sector, NULL)) {
        xml_add_node_with_content(parent, TAG_START_SECTOR, "%d", start_sector);
    }

    if (mirage_track_layout_get_length(MIRAGE_TRACK(track), &length, NULL)) {
        xml_add_node_with_content(parent, TAG_LENGTH, "%d", length);
    }

    if (mirage_track_get_number_of_fragments(MIRAGE_TRACK(track), &num_fragments, NULL)) {
        xml_add_node_with_content(parent, TAG_NUM_FRAGMENTS, "%d", num_fragments);
    }

    if (num_fragments) {
        xmlNodePtr node = xml_add_node(parent, TAG_FRAGMENTS);
        mirage_track_for_each_fragment(MIRAGE_TRACK(track), (MIRAGE_CallbackFunction)xml_dump_fragment, node, NULL);
    }

    if (mirage_track_get_track_start(MIRAGE_TRACK(track), &track_start, NULL)) {
        xml_add_node_with_content(parent, TAG_TRACK_START, "%d", track_start);
    }

    if (mirage_track_get_number_of_indices(MIRAGE_TRACK(track), &num_indices, NULL)) {
        xml_add_node_with_content(parent, TAG_NUM_INDICES, "%d", num_indices);
    }

    if (num_indices) {
        xmlNodePtr node = xml_add_node(parent, TAG_INDICES);
        mirage_track_for_each_index(MIRAGE_TRACK(track), (MIRAGE_CallbackFunction)xml_dump_index, node, NULL);
    }

    if (mirage_track_get_number_of_languages(MIRAGE_TRACK(track), &num_languages, NULL)) {
        xml_add_node_with_content(parent, TAG_NUM_LANGUAGES, "%d", num_languages);
    }

    if (num_languages) {
        xmlNodePtr node = xml_add_node(parent, TAG_LANGUAGES);
        mirage_track_for_each_language(MIRAGE_TRACK(track), (MIRAGE_CallbackFunction)xml_dump_language, node, NULL);
    }

    return TRUE;
}

static gboolean xml_dump_session (GObject *session, xmlNodePtr parent)
{
    gint session_type, session_number;
    gint first_track;
    gint start_sector, length;
    gint leadout_length;
    gint num_tracks, num_languages;

    /* Make session node parent */
    parent = xml_add_node(parent, TAG_SESSION);

    if (mirage_session_get_session_type(MIRAGE_SESSION(session), &session_type, NULL)) {
        xml_add_node_with_content(parent, TAG_SESSION_TYPE, "0x%X", session_type);
    }

    if (mirage_session_layout_get_session_number(MIRAGE_SESSION(session), &session_number, NULL)) {
        xml_add_node_with_content(parent, TAG_SESSION_NUMBER, "%d", session_number);
    }

    if (mirage_session_layout_get_first_track(MIRAGE_SESSION(session), &first_track, NULL)) {
        xml_add_node_with_content(parent, TAG_FIRST_TRACK, "%d", first_track);
    }

    if (mirage_session_layout_get_start_sector(MIRAGE_SESSION(session), &start_sector, NULL)) {
        xml_add_node_with_content(parent, TAG_START_SECTOR, "%d", start_sector);
    }

    if (mirage_session_layout_get_length(MIRAGE_SESSION(session), &length, NULL)) {
        xml_add_node_with_content(parent, TAG_LENGTH, "%d", length);
    }

    if (mirage_session_get_leadout_length(MIRAGE_SESSION(session), &leadout_length, NULL)) {
        xml_add_node_with_content(parent, TAG_LEADOUT_LENGTH, "%d", leadout_length);
    }

    if (mirage_session_get_number_of_tracks(MIRAGE_SESSION(session), &num_tracks, NULL)) {
        xml_add_node_with_content(parent, TAG_NUM_TRACKS, "%d", num_tracks);
    }

    if (num_tracks) {
        xmlNodePtr node = xml_add_node(parent, TAG_TRACKS);
        mirage_session_for_each_track(MIRAGE_SESSION(session), (MIRAGE_CallbackFunction)xml_dump_track, node, NULL);
    }

    if (mirage_session_get_number_of_languages(MIRAGE_SESSION(session), &num_languages, NULL)) {
        xml_add_node_with_content(parent, TAG_NUM_LANGUAGES, "%d", num_languages);
    }

    if (num_languages) {
        xmlNodePtr node = xml_add_node(parent, TAG_LANGUAGES);
        mirage_session_for_each_language(MIRAGE_SESSION(session), (MIRAGE_CallbackFunction)xml_dump_language, node, NULL);
    }

    //mirage_session_get_cdtext_data(MIRAGE_SESSION(session), guint8 **data, gint *len, NULL);

    return TRUE;
}

static gboolean xml_dump_disc (GObject *disc, xmlNodePtr parent)
{
    gint medium_type;
    gchar **filenames;
    const gchar *mcn;
    gint first_session, first_track;
    gint start_sector, length;
    gint num_sessions, num_tracks;

    gint dpm_start, dpm_resolution, dpm_entries;
    const guint32 *dpm_data;

    /* Make disc node parent */
    parent = xml_add_node(parent, TAG_DISC);

    if (mirage_disc_get_medium_type(MIRAGE_DISC(disc), &medium_type, NULL)) {
        xml_add_node_with_content(parent, TAG_MEDIUM_TYPE, "0x%X", medium_type);
    }

    if (mirage_disc_get_filenames(MIRAGE_DISC(disc), &filenames, NULL)) {
        gint i = 0;
        xmlNodePtr node = xml_add_node(parent, TAG_FILENAMES);
        /* Filenames */
        while (filenames[i]) {
            xml_add_node_with_content(node, TAG_FILENAME, "%s", filenames[i]);
            i++;
        }
    }

    if (mirage_disc_get_mcn(MIRAGE_DISC(disc), &mcn, NULL)) {
        xml_add_node_with_content(parent, TAG_MCN, "%s", mcn);
    }

    if (mirage_disc_layout_get_first_session(MIRAGE_DISC(disc), &first_session, NULL)) {
        xml_add_node_with_content(parent, TAG_FIRST_SESSION, "%d", first_session);
    }

    if (mirage_disc_layout_get_first_track(MIRAGE_DISC(disc), &first_track, NULL)) {
        xml_add_node_with_content(parent, TAG_FIRST_TRACK, "%d", first_track);
    }

    if (mirage_disc_layout_get_start_sector(MIRAGE_DISC(disc), &start_sector, NULL)) {
        xml_add_node_with_content(parent, TAG_START_SECTOR, "%d", start_sector);
    }

    if (mirage_disc_layout_get_length(MIRAGE_DISC(disc), &length, NULL)) {
        xml_add_node_with_content(parent, TAG_LENGTH, "%d", length);
    }

    if (mirage_disc_get_number_of_sessions(MIRAGE_DISC(disc), &num_sessions, NULL)) {
        xml_add_node_with_content(parent, TAG_NUM_SESSIONS, "%d", num_sessions);
    }

    if (mirage_disc_get_number_of_tracks(MIRAGE_DISC(disc), &num_tracks, NULL)) {
        xml_add_node_with_content(parent, TAG_NUM_TRACKS, "%d", num_tracks);
    }

    if (num_sessions) {
        xmlNodePtr node = xml_add_node(parent, TAG_SESSIONS);
        mirage_disc_for_each_session(MIRAGE_DISC(disc), (MIRAGE_CallbackFunction)xml_dump_session, node, NULL);
    }

    if (mirage_disc_get_dpm_data(MIRAGE_DISC(disc), &dpm_start, &dpm_resolution, &dpm_entries, &dpm_data, NULL)) {
        gint i;
        xmlNodePtr node = xml_add_node(parent, TAG_DPM);

        /* DPM */
        xml_add_node_with_content(node, TAG_DPM_START, "%d", dpm_start);
        xml_add_node_with_content(node, TAG_DPM_RESOLUTION, "%d", dpm_resolution);
        xml_add_node_with_content(node, TAG_DPM_NUM_ENTRIES, "%d", dpm_entries);

        node = xml_add_node(node, TAG_DPM_ENTRIES);
        for (i = 0; i < dpm_entries; i++) {
            xml_add_node_with_content(node, TAG_DPM_ENTRY, "%d", dpm_data[i]);
        }
    }

    return TRUE;
}


/**********************************************************************\
 *                          Public function                           *
\**********************************************************************/
void image_analyzer_application_create_xml_dump (IMAGE_ANALYZER_Application *self)
{
    xmlNodePtr root_node;
    xmlDocPtr doc;

    /* Create new XML tree */
    doc = xmlNewDoc(BAD_CAST "1.0");
    root_node = xmlNewNode(NULL, BAD_CAST TAG_IMAGE_ANALYZER_DUMP);
    xmlDocSetRootElement(doc, root_node);

    /* Dump disc to XML tree */
    xml_dump_disc(self->priv->disc, root_node);

    /* Add parser log */
    xmlNewTextChild(root_node, NULL, BAD_CAST TAG_PARSER_LOG, BAD_CAST self->priv->parser_log->str);

    /* Store XML */
    self->priv->xml_doc = doc;
}
