/*
 *  libMirage: UIF image parser: Disc object
 *  Copyright (C) 2006-2008 Henrik Stokseth
 *
 *  Thanks to Luigi Auriemma for reverse engineering work.
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

#include "image-uif.h"


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_DISC_UIF_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_DISC_UIF, MIRAGE_Disc_UIFPrivate))

typedef struct {   
    bbis_t      bbis_block;
    blhr_t      blhr_block, blms_block, blss_block;
    blhr_data_t *blhr_data;
    guint8      *blms_data, *blss_data;

    gint outtype;

    gchar  *uif_filename;
    FILE   *uif_file;
    guint  uif_file_size;

    z_stream         z;
    /* DES_key_schedule *ctx; */

    /* Parser info */
    MIRAGE_ParserInfo *parser_info;
} MIRAGE_Disc_UIFPrivate;

static gboolean __mirage_disc_uif_parse_iso (MIRAGE_Disc *self, GError **error) {
    MIRAGE_Disc_UIFPrivate *_priv = MIRAGE_DISC_UIF_GET_PRIVATE(self);

    GObject            *cur_session = NULL;
    gint               medium_type = 0;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading ISO blocks\n", __func__);        
    
    /* Fetch medium type which we'll need later */
    mirage_disc_get_medium_type(self, &medium_type, NULL);

    /* Get current session */
    if (!mirage_disc_get_session_by_index(self, -1, &cur_session, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current session!\n", __func__);
        return FALSE;
    }

    /* TODO: This parser is incomplete. So trigger a loading failure here! */
    return FALSE;
}

static guint8 *show_hash(guint8 *hash) {
    gint               i = 0;
    static guint8      vis[33];
    static const gchar hex[16] = "0123456789abcdef";
    guint8             *p = NULL;

    p = vis;
    for(i = 0; i < 16; i++) {
        *p++ = hex[hash[i] >> 4];
        *p++ = hex[hash[i] & 15];
    }
    *p = 0;

    return vis;
}

static gint unzip(z_stream *z, guint8 *in_data, guint32 in_size, guint8 *out_data, guint32 out_size) {
    inflateReset(z);

    z->next_in   = in_data;
    z->avail_in  = in_size;
    z->next_out  = out_data;
    z->avail_out = out_size;

    if(inflate(z, Z_SYNC_FLUSH) != Z_STREAM_END) {
        return (gint) -1; /* Return -1 on error */
    }

    return z->total_out;
}

static guint8 *blhr_unzip(FILE *fd, z_stream *z, guint32 zsize, guint32 unzsize, gint compressed) {
    guint8 *in_data  = NULL;
    guint8 *out_data = NULL;
    gint   ret;

    if(compressed) {
        in_data = g_malloc(zsize);
        if(!in_data) return NULL;
        out_data = g_malloc(unzsize);
        if(!out_data) return NULL;
          
        if(fread(in_data, 1, zsize, fd) != zsize) {
            return NULL;
        }

        ret = unzip(z, in_data, zsize, (void *) out_data, unzsize);
        if (ret == -1) return NULL;

        g_free(in_data);
    } else {
        out_data = g_malloc(unzsize);
        if(!out_data) return NULL;

        if(fread(out_data, 1, unzsize, fd) != zsize) {
            return NULL;
        }
    }

    return out_data;
}

static gboolean __mirage_disc_uif_load_disc (MIRAGE_Disc *self, GError **error) {
    MIRAGE_Disc_UIFPrivate *_priv = MIRAGE_DISC_UIF_GET_PRIVATE(self);
    gint                   cur_pos;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Loading file '%s' of size %u.\n\n", __func__, _priv->uif_filename, _priv->uif_file_size);

    /* Init some pointers */
    _priv->blhr_data = NULL;
    _priv->blms_data = NULL;
    _priv->blss_data = NULL;

    /* Read "bbis" block */
    cur_pos = _priv->uif_file_size - sizeof(bbis_t);
    fseeko(_priv->uif_file, cur_pos, SEEK_SET);
    fread(&_priv->bbis_block, sizeof(bbis_t), 1, _priv->uif_file);
    if(memcmp(_priv->bbis_block.sign, "bbis", 4)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Expected \"bbis\" signature!\n", __func__);        
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: BBIS BLOCK:\n", __func__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Signature: %.4s\n", __func__, _priv->bbis_block.sign);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   BBIS size: %i\n", __func__, _priv->bbis_block.bbis_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Version: %i\n", __func__, _priv->bbis_block.ver);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Image type: %i\n", __func__, _priv->bbis_block.image_type);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Unknown 1: %i\n", __func__, _priv->bbis_block.unknown1);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Padding: %i\n", __func__, _priv->bbis_block.padding);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Sectors: %i\n", __func__, _priv->bbis_block.sectors);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Sector size: %i\n", __func__, _priv->bbis_block.sector_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Unknown 2: %i\n", __func__, _priv->bbis_block.unknown2);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Block header offset: %i (%X)\n", __func__, _priv->bbis_block.blhr_ofs, _priv->bbis_block.blhr_ofs);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Size of blhr, blhr_data and bbis blocks: %i\n", __func__, _priv->bbis_block.blhr_bbis_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Password hash: %s\n", __func__, show_hash(_priv->bbis_block.hash));
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Fixed key: %i\n", __func__, _priv->bbis_block.fixedkey);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Unknown 4: %i\n", __func__, _priv->bbis_block.unknown3);

    /* Determine image type */
    if(_priv->bbis_block.image_type == IMAGE_TYPE_ISO) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Image type: ISO\n", __func__);
    } else if(_priv->bbis_block.image_type == IMAGE_TYPE_MIXED) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Image type: Raw or Mixed\n", __func__);
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Unrecognized image type: %i\n", __func__, _priv->bbis_block.image_type);
        return FALSE;
    }

    /* Read "blhr" block */
    cur_pos = _priv->bbis_block.blhr_ofs;
    fseeko(_priv->uif_file, cur_pos, SEEK_SET);
    fread(&_priv->blhr_block, sizeof(blhr_t), 1, _priv->uif_file);
    if(memcmp(_priv->blhr_block.sign, "blhr", 4)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Expected 'blhr' signature!\n", __func__);
        if(!memcmp(_priv->blhr_block.sign, "bsdr", 4)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Found 'bsdr' signature! We don't support encryption yet!\n", __func__);
        }
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: BLHR BLOCK:\n", __func__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Signature: %.4s\n", __func__, _priv->blhr_block.sign);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Block size (+8): %i\n", __func__, _priv->blhr_block.block_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Compressed: %i\n", __func__, _priv->blhr_block.compressed);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Number of data blocks: %i\n", __func__, _priv->blhr_block.num_blocks);

    /* Read blhr data blocks */
    _priv->blhr_data = (blhr_data_t *) blhr_unzip(_priv->uif_file, &_priv->z, _priv->blhr_block.block_size - 8, sizeof(blhr_data_t) * _priv->blhr_block.num_blocks, _priv->blhr_block.compressed);
    if (!_priv->blhr_data) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Error reading blhr data!\n", __func__);
        return FALSE;
    }

    /* Determine output type and read extra blocks */
    switch(_priv->bbis_block.image_type) {
        case IMAGE_TYPE_MIXED:
            /* Read "blms" block */
            cur_pos = _priv->bbis_block.blhr_ofs + sizeof(blhr_t) + (_priv->blhr_block.block_size - 8);
            fseeko(_priv->uif_file, cur_pos, SEEK_SET);
            fread(&_priv->blms_block, sizeof(blhr_t), 1, _priv->uif_file);
            if(memcmp(_priv->blms_block.sign, "blms", 4)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Expected 'blms' signature!\n", __func__);
                return FALSE;
            }

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: BLMS BLOCK:\n", __func__);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Signature: %.4s\n", __func__, _priv->blms_block.sign);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Block size (+8): %i\n", __func__, _priv->blms_block.block_size);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Compressed: %i\n", __func__, _priv->blms_block.compressed);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Number of data blocks: %i\n", __func__, _priv->blms_block.num_blocks);

            /* Read blms data blocks */
            _priv->blms_data = (guint8 *) blhr_unzip(_priv->uif_file, &_priv->z, _priv->blms_block.block_size - 8, _priv->blms_block.num_blocks, _priv->blms_block.compressed);
            if (!_priv->blms_data) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Error reading blms data!\n", __func__);
                return FALSE;
            }

            /* Read "blss" block */
            cur_pos = _priv->bbis_block.blhr_ofs + sizeof(blhr_t) + (_priv->blhr_block.block_size - 8) + (_priv->blms_block.block_size - 8);
            fseeko(_priv->uif_file, cur_pos, SEEK_SET);
            fread(&_priv->blss_block, sizeof(blhr_t), 1, _priv->uif_file);
            if(memcmp(_priv->blss_block.sign, "blss", 4)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Expected 'blss' signature!\n", __func__);
                return FALSE;
            }

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: BLSS BLOCK:\n", __func__);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Signature: %.4s\n", __func__, _priv->blss_block.sign);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Block size (+8): %i\n", __func__, _priv->blss_block.block_size);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Compressed: %i\n", __func__, _priv->blss_block.compressed);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Number of data blocks: %i\n", __func__, _priv->blss_block.num_blocks);

            /* Read blss data blocks if appropriate */
            if(_priv->blss_block.num_blocks) {
                _priv->outtype = OUT_CUE;
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Output type CUE!\n", __func__);
                fseeko(_priv->uif_file, 4, SEEK_CUR); /* Skip 4 bytes here */
                _priv->blss_data = (guint8 *) blhr_unzip(_priv->uif_file, &_priv->z, _priv->blss_block.block_size - 12, _priv->blss_block.num_blocks, _priv->blss_block.compressed);
                if (!_priv->blss_data) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Error reading blms data!\n", __func__);
                    return FALSE;
                }
            } else {
                _priv->outtype = OUT_NRG;
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Output type NRG!\n", __func__);
            }
            break;
        case IMAGE_TYPE_ISO:
            _priv->outtype = OUT_ISO;
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Output type ISO!\n", __func__);
            break;
        default:
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Unknown image type! Aborting!\n", __func__);
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
    switch (_priv->outtype) {
        case OUT_ISO:
            if (!__mirage_disc_uif_parse_iso(self, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse ISO blocks!\n", __func__);
                return FALSE;
            }
            break;
#if 0
        case OUT_CUE:
        case OUT_NRG:
            if (!__mirage_disc_uif_parse_track_entries(self, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse track entries!\n", __func__);
                return FALSE;
            }
            break;
#endif
        default:
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Unknown image type! Aborting!\n", __func__);
            return FALSE;
    }

    return TRUE;
}


/******************************************************************************\
 *                     MIRAGE_Disc methods implementation                     *
\******************************************************************************/
static gboolean __mirage_disc_uif_get_parser_info (MIRAGE_Disc *self, MIRAGE_ParserInfo **parser_info, GError **error) {
    MIRAGE_Disc_UIFPrivate *_priv = MIRAGE_DISC_UIF_GET_PRIVATE(self);
    *parser_info = _priv->parser_info;
    return TRUE;
}

static gboolean __mirage_disc_uif_can_load_file (MIRAGE_Disc *self, gchar *filename, GError **error) {
    MIRAGE_Disc_UIFPrivate *_priv = MIRAGE_DISC_UIF_GET_PRIVATE(self);

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
    FILE  *file = g_fopen(filename, "r");
    gchar sig[4] = {0};
    gint cur_pos = 0;

    if (!file) {
        return FALSE;
    }

    cur_pos = -sizeof(bbis_t);
    fseeko(file, cur_pos, SEEK_END);
    fread(sig, 4, 1, file);
    fclose(file);
    if (memcmp(sig, "bbis", 4)) {
        return FALSE;
    }

    return TRUE;
}

static gboolean __mirage_disc_uif_load_image (MIRAGE_Disc *self, gchar **filenames, GError **error) {
    MIRAGE_Disc_UIFPrivate *_priv = MIRAGE_DISC_UIF_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    
    /* For now, UIF parser supports only one-file images */
    if (g_strv_length(filenames) > 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: only single-file images supported!\n", __func__);
        mirage_error(MIRAGE_E_SINGLEFILE, error);
        return FALSE;
    }

    /* Open file */
    _priv->uif_file = g_fopen(filenames[0], "r");
    if (!_priv->uif_file) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open file '%s'!\n", __func__, filenames[0]);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }

    /* Set filename */
    mirage_disc_set_filenames(self, filenames, NULL);
    _priv->uif_filename = g_strdup(filenames[0]);

    /* Get filesize */
    fseeko(_priv->uif_file, 0, SEEK_END);
    _priv->uif_file_size = ftello(_priv->uif_file);

    /* Init zlib */
    _priv->z.zalloc = (alloc_func) Z_NULL;
    _priv->z.zfree  = (free_func) Z_NULL;
    _priv->z.opaque = (voidpf) Z_NULL;
    if (inflateInit(&_priv->z)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: zlib initialization error!\n", __func__);
        return FALSE;
    }

    /* Load disc */
    succeeded = __mirage_disc_uif_load_disc(self, error);

    inflateEnd(&_priv->z);

    fclose(_priv->uif_file);
        
    return succeeded;    
}


/******************************************************************************\
 *                                Object init                                 *
\******************************************************************************/
/* Our parent class */
static MIRAGE_DiscClass *parent_class = NULL;

static void __mirage_disc_uif_instance_init (GTypeInstance *instance, gpointer g_class) {
    MIRAGE_Disc_UIF *self = MIRAGE_DISC_UIF(instance);
    MIRAGE_Disc_UIFPrivate *_priv = MIRAGE_DISC_UIF_GET_PRIVATE(self);
    
    /* Create parser info */
    _priv->parser_info = mirage_helper_create_parser_info(
        "PARSER-UIF",
        "UIF Image Parser",
        "1.0.0",
        "Henrik Stokseth",
        FALSE,
        "UIF (Universal Image Format) images",
        2, ".uif", NULL
    );
    
    return;
}

static void __mirage_disc_uif_finalize (GObject *obj) {
    MIRAGE_Disc_UIF *self_uif = MIRAGE_DISC_UIF(obj);
    MIRAGE_Disc_UIFPrivate *_priv = MIRAGE_DISC_UIF_GET_PRIVATE(self_uif);
    
    MIRAGE_DEBUG(self_uif, MIRAGE_DEBUG_GOBJECT, "%s:\n", __func__);

    g_free(_priv->uif_filename);

    if(_priv->blhr_data) g_free(_priv->blhr_data);
    if(_priv->blms_data) g_free(_priv->blms_data);
    if(_priv->blss_data) g_free(_priv->blss_data);
    
    /* Free parser info */
    mirage_helper_destroy_parser_info(_priv->parser_info);

    /* Chain up to the parent class */
    MIRAGE_DEBUG(self_uif, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __func__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __mirage_disc_uif_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_DiscClass *class_disc = MIRAGE_DISC_CLASS(g_class);
    MIRAGE_Disc_UIFClass *klass = MIRAGE_DISC_UIF_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Disc_UIFPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_disc_uif_finalize;
    
    /* Initialize MIRAGE_Disc methods */
    class_disc->get_parser_info = __mirage_disc_uif_get_parser_info;
    class_disc->can_load_file = __mirage_disc_uif_can_load_file;
    class_disc->load_image = __mirage_disc_uif_load_image;
        
    return;
}

GType mirage_disc_uif_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Disc_UIFClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_disc_uif_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Disc_UIF),
            0,      /* n_preallocs */
            __mirage_disc_uif_instance_init    /* instance_init */
        };
        
        type = g_type_module_register_type(module, MIRAGE_TYPE_DISC, "MIRAGE_Disc_UIF", &info, 0);
    }
    
    return type;
}

