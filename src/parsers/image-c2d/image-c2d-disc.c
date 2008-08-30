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
    C2D_TrackBlock   *track_block;
    
    gchar *c2d_filename;
    
    GMappedFile *c2d_mapped;
    guint8 *c2d_data;
    
    /* Parser info */
    MIRAGE_ParserInfo *parser_info;
} MIRAGE_Disc_C2DPrivate;


static gint __mirage_disc_c2d_convert_track_mode (MIRAGE_Disc *self, guint32 mode, guint16 sector_size) {
    if(mode == C2D_MODE_AUDIO) {
        switch(sector_size) {
            case 2352:
                return MIRAGE_MODE_AUDIO;
            case 2448:
                return MIRAGE_MODE_AUDIO;
            default:
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown sector size %i!\n", __func__, sector_size);
                return -1;
        }
    } else if(mode & C2D_MODE_DATA) {
        switch(sector_size) {
            /* FIXME: This needs some work */
            case 2048:
                return MIRAGE_MODE_MODE1;
            case 2448:
                return MIRAGE_MODE_MODE1;
            case 2332:
                return MIRAGE_MODE_MODE2_MIXED;
            case 2336:
                return MIRAGE_MODE_MODE2_MIXED;
            case 2328:
                return MIRAGE_MODE_MODE2_FORM2;
            case 2352:
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

    gint    last_session = 0;
    gint    medium_type = 0;
    gint    track;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading track blocks\n", __func__);        
    
    /* Fetch medium type which we'll need later */
    mirage_disc_get_medium_type(self, &medium_type, NULL);

    /* Read track entries */
    for (track = 0; track < _priv->header_block->track_blocks; track++) {
        GObject *cur_session = NULL;
        GObject *cur_track = NULL;

        /* Read main blocks related to track */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: TRACK %2i:\n", __func__, track);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   block size: %i\n", __func__, _priv->track_block[track].block_size);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   first sector: %i\n", __func__, _priv->track_block[track].first_sector);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   last sector: %i\n", __func__, _priv->track_block[track].last_sector);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   image offset: %i\n", __func__, _priv->track_block[track].image_offset);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   sector size: %i\n", __func__, _priv->track_block[track].sector_size);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   mode: %i\n", __func__, _priv->track_block[track].mode);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   session: %i\n", __func__, _priv->track_block[track].session);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   point: %i\n", __func__, _priv->track_block[track].point);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   index?: %i\n", __func__, _priv->track_block[track].index);

        /* Create a new session? */
        if (_priv->track_block[track].session > last_session) {
            if (!mirage_disc_add_session_by_number(self, _priv->track_block[track].session, &cur_session, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __func__);        
                return FALSE;
            }
            last_session = _priv->track_block[track].session;
        }

        /* Get current session */
        if (!mirage_disc_get_session_by_index(self, -1, &cur_session, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current session!\n", __func__);
            return FALSE;
        }

        /* Add track to session */
        if (!mirage_session_add_track_by_number(MIRAGE_SESSION(cur_session), _priv->track_block[track].point, &cur_track, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __func__);
            g_object_unref(cur_session);
            return FALSE;
        }
        gint converted_mode = __mirage_disc_c2d_convert_track_mode(self, _priv->track_block[track].mode, _priv->track_block[track].sector_size);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   converted mode: 0x%X\n", __func__, converted_mode);
        mirage_track_set_mode(MIRAGE_TRACK(cur_track), converted_mode, NULL);

        /* Get Mirage and have it make us fragments */
        GObject *mirage = NULL;

        if (!mirage_object_get_mirage(MIRAGE_OBJECT(self), &mirage, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to get Mirage object!\n", __func__);
            g_object_unref(cur_track);
            g_object_unref(cur_session);
            return FALSE;
        }

        /* Pregap fragment */
        if(track == 0) {
            GObject *pregap_fragment = NULL;

            mirage_mirage_create_fragment(MIRAGE_MIRAGE(mirage), MIRAGE_TYPE_FINTERFACE_NULL, "NULL", &pregap_fragment, error);
            if (!pregap_fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to create NULL fragment!\n", __func__);
                g_object_unref(mirage);
                g_object_unref(cur_track);
                g_object_unref(cur_session);
                return FALSE;
            }

            mirage_fragment_set_length(MIRAGE_FRAGMENT(pregap_fragment), 150, NULL);

            mirage_track_add_fragment(MIRAGE_TRACK(cur_track), -1, &pregap_fragment, error);
            g_object_unref(pregap_fragment);

            mirage_track_set_track_start(MIRAGE_TRACK(cur_track), 150, NULL);
        }

        /* Data fragment */
        GObject *data_fragment = NULL;
        mirage_mirage_create_fragment(MIRAGE_MIRAGE(mirage), MIRAGE_TYPE_FINTERFACE_BINARY, _priv->c2d_filename, &data_fragment, error);
        g_object_unref(mirage);
        if (!data_fragment) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to create fragment!\n", __func__);
            g_object_unref(mirage);
            g_object_unref(cur_track);
            g_object_unref(cur_session);
            return FALSE;
        }
            
        /* Prepare data fragment */
        FILE *tfile_handle = g_fopen(_priv->c2d_filename, "r");
        guint64 tfile_offset = _priv->track_block[track].image_offset;
        gint tfile_sectsize = _priv->track_block[track].sector_size;
        gint tfile_format = 0;

        if (converted_mode == MIRAGE_MODE_AUDIO) {
            tfile_format = FR_BIN_TFILE_AUDIO;
        } else {
            tfile_format = FR_BIN_TFILE_DATA;
        }

        gint fragment_len = _priv->track_block[track].last_sector - _priv->track_block[track].first_sector + 1;
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   length: %i\n", __func__, fragment_len);

        mirage_fragment_set_length(MIRAGE_FRAGMENT(data_fragment), fragment_len, NULL);

        mirage_finterface_binary_track_file_set_handle(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_handle, NULL);
        mirage_finterface_binary_track_file_set_offset(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_offset, NULL);
        mirage_finterface_binary_track_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_sectsize, NULL);
        mirage_finterface_binary_track_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_format, NULL);

#ifdef WAIT_FOR_CONFIRMATION
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
                tfile_sectsize = real_sector_size - sfile_sectsize;
                mirage_finterface_binary_track_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_sectsize, NULL);

                break;
            }
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: no subchannel\n", __func__);
                break;
            }
        }
#endif /* WAIT_FOR_CONFIRMATION */

        mirage_track_add_fragment(MIRAGE_TRACK(cur_track), -1, &data_fragment, error);

        g_object_unref(data_fragment);
        g_object_unref(cur_track);
        g_object_unref(cur_session);
    }
   
    return TRUE;
}


static gboolean __mirage_disc_c2d_load_disc (MIRAGE_Disc *self, GError **error) {
    MIRAGE_Disc_C2DPrivate *_priv = MIRAGE_DISC_C2D_GET_PRIVATE(self);

    /* Init some block pointers */
    _priv->header_block = (C2D_HeaderBlock *) (_priv->c2d_data);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: HEADER:\n", __func__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Signature: %.32s\n", __func__, &_priv->header_block->signature);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Track blocks: %i\n", __func__, _priv->header_block->track_blocks);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Offset to track blocks: %i\n", __func__, _priv->header_block->offset_tracks);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Length of header: %i\n", __func__, _priv->header_block->header_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Has description: %i\n", __func__, _priv->header_block->has_description);
    if(_priv->header_block->has_description) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Description: %s\n", __func__, &_priv->header_block->description);
    }

    _priv->track_block = (C2D_TrackBlock *) (_priv->c2d_data + _priv->header_block->offset_tracks);

    /* For now we only support (and assume) CD media */    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD-ROM image\n", __func__);
    mirage_disc_set_medium_type(self, MIRAGE_MEDIUM_CD, NULL);

    /* CD-ROMs start at -150 as per Red Book... */
    mirage_disc_layout_set_start_sector(self, -150, NULL);

    /* Load tracks */
    if (!__mirage_disc_c2d_parse_track_entries(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse track entries!\n", __func__);
        return FALSE;
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

    if (!memcmp(sig, C2D_SIGNATURE_1, strnlen(C2D_SIGNATURE_1, 32))) {
        return TRUE;
    }
    if (!memcmp(sig, C2D_SIGNATURE_2, strnlen(C2D_SIGNATURE_2, 32))) {
        return TRUE;
    }
    
    return FALSE;
}

static gboolean __mirage_disc_c2d_load_image (MIRAGE_Disc *self, gchar **filenames, GError **error) {
    MIRAGE_Disc_C2DPrivate *_priv = MIRAGE_DISC_C2D_GET_PRIVATE(self);
    GError *local_error = NULL;
    gboolean succeeded = TRUE;
    
    /* For now, C2D parser supports only one-file images */
    if (g_strv_length(filenames) > 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: only single-file images supported!\n", __func__);
        mirage_error(MIRAGE_E_SINGLEFILE, error);
        return FALSE;
    }
    
    mirage_disc_set_filenames(self, filenames, NULL);
    _priv->c2d_filename = g_strdup(filenames[0]);
    
    /* Map the file using GLib's GMappedFile */
    _priv->c2d_mapped = g_mapped_file_new(_priv->c2d_filename, FALSE, &local_error);
    if (!_priv->c2d_mapped) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to map file '%s': %s!\n", __func__, _priv->c2d_filename, local_error->message);
        g_error_free(local_error);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }
    
    _priv->c2d_data = (guint8 *)g_mapped_file_get_contents(_priv->c2d_mapped);
    
    /* Load disc */
    succeeded = __mirage_disc_c2d_load_disc(self, error);

    _priv->c2d_data = NULL;
    g_mapped_file_free(_priv->c2d_mapped);
        
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
        "C2D (WinOnCD) images",
        2, ".c2d", NULL
    );
    
    return;
}

static void __mirage_disc_c2d_finalize (GObject *obj) {
    MIRAGE_Disc_C2D *self_c2d = MIRAGE_DISC_C2D(obj);
    MIRAGE_Disc_C2DPrivate *_priv = MIRAGE_DISC_C2D_GET_PRIVATE(self_c2d);
    
    MIRAGE_DEBUG(self_c2d, MIRAGE_DEBUG_GOBJECT, "%s:\n", __func__);

    g_free(_priv->c2d_filename);
    
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

