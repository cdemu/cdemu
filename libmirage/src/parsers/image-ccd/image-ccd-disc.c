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
    gint prev_plba;
    
    gint leadout_address;
    gchar *img_filename;
    gchar *sub_filename;
    
    gint offset;
    
    gint cur_track;
    
    /* Parser info */
    MIRAGE_ParserInfo *parser_info;
} MIRAGE_Disc_CCDPrivate;


/******************************************************************************\
 *                         Disc private functions                             *
\******************************************************************************/
gboolean __mirage_disc_ccd_track_set_isrc (MIRAGE_Disc *self, gchar *isrc, GError **error) {
    MIRAGE_Disc_CCD *self_ccd = MIRAGE_DISC_CCD(self);
    MIRAGE_Disc_CCDPrivate *_priv = MIRAGE_DISC_CCD_GET_PRIVATE(self_ccd);
    GObject *track = NULL;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting ISRC for Track %02i to %s\n", __func__, _priv->cur_track, isrc);
    
    if (!mirage_disc_get_track_by_number(self, _priv->cur_track, &track, NULL)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get Track %02i!\n", __func__, _priv->cur_track);
        return FALSE;
    }
    
    mirage_track_set_isrc(MIRAGE_TRACK(track), isrc, NULL);
        
    g_object_unref(track);
    
    return TRUE;
}

gboolean __mirage_disc_ccd_set_current_track (MIRAGE_Disc *self, gint track, GError **error) {
    MIRAGE_Disc_CCD *self_ccd = MIRAGE_DISC_CCD(self);
    MIRAGE_Disc_CCDPrivate *_priv = MIRAGE_DISC_CCD_GET_PRIVATE(self_ccd);
    
    _priv->cur_track = track;
    
    return TRUE;
}


gboolean __mirage_disc_ccd_decode_disc_section (MIRAGE_Disc *self, gint session, GError **error) {
    /* Nothing to do here, actually */
    return TRUE;
}

static gboolean __mirage_disc_ccd_determine_track_mode (MIRAGE_Disc *self, GObject *track, GError **error) {
    GObject *data_fragment = NULL;
    guint64 offset = 0;
    FILE *file = NULL;
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
    fread(buf, 1, 24, file);
    
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
        mirage_finterface_binary_track_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), FR_BIN_TFILE_AUDIO, NULL);
    }
    
    g_object_unref(data_fragment);
    
    return TRUE;
}

static gboolean __mirage_disc_ccd_set_last_track_length (MIRAGE_Disc *self, GObject *session, gint fragment_length, GError **error) {
    MIRAGE_Disc_CCD *self_ccd = MIRAGE_DISC_CCD(self);
    MIRAGE_Disc_CCDPrivate *_priv = MIRAGE_DISC_CCD_GET_PRIVATE(self_ccd);
    GObject *prev_track = NULL;
    
    if (mirage_session_get_track_by_index(MIRAGE_SESSION(session), -1, &prev_track, NULL)) {
        GObject *prev_fragment = NULL;
        
        mirage_track_get_fragment_by_index(MIRAGE_TRACK(prev_track), -1, &prev_fragment, NULL);
        mirage_fragment_set_length(MIRAGE_FRAGMENT(prev_fragment), fragment_length, NULL);
        g_object_unref(prev_fragment);
            
        /* Also, update our offset within data and sub file accordingly */
        _priv->offset += fragment_length;
        
        g_object_unref(prev_track);
    }
    
    return TRUE;
}

gboolean __mirage_disc_ccd_decode_entry_section (MIRAGE_Disc *self, gint session, gint point, gint adr, gint ctl, gint tno, gint amin, gint asec, gint aframe, gint alba, gint zero, gint pmin, gint psec, gint pframe, gint plba, GError **error) {
    MIRAGE_Disc_CCD *self_ccd = MIRAGE_DISC_CCD(self);
    MIRAGE_Disc_CCDPrivate *_priv = MIRAGE_DISC_CCD_GET_PRIVATE(self_ccd);
    
    /* Depending on point... */
    if (point == 0xA0) {
        /* 0xA0 is entry each session should begin with... so add the session here */
        GObject *prev_session = NULL;
        GObject *cur_session = NULL;
        
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding session #%i\n", __func__, session);
                
        /* Actually, first we try to close the previous session */
        if (mirage_disc_get_session_by_index(self, -1, &prev_session, NULL)) {
            gint ltrack_length = 0;
            gint leadout_length = 0;
            gint i;
            
            /* Calculate the length of last track in previous session (0xA0 entry
               tends to appear before 0xA1, therefore we still have leadout address
               of previous session stored in our struct...) */
            ltrack_length =_priv->leadout_address - _priv->prev_plba;
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: closing previous session (last track length: 0x%X (%i)\n", __func__, ltrack_length, ltrack_length);
            __mirage_disc_ccd_set_last_track_length(self, prev_session, ltrack_length, NULL);
            
            /* Multi-session disc requires us to set up leadout length; to calculate
               it from the image itself, we'd need to get the start address of the
               first track in next session, which at this point we don't have. So
               for now, we'll assume the standard values... */
            mirage_session_layout_get_session_number(MIRAGE_SESSION(prev_session), &i, NULL);
            if (i == 1) {
                leadout_length = 11250;
            } else {
                leadout_length = 6750;
            }
            
            mirage_session_set_leadout_length(MIRAGE_SESSION(prev_session), leadout_length, NULL);
            
            g_object_unref(prev_session);
        }
        
        if (!mirage_disc_add_session_by_number(self, session, &cur_session, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __func__);
            return FALSE;
        }
        mirage_session_set_session_type(MIRAGE_SESSION(cur_session), psec, NULL); /* PSEC = Disc Type */
        
        g_object_unref(cur_session);
    } else if (point == 0xA1) {
        /* Nothing to do here */
    } else if (point == 0xA2) {
        /* 0xA2: start location of the leadout area */
        _priv->leadout_address = plba; /* PLBA */
    } else if (point == 0xB0) {
        /* 0xB0: identifies a Hybrid disc; contains start time of next possible program area */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: presence of 0xB0 entry indicates hybrid disc\n", __func__);
    } else if (point >= 0xB1 && point <= 0xB4) {
        /* 0xB1-0xB4: skip track assignments pointers */
    } else if (point == 0xC0) {
        /* 0xC0: start time of the first leadin area of Hybrid disc */
    } else if (point == 0xC1) {
        /* 0xC1: copy of infirnatuib from additional area in ATIP */
    } else if (point > 0 && point <= 99) {
        /* Track entry */
        GObject *cur_session = NULL;
        GObject *cur_track = NULL;
        
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding track #%d\n", __func__, point);

        if (!mirage_disc_get_session_by_number(self, session, &cur_session, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get session %i!\n", __func__, session);
            return FALSE;
        }
            
        /* First thing we do is set length for previous track... which we can 
           actually do now (difference of previous PLBA and current one) */
        gint prev_fragment_length = plba - _priv->prev_plba;
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting length of previous track's fragment to 0x%X (%i)\n", __func__, prev_fragment_length, prev_fragment_length);
        __mirage_disc_ccd_set_last_track_length(self, cur_session, prev_fragment_length, NULL);
        _priv->prev_plba = plba;
            
        /* Now add new track */
        if (!mirage_session_add_track_by_number(MIRAGE_SESSION(cur_session), point, &cur_track, error)) {
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
            
        if (point == 1 && plba) {
            GObject *pregap_fragment = NULL;
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: first track appears to contain %i-sector pregap\n", __func__, plba);
                
            mirage_mirage_create_fragment(MIRAGE_MIRAGE(mirage), MIRAGE_TYPE_FINTERFACE_NULL, "NULL", &pregap_fragment, error);
            if (!pregap_fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to create pregap fragment!\n", __func__);
                g_object_unref(mirage);
                g_object_unref(cur_track);
                g_object_unref(cur_session);
                return FALSE;
            }
                
            mirage_track_add_fragment(MIRAGE_TRACK(cur_track), 0, &pregap_fragment, NULL);
            mirage_fragment_set_length(MIRAGE_FRAGMENT(pregap_fragment), plba, NULL);
            g_object_unref(pregap_fragment);
            mirage_track_set_track_start(MIRAGE_TRACK(cur_track), plba, NULL);
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
        g_object_unref(data_fragment);
        
        /* Determine track mode */
        __mirage_disc_ccd_determine_track_mode(self, cur_track, NULL);
            
        g_object_unref(cur_track);
        g_object_unref(cur_session);
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
    MIRAGE_Disc_CCD *self_ccd = MIRAGE_DISC_CCD(self);
    MIRAGE_Disc_CCDPrivate *_priv = MIRAGE_DISC_CCD_GET_PRIVATE(self_ccd);

    void *scanner = NULL;
    FILE *file = NULL;
    gint i;
    
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
    if (yyparse(scanner, MIRAGE_DISC(self), error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse CCD file!\n", __func__);
        fclose(file);
        return FALSE;
    }
    
    /* Destroy scanner */
    yylex_destroy(scanner);        
    fclose(file);
    
    /* Set length of last track in last session */
    gint ltrack_length = _priv->leadout_address - _priv->prev_plba;
    GObject *lsession = NULL;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting length of last track of last session to 0x%X (%i)\n", __func__, ltrack_length, ltrack_length);
    mirage_disc_get_session_by_index(self, -1, &lsession, NULL);
    __mirage_disc_ccd_set_last_track_length(self, lsession, ltrack_length, NULL);
    
    g_object_unref(lsession);
    
    /* Now get length and if it surpasses length of 90min CD, assume we
       have a DVD */
    gint length = 0;
    mirage_disc_layout_get_length(self, &length, NULL);
    if (length > 90*60*75) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: disc length implies DVD-ROM image\n", __func__);
        mirage_disc_set_medium_type(self, MIRAGE_MEDIUM_DVD, NULL);
    } else {
        gint num_sessions = 0;
        
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: disc length implies CD-ROM image\n", __func__);
        mirage_disc_set_medium_type(self, MIRAGE_MEDIUM_CD, NULL);
        
        /* CD-ROMs start at -150 as per Red Book... */
        mirage_disc_layout_set_start_sector(self, -150, NULL);
        
        mirage_disc_get_number_of_sessions(self, &num_sessions, NULL);
        
        /* Give each first track in a session 150-sector pregap... */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: since this is CD-ROM, we're adding 150-sector pregap to first tracks in all sessions\n", __func__);
        for (i = 0; i < num_sessions; i++) {
            GObject *session = NULL;
            GObject *ftrack = NULL;
            
            if (!mirage_disc_get_session_by_index(self, i, &session, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to get session with index %i!\n", __func__, i);
                return FALSE;
            }
            
            if (!mirage_session_get_track_by_index(MIRAGE_SESSION(session), 0, &ftrack, NULL)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to first track of session with index %i!\n", __func__, i);
                g_object_unref(session);
                return FALSE;
            }
            
            /* Add pregap fragment (empty) */
            GObject *mirage = NULL;
            if (!mirage_object_get_mirage(MIRAGE_OBJECT(self), &mirage, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to get Mirage object!\n", __func__);
                g_object_unref(session);
                g_object_unref(ftrack);
                return FALSE;
            }
            GObject *pregap_fragment = NULL;
            mirage_mirage_create_fragment(MIRAGE_MIRAGE(mirage), MIRAGE_TYPE_FINTERFACE_NULL, "NULL", &pregap_fragment, error);
            g_object_unref(mirage);
            if (!pregap_fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to create pregap fragment!\n", __func__);
                g_object_unref(session);
                g_object_unref(ftrack);
                return FALSE;
            }
            mirage_track_add_fragment(MIRAGE_TRACK(ftrack), 0, &pregap_fragment, NULL);
            mirage_fragment_set_length(MIRAGE_FRAGMENT(pregap_fragment), 150, NULL);
            g_object_unref(pregap_fragment);
            
            /* Track starts at 150... well, unless it already has a pregap, in
               which case they should stack */
            gint old_start = 0;
            mirage_track_get_track_start(MIRAGE_TRACK(ftrack), &old_start, NULL);
            old_start += 150;
            mirage_track_set_track_start(MIRAGE_TRACK(ftrack), old_start, NULL);
        
            g_object_unref(ftrack);
            g_object_unref(session);
        }
    }   
    
    return TRUE;
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
