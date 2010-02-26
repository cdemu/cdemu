/*
 *  libMirage: UIF image parser: Parser object
 *  Copyright (C) 2008-2010 Henrik Stokseth
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

#define __debug__ "UIF-Parser"


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_PARSER_UIF_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_UIF, MIRAGE_Parser_UIFPrivate))

typedef struct {
    GObject *disc;
    
    bbis_t bbis_block;
    blhr_t blhr_block, blms_block, blss_block;
    blhr_data_t *blhr_data;
    guint8 *blms_data, *blss_data;

    gint outtype;

    gchar *uif_filename;
    FILE *uif_file;
    guint uif_file_size;

    z_stream z;
    /* DES_key_schedule *ctx; */
} MIRAGE_Parser_UIFPrivate;

static gboolean __mirage_parser_uif_parse_iso (MIRAGE_Parser *self, GError **error) {
    MIRAGE_Parser_UIFPrivate *_priv = MIRAGE_PARSER_UIF_GET_PRIVATE(self);

    GObject *cur_session = NULL;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading ISO blocks\n", __debug__);        
    
    /* Get current session */
    if (!mirage_disc_get_session_by_index(MIRAGE_DISC(_priv->disc), -1, &cur_session, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current session!\n", __debug__);
        return FALSE;
    }

    /* TODO: This parser is incomplete. So trigger a loading failure here! */
    return FALSE;
}

static guint8 *__show_hash (guint8 *hash) {
    gint i = 0;
    static guint8 vis[33];
    static const gchar hex[16] = "0123456789abcdef";
    guint8 *p;

    p = vis;
    for (i = 0; i < 16; i++) {
        *p++ = hex[hash[i] >> 4];
        *p++ = hex[hash[i] & 15];
    }
    *p = 0;

    return vis;
}

static gint __unzip (z_stream *z, guint8 *in_data, guint32 in_size, guint8 *out_data, guint32 out_size) {
    inflateReset(z);

    z->next_in = in_data;
    z->avail_in = in_size;
    z->next_out = out_data;
    z->avail_out = out_size;

    if (inflate(z, Z_SYNC_FLUSH) != Z_STREAM_END) {
        return (gint) -1; /* Return -1 on error */
    }

    return z->total_out;
}

static guint8 *__blhr_unzip(FILE *fd, z_stream *z, guint32 zsize, guint32 unzsize, gint compressed) {
    guint8 *in_data  = NULL;
    guint8 *out_data = NULL;
    gint ret;

    if (compressed) {
        in_data = g_malloc(zsize);
        if (!in_data) return NULL;
        out_data = g_malloc(unzsize);
        if (!out_data) return NULL;
          
        if (fread(in_data, 1, zsize, fd) != zsize) {
            return NULL;
        }

        ret = __unzip(z, in_data, zsize, (void *) out_data, unzsize);
        if (ret == -1) return NULL;

        g_free(in_data);
    } else {
        out_data = g_malloc(unzsize);
        if (!out_data) return NULL;

        if (fread(out_data, 1, unzsize, fd) != zsize) {
            return NULL;
        }
    }

    return out_data;
}

static gboolean __mirage_parser_uif_load_disc (MIRAGE_Parser *self, GError **error) {
    MIRAGE_Parser_UIFPrivate *_priv = MIRAGE_PARSER_UIF_GET_PRIVATE(self);
    gint cur_pos;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: loading file '%s' of size %u.\n\n", __debug__, _priv->uif_filename, _priv->uif_file_size);

    /* Init some pointers */
    _priv->blhr_data = NULL;
    _priv->blms_data = NULL;
    _priv->blss_data = NULL;

    /* Read "bbis" block */
    cur_pos = _priv->uif_file_size - sizeof(bbis_t);
    fseeko(_priv->uif_file, cur_pos, SEEK_SET);
    if (fread(&_priv->bbis_block, sizeof(bbis_t), 1, _priv->uif_file) < 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read 'bbis' block!\n", __debug__);        
        mirage_error(MIRAGE_E_READFAILED, error);
        return FALSE;
    }
    if (memcmp(_priv->bbis_block.sign, "bbis", 4)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: expected 'bbis' signature!\n", __debug__);        
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: BBIS BLOCK:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   signature: %.4s\n", __debug__, _priv->bbis_block.sign);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   BBIS size: %i\n", __debug__, _priv->bbis_block.bbis_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   version: %i\n", __debug__, _priv->bbis_block.ver);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   image type: %i\n", __debug__, _priv->bbis_block.image_type);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   unknown 1: %i\n", __debug__, _priv->bbis_block.unknown1);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   padding: %i\n", __debug__, _priv->bbis_block.padding);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   sectors: %i\n", __debug__, _priv->bbis_block.sectors);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   sector size: %i\n", __debug__, _priv->bbis_block.sector_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   unknown 2: %i\n", __debug__, _priv->bbis_block.unknown2);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   block header offset: %i (%X)\n", __debug__, _priv->bbis_block.blhr_ofs, _priv->bbis_block.blhr_ofs);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   size of blhr, blhr_data and bbis blocks: %i\n", __debug__, _priv->bbis_block.blhr_bbis_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   password hash: %s\n", __debug__, __show_hash(_priv->bbis_block.hash));
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   fixed key: %i\n", __debug__, _priv->bbis_block.fixedkey);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   unknown 4: %i\n", __debug__, _priv->bbis_block.unknown3);

    /* Determine image type */
    if (_priv->bbis_block.image_type == IMAGE_TYPE_ISO) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: image type: ISO\n", __debug__);
    } else if (_priv->bbis_block.image_type == IMAGE_TYPE_MIXED) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: image type: Raw or Mixed\n", __debug__);
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: inrecognized image type: %i\n", __debug__, _priv->bbis_block.image_type);
        return FALSE;
    }

    /* Read "blhr" block */
    cur_pos = _priv->bbis_block.blhr_ofs;
    fseeko(_priv->uif_file, cur_pos, SEEK_SET);
    if (fread(&_priv->blhr_block, sizeof(blhr_t), 1, _priv->uif_file) < 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read 'blhr' block!\n", __debug__);        
        mirage_error(MIRAGE_E_READFAILED, error);
        return FALSE;
    }
    if(memcmp(_priv->blhr_block.sign, "blhr", 4)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: expected 'blhr' signature!\n", __debug__);
        if (!memcmp(_priv->blhr_block.sign, "bsdr", 4)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: found 'bsdr' signature! We don't support encryption yet!\n", __debug__);
        }
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: BLHR BLOCK:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   signature: %.4s\n", __debug__, _priv->blhr_block.sign);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   block size (+8): %i\n", __debug__, _priv->blhr_block.block_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   compressed: %i\n", __debug__, _priv->blhr_block.compressed);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   number of data blocks: %i\n", __debug__, _priv->blhr_block.num_blocks);

    /* Read blhr data blocks */
    _priv->blhr_data = (blhr_data_t *)__blhr_unzip(_priv->uif_file, &_priv->z, _priv->blhr_block.block_size - 8, sizeof(blhr_data_t) * _priv->blhr_block.num_blocks, _priv->blhr_block.compressed);
    if (!_priv->blhr_data) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: error reading blhr data!\n", __debug__);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }

    /* Determine output type and read extra blocks */
    switch (_priv->bbis_block.image_type) {
        case IMAGE_TYPE_MIXED: {
            /* Read "blms" block */
            cur_pos = _priv->bbis_block.blhr_ofs + sizeof(blhr_t) + (_priv->blhr_block.block_size - 8);
            fseeko(_priv->uif_file, cur_pos, SEEK_SET);
            if (fread(&_priv->blms_block, sizeof(blhr_t), 1, _priv->uif_file) < 1) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read 'blms' block!\n", __debug__);        
                mirage_error(MIRAGE_E_READFAILED, error);
                return FALSE;
            }
            if (memcmp(_priv->blms_block.sign, "blms", 4)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Expected 'blms' signature!\n", __debug__);
                mirage_error(MIRAGE_E_PARSER, error);
                return FALSE;
            }

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: BLMS BLOCK:\n", __debug__);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   signature: %.4s\n", __debug__, _priv->blms_block.sign);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   block size (+8): %i\n", __debug__, _priv->blms_block.block_size);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   compressed: %i\n", __debug__, _priv->blms_block.compressed);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   number of data blocks: %i\n", __debug__, _priv->blms_block.num_blocks);

            /* Read blms data blocks */
            _priv->blms_data = __blhr_unzip(_priv->uif_file, &_priv->z, _priv->blms_block.block_size - 8, _priv->blms_block.num_blocks, _priv->blms_block.compressed);
            if (!_priv->blms_data) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: error reading blms data!\n", __debug__);
                mirage_error(MIRAGE_E_PARSER, error);
                return FALSE;
            }

            /* Read "blss" block */
            cur_pos = _priv->bbis_block.blhr_ofs + sizeof(blhr_t) + (_priv->blhr_block.block_size - 8) + (_priv->blms_block.block_size - 8);
            fseeko(_priv->uif_file, cur_pos, SEEK_SET);
            if (fread(&_priv->blss_block, sizeof(blhr_t), 1, _priv->uif_file) < 1) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read 'blss' block!\n", __debug__);        
                mirage_error(MIRAGE_E_READFAILED, error);
                return FALSE;
            }
            if (memcmp(_priv->blss_block.sign, "blss", 4)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: expected 'blss' signature!\n", __debug__);
                mirage_error(MIRAGE_E_PARSER, error);
                return FALSE;
            }

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: BLSS BLOCK:\n", __debug__);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   signature: %.4s\n", __debug__, _priv->blss_block.sign);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   block size (+12): %i\n", __debug__, _priv->blss_block.block_size);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   compressed: %i\n", __debug__, _priv->blss_block.compressed);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   number of data blocks: %i\n", __debug__, _priv->blss_block.num_blocks);

            /* Read blss data blocks if appropriate */
            if (_priv->blss_block.num_blocks) {
                _priv->outtype = OUT_CUE;
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: output type CUE!\n", __debug__);
                fseeko(_priv->uif_file, 4, SEEK_CUR); /* Skip 4 bytes here */
                _priv->blss_data = __blhr_unzip(_priv->uif_file, &_priv->z, _priv->blss_block.block_size - 12, _priv->blss_block.num_blocks, _priv->blss_block.compressed);
                if (!_priv->blss_data) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: error reading blms data!\n", __debug__);
                    mirage_error(MIRAGE_E_PARSER, error);
                    return FALSE;
                }
            } else {
                _priv->outtype = OUT_NRG;
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: output type NRG!\n", __debug__);
            }
            break;
        }
        case IMAGE_TYPE_ISO: {
            _priv->outtype = OUT_ISO;
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: output type ISO!\n", __debug__);
            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown image type! Aborting!\n", __debug__);
            mirage_error(MIRAGE_E_PARSER, error);
            return FALSE;
        }
    }

    /* For now we only support (and assume) CD media */    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD-ROM image\n", __debug__);
    mirage_disc_set_medium_type(MIRAGE_DISC(_priv->disc), MIRAGE_MEDIUM_CD, NULL);

    /* CD-ROMs start at -150 as per Red Book... */
    mirage_disc_layout_set_start_sector(MIRAGE_DISC(_priv->disc), -150, NULL);

    /* Add session */
    if (!mirage_disc_add_session_by_number(MIRAGE_DISC(_priv->disc), 0, NULL, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __debug__);        
        return FALSE;
    }

    /* Load tracks */
    switch (_priv->outtype) {
        case OUT_ISO:
            if (!__mirage_parser_uif_parse_iso(self, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse ISO blocks!\n", __debug__);
                return FALSE;
            }
            break;
#if 0
        case OUT_CUE:
        case OUT_NRG:
            if (!__mirage_parser_uif_parse_track_entries(self, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse track entries!\n", __debug__);
                return FALSE;
            }
            break;
#endif
        default:
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Unknown image type! Aborting!\n", __debug__);
            return FALSE;
    }

    return TRUE;
}


/******************************************************************************\
 *                     MIRAGE_Parser methods implementation                     *
\******************************************************************************/
static gboolean __mirage_parser_uif_load_image (MIRAGE_Parser *self, gchar **filenames, GObject **disc, GError **error) {
    MIRAGE_Parser_UIFPrivate *_priv = MIRAGE_PARSER_UIF_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    FILE *file;
    gchar sig[4] = "";
    gint cur_pos = 0;

    /* Open file */
    file = g_fopen(filenames[0], "r");
    if (!file) {
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }
    
    /* Check the signature */
    cur_pos -= sizeof(bbis_t);
    fseeko(file, cur_pos, SEEK_END);
    if (fread(sig, 4, 1, file) < 1) {
        fclose(file);
        mirage_error(MIRAGE_E_READFAILED, error);
        return FALSE;
    }

    if (memcmp(sig, "bbis", 4)) {
        fclose(file);
        mirage_error(MIRAGE_E_CANTHANDLE, error);
        return FALSE;
    }

    _priv->uif_file = file;
    
    /* Create disc */
    _priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_attach_child(MIRAGE_OBJECT(self), _priv->disc, NULL);

    mirage_disc_set_filename(MIRAGE_DISC(_priv->disc), filenames, NULL);
    _priv->uif_filename = g_strdup(filenames[0]);

    /* Get filesize */
    fseeko(_priv->uif_file, 0, SEEK_END);
    _priv->uif_file_size = ftello(_priv->uif_file);

    /* Init zlib */
    _priv->z.zalloc = (alloc_func) Z_NULL;
    _priv->z.zfree = (free_func) Z_NULL;
    _priv->z.opaque = (voidpf) Z_NULL;
    if (inflateInit(&_priv->z)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: zlib initialization error!\n", __debug__);
        succeeded = FALSE;
        goto end;
    }

    /* Load disc */
    succeeded = __mirage_parser_uif_load_disc(self, error);

    inflateEnd(&_priv->z);

end:
    fclose(_priv->uif_file);
        
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

static void __mirage_parser_uif_instance_init (GTypeInstance *instance, gpointer g_class) {
    mirage_parser_generate_parser_info(MIRAGE_PARSER(instance),
        "PARSER-UIF",
        "UIF Image Parser",
        "UIF (Universal Image Format) images",
        "application/libmirage-uif"
    );
    
    return;
}

static void __mirage_parser_uif_finalize (GObject *obj) {
    MIRAGE_Parser_UIF *self_uif = MIRAGE_PARSER_UIF(obj);
    MIRAGE_Parser_UIFPrivate *_priv = MIRAGE_PARSER_UIF_GET_PRIVATE(self_uif);
    
    MIRAGE_DEBUG(self_uif, MIRAGE_DEBUG_GOBJECT, "%s: finalizing object\n", __debug__);

    g_free(_priv->uif_filename);

    if (_priv->blhr_data) g_free(_priv->blhr_data);
    if (_priv->blms_data) g_free(_priv->blms_data);
    if (_priv->blss_data) g_free(_priv->blss_data);
    
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self_uif, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __debug__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __mirage_parser_uif_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_ParserClass *class_parser = MIRAGE_PARSER_CLASS(g_class);
    MIRAGE_Parser_UIFClass *klass = MIRAGE_PARSER_UIF_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Parser_UIFPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_parser_uif_finalize;
    
    /* Initialize MIRAGE_Parser methods */
    class_parser->load_image = __mirage_parser_uif_load_image;
        
    return;
}

GType mirage_parser_uif_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Parser_UIFClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_parser_uif_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Parser_UIF),
            0,      /* n_preallocs */
            __mirage_parser_uif_instance_init    /* instance_init */
        };
        
        type = g_type_module_register_type(module, MIRAGE_TYPE_PARSER, "MIRAGE_Parser_UIF", &info, 0);
    }
    
    return type;
}

