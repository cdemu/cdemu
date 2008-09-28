/*
 *  libMirage: C2D image parser: Disc object
 *  Copyright (C) 2006-2008 Henrik Stokseth
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

#include "image-c2d.h"


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_DISC_C2D_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_DISC_C2D, MIRAGE_Disc_C2DPrivate))

typedef struct {   
    C2D_HeaderBlock  *header_block;
    C2D_CDTextBlock  *cdtext_block;
    C2D_TrackBlock   *track_block;
    
    guint8 *c2d_data;
    gint   c2d_data_length;
    
    /* Parser info */
    MIRAGE_ParserInfo *parser_info;
} MIRAGE_Disc_C2DPrivate;


static gint __mirage_disc_c2d_convert_track_mode (MIRAGE_Disc *self, guint32 mode, guint16 sector_size) {
    if((mode == C2D_MODE_AUDIO) || (mode == C2D_MODE_AUDIO2)) {
        switch(sector_size) {
            case 2352:
                return MIRAGE_MODE_AUDIO;
            case 2448:
                return MIRAGE_MODE_AUDIO;
            default:
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown sector size %i!\n", __func__, sector_size);
                return -1;
        }
    } else if(mode == C2D_MODE_MODE1) {
        switch(sector_size) {
            case 2048:
                return MIRAGE_MODE_MODE1;
            case 2448:
                return MIRAGE_MODE_MODE2_MIXED; /* HACK to support sector size */
            default: 
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown sector size %i!\n", __func__, sector_size);
                return -1;
        }
    } else if(mode == C2D_MODE_MODE2) {
        switch(sector_size) {
            case 2048:
                return MIRAGE_MODE_MODE2_FORM1;
            case 2324:
                return MIRAGE_MODE_MODE2_FORM2;
            case 2336:
                return MIRAGE_MODE_MODE2;
            case 2352:
                return MIRAGE_MODE_MODE2_MIXED;
            case 2448:
                return MIRAGE_MODE_MODE2_MIXED;
            default: 
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown sector size %i!\n", __func__, sector_size);
                return -1;
        }
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown track mode 0x%X!\n", __func__, mode);    
        return -1;
    }
}

static gboolean __mirage_disc_c2d_parse_track_entries (MIRAGE_Disc *self, GError **error) {
    MIRAGE_Disc_C2DPrivate *_priv = MIRAGE_DISC_C2D_GET_PRIVATE(self);

    gint     last_session = 0;
    gint     last_point = 0;
    gint     last_index = 0;

    gint     medium_type = 0;
    gint     track = 0;
    gint     track_start = 0;
    gint     track_first_sector = 0;
    gint     track_last_sector = 0;
    gboolean new_track = FALSE;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading track blocks\n", __func__);        

    /* Fetch medium type which we'll need later */
    mirage_disc_get_medium_type(self, &medium_type, NULL);

    /* Read track entries */
    for (track = 0; track < _priv->header_block->track_blocks; track++) {
        GObject *cur_session = NULL;
        GObject *cur_point = NULL;

        /* Read main blocks related to track */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: TRACK BLOCK %2i:\n", __func__, track);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   block size: %i\n", __func__, _priv->track_block[track].block_size);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   first sector: %i\n", __func__, _priv->track_block[track].first_sector);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   last sector: %i\n", __func__, _priv->track_block[track].last_sector);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   image offset: 0x%X\n", __func__, _priv->track_block[track].image_offset);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   sector size: %i\n", __func__, _priv->track_block[track].sector_size);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   ISRC: %.12s\n", __func__, &_priv->track_block[track].isrc);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   flags: 0x%X\n", __func__, _priv->track_block[track].flags);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   mode: %i\n", __func__, _priv->track_block[track].mode);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   index: %i\n", __func__, _priv->track_block[track].index);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   session: %i\n", __func__, _priv->track_block[track].session);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   point: %i\n", __func__, _priv->track_block[track].point);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   compressed: %i\n", __func__, _priv->track_block[track].compressed);

        /* Abort on compressed track data */
        if (_priv->track_block[track].compressed) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Sorry! We don't handle compressed data tracks yet!\n", __func__);
            return FALSE;
        }

        /* Create a new session? */
        if (_priv->track_block[track].session > last_session) {
            if (!mirage_disc_add_session_by_number(self, _priv->track_block[track].session, &cur_session, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __func__);        
                return FALSE;
            }
            last_session = _priv->track_block[track].session;
            last_point = 0;
        }

        /* Get current session */
        if (!mirage_disc_get_session_by_index(self, -1, &cur_session, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current session!\n", __func__);
            return FALSE;
        }

        /* Add a new track? */
        new_track = FALSE; /* reset */
        if (_priv->track_block[track].point > last_point) {
            if (!mirage_session_add_track_by_number(MIRAGE_SESSION(cur_session), _priv->track_block[track].point, &cur_point, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __func__);
                g_object_unref(cur_session);
                return FALSE;
            }
            last_point = _priv->track_block[track].point;
            last_index = 0;
            new_track = TRUE;
        }

        /* Get current track */
        if (!mirage_session_get_track_by_index(MIRAGE_SESSION(cur_session), -1, &cur_point, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current track!\n", __func__);
            return FALSE;
        }

        /* Calculate track start and track's first and last sector */
        if (new_track) {
            gint t;

            mirage_track_get_track_start(MIRAGE_TRACK(cur_point), &track_start, NULL);
            if(_priv->track_block[track].point == 1) {
                track_start += 150;
            }
            /* track_start -= _priv->track_block[track].first_sector; */
            
            for(t=track; t < _priv->header_block->track_blocks; t++) {
                if (_priv->track_block[track].point != _priv->track_block[t].point) {
                    break;
                }
            }
            t--;
            track_first_sector = _priv->track_block[track].first_sector;
            track_last_sector = _priv->track_block[t].last_sector;
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   track's first sector: %i, last sector: %i.\n", __func__, track_first_sector, track_last_sector);
        }

        /* Add new index? */
        if (_priv->track_block[track].index > last_index) {
            if (!mirage_track_add_index(MIRAGE_TRACK(cur_point), _priv->track_block[track].first_sector + track_start, NULL, NULL)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add index!\n", __func__);
                g_object_unref(cur_point);
                g_object_unref(cur_session);
                return FALSE;
            }
            last_index = _priv->track_block[track].index;
        }

        /* If index > 1 then skip making fragments */
        if (_priv->track_block[track].index > 1) goto skip_making_fragments;

        /* Decode mode */
        gint converted_mode = 0;
        converted_mode = __mirage_disc_c2d_convert_track_mode(self, _priv->track_block[track].mode, _priv->track_block[track].sector_size);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   converted mode: 0x%X\n", __func__, converted_mode);
        mirage_track_set_mode(MIRAGE_TRACK(cur_point), converted_mode, NULL);

        /* Set ISRC */
        if ((_priv->track_block[track].index == 1) && _priv->track_block[track].isrc[0]) {
            mirage_track_set_isrc(MIRAGE_TRACK(cur_point), _priv->track_block[track].isrc, NULL);
        }

        /* Get Mirage and have it make us fragments */
        GObject *mirage = NULL;

        if (!mirage_object_get_mirage(MIRAGE_OBJECT(self), &mirage, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to get Mirage object!\n", __func__);
            g_object_unref(cur_point);
            g_object_unref(cur_session);
            return FALSE;
        }

        gchar **filenames = NULL;
        mirage_disc_get_filenames(self, &filenames, NULL);

        /* Pregap fragment at the beginning of track */
        if((_priv->track_block[track].point == 1) && (_priv->track_block[track].index == 1)) {
            GObject *pregap_fragment = NULL;

            mirage_mirage_create_fragment(MIRAGE_MIRAGE(mirage), MIRAGE_TYPE_FINTERFACE_NULL, "NULL", &pregap_fragment, error);
            if (!pregap_fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to create NULL fragment!\n", __func__);
                g_object_unref(mirage);
                g_object_unref(cur_point);
                g_object_unref(cur_session);
                return FALSE;
            }

            mirage_fragment_set_length(MIRAGE_FRAGMENT(pregap_fragment), 150, NULL);

            mirage_track_add_fragment(MIRAGE_TRACK(cur_point), -1, &pregap_fragment, error);
            g_object_unref(pregap_fragment);

            mirage_track_set_track_start(MIRAGE_TRACK(cur_point), 150, NULL);
        }

        /* Data fragment */
        GObject *data_fragment = NULL;
        mirage_mirage_create_fragment(MIRAGE_MIRAGE(mirage), MIRAGE_TYPE_FINTERFACE_BINARY, filenames[0], &data_fragment, error);
        g_object_unref(mirage);
        if (!data_fragment) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to create fragment!\n", __func__);
            g_object_unref(mirage);
            g_object_unref(cur_point);
            g_object_unref(cur_session);
            return FALSE;
        }
            
        /* Prepare data fragment */
        FILE *tfile_handle = g_fopen(filenames[0], "r");
        guint64 tfile_offset = _priv->track_block[track].image_offset;
        gint tfile_sectsize = _priv->track_block[track].sector_size;
        gint tfile_format = 0;

        if (converted_mode == MIRAGE_MODE_AUDIO) {
            tfile_format = FR_BIN_TFILE_AUDIO;
        } else {
            tfile_format = FR_BIN_TFILE_DATA;
        }

        gint fragment_len = track_last_sector - track_first_sector + 1;
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   length: %i\n", __func__, fragment_len);

        mirage_fragment_set_length(MIRAGE_FRAGMENT(data_fragment), fragment_len, NULL);

        mirage_finterface_binary_track_file_set_handle(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_handle, NULL);
        mirage_finterface_binary_track_file_set_offset(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_offset, NULL);
        mirage_finterface_binary_track_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_sectsize, NULL);
        mirage_finterface_binary_track_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_format, NULL);

        /* Subchannel */
        switch (_priv->track_block[track].sector_size) {
            case 2448: {
                gint sfile_sectsize = 96;
                gint sfile_format = FR_BIN_SFILE_PW96_INT | FR_BIN_SFILE_INT;

                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: subchannel found; interleaved PW96\n", __func__);

                mirage_finterface_binary_subchannel_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), sfile_sectsize, NULL);
                mirage_finterface_binary_subchannel_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), sfile_format, NULL);

                /* We need to correct the data for track sector size...
                   C2D format has already added 96 bytes to sector size,
                   so we need to subtract it */
                tfile_sectsize = _priv->track_block[track].sector_size - sfile_sectsize;
                mirage_finterface_binary_track_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_sectsize, NULL);

                break;
            }
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: no subchannel\n", __func__);
                break;
            }
        }

        mirage_track_add_fragment(MIRAGE_TRACK(cur_point), -1, &data_fragment, error);
        g_object_unref(data_fragment);

    skip_making_fragments:

        g_object_unref(cur_point);
        g_object_unref(cur_session);
    }
   
    return TRUE;
}

static gboolean __mirage_disc_c2d_load_cdtext(MIRAGE_Disc *self, GError **error) {
    MIRAGE_Disc_C2DPrivate *_priv = MIRAGE_DISC_C2D_GET_PRIVATE(self);
    guint8                 *cdtext_data = NULL;
    guint                  cdtext_length = 0;
    GObject                *session = NULL;   
    gboolean               succeeded = TRUE;

    /* Read CD-Text data */
    cdtext_data = (guint8 *) _priv->cdtext_block + sizeof(guint32);
    cdtext_length = _priv->header_block->size_cdtext - sizeof(guint32);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD-TEXT:\n", __func__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Loading %i bytes from offset 0x%X.\n", __func__, cdtext_length, _priv->header_block->header_size);

    if (mirage_disc_get_session_by_index(self, 0, &session, error)) {
        if (!mirage_session_set_cdtext_data(MIRAGE_SESSION(session), cdtext_data, cdtext_length, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set CD-TEXT data!\n", __func__);
            succeeded = FALSE;
        }
        g_object_unref(session);
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get session!\n", __func__);
        succeeded = FALSE;
    }

    return succeeded;
}

static gboolean __mirage_disc_c2d_load_disc (MIRAGE_Disc *self, GError **error) {
    MIRAGE_Disc_C2DPrivate *_priv = MIRAGE_DISC_C2D_GET_PRIVATE(self);

    /* Init some block pointers */
    _priv->header_block = (C2D_HeaderBlock *) _priv->c2d_data;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: HEADER:\n", __func__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Signature: %.32s\n", __func__, &_priv->header_block->signature);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Length of header: %i\n", __func__, _priv->header_block->header_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Has UPC / EAN: %i\n", __func__, _priv->header_block->has_upc_ean);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Track blocks: %i\n", __func__, _priv->header_block->track_blocks);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Size of CD-Text block: %i\n", __func__, _priv->header_block->size_cdtext);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Offset to track blocks: 0x%X\n", __func__, _priv->header_block->offset_tracks);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Offset to C2CK block: 0x%X\n", __func__, _priv->header_block->offset_c2ck);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Description: %.80s\n", __func__, &_priv->header_block->description);

    /* Set disc's MCN */
    if(_priv->header_block->has_upc_ean) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   UPC / EAN: %.13s\n", __func__, &_priv->header_block->upc_ean);
        mirage_disc_set_mcn(self, _priv->header_block->upc_ean, NULL);
    } 

    /* For now we only support (and assume) CD media */    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD-ROM image\n", __func__);
    mirage_disc_set_medium_type(self, MIRAGE_MEDIUM_CD, NULL);

    /* CD-ROMs start at -150 as per Red Book... */
    mirage_disc_layout_set_start_sector(self, -150, NULL);

    /* Load tracks */
    _priv->track_block = (C2D_TrackBlock *) (_priv->c2d_data + _priv->header_block->offset_tracks);
    if (!__mirage_disc_c2d_parse_track_entries(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse track entries!\n", __func__);
        return FALSE;
    }

    /* Load CD-Text */
    if(_priv->header_block->size_cdtext) {
        _priv->cdtext_block = (C2D_CDTextBlock *) (_priv->c2d_data + _priv->header_block->header_size);
        if(!__mirage_disc_c2d_load_cdtext(self, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse CD-Text!\n", __func__);
            return FALSE;
        }
    }

    return TRUE;
}

/******************************************************************************\
 *                     MIRAGE_Disc methods implementation                     *
\******************************************************************************/
static gboolean __mirage_disc_c2d_get_parser_info (MIRAGE_Disc *self, MIRAGE_ParserInfo **parser_info, GError **error) {
    MIRAGE_Disc_C2DPrivate *_priv = MIRAGE_DISC_C2D_GET_PRIVATE(self);
    *parser_info = _priv->parser_info;
    return TRUE;
}

static gboolean __mirage_disc_c2d_can_load_file (MIRAGE_Disc *self, gchar *filename, GError **error) {
    MIRAGE_Disc_C2DPrivate *_priv = MIRAGE_DISC_C2D_GET_PRIVATE(self);

    /* Does file exist? */
    if (!g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
        return FALSE;
    }
    
    /* FIXME: Should support anything that passes the signature test and 
       ignore suffixes? */
    if (!mirage_helper_match_suffixes(filename, _priv->parser_info->suffixes)) {
        return FALSE;
    }

    /* Also check that there's appropriate signature */
    FILE *file = g_fopen(filename, "r");
    if (!file) {
        return FALSE;
    }

    gchar sig[32] = {0};
    if(!fread(sig, 32, 1, file)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read image file!\n", __func__);
        return FALSE;
    }
    fclose(file);

    if (!memcmp(sig, C2D_SIGNATURE_1, sizeof(C2D_SIGNATURE_1) - 1)) {
        return TRUE;
    }
    if (!memcmp(sig, C2D_SIGNATURE_2, sizeof(C2D_SIGNATURE_2) - 1)) {
        return TRUE;
    }

    return FALSE;
}

static gboolean __mirage_disc_c2d_load_image (MIRAGE_Disc *self, gchar **filenames, GError **error) {
    MIRAGE_Disc_C2DPrivate *_priv = MIRAGE_DISC_C2D_GET_PRIVATE(self);

    FILE     *c2d_file = NULL;
    gint     bytes_read = 0;
    gboolean succeeded = TRUE;
    
    /* For now, C2D parser supports only one-file images */
    if (g_strv_length(filenames) > 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: only single-file images supported!\n", __func__);
        mirage_error(MIRAGE_E_SINGLEFILE, error);
        return FALSE;
    }
    
    mirage_disc_set_filenames(self, filenames, NULL);
    
    /* Open image file */
    c2d_file = g_fopen(filenames[0], "r");
    if(!c2d_file) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open image file!\n", __func__);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }

    /* Load image header */
    _priv->c2d_data = g_malloc(sizeof(C2D_HeaderBlock));
    if(!_priv->c2d_data) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to allocate memory!\n", __func__);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }

    bytes_read = fread(_priv->c2d_data, sizeof(C2D_HeaderBlock), 1, c2d_file);
    _priv->header_block = (C2D_HeaderBlock *) _priv->c2d_data;

    /* Calculate length of image descriptor data */
    _priv->c2d_data_length = _priv->header_block->offset_tracks + _priv->header_block->track_blocks * sizeof(C2D_TrackBlock);

    g_free(_priv->c2d_data);

    /* Load image descriptor data */
    _priv->c2d_data = g_malloc(_priv->c2d_data_length);
    if(!_priv->c2d_data) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to allocate memory!\n", __func__);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }

    fseeko(c2d_file, 0, SEEK_SET);
    bytes_read = fread(_priv->c2d_data, _priv->c2d_data_length, 1, c2d_file);
    fclose(c2d_file);
    
    /* Load disc */
    succeeded = __mirage_disc_c2d_load_disc(self, error);
   
    g_free(_priv->c2d_data);

    return succeeded;    
}


/******************************************************************************\
 *                                Object init                                 *
\******************************************************************************/
/* Our parent class */
static MIRAGE_DiscClass *parent_class = NULL;

static void __mirage_disc_c2d_instance_init (GTypeInstance *instance, gpointer g_class) {
    MIRAGE_Disc_C2D *self = MIRAGE_DISC_C2D(instance);
    MIRAGE_Disc_C2DPrivate *_priv = MIRAGE_DISC_C2D_GET_PRIVATE(self);
    
    /* Create parser info */
    _priv->parser_info = mirage_helper_create_parser_info(
        "PARSER-C2D",
        "C2D Image Parser",
        "1.0.0",
        "Henrik Stokseth",
        FALSE,
        "C2D (CeQuadrat WinOnCD) images",
        2, ".c2d", NULL
    );
    
    return;
}

static void __mirage_disc_c2d_finalize (GObject *obj) {
    MIRAGE_Disc_C2D *self_c2d = MIRAGE_DISC_C2D(obj);
    MIRAGE_Disc_C2DPrivate *_priv = MIRAGE_DISC_C2D_GET_PRIVATE(self_c2d);
    
    MIRAGE_DEBUG(self_c2d, MIRAGE_DEBUG_GOBJECT, "%s:\n", __func__);
  
    /* Free parser info */
    mirage_helper_destroy_parser_info(_priv->parser_info);

    /* Chain up to the parent class */
    MIRAGE_DEBUG(self_c2d, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __func__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __mirage_disc_c2d_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_DiscClass *class_disc = MIRAGE_DISC_CLASS(g_class);
    MIRAGE_Disc_C2DClass *klass = MIRAGE_DISC_C2D_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Disc_C2DPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_disc_c2d_finalize;
    
    /* Initialize MIRAGE_Disc methods */
    class_disc->get_parser_info = __mirage_disc_c2d_get_parser_info;
    class_disc->can_load_file = __mirage_disc_c2d_can_load_file;
    class_disc->load_image = __mirage_disc_c2d_load_image;
        
    return;
}

GType mirage_disc_c2d_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Disc_C2DClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_disc_c2d_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Disc_C2D),
            0,      /* n_preallocs */
            __mirage_disc_c2d_instance_init    /* instance_init */
        };
        
        type = g_type_module_register_type(module, MIRAGE_TYPE_DISC, "MIRAGE_Disc_C2D", &info, 0);
    }
    
    return type;
}

