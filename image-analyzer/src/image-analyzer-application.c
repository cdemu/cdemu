/*
 *  MIRAGE Image Analyzer: Application object
 *  Copyright (C) 2007-2008 Rok Mandeljc
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
#include "image-analyzer-dump.h"
#include "image-analyzer-application.h"
#include "image-analyzer-parser-log.h"
#include "image-analyzer-sector-read.h"
#include "image-analyzer-disc-topology.h"


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), IMAGE_ANALYZER_TYPE_APPLICATION, IMAGE_ANALYZER_ApplicationPrivate))

typedef struct {
    /* Mirage core object */
    GObject *mirage;
    
    /* Disc */
    gboolean loaded;
    GObject *disc; /* Disc */
    
    /* Dialogs */
    GtkWidget *filedialog;
    GtkWidget *parserdialog;
    GtkWidget *sectordialog;
    GtkWidget *topologydialog;
    
    /* Window */
    GtkWidget *window;
    
    /* UI Manager */
    GtkUIManager *ui_manager;
    
    /* Status bar */
    GtkWidget *statusbar;
    guint context_id;
    
    /* Model */
    GtkTreeStore *treestore;
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
 *                             Disc dump functions                            *
\******************************************************************************/
typedef struct {
    GtkTreeStore *treestore;
    GtkTreeIter *parent;
} IMAGE_ANALYZER_DumpContext;

static GtkTreeIter *__image_analyzer_add_node (GtkTreeStore *treestore, GtkTreeIter *parent, gchar *format, ...) {
    GtkTreeIter *node = NULL;
    gchar *node_string = NULL;
    va_list args;
    
    va_start(args, format);
    node_string = g_strdup_vprintf(format, args);
    va_end(args);
        
    node = g_new0(GtkTreeIter, 1);
    gtk_tree_store_append(treestore, node, parent);
    gtk_tree_store_set(treestore, node, 0,node_string, -1);
    
    g_free(node_string);
    
    return node;
}

static gboolean __image_analyzer_dump_fragment (gpointer data, gpointer user_data) {
    IMAGE_ANALYZER_DumpContext *context = user_data;
    GObject *fragment = data;
        
    MIRAGE_FragmentInfo *fragment_info = NULL;
    gint address = 0;
    gint length = 0;
    
    GtkTreeStore *treestore = context->treestore;
    GtkTreeIter *node = __image_analyzer_add_node(treestore, context->parent, "Fragment");
    
    if (mirage_fragment_get_fragment_info(MIRAGE_FRAGMENT(fragment), &fragment_info, NULL)) {
        __image_analyzer_add_node(treestore, node, "Fragment ID: %s", fragment_info->id);
    }
    
    if (mirage_fragment_get_address(MIRAGE_FRAGMENT(fragment), &address, NULL)) {
        __image_analyzer_add_node(treestore, node, "Address: %d (0x%X)", address, address);
    }
    
    if (mirage_fragment_get_length(MIRAGE_FRAGMENT(fragment), &length, NULL)) {
        __image_analyzer_add_node(treestore, node, "Length: %d (0x%X)", length, length);        
    }
    
    if (MIRAGE_IS_FINTERFACE_NULL(fragment)) {
        /* Nothing to do here*/
    } else if (MIRAGE_IS_FINTERFACE_BINARY(fragment)) {
        FILE *tfile_handle = NULL;
        guint64 tfile_offset = 0;
        gint tfile_sectsize = 0;
        gint tfile_format = 0;
        FILE *sfile_handle = NULL;
        guint64 sfile_offset = 0;
        gint sfile_sectsize = 0;
        gint sfile_format = 0;
        
        if (mirage_finterface_binary_track_file_get_handle(MIRAGE_FINTERFACE_BINARY(fragment), &tfile_handle, NULL)) {
            __image_analyzer_add_node(treestore, node, "Track file: Handle: %p", tfile_handle);
        }
        
        if (mirage_finterface_binary_track_file_get_offset(MIRAGE_FINTERFACE_BINARY(fragment), &tfile_offset, NULL)) {
            __image_analyzer_add_node(treestore, node, "Track file: Offset: %lli (0x%llX)", tfile_offset, tfile_offset);
        }
            
        if (mirage_finterface_binary_track_file_get_sectsize(MIRAGE_FINTERFACE_BINARY(fragment), &tfile_sectsize, NULL)) {
            __image_analyzer_add_node(treestore, node, "Track file: Sector size: %d (0x%X)", tfile_sectsize, tfile_sectsize);
        }
            
        if (mirage_finterface_binary_track_file_get_format(MIRAGE_FINTERFACE_BINARY(fragment), &tfile_format, NULL)) {
            __image_analyzer_add_node(treestore, node, "Track file: Format: 0x%X (%s)", tfile_format, __dump_binary_fragment_tfile_format(tfile_format));
        }

        if (mirage_finterface_binary_subchannel_file_get_handle(MIRAGE_FINTERFACE_BINARY(fragment), &sfile_handle, NULL)) {
            __image_analyzer_add_node(treestore, node, "Subchannel file: Handle: %p", sfile_handle);
        }
        
        if (mirage_finterface_binary_subchannel_file_get_offset(MIRAGE_FINTERFACE_BINARY(fragment), &sfile_offset, NULL)) {
            __image_analyzer_add_node(treestore, node, "Subchannel file: Offset: %lli (0x%llX)", sfile_offset, sfile_offset);
        }

        if (mirage_finterface_binary_subchannel_file_get_sectsize(MIRAGE_FINTERFACE_BINARY(fragment), &sfile_sectsize, NULL)) {
            __image_analyzer_add_node(treestore, node, "Subchannel file: Sector size: %d (0x%X)", sfile_sectsize, sfile_sectsize);
        }
        
        if (mirage_finterface_binary_subchannel_file_get_format(MIRAGE_FINTERFACE_BINARY(fragment), &sfile_format, NULL)) {
            __image_analyzer_add_node(treestore, node, "Subchannel file: Format: 0x%X (%s)", sfile_format, __dump_binary_fragment_sfile_format(sfile_format));
        }

    } else if (MIRAGE_IS_FINTERFACE_AUDIO(fragment)) {
        gchar *filename = NULL;
        gint offset = 0;
        
        if (mirage_finterface_audio_get_file(MIRAGE_FINTERFACE_AUDIO(fragment), &filename, NULL)) {
            __image_analyzer_add_node(treestore, node, "Filename: %s", filename);
            g_free(filename);
        }
        
        if (mirage_finterface_audio_get_offset(MIRAGE_FINTERFACE_AUDIO(fragment), &offset, NULL)) {
            __image_analyzer_add_node(treestore, node, "Offset (sectors): %d (0x%X)", offset, offset);
        }
    }
    
    return TRUE;
}

static gboolean __image_analyzer_dump_index (gpointer data, gpointer user_data) {
    IMAGE_ANALYZER_DumpContext *context = user_data;
    GObject *index = data;
        
    gint number = 0;
    gint address = 0;
    
    GtkTreeStore *treestore = context->treestore;
    GtkTreeIter *node = __image_analyzer_add_node(treestore, context->parent, "Index");
        
    if (mirage_index_get_number(MIRAGE_INDEX(index), &number, NULL)) {
        __image_analyzer_add_node(treestore, node, "Number: %d", number);
    }
    
    if (mirage_index_get_address(MIRAGE_INDEX(index), &address, NULL)) {
        __image_analyzer_add_node(treestore, node, "Address: %d (0x%X)", address, address);
    }
    
    return TRUE;
}

static gboolean __image_analyzer_dump_language (gpointer data, gpointer user_data) {
    IMAGE_ANALYZER_DumpContext *context = user_data;
    GObject *language = data;
        
    static const struct {
        gchar *name;
        gint code;
    } pack_types[] = {
        { "Title", MIRAGE_LANGUAGE_PACK_TITLE },
        { "Performer", MIRAGE_LANGUAGE_PACK_PERFORMER },
        { "Songwriter", MIRAGE_LANGUAGE_PACK_SONGWRITER },
        { "Composer", MIRAGE_LANGUAGE_PACK_COMPOSER },
        { "Arranger", MIRAGE_LANGUAGE_PACK_ARRANGER },
        { "Message", MIRAGE_LANGUAGE_PACK_MESSAGE },
        { "Disc ID", MIRAGE_LANGUAGE_PACK_DISC_ID },
        { "Genre", MIRAGE_LANGUAGE_PACK_GENRE },
        { "TOC", MIRAGE_LANGUAGE_PACK_TOC },
        { "TOC2", MIRAGE_LANGUAGE_PACK_TOC2 },
        { "Reserved 8A", MIRAGE_LANGUAGE_PACK_RES_8A },
        { "Reserved 8B", MIRAGE_LANGUAGE_PACK_RES_8B },
        { "Reserved 8C", MIRAGE_LANGUAGE_PACK_RES_8C },
        { "Closed info", MIRAGE_LANGUAGE_PACK_CLOSED_INFO },
        { "UPC/ISRC", MIRAGE_LANGUAGE_PACK_UPC_ISRC },
        { "Size", MIRAGE_LANGUAGE_PACK_SIZE },
    };
    
    gint i;
    
    gint langcode = 0;
    
    GtkTreeStore *treestore = context->treestore;
    GtkTreeIter *node = __image_analyzer_add_node(treestore, context->parent, "Language");
    
    if (mirage_language_get_langcode(MIRAGE_LANGUAGE(language), &langcode, NULL)) {
        __image_analyzer_add_node(treestore, node, "Language code: %d", langcode);
    }
    
    for (i = 0; i < G_N_ELEMENTS(pack_types); i++) {
        gchar *data = NULL;
        gint len = 0;
        if (mirage_language_get_pack_data(MIRAGE_LANGUAGE(language), pack_types[i].code, &data, &len, NULL)) {
            __image_analyzer_add_node(treestore, node, "%s: %s (%i)", pack_types[i].name, data, len);
            g_free(data);
        }
    }
    
    
    return TRUE;
}

static gboolean __image_analyzer_dump_track (gpointer data, gpointer user_data) {
    IMAGE_ANALYZER_DumpContext *context = user_data;
    GObject *track = data;
        
    gint flags = 0;
    gint mode = 0;
    gint adr = 0;
    gint ctl = 0;
    gchar *isrc = NULL;
    gint session_number = 0;
    gint track_number = 0;
    gint start_sector = 0;
    gint length = 0;
    gint number_of_fragments = 0;
    gint track_start = 0;
    gint number_of_indices = 0;
    gint number_of_languages = 0;
    
    IMAGE_ANALYZER_DumpContext tmp_context;
    
    GtkTreeStore *treestore = context->treestore;
    GtkTreeIter *node = __image_analyzer_add_node(treestore, context->parent, "Track");
    
    if (mirage_track_get_flags(MIRAGE_TRACK(track), &flags, NULL)) {
        __image_analyzer_add_node(treestore, node, "Flags: 0x%X (%s)", flags, __dump_track_flags(flags));
    }

    if (mirage_track_get_mode(MIRAGE_TRACK(track), &mode, NULL)) {
        __image_analyzer_add_node(treestore, node, "Mode: 0x%X (%s)", mode, __dump_track_mode(mode));
    }

    if (mirage_track_get_adr(MIRAGE_TRACK(track), &adr, NULL)) {
        __image_analyzer_add_node(treestore, node, "ADR: %d", adr);
    }
    
    if (mirage_track_get_ctl(MIRAGE_TRACK(track), &ctl, NULL)) {
        __image_analyzer_add_node(treestore, node, "CTL: %d", ctl);
    }

    if (mirage_track_get_isrc(MIRAGE_TRACK(track), &isrc, NULL)) {
        __image_analyzer_add_node(treestore, node, "ISRC: %s", isrc);
        g_free(isrc);
    }

    if (mirage_track_layout_get_session_number(MIRAGE_TRACK(track), &session_number, NULL)) {
        __image_analyzer_add_node(treestore, node, "Session number: %d", session_number);
    }
    
    if (mirage_track_layout_get_track_number(MIRAGE_TRACK(track), &track_number, NULL)) {
        __image_analyzer_add_node(treestore, node, "Track number: %d", track_number);
    }
    
    if (mirage_track_layout_get_start_sector(MIRAGE_TRACK(track), &start_sector, NULL)) {
        __image_analyzer_add_node(treestore, node, "Start sector: %d (0x%X)", start_sector, start_sector);
    }
    
    if (mirage_track_layout_get_length(MIRAGE_TRACK(track), &length, NULL)) {
        __image_analyzer_add_node(treestore, node, "Length: %d (0x%X)", length, length);
    }

    if (mirage_track_get_number_of_fragments(MIRAGE_TRACK(track), &number_of_fragments, NULL)) {
        __image_analyzer_add_node(treestore, node, "Number of fragments: %d", number_of_fragments);
    }
    
    if (number_of_fragments) {
        tmp_context.treestore = treestore;
        tmp_context.parent = __image_analyzer_add_node(treestore, node, "Fragments");
        mirage_track_for_each_fragment(MIRAGE_TRACK(track), __image_analyzer_dump_fragment, &tmp_context, NULL);
    }

    if (mirage_track_get_track_start(MIRAGE_TRACK(track), &track_start, NULL)) {
        __image_analyzer_add_node(treestore, node, "Track start: %d (0x%X)", track_start, track_start);
    }

    if (mirage_track_get_number_of_indices(MIRAGE_TRACK(track), &number_of_indices, NULL)) {
        __image_analyzer_add_node(treestore, node, "Number of indices: %d", number_of_indices);
    }
    
    if (number_of_indices) {
        tmp_context.treestore = treestore;
        tmp_context.parent = __image_analyzer_add_node(treestore, node, "Indices");
        mirage_track_for_each_index(MIRAGE_TRACK(track), __image_analyzer_dump_index, &tmp_context, NULL);
    }
     
    if (mirage_track_get_number_of_languages(MIRAGE_TRACK(track), &number_of_languages, NULL)) {
        __image_analyzer_add_node(treestore, node, "Number of languages: %d", number_of_languages);
    }
    
    if (number_of_languages) {
        tmp_context.treestore = treestore;
        tmp_context.parent = __image_analyzer_add_node(treestore, node, "Languages");
        mirage_track_for_each_language(MIRAGE_TRACK(track), __image_analyzer_dump_language, &tmp_context, NULL);
    }

    return TRUE;
}

static gboolean __image_analyzer_dump_session (gpointer data, gpointer user_data) {
    IMAGE_ANALYZER_DumpContext *context = user_data;
    GObject *session = data;
        
    gint session_type = 0;
    gint session_number = 0;
    gint first_track = 0;
    gint start_sector = 0;
    gint length = 0;
    gint leadout_length = 0;
    gint number_of_tracks = 0;
    gint number_of_languages = 0;
    
    IMAGE_ANALYZER_DumpContext tmp_context;
    
    GtkTreeStore *treestore = context->treestore;
    GtkTreeIter *node = __image_analyzer_add_node(treestore, context->parent, "Session");
    
    if (mirage_session_get_session_type(MIRAGE_SESSION(session), &session_type, NULL)) {
        __image_analyzer_add_node(treestore, node, "Session type: 0x%X (%s)", session_type, __dump_session_type(session_type));
    }
    
    if (mirage_session_layout_get_session_number(MIRAGE_SESSION(session), &session_number, NULL)) {
        __image_analyzer_add_node(treestore, node, "Session number: %d", session_number);
    }
    
    if (mirage_session_layout_get_first_track(MIRAGE_SESSION(session), &first_track, NULL)) {
        __image_analyzer_add_node(treestore, node, "Layout: First track: %d", first_track);
    }
    
    if (mirage_session_layout_get_start_sector(MIRAGE_SESSION(session), &start_sector, NULL)) {
        __image_analyzer_add_node(treestore, node, "Layout: Start sector: %d (0x%X)", start_sector);
    }
    
    if (mirage_session_layout_get_length(MIRAGE_SESSION(session), &length, NULL)) {
        __image_analyzer_add_node(treestore, node, "Layout: Length: %d (0x%X)", length, length);
    }

    if (mirage_session_get_leadout_length(MIRAGE_SESSION(session), &leadout_length, NULL)) {
        __image_analyzer_add_node(treestore, node, "Leadout length: %d (0x%X)", leadout_length, leadout_length);
    }

    if (mirage_session_get_number_of_tracks(MIRAGE_SESSION(session), &number_of_tracks, NULL)) {
        __image_analyzer_add_node(treestore, node, "Number of tracks: %d", number_of_tracks);
    }
    
    if (number_of_tracks) {
        tmp_context.treestore = treestore;
        tmp_context.parent = __image_analyzer_add_node(treestore, node, "Tracks");
        mirage_session_for_each_track(MIRAGE_SESSION(session), __image_analyzer_dump_track, &tmp_context, NULL);
    }

    if (mirage_session_get_number_of_languages(MIRAGE_SESSION(session), &number_of_languages, NULL)) {
        __image_analyzer_add_node(treestore, node, "Number of languages: %d", number_of_languages);
    }
    
    if (number_of_languages) {
        tmp_context.treestore = treestore;
        tmp_context.parent = __image_analyzer_add_node(treestore, node, "Languages");
        mirage_session_for_each_language(MIRAGE_SESSION(session), __image_analyzer_dump_language, &tmp_context, NULL);
    }

    //mirage_session_get_cdtext_data(MIRAGE_SESSION(session), guint8 **data, gint *len, NULL);

    return TRUE;
}

static gboolean __image_analyzer_dump_disc (gpointer data, gpointer user_data) {
    IMAGE_ANALYZER_DumpContext *context = user_data;
    GObject *disc = data;
        
    MIRAGE_ParserInfo *parser_info = NULL;
    gint medium_type = 0;
    gchar **filenames = NULL;
    gchar *mcn = NULL;
    gint first_session = 0;
    gint first_track = 0;
    gint start_sector = 0;
    gint length = 0;
    gint number_of_sessions = 0;
    gint number_of_tracks = 0;
    
    gint dpm_start = 0;
    gint dpm_resolution = 0;
    gint dpm_entries = 0;
    guint32 *dpm_data = NULL;
    
    IMAGE_ANALYZER_DumpContext tmp_context;
    
    GtkTreeStore *treestore = context->treestore;
    GtkTreeIter *node = __image_analyzer_add_node(treestore, context->parent, "Disc");
    
    if (mirage_disc_get_medium_type(MIRAGE_DISC(disc), &medium_type, NULL)) {
        __image_analyzer_add_node(treestore, node, "Medium type: 0x%X (%s)", medium_type, __dump_medium_type(medium_type));
    }
    
    if (mirage_disc_get_filenames(MIRAGE_DISC(disc), &filenames, NULL)) {
        gint i = 0;
        GtkTreeIter *files = NULL;
        
        /* Filenames */
        files = __image_analyzer_add_node(treestore, node, "Filename(s)");
        while (filenames[i]) {
            __image_analyzer_add_node(treestore, files, "%s", filenames[i]);
            i++;
        }
    }
    
    if (mirage_disc_get_mcn(MIRAGE_DISC(disc), &mcn, NULL)) {
        __image_analyzer_add_node(treestore, node, "MCN: %s", mcn);
        g_free(mcn);
    }

    if (mirage_disc_layout_get_first_session(MIRAGE_DISC(disc), &first_session, NULL)) {
        __image_analyzer_add_node(treestore, node, "Layout: First session: %d", first_session);
    }
    
    if (mirage_disc_layout_get_first_track(MIRAGE_DISC(disc), &first_track, NULL)) {
        __image_analyzer_add_node(treestore, node, "Layout: First track: %d", first_track);
    }
    
    if (mirage_disc_layout_get_start_sector(MIRAGE_DISC(disc), &start_sector, NULL)) {
        __image_analyzer_add_node(treestore, node, "Layout: Start sector: %d (0x%X)", start_sector, start_sector);
    }
    
    if (mirage_disc_layout_get_length(MIRAGE_DISC(disc), &length, NULL)) {
        __image_analyzer_add_node(treestore, node, "Layout: Length: %d (0x%X)", length, length);
    }

    if (mirage_disc_get_number_of_sessions(MIRAGE_DISC(disc), &number_of_sessions, NULL)) {
        __image_analyzer_add_node(treestore, node, "Number of sessions: %d", number_of_sessions);
    }
    
    if (mirage_disc_get_number_of_tracks(MIRAGE_DISC(disc), &number_of_tracks, NULL)) {
        __image_analyzer_add_node(treestore, node, "Number of tracks: %d", number_of_tracks);
    }
    
    if (number_of_sessions) {
        tmp_context.treestore = treestore;
        tmp_context.parent = __image_analyzer_add_node(treestore, node, "Sessions");
        mirage_disc_for_each_session(MIRAGE_DISC(disc), __image_analyzer_dump_session, &tmp_context, NULL);
    }
    
    if (mirage_disc_get_dpm_data(MIRAGE_DISC(disc), &dpm_start, &dpm_resolution, &dpm_entries, &dpm_data, NULL)) {
        gint i = 0;
        GtkTreeIter *dpm = NULL;
        GtkTreeIter *data_entries = NULL;
        
        /* DPM */
        dpm = __image_analyzer_add_node(treestore, node, "DPM");
        
        __image_analyzer_add_node(treestore, dpm, "Start sector: %d (0x%X)", dpm_start, dpm_start);
        __image_analyzer_add_node(treestore, dpm, "Resolution: %d (0x%X)", dpm_resolution, dpm_resolution);
        __image_analyzer_add_node(treestore, dpm, "Number of entries: %d (0x%X)", dpm_entries, dpm_entries);
        
        data_entries = __image_analyzer_add_node(treestore, dpm, "Data entries");
        for (i = 0; i < dpm_entries; i++) {
            __image_analyzer_add_node(treestore, data_entries, "0x%08X | %d", dpm_data[i], (i+1)*dpm_resolution);
        }
    }    
    
    return TRUE;
}


/******************************************************************************\
 *                              Logging redirection                           *
\******************************************************************************/
static void __image_analyzer_capture_parser_log (const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer user_data) {
    IMAGE_ANALYZER_Application *self = IMAGE_ANALYZER_APPLICATION(user_data);
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);
    
    image_analyzer_parser_log_append_to_log(IMAGE_ANALYZER_PARSER_LOG(_priv->parserdialog), (gchar *)message, NULL);
    
    return;
}


/******************************************************************************\
 *                                Status message                              *
\******************************************************************************/
static void __image_analyzer_application_message (IMAGE_ANALYZER_Application *self, gchar *format, ...) {
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);
    gchar *message = NULL;
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
static gboolean __image_analyzer_close_image (IMAGE_ANALYZER_Application *self, GError **error) {
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);
    
    /* Clear log whether we're loaded or not... it doesn't really hurt to do it
       before the check, and it ensures the log is always cleared (i.e. if load
       call failed, we'd have error log but it wouldn't be cleared on subsequent
       load call... */
    image_analyzer_parser_log_clear_log(IMAGE_ANALYZER_PARSER_LOG(_priv->parserdialog), NULL);
    
    /* Clear disc topology */
    image_analyzer_disc_topology_clear(IMAGE_ANALYZER_DISC_TOPOLOGY(_priv->topologydialog), NULL);
    
    /* Check if we're loaded */
    if (!_priv->loaded) {
        /* Not loaded, nothing to do here */
        return FALSE;
    }
        
    /* Clear TreeStore */
    gtk_tree_store_clear(_priv->treestore);
    
    /* Release disc reference */
    g_object_unref(_priv->disc);
    
    __image_analyzer_application_message(self, "Image closed.");

    _priv->loaded = FALSE;

    return TRUE;
}

static gboolean __image_analyzer_open_image (IMAGE_ANALYZER_Application *self, gchar **filenames, GError **error) {
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);
    GObject *debug_context = NULL;
    IMAGE_ANALYZER_DumpContext context;
    guint log_handler = 0;
    
    /* Close image */
    __image_analyzer_close_image(self, NULL);
    
    /* Create debug context for disc */
    debug_context = g_object_new(MIRAGE_TYPE_DEBUG_CONTEXT, NULL);
    mirage_debug_context_set_domain(MIRAGE_DEBUG_CONTEXT(debug_context), DEBUG_DOMAIN_PARSER, NULL);
    mirage_debug_context_set_debug_mask(MIRAGE_DEBUG_CONTEXT(debug_context), MIRAGE_DEBUG_PARSER, NULL);
    
    /* Set log handler */
    log_handler = g_log_set_handler(DEBUG_DOMAIN_PARSER, G_LOG_LEVEL_MASK, __image_analyzer_capture_parser_log, self);

    /* Create disc */
    if (!mirage_mirage_create_disc(MIRAGE_MIRAGE(_priv->mirage), filenames, &_priv->disc, debug_context, error)) {
        g_warning("Failed to create disc!\n");
        __image_analyzer_application_message(self, "Failed to open image!");
        return FALSE;
    }
    
    /* Release reference to debug context; it's attached to disc now */
    g_object_unref(debug_context);

    /* Remove log handler */
    g_log_remove_handler(DEBUG_DOMAIN_PARSER, log_handler);
    
    /* Disc topology */
    image_analyzer_disc_topology_create(IMAGE_ANALYZER_DISC_TOPOLOGY(_priv->topologydialog), _priv->disc, NULL);    
    
    /* Dump disc */
    context.parent = NULL;
    context.treestore = _priv->treestore;
    if (!__image_analyzer_dump_disc(_priv->disc, &context)) {
        g_warning("Failed to dump disc!\n");
        __image_analyzer_application_message(self, "Failed to dump disc!");
        return FALSE;
    }
    
    __image_analyzer_application_message(self, "Image successfully opened.");

    _priv->loaded = TRUE;
    
    return TRUE;
}


/******************************************************************************\
 *                                 UI callbacks                               *
\******************************************************************************/
static void __image_analyzer_application_ui_callback_open (GtkAction *action, gpointer user_data) {
    IMAGE_ANALYZER_Application *self = IMAGE_ANALYZER_APPLICATION(user_data);
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);
    
    /* Run the dialog */
    if (gtk_dialog_run(GTK_DIALOG(_priv->filedialog)) == GTK_RESPONSE_ACCEPT) {
        GSList *filenames_list = NULL;
        GSList *entry = NULL;
        gint num_filenames = 0;
        gchar **filenames = NULL;
        gint i = 0;
        
        /* Get filenames from dialog */
        filenames_list = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(_priv->filedialog));
        
        /* Create strings array */
        num_filenames = g_slist_length(filenames_list);
        filenames = g_new0(gchar *, num_filenames + 1); /* NULL-terminated */
        
        /* GSList -> strings array conversion */
        entry = filenames_list;
        while (entry != NULL) {
            filenames[i++] = entry->data;
            entry = g_slist_next(entry);
        }
        
        __image_analyzer_open_image(self, filenames, NULL);
        
        /* Free filenames list */
        g_slist_free(filenames_list);
        /* Free filenames array */
        g_strfreev(filenames);
    }
    
    gtk_widget_hide(GTK_WIDGET(_priv->filedialog));
            
    return;
}

static void __image_analyzer_application_ui_callback_close (GtkAction *action, gpointer user_data) {
    IMAGE_ANALYZER_Application *self = IMAGE_ANALYZER_APPLICATION(user_data);
    /*IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);*/
    
    __image_analyzer_close_image(self, NULL);
    
    return;
}

static void __image_analyzer_application_ui_callback_quit (GtkAction *action, gpointer user_data) {
    gtk_main_quit();
    return;
}

static void __image_analyzer_application_ui_callback_parser_log (GtkAction *action, gpointer user_data) {
    IMAGE_ANALYZER_Application *self = IMAGE_ANALYZER_APPLICATION(user_data);
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);
    
    /* Make window (re)appear by first hiding, then showing it */
    gtk_widget_hide(_priv->parserdialog);
    gtk_widget_show_all(_priv->parserdialog);
    
    return;
}

static void __image_analyzer_application_ui_callback_sector (GtkAction *action, gpointer user_data) {
    IMAGE_ANALYZER_Application *self = IMAGE_ANALYZER_APPLICATION(user_data);
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);
    
    /* Make window (re)appear by first hiding, then showing it */
    gtk_widget_hide(_priv->sectordialog);
    gtk_widget_show_all(_priv->sectordialog);
    
    return;
}

static void __image_analyzer_application_ui_callback_topology (GtkAction *action, gpointer user_data) {
    IMAGE_ANALYZER_Application *self = IMAGE_ANALYZER_APPLICATION(user_data);
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);
    
    /* Make window (re)appear by first hiding, then showing it */
    gtk_widget_hide(_priv->topologydialog);
    gtk_widget_show_all(_priv->topologydialog);
    
    return;
}

static void __image_analyzer_application_ui_callback_about (GtkAction *action, gpointer user_data) {
    IMAGE_ANALYZER_Application *self = IMAGE_ANALYZER_APPLICATION(user_data);
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);
    
    gchar *authors[] = { "Rok Mandeljc <rok.mandeljc@email.si>", NULL };
    
    gtk_show_about_dialog(
        GTK_WINDOW(_priv->window),
        "name", "Image Analyzer",
        "comments", "Image Analyzer displays tree structure of disc image created by libMirage.",
        "version", PACKAGE_VERSION,
        "authors", authors,
        "copyright", "Copyright (C) 2007 Rok Mandeljc",
        NULL);
    
    return;
}


static gboolean __image_analyzer_application_cb_window_delete_event (GtkWidget *widget, GdkEvent *event, gpointer user_data) {
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

static gboolean __case_insensitive_ext_filter (const GtkFileFilterInfo *filter_info, gpointer data) {
    gchar *filename_ext = NULL;
    gchar *pattern_ext = data;
        
    g_return_val_if_fail(data != NULL, FALSE);
    g_return_val_if_fail(filter_info != NULL, FALSE);

    if (!filter_info->filename) {
        return FALSE;
    }
    
    filename_ext = mirage_helper_get_suffix((gchar *)filter_info->filename);
    if (!filename_ext) {
        return FALSE;
    }
        
    return !mirage_helper_strcasecmp(filename_ext, pattern_ext);
}

static gboolean __image_analyzer_application_add_file_filter (gpointer data, gpointer user_data) {
    IMAGE_ANALYZER_FilterContext *context = user_data;
    MIRAGE_ParserInfo *parser_info = data;
    gint i = 0;
    
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, parser_info->description);

    while (parser_info->suffixes[i]) {
        /* Per-parser filter */
        gtk_file_filter_add_custom(filter, GTK_FILE_FILTER_FILENAME, __case_insensitive_ext_filter, g_strdup(parser_info->suffixes[i]), g_free);
        /* "All images" filter */
        gtk_file_filter_add_custom(context->all_images, GTK_FILE_FILTER_FILENAME, __case_insensitive_ext_filter, g_strdup(parser_info->suffixes[i]), g_free);
        
        i++;
    }
    
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(context->dialog), filter);
    
    return TRUE;
}

static GtkWidget *__image_analyzer_application_build_file_dialog (IMAGE_ANALYZER_Application *self) {
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);
    IMAGE_ANALYZER_FilterContext context;
    
    GtkWidget *dialog = NULL;
    GtkFileFilter *filter = NULL;
    
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
    mirage_mirage_for_each_parser(MIRAGE_MIRAGE(_priv->mirage), __image_analyzer_application_add_file_filter, &context, NULL);
    
    return dialog;    
}

static GtkWidget *__image_analyzer_application_build_parser_dialog (IMAGE_ANALYZER_Application *self) {
    GtkWidget *dialog = NULL;
    dialog = g_object_new(IMAGE_ANALYZER_TYPE_PARSER_LOG, NULL);
    g_signal_connect(dialog, "delete_event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    return dialog;
}

static GtkWidget *__image_analyzer_application_build_sector_dialog (IMAGE_ANALYZER_Application *self) {
    GtkWidget *dialog = NULL;
    dialog = g_object_new(IMAGE_ANALYZER_TYPE_SECTOR_READ, "application",self, NULL);
    g_signal_connect(dialog, "delete_event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    return dialog;
}

static GtkWidget *__image_analyzer_application_build_topology_dialog (IMAGE_ANALYZER_Application *self) {
    GtkWidget *dialog = NULL;
    dialog = g_object_new(IMAGE_ANALYZER_TYPE_DISC_TOPOLOGY, "application",self, NULL);
    g_signal_connect(dialog, "delete_event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    return dialog;
}

static GtkWidget *__image_analyzer_application_build_menu (IMAGE_ANALYZER_Application *self) {
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);

    static GtkActionEntry entries[] = {
        { "FileMenuAction", NULL, "_File" },
        { "ImageMenuAction", NULL, "_Image" },
        { "HelpMenuAction", NULL, "_Help" },

        { "OpenAction", GTK_STOCK_OPEN, "_Open", "<control>O", "Open", G_CALLBACK(__image_analyzer_application_ui_callback_open) },
        { "CloseAction", GTK_STOCK_CLOSE, "_Close", "<control>W", "Close", G_CALLBACK(__image_analyzer_application_ui_callback_close) },
        { "QuitAction", GTK_STOCK_QUIT, "_Quit", "<control>Q", "Quit", G_CALLBACK(__image_analyzer_application_ui_callback_quit) },
        
        { "ParserLogAction", GTK_STOCK_DIALOG_INFO, "_Parser log", "<control>P", "Parser log", G_CALLBACK(__image_analyzer_application_ui_callback_parser_log) },
        { "SectorAction", GTK_STOCK_EXECUTE, "_Read sector", "<control>R", "Read sector", G_CALLBACK(__image_analyzer_application_ui_callback_sector) },
        { "TopologyAction", GTK_STOCK_EXECUTE, "Disc _topology", "<control>T", "Disc topology", G_CALLBACK(__image_analyzer_application_ui_callback_topology) },

        { "AboutAction", GTK_STOCK_ABOUT, "_About", NULL, "About", G_CALLBACK(__image_analyzer_application_ui_callback_about) },
    };
    
    static guint n_entries = G_N_ELEMENTS(entries);
    
    static gchar *ui_xml = "\
        <ui> \
            <menubar name='MenuBar'> \
                <menu name='FileMenu' action='FileMenuAction'> \
                    <menuitem name='Open' action='OpenAction' /> \
                    <menuitem name='Close' action='CloseAction' /> \
                    <separator/> \
                    <menuitem name='Quit' action='QuitAction' /> \
                </menu> \
                <menu name='Image' action='ImageMenuAction'> \
                    <menuitem name='Parser log' action='ParserLogAction' /> \
                    <menuitem name='Read sector' action='SectorAction' /> \
                    <menuitem name='Disc topology' action='TopologyAction' /> \
                </menu> \
                <menu name='HelpMenu' action='HelpMenuAction'> \
                    <menuitem name='About' action='AboutAction' /> \
                </menu> \
            </menubar> \
        </ui> \
        ";
    
    GtkActionGroup *actiongroup = NULL;
    GError *error = NULL;
    
    /* Action group */
    actiongroup = gtk_action_group_new("MIRAGE Image Analyzer");
    gtk_action_group_add_actions(actiongroup, entries, n_entries, self);
    gtk_ui_manager_insert_action_group(_priv->ui_manager, actiongroup, 0);
    
    gtk_ui_manager_add_ui_from_string(_priv->ui_manager, ui_xml, strlen(ui_xml), &error);
    if (error) {
        g_warning("Building menus failed: %s", error->message);
        g_error_free(error);
    }
    
    return gtk_ui_manager_get_widget(_priv->ui_manager, "/MenuBar");
}

static GtkWidget *__image_analyzer_application_build_treeview (IMAGE_ANALYZER_Application *self) {
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);
    
    GtkWidget *treeview = NULL;
    
    GtkTreeViewColumn *column = NULL;
    GtkCellRenderer *renderer = NULL;
    
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
gboolean image_analyzer_application_run (IMAGE_ANALYZER_Application *self, gchar **open_image, GError **error) {
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);
    
    /* Open image, if provided */
    if (g_strv_length(open_image)) {
        __image_analyzer_open_image(self, open_image, NULL);
    }

    /* Show window */
    gtk_widget_show_all(_priv->window);
    
    /* GtkMain() */
    gtk_main();
        
    return TRUE;
}

gboolean image_analyzer_application_get_loaded_image (IMAGE_ANALYZER_Application *self, GObject **disc, GError **error) {
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);
    
    if (!_priv->loaded) {
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

static void __image_analyzer_application_instance_init (GTypeInstance *instance, gpointer g_class) {
    IMAGE_ANALYZER_Application *self = IMAGE_ANALYZER_APPLICATION(instance);
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);
    
    GtkWidget *vbox = NULL;
    GtkWidget *menubar = NULL;
    GtkWidget *scrolledwindow = NULL;
    GtkWidget *treeview = NULL;
    
    GtkAccelGroup *accel_group = NULL;
    
    /* libMirage core object */
    _priv->mirage = libmirage_init();
    
    /* UI manager */
    _priv->ui_manager = gtk_ui_manager_new();
    
    /* Window */
    _priv->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    g_signal_connect(_priv->window, "delete_event", G_CALLBACK(__image_analyzer_application_cb_window_delete_event), self);
    gtk_window_set_title(GTK_WINDOW(_priv->window), "MIRAGE Image analyzer");
    gtk_window_set_default_size(GTK_WINDOW(_priv->window), 300, 400);
    gtk_container_set_border_width(GTK_CONTAINER(_priv->window), 5);

    /* VBox */
    vbox = gtk_vbox_new(FALSE, 5);
    gtk_container_add(GTK_CONTAINER(_priv->window), vbox);
    
    /* Menu */
    menubar = __image_analyzer_application_build_menu(self);
    gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 0);
    
    /* Scrolled window */
    scrolledwindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(vbox), scrolledwindow, TRUE, TRUE, 0);

    /* Tree view widget */
    treeview = __image_analyzer_application_build_treeview(self);
    gtk_container_add(GTK_CONTAINER(scrolledwindow), treeview);
    
    /* Status bar */
    _priv->statusbar = gtk_statusbar_new();
    gtk_box_pack_start(GTK_BOX(vbox), _priv->statusbar, FALSE, FALSE, 0);
    _priv->context_id = gtk_statusbar_get_context_id(GTK_STATUSBAR(_priv->statusbar), "Message");
    
    /* Dialogs */
    _priv->filedialog = __image_analyzer_application_build_file_dialog(self);
    _priv->parserdialog = __image_analyzer_application_build_parser_dialog(self);
    _priv->sectordialog = __image_analyzer_application_build_sector_dialog(self);
    _priv->topologydialog = __image_analyzer_application_build_topology_dialog(self);
    
    /* Accelerator group */
    accel_group = gtk_ui_manager_get_accel_group(_priv->ui_manager);
    gtk_window_add_accel_group(GTK_WINDOW(_priv->window), accel_group);
    
    return;
}

static void __image_analyzer_application_finalize (GObject *obj) {
    IMAGE_ANALYZER_Application *self = IMAGE_ANALYZER_APPLICATION(obj);
    IMAGE_ANALYZER_ApplicationPrivate *_priv = IMAGE_ANALYZER_APPLICATION_GET_PRIVATE(self);
    
    /* Close image */
    __image_analyzer_close_image(self, NULL);

    /* Release reference to libMirage core object */
    libmirage_destroy();
    
    gtk_widget_destroy(_priv->window);
    
    gtk_widget_destroy(_priv->filedialog);
    gtk_widget_destroy(_priv->parserdialog);
    gtk_widget_destroy(_priv->sectordialog);
    gtk_widget_destroy(_priv->topologydialog);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __image_analyzer_application_class_init (gpointer g_class, gpointer g_class_data) {
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
            __image_analyzer_application_instance_init    /* instance_init */
        };
        
        type = g_type_register_static(G_TYPE_OBJECT, "IMAGE_ANALYZER_Application", &info, 0);
    }
    
    return type;
}
