/*
 *  MIRAGE Image Analyzer: Application object
 *  Copyright (C) 2007-2010 Rok Mandeljc
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

#include "image-analyzer-dump.h"
#include "image-analyzer-application.h"
#include "image-analyzer-parser-log.h"
#include "image-analyzer-sector-analysis.h"
#include "image-analyzer-sector-read.h"
#include "image-analyzer-disc-topology.h"

/* XML tags */
#define TAG_IMAGE_ANALYZER_DUMP "image-analyzer-dump"
#define TAG_PARSER_LOG "parser-log"

#define TAG_DISC "disc"
#define TAG_MEDIUM_TYPE "medium-type"
#define TAG_FILENAMES "filenames"
#define TAG_FILENAME "filename"
#define TAG_MCN "mcn"
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
#define TAG_MODE "mode"
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
#define TAG_FRAGMENT_ID "fragment-id"
#define TAG_ADDRESS "address"
#define TAG_LENGTH "length"
#define TAG_TFILE_HANDLE "tfile-handle"
#define TAG_TFILE_OFFSET "tfile-offset"
#define TAG_TFILE_SECTSIZE "tfile-sectsize"
#define TAG_TFILE_FORMAT "tfile-format"
#define TAG_SFILE_HANDLE "sfile-handle"
#define TAG_SFILE_OFFSET "sfile-offset"
#define TAG_SFILE_SECTSIZE "sfile-sectsize"
#define TAG_SFILE_FORMAT "sfile-format"
#define TAG_ADDRESS "address"
#define TAG_OFFSET "offset"

/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), IMAGE_ANALYZER_TYPE_APPLICATION, IMAGE_ANALYZER_ApplicationPrivate))

typedef struct {
    /* Disc */
    gboolean loaded;
    GObject *disc; /* Disc */

    /* Dialogs */
    GtkWidget *dialog_open_image;
    GtkWidget *dialog_open_dump;
    GtkWidget *dialog_save_dump;
    GtkWidget *dialog_parser;
    GtkWidget *dialog_sector;
    GtkWidget *dialog_analysis;
    GtkWidget *dialog_topology;

    /* Window */
    GtkWidget *window;

    /* UI Manager */
    GtkUIManager *ui_manager;

    /* Status bar */
    GtkWidget *statusbar;
    guint context_id;

    /* Model */
    GString *parser_log;
    GtkTreeStore *treestore;
    xmlDocPtr xml_doc;
} IMAGE_ANALYZER_ApplicationPrivate;

#define DEBUG_DOMAIN_PARSER "libMirage parser"


/******************************************************************************\
 *                                Dump functions                              *
\******************************************************************************/
static gchar *__dump_track_flags (gint track_flags) {
    static DUMP_Value values[] = {
        { MIRAGE_TRACKF_FOURCHANNEL, "four channel audio" },
        { MIRAGE_TRACKF_COPYPERMITTED, "copy permitted" },
        { MIRAGE_TRACKF_PREEMPHASIS, "pre-emphasis" },
    };

    return __dump_flags(track_flags, values, G_N_ELEMENTS(values));
}

static gchar *__dump_track_mode (gint track_mode) {
    static DUMP_Value values[] = {
        { MIRAGE_MODE_MODE0, "Mode 0" },
        { MIRAGE_MODE_AUDIO, "Audio" },
        { MIRAGE_MODE_MODE1, "Mode 1" },
        { MIRAGE_MODE_MODE2, "Mode 2 Formless" },
        { MIRAGE_MODE_MODE2_FORM1, "Mode 2 Form 1" },
        { MIRAGE_MODE_MODE2_FORM2, "Mode 2 Form 2" },
        { MIRAGE_MODE_MODE2_MIXED, "Mode 2 Mixed" },
    };

    return __dump_value(track_mode, values, G_N_ELEMENTS(values));
}

static gchar *__dump_session_type (gint session_type) {
    static DUMP_Value values[] = {
        { MIRAGE_SESSION_CD_ROM, "CD-DA/CD-ROM" },
        { MIRAGE_SESSION_CD_I, "CD-I" },
        { MIRAGE_SESSION_CD_ROM_XA, "CD-ROM XA" },
    };

    return __dump_value(session_type, values, G_N_ELEMENTS(values));
}

static gchar *__dump_medium_type (gint medium_type) {
    static DUMP_Value values[] = {
        { MIRAGE_MEDIUM_CD, "CD-ROM" },
        { MIRAGE_MEDIUM_DVD, "DVD-ROM" },
        { MIRAGE_MEDIUM_BD, "BD Disc" },
    };

    return __dump_value(medium_type, values, G_N_ELEMENTS(values));
}


static gchar *__dump_binary_fragment_tfile_format (gint format) {
    static DUMP_Value values[] = {
        { FR_BIN_TFILE_DATA, "Binary data" },
        { FR_BIN_TFILE_AUDIO, "Audio data" },
        { FR_BIN_TFILE_AUDIO_SWAP, "Audio data (swapped)" },
    };

    return __dump_flags(format, values, G_N_ELEMENTS(values));
}

static gchar *__dump_binary_fragment_sfile_format (gint format) {
    static DUMP_Value values[] = {
        { FR_BIN_SFILE_INT, "internal" },
        { FR_BIN_SFILE_EXT, "external" },

        { FR_BIN_SFILE_PW96_INT, "PW96 interleaved" },
        { FR_BIN_SFILE_PW96_LIN, "PW96 linear" },
        { FR_BIN_SFILE_RW96, "RW96" },
        { FR_BIN_SFILE_PQ16, "PQ16" },
    };

    return __dump_flags(format, values, G_N_ELEMENTS(values));
}

/******************************************************************************\
 *                              XML dump functions                            *
\******************************************************************************/
static xmlNodePtr __xml_add_node (xmlNodePtr parent, gchar *name) {
    return xmlNewChild(parent, NULL, BAD_CAST name, NULL);
}

static xmlAttrPtr __xml_add_attribute (xmlNodePtr node, gchar *name, gchar *format, ...) {
    va_list args;
    gchar content[255] = "";

    /* Create content string */
    va_start(args, format);
    g_vsnprintf(content, sizeof(content), format, args);
    va_end(args);

    /* Create the node */
    return xmlNewProp(node, BAD_CAST name, BAD_CAST content);
}

static xmlNodePtr __xml_add_node_with_content (xmlNodePtr parent, gchar *name, gchar *format, ...) {
    va_list args;
    gchar content[255] = "";

    /* Create content string */
    va_start(args, format);
    g_vsnprintf(content, sizeof(content), format, args);
    va_end(args);

    /* Create the node */
    return xmlNewTextChild(parent, NULL, BAD_CAST name, BAD_CAST content);
}


static gboolean __xml_dump_fragment (gpointer data, gpointer user_data) {
    GObject *fragment = data;
    xmlNodePtr parent = user_data;

    const MIRAGE_FragmentInfo *fragment_info;
    gint address, length;

    /* Make fragment node parent */
    parent = __xml_add_node(parent, TAG_FRAGMENT);

    if (mirage_fragment_get_fragment_info(MIRAGE_FRAGMENT(fragment), &fragment_info, NULL)) {
        __xml_add_node_with_content(parent, TAG_FRAGMENT_ID, "%s", fragment_info->id);
    }

    if (mirage_fragment_get_address(MIRAGE_FRAGMENT(fragment), &address, NULL)) {
        __xml_add_node_with_content(parent, TAG_ADDRESS, "%d", address);
    }

    if (mirage_fragment_get_length(MIRAGE_FRAGMENT(fragment), &length, NULL)) {
        __xml_add_node_with_content(parent, TAG_LENGTH, "%d", length);
    }

    if (MIRAGE_IS_FINTERFACE_NULL(fragment)) {
        /* Nothing to do here*/
    } else if (MIRAGE_IS_FINTERFACE_BINARY(fragment)) {
        FILE *tfile_handle, *sfile_handle;
        guint64 tfile_offset, sfile_offset;
        gint tfile_sectsize, sfile_sectsize;
        gint tfile_format, sfile_format;

        if (mirage_finterface_binary_track_file_get_handle(MIRAGE_FINTERFACE_BINARY(fragment), &tfile_handle, NULL)) {
            __xml_add_node_with_content(parent, TAG_TFILE_HANDLE, "%p", tfile_handle);
        }

        if (mirage_finterface_binary_track_file_get_offset(MIRAGE_FINTERFACE_BINARY(fragment), &tfile_offset, NULL)) {
            __xml_add_node_with_content(parent, TAG_TFILE_OFFSET, "%lld", tfile_offset);
        }

        if (mirage_finterface_binary_track_file_get_sectsize(MIRAGE_FINTERFACE_BINARY(fragment), &tfile_sectsize, NULL)) {
            __xml_add_node_with_content(parent, TAG_TFILE_SECTSIZE, "%d", tfile_sectsize);
        }

        if (mirage_finterface_binary_track_file_get_format(MIRAGE_FINTERFACE_BINARY(fragment), &tfile_format, NULL)) {
            __xml_add_node_with_content(parent, TAG_TFILE_FORMAT, "0x%X", tfile_format);
        }

        if (mirage_finterface_binary_subchannel_file_get_handle(MIRAGE_FINTERFACE_BINARY(fragment), &sfile_handle, NULL)) {
            __xml_add_node_with_content(parent, TAG_SFILE_HANDLE, "%p", sfile_handle);
        }

        if (mirage_finterface_binary_subchannel_file_get_offset(MIRAGE_FINTERFACE_BINARY(fragment), &sfile_offset, NULL)) {
            __xml_add_node_with_content(parent, TAG_SFILE_OFFSET, "%lld", sfile_offset);
        }

        if (mirage_finterface_binary_subchannel_file_get_sectsize(MIRAGE_FINTERFACE_BINARY(fragment), &sfile_sectsize, NULL)) {
            __xml_add_node_with_content(parent, TAG_SFILE_SECTSIZE, "%d", sfile_sectsize);
        }

        if (mirage_finterface_binary_subchannel_file_get_format(MIRAGE_FINTERFACE_BINARY(fragment), &sfile_format, NULL)) {
            __xml_add_node_with_content(parent, TAG_SFILE_FORMAT, "0x%X", sfile_format);
        }

    } else if (MIRAGE_IS_FINTERFACE_AUDIO(fragment)) {
        const gchar *filename;
        gint offset;

        if (mirage_finterface_audio_get_file(MIRAGE_FINTERFACE_AUDIO(fragment), &filename, NULL)) {
            __xml_add_node_with_content(parent, TAG_FILENAME, "%s", filename);
        }

        if (mirage_finterface_audio_get_offset(MIRAGE_FINTERFACE_AUDIO(fragment), &offset, NULL)) {
            __xml_add_node_with_content(parent, TAG_OFFSET, "%d", offset);
        }
    }

    return TRUE;
}

static gboolean __xml_dump_index (gpointer data, gpointer user_data) {
    GObject *index = data;
    xmlNodePtr parent = user_data;

    gint number, address;

    /* Make index node parent */
    parent = __xml_add_node(parent, TAG_INDEX);

    if (mirage_index_get_number(MIRAGE_INDEX(index), &number, NULL)) {
        __xml_add_node_with_content(parent, TAG_NUMBER, "%d", number);
    }

    if (mirage_index_get_address(MIRAGE_INDEX(index), &address, NULL)) {
        __xml_add_node_with_content(parent, TAG_ADDRESS, "%d", address);
    }

    return TRUE;
}

static gboolean __xml_dump_language (gpointer data, gpointer user_data) {
    GObject *language = data;
    xmlNodePtr parent = user_data;

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
    parent = __xml_add_node(parent, TAG_LANGUAGE);

    if (mirage_language_get_langcode(MIRAGE_LANGUAGE(language), &langcode, NULL)) {
        __xml_add_node_with_content(parent, TAG_LANGUAGE_CODE, "%d", langcode);
    }

    for (i = 0; i < G_N_ELEMENTS(pack_types); i++) {
        const gchar *data;
        gint len;
        if (mirage_language_get_pack_data(MIRAGE_LANGUAGE(language), pack_types[i].code, &data, &len, NULL)) {
            xmlNodePtr pack_node = __xml_add_node_with_content(parent, pack_types[i].tag, "%s", data);
            __xml_add_attribute(pack_node, ATTR_LENGTH, "%d", len);
        }
    }


    return TRUE;
}

static gboolean __xml_dump_track (gpointer data, gpointer user_data) {
    GObject *track = data;
    xmlNodePtr parent = user_data;

    gint flags, mode;
    gint adr, ctl;
    const gchar *isrc;
    gint session_number, track_number;
    gint start_sector, length;
    gint num_fragments;
    gint track_start;
    gint num_indices, num_languages;

    /* Make track node parent */
    parent = __xml_add_node(parent, TAG_TRACK);

    if (mirage_track_get_flags(MIRAGE_TRACK(track), &flags, NULL)) {
        __xml_add_node_with_content(parent, TAG_FLAGS, "0x%X", flags);
    }

    if (mirage_track_get_mode(MIRAGE_TRACK(track), &mode, NULL)) {
        __xml_add_node_with_content(parent, TAG_MODE, "0x%X", mode);
    }

    if (mirage_track_get_adr(MIRAGE_TRACK(track), &adr, NULL)) {
        __xml_add_node_with_content(parent, TAG_ADR, "%d", adr);
    }

    if (mirage_track_get_ctl(MIRAGE_TRACK(track), &ctl, NULL)) {
        __xml_add_node_with_content(parent, TAG_CTL, "%d", ctl);
    }

    if (mirage_track_get_isrc(MIRAGE_TRACK(track), &isrc, NULL)) {
        __xml_add_node_with_content(parent, TAG_ISRC, "%s", isrc);
    }

    if (mirage_track_layout_get_session_number(MIRAGE_TRACK(track), &session_number, NULL)) {
        __xml_add_node_with_content(parent, TAG_SESSION_NUMBER, "%d", session_number);
    }

    if (mirage_track_layout_get_track_number(MIRAGE_TRACK(track), &track_number, NULL)) {
        __xml_add_node_with_content(parent, TAG_TRACK_NUMBER, "%d", track_number);
    }

    if (mirage_track_layout_get_start_sector(MIRAGE_TRACK(track), &start_sector, NULL)) {
        __xml_add_node_with_content(parent, TAG_START_SECTOR, "%d", start_sector);
    }

    if (mirage_track_layout_get_length(MIRAGE_TRACK(track), &length, NULL)) {
        __xml_add_node_with_content(parent, TAG_LENGTH, "%d", length);
    }

    if (mirage_track_get_number_of_fragments(MIRAGE_TRACK(track), &num_fragments, NULL)) {
        __xml_add_node_with_content(parent, TAG_NUM_FRAGMENTS, "%d", num_fragments);
    }

    if (num_fragments) {
        xmlNodePtr node = __xml_add_node(parent, TAG_FRAGMENTS);
        mirage_track_for_each_fragment(MIRAGE_TRACK(track), __xml_dump_fragment, node, NULL);
    }

    if (mirage_track_get_track_start(MIRAGE_TRACK(track), &track_start, NULL)) {
        __xml_add_node_with_content(parent, TAG_TRACK_START, "%d", track_start);
    }

    if (mirage_track_get_number_of_indices(MIRAGE_TRACK(track), &num_indices, NULL)) {
        __xml_add_node_with_content(parent, TAG_NUM_INDICES, "%d", num_indices);
    }

    if (num_indices) {
        xmlNodePtr node = __xml_add_node(parent, TAG_INDICES);
        mirage_track_for_each_index(MIRAGE_TRACK(track), __xml_dump_index, node, NULL);
    }

    if (mirage_track_get_number_of_languages(MIRAGE_TRACK(track), &num_languages, NULL)) {
        __xml_add_node_with_content(parent, TAG_NUM_LANGUAGES, "%d", num_languages);
    }

    if (num_languages) {
        xmlNodePtr node = __xml_add_node(parent, TAG_LANGUAGES);
        mirage_track_for_each_language(MIRAGE_TRACK(track), __xml_dump_language, node, NULL);
    }

    return TRUE;
}


static gboolean __xml_dump_session (gpointer data, gpointer user_data) {
    GObject *session = data;
    xmlNodePtr parent = user_data;

    gint session_type, session_number;
    gint first_track;
    gint start_sector, length;
    gint leadout_length;
    gint num_tracks, num_languages;

    /* Make session node parent */
    parent = __xml_add_node(parent, TAG_SESSION);

    if (mirage_session_get_session_type(MIRAGE_SESSION(session), &session_type, NULL)) {
        __xml_add_node_with_content(parent, TAG_SESSION_TYPE, "0x%X", session_type);
    }

    if (mirage_session_layout_get_session_number(MIRAGE_SESSION(session), &session_number, NULL)) {
        __xml_add_node_with_content(parent, TAG_SESSION_NUMBER, "%d", session_number);
    }

    if (mirage_session_layout_get_first_track(MIRAGE_SESSION(session), &first_track, NULL)) {
        __xml_add_node_with_content(parent, TAG_FIRST_TRACK, "%d", first_track);
    }

    if (mirage_session_layout_get_start_sector(MIRAGE_SESSION(session), &start_sector, NULL)) {
        __xml_add_node_with_content(parent, TAG_START_SECTOR, "%d", start_sector);
    }

    if (mirage_session_layout_get_length(MIRAGE_SESSION(session), &length, NULL)) {
        __xml_add_node_with_content(parent, TAG_LENGTH, "%d", length);
    }

    if (mirage_session_get_leadout_length(MIRAGE_SESSION(session), &leadout_length, NULL)) {
        __xml_add_node_with_content(parent, TAG_LEADOUT_LENGTH, "%d", leadout_length);
    }

    if (mirage_session_get_number_of_tracks(MIRAGE_SESSION(session), &num_tracks, NULL)) {
        __xml_add_node_with_content(parent, TAG_NUM_TRACKS, "%d", num_tracks);
    }

    if (num_tracks) {
        xmlNodePtr node = __xml_add_node(parent, TAG_TRACKS);
        mirage_session_for_each_track(MIRAGE_SESSION(session), __xml_dump_track, node, NULL);
    }

    if (mirage_session_get_number_of_languages(MIRAGE_SESSION(session), &num_languages, NULL)) {
        __xml_add_node_with_content(parent, TAG_NUM_LANGUAGES, "%d", num_languages);
    }

    if (num_languages) {
        xmlNodePtr node = __xml_add_node(parent, TAG_LANGUAGES);
        mirage_session_for_each_language(MIRAGE_SESSION(session), __xml_dump_language, node, NULL);
    }

    //mirage_session_get_cdtext_data(MIRAGE_SESSION(session), guint8 **data, gint *len, NULL);

    return TRUE;
}

static gboolean __xml_dump_disc (gpointer data, gpointer user_data) {
    GObject *disc = data;
    xmlNodePtr parent = user_data;

    gint medium_type;
    gchar **filenames;
    const gchar *mcn;
    gint first_session, first_track;
    gint start_sector, length;
    gint num_sessions, num_tracks;

    gint dpm_start, dpm_resolution, dpm_entries;
    const guint32 *dpm_data;

    /* Make disc node parent */
    parent = __xml_add_node(parent, TAG_DISC);

    if (mirage_disc_get_medium_type(MIRAGE_DISC(disc), &medium_type, NULL)) {
        __xml_add_node_with_content(parent, TAG_MEDIUM_TYPE, "0x%X", medium_type);
    }

    if (mirage_disc_get_filenames(MIRAGE_DISC(disc), &filenames, NULL)) {
        gint i = 0;
        xmlNodePtr node = __xml_add_node(parent, TAG_FILENAMES);
        /* Filenames */
        while (filenames[i]) {
            __xml_add_node_with_content(node, TAG_FILENAME, "%s", filenames[i]);
            i++;
        }
    }

    if (mirage_disc_get_mcn(MIRAGE_DISC(disc), &mcn, NULL)) {
        __xml_add_node_with_content(parent, TAG_MCN, "%s", mcn);
    }

    if (mirage_disc_layout_get_first_session(MIRAGE_DISC(disc), &first_session, NULL)) {
        __xml_add_node_with_content(parent, TAG_FIRST_SESSION, "%d", first_session);
    }

    if (mirage_disc_layout_get_first_track(MIRAGE_DISC(disc), &first_track, NULL)) {
        __xml_add_node_with_content(parent, TAG_FIRST_TRACK, "%d", first_track);
    }

    if (mirage_disc_layout_get_start_sector(MIRAGE_DISC(disc), &start_sector, NULL)) {
        __xml_add_node_with_content(parent, TAG_START_SECTOR, "%d", start_sector);
    }

    if (mirage_disc_layout_get_length(MIRAGE_DISC(disc), &length, NULL)) {
        __xml_add_node_with_content(parent, TAG_LENGTH, "%d", length);
    }

    if (mirage_disc_get_number_of_sessions(MIRAGE_DISC(disc), &num_sessions, NULL)) {
        __xml_add_node_with_content(parent, TAG_NUM_SESSIONS, "%d", num_sessions);
    }

    if (mirage_disc_get_number_of_tracks(MIRAGE_DISC(disc), &num_tracks, NULL)) {
        __xml_add_node_with_content(parent, TAG_NUM_TRACKS, "%d", num_tracks);
    }

    if (num_sessions) {
        xmlNodePtr node = __xml_add_node(parent, TAG_SESSIONS);
        mirage_disc_for_each_session(MIRAGE_DISC(disc), __xml_dump_session, node, NULL);
    }

    if (mirage_disc_get_dpm_data(MIRAGE_DISC(disc), &dpm_start, &dpm_resolution, &dpm_entries, &dpm_data, NULL)) {
        gint i;
        xmlNodePtr node = __xml_add_node(parent, TAG_DPM);

        /* DPM */
        __xml_add_node_with_content(node, TAG_DPM_START, "%d", dpm_start);
        __xml_add_node_with_content(node, TAG_DPM_RESOLUTION, "%d", dpm_resolution);
        __xml_add_node_with_content(node, TAG_DPM_NUM_ENTRIES, "%d", dpm_entries);

        node = __xml_add_node(node, TAG_DPM_ENTRIES);
        for (i = 0; i < dpm_entries; i++) {
            __xml_add_node_with_content(node, TAG_DPM_ENTRY, "%d", dpm_data[i]);
        }
    }

    return TRUE;
}

static xmlDocPtr __xml_create_dump (GObject *disc, GString *parser_log) {
    xmlDocPtr doc;
    xmlNodePtr root_node;

    /* Create new XML tree */
    doc = xmlNewDoc(BAD_CAST "1.0");
    root_node = xmlNewNode(NULL, BAD_CAST TAG_IMAGE_ANALYZER_DUMP);
    xmlDocSetRootElement(doc, root_node);

    /* Dump disc to XML tree */
    __xml_dump_disc(disc, root_node);

    /* Add parser log */
    xmlNewTextChild(root_node, NULL, BAD_CAST TAG_PARSER_LOG, BAD_CAST parser_log->str);

    return doc;
}
/******************************************************************************\
 *                                 XML loading                                *
\******************************************************************************/
static inline gchar *__xml_node_get_string (xmlNodePtr node) {
    xmlChar *xml_str = xmlNodeGetContent(node);
    gchar *ret_str = g_strdup((gchar *)xml_str);
    xmlFree(xml_str);
    return ret_str;
}

static inline gdouble __xml_node_get_attr_double (xmlNodePtr node, gchar *attr) {
    xmlChar *xml_str = xmlGetProp(node, BAD_CAST attr);
    gdouble ret_double = g_strtod((gchar *)xml_str, NULL);
    xmlFree(xml_str);
    return ret_double;
}

static inline gdouble __xml_node_get_double (xmlNodePtr node) {
    xmlChar *xml_str = xmlNodeGetContent(node);
    gdouble ret_double = g_strtod((gchar *)xml_str, NULL);
    xmlFree(xml_str);
    return ret_double;
}

static inline guint64 __xml_node_get_uint64 (xmlNodePtr node) {
    xmlChar *xml_str = xmlNodeGetContent(node);
    guint64 ret_uint64 = g_ascii_strtoull((gchar *)xml_str, NULL, 0);
    xmlFree(xml_str);
    return ret_uint64;
}

static void __treestore_add_node (GtkTreeStore *treestore, GtkTreeIter *parent, GtkTreeIter *node, gchar *format, ...) {
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

static void __treestore_dump_fragment (GtkTreeStore *treestore, GtkTreeIter *parent, xmlNodePtr xml_node) {
    GtkTreeIter node;
    xmlNodePtr cur_node;

    __treestore_add_node(treestore, parent, &node, "Fragment");

    /* Go over nodes */
    for (cur_node = xml_node->children; cur_node; cur_node = cur_node->next) {
        /* Skip non-element nodes */
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_FRAGMENT_ID)) {
            gchar *fragment_id = __xml_node_get_string(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Fragment ID: %s", fragment_id);
            g_free(fragment_id);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_ADDRESS)) {
            gint address = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Address: %d (0x%X)", address, address);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_LENGTH)) {
            gint length = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Length: %d (0x%X)", length, length);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_TFILE_HANDLE)) {
            FILE *tfile_handle = (FILE *)__xml_node_get_uint64(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Track file: Handle: %p", tfile_handle);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_TFILE_OFFSET)) {
            guint64 tfile_offset = __xml_node_get_uint64(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Track file: Offset: %lli (0x%llX)", tfile_offset, tfile_offset);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_TFILE_SECTSIZE)) {
            gint tfile_sectsize  = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Track file: Sector size: %d (0x%X)", tfile_sectsize, tfile_sectsize);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_TFILE_FORMAT)) {
            gint tfile_format = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Track file: Format: 0x%X (%s)", tfile_format, __dump_binary_fragment_tfile_format(tfile_format));
        }  else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_SFILE_HANDLE)) {
            FILE *sfile_handle = (FILE *)__xml_node_get_uint64(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Subchannel file: Handle: %p", sfile_handle);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_SFILE_OFFSET)) {
            guint64 sfile_offset = __xml_node_get_uint64(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Subchannel file: Offset: %lli (0x%llX)", sfile_offset, sfile_offset);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_SFILE_SECTSIZE)) {
            gint sfile_sectsize  = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Subchannel file: Sector size: %d (0x%X)", sfile_sectsize, sfile_sectsize);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_SFILE_FORMAT)) {
            gint sfile_format = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Subchannel file: Format: 0x%X (%s)", sfile_format, __dump_binary_fragment_sfile_format(sfile_format));
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_FILENAME)) {
            gchar *filename = __xml_node_get_string(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Filename: %s", filename);
            g_free(filename);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_OFFSET)) {
            gint offset = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Offset (sectors): %d (0x%X)", offset, offset);
        }
    }
}

static void __treestore_dump_index (GtkTreeStore *treestore, GtkTreeIter *parent, xmlNodePtr xml_node) {
    GtkTreeIter node;
    xmlNodePtr cur_node;

    __treestore_add_node(treestore, parent, &node, "Index");

    /* Go over nodes */
    for (cur_node = xml_node->children; cur_node; cur_node = cur_node->next) {
        /* Skip non-element nodes */
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_NUMBER)) {
            gint number = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Number: %d", number);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_ADDRESS)) {
            gint address = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Address: %d (0x%X)", address, address);
        }
    }
}

static void __treestore_dump_language (GtkTreeStore *treestore, GtkTreeIter *parent, xmlNodePtr xml_node) {
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

    __treestore_add_node(treestore, parent, &node, "Language");

    /* Go over nodes */
    for (cur_node = xml_node->children; cur_node; cur_node = cur_node->next) {
        /* Skip non-element nodes */
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_LANGUAGE_CODE)) {
            gint langcode = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Language code: %d", langcode);
        } else {
            gint i;

            for (i = 0; i < G_N_ELEMENTS(pack_types); i++) {
                if (!g_ascii_strcasecmp((gchar *)cur_node->name, pack_types[i].tag)) {
                    gchar *data = __xml_node_get_string(cur_node);
                    gint len = __xml_node_get_attr_double(cur_node, ATTR_LENGTH);
                    __treestore_add_node(treestore, &node, NULL, "%s: %s (%i)", pack_types[i].name, data, len);
                    g_free(data);
                }
            }
        }
    }
}

static void __treestore_dump_track (GtkTreeStore *treestore, GtkTreeIter *parent, xmlNodePtr xml_node) {
    GtkTreeIter node;
    xmlNodePtr cur_node;

    __treestore_add_node(treestore, parent, &node, "Track");

    /* Go over nodes */
    for (cur_node = xml_node->children; cur_node; cur_node = cur_node->next) {
        /* Skip non-element nodes */
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_FLAGS)) {
            gint flags = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Flags: 0x%X (%s)", flags, __dump_track_flags(flags));
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_MODE)) {
            gint mode = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Mode: 0x%X (%s)", mode, __dump_track_mode(mode));
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_ADR)) {
            gint adr = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "ADR: %d", adr);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_CTL)) {
            gint ctl = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "CTL: %d", ctl);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_ISRC)) {
            gchar *isrc = __xml_node_get_string(cur_node);
            __treestore_add_node(treestore, &node, NULL, "ISRC: %s", isrc);
            g_free(isrc);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_SESSION_NUMBER)) {
            gint session_number = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Session number: %d", session_number);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_TRACK_NUMBER)) {
            gint track_number = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Track number: %d", track_number);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_START_SECTOR)) {
            gint start_sector = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Start sector: %d (0x%X)", start_sector, start_sector);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_LENGTH)) {
            gint length = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Length: %d (0x%X)", length, length);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_NUM_FRAGMENTS)) {
            gint num_fragments = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Number of fragments: %d", num_fragments);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_FRAGMENTS)) {
            xmlNodePtr child_node;
            GtkTreeIter fragments;

            __treestore_add_node(treestore, &node, &fragments, "Fragments");
            for (child_node = cur_node->children; child_node; child_node = child_node->next) {
                if (child_node->type != XML_ELEMENT_NODE) {
                    continue;
                }

                if (!g_ascii_strcasecmp((gchar *)child_node->name, TAG_FRAGMENT)) {
                    __treestore_dump_fragment(treestore, &fragments, child_node);
                }
            }
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_TRACK_START)) {
            gint track_start = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Track start: %d (0x%X)", track_start, track_start);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_NUM_INDICES)) {
            gint num_indices = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Number of indices: %d", num_indices);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_INDICES)) {
            xmlNodePtr child_node;
            GtkTreeIter indices;

            __treestore_add_node(treestore, &node, &indices, "Indices");
            for (child_node = cur_node->children; child_node; child_node = child_node->next) {
                if (child_node->type != XML_ELEMENT_NODE) {
                    continue;
                }

                if (!g_ascii_strcasecmp((gchar *)child_node->name, TAG_INDEX)) {
                    __treestore_dump_index(treestore, &indices, child_node);
                }
            }
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_NUM_LANGUAGES)) {
            gint num_languages = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Number of languages: %d", num_languages);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_LANGUAGES)) {
            xmlNodePtr child_node;
            GtkTreeIter languages;

            __treestore_add_node(treestore, &node, &languages, "Languages");
            for (child_node = cur_node->children; child_node; child_node = child_node->next) {
                if (child_node->type != XML_ELEMENT_NODE) {
                    continue;
                }

                if (!g_ascii_strcasecmp((gchar *)child_node->name, TAG_LANGUAGE)) {
                    __treestore_dump_language(treestore, &languages, child_node);
                }
            }
        }
    }
}

static void __treestore_dump_session (GtkTreeStore *treestore, GtkTreeIter *parent, xmlNodePtr xml_node) {
    GtkTreeIter node;
    xmlNodePtr cur_node;

    __treestore_add_node(treestore, parent, &node, "Session");

    /* Go over nodes */
    for (cur_node = xml_node->children; cur_node; cur_node = cur_node->next) {
        /* Skip non-element nodes */
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_SESSION_TYPE)) {
            gint session_type = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Session type: 0x%X (%s)", session_type, __dump_session_type(session_type));
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_SESSION_NUMBER)) {
            gint session_number = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Session number: %d", session_number);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_FIRST_TRACK)) {
            gint first_track = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Layout: First track: %d", first_track);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_START_SECTOR)) {
            gint start_sector = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Layout: Start sector: %d (0x%X)", start_sector);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_LENGTH)) {
            gint length = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Layout: Length: %d (0x%X)", length, length);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_LEADOUT_LENGTH)) {
            gint leadout_length = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Leadout length: %d (0x%X)", leadout_length, leadout_length);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_NUM_TRACKS)) {
            gint num_tracks = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Number of tracks: %d", num_tracks);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_TRACKS)) {
            xmlNodePtr child_node;
            GtkTreeIter tracks;

            __treestore_add_node(treestore, &node, &tracks, "Tracks");
            for (child_node = cur_node->children; child_node; child_node = child_node->next) {
                if (child_node->type != XML_ELEMENT_NODE) {
                    continue;
                }

                if (!g_ascii_strcasecmp((gchar *)child_node->name, TAG_TRACK)) {
                    __treestore_dump_track(treestore, &tracks, child_node);
                }
            }
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_NUM_LANGUAGES)) {
            gint num_languages = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Number of languages: %d", num_languages);
        }  else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_LANGUAGES)) {
            xmlNodePtr child_node;
            GtkTreeIter languages;

            __treestore_add_node(treestore, &node, &languages, "Languages");
            for (child_node = cur_node->children; child_node; child_node = child_node->next) {
                __treestore_dump_language(treestore, &languages, child_node);
            }
        }
    }
}

static void __treestore_dump_dpm (GtkTreeStore *treestore, GtkTreeIter *parent, xmlNodePtr xml_node) {
    GtkTreeIter node;
    xmlNodePtr cur_node;

    __treestore_add_node(treestore, parent, &node, "DPM");

    /* Go over nodes */
    for (cur_node = xml_node->children; cur_node; cur_node = cur_node->next) {
        /* Skip non-element nodes */
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_DPM_START)) {
            gint dpm_start = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Start sector: %d (0x%X)", dpm_start, dpm_start);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_DPM_RESOLUTION)) {
            gint dpm_resolution = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Resolution: %d (0x%X)", dpm_resolution, dpm_resolution);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_DPM_NUM_ENTRIES)) {
            gint dpm_entries = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Number of entries: %d (0x%X)", dpm_entries, dpm_entries);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_DPM_ENTRIES)) {
            xmlNodePtr child_node;
            GtkTreeIter dpm_entries;

            __treestore_add_node(treestore, &node, &dpm_entries, "Data entries");
            for (child_node = cur_node->children; child_node; child_node = child_node->next) {
                if (child_node->type != XML_ELEMENT_NODE) {
                    continue;
                }

                if (!g_ascii_strcasecmp((gchar *)child_node->name, TAG_DPM_ENTRY)) {
                    guint32 dpm_entry = __xml_node_get_double(child_node);
                    __treestore_add_node(treestore, &dpm_entries, NULL, "0x%08X", dpm_entry);
                }
            }
        }
    }
}

static void __treestore_dump_disc (GtkTreeStore *treestore, GtkTreeIter *parent, xmlNodePtr xml_node) {
    GtkTreeIter node;
    xmlNodePtr cur_node;

    __treestore_add_node(treestore, parent, &node, "Disc");

    /* Go over nodes */
    for (cur_node = xml_node->children; cur_node; cur_node = cur_node->next) {
        /* Skip non-element nodes */
        if (cur_node->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_MEDIUM_TYPE)) {
            gint medium_type = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Medium type: 0x%X (%s)", medium_type, __dump_medium_type(medium_type));
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_FILENAMES)) {
            xmlNodePtr child_node;
            GtkTreeIter files;

            __treestore_add_node(treestore, &node, &files, "Filename(s)");

            for (child_node = cur_node->children; child_node; child_node = child_node->next) {
                if (child_node->type != XML_ELEMENT_NODE) {
                    continue;
                }

                if (!g_ascii_strcasecmp((gchar *)child_node->name, TAG_FILENAME)) {
                    gchar *filename = __xml_node_get_string(child_node);
                    __treestore_add_node(treestore, &files, NULL, "%s", filename);
                    g_free(filename);
                }
            }
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_MCN)) {
            gchar *mcn = __xml_node_get_string(cur_node);
            __treestore_add_node(treestore, &node, NULL, "MCN: %s", mcn);
            g_free(mcn);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_FIRST_SESSION)) {
            gint first_session = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Layout: First session: %d", first_session);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_FIRST_TRACK)) {
            gint first_track = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Layout: First track: %d", first_track);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_START_SECTOR)) {
            gint start_sector = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Layout: Start sector: %d (0x%X)", start_sector, start_sector);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_LENGTH)) {
            gint length = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Layout: Length: %d (0x%X)", length, length);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_NUM_SESSIONS)) {
            gint num_sessions = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Number of sessions: %d", num_sessions);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_NUM_TRACKS)) {
            gint num_tracks = __xml_node_get_double(cur_node);
            __treestore_add_node(treestore, &node, NULL, "Number of tracks: %d", num_tracks);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_SESSIONS)) {
            xmlNodePtr child_node;
            GtkTreeIter sessions;

            __treestore_add_node(treestore, &node, &sessions, "Sessions");
            for (child_node = cur_node->children; child_node; child_node = child_node->next) {
                if (child_node->type != XML_ELEMENT_NODE) {
                    continue;
                }

                if (!g_ascii_strcasecmp((gchar *)child_node->name, TAG_SESSION)) {
                    __treestore_dump_session(treestore, &sessions, child_node);
                }
            }
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_DPM)) {
            __treestore_dump_dpm(treestore, &node, cur_node);
        }
    }
}



static gboolean __image_analyzer_application_load_xml_tree (IMAGE_ANALYZER_Application *self) {
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);
    xmlNodePtr root_node;
    xmlNodePtr cur_node;

    /* Make sure XML tree is valid */
    if (!_priv->xml_doc) {
        return FALSE;
    }

    /* Get root element and verify it */
    root_node = xmlDocGetRootElement(_priv->xml_doc);
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
            __treestore_dump_disc(_priv->treestore, NULL, cur_node);
        } else if (!g_ascii_strcasecmp((gchar *)cur_node->name, TAG_PARSER_LOG)) {
            gchar *log = __xml_node_get_string(cur_node);
            image_analyzer_parser_log_append_to_log(IMAGE_ANALYZER_PARSER_LOG(_priv->dialog_parser), log, NULL);
            g_free(log);
        }
    }


    return TRUE;
}

/******************************************************************************\
 *                              Logging redirection                           *
\******************************************************************************/
static void __capture_parser_log (const gchar *log_domain G_GNUC_UNUSED, GLogLevelFlags log_level G_GNUC_UNUSED, const gchar *message, gpointer user_data) {
    IMAGE_ANALYZER_Application *self = IMAGE_ANALYZER_APPLICATION(user_data);
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);

    /* Append to our log string */
    _priv->parser_log = g_string_append(_priv->parser_log, message);

    return;
}


/******************************************************************************\
 *                                Status message                              *
\******************************************************************************/
static void __image_analyzer_application_message (IMAGE_ANALYZER_Application *self, gchar *format, ...) {
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);
    gchar *message;
    va_list args;

    /* Pop message (so that anything set previously will be removed */
    gtk_statusbar_pop(GTK_STATUSBAR(_priv->statusbar), _priv->context_id);

    /* Push message */
    va_start(args, format);
    message = g_strdup_vprintf(format, args);
    va_end(args);

    gtk_statusbar_pop(GTK_STATUSBAR(_priv->statusbar), _priv->context_id);
    gtk_statusbar_push(GTK_STATUSBAR(_priv->statusbar), _priv->context_id, message);

    g_free(message);

    return;
}


/******************************************************************************\
 *                               Open/close image                             *
\******************************************************************************/
static gchar *__image_analyzer_application_get_password (gpointer user_data) {
    IMAGE_ANALYZER_Application *self = user_data;
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);
    gchar *password;
    GtkDialog *dialog;
    GtkWidget *hbox, *entry, *label;
    gint result;

    dialog = GTK_DIALOG(gtk_dialog_new_with_buttons(
        "Enter password",
        GTK_WINDOW(_priv->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_STOCK_OK, GTK_RESPONSE_OK,
        GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
        NULL));
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

    gtk_box_set_spacing(GTK_BOX(gtk_dialog_get_content_area(dialog)), 5);

    label = gtk_label_new("The image you are trying to load is encrypted.");
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(dialog)), label, TRUE, TRUE, 0);

    entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);

    hbox = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new("Password: "), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(dialog)), hbox, FALSE, FALSE, 0);

    /* Run dialog */
    gtk_widget_show_all(GTK_WIDGET(dialog));
    result = gtk_dialog_run(dialog);
    switch (result) {
        case GTK_RESPONSE_OK: {
            password = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
            break;
        }
        default: {
            password = NULL;
            break;
        }
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));

    return password;
}

static gboolean __image_analyzer_application_close_image_or_dump (IMAGE_ANALYZER_Application *self) {
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);

    /* Clear log whether we're loaded or not... it doesn't really hurt to do it
       before the check, and it ensures the log is always cleared (i.e. if load
       call failed, we'd have error log but it wouldn't be cleared on subsequent
       load call... */
    image_analyzer_parser_log_clear_log(IMAGE_ANALYZER_PARSER_LOG(_priv->dialog_parser), NULL);

    /* Clear disc topology */
    image_analyzer_disc_topology_refresh(IMAGE_ANALYZER_DISC_TOPOLOGY(_priv->dialog_topology), NULL, NULL);

    /* Clear TreeStore */
    gtk_tree_store_clear(_priv->treestore);

    /* Free XML doc */
    if (_priv->xml_doc) {
        xmlFreeDoc(_priv->xml_doc);
        _priv->xml_doc = NULL;
    }

    /* Release disc reference */
    if (_priv->disc) {
        g_object_unref(_priv->disc);
        _priv->disc = NULL;
    }

    /* Print message only if something was loaded */
    if (_priv->loaded) {
        __image_analyzer_application_message(self, "Image/dump closed.");
    }

    _priv->loaded = FALSE;

    return TRUE;
}

static gboolean __image_analyzer_application_open_image (IMAGE_ANALYZER_Application *self, gchar **filenames) {
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);
    GObject *debug_context;
    guint log_handler;
    GError *error = NULL;

    /* Close any opened image or dump */
    __image_analyzer_application_close_image_or_dump(self);

    /* Create debug context for disc */
    debug_context = g_object_new(MIRAGE_TYPE_DEBUG_CONTEXT, NULL);
    mirage_debug_context_set_domain(MIRAGE_DEBUG_CONTEXT(debug_context), DEBUG_DOMAIN_PARSER, NULL);
    mirage_debug_context_set_debug_mask(MIRAGE_DEBUG_CONTEXT(debug_context), MIRAGE_DEBUG_PARSER, NULL);

    /* Set log handler */
    _priv->parser_log = g_string_new("");
    log_handler = g_log_set_handler(DEBUG_DOMAIN_PARSER, G_LOG_LEVEL_MASK, __capture_parser_log, self);

    /* Create disc */
    _priv->disc = libmirage_create_disc(filenames, debug_context, NULL, &error);
    if (!_priv->disc) {
        g_warning("Failed to create disc: %s\n", error->message);
        __image_analyzer_application_message(self, "Failed to open image: %s", error->message);
        g_error_free(error);

        /* Manually fill in the log */
        image_analyzer_parser_log_append_to_log(IMAGE_ANALYZER_PARSER_LOG(_priv->dialog_parser), _priv->parser_log->str, NULL);

        return FALSE;
    }

    /* Release reference to debug context; it's attached to disc now */
    g_object_unref(debug_context);

    /* Remove log handler */
    g_log_remove_handler(DEBUG_DOMAIN_PARSER, log_handler);

    /* Disc topology */
    image_analyzer_disc_topology_refresh(IMAGE_ANALYZER_DISC_TOPOLOGY(_priv->dialog_topology), _priv->disc, NULL);

    /* Make XML dump */
    _priv->xml_doc = __xml_create_dump(_priv->disc, _priv->parser_log);
    if (!_priv->xml_doc) {
        g_warning("Failed to dump disc!\n");
        __image_analyzer_application_message(self, "Failed to dump disc!");
        return FALSE;
    }

    /* Free parser log string */
    g_string_free(_priv->parser_log, TRUE);

    /* Convert XML to treestore */
    __image_analyzer_application_load_xml_tree(self);

    _priv->loaded = TRUE;

    __image_analyzer_application_message(self, "Image successfully opened.");

    return TRUE;
}

static gboolean __image_analyzer_application_open_dump (IMAGE_ANALYZER_Application *self, gchar *filename) {
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);

    /* Close any opened image or dump */
    __image_analyzer_application_close_image_or_dump(self);

    /* Load XML */
    _priv->xml_doc = xmlReadFile(filename, NULL, 0);
    if (!_priv->xml_doc) {
        g_warning("Failed to dump disc!\n");
        __image_analyzer_application_message(self, "Failed to load dump!");
        return FALSE;
    }

    /* Convert XML to treestore */
    __image_analyzer_application_load_xml_tree(self);

    _priv->loaded = TRUE;

    __image_analyzer_application_message(self, "Dump successfully opened.");

    return TRUE;
}

static gboolean __image_analyzer_application_save_dump (IMAGE_ANALYZER_Application *self, gchar *filename) {
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);

    /* Save the XML tree */
    xmlSaveFormatFileEnc(filename, _priv->xml_doc, "UTF-8", 1);

    __image_analyzer_application_message(self, "Dump successfully saved.");

    return TRUE;
}


/******************************************************************************\
 *                                 UI callbacks                               *
\******************************************************************************/
static void __ui_callback_open_image (GtkAction *action G_GNUC_UNUSED, gpointer user_data) {
    IMAGE_ANALYZER_Application *self = IMAGE_ANALYZER_APPLICATION(user_data);
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);

    /* Run the dialog */
    if (gtk_dialog_run(GTK_DIALOG(_priv->dialog_open_image)) == GTK_RESPONSE_ACCEPT) {
        GSList *filenames_list;
        GSList *entry;
        gint num_filenames;
        gchar **filenames;
        gint i = 0;

        /* Get filenames from dialog */
        filenames_list = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(_priv->dialog_open_image));

        /* Create strings array */
        num_filenames = g_slist_length(filenames_list);
        filenames = g_new0(gchar *, num_filenames + 1); /* NULL-terminated */

        /* GSList -> strings array conversion */
        entry = filenames_list;
        while (entry != NULL) {
            filenames[i++] = entry->data;
            entry = g_slist_next(entry);
        }

        /* Open image */
        __image_analyzer_application_open_image(self, filenames);

        /* Free filenames list */
        g_slist_free(filenames_list);
        /* Free filenames array */
        g_strfreev(filenames);
    }

    gtk_widget_hide(GTK_WIDGET(_priv->dialog_open_image));

    return;
}

static void __ui_callback_open_dump (GtkAction *action G_GNUC_UNUSED, gpointer user_data) {
    IMAGE_ANALYZER_Application *self = IMAGE_ANALYZER_APPLICATION(user_data);
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);

    /* Run the dialog */
    if (gtk_dialog_run(GTK_DIALOG(_priv->dialog_open_dump)) == GTK_RESPONSE_ACCEPT) {
        gchar *filename;

        /* Get filenames from dialog */
        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(_priv->dialog_open_dump));

        /* Open dump */
        __image_analyzer_application_open_dump(self, filename);

        /* Free filename */
        g_free(filename);
    }

    gtk_widget_hide(GTK_WIDGET(_priv->dialog_open_dump));

    return;
}

static void __ui_callback_save_dump (GtkAction *action G_GNUC_UNUSED, gpointer user_data) {
    IMAGE_ANALYZER_Application *self = IMAGE_ANALYZER_APPLICATION(user_data);
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);

    /* We need an opened image or dump for this */
    if (!_priv->loaded) {
        return;
    }

    /* Run the dialog */
    if (gtk_dialog_run(GTK_DIALOG(_priv->dialog_save_dump)) == GTK_RESPONSE_ACCEPT) {
        gchar *filename;

        /* Get filenames from dialog */
        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(_priv->dialog_save_dump));

        /* Save */
        __image_analyzer_application_save_dump(self, filename);

        /* Free filename */
        g_free(filename);
    }

    gtk_widget_hide(GTK_WIDGET(_priv->dialog_save_dump));

    return;
}

static void __ui_callback_close (GtkAction *action G_GNUC_UNUSED, gpointer user_data) {
    IMAGE_ANALYZER_Application *self = IMAGE_ANALYZER_APPLICATION(user_data);
    /*IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);*/

    __image_analyzer_application_close_image_or_dump(self);

    return;
}

static void __ui_callback_quit (GtkAction *action G_GNUC_UNUSED, gpointer user_data G_GNUC_UNUSED) {
    gtk_main_quit();
    return;
}

static void __ui_callback_parser_log (GtkAction *action G_GNUC_UNUSED, gpointer user_data) {
    IMAGE_ANALYZER_Application *self = IMAGE_ANALYZER_APPLICATION(user_data);
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);

    /* Make window (re)appear by first hiding, then showing it */
    gtk_widget_hide(_priv->dialog_parser);
    gtk_widget_show_all(_priv->dialog_parser);

    return;
}

static void __ui_callback_sector (GtkAction *action G_GNUC_UNUSED, gpointer user_data) {
    IMAGE_ANALYZER_Application *self = IMAGE_ANALYZER_APPLICATION(user_data);
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);

    /* Make window (re)appear by first hiding, then showing it */
    gtk_widget_hide(_priv->dialog_sector);
    gtk_widget_show_all(_priv->dialog_sector);

    return;
}

static void __ui_callback_analysis (GtkAction *action G_GNUC_UNUSED, gpointer user_data) {
    IMAGE_ANALYZER_Application *self = IMAGE_ANALYZER_APPLICATION(user_data);
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);

    /* Make window (re)appear by first hiding, then showing it */
    gtk_widget_hide(_priv->dialog_analysis);
    gtk_widget_show_all(_priv->dialog_analysis);

    return;
}

static void __ui_callback_topology (GtkAction *action G_GNUC_UNUSED, gpointer user_data) {
    IMAGE_ANALYZER_Application *self = IMAGE_ANALYZER_APPLICATION(user_data);
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);

    /* Make window (re)appear by first hiding, then showing it */
    gtk_widget_hide(_priv->dialog_topology);
    gtk_widget_show_all(_priv->dialog_topology);

    return;
}

static void __ui_callback_about (GtkAction *action G_GNUC_UNUSED, gpointer user_data) {
    IMAGE_ANALYZER_Application *self = IMAGE_ANALYZER_APPLICATION(user_data);
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);

    gchar *authors[] = { "Rok Mandeljc <rok.mandeljc@gmail.com>", NULL };

    gtk_show_about_dialog(
        GTK_WINDOW(_priv->window),
        "name", "Image Analyzer",
        "comments", "Image Analyzer displays tree structure of disc image created by libMirage.",
        "version", PACKAGE_VERSION,
        "authors", authors,
        "copyright", "Copyright (C) 2007-2012 Rok Mandeljc",
        NULL);

    return;
}


static gboolean __cb_window_delete_event (GtkWidget *widget G_GNUC_UNUSED, GdkEvent *event G_GNUC_UNUSED, gpointer user_data G_GNUC_UNUSED) {
    /* Quit the app */
    gtk_main_quit();
    /* Don't invoke other handlers, we'll cleanup stuff ourselves */
    return TRUE;
}


/******************************************************************************\
 *                               GUI build helpers                            *
\******************************************************************************/
typedef struct {
    GtkWidget *dialog;
    GtkFileFilter *all_images;
} IMAGE_ANALYZER_FilterContext;

static gboolean __add_file_filter (gpointer data, gpointer user_data) {
    IMAGE_ANALYZER_FilterContext *context = user_data;
    MIRAGE_ParserInfo *parser_info = data;

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, parser_info->description);

    /* Per-parser filter */
    gtk_file_filter_add_mime_type(filter, parser_info->mime_type);
    /* "All images" filter */
    gtk_file_filter_add_mime_type(context->all_images, parser_info->mime_type);

    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(context->dialog), filter);

    return TRUE;
}

static GtkWidget *__build_dialog_open_image (IMAGE_ANALYZER_Application *self) {
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);
    IMAGE_ANALYZER_FilterContext context;

    GtkWidget *dialog;
    GtkFileFilter *filter;

    dialog = gtk_file_chooser_dialog_new(
        "Open File",
        GTK_WINDOW(_priv->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
        NULL);

    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);

    /* "All files" filter */
    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "All files");
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    /* "All images" filter */
    filter= gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "All images");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    /* Per-parser filters */
    context.dialog = dialog;
    context.all_images = filter;
    libmirage_for_each_parser(__add_file_filter, &context, NULL);

    return dialog;
}

static GtkWidget *__build_dialog_open_dump (IMAGE_ANALYZER_Application *self) {
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);

    GtkWidget *dialog;
    GtkFileFilter *filter;

    dialog = gtk_file_chooser_dialog_new(
        "Open File",
        GTK_WINDOW(_priv->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
        NULL);

    /* "XML files" filter */
    filter= gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "XML files");
    gtk_file_filter_add_pattern(filter, "*.xml");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    /* "All files" filter */
    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "All files");
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    return dialog;
}

static GtkWidget *__build_dialog_save_dump (IMAGE_ANALYZER_Application *self) {
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);

    GtkWidget *dialog;
    GtkFileFilter *filter;

    dialog = gtk_file_chooser_dialog_new(
        "Save File",
        GTK_WINDOW(_priv->window),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
        NULL);

    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);

    /* "XML files" filter */
    filter= gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "XML files");
    gtk_file_filter_add_pattern(filter, "*.xml");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    /* "All files" filter */
    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "All files");
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    return dialog;
}

static GtkWidget *__build_dialog_parser () {
    GtkWidget *dialog;
    dialog = g_object_new(IMAGE_ANALYZER_TYPE_PARSER_LOG, NULL);
    g_signal_connect(dialog, "delete_event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    return dialog;
}

static GtkWidget *__build_dialog_sector (IMAGE_ANALYZER_Application *self) {
    GtkWidget *dialog;
    dialog = g_object_new(IMAGE_ANALYZER_TYPE_SECTOR_READ, "application", self, NULL);
    g_signal_connect(dialog, "delete_event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    return dialog;
}

static GtkWidget *__build_dialog_analysis (IMAGE_ANALYZER_Application *self) {
    GtkWidget *dialog;
    dialog = g_object_new(IMAGE_ANALYZER_TYPE_SECTOR_ANALYSIS, "application", self, NULL);
    g_signal_connect(dialog, "delete_event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    return dialog;
}

static GtkWidget *__build_dialog_topology () {
    GtkWidget *dialog;
    dialog = g_object_new(IMAGE_ANALYZER_TYPE_DISC_TOPOLOGY, NULL);
    g_signal_connect(dialog, "delete_event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);

    image_analyzer_disc_topology_refresh(IMAGE_ANALYZER_DISC_TOPOLOGY(dialog), NULL, NULL);
    
    return dialog;
}

static GtkWidget *__build_menu (IMAGE_ANALYZER_Application *self) {
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);

    static GtkActionEntry entries[] = {
        { "FileMenuAction", NULL, "_File", NULL, NULL, NULL },
        { "ImageMenuAction", NULL, "_Image", NULL, NULL, NULL },
        { "HelpMenuAction", NULL, "_Help", NULL, NULL, NULL },

        { "OpenImageAction", GTK_STOCK_OPEN, "_Open image", "<control>O", "Open image", G_CALLBACK(__ui_callback_open_image) },
        { "OpenDumpAction", GTK_STOCK_OPEN, "Open _dump", "<control>D", "Open dump", G_CALLBACK(__ui_callback_open_dump) },
        { "SaveDumpAction", GTK_STOCK_SAVE, "_Save dump", "<control>S", "Save dump", G_CALLBACK(__ui_callback_save_dump) },
        { "CloseAction", GTK_STOCK_CLOSE, "_Close", "<control>W", "Close", G_CALLBACK(__ui_callback_close) },
        { "QuitAction", GTK_STOCK_QUIT, "_Quit", "<control>Q", "Quit", G_CALLBACK(__ui_callback_quit) },

        { "ParserLogAction", GTK_STOCK_DIALOG_INFO, "_Parser log", "<control>P", "Parser log", G_CALLBACK(__ui_callback_parser_log) },
        { "SectorAction", GTK_STOCK_EXECUTE, "_Read sector", "<control>R", "Read sector", G_CALLBACK(__ui_callback_sector) },
        { "AnalysisAction", GTK_STOCK_EXECUTE, "Sector _Analysis", "<control>A", "Sector analysis", G_CALLBACK(__ui_callback_analysis) },
        { "TopologyAction", GTK_STOCK_EXECUTE, "Disc _topology", "<control>T", "Disc topology", G_CALLBACK(__ui_callback_topology) },

        { "AboutAction", GTK_STOCK_ABOUT, "_About", NULL, "About", G_CALLBACK(__ui_callback_about) },
    };

    static guint n_entries = G_N_ELEMENTS(entries);

    static gchar *ui_xml = "\
        <ui> \
            <menubar name='MenuBar'> \
                <menu name='FileMenu' action='FileMenuAction'> \
                    <menuitem name='Open image' action='OpenImageAction' /> \
                    <separator/> \
                    <menuitem name='Open dump' action='OpenDumpAction' /> \
                    <menuitem name='Save dump' action='SaveDumpAction' /> \
                    <separator/> \
                    <menuitem name='Close' action='CloseAction' /> \
                    <menuitem name='Quit' action='QuitAction' /> \
                </menu> \
                <menu name='Image' action='ImageMenuAction'> \
                    <menuitem name='Parser log' action='ParserLogAction' /> \
                    <menuitem name='Read sector' action='SectorAction' /> \
                    <menuitem name='Sector analysis' action='AnalysisAction' /> \
                    <menuitem name='Disc topology' action='TopologyAction' /> \
                </menu> \
                <menu name='HelpMenu' action='HelpMenuAction'> \
                    <menuitem name='About' action='AboutAction' /> \
                </menu> \
            </menubar> \
        </ui> \
        ";

    GtkActionGroup *actiongroup;
    GError *error = NULL;

    /* Action group */
    actiongroup = gtk_action_group_new("Image Analyzer");
    gtk_action_group_add_actions(actiongroup, entries, n_entries, self);
    gtk_ui_manager_insert_action_group(_priv->ui_manager, actiongroup, 0);

    gtk_ui_manager_add_ui_from_string(_priv->ui_manager, ui_xml, strlen(ui_xml), &error);
    if (error) {
        g_warning("Building menus failed: %s", error->message);
        g_error_free(error);
    }

    return gtk_ui_manager_get_widget(_priv->ui_manager, "/MenuBar");
}

static GtkWidget *__build_treeview (IMAGE_ANALYZER_Application *self) {
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);

    GtkWidget *treeview;

    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;

    /* GtkTreeView */
    treeview = gtk_tree_view_new();

    /* GktTreeViewColumn */
    column = gtk_tree_view_column_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    gtk_tree_view_column_add_attribute(column, renderer, "text", 0);

    /* GtkTreeStore */
    _priv->treestore = gtk_tree_store_new(1, G_TYPE_STRING);
    gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), GTK_TREE_MODEL(_priv->treestore));

    return treeview;
}


/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
gboolean image_analyzer_application_run (IMAGE_ANALYZER_Application *self, gchar **open_image) {
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);

    /* Open image, if provided */
    if (g_strv_length(open_image)) {
        /* If it ends with .xml, we treat it as a dump file */
        if (mirage_helper_has_suffix(open_image[0], ".xml")) {
            __image_analyzer_application_open_dump(self, open_image[0]);
        } else {
            __image_analyzer_application_open_image(self, open_image);
        }
    }

    /* Show window */
    gtk_widget_show_all(_priv->window);

    /* GtkMain() */
    gtk_main();

    return TRUE;
}

gboolean image_analyzer_application_get_loaded_image (IMAGE_ANALYZER_Application *self, GObject **disc) {
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);

    if (!_priv->loaded || !_priv->disc) {
        return FALSE;
    }

    if (disc) {
        g_object_ref(_priv->disc);
        *disc = _priv->disc;
    }

    return TRUE;
}

/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static GObjectClass *parent_class = NULL;

static void __image_analyzer_application_instance_init (GTypeInstance *instance, gpointer g_class G_GNUC_UNUSED) {
    IMAGE_ANALYZER_Application *self = IMAGE_ANALYZER_APPLICATION(instance);
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);

    GtkWidget *vbox, *menubar, *scrolledwindow, *treeview;
    GtkAccelGroup *accel_group;

    /* UI manager */
    _priv->ui_manager = gtk_ui_manager_new();

    /* Window */
    _priv->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_signal_connect(_priv->window, "delete_event", G_CALLBACK(__cb_window_delete_event), self);
    gtk_window_set_title(GTK_WINDOW(_priv->window), "Image analyzer");
    gtk_window_set_default_size(GTK_WINDOW(_priv->window), 300, 400);
    gtk_container_set_border_width(GTK_CONTAINER(_priv->window), 5);

    /* VBox */
    vbox = gtk_vbox_new(FALSE, 5);
    gtk_container_add(GTK_CONTAINER(_priv->window), vbox);

    /* Menu */
    menubar = __build_menu(self);
    gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 0);

    /* Scrolled window */
    scrolledwindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(vbox), scrolledwindow, TRUE, TRUE, 0);

    /* Tree view widget */
    treeview = __build_treeview(self);
    gtk_container_add(GTK_CONTAINER(scrolledwindow), treeview);

    /* Status bar */
    _priv->statusbar = gtk_statusbar_new();
    gtk_box_pack_start(GTK_BOX(vbox), _priv->statusbar, FALSE, FALSE, 0);
    _priv->context_id = gtk_statusbar_get_context_id(GTK_STATUSBAR(_priv->statusbar), "Message");

    /* Dialogs */
    _priv->dialog_open_image = __build_dialog_open_image(self);
    _priv->dialog_open_dump = __build_dialog_open_dump(self);
    _priv->dialog_save_dump = __build_dialog_save_dump(self);
    _priv->dialog_parser = __build_dialog_parser();
    _priv->dialog_sector = __build_dialog_sector(self);
    _priv->dialog_analysis = __build_dialog_analysis(self);
    _priv->dialog_topology = __build_dialog_topology();

    /* Accelerator group */
    accel_group = gtk_ui_manager_get_accel_group(_priv->ui_manager);
    gtk_window_add_accel_group(GTK_WINDOW(_priv->window), accel_group);

    /* Set libMirage password function */
    libmirage_set_password_function (__image_analyzer_application_get_password, self, NULL);

    return;
}

static void __image_analyzer_application_finalize (GObject *obj) {
    IMAGE_ANALYZER_Application *self = IMAGE_ANALYZER_APPLICATION(obj);
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);

    /* Close image */
    __image_analyzer_application_close_image_or_dump(self);

#if 1
    gtk_widget_destroy(_priv->window);

    gtk_widget_destroy(_priv->dialog_open_image);
    gtk_widget_destroy(_priv->dialog_open_dump);
    gtk_widget_destroy(_priv->dialog_save_dump);
    gtk_widget_destroy(_priv->dialog_parser);
    gtk_widget_destroy(_priv->dialog_sector);
    gtk_widget_destroy(_priv->dialog_analysis);
    gtk_widget_destroy(_priv->dialog_topology);
#endif

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __image_analyzer_application_class_init (gpointer g_class, gpointer g_class_data G_GNUC_UNUSED) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    IMAGE_ANALYZER_ApplicationClass *klass = IMAGE_ANALYZER_APPLICATION_CLASS(g_class);

    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(IMAGE_ANALYZER_ApplicationPrivate));

    /* Initialize GObject methods */
    class_gobject->finalize = __image_analyzer_application_finalize;

    return;
}

GType image_analyzer_application_get_type (void) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(IMAGE_ANALYZER_ApplicationClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __image_analyzer_application_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(IMAGE_ANALYZER_Application),
            0,      /* n_preallocs */
            __image_analyzer_application_instance_init,   /* instance_init */
            NULL,   /* value_table */
        };

        type = g_type_register_static(G_TYPE_OBJECT, "IMAGE_ANALYZER_Application", &info, 0);
    }

    return type;
}
