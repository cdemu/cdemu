/*
 *  libMirage: CCD image parser: Parser object
 *  Copyright (C) 2006-2009 Rok Mandeljc
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
 
#include "image-ccd.h"

#define __debug__ "CCD-Parser"


/* Regex engine */
typedef gboolean (*MIRAGE_RegexCallback) (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error);

typedef struct {
    GRegex *regex;
    MIRAGE_RegexCallback callback_func;
} MIRAGE_RegexRule;


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_PARSER_CCD_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_CCD, MIRAGE_Parser_CCDPrivate))

typedef struct {
    GObject *disc;
    
    /* Data and subchannel filenames */
    gchar *img_filename;
    gchar *sub_filename;
    
    /* Offset within data/subchannel file */
    gint offset;
    
    /* Parsed data */
    CCD_CloneCD *header;
    CCD_Disc *disc_data;
    GList *sessions_list;
    GList *entries_list;
    
    /* Regex engine */
    gpointer cur_data;
    GList *cur_rules;
    
    GList *regex_rules;
    GList *regex_rules_clonecd;
    GList *regex_rules_disc;
    GList *regex_rules_session;
    GList *regex_rules_entry;
    GList *regex_rules_track;
} MIRAGE_Parser_CCDPrivate;


/******************************************************************************\
 *                         Parser private functions                             *
\******************************************************************************/
static gint __find_redundant_entries (CCD_Entry *entry, gconstpointer not_used) {
    return ((entry->Point > 0 && entry->Point < 99) || entry->Point == 0xA0 || entry->Point == 0xA2);
}

static gint __sort_entries (CCD_Entry *entry1, CCD_Entry *entry2) {
    /* We sort entries by session; then, we put 0xA0 before 1-99, and we put
       0xA2 at the end. NOTE: the function compares newly added entry1 to already
       existing entry2... */
    if (entry2->Session == entry1->Session) {
        if (entry1->Point == 0xA0) {
            /* Put entry1 (0xA0) before entry2 */
            return -1;
        } else if (entry1->Point == 0xA2) {
            /* Put entry1 (0xA2) after entry2 */
            return 1;
        } else {
            return (entry1->Point < entry2->Point)*(-1) + (entry1->Point > entry2->Point)*(1);
        }
    } else {
        return (entry1->Session < entry2->Session)*(-1) + (entry1->Session > entry2->Session)*(1);
    }
}

static gboolean __mirage_parser_ccd_sort_entries (MIRAGE_Parser *self, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    GList *entry;
    
    /* First, remove the entries that we won't need */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: removing redundant entries\n", __debug__);
    entry = g_list_find_custom(_priv->entries_list, NULL, (GCompareFunc)__find_redundant_entries);
    while (entry) {
        CCD_Entry *data = entry->data;
        
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: removing entry #%d, point 0x%X\n", __debug__, data->number, data->Point);
        
        g_free(data->ISRC);
        g_free(data);
        
        _priv->entries_list = g_list_delete_link(_priv->entries_list, entry);
        entry = g_list_find_custom(_priv->entries_list, NULL, (GCompareFunc)__find_redundant_entries);
    }
    
    /* Now, reorder the entries */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reordering entries\n", __debug__);
    _priv->entries_list = g_list_sort(_priv->entries_list, (GCompareFunc)__sort_entries);
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_determine_track_mode (MIRAGE_Parser *self, GObject *track, GError **error) {
    GObject *data_fragment = NULL;
    guint64 offset = 0;
    FILE *file = NULL;
    size_t blocks_read;
    guint8 sync[12] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
    guint8 buf[24] = {0};

    /* Get last fragment */
    if (!mirage_track_get_fragment_by_index(MIRAGE_TRACK(track), -1, &data_fragment, NULL)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get fragment\n", __debug__);
        return FALSE;
    }
    
    /* 2352-byte sectors are assumed */
    mirage_finterface_binary_track_file_get_handle(MIRAGE_FINTERFACE_BINARY(data_fragment), &file, NULL);
    mirage_finterface_binary_track_file_get_position(MIRAGE_FINTERFACE_BINARY(data_fragment), 0, &offset, NULL);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: checking for track type in data file at location 0x%llX\n", __debug__, offset);
    fseeko(file, offset, SEEK_SET);
    blocks_read = fread(buf, 24, 1, file);
    if (blocks_read < 1) return FALSE;
    if (!memcmp(buf, sync, 12)) {
        switch (buf[15]) {
            case 0x01: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Mode 1 track\n", __debug__);
                mirage_track_set_mode(MIRAGE_TRACK(track), MIRAGE_MODE_MODE1, NULL);
                break;
            }
            case 0x02: {
                /* Mode 2; let's say we're Mode 2 Mixed and let the sector
                   code do the rest for us */
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Mode 2 track; setting to mixed...\n", __debug__);
                mirage_track_set_mode(MIRAGE_TRACK(track), MIRAGE_MODE_MODE2_MIXED, NULL);
                break;
            }
        }
    } else { 
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Audio track\n", __debug__);
        mirage_track_set_mode(MIRAGE_TRACK(track), MIRAGE_MODE_AUDIO, NULL);
    }
    
    g_object_unref(data_fragment);
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_clean_parsed_structures (MIRAGE_Parser *self, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    GList *entry = NULL;
        
    /* CloneCD header */
    g_free(_priv->header);
    
    /* Disc */
    g_free(_priv->disc_data->Catalog);
    g_free(_priv->disc_data);
    
    /* Sessions list */
    G_LIST_FOR_EACH(entry, _priv->sessions_list) {
        CCD_Session *ccd_session = entry->data;
        g_free(ccd_session);
    }
    g_list_free(_priv->sessions_list);
    
    /* Entries list */
    G_LIST_FOR_EACH(entry, _priv->entries_list) {
        CCD_Entry *ccd_entry = entry->data;
        g_free(ccd_entry->ISRC);
        g_free(ccd_entry);
    }
    g_list_free(_priv->entries_list);
        
    return TRUE;
}

static gboolean __mirage_parser_ccd_build_disc_layout (MIRAGE_Parser *self, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    GList *entry = NULL;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: building disc layout\n", __debug__);
    
    if (_priv->disc_data->Catalog) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting disc catalog to %.13s\n", __debug__, _priv->disc_data->Catalog);
        mirage_disc_set_mcn(MIRAGE_DISC(_priv->disc), _priv->disc_data->Catalog, NULL);
    }
    
    if (!__mirage_parser_ccd_sort_entries(self, error)) {
        return FALSE;
    }
    
    /* Go over stored entries and build the layout */
    G_LIST_FOR_EACH(entry, _priv->entries_list) {
        CCD_Entry *ccd_cur_entry = entry->data;
        CCD_Entry *ccd_next_entry = entry->next ? entry->next->data : NULL;
        
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n", __debug__);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: processing entry; point %02X, session %d\n", __debug__, ccd_cur_entry->Point, ccd_cur_entry->Session);
        
        if (ccd_cur_entry->Point == 0xA0) {
            /* 0xA0 is entry each session should begin with... so add the session here */
            GObject *cur_session = NULL;
        
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding session #%i\n", __debug__, ccd_cur_entry->Session);
        
            if (!mirage_disc_add_session_by_number(MIRAGE_DISC(_priv->disc), ccd_cur_entry->Session, &cur_session, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __debug__);
                return FALSE;
            }
            mirage_session_set_session_type(MIRAGE_SESSION(cur_session), ccd_cur_entry->PSec, NULL); /* PSEC = Parser Type */
            
            g_object_unref(cur_session);
        } else if (ccd_cur_entry->Point == 0xA2) {
            /* 0xA2 is entry each session should end with... */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: closing session #%i\n", __debug__, ccd_cur_entry->Session);
            
            /* If there is next entry, we're dealing with multi-session disc; in 
               this case, we need to set leadout length */
            if (ccd_next_entry) {
                GObject *cur_session = NULL;
                gint leadout_length = 0;
                
                if (!mirage_disc_get_session_by_number(MIRAGE_DISC(_priv->disc), ccd_cur_entry->Session, &cur_session, error)) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get session %i!\n", __debug__, ccd_cur_entry->Session);
                    return FALSE;
                }
                
                if (ccd_cur_entry->Session == 1) {
                    leadout_length = 11250;
                } else {
                    leadout_length = 6750;
                }
            
                mirage_session_set_leadout_length(MIRAGE_SESSION(cur_session), leadout_length, NULL);
            
                g_object_unref(cur_session);
            }
        } else {
            /* Track */
            GObject *cur_session = NULL;
            GObject *cur_track = NULL;
            gint fragment_length = 0;
            
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding track #%d\n", __debug__, ccd_cur_entry->Point);
            
            /* Shouldn't really happen... */
            if (!ccd_next_entry) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: ccd_next_entry == NULL; shouldn't happen!\n", __debug__);
                mirage_error(MIRAGE_E_PARSER, error);
                return FALSE;
            }
            
            /* Grab the session */
            if (!mirage_disc_get_session_by_number(MIRAGE_DISC(_priv->disc), ccd_cur_entry->Session, &cur_session, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get session %i!\n", __debug__, ccd_cur_entry->Session);
                return FALSE;
            }
            
            /* Add the track */
            if (!mirage_session_add_track_by_number(MIRAGE_SESSION(cur_session), ccd_cur_entry->Point, &cur_track, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __debug__);
                g_object_unref(cur_session);
                return FALSE;
            }
            
            
            /* Data fragment */
            GObject *data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FINTERFACE_BINARY, _priv->img_filename, error);
            if (!data_fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create data fragment!\n", __debug__);
                g_object_unref(cur_track);
                g_object_unref(cur_session);
                return FALSE;
            }
            
            FILE *tfile_file = g_fopen(_priv->img_filename, "r");
            gint tfile_sectsize = 2352; /* Always */
            guint64 tfile_offset = _priv->offset * 2352; /* Guess this one's always true, too */
            gint tfile_format = FR_BIN_TFILE_DATA; /* Assume data, but change later if it's audio */
                
            FILE *sfile_file = g_fopen(_priv->sub_filename, "r");
            gint sfile_sectsize = 96; /* Always */
            guint64 sfile_offset = _priv->offset * 96; /* Guess this one's always true, too */
            gint sfile_format = FR_BIN_SFILE_PW96_LIN | FR_BIN_SFILE_EXT;
                
            mirage_finterface_binary_track_file_set_handle(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_file, NULL);
            mirage_finterface_binary_track_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_sectsize, NULL);
            mirage_finterface_binary_track_file_set_offset(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_offset, NULL);
            mirage_finterface_binary_track_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_format, NULL);
                
            mirage_finterface_binary_subchannel_file_set_handle(MIRAGE_FINTERFACE_BINARY(data_fragment), sfile_file, NULL);
            mirage_finterface_binary_subchannel_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), sfile_sectsize, NULL);
            mirage_finterface_binary_subchannel_file_set_offset(MIRAGE_FINTERFACE_BINARY(data_fragment), sfile_offset, NULL);
            mirage_finterface_binary_subchannel_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), sfile_format, NULL);
            
            mirage_track_add_fragment(MIRAGE_TRACK(cur_track), -1, &data_fragment, NULL);
            
            /* Track mode */
            if (ccd_cur_entry->Mode) {
                /* Provided via [Track] entry */
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track mode is provided (0x%X)\n", __debug__, ccd_cur_entry->Mode);
                switch (ccd_cur_entry->Mode) {
                    case 0: {
                        mirage_track_set_mode(MIRAGE_TRACK(cur_track), MIRAGE_MODE_AUDIO, NULL);
                        break;
                    }
                    case 1: {
                        mirage_track_set_mode(MIRAGE_TRACK(cur_track), MIRAGE_MODE_MODE1, NULL);
                        break;
                    }
                    case 2: {
                        mirage_track_set_mode(MIRAGE_TRACK(cur_track), MIRAGE_MODE_MODE2_MIXED, NULL);
                        break;
                    }
                    default: {
                        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: invalid mode %d!\n", __debug__, ccd_cur_entry->Mode);
                    }
                }
            } else {
                /* Determine it manually */
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track mode is not provided\n", __debug__);
                __mirage_parser_ccd_determine_track_mode(self, cur_track, NULL);
            }
            
            /* If track mode is determined to be audio, set fragment's format accordingly */
            gint mode = 0;
            mirage_track_get_mode(MIRAGE_TRACK(cur_track), &mode, NULL);
            if (mode == MIRAGE_MODE_AUDIO) {
                mirage_finterface_binary_track_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), FR_BIN_TFILE_AUDIO, NULL);
            }
            
            /* ISRC */
            if (ccd_cur_entry->ISRC) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting ISRC to %.12s\n", __debug__, ccd_cur_entry->ISRC);
                mirage_track_set_isrc(MIRAGE_TRACK(cur_track), ccd_cur_entry->ISRC, NULL);
            }
            
            
            /* Pregap of current track; note that first track in the session does
               not seem to need Index 0 entry. Another thing to note: Index addresses
               seem to be relative to session start; so we use their difference
               to calculate the pregap and then subtract it from PLBA, which is
               relative to disc start */
            gint cur_pregap = 0;
            gint num_tracks = 0;
            mirage_session_get_number_of_tracks(MIRAGE_SESSION(cur_session), &num_tracks, NULL);
            if ((num_tracks == 1 && ccd_cur_entry->Index1) ||
                (ccd_cur_entry->Index0 && ccd_cur_entry->Index1)) {
                /* If Index 0 is not set (first track in session), it's 0 and 
                   the formula still works */
                cur_pregap = ccd_cur_entry->Index1 - ccd_cur_entry->Index0;
                
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: pregap determined to be 0x%X (%i)\n", __debug__, cur_pregap, cur_pregap);
                mirage_track_set_track_start(MIRAGE_TRACK(cur_track), cur_pregap, NULL);
            }
            
            /* Pregap of next track; this one is needed to properly calculate
               fragment length (otherwise pregap of next track gets appended
               to current track) */
            gint next_pregap = 0;
            if (ccd_next_entry->Index0 && ccd_next_entry->Index1) {
                next_pregap = ccd_next_entry->Index1 - ccd_next_entry->Index0;
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: next track's pregap determined to be 0x%X (%i)\n", __debug__, next_pregap, next_pregap);
            }
            
            /* Fragment length calculation magic... */
            gint track_start = ccd_cur_entry->PLBA - cur_pregap;
            gint track_end = ccd_next_entry->PLBA - next_pregap;
            
            fragment_length = track_end - track_start;
            
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: fragment length determined to be 0x%X (%i)\n", __debug__, fragment_length, fragment_length);
            mirage_fragment_set_length(MIRAGE_FRAGMENT(data_fragment), fragment_length, NULL);
            
            /* Update offset */
            _priv->offset += fragment_length;
            
            g_object_unref(data_fragment);
            g_object_unref(cur_track);
            g_object_unref(cur_session);
        }
        
    }
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finishing the layout\n", __debug__);
    /* Finish disc layout (i.e. guess medium type and set pregaps if necessary) */
    gint medium_type = mirage_parser_guess_medium_type(self, _priv->disc);
    mirage_disc_set_medium_type(MIRAGE_DISC(_priv->disc), medium_type, NULL);
    if (medium_type == MIRAGE_MEDIUM_CD) {
        mirage_parser_add_redbook_pregap(self, _priv->disc, NULL);
    }
    
    return TRUE;
}





/******************************************************************************\
 *                           Regex parsing engine                             *
\******************************************************************************/
/*** [CloneCD] ***/
static gboolean __mirage_parser_ccd_callback_clonecd (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n"); /* To make log more readable */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed [CloneCD] header\n", __debug__);
    
    _priv->header = g_new0(CCD_CloneCD, 1);
    _priv->cur_data = _priv->header;
    
    _priv->cur_rules = _priv->regex_rules_clonecd;
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_callback_clonecd_version (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    CCD_CloneCD *clonecd = _priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: Version = %s\n", __debug__, value_str);
    clonecd->Version = g_strtod(value_str, NULL);
    
    g_free(value_str);
    
    return TRUE;
}


/*** [Disc] ***/
static gboolean __mirage_parser_ccd_callback_disc (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n"); /* To make log more readable */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed [Disc] header\n", __debug__);
    
    _priv->disc_data = g_new0(CCD_Disc, 1);
    _priv->cur_data = _priv->disc_data;
    
    _priv->cur_rules = _priv->regex_rules_disc;
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_callback_disc_toc_entries (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    CCD_Disc *disc = _priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: TocEntries = %s\n", __debug__, value_str);
    disc->TocEntries = g_strtod(value_str, NULL);
    
    g_free(value_str);
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_callback_disc_sessions (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    CCD_Disc *disc = _priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: Sessions = %s\n", __debug__, value_str);
    disc->Sessions = g_strtod(value_str, NULL);
    
    g_free(value_str);
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_callback_disc_data_tracks_scrambled (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    CCD_Disc *disc = _priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: DataTracksScrambled = %s\n", __debug__, value_str);
    disc->DataTracksScrambled = g_strtod(value_str, NULL);
    
    g_free(value_str);
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_callback_disc_cdtext_length (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    CCD_Disc *disc = _priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: CDTextLength = %s\n", __debug__, value_str);
    disc->CDTextLength = g_strtod(value_str, NULL);
    
    g_free(value_str);
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_callback_disc_catalog (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    CCD_Disc *disc = _priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: Catalog = %s\n", __debug__, value_str);
    disc->Catalog = value_str;
    
    /*g_free(value_str);*/
    
    return TRUE;
}


/*** [Session X] ***/
static gboolean __mirage_parser_ccd_callback_session (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    gchar *number_str = g_match_info_fetch_named(match_info, "number");
    CCD_Session *session;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n"); /* To make log more readable */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed [Session %s] header\n", __debug__, number_str);
    
    session = g_new0(CCD_Session, 1);
    session->number = g_strtod(number_str, NULL);
    
    _priv->sessions_list = g_list_append(_priv->sessions_list, session);
    _priv->cur_data = session;

    _priv->cur_rules = _priv->regex_rules_session;

    g_free(number_str);
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_callback_session_pregap_mode (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    CCD_Session *session = _priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: PreGapMode = %s\n", __debug__, value_str);
    session->PreGapMode = g_strtod(value_str, NULL);
    
    g_free(value_str);
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_callback_session_pregap_subc (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    CCD_Session *session = _priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: PreGapSubC = %s\n", __debug__, value_str);
    session->PreGapSubC = g_strtod(value_str, NULL);
    
    g_free(value_str);
    
    return TRUE;
}


/*** [Entry X] ***/
static gboolean __mirage_parser_ccd_callback_entry (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    gchar *number_str = g_match_info_fetch_named(match_info, "number");
    CCD_Entry *entry;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n"); /* To make log more readable */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed [Entry %s] header\n", __debug__, number_str);
    
    entry = g_new0(CCD_Entry, 1);
    entry->number = g_strtod(number_str, NULL);
    
    _priv->entries_list = g_list_append(_priv->entries_list, entry);
    _priv->cur_data = entry;
    
    _priv->cur_rules = _priv->regex_rules_entry;

    g_free(number_str);
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_callback_entry_session (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    CCD_Entry *entry = _priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: Session = %s\n", __debug__, value_str);
    entry->Session = g_strtod(value_str, NULL);
    
    g_free(value_str);
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_callback_entry_point (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    CCD_Entry *entry = _priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: Point = %s\n", __debug__, value_str);
    entry->Point = g_strtod(value_str, NULL);
    
    g_free(value_str);
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_callback_entry_adr (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    CCD_Entry *entry = _priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: ADR = %s\n", __debug__, value_str);
    entry->ADR = g_strtod(value_str, NULL);
    
    g_free(value_str);
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_callback_entry_control (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    CCD_Entry *entry = _priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: Control = %s\n", __debug__, value_str);
    entry->Control = g_strtod(value_str, NULL);
    
    g_free(value_str);
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_callback_entry_trackno (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    CCD_Entry *entry = _priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: TrackNo = %s\n", __debug__, value_str);
    entry->TrackNo = g_strtod(value_str, NULL);
    
    g_free(value_str);
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_callback_entry_amin (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    CCD_Entry *entry = _priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: AMin = %s\n", __debug__, value_str);
    entry->AMin = g_strtod(value_str, NULL);
    
    g_free(value_str);
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_callback_entry_asec (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    CCD_Entry *entry = _priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: ASec = %s\n", __debug__, value_str);
    entry->ASec = g_strtod(value_str, NULL);
    
    g_free(value_str);
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_callback_entry_aframe (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    CCD_Entry *entry = _priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: AFrame = %s\n", __debug__, value_str);
    entry->AFrame = g_strtod(value_str, NULL);
    
    g_free(value_str);
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_callback_entry_alba (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    CCD_Entry *entry = _priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: ALBA = %s\n", __debug__, value_str);
    entry->ALBA = g_strtod(value_str, NULL);
    
    g_free(value_str);
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_callback_entry_zero (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    CCD_Entry *entry = _priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: Zero = %s\n", __debug__, value_str);
    entry->Zero = g_strtod(value_str, NULL);
    
    g_free(value_str);
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_callback_entry_pmin (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    CCD_Entry *entry = _priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: PMin = %s\n", __debug__, value_str);
    entry->PMin = g_strtod(value_str, NULL);
    
    g_free(value_str);
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_callback_entry_psec (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    CCD_Entry *entry = _priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: PSec = %s\n", __debug__, value_str);
    entry->PSec = g_strtod(value_str, NULL);
    
    g_free(value_str);
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_callback_entry_pframe (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    CCD_Entry *entry = _priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: PFrame = %s\n", __debug__, value_str);
    entry->PFrame = g_strtod(value_str, NULL);
    
    g_free(value_str);
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_callback_entry_plba (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    CCD_Entry *entry = _priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: PLBA = %s\n", __debug__, value_str);
    entry->PLBA = g_strtod(value_str, NULL);
    
    g_free(value_str);
    
    return TRUE;
}

/*** [TRACK X] ***/
static gint __find_entry_by_point (CCD_Entry *entry, gpointer data) {
    gint point = GPOINTER_TO_INT(data);
    return !(entry->Point == point);
}

static gboolean __mirage_parser_ccd_callback_track (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    gchar *number_str = g_match_info_fetch_named(match_info, "number");
    gint number = g_strtod(number_str, NULL);
    GList *entry;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n"); /* To make log more readable */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed [TRACK %s] header\n", __debug__, number_str);
    
    /* Get corresponding entry data and store the pointer */
    entry = g_list_find_custom(_priv->entries_list, GINT_TO_POINTER(number), (GCompareFunc)__find_entry_by_point);
    if (!entry) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get entry with point #%d!\n", __debug__, number);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }
    _priv->cur_data = entry->data;
    
    _priv->cur_rules = _priv->regex_rules_track;
    
    g_free(number_str);
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_callback_track_mode (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    CCD_Entry *entry = _priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: MODE = %s\n", __debug__, value_str);
    entry->Mode = g_strtod(value_str, NULL);
    
    g_free(value_str);
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_callback_track_index0 (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    CCD_Entry *entry = _priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: INDEX 0 = %s\n", __debug__, value_str);
    entry->Index0 = g_strtod(value_str, NULL);
    
    g_free(value_str);
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_callback_track_index1 (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    CCD_Entry *entry = _priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: INDEX 1 = %s\n", __debug__, value_str);
    entry->Index1 = g_strtod(value_str, NULL);
    
    g_free(value_str);
    
    return TRUE;
}

static gboolean __mirage_parser_ccd_callback_track_isrc (MIRAGE_Parser *self, GMatchInfo *match_info, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    CCD_Entry *entry = _priv->cur_data;
    gchar *value_str = g_match_info_fetch_named(match_info, "value");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsed: ISRC = %s\n", __debug__, value_str);
    entry->ISRC = value_str;
    
    /*g_free(value_str);*/
    
    return TRUE;
}

#define APPEND_REGEX_RULE(list,rule,callback) {                         \
    MIRAGE_RegexRule *new_rule = g_new(MIRAGE_RegexRule, 1);            \
    new_rule->regex = g_regex_new(rule, G_REGEX_OPTIMIZE, 0, NULL);     \
    new_rule->callback_func = callback;                                 \
                                                                        \
    list = g_list_append(list, new_rule);                               \
}

static void __mirage_parser_ccd_init_regex_parser (MIRAGE_Parser *self) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);

    /* Ignore empty lines */
    APPEND_REGEX_RULE(_priv->regex_rules, "^[\\s]*$", NULL);

    /* Section rules */
    APPEND_REGEX_RULE(_priv->regex_rules, "^\\s*\\[CloneCD\\]", __mirage_parser_ccd_callback_clonecd);
    APPEND_REGEX_RULE(_priv->regex_rules, "^\\s*\\[Disc\\]", __mirage_parser_ccd_callback_disc);
    APPEND_REGEX_RULE(_priv->regex_rules, "^\\s*\\[Session\\s*(?<number>\\d+)\\]", __mirage_parser_ccd_callback_session);
    APPEND_REGEX_RULE(_priv->regex_rules, "^\\s*\\[Entry\\s*(?<number>\\d+)\\]", __mirage_parser_ccd_callback_entry);
    APPEND_REGEX_RULE(_priv->regex_rules, "^\\s*\\[TRACK\\s*(?<number>\\d+)\\]", __mirage_parser_ccd_callback_track);
    
    /* [CloneCD] rules */
    APPEND_REGEX_RULE(_priv->regex_rules_clonecd, "^\\s*Version\\s*=\\s*(?<value>\\d+)", __mirage_parser_ccd_callback_clonecd_version);

    /* [Disc] rules */
    APPEND_REGEX_RULE(_priv->regex_rules_disc, "^\\s*TocEntries\\s*=\\s*(?<value>\\d+)", __mirage_parser_ccd_callback_disc_toc_entries);
    APPEND_REGEX_RULE(_priv->regex_rules_disc, "^\\s*Sessions\\s*=\\s*(?<value>\\d+)", __mirage_parser_ccd_callback_disc_sessions);
    APPEND_REGEX_RULE(_priv->regex_rules_disc, "^\\s*DataTracksScrambled\\s*=\\s*(?<value>\\d+)", __mirage_parser_ccd_callback_disc_data_tracks_scrambled);
    APPEND_REGEX_RULE(_priv->regex_rules_disc, "^\\s*CDTextLength\\s*=\\s*(?<value>\\d+)", __mirage_parser_ccd_callback_disc_cdtext_length);
    APPEND_REGEX_RULE(_priv->regex_rules_disc, "^\\s*CATALOG\\s*=\\s*(?<value>\\w+)", __mirage_parser_ccd_callback_disc_catalog);
    
    /* [Session X] rules */
    APPEND_REGEX_RULE(_priv->regex_rules_session, "^\\s*PreGapMode\\s*=\\s*(?<value>\\d+)", __mirage_parser_ccd_callback_session_pregap_mode);
    APPEND_REGEX_RULE(_priv->regex_rules_session, "^\\s*PreGapSubC\\s*=\\s*(?<value>\\d+)", __mirage_parser_ccd_callback_session_pregap_subc);
    
    /* [Entry X] rules */
    APPEND_REGEX_RULE(_priv->regex_rules_entry, "^\\s*Session\\s*=\\s*(?<value>\\d+)", __mirage_parser_ccd_callback_entry_session);
    APPEND_REGEX_RULE(_priv->regex_rules_entry, "^\\s*Point\\s*=\\s*(?<value>[\\w+]+)", __mirage_parser_ccd_callback_entry_point);
    APPEND_REGEX_RULE(_priv->regex_rules_entry, "^\\s*ADR\\s*=\\s*(?<value>\\w+)", __mirage_parser_ccd_callback_entry_adr);
    APPEND_REGEX_RULE(_priv->regex_rules_entry, "^\\s*Control\\s*=\\s*(?<value>\\w+)", __mirage_parser_ccd_callback_entry_control);
    APPEND_REGEX_RULE(_priv->regex_rules_entry, "^\\s*TrackNo\\s*=\\s*(?<value>\\d+)", __mirage_parser_ccd_callback_entry_trackno);
    APPEND_REGEX_RULE(_priv->regex_rules_entry, "^\\s*AMin\\s*=\\s*(?<value>\\d+)", __mirage_parser_ccd_callback_entry_amin);
    APPEND_REGEX_RULE(_priv->regex_rules_entry, "^\\s*ASec\\s*=\\s*(?<value>\\d+)", __mirage_parser_ccd_callback_entry_asec);
    APPEND_REGEX_RULE(_priv->regex_rules_entry, "^\\s*AFrame\\s*=\\s*(?<value>\\d+)", __mirage_parser_ccd_callback_entry_aframe);
    APPEND_REGEX_RULE(_priv->regex_rules_entry, "^\\s*ALBA\\s*=\\s*(?<value>-?\\d+)", __mirage_parser_ccd_callback_entry_alba);
    APPEND_REGEX_RULE(_priv->regex_rules_entry, "^\\s*Zero\\s*=\\s*(?<value>\\d+)", __mirage_parser_ccd_callback_entry_zero);
    APPEND_REGEX_RULE(_priv->regex_rules_entry, "^\\s*PMin\\s*=\\s*(?<value>\\d+)", __mirage_parser_ccd_callback_entry_pmin);
    APPEND_REGEX_RULE(_priv->regex_rules_entry, "^\\s*PSec\\s*=\\s*(?<value>\\d+)", __mirage_parser_ccd_callback_entry_psec);
    APPEND_REGEX_RULE(_priv->regex_rules_entry, "^\\s*PFrame\\s*=\\s*(?<value>\\d+)", __mirage_parser_ccd_callback_entry_pframe);
    APPEND_REGEX_RULE(_priv->regex_rules_entry, "^\\s*PLBA\\s*=\\s*(?<value>\\d+)", __mirage_parser_ccd_callback_entry_plba);
    
    /* [TRACK X] rules */
    APPEND_REGEX_RULE(_priv->regex_rules_track, "^\\s*MODE\\s*=\\s*(?<value>\\d+)", __mirage_parser_ccd_callback_track_mode);
    APPEND_REGEX_RULE(_priv->regex_rules_track, "^\\s*INDEX\\s*0\\s*=\\s*(?<value>\\d+)", __mirage_parser_ccd_callback_track_index0);
    APPEND_REGEX_RULE(_priv->regex_rules_track, "^\\s*INDEX\\s*1\\s*=\\s*(?<value>\\d+)", __mirage_parser_ccd_callback_track_index1);
    APPEND_REGEX_RULE(_priv->regex_rules_track, "^\\s*ISRC\\s*=\\s*(?<value>\\w+)", __mirage_parser_ccd_callback_track_isrc);
    
    return;
}

static void __free_regex_rules (GList *rules) {
    GList *entry;
    
    G_LIST_FOR_EACH(entry, rules) {
        MIRAGE_RegexRule *rule = entry->data;
        g_regex_unref(rule->regex);
        g_free(rule);
    }
    g_list_free(rules);
}

static void __mirage_parser_ccd_cleanup_regex_parser (MIRAGE_Parser *self) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    
    __free_regex_rules(_priv->regex_rules);
    __free_regex_rules(_priv->regex_rules_clonecd);
    __free_regex_rules(_priv->regex_rules_disc);
    __free_regex_rules(_priv->regex_rules_session);
    __free_regex_rules(_priv->regex_rules_entry);
    __free_regex_rules(_priv->regex_rules_track);
}

static gboolean __mirage_parser_ccd_parse_ccd_file (MIRAGE_Parser *self, gchar *filename, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    GError *io_error = NULL;
    GIOChannel *io_channel;
    gboolean succeeded = TRUE;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: opening file: %s\n", __debug__, filename);
    
    /* Create IO channel for file */
    io_channel = g_io_channel_new_file(filename, "r", &io_error);
    if (!io_channel) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create IO channel: %s\n", __debug__, io_error->message);
        g_error_free(io_error);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }
    
    /* If provided, use the specified encoding; otherwise, use default (UTF-8) */
    gchar *encoding = NULL;
    if (mirage_parser_get_param_string(self, "encoding", (const gchar **)&encoding, NULL)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: using specified encoding: %s\n", __debug__, encoding);
        g_io_channel_set_encoding(io_channel, encoding, NULL);
    }
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing\n", __debug__);
    
    /* Read file line-by-line */
    gint line_nr;
    for (line_nr = 1; ; line_nr++) {
        GIOStatus status;
        gchar *line_str;
        gsize line_len;
        
        status = g_io_channel_read_line(io_channel, &line_str, &line_len, NULL, &io_error);
        
        /* Handle EOF */
        if (status == G_IO_STATUS_EOF) {
            break;
        }
        
        /* Handle abnormal status */
        if (status != G_IO_STATUS_NORMAL) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: status %d while reading line #%d from IO channel: %s\n", __debug__, status, line_nr, io_error ? io_error->message : "no error message");
            g_error_free(io_error);
            
            mirage_error(MIRAGE_E_IMAGEFILE, error);
            succeeded = FALSE;
            break;
        }
        
        /* GRegex matching engine */
        GMatchInfo *match_info = NULL;
        gboolean matched = FALSE;
        GList *entry;
        
        /* If current rules are active, use those */
        if (_priv->cur_rules) {
            G_LIST_FOR_EACH(entry, _priv->cur_rules) {
                MIRAGE_RegexRule *regex_rule = entry->data;
                            
                /* Try to match the given rule */
                if (g_regex_match(regex_rule->regex, line_str, 0, &match_info)) {
                    if (regex_rule->callback_func) {
                        succeeded = regex_rule->callback_func(self, match_info, error);
                    }
                    matched = TRUE;
                }
                
                /* Must be freed in any case */
                g_match_info_free(match_info);
                
                /* Break if we had a match */
                if (matched) {
                    break;
                }
            }
        }
        
        /* If no match was found, try base rules */
        if (!matched) {
            G_LIST_FOR_EACH(entry, _priv->regex_rules) {
                MIRAGE_RegexRule *regex_rule = entry->data;
                            
                /* Try to match the given rule */
                if (g_regex_match(regex_rule->regex, line_str, 0, &match_info)) {
                    if (regex_rule->callback_func) {
                        succeeded = regex_rule->callback_func(self, match_info, error);
                    }
                    matched = TRUE;
                }
                
                /* Must be freed in any case */
                g_match_info_free(match_info);
                
                /* Break if we had a match */
                if (matched) {
                    break;
                }
            }
        }
        
        /* Complain if we failed to match the line (should it be fatal?) */
        if (!matched) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to match line #%d: %s\n", __debug__, line_nr, line_str);
            /* succeeded = FALSE */
        }
        
        g_free(line_str);
                
        /* In case callback didn't succeed... */
        if (!succeeded) {
            break;
        }
    }
    
    g_io_channel_unref(io_channel);
    
    return succeeded;
}

/******************************************************************************\
 *                     MIRAGE_Parser methods implementation                     *
\******************************************************************************/
static gboolean __mirage_parser_ccd_load_image (MIRAGE_Parser *self, gchar **filenames, GObject **disc, GError **error) {
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    
    /* Check if we can load the file; we check the suffix */
    if (!mirage_helper_has_suffix(filenames[0], ".ccd")) {
        mirage_error(MIRAGE_E_CANTHANDLE, error);
        return FALSE;
    }
    
    /* Create disc */
    _priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_attach_child(MIRAGE_OBJECT(self), _priv->disc, NULL);

    mirage_disc_set_filename(MIRAGE_DISC(_priv->disc), filenames[0], NULL);

    /* Compose image and subchannel filename */
    gint len = strlen(filenames[0]);
    _priv->img_filename = g_strdup(filenames[0]);
    _priv->sub_filename = g_strdup(filenames[0]);
    sprintf(_priv->img_filename+len-3, "img");
    sprintf(_priv->sub_filename+len-3, "sub");
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: assumed data file: %s\n", __debug__, _priv->img_filename);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: assumed subchannel file: %s\n", __debug__, _priv->sub_filename);
    
    /* Parse the CCD */
    if (!__mirage_parser_ccd_parse_ccd_file(self, filenames[0], error)) {
        succeeded = FALSE;
        goto end;
    }
    
    /* Build the layout */
    succeeded = __mirage_parser_ccd_build_disc_layout(self, error);
    
    /* Clean the parsed structures as they aren't needed anymore */
    __mirage_parser_ccd_clean_parsed_structures(self, NULL);
    
end:
    /* Return disc */
    mirage_object_detach_child(MIRAGE_OBJECT(self), _priv->disc, NULL);
    if (succeeded) {
        *disc = _priv->disc;
    } else {
        g_object_unref(_priv->disc);
        *disc = NULL;
    }
        
    return succeeded;
}


/******************************************************************************\
 *                                Object init                                 *
\******************************************************************************/
/* Our parent class */
static MIRAGE_ParserClass *parent_class = NULL;

static void __mirage_parser_ccd_instance_init (GTypeInstance *instance, gpointer g_class) {
    mirage_parser_generate_parser_info(MIRAGE_PARSER(instance),
        "PARSER-CCD",
        "CCD Image Parser",
        "CCD (CloneCD) images",
        "application/libmirage-ccd"
    );

    __mirage_parser_ccd_init_regex_parser(MIRAGE_PARSER(instance));

    return;
}

static void __mirage_parser_ccd_finalize (GObject *obj) {
    MIRAGE_Parser_CCD *self = MIRAGE_PARSER_CCD(obj);
    MIRAGE_Parser_CCDPrivate *_priv = MIRAGE_PARSER_CCD_GET_PRIVATE(self);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: finalizing object\n", __debug__);

    g_free(_priv->img_filename);
    g_free(_priv->sub_filename);
    
    /* Cleanup regex parser engine */
    __mirage_parser_ccd_cleanup_regex_parser(MIRAGE_PARSER(self));
    
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __debug__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __mirage_parser_ccd_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_ParserClass *class_parser = MIRAGE_PARSER_CLASS(g_class);
    MIRAGE_Parser_CCDClass *klass = MIRAGE_PARSER_CCD_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Parser_CCDPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_parser_ccd_finalize;
    
    /* Initialize MIRAGE_Parser methods */
    class_parser->load_image = __mirage_parser_ccd_load_image;
    
    return;
}

GType mirage_parser_ccd_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Parser_CCDClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_parser_ccd_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Parser_CCD),
            0,      /* n_preallocs */
            __mirage_parser_ccd_instance_init    /* instance_init */
        };
        
        type = g_type_module_register_type(module, MIRAGE_TYPE_PARSER, "MIRAGE_Parser_CCD", &info, 0);
    }
    
    return type;
}
