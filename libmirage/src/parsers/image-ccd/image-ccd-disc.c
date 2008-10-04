/*
 *  libMirage: CCD image parser: Disc object
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
 
#include "image-ccd.h"


/* Some prototypes from flex/bison */
int yylex_init (void *scanner);
void yyset_in  (FILE *in_str, void *yyscanner);
int yylex_destroy (void *yyscanner);
int yyparse (void *scanner, MIRAGE_Disc *self, GError **error);


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_DISC_CCD_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_DISC_CCD, MIRAGE_Disc_CCDPrivate))

typedef struct {       
    /* Data and subchannel filenames */
    gchar *img_filename;
    gchar *sub_filename;
    
    /* Offset within data/subchannel file */
    gint offset;
    
    /* Parsed data */
    CCD_CloneCD *header;
    
    CCD_Disc *disc;
    
    GList *sessions_list;
    
    GList *entries_list;
    CCD_Entry *cur_track;
    
    /* Parser info */
    MIRAGE_ParserInfo *parser_info;
} MIRAGE_Disc_CCDPrivate;


/******************************************************************************\
 *                         Disc private functions                             *
\******************************************************************************/
gboolean __mirage_disc_ccd_read_header (MIRAGE_Disc *self, CCD_CloneCD *header, GError **error) {
    MIRAGE_Disc_CCDPrivate *_priv = MIRAGE_DISC_CCD_GET_PRIVATE(self);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading header entry\n", __func__);
    _priv->header = header;
    return TRUE;
}

gboolean __mirage_disc_ccd_read_disc (MIRAGE_Disc *self, CCD_Disc *disc, GError **error) {
    MIRAGE_Disc_CCDPrivate *_priv = MIRAGE_DISC_CCD_GET_PRIVATE(self);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading disc entry\n", __func__);
    _priv->disc = disc;
    return TRUE;
}

gboolean __mirage_disc_ccd_read_disc_catalog (MIRAGE_Disc *self, gchar *catalog, GError **error) {
    MIRAGE_Disc_CCDPrivate *_priv = MIRAGE_DISC_CCD_GET_PRIVATE(self);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading disc catalog\n", __func__);
    _priv->disc->Catalog = catalog;
    return TRUE;
}

gboolean __mirage_disc_ccd_read_session (MIRAGE_Disc *self, CCD_Session *session, GError **error) {
    MIRAGE_Disc_CCDPrivate *_priv = MIRAGE_DISC_CCD_GET_PRIVATE(self);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading session entry\n", __func__);
    _priv->sessions_list = g_list_append(_priv->sessions_list, session);
    return TRUE;
}


static gint __sort_entries (CCD_Entry *entry1, CCD_Entry *entry2) {
    /* We sort entries by session; then, we put 0xA0 before 1-99, and we put
     0xA2 at the end. NOTE: the function compares newly added entry1 to already
     existing entry2... */
    if (entry2->Session == entry1->Session) {
        if (entry2->Point == 0xA0) {
            /* Put entry1 after entry2 which is 0xA0 */
            return 1;
        } else if (entry2->Point == 0xA2) {
            /* Put entry1 before entry2 which is 0xA2 */
            return -1;
        } else {
            return (entry1->Point < entry2->Point)*(-1) + (entry1->Point > entry2->Point)*(1);
        }
    } else {
        return (entry1->Session < entry2->Session)*(-1) + (entry1->Session > entry2->Session)*(1);
    }
}

static gint __find_entry_by_point (CCD_Entry *entry, gpointer data) {
    gint point = GPOINTER_TO_INT(data);
    return !(entry->Point == point);
}

gboolean __mirage_disc_ccd_read_entry (MIRAGE_Disc *self, CCD_Entry *entry, GError **error) {
    MIRAGE_Disc_CCDPrivate *_priv = MIRAGE_DISC_CCD_GET_PRIVATE(self);
    
    /* As far as entries go, we need only entries with point 1-99, 0xA0 (lead-in) 
       and 0xA2 (lead-out). So we'll ignore the rest, plus we'll put 0xA2 at 
       the end, to simplify later processing */
    if ((entry->Point > 0 && entry->Point < 99) || entry->Point == 0xA0 || entry->Point == 0xA2) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding entry #%d with point %02X\n", __func__, entry->number, entry->Point);
        /* Add to the list */
        _priv->entries_list = g_list_insert_sorted(_priv->entries_list, entry, (GCompareFunc)__sort_entries);
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: ignoring entry #%d with point %02X\n", __func__, entry->number, entry->Point);
        g_free(entry);
    }
    return TRUE;
}

gboolean __mirage_disc_ccd_read_track (MIRAGE_Disc *self, gint number, GError **error) {
    MIRAGE_Disc_CCDPrivate *_priv = MIRAGE_DISC_CCD_GET_PRIVATE(self);
    GList *entry = NULL;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading track entry for track #%d\n", __func__, number);
        
    /* Store a pointer to the corresponding entry */
    entry = g_list_find_custom(_priv->entries_list, GINT_TO_POINTER(number), (GCompareFunc)__find_entry_by_point);
    if (!entry) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get entry!\n", __func__);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }
    _priv->cur_track = entry->data;
    
    return TRUE;
}

gboolean __mirage_disc_ccd_read_track_mode (MIRAGE_Disc *self, gint mode, GError **error) {
    MIRAGE_Disc_CCDPrivate *_priv = MIRAGE_DISC_CCD_GET_PRIVATE(self);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading track mode (%d)\n", __func__, mode);
    
    /* Decode mode at this point in order to simplify later processing */
    switch (mode) {
        case 0: {
            /* Audio */
            _priv->cur_track->Mode = MIRAGE_MODE_AUDIO;
            break;
        }
        case 1: {
            /* Mode 1 */
            _priv->cur_track->Mode = MIRAGE_MODE_MODE1;
            break;
        }
        case 2: {
            /* Mode 2 */
            _priv->cur_track->Mode = MIRAGE_MODE_MODE2_MIXED;
            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unhandled mode %d!\n", __func__, mode);
        }
    }

    return TRUE;
}

gboolean __mirage_disc_ccd_read_track_index0 (MIRAGE_Disc *self, gint index0, GError **error) {
    MIRAGE_Disc_CCDPrivate *_priv = MIRAGE_DISC_CCD_GET_PRIVATE(self);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading index 0\n", __func__);
    _priv->cur_track->Index0 = index0;
    return TRUE;
}

gboolean __mirage_disc_ccd_read_track_index1 (MIRAGE_Disc *self, gint index1, GError **error) {
    MIRAGE_Disc_CCDPrivate *_priv = MIRAGE_DISC_CCD_GET_PRIVATE(self);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading index 1\n", __func__);
    _priv->cur_track->Index1 = index1;
    return TRUE;
}

gboolean __mirage_disc_ccd_read_track_isrc (MIRAGE_Disc *self, gchar *isrc, GError **error) {
    MIRAGE_Disc_CCDPrivate *_priv = MIRAGE_DISC_CCD_GET_PRIVATE(self);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading ISRC\n", __func__);
    _priv->cur_track->ISRC = isrc;
    return TRUE;
}


static gboolean __mirage_disc_ccd_determine_track_mode (MIRAGE_Disc *self, GObject *track, GError **error) {
    GObject *data_fragment = NULL;
    guint64 offset = 0;
    FILE *file = NULL;
    size_t blocks_read;
    guint8 sync[12] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
    guint8 buf[24] = {0};

    /* Get last fragment */
    if (!mirage_track_get_fragment_by_index(MIRAGE_TRACK(track), -1, &data_fragment, NULL)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get fragment\n", __func__);
        return FALSE;
    }
    
    /* 2352-byte sectors are assumed */
    mirage_finterface_binary_track_file_get_handle(MIRAGE_FINTERFACE_BINARY(data_fragment), &file, NULL);
    mirage_finterface_binary_track_file_get_position(MIRAGE_FINTERFACE_BINARY(data_fragment), 0, &offset, NULL);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: checking for track type in data file at location 0x%llX\n", __func__, offset);
    fseeko(file, offset, SEEK_SET);
    blocks_read = fread(buf, 24, 1, file);
    if (blocks_read < 1) return FALSE;
    if (!memcmp(buf, sync, 12)) {
        switch (buf[15]) {
            case 0x01: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Mode 1 track\n", __func__);
                mirage_track_set_mode(MIRAGE_TRACK(track), MIRAGE_MODE_MODE1, NULL);
                break;
            }
            case 0x02: {
                /* Mode 2; let's say we're Mode 2 Mixed and let the sector
                   code do the rest for us */
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Mode 2 track; setting to mixed...\n", __func__);
                mirage_track_set_mode(MIRAGE_TRACK(track), MIRAGE_MODE_MODE2_MIXED, NULL);
                break;
            }
        }
    } else { 
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Audio track\n", __func__);
        mirage_track_set_mode(MIRAGE_TRACK(track), MIRAGE_MODE_AUDIO, NULL);
    }
    
    g_object_unref(data_fragment);
    
    return TRUE;
}

static gboolean __mirage_disc_ccd_clean_parsed_structures (MIRAGE_Disc *self, GError **error) {
    MIRAGE_Disc_CCDPrivate *_priv = MIRAGE_DISC_CCD_GET_PRIVATE(self);
    GList *entry = NULL;
        
    /* CloneCD header */
    g_free(_priv->header);
    
    /* Disc */
    g_free(_priv->disc->Catalog);
    g_free(_priv->disc);
    
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
    
    /* Current track pointer */
    _priv->cur_track = NULL;
    
    return TRUE;
}

static gboolean __mirage_disc_ccd_build_disc_layout (MIRAGE_Disc *self, GError **error) {
    MIRAGE_Disc_CCDPrivate *_priv = MIRAGE_DISC_CCD_GET_PRIVATE(self);
    GList *entry = NULL;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: building disc layout\n", __func__);
    
    if (_priv->disc->Catalog) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting disc catalog to %.13s\n", __func__, _priv->disc->Catalog);
        mirage_disc_set_mcn(self, _priv->disc->Catalog, NULL);
    }
    
    /* Go over stored entries and build the layout */
    G_LIST_FOR_EACH(entry, _priv->entries_list) {
        CCD_Entry *ccd_cur_entry = entry->data;
        CCD_Entry *ccd_next_entry = entry->next ? entry->next->data : NULL;
        
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n", __func__);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: processing entry; point %02X, session %d\n", __func__, ccd_cur_entry->Point, ccd_cur_entry->Session);
        
        if (ccd_cur_entry->Point == 0xA0) {
            /* 0xA0 is entry each session should begin with... so add the session here */
            GObject *cur_session = NULL;
        
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding session #%i\n", __func__, ccd_cur_entry->Session);
        
            if (!mirage_disc_add_session_by_number(self, ccd_cur_entry->Session, &cur_session, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __func__);
                return FALSE;
            }
            mirage_session_set_session_type(MIRAGE_SESSION(cur_session), ccd_cur_entry->PSec, NULL); /* PSEC = Disc Type */
            
            g_object_unref(cur_session);
        } else if (ccd_cur_entry->Point == 0xA2) {
            /* 0xA2 is entry each session should end with... */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: closing session #%i\n", __func__, ccd_cur_entry->Session);
            
            /* If there is next entry, we're dealing with multi-session disc; in 
               this case, we need to set leadout length */
            if (ccd_next_entry) {
                GObject *cur_session = NULL;
                gint leadout_length = 0;
                
                if (!mirage_disc_get_session_by_number(self, ccd_cur_entry->Session, &cur_session, error)) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get session %i!\n", __func__, ccd_cur_entry->Session);
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
            
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding track #%d\n", __func__, ccd_cur_entry->Point);
            
            /* Shouldn't really happen... */
            if (!ccd_next_entry) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: ccd_next_entry == NULL; shouldn't happen!\n", __func__);
                mirage_error(MIRAGE_E_PARSER, error);
                return FALSE;
            }
            
            /* Grab the session */
            if (!mirage_disc_get_session_by_number(self, ccd_cur_entry->Session, &cur_session, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get session %i!\n", __func__, ccd_cur_entry->Session);
                return FALSE;
            }
            
            /* Add the track */
            if (!mirage_session_add_track_by_number(MIRAGE_SESSION(cur_session), ccd_cur_entry->Point, &cur_track, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __func__);
                g_object_unref(cur_session);
                return FALSE;
            }
            
            /* Get Mirage and have it make us fragment */
            GObject *mirage = NULL;

            if (!mirage_object_get_mirage(MIRAGE_OBJECT(self), &mirage, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get Mirage object!\n", __func__);
                g_object_unref(cur_track);
                g_object_unref(cur_session);
                return FALSE;
            }
            
            GObject *data_fragment = NULL;
            mirage_mirage_create_fragment(MIRAGE_MIRAGE(mirage), MIRAGE_TYPE_FINTERFACE_BINARY, _priv->img_filename, &data_fragment, error);
            g_object_unref(mirage);
            if (!data_fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create data fragment!\n", __func__);
                g_object_unref(cur_track);
                g_object_unref(cur_session);
                return FALSE;
            }
            
            /* Data fragment */
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
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track mode is provided (0x%X)\n", __func__, ccd_cur_entry->Mode);
                mirage_track_set_mode(MIRAGE_TRACK(cur_track), ccd_cur_entry->Mode, NULL);
            } else {
                /* Determine it manually */
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track mode is not provided\n", __func__);
                __mirage_disc_ccd_determine_track_mode(self, cur_track, NULL);
            }
            
            /* If track mode is determined to be audio, set fragment's format accordingly */
            gint mode = 0;
            mirage_track_get_mode(MIRAGE_TRACK(cur_track), &mode, NULL);
            if (mode == MIRAGE_MODE_AUDIO) {
                mirage_finterface_binary_track_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), FR_BIN_TFILE_AUDIO, NULL);
            }
            
            /* ISRC */
            if (ccd_cur_entry->ISRC) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting ISRC to %.12s\n", __func__, ccd_cur_entry->ISRC);
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
                
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: pregap determined to be 0x%X (%i)\n", __func__, cur_pregap, cur_pregap);
                mirage_track_set_track_start(MIRAGE_TRACK(cur_track), cur_pregap, NULL);
            }
            
            /* Pregap of next track; this one is needed to properly calculate
               fragment length (otherwise pregap of next track gets appended
               to current track) */
            gint next_pregap = 0;
            if (ccd_next_entry->Index0 && ccd_next_entry->Index1) {
                next_pregap = ccd_next_entry->Index1 - ccd_next_entry->Index0;
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: next track's pregap determined to be 0x%X (%i)\n", __func__, next_pregap, next_pregap);
            }
            
            /* Fragment length calculation magic... */
            gint track_start = ccd_cur_entry->PLBA - cur_pregap;
            gint track_end = ccd_next_entry->PLBA - next_pregap;
            
            fragment_length = track_end - track_start;
            
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: fragment length determined to be 0x%X (%i)\n", __func__, fragment_length, fragment_length);
            mirage_fragment_set_length(MIRAGE_FRAGMENT(data_fragment), fragment_length, NULL);
            
            /* Update offset */
            _priv->offset += fragment_length;
            
            g_object_unref(data_fragment);
            g_object_unref(cur_track);
            g_object_unref(cur_session);
        }
        
    }
    
    /* Finish disc layout (i.e. guess medium type and set pregaps if necessary) */
    gint medium_type = mirage_helper_guess_medium_type(self);
    mirage_disc_set_medium_type(self, medium_type, NULL);
    if (medium_type == MIRAGE_MEDIUM_CD) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: disc length implies CD-ROM image; setting Red Book pregaps\n", __func__);
        mirage_helper_add_redbook_pregap(self);
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: disc length implies non CD-ROM image\n", __func__);
    }
    
    /* Clean the parsed structures as they aren't needed anymore */
    if (!__mirage_disc_ccd_clean_parsed_structures(self, error)) {
        return FALSE;
    }
    
    return TRUE;
}


/******************************************************************************\
 *                     MIRAGE_Disc methods implementation                     *
\******************************************************************************/
static gboolean __mirage_disc_ccd_get_parser_info (MIRAGE_Disc *self, MIRAGE_ParserInfo **parser_info, GError **error) {
    MIRAGE_Disc_CCDPrivate *_priv = MIRAGE_DISC_CCD_GET_PRIVATE(self);
    *parser_info = _priv->parser_info;
    return TRUE;
}

static gboolean __mirage_disc_ccd_can_load_file (MIRAGE_Disc *self, gchar *filename, GError **error) {
    MIRAGE_Disc_CCDPrivate *_priv = MIRAGE_DISC_CCD_GET_PRIVATE(self);
    
    /* Does file exist? */
    if (!g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
        return FALSE;
    }
    
    /* Check the suffixes we support... */
    return mirage_helper_match_suffixes(filename, _priv->parser_info->suffixes);
}

static gboolean __mirage_disc_ccd_load_image (MIRAGE_Disc *self, gchar **filenames, GError **error) {
    MIRAGE_Disc_CCDPrivate *_priv = MIRAGE_DISC_CCD_GET_PRIVATE(self);

    void *scanner = NULL;
    FILE *file = NULL;
    
    /* For now, CCD parser supports only one-file images */
    if (g_strv_length(filenames) > 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: only single-file images supported!\n", __func__);
        mirage_error(MIRAGE_E_SINGLEFILE, error);
        return FALSE;
    }
    
    /* Compose image and subchannel filename */
    gchar *img_filename = g_strdup(filenames[0]);
    gchar *sub_filename = g_strdup(filenames[0]);
    gint len = strlen(filenames[0]);
    sprintf(img_filename+len-3, "img");
    sprintf(sub_filename+len-3, "sub");
    
    _priv->img_filename = img_filename;
    _priv->sub_filename = sub_filename;    
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: assumed data file: %s\n", __func__, img_filename);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: assumed subchannel file: %s\n", __func__, sub_filename);
    
    /* Open file */
    file = g_fopen(filenames[0], "r");
    if (!file) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open file '%s'!\n", __func__, filenames[0]);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }
    
    mirage_disc_set_filenames(self, filenames, NULL);
    
    /* Prepare scanner */
    yylex_init(&scanner);
    yyset_in(file, scanner);
    
    /* Load */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing\n", __func__);
    if (yyparse(scanner, MIRAGE_DISC(self), error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse CCD file!\n", __func__);
        fclose(file);
        return FALSE;
    }
    
    /* Destroy scanner */
    yylex_destroy(scanner);        
    fclose(file);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished parsing\n", __func__);
    
    return __mirage_disc_ccd_build_disc_layout(self, error);
}


/******************************************************************************\
 *                                Object init                                 *
\******************************************************************************/
/* Our parent class */
static MIRAGE_DiscClass *parent_class = NULL;

static void __mirage_disc_b6t_instance_init (GTypeInstance *instance, gpointer g_class) {
    MIRAGE_Disc_CCD *self = MIRAGE_DISC_CCD(instance);
    MIRAGE_Disc_CCDPrivate *_priv = MIRAGE_DISC_CCD_GET_PRIVATE(self);
    
    /* Create parser info */
    _priv->parser_info = mirage_helper_create_parser_info(
        "PARSER-CDI",
        "CDI Image Parser",
        "1.0.0",
        "Rok Mandeljc",
        TRUE,
        "CCD (CloneCD) images",
        2, ".ccd", NULL
    );
    
    return;
}

static void __mirage_disc_ccd_finalize (GObject *obj) {
    MIRAGE_Disc_CCD *self = MIRAGE_DISC_CCD(obj);
    MIRAGE_Disc_CCDPrivate *_priv = MIRAGE_DISC_CCD_GET_PRIVATE(self);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s:\n", __func__);

    g_free(_priv->img_filename);
    g_free(_priv->sub_filename);
    
    /* Free parser info */
    mirage_helper_destroy_parser_info(_priv->parser_info);
    
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __func__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __mirage_disc_ccd_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_DiscClass *class_disc = MIRAGE_DISC_CLASS(g_class);
    MIRAGE_Disc_CCDClass *klass = MIRAGE_DISC_CCD_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Disc_CCDPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_disc_ccd_finalize;
    
    /* Initialize MIRAGE_Disc methods */
    class_disc->get_parser_info = __mirage_disc_ccd_get_parser_info;
    class_disc->can_load_file = __mirage_disc_ccd_can_load_file;
    class_disc->load_image = __mirage_disc_ccd_load_image;
    
    return;
}

GType mirage_disc_ccd_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Disc_CCDClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_disc_ccd_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Disc_CCD),
            0,      /* n_preallocs */
            __mirage_disc_b6t_instance_init    /* instance_init */
        };
        
        type = g_type_module_register_type(module, MIRAGE_TYPE_DISC, "MIRAGE_Disc_CCD", &info, 0);
    }
    
    return type;
}
