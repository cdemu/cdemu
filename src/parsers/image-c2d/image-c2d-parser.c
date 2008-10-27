/*
 *  libMirage: C2D image parser: Disc object
 *  Copyright (C) 2008 Henrik Stokseth
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
#define MIRAGE_PARSER_C2D_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_C2D, MIRAGE_Parser_C2DPrivate))

typedef struct {
    GObject *disc;
    
    gchar *c2d_filename;
    
    C2D_HeaderBlock *header_block;
    C2D_CDTextBlock *cdtext_block;
    C2D_TrackBlock *track_block;
    
    guint8 *c2d_data;
    gint c2d_data_length;
} MIRAGE_Parser_C2DPrivate;


static gint __mirage_parser_c2d_convert_track_mode (MIRAGE_Parser *self, guint32 mode, guint16 sector_size) {
    if ((mode == C2D_MODE_AUDIO) || (mode == C2D_MODE_AUDIO2)) {
        switch (sector_size) {
            case 2352: {
                return MIRAGE_MODE_AUDIO;
            }
            case 2448: {
                return MIRAGE_MODE_AUDIO;
            }
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown sector size %i!\n", __func__, sector_size);
                return -1;
            }
        }
    } else if (mode == C2D_MODE_MODE1) {
        switch (sector_size) {
            case 2048: {
                return MIRAGE_MODE_MODE1;
            }
            case 2448: {
                return MIRAGE_MODE_MODE2_MIXED; /* HACK to support sector size */
            }
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown sector size %i!\n", __func__, sector_size);
                return -1;
            }
        }
    } else if (mode == C2D_MODE_MODE2) {
        switch (sector_size) {
            case 2048: {
                return MIRAGE_MODE_MODE2_FORM1;
            }
            case 2324: {
                return MIRAGE_MODE_MODE2_FORM2;
            }
            case 2336: {
                return MIRAGE_MODE_MODE2;
            }
            case 2352: {
                return MIRAGE_MODE_MODE2_MIXED;
            }
            case 2448: {
                return MIRAGE_MODE_MODE2_MIXED;
            }
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown sector size %i!\n", __func__, sector_size);
                return -1;
            }
        }
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown track mode 0x%X!\n", __func__, mode);    
        return -1;
    }
}

static gboolean __mirage_parser_c2d_parse_compressed_track (MIRAGE_Parser *self, guint64 offset, GError **error) {
    MIRAGE_Parser_C2DPrivate *_priv = MIRAGE_PARSER_C2D_GET_PRIVATE(self);

    FILE *infile;
    gint num = 0;

    C2D_Z_Info_Header header;
    C2D_Z_Info zinfo;

    infile = fopen(_priv->c2d_filename, "r");
    if (!infile) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open file!\n", __func__);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }
    fseeko(infile, offset, SEEK_SET);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: compression info blocks!\n", __func__);

    if (fread(&header, sizeof(C2D_Z_Info_Header), 1, infile) < 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read Z info header!\n", __func__);
        fclose(infile);
        mirage_error(MIRAGE_E_READFAILED, error);
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: dummy: 0x%X\n", __func__, header.dummy);

    do {
        if (fread(&zinfo, sizeof(C2D_Z_Info), 1, infile) < 1) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read Z info!\n", __func__);
            fclose(infile);
            mirage_error(MIRAGE_E_READFAILED, error);
            return FALSE;
        }
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: [%03X] size: 0x%X offset: 0x%X\n", __func__, num, zinfo.compressed_size, zinfo.image_offset);
        num++;
    } while (zinfo.image_offset);

    fclose(infile);

    return FALSE;
}

static gboolean __mirage_parser_c2d_parse_track_entries (MIRAGE_Parser *self, GError **error) {
    MIRAGE_Parser_C2DPrivate *_priv = MIRAGE_PARSER_C2D_GET_PRIVATE(self);

    gint last_session = 0;
    gint last_point = 0;
    gint last_index = 1;

    gint track = 0;
    gint tracks = _priv->header_block->track_blocks;
    gint track_start = 0;
    gint track_first_sector = 0;
    gint track_last_sector = 0;
    gboolean new_track = FALSE;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Reading track blocks\n", __func__);        

    /* Read track entries */
    for (track = 0; track < tracks; track++) {
        C2D_TrackBlock *cur_tb = &_priv->track_block[track];
        GObject *cur_session = NULL;
        GObject *cur_point = NULL;

        /* Read main blocks related to track */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: TRACK BLOCK %2i:\n", __func__, track);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   block size: %i\n", __func__, cur_tb->block_size);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   first sector: %i\n", __func__, cur_tb->first_sector);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   last sector: %i\n", __func__, cur_tb->last_sector);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   image offset: 0x%X\n", __func__, cur_tb->image_offset);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   sector size: %i\n", __func__, cur_tb->sector_size);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   ISRC: %.12s\n", __func__, cur_tb->isrc);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   flags: 0x%X\n", __func__, cur_tb->flags);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   mode: %i\n", __func__, cur_tb->mode);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   index: %i\n", __func__, cur_tb->index);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   session: %i\n", __func__, cur_tb->session);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   point: %i\n", __func__, cur_tb->point);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   compressed: %i\n", __func__, cur_tb->compressed);

        /* Abort on compressed track data */
        if (cur_tb->compressed) {
            if(!__mirage_parser_c2d_parse_compressed_track(self, cur_tb->image_offset, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse compressed track!\n", __func__);        
                return FALSE;
            }
        }

        /* Create a new session? */
        if (cur_tb->session > last_session) {
            if (!mirage_disc_add_session_by_number(MIRAGE_DISC(_priv->disc), cur_tb->session, NULL, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __func__);        
                return FALSE;
            }
            last_session = cur_tb->session;
            last_point = 0;
        }

        /* Get current session */
        if (!mirage_disc_get_session_by_index(MIRAGE_DISC(_priv->disc), -1, &cur_session, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current session!\n", __func__);
            return FALSE;
        }

        /* Add a new track? */
        new_track = FALSE; /* reset */
        if (cur_tb->point > last_point) {
            if (!mirage_session_add_track_by_number(MIRAGE_SESSION(cur_session), cur_tb->point, NULL, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __func__);
                g_object_unref(cur_session);
                return FALSE;
            }
            last_point = cur_tb->point;
            last_index = 1;
            new_track = TRUE;
        }

        /* Get current track */
        if (!mirage_session_get_track_by_index(MIRAGE_SESSION(cur_session), -1, &cur_point, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current track!\n", __func__);
            g_object_unref(cur_session);
            return FALSE;
        }

        /* Set track start and calculate track's first and last sector */
        if (new_track) {
            gint t;

            if (cur_tb->point == 1) {
                mirage_track_set_track_start(MIRAGE_TRACK(cur_point), 150, NULL);
            }
            mirage_track_get_track_start(MIRAGE_TRACK(cur_point), &track_start, NULL);

            for (t = 0; t < tracks-track; t++) {
                if (cur_tb->point != cur_tb[t].point) break;
            }
            t--;
            track_first_sector = cur_tb->first_sector;
            track_last_sector = cur_tb[t].last_sector;
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   track's first sector: %i, last sector: %i.\n", __func__, track_first_sector, track_last_sector);
        }

        /* Add new index? */
        if (cur_tb->index > last_index) {
            if (!mirage_track_add_index(MIRAGE_TRACK(cur_point), cur_tb->first_sector - track_first_sector + track_start, NULL, NULL)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add index!\n", __func__);
                g_object_unref(cur_point);
                g_object_unref(cur_session);
                return FALSE;
            }
            last_index = cur_tb->index;

            /* If index > 1 then skip making fragments */
            goto skip_making_fragments;
        }

        /* Decode mode */
        gint converted_mode = 0;
        converted_mode = __mirage_parser_c2d_convert_track_mode(self, cur_tb->mode, cur_tb->sector_size);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   converted mode: 0x%X\n", __func__, converted_mode);
        mirage_track_set_mode(MIRAGE_TRACK(cur_point), converted_mode, NULL);

        /* Set ISRC */
        if (cur_tb->isrc[0]) {
            mirage_track_set_isrc(MIRAGE_TRACK(cur_point), cur_tb->isrc, NULL);
        }

        /* Pregap fragment at the beginning of track */
        if ((cur_tb->point == 1) && (cur_tb->index == 1)) {
            GObject *pregap_fragment = libmirage_create_fragment(MIRAGE_TYPE_FINTERFACE_NULL, "NULL", error);
            if (!pregap_fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to create NULL fragment!\n", __func__);
                g_object_unref(cur_point);
                g_object_unref(cur_session);
                return FALSE;
            }

            mirage_fragment_set_length(MIRAGE_FRAGMENT(pregap_fragment), 150, NULL);

            mirage_track_add_fragment(MIRAGE_TRACK(cur_point), -1, &pregap_fragment, error);
            g_object_unref(pregap_fragment);
        }

        /* Data fragment */
        GObject *data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FINTERFACE_BINARY, _priv->c2d_filename, error);
        if (!data_fragment) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to create fragment!\n", __func__);
            g_object_unref(cur_point);
            g_object_unref(cur_session);
            return FALSE;
        }
            
        /* Prepare data fragment */
        FILE *tfile_handle = g_fopen(_priv->c2d_filename, "r");
        guint64 tfile_offset = cur_tb->image_offset;
        gint tfile_sectsize = cur_tb->sector_size;
        gint tfile_format = 0;

        if (converted_mode == MIRAGE_MODE_AUDIO) {
            tfile_format = FR_BIN_TFILE_AUDIO;
        } else {
            tfile_format = FR_BIN_TFILE_DATA;
        }

        gint fragment_len = track_last_sector - track_first_sector + 1;
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   fragment length: %i\n", __func__, fragment_len);

        mirage_fragment_set_length(MIRAGE_FRAGMENT(data_fragment), fragment_len, NULL);

        mirage_finterface_binary_track_file_set_handle(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_handle, NULL);
        mirage_finterface_binary_track_file_set_offset(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_offset, NULL);
        mirage_finterface_binary_track_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_sectsize, NULL);
        mirage_finterface_binary_track_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_format, NULL);

        /* Subchannel */
        switch (cur_tb->sector_size) {
            case 2448: {
                gint sfile_sectsize = 96;
                gint sfile_format = FR_BIN_SFILE_PW96_INT | FR_BIN_SFILE_INT;

                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   subchannel found; interleaved PW96\n", __func__);

                mirage_finterface_binary_subchannel_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), sfile_sectsize, NULL);
                mirage_finterface_binary_subchannel_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), sfile_format, NULL);

                /* We need to correct the data for track sector size...
                   C2D format has already added 96 bytes to sector size,
                   so we need to subtract it */
                tfile_sectsize = cur_tb->sector_size - sfile_sectsize;
                mirage_finterface_binary_track_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_sectsize, NULL);

                break;
            }
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   no subchannel\n", __func__);
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

static gboolean __mirage_parser_c2d_load_cdtext(MIRAGE_Parser *self, GError **error) {
    MIRAGE_Parser_C2DPrivate *_priv = MIRAGE_PARSER_C2D_GET_PRIVATE(self);

    GObject *session = NULL;   
    guint8 *cdtext_data = (guint8 *)_priv->cdtext_block;
    gint cdtext_length = _priv->header_block->size_cdtext;

    /* Read CD-Text data */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD-TEXT:\n", __func__);

    if (!mirage_disc_get_session_by_index(MIRAGE_DISC(_priv->disc), 0, &session, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get session!\n", __func__);
        return FALSE;
    }

    if (!mirage_session_set_cdtext_data(MIRAGE_SESSION(session), cdtext_data, cdtext_length, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set CD-TEXT data!\n", __func__);
        g_object_unref(session);
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   loaded a total of %i blocks.\n", __func__, cdtext_length / sizeof(C2D_CDTextBlock));
    g_assert(cdtext_length % sizeof(C2D_CDTextBlock) == 0);

    g_object_unref(session);

    return TRUE;
}

static gboolean __mirage_parser_c2d_load_disc (MIRAGE_Parser *self, GError **error) {
    MIRAGE_Parser_C2DPrivate *_priv = MIRAGE_PARSER_C2D_GET_PRIVATE(self);

    C2D_HeaderBlock *hb = (C2D_HeaderBlock *) _priv->c2d_data;

    /* Init some block pointers */
    _priv->header_block = (C2D_HeaderBlock *) _priv->c2d_data;
    _priv->track_block = (C2D_TrackBlock *) (_priv->c2d_data + hb->offset_tracks);
    _priv->cdtext_block = (C2D_CDTextBlock *) (_priv->c2d_data + hb->header_size);

    /* Print some info from the header */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: HEADER:\n", __func__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Signature: %.32s\n", __func__, &hb->signature);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Length of header: %i\n", __func__, hb->header_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Has UPC / EAN: %i\n", __func__, hb->has_upc_ean);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Track blocks: %i\n", __func__, hb->track_blocks);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Size of CD-Text block: %i\n", __func__, hb->size_cdtext);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Offset to track blocks: 0x%X\n", __func__, hb->offset_tracks);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Offset to C2CK block: 0x%X\n", __func__, hb->offset_c2ck);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Description: %.80s\n", __func__, &hb->description);
#if 1
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Dummy1: 0x%X\n", __func__, hb->dummy1);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Dummy2: 0x%X\n", __func__, hb->dummy2);
#endif

    /* Set disc's MCN */
    if (hb->has_upc_ean) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   UPC / EAN: %.13s\n", __func__, &hb->upc_ean);
        mirage_disc_set_mcn(MIRAGE_DISC(_priv->disc), hb->upc_ean, NULL);
    } 

    /* For now we only support (and assume) CD media */    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD-ROM image\n", __func__);
    mirage_disc_set_medium_type(MIRAGE_DISC(_priv->disc), MIRAGE_MEDIUM_CD, NULL);

    /* CD-ROMs start at -150 as per Red Book... */
    mirage_disc_layout_set_start_sector(MIRAGE_DISC(_priv->disc), -150, NULL);

    /* Load tracks */
    if (!__mirage_parser_c2d_parse_track_entries(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse track entries!\n", __func__);
        return FALSE;
    }

    /* Load CD-Text */
    if (hb->size_cdtext) {
        if(!__mirage_parser_c2d_load_cdtext(self, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse CD-Text!\n", __func__);
            return FALSE;
        }
    }

    return TRUE;
}

/******************************************************************************\
 *                     MIRAGE_Parser methods implementation                     *
\******************************************************************************/
static gboolean __mirage_parser_c2d_load_image (MIRAGE_Parser *self, gchar **filenames, GObject **disc, GError **error) {
    MIRAGE_Parser_C2DPrivate *_priv = MIRAGE_PARSER_C2D_GET_PRIVATE(self);
    FILE *file;
    gboolean succeeded = TRUE;
    gchar sig[32] = "";

    /* Open file */
    file = g_fopen(filenames[0], "r");
    if (!file) {
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }

    /* Read signature */
    if (fread(sig, 32, 1, file) < 1) {
        mirage_error(MIRAGE_E_READFAILED, error);
        return FALSE;
    }
    
    if (memcmp(sig, C2D_SIGNATURE_1, sizeof(C2D_SIGNATURE_1) - 1)
        && memcmp(sig, C2D_SIGNATURE_2, sizeof(C2D_SIGNATURE_2) - 1)) {
        fclose(file);
        mirage_error(MIRAGE_E_CANTHANDLE, error);
        return TRUE;
    }
    
    fseek(file, 0, SEEK_SET); /* Reset position */
    
    /* Create disc */
    _priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    
    mirage_disc_set_filename(MIRAGE_DISC(_priv->disc), filenames, NULL);
    _priv->c2d_filename = g_strdup(filenames[0]);
    
    /* Load image header */
    _priv->c2d_data = g_malloc(sizeof(C2D_HeaderBlock));
    if (!_priv->c2d_data) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to allocate memory for header (%d)!\n", __func__, sizeof(C2D_HeaderBlock));
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        succeeded = FALSE;
        goto end;
    }

    if (fread(_priv->c2d_data, sizeof(C2D_HeaderBlock), 1, file) < 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read header!\n", __func__);
        mirage_error(MIRAGE_E_READFAILED, error);
        succeeded = FALSE;
        goto end;
    }
    _priv->header_block = (C2D_HeaderBlock *) _priv->c2d_data;

    /* Calculate length of image descriptor data */
    _priv->c2d_data_length = _priv->header_block->offset_tracks + _priv->header_block->track_blocks * sizeof(C2D_TrackBlock);

    /* Load image descriptor data */
    g_free(_priv->c2d_data);
    _priv->c2d_data = g_malloc(_priv->c2d_data_length);
    if (!_priv->c2d_data) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to allocate memory for descriptor (%d)!\n", __func__, _priv->c2d_data_length);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        succeeded = FALSE;
        goto end;
    }

    fseeko(file, 0, SEEK_SET);
    if (fread(_priv->c2d_data, _priv->c2d_data_length, 1, file) < 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read descriptor!\n", __func__);
        mirage_error(MIRAGE_E_READFAILED, error);
        succeeded = FALSE;
        goto end;
    }
    
    /* Load disc */
    succeeded = __mirage_parser_c2d_load_disc(self, error);

end:
    fclose(file);
    g_free(_priv->c2d_data);

    /* Return disc */
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

static void __mirage_parser_c2d_instance_init (GTypeInstance *instance, gpointer g_class) {
    mirage_parser_generate_parser_info(MIRAGE_PARSER(instance),
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

static void __mirage_parser_c2d_finalize (GObject *obj) {
    MIRAGE_Parser_C2D *self_c2d = MIRAGE_PARSER_C2D(obj);
    MIRAGE_Parser_C2DPrivate *_priv = MIRAGE_PARSER_C2D_GET_PRIVATE(self_c2d);
    
    MIRAGE_DEBUG(self_c2d, MIRAGE_DEBUG_GOBJECT, "%s:\n", __func__);

    g_free(_priv->c2d_filename);
    
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self_c2d, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __func__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __mirage_parser_c2d_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_ParserClass *class_parser = MIRAGE_PARSER_CLASS(g_class);
    MIRAGE_Parser_C2DClass *klass = MIRAGE_PARSER_C2D_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Parser_C2DPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_parser_c2d_finalize;
    
    /* Initialize MIRAGE_Parser methods */
    class_parser->load_image = __mirage_parser_c2d_load_image;
        
    return;
}

GType mirage_parser_c2d_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Parser_C2DClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_parser_c2d_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Parser_C2D),
            0,      /* n_preallocs */
            __mirage_parser_c2d_instance_init    /* instance_init */
        };
        
        type = g_type_module_register_type(module, MIRAGE_TYPE_PARSER, "MIRAGE_Parser_C2D", &info, 0);
    }
    
    return type;
}

