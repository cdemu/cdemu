/*
 *  libMirage: CIF image parser: Disc object
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

#include "image-cif.h"


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_DISC_CIF_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_DISC_CIF, MIRAGE_Disc_CIFPrivate))

typedef struct {   
    GList *block_index;
    gint  block_index_entries;
    
    gchar *cif_filename;
    
    GMappedFile *cif_mapped;
    guint8 *cif_data;
    
    /* Parser info */
    MIRAGE_ParserInfo *parser_info;
} MIRAGE_Disc_CIFPrivate;

static gboolean __mirage_disc_cif_build_block_index(MIRAGE_Disc *self, GError **error) {
    MIRAGE_Disc_CIFPrivate *_priv = MIRAGE_DISC_CIF_GET_PRIVATE(self);
    GList                  *blockindex = NULL;
    CIFBlockIndexEntry     *blockentry = NULL;
    gint                   num_blocks = 0, index = 0, success = TRUE;
    guint8                 *cur_ptr = NULL;
    CIF_BlockHeader        *block = NULL;

    /* Populate block index */
    cur_ptr = _priv->cif_data;
    index = 0;
    do {
        blockentry = g_new(CIFBlockIndexEntry, 1);
        if (!blockentry) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to allocate memory for block index!\n", __func__);
            return FALSE;
        }
        block = MIRAGE_CAST_PTR(cur_ptr, 0, CIF_BlockHeader *);
        if(memcmp(block->signature, "RIFF", 4)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: expected 'RIFF' block signature!\n", __func__);
            return FALSE;
        }
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Block: %2i, ID: %4s, start: %p, length: %i (0x%X).\n", \
            __func__, index, block->block_id, block, block->length, block->length);
        blockentry->block_header = block;

        if(!memcmp(block->block_id, "info", 4)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: This parser does not yet support binary tracks.\n", __func__);
            //success = FALSE;
        }

        /* Got sub-blocks? */
        if(!memcmp(block->block_id, "disc", 4)) {
            guint16 *skip_ahead = (guint16 *) (cur_ptr + CIF_BLOCK_HEADER_SIZE);
            guint16 *subblocks = (guint16 *) (cur_ptr + CIF_BLOCK_HEADER_SIZE + *skip_ahead + 2);
            guint16 *blocksize = (guint16 *) (cur_ptr + CIF_BLOCK_HEADER_SIZE + *skip_ahead + 18);

            blockentry->subblocks_start = (guint8 *) cur_ptr + CIF_BLOCK_HEADER_SIZE + *skip_ahead + 18;
            blockentry->num_subblocks = *subblocks;
            blockentry->subblocks_length = *blocksize;
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Sub-blocks: %i, blocksize: %i.\n", __func__, *subblocks, *blocksize);
        } else if(!memcmp(block->block_id, "ofs ", 4)) {
            guint16         *subblocks = (guint16 *) (cur_ptr + CIF_BLOCK_HEADER_SIZE);
            guint16         blocksize = CIF_BLOCK_HEADER_SIZE + 2;

            blockentry->subblocks_start = (guint8 *) cur_ptr + CIF_BLOCK_HEADER_SIZE + 2;
            blockentry->num_subblocks = *subblocks;
            blockentry->subblocks_length = blocksize;
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Sub-blocks: %i, blocksize: %i.\n", __func__, *subblocks, blocksize);
        } else {
            blockentry->subblocks_start = NULL; /* This block has no sub-blocks */
            blockentry->num_subblocks = 0;
            blockentry->subblocks_length = 0;
        }

        /* Add entry to list */
        blockindex = g_list_prepend(blockindex, blockentry);
        if (!blockindex) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to allocate memory for block index!\n", __func__);
            return FALSE;
        }

        /* Get ready for next block */
        index++;
        cur_ptr += (block->length + CIF_BLOCK_LENGTH_ADJUST);
        if(block->length % 2) cur_ptr++; /* Padding byte if length is not even */
    } while((cur_ptr - _priv->cif_data) < g_mapped_file_get_length (_priv->cif_mapped));
    blockindex = g_list_reverse(blockindex);
    num_blocks = g_list_length(blockindex);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: counted %i 'RIFF' blocks.\n", __func__, num_blocks);

    /* Link to block index */
    _priv->block_index = blockindex;
    _priv->block_index_entries = num_blocks;

    return success;
}

static void __mirage_disc_cif_destroy_block_index_helper(gpointer data, gpointer user_data) {
    if(data) {
        g_free(data);
    }
}

static gboolean __mirage_disc_cif_destroy_block_index(MIRAGE_Disc *self, GError **error) {
    MIRAGE_Disc_CIFPrivate *_priv = MIRAGE_DISC_CIF_GET_PRIVATE(self);

    if (_priv->block_index) {
        g_list_foreach(_priv->block_index, __mirage_disc_cif_destroy_block_index_helper, NULL);
        g_list_free(_priv->block_index);
        _priv->block_index = NULL;
        _priv->block_index_entries = 0;
    }
    else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to free memory for block index!\n", __func__);
        return FALSE;
    }

    return TRUE;
}

static gint __mirage_disc_cif_find_block_entry_helper(gconstpointer a, gconstpointer b) {
    CIFBlockIndexEntry     *blockentry = (CIFBlockIndexEntry *) a;
    gchar                  *block_id = (gchar *) b;

    return memcmp(blockentry->block_header->block_id, block_id, 4);
}

static CIFBlockIndexEntry *__mirage_disc_cif_find_block_entry(MIRAGE_Disc *self, gchar *block_id, GError **error) {
    MIRAGE_Disc_CIFPrivate *_priv = MIRAGE_DISC_CIF_GET_PRIVATE(self);
    GList *listentry = NULL;

    listentry = g_list_find_custom(_priv->block_index, block_id, __mirage_disc_cif_find_block_entry_helper);

    if(listentry) {
        return (CIFBlockIndexEntry *) listentry->data;
    } else {
        return (CIFBlockIndexEntry *) NULL;
    }
}

static gint __mirage_disc_cif_convert_track_mode (MIRAGE_Disc *self, guint32 mode, guint16 sector_size) {
    if(mode == CIF_TRACK_AUDIO) {
        switch(sector_size) {
            case 2352:
                return MIRAGE_MODE_AUDIO;
            case 2448:
                return MIRAGE_MODE_AUDIO;
            default:
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown sector size %i!\n", __func__, sector_size);
                return -1;
        }
    } else if(mode == CIF_TRACK_BINARY) {
        switch(sector_size) {
            /* FIXME: This needs some work */
            case 2048:
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

static gboolean __mirage_disc_cif_parse_track_entries (MIRAGE_Disc *self, GError **error) {
    MIRAGE_Disc_CIFPrivate *_priv = MIRAGE_DISC_CIF_GET_PRIVATE(self);
    CIFBlockIndexEntry *disc_block_ptr = NULL;
    CIFBlockIndexEntry *ofs_block_ptr = NULL;
    GObject            *cur_session = NULL;
    gint               medium_type = 0;
    gint               track, tracks;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading track blocks\n", __func__);        
    
    /* Fetch medium type which we'll need later */
    mirage_disc_get_medium_type(self, &medium_type, NULL);

    /* Get current session */
    if (!mirage_disc_get_session_by_index(self, -1, &cur_session, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current session!\n", __func__);
        return FALSE;
    }

    /* Initialize disc block and ofs block pointers first */
    disc_block_ptr = __mirage_disc_cif_find_block_entry(self, "disc", error);
    ofs_block_ptr = __mirage_disc_cif_find_block_entry(self, "ofs ", error);
    if (!(disc_block_ptr && ofs_block_ptr)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: \"disc\" or \"ofs\" blocks not located. Can not proceed.\n", __func__);
        return FALSE;
    }
 
    tracks = disc_block_ptr->num_subblocks;

    /* Read track entries */
    for (track = 0; track < tracks; track++) {
        CIF_BlockHeader    *ofs_subblock = (CIF_BlockHeader *) (ofs_block_ptr->subblocks_start + track * ofs_block_ptr->subblocks_length);
        guint8             *disc_subblock = (guint8 *) (disc_block_ptr->subblocks_start + track * disc_block_ptr->subblocks_length);
        guint8             *track_block = (guint8 *) (_priv->cif_data + ofs_subblock->ofs_offset + OFS_OFFSET_ADJUST);

        guint8             *track_mode = ofs_subblock->block_id;
        guint32            *hex_track_mode = (guint32 *) track_mode;
        guint8             *track_start = track_block + CIF_BLOCK_HEADER_SIZE;
        guint16            *sector_size = (guint16 *) (disc_subblock + 22);
        guint16            real_sector_size = *sector_size;
        //guint16            *pregap = (guint16 *) (disc_subblock + 24); /* only valid for binary tracks? */
        //guint16            *postgap = (guint16 *) (disc_subblock + 26); /* only valid for binary tracks? */
        guint32            *sectors = (guint32 *) (disc_subblock + 4); /* not correct for binary tracks? */

        GObject            *cur_track = NULL;

        /* CIF binary tracks seems to use 2332 bytes sectors in the image file */
        if(*hex_track_mode == CIF_TRACK_BINARY) {
            real_sector_size = 2332;
            track_start -= 16; /* shameless hack */
        }
        /* Read main blocks related to track */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track #%i:\n", __func__, track);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   mode: %c%c%c%c\n", __func__, track_mode[0], track_mode[1], track_mode[2], track_mode[3]);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   sector size: %i\n", __func__, real_sector_size);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   sectors: %i\n", __func__, *sectors);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   start: %p\n", __func__, track_start);
        //MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   pregap: %i\n", __func__, *pregap);
        //MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   postgap: %i\n", __func__, *postgap);

        if (!mirage_session_add_track_by_number(MIRAGE_SESSION(cur_session), track+1, &cur_track, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __func__);
            g_object_unref(cur_session);
            return FALSE;
        }
        gint converted_mode = __mirage_disc_cif_convert_track_mode(self, *hex_track_mode, *sector_size);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   track mode: 0x%X\n", __func__, converted_mode);
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
        mirage_mirage_create_fragment(MIRAGE_MIRAGE(mirage), MIRAGE_TYPE_FINTERFACE_BINARY, _priv->cif_filename, &data_fragment, error);
        g_object_unref(mirage);
        if (!data_fragment) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to create fragment!\n", __func__);
            g_object_unref(mirage);
            g_object_unref(cur_track);
            g_object_unref(cur_session);
            return FALSE;
        }
            
        /* Prepare data fragment */
        FILE *tfile_handle = g_fopen(_priv->cif_filename, "r");
        guint64 tfile_offset = (guint64) (track_start - _priv->cif_data);
        gint tfile_sectsize = real_sector_size;
        gint tfile_format = 0;

        if (converted_mode == MIRAGE_MODE_AUDIO) {
            tfile_format = FR_BIN_TFILE_AUDIO;
        } else {
            tfile_format = FR_BIN_TFILE_DATA;
        }

        gint fragment_len = 0;
	if (converted_mode == MIRAGE_MODE_AUDIO) {
            fragment_len = *sectors;
        } else {
            fragment_len = *sectors / 2; /* shameless hack #2 */
        }
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   length: %i\n", __func__, fragment_len);

        mirage_fragment_set_length(MIRAGE_FRAGMENT(data_fragment), fragment_len, NULL);

        mirage_finterface_binary_track_file_set_handle(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_handle, NULL);
        mirage_finterface_binary_track_file_set_offset(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_offset, NULL);
        mirage_finterface_binary_track_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_sectsize, NULL);
        mirage_finterface_binary_track_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_format, NULL);

#ifdef JALLA
        /* Subchannel */
        switch (real_sector_size) {
            case 2352: {
                gint sfile_sectsize = 96;
                gint sfile_format = FR_BIN_SFILE_PW96_INT | FR_BIN_SFILE_INT;

                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: subchannel found; interleaved PW96\n", __func__);

                mirage_finterface_binary_subchannel_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), sfile_sectsize, NULL);
                mirage_finterface_binary_subchannel_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), sfile_format, NULL);

                /* We need to correct the data for track sector size...
                   CIF format has already added 96 bytes to sector size,
                   so we need to subtract it */
                tfile_sectsize = real_sector_size - sfile_sectsize;
                mirage_finterface_binary_track_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_sectsize, NULL);

                break;
            }
            case 2048: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: no subchannel\n", __func__);
                break;
            }
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown subchannel type 0x%X!\n", __func__, real_sector_size);
                break;
            }
        }
#endif /* JALLA */

        mirage_track_add_fragment(MIRAGE_TRACK(cur_track), -1, &data_fragment, error);

        g_object_unref(data_fragment);
        g_object_unref(cur_track);
    }
    g_object_unref(cur_session);
   
    return TRUE;
}

static gboolean __mirage_disc_cif_load_disc (MIRAGE_Disc *self, GError **error) {
    /*MIRAGE_Disc_CIFPrivate *_priv = MIRAGE_DISC_CIF_GET_PRIVATE(self);*/

    /* Build an index over blocks contained in the disc image */
    if (!__mirage_disc_cif_build_block_index(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to build block index!\n", __func__);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }

    /* For now we only support (and assume) CD media */    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD-ROM image\n", __func__);
    mirage_disc_set_medium_type(self, MIRAGE_MEDIUM_CD, NULL);

    /* CD-ROMs start at -150 as per Red Book... */
    mirage_disc_layout_set_start_sector(self, -150, NULL);

    /* Add session */
    if (!mirage_disc_add_session_by_number(self, 0, NULL, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __func__);        
        return FALSE;
    }

    /* Load tracks */
    if (!__mirage_disc_cif_parse_track_entries(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse track entries!\n", __func__);
        return FALSE;
    }

    /* Destroy block index */
    if(!__mirage_disc_cif_destroy_block_index(self, error)) {
        return FALSE;
    }

    return TRUE;
}

/******************************************************************************\
 *                     MIRAGE_Disc methods implementation                     *
\******************************************************************************/
static gboolean __mirage_disc_cif_get_parser_info (MIRAGE_Disc *self, MIRAGE_ParserInfo **parser_info, GError **error) {
    MIRAGE_Disc_CIFPrivate *_priv = MIRAGE_DISC_CIF_GET_PRIVATE(self);
    *parser_info = _priv->parser_info;
    return TRUE;
}

static gboolean __mirage_disc_cif_can_load_file (MIRAGE_Disc *self, gchar *filename, GError **error) {
    MIRAGE_Disc_CIFPrivate *_priv = MIRAGE_DISC_CIF_GET_PRIVATE(self);

    /* Does file exist? */
    if (!g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
        return FALSE;
    }
    
    /* FIXME: Should support anything that passes the RIFF test and 
       ignore suffixes? */
    if (!mirage_helper_match_suffixes(filename, _priv->parser_info->suffixes)) {
        return FALSE;
    }

    /* Also check that there's appropriate signature */
    FILE *file = g_fopen(filename, "r");
    if (!file) {
        return FALSE;
    }
    
    gchar sig[4] = {0};
    fread(sig, 4, 1, file);
    fclose(file);
    if (memcmp(sig, "RIFF", 4)) {
        return FALSE;
    }
    
    return TRUE;
}

static gboolean __mirage_disc_cif_load_image (MIRAGE_Disc *self, gchar **filenames, GError **error) {
    MIRAGE_Disc_CIFPrivate *_priv = MIRAGE_DISC_CIF_GET_PRIVATE(self);
    GError *local_error = NULL;
    gboolean succeeded = TRUE;
    
    /* For now, CIF parser supports only one-file images */
    if (g_strv_length(filenames) > 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: only single-file images supported!\n", __func__);
        mirage_error(MIRAGE_E_SINGLEFILE, error);
        return FALSE;
    }
    
    mirage_disc_set_filenames(self, filenames, NULL);
    _priv->cif_filename = g_strdup(filenames[0]);
    
    /* Map the file using GLib's GMappedFile */
    _priv->cif_mapped = g_mapped_file_new(_priv->cif_filename, FALSE, &local_error);
    if (!_priv->cif_mapped) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to map file '%s': %s!\n", __func__, _priv->cif_filename, local_error->message);
        g_error_free(local_error);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }
    
    _priv->cif_data = (guint8 *)g_mapped_file_get_contents(_priv->cif_mapped);
    
    /* Load disc */
    succeeded = __mirage_disc_cif_load_disc(self, error);

    _priv->cif_data = NULL;
    g_mapped_file_free(_priv->cif_mapped);
        
    return succeeded;    
}


/******************************************************************************\
 *                                Object init                                 *
\******************************************************************************/
/* Our parent class */
static MIRAGE_DiscClass *parent_class = NULL;

static void __mirage_disc_cif_instance_init (GTypeInstance *instance, gpointer g_class) {
    MIRAGE_Disc_CIF *self = MIRAGE_DISC_CIF(instance);
    MIRAGE_Disc_CIFPrivate *_priv = MIRAGE_DISC_CIF_GET_PRIVATE(self);
    
    /* Create parser info */
    _priv->parser_info = mirage_helper_create_parser_info(
        "PARSER-CIF",
        "CIF Image Parser",
        "1.0.0",
        "Henrik Stokseth",
        FALSE,
        "CIF (Easy CD Creator <= v5.0) images",
        2, ".cif", NULL
    );
    
    return;
}

static void __mirage_disc_cif_finalize (GObject *obj) {
    MIRAGE_Disc_CIF *self_cif = MIRAGE_DISC_CIF(obj);
    MIRAGE_Disc_CIFPrivate *_priv = MIRAGE_DISC_CIF_GET_PRIVATE(self_cif);
    
    MIRAGE_DEBUG(self_cif, MIRAGE_DEBUG_GOBJECT, "%s:\n", __func__);

    g_free(_priv->cif_filename);
    
    /* Free parser info */
    mirage_helper_destroy_parser_info(_priv->parser_info);

    /* Chain up to the parent class */
    MIRAGE_DEBUG(self_cif, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __func__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __mirage_disc_cif_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_DiscClass *class_disc = MIRAGE_DISC_CLASS(g_class);
    MIRAGE_Disc_CIFClass *klass = MIRAGE_DISC_CIF_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Disc_CIFPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_disc_cif_finalize;
    
    /* Initialize MIRAGE_Disc methods */
    class_disc->get_parser_info = __mirage_disc_cif_get_parser_info;
    class_disc->can_load_file = __mirage_disc_cif_can_load_file;
    class_disc->load_image = __mirage_disc_cif_load_image;
        
    return;
}

GType mirage_disc_cif_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Disc_CIFClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_disc_cif_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Disc_CIF),
            0,      /* n_preallocs */
            __mirage_disc_cif_instance_init    /* instance_init */
        };
        
        type = g_type_module_register_type(module, MIRAGE_TYPE_DISC, "MIRAGE_Disc_CIF", &info, 0);
    }
    
    return type;
}

