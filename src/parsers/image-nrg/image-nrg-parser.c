/*
 *  libMirage: NRG image parser: Parser object
 *  Copyright (C) 2006-2008 Rok Mandeljc
 * 
 *  Reverse-engineering work in March, 2005 by Henrik Stokseth.
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

#include "image-nrg.h"


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_PARSER_NRG_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_NRG, MIRAGE_Parser_NRGPrivate))

typedef struct {
    GObject *disc;
    
    gchar *nrg_filename;
    
    GList *block_index;
    gint  block_index_entries;

    gboolean old_format;    

    guint8 *nrg_data;
    guint  nrg_data_length;

    gint32 prev_session_end;

    NRG_ETN_Block *etn_blocks;
    gint num_etn_blocks;

    NRG_CUE_Block *cue_blocks;
    gint num_cue_blocks;

    NRG_DAO_Header *dao_header;
    NRG_DAO_Block *dao_blocks;
    gint num_dao_blocks;
} MIRAGE_Parser_NRGPrivate;

typedef struct {
    gchar *block_id;
    gint subblock_offset;
    gint subblock_length;
} NRG_BlockIDs;

/* NULL terminated list of valid block IDs and subblock offset + length */
static NRG_BlockIDs NRGBlockID[] = {
    { "CUEX", 0,  8  },
    { "CUES", 0,  8  },
    { "ETN2", 0,  32 },
    { "ETNF", 0,  20 },
    { "DAOX", 22, 42 },
    { "DAOI", 22, 30 },
    { "CDTX", 0,  0  },
    { "SINF", 0,  0  },
    { "MTYP", 0,  0  },
    { "END!", 0,  0  },
    { NULL,   0,  0  }
};

static gboolean __mirage_parser_nrg_build_block_index (MIRAGE_Parser *self, GError **error) {
    MIRAGE_Parser_NRGPrivate *_priv = MIRAGE_PARSER_NRG_GET_PRIVATE(self);
    NRGBlockIndexEntry *blockentry;
    GList *blockindex;
    gint num_blocks;
    gint index;
    guint8 *cur_ptr;

    /* Populate block index */
    cur_ptr = _priv->nrg_data;
    index = 0;
    do {
        blockentry = g_new0(NRGBlockIndexEntry, 1);
        if (!blockentry) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to allocate memory for block index!\n", __func__);
            mirage_error(MIRAGE_E_PARSER, error);
            return FALSE;
        }
        blockentry->offset = (guint64) (cur_ptr - _priv->nrg_data);
        memcpy(blockentry->block_id, cur_ptr, 4);
        cur_ptr += 4;
        blockentry->length = GINT32_FROM_BE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
        cur_ptr += sizeof(guint32);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: block %2i, ID: %.4s, offset: %Li (0x%LX), length: %i (0x%X).\n", \
        __func__, index, blockentry->block_id, blockentry->offset, blockentry->offset, blockentry->length, blockentry->length);

        /* Got sub-blocks? */
        gint id_index;
        for (id_index = 0; NRGBlockID[id_index].block_id; id_index++) {
            if (!memcmp(blockentry->block_id, NRGBlockID[id_index].block_id, 4)) {
                if (NRGBlockID[id_index].subblock_length) {
                    blockentry->subblocks_offset = blockentry->offset + 8 + NRGBlockID[id_index].subblock_offset;
                    blockentry->subblocks_length = NRGBlockID[id_index].subblock_length;
                    blockentry->num_subblocks = (blockentry->length - NRGBlockID[id_index].subblock_offset) / blockentry->subblocks_length;
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Sub-blocks: %i, blocksize: %i.\n", __func__, blockentry->num_subblocks, blockentry->subblocks_length);
                } else {
                    /* This block has no sub-blocks */
                    blockentry->subblocks_offset = 0;
                    blockentry->subblocks_length = 0;
                    blockentry->num_subblocks = 0;
                }
                break;
            }
        }

        /* Add entry to list */
        blockindex = g_list_prepend(blockindex, blockentry);
        
        /* Get ready for next block */
        index++;
        cur_ptr += blockentry->length;
    } while (cur_ptr < _priv->nrg_data + _priv->nrg_data_length);
    
    blockindex = g_list_reverse(blockindex);
    num_blocks = g_list_length(blockindex);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: counted %i blocks.\n", __func__, num_blocks);

    /* Link to block index */
    _priv->block_index = blockindex;
    _priv->block_index_entries = num_blocks;

    return TRUE;
}

static gboolean __mirage_parser_nrg_destroy_block_index (MIRAGE_Parser *self) {
    MIRAGE_Parser_NRGPrivate *_priv = MIRAGE_PARSER_NRG_GET_PRIVATE(self);

    if (_priv->block_index) {
        GList *entry;
        
        G_LIST_FOR_EACH(entry, _priv->block_index) {
            g_free(entry->data);
        }
        g_list_free(_priv->block_index);
        _priv->block_index = NULL;
        _priv->block_index_entries = 0;
    }

    return TRUE;
}

static gint __find_by_block_id (gconstpointer a, gconstpointer b) {
    NRGBlockIndexEntry *blockentry = (NRGBlockIndexEntry *) a;
    gchar *block_id = (gchar *) b;

    return memcmp(blockentry->block_id, block_id, 4);
}

static NRGBlockIndexEntry *__mirage_parser_nrg_find_block_entry (MIRAGE_Parser *self, gchar *block_id, gint index) {
    MIRAGE_Parser_NRGPrivate *_priv = MIRAGE_PARSER_NRG_GET_PRIVATE(self);
    GList *list_start = _priv->block_index;
    gint cur_index = 0;

    do {
        GList *list_entry = g_list_find_custom(list_start, block_id, __find_by_block_id);

        if (list_entry) {
            if (cur_index == index) {
                return list_entry->data;
            } else {
                if (list_entry->next) {
                    list_start = list_entry->next;
                } else {
                    break;
                }
            }
        } else {
            break;
        }
    } while (cur_index++ < index);

    return NULL;
}

static gboolean __mirage_parser_nrg_load_medium_type (MIRAGE_Parser *self, GError **error) {
    MIRAGE_Parser_NRGPrivate *_priv = MIRAGE_PARSER_NRG_GET_PRIVATE(self);
    NRGBlockIndexEntry *blockentry;
    guint8 *cur_ptr;
    guint32 mtyp_data;

    /* Look up MTYP block */
    blockentry = __mirage_parser_nrg_find_block_entry(self, "MTYP", 0);
    if (!blockentry) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to look up 'MTYP' block!\n", __func__);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }
    cur_ptr = _priv->nrg_data + blockentry->offset + 8;

    mtyp_data = MIRAGE_CAST_DATA(cur_ptr, 0, guint32);
    mtyp_data = GINT32_FROM_BE(mtyp_data);
    cur_ptr += sizeof(guint32);
    
    /* Decode medium type */
    NERO_MEDIA_TYPE CD_EQUIV = MEDIA_CD | MEDIA_CDROM;
    NERO_MEDIA_TYPE DVD_EQUIV = MEDIA_DVD_ANY | MEDIA_DVD_ROM;
    NERO_MEDIA_TYPE BD_EQUIV = MEDIA_BD_ANY;
    NERO_MEDIA_TYPE HD_EQUIV = MEDIA_HD_DVD_ANY;

    if (mtyp_data & CD_EQUIV) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: medium type: CD-ROM\n", __func__);
        mirage_disc_set_medium_type(MIRAGE_DISC(_priv->disc), MIRAGE_MEDIUM_CD, NULL);
    } else if (mtyp_data & DVD_EQUIV) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: medium type: DVD-ROM\n", __func__);
        mirage_disc_set_medium_type(MIRAGE_DISC(_priv->disc), MIRAGE_MEDIUM_DVD, NULL);
    } else if (mtyp_data & BD_EQUIV) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: medium type: Blue-ray\n", __func__);
        mirage_disc_set_medium_type(MIRAGE_DISC(_priv->disc), MIRAGE_MEDIUM_BD, NULL);
    } else if (mtyp_data & HD_EQUIV) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: medium type: HD-DVD\n", __func__);
        mirage_disc_set_medium_type(MIRAGE_DISC(_priv->disc), MIRAGE_MEDIUM_HD, NULL);
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unhandled medium type: %d!\n", __func__, mtyp_data);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }

    return TRUE;
}

static gboolean __mirage_parser_nrg_decode_mode (MIRAGE_Parser *self, gint code, gint *mode, gint *main_sectsize, gint *sub_sectsize, GError **error) {    
    /* The meaning of the following codes was determined experimentally; we're
       missing mappings for Mode 2 Formless, but that doesn't seem a very common
       format anyway */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: mode code: %d\n", __func__, code);
    switch (code) {
        case 0x00: {
            /* Mode 1, user data only */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Mode 1, user data only\n", __func__, code);
            *mode = MIRAGE_MODE_MODE1;
            *main_sectsize = 2048;
            *sub_sectsize = 0;
            break;
        }
        case 0x02: {
            /* Mode 2 Form 1, user data only */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Mode 2 Form 1, user data only\n", __func__, code);
            *mode = MIRAGE_MODE_MODE2_FORM1;
            *main_sectsize = 2048;
            *sub_sectsize = 0;
            break;
        }
        case 0x03: {
            /* Mode 2 Form 2, user data only */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Mode 2 Form 2, user data only\n", __func__, code);
            *mode = MIRAGE_MODE_MODE2_FORM2;
            *main_sectsize = 2336;
            *sub_sectsize = 0;
            break;
        }
        case 0x05: {
            /* Mode 1, full sector */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Mode 1, full sector\n", __func__, code);
            *mode = MIRAGE_MODE_MODE1;
            *main_sectsize = 2352;
            *sub_sectsize = 0;
            break;
        }
        case 0x06: {
            /* Mode 2 Form 1 or Mode 2 Form 2, full sector */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Mode 2 Form 1/2, full sector\n", __func__, code);
            *mode = MIRAGE_MODE_MODE2_MIXED;
            *main_sectsize = 2352;
            *sub_sectsize = 0;
            break;
        }
        case 0x07: {
            /* Audio, full sector */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Audio, full sector\n", __func__, code);
            *mode = MIRAGE_MODE_AUDIO;
            *main_sectsize = 2352;
            *sub_sectsize = 0;
            break;
        }
        case 0x0F: {
             /* Mode 1, full sector with subchannel */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Mode 1, full sector with subchannel\n", __func__, code);
            *mode = MIRAGE_MODE_MODE1;
            *main_sectsize = 2352;
            *sub_sectsize = 96;
            break;
        }
        case 0x10: {
            /* Audio, full sector with subchannel */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Audio, full sector with subchannel\n", __func__, code);
            *mode = MIRAGE_MODE_AUDIO;
            *main_sectsize = 2352;
            *sub_sectsize = 96;
            break;
        }
        case 0x11: {
            /* Mode 2 Form 1 or Mode 2 Form 2, full sector with subchannel */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Mode 2 Form 1/2, full sector with subchannel\n", __func__, code);
            *mode = MIRAGE_MODE_MODE2_MIXED;
            *main_sectsize = 2352;
            *sub_sectsize = 96;
            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: Unknown mode code: %d!\n", __func__, code);
            mirage_error(MIRAGE_E_PARSER, error);
            return FALSE;
        }
    }
    
    return TRUE;
}

static gboolean __mirage_parser_nrg_load_etn_data (MIRAGE_Parser *self, gint session_num, GError **error) {
    MIRAGE_Parser_NRGPrivate *_priv = MIRAGE_PARSER_NRG_GET_PRIVATE(self);
    NRGBlockIndexEntry *blockentry;
    guint8 *cur_ptr;

    /* Look up ETN2 / ETNF block */
    if (!_priv->old_format) {
        blockentry = __mirage_parser_nrg_find_block_entry(self, "ETN2", session_num);
    } else {
        blockentry = __mirage_parser_nrg_find_block_entry(self, "ETNF", session_num);
    }
    if (!blockentry) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to look up 'ETN2' or 'ETNF' block!\n", __func__);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }
    cur_ptr = _priv->nrg_data + blockentry->offset + 8;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: %d ETN blocks\n", __func__, blockentry->num_subblocks);
    _priv->num_etn_blocks = blockentry->num_subblocks;
        
    /* Allocate space and read ETN data (we need to copy data because we'll have
       to modify it) */
    _priv->etn_blocks = g_new0(NRG_ETN_Block, blockentry->num_subblocks);
    if (!_priv->etn_blocks) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to allocate space for ETN blocks!\n", __func__);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }
    
    /* Read ETN blocks */
    gint i;
    for (i = 0; i < blockentry->num_subblocks; i++) {
        NRG_ETN_Block *block = &_priv->etn_blocks[i];

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: ETN block #%i\n", __func__, i);

        if (_priv->old_format) {
            block->offset = GINT32_FROM_BE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
            block->size = GINT32_FROM_BE(MIRAGE_CAST_DATA(cur_ptr, 4, guint32));
            block->mode = MIRAGE_CAST_DATA(cur_ptr, 11, guint8);
            block->sector = GINT32_FROM_BE(MIRAGE_CAST_DATA(cur_ptr, 12, guint32));
        }
        else {
            block->offset = GINT64_FROM_BE(MIRAGE_CAST_DATA(cur_ptr, 0, guint64));
            block->size = GINT64_FROM_BE(MIRAGE_CAST_DATA(cur_ptr, 8, guint64));
            block->mode = MIRAGE_CAST_DATA(cur_ptr, 19, guint8);
            block->sector = GINT32_FROM_BE(MIRAGE_CAST_DATA(cur_ptr, 20, guint32));
        }
        cur_ptr += blockentry->subblocks_length;
        
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  offset: %u\n", __func__, block->offset);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  size: %u\n", __func__, block->size);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  mode: 0x%X\n", __func__, block->mode);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  sector: %u\n", __func__, block->sector);
    }

    return TRUE;
}

static gboolean __mirage_parser_nrg_load_cue_data (MIRAGE_Parser *self, gint session_num, GError **error) {
    MIRAGE_Parser_NRGPrivate *_priv = MIRAGE_PARSER_NRG_GET_PRIVATE(self);
    NRGBlockIndexEntry *blockentry;
    guint8 *cur_ptr;

    /* Look up CUEX / CUES block */
    if (!_priv->old_format) {
        blockentry = __mirage_parser_nrg_find_block_entry(self, "CUEX", session_num);
    } else {
        blockentry = __mirage_parser_nrg_find_block_entry(self, "CUES", session_num);
    }
    if (!blockentry) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to look up 'CUEX' or 'CUES' block!\n", __func__);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }
    cur_ptr = _priv->nrg_data + blockentry->offset + 8;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: %d CUE blocks\n", __func__, blockentry->num_subblocks);
    _priv->num_cue_blocks = blockentry->num_subblocks;
        
    /* Allocate space and read CUE data (we need to copy data because we'll have
       to modify it) */
    _priv->cue_blocks = g_new0(NRG_CUE_Block, blockentry->num_subblocks);
    if (!_priv->cue_blocks) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to allocate space for CUE blocks!\n", __func__);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }
    
    /* Read CUE blocks */
    memcpy(_priv->cue_blocks, MIRAGE_CAST_PTR(cur_ptr, 0, guint8 *), blockentry->length);
    cur_ptr += blockentry->length;
    
    /* Conversion */
    gint i;
    for (i = 0; i < blockentry->num_subblocks; i++) {
        NRG_CUE_Block *block = &_priv->cue_blocks[i];

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CUE block #%i\n", __func__, i);

        /* BCD -> Hex */
        block->track = mirage_helper_bcd2hex(block->track);
        block->index = mirage_helper_bcd2hex(block->index);
        
        /* Old format has MSF format, new one has Big-endian LBA */
        if (_priv->old_format) {
            guint8 *hmsf = (guint8 *)&block->start_sector;
            block->start_sector = mirage_helper_msf2lba(hmsf[1], hmsf[2], hmsf[3], TRUE);
        } else {
            block->start_sector = GUINT32_FROM_BE(block->start_sector);
        }
        
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  adr/ctl: 0x%X\n", __func__, block->adr_ctl);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  track: 0x%X\n", __func__, block->track);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  index: 0x%X\n", __func__, block->index);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  start_sector: 0x%X\n", __func__, block->start_sector);
    }
    
    return TRUE;
}

static gboolean __mirage_parser_nrg_load_dao_data (MIRAGE_Parser *self, gint session_num, GError **error) {
    MIRAGE_Parser_NRGPrivate *_priv = MIRAGE_PARSER_NRG_GET_PRIVATE(self);
    NRGBlockIndexEntry *blockentry;
    guint8 *cur_ptr;

    /* Look up DAOX / DAOI block */
    if (!_priv->old_format) {
        blockentry = __mirage_parser_nrg_find_block_entry(self, "DAOX", session_num);
    } else {
        blockentry = __mirage_parser_nrg_find_block_entry(self, "DAOI", session_num);
    }
    if (!blockentry) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to look up 'DAOX' or 'DAOI' block!\n", __func__);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }
    cur_ptr = _priv->nrg_data + blockentry->offset + 8;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: %d DAO blocks\n", __func__, blockentry->num_subblocks);
    _priv->num_dao_blocks = blockentry->num_subblocks;
    
    /* Allocate space and read DAO header */
    _priv->dao_header = g_new0(NRG_DAO_Header, 1);
    if (!_priv->dao_header) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to allocate space for DAO header!\n", __func__);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }
    memcpy(_priv->dao_header, MIRAGE_CAST_PTR(cur_ptr, 0, NRG_DAO_Header *), sizeof(NRG_DAO_Header));
    cur_ptr += sizeof(NRG_DAO_Header);
    
    /* Allocate space and read DAO blocks */
    _priv->dao_blocks = g_new0(NRG_DAO_Block, blockentry->num_subblocks);
    gint i;
    for (i = 0; i < blockentry->num_subblocks; i++) {
        NRG_DAO_Block *block = &_priv->dao_blocks[i];

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: DAO block #%i\n", __func__, i);

        /* We read each block separately because the last fields are different 
           between formats */
        memcpy(block, MIRAGE_CAST_PTR(cur_ptr, 0, guint8 *), 18); /* First 18 bytes are common */
        cur_ptr += 18;
        
        /* Handle big-endianess */
        block->sector_size = GUINT16_FROM_BE(block->sector_size);
                
        if (_priv->old_format) {
            gint32 *tmp_int = MIRAGE_CAST_PTR(cur_ptr, 0, gint32 *);
            cur_ptr += 3 * sizeof(gint32);
            /* Conversion */
            block->pregap_offset = GINT32_FROM_BE(tmp_int[0]);
            block->start_offset = GINT32_FROM_BE(tmp_int[1]);
            block->end_offset = GINT32_FROM_BE(tmp_int[2]);            
        } else {
            gint64 *tmp_int = MIRAGE_CAST_PTR(cur_ptr, 0, gint64 *);
            cur_ptr += 3 * sizeof(gint64);
            /* Conversion */
            block->pregap_offset = GINT64_FROM_BE(tmp_int[0]);
            block->start_offset = GINT64_FROM_BE(tmp_int[1]);
            block->end_offset = GINT64_FROM_BE(tmp_int[2]);
        }
        
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  ISRC: %.12s\n", __func__, block->isrc);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  mode code: 0x%X\n", __func__, block->mode_code);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  sector size: 0x%X\n", __func__, block->sector_size);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  pregap offset: 0x%llX\n", __func__, block->pregap_offset);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  start offset: 0x%llX\n", __func__, block->start_offset);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  end offset: 0x%llX\n", __func__, block->end_offset);
    }
    
    return TRUE;
}

static gboolean __mirage_parser_nrg_load_session (MIRAGE_Parser *self, gint session_num, GError **error) {
    MIRAGE_Parser_NRGPrivate *_priv = MIRAGE_PARSER_NRG_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: loading session\n", __func__);
    
    /* Read CUEX/CUES blocks */
    if (!__mirage_parser_nrg_load_cue_data(self, session_num, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to load CUE blocks!\n", __func__);
        succeeded = FALSE;
        goto end;
    }
    
    /* Read DAOX/DAOI blocks */
    if (!__mirage_parser_nrg_load_dao_data(self, session_num, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to load DAO blocks!\n", __func__);
        succeeded = FALSE;
        goto end;
    }
    
    /* Set parser's MCN */
    if (_priv->dao_header->mcn) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: MCN: %.13s\n", __func__, _priv->dao_header->mcn);
        mirage_disc_set_mcn(MIRAGE_DISC(_priv->disc), _priv->dao_header->mcn, NULL);
    } 
    
    /* Build session */
    GObject *cur_session = NULL;
    gint i;
    
    if (!mirage_disc_add_session_by_index(MIRAGE_DISC(_priv->disc), -1, &cur_session, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __func__);
        succeeded = FALSE;
        goto end;
    }
    
    /* Use DAO blocks to build tracks */
    for (i = 0; i < _priv->num_dao_blocks; i++) {
        NRG_DAO_Block *dao_block = _priv->dao_blocks + i;
        
        GObject *cur_track = NULL;        
        gint mode = 0;
        gint main_sectsize = 0;
        gint sub_sectsize = 0;
        
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating track for DAO block #%i\n", __func__, i);
        /* Add track */
        if (!mirage_session_add_track_by_index(MIRAGE_SESSION(cur_session), i, &cur_track, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __func__);
            g_object_unref(cur_session);
            succeeded = FALSE;
            goto end;
        }
        
        /* Decode mode */
        __mirage_parser_nrg_decode_mode(self, dao_block->mode_code, &mode, &main_sectsize, &sub_sectsize, NULL);
        mirage_track_set_mode(MIRAGE_TRACK(cur_track), mode, NULL);
        
        /* Shouldn't happen, but just in case I misinterpreted something */
        if (main_sectsize + sub_sectsize != dao_block->sector_size) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: sector size mismatch (%i vs %i)!\n", __func__, main_sectsize + sub_sectsize, dao_block->sector_size);
        }
        
        /* Prepare data fragment: we use two fragments, one for pregap and one
           for track itself; we could use only one that spans across both, but
           I'm not sure why image file has the offsets separated - maybe they
           don't have to be adjacent?
        */
        FILE *tfile_handle = NULL;
        gint tfile_sectsize = 0;
        guint64 tfile_offset = 0;
        guint64 tfile_format = 0;
        
        gint sfile_sectsize = 0;
        guint64 sfile_format = 0;
        
        gint fragment_len = 0;
        
        GObject *data_fragment = NULL;
       
        /* Pregap fragment */
        fragment_len = (dao_block->start_offset - dao_block->pregap_offset) / dao_block->sector_size;
        if (fragment_len) {
            /* Create a binary fragment */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating pregap fragment\n", __func__);

            data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FINTERFACE_BINARY, _priv->nrg_filename, error);
            if (!data_fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create fragment!\n", __func__);
                succeeded = FALSE;
                g_object_unref(cur_track);
                g_object_unref(cur_session);
                goto end;
            }
            
            /* Main channel data */
            tfile_handle = g_fopen(_priv->nrg_filename, "r");
            tfile_sectsize = main_sectsize; /* We use the one from decoded mode code */
            tfile_offset = dao_block->pregap_offset;
            if (mode == MIRAGE_MODE_AUDIO) {
                tfile_format = FR_BIN_TFILE_AUDIO;
            } else {
                tfile_format = FR_BIN_TFILE_DATA;
            }
            
            /* Subchannel */
            if (sub_sectsize) {
                sfile_sectsize = sub_sectsize; /* We use the one from decoded mode code */
                sfile_format = FR_BIN_SFILE_PW96_INT | FR_BIN_SFILE_INT; /* PW96 interleaved, internal */
            }
                        
            mirage_fragment_set_length(MIRAGE_FRAGMENT(data_fragment), fragment_len, NULL);
                
            mirage_finterface_binary_track_file_set_handle(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_handle, NULL);
            mirage_finterface_binary_track_file_set_offset(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_offset, NULL);
            mirage_finterface_binary_track_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_sectsize, NULL);
            mirage_finterface_binary_track_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_format, NULL);
            
            mirage_finterface_binary_subchannel_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), sfile_sectsize, NULL);
            mirage_finterface_binary_subchannel_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), sfile_format, NULL);
            
            mirage_track_add_fragment(MIRAGE_TRACK(cur_track), -1, &data_fragment, NULL);
            
            g_object_unref(data_fragment);
        }
        
        /* Data fragment */
        fragment_len = (dao_block->end_offset - dao_block->start_offset) / dao_block->sector_size;
        if (fragment_len) {
            /* Create a binary fragment */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating data fragment\n", __func__);

            data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FINTERFACE_BINARY, _priv->nrg_filename, error);
            if (!data_fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create fragment!\n", __func__);
                succeeded = FALSE;
                g_object_unref(cur_track);
                g_object_unref(cur_session);
                goto end;
            }
            
            /* Main channel data */
            tfile_handle = g_fopen(_priv->nrg_filename, "r");
            tfile_sectsize = main_sectsize; /* We use the one from decoded mode code */
            tfile_offset = dao_block->start_offset;
            if (mode == MIRAGE_MODE_AUDIO) {
                tfile_format = FR_BIN_TFILE_AUDIO;
            } else {
                tfile_format = FR_BIN_TFILE_DATA;
            }
            
            /* Subchannel */
            if (sub_sectsize) {
                sfile_sectsize = sub_sectsize; /* We use the one from decoded mode code */
                sfile_format = FR_BIN_SFILE_PW96_INT | FR_BIN_SFILE_INT; /* PW96 interleaved, internal */
            }
            
            mirage_fragment_set_length(MIRAGE_FRAGMENT(data_fragment), fragment_len, NULL);
                
            mirage_finterface_binary_track_file_set_handle(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_handle, NULL);
            mirage_finterface_binary_track_file_set_offset(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_offset, NULL);
            mirage_finterface_binary_track_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_sectsize, NULL);
            mirage_finterface_binary_track_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_format, NULL);
            
            mirage_finterface_binary_subchannel_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), sfile_sectsize, NULL);
            mirage_finterface_binary_subchannel_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), sfile_format, NULL);
            
            mirage_track_add_fragment(MIRAGE_TRACK(cur_track), -1, &data_fragment, NULL);
            
            g_object_unref(data_fragment);
        }

        /* Set ISRC */
        if (dao_block->isrc) {
            mirage_track_set_isrc(MIRAGE_TRACK(cur_track), dao_block->isrc, NULL);
        }
        
        g_object_unref(cur_track);
    }
    
    /* Use CUE blocks to set pregaps and indices */
    gint track_start = 0;
    for (i = 0; i < _priv->num_cue_blocks; i++) {
        NRG_CUE_Block *cue_block = _priv->cue_blocks + i;
                
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track %i, index %i, sector 0x%X\n", __func__, cue_block->track, cue_block->index, cue_block->start_sector);
        
        /* Track 0, index 0... lead-in */
        if (cue_block->track == 0 && cue_block->index == 0) {
            /* Try to get previous session; if we fail, it means this if first
               session, so we use its lead-in block to set parser start; otherwise,
               we calculate length of previous session's leadout based on this
               session's start sector and length of parser so far */
            GObject *prev_session = NULL;

            /* Index is -1, because current session has already been added */
            if (!mirage_disc_get_session_by_index(MIRAGE_DISC(_priv->disc), -2, &prev_session, NULL)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: lead-in block of first session; setting parser start to 0x%X\n", __func__, cue_block->start_sector);
                mirage_disc_layout_set_start_sector(MIRAGE_DISC(_priv->disc), cue_block->start_sector, NULL);
            } else {
                gint leadout_length = 0;
                
                leadout_length = cue_block->start_sector - _priv->prev_session_end;
                
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: lead-in block; setting previous session's leadout length to 0x%X\n", __func__, leadout_length);
                
                mirage_session_set_leadout_length(MIRAGE_SESSION(prev_session), leadout_length, NULL);
                
                g_object_unref(prev_session);
            }
        } else if (cue_block->track == 170) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: lead-out block\n", __func__);
            _priv->prev_session_end = cue_block->start_sector;
        } else {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track entry\n", __func__);
            if (cue_block->index == 0) {
                track_start = cue_block->start_sector;
            } else {
                GObject *track = NULL;
                gint cur_address = cue_block->start_sector - track_start;
                
                mirage_session_get_track_by_number(MIRAGE_SESSION(cur_session), cue_block->track, &track, NULL);
                
                if (cue_block->index == 1) {
                    /* Track start */
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: setting index 1 to address 0x%X\n", __func__, cur_address);
                    mirage_track_set_track_start(MIRAGE_TRACK(track), cur_address, NULL);
                } else {
                    /* Additional index */
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding index at address 0x%X\n", __func__, cur_address);
                    mirage_track_add_index(MIRAGE_TRACK(track), cur_address, NULL, NULL);
                }
                
                g_object_unref(track);
            }
        }
    }
    
    g_object_unref(cur_session);
    
end:
    /* Free data */
    g_free(_priv->cue_blocks);
    _priv->num_cue_blocks = 0;
    g_free(_priv->dao_header);
    g_free(_priv->dao_blocks);
    _priv->num_dao_blocks = 0;
    
    return succeeded;
}

static gboolean __mirage_parser_nrg_load_session_tao (MIRAGE_Parser *self, gint session_num, GError **error) {
    MIRAGE_Parser_NRGPrivate *_priv = MIRAGE_PARSER_NRG_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: loading session\n", __func__);
    
    /* Read ETNF/ETN2 blocks */
    if (!__mirage_parser_nrg_load_etn_data(self, session_num, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to load ETNF/ETN2 blocks!\n", __func__);
        succeeded = FALSE;
        goto end;
    }
    
    /* Build session */
    GObject *cur_session = NULL;
    gint i;
    
    if (!mirage_disc_add_session_by_index(MIRAGE_DISC(_priv->disc), -1, &cur_session, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __func__);
        succeeded = FALSE;
        goto end;
    }
    
    /* Use ETN blocks to build tracks */
    for (i = 0; i < _priv->num_etn_blocks; i++) {
        NRG_ETN_Block *etn_block = _priv->etn_blocks + i;
        
        GObject *cur_track = NULL;        
        gint mode = 0;
        gint main_sectsize = 0;
        gint sub_sectsize = 0;
        
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating track for ETN block #%i\n", __func__, i);
        /* Add track */
        if (!mirage_session_add_track_by_index(MIRAGE_SESSION(cur_session), i, &cur_track, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __func__);
            g_object_unref(cur_session);
            succeeded = FALSE;
            goto end;
        }
        
        /* Decode mode */
        __mirage_parser_nrg_decode_mode(self, etn_block->mode, &mode, &main_sectsize, &sub_sectsize, NULL);
        mirage_track_set_mode(MIRAGE_TRACK(cur_track), mode, NULL);
        
        /* Prepare data fragment: we use two fragments, one for pregap and one
           for track itself; we could use only one that spans across both, but
           I'm not sure why image file has the offsets separated - maybe they
           don't have to be adjacent?
        */
        FILE *tfile_handle = NULL;
        gint tfile_sectsize = 0;
        guint64 tfile_offset = 0;
        guint64 tfile_format = 0;
        
        gint sfile_sectsize = 0;
        guint64 sfile_format = 0;
        
        gint fragment_len = 0;
        
        GObject *data_fragment = NULL;
        GObject *pregap_fragment = NULL;

        /* Pregap fragment */
        pregap_fragment = libmirage_create_fragment(MIRAGE_TYPE_FINTERFACE_NULL, "NULL", error);
        if (!pregap_fragment) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to create pregap fragment!\n", __func__);
            g_object_unref(cur_session);
            g_object_unref(cur_track);
            succeeded = FALSE;
            goto end;
        }
        mirage_track_add_fragment(MIRAGE_TRACK(cur_track), 0, &pregap_fragment, NULL);
        mirage_fragment_set_length(MIRAGE_FRAGMENT(pregap_fragment), 150, NULL);

        g_object_unref(pregap_fragment);

        mirage_track_set_track_start(MIRAGE_TRACK(cur_track), 150, NULL);

        /* Data fragment */
        fragment_len = etn_block->size / main_sectsize;
        if (fragment_len) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating data fragment\n", __func__);

            data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FINTERFACE_BINARY, _priv->nrg_filename, error);
            if (!data_fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create fragment!\n", __func__);
                g_object_unref(cur_track);
                g_object_unref(cur_session);
                succeeded = FALSE;
                goto end;
            }
            
            /* Main channel data */
            tfile_handle = g_fopen(_priv->nrg_filename, "r");
            tfile_sectsize = main_sectsize; /* We use the one from decoded mode code */
            tfile_offset = etn_block->offset;
            if (mode == MIRAGE_MODE_AUDIO) {
                tfile_format = FR_BIN_TFILE_AUDIO;
            } else {
                tfile_format = FR_BIN_TFILE_DATA;
            }
            
            /* Subchannel */
            if (sub_sectsize) {
                sfile_sectsize = sub_sectsize; /* We use the one from decoded mode code */
                sfile_format = FR_BIN_SFILE_PW96_INT | FR_BIN_SFILE_INT; /* PW96 interleaved, internal */
            }
            
            mirage_fragment_set_length(MIRAGE_FRAGMENT(data_fragment), fragment_len, NULL);
                
            mirage_finterface_binary_track_file_set_handle(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_handle, NULL);
            mirage_finterface_binary_track_file_set_offset(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_offset, NULL);
            mirage_finterface_binary_track_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_sectsize, NULL);
            mirage_finterface_binary_track_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_format, NULL);
            
            mirage_finterface_binary_subchannel_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), sfile_sectsize, NULL);
            mirage_finterface_binary_subchannel_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), sfile_format, NULL);
            
            mirage_track_add_fragment(MIRAGE_TRACK(cur_track), -1, &data_fragment, NULL);
            
            g_object_unref(data_fragment);
        }

        g_object_unref(cur_track);
    }

    g_object_unref(cur_session);
    
end:
    /* Free data */
    g_free(_priv->etn_blocks);
    _priv->num_etn_blocks = 0;
    
    return succeeded;
}

static gboolean __mirage_parser_nrg_load_cdtext (MIRAGE_Parser *self, GError **error) {
    MIRAGE_Parser_NRGPrivate *_priv = MIRAGE_PARSER_NRG_GET_PRIVATE(self);
    NRGBlockIndexEntry *blockentry;
    guint8 *cur_ptr;
    guint8 *cdtx_data;
    GObject *session;   
    gboolean succeeded = TRUE;

    /* Look up CDTX block */
    blockentry = __mirage_parser_nrg_find_block_entry(self, "CDTX", 0);
    if (!blockentry) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to look up 'CDTX' block!\n", __func__);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }
    cur_ptr = _priv->nrg_data + blockentry->offset + 8;
    
    /* Read CDTX data */
    cdtx_data = cur_ptr;

    if (mirage_disc_get_session_by_index(MIRAGE_DISC(_priv->disc), 0, &session, error)) {
        if (!mirage_session_set_cdtext_data(MIRAGE_SESSION(session), cdtx_data, blockentry->length, error)) {
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


/******************************************************************************\
 *                     MIRAGE_Parser methods implementation                     *
\******************************************************************************/
static gboolean __mirage_parser_nrg_load_image (MIRAGE_Parser *self, gchar **filenames, GObject **disc, GError **error) {
    MIRAGE_Parser_NRGPrivate *_priv = MIRAGE_PARSER_NRG_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    FILE *file;
    guint64 filesize;
    guint64 trailer_offset;
    gchar sig[4];

    /* Open file */
    file = g_fopen(filenames[0], "r");
    if (!file) {
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }
    
    /* Get filesize */
    fseeko(file, 0, SEEK_END);
    filesize = ftello(file);
    
    /* Read signature */
    fseeko(file, -12, SEEK_END);
    
    if (fread(sig, 4, 1, file) < 1) {
        mirage_error(MIRAGE_E_READFAILED, error);
        fclose(file);
        return FALSE;
    }
    
    if (!memcmp(sig, "NER5", 4)) {
        /* New format, 64-bit offset */
        guint64 tmp_offset = 0;
        _priv->old_format = FALSE;
        
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: new format\n", __func__);
        
        if (fread(&tmp_offset, 8, 1, file) < 1) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read trailer offset!\n", __func__);
            fclose(file);
            mirage_error(MIRAGE_E_READFAILED, error);
            return FALSE;
        }
        trailer_offset = GUINT64_FROM_BE(tmp_offset);
        _priv->nrg_data_length = (filesize - 12) - trailer_offset;
    } else {
        /* Try old format, with 32-bit offset */
        fseeko(file, -8, SEEK_END);
        if (fread(sig, 4, 1, file) < 1) {
            mirage_error(MIRAGE_E_READFAILED, error);
            fclose(file);
            return FALSE;
        }
        
        if (!memcmp(sig, "NERO", 4)) {
            guint32 tmp_offset = 0;
            _priv->old_format = TRUE;
            
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: old format\n", __func__);
            
            if (fread(&tmp_offset, 4, 1, file) < 1) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read trailer offset!\n", __func__);
                fclose(file);
                mirage_error(MIRAGE_E_READFAILED, error);
                return FALSE;
            }
            
            trailer_offset = GUINT32_FROM_BE(tmp_offset);
            _priv->nrg_data_length = (filesize - 8) - trailer_offset;
        } else {
            /* Unknown signature, can't handle the file */
            fclose(file);
            mirage_error(MIRAGE_E_CANTHANDLE, error);
            return FALSE;
        }
    }
    
    /* Create disc */
    _priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_attach_child(MIRAGE_OBJECT(self), _priv->disc, NULL);

    mirage_disc_set_filename(MIRAGE_DISC(_priv->disc), filenames, NULL);
    _priv->nrg_filename = g_strdup(filenames[0]);

    /* Set CD-ROM as default medium type, will be changed accordingly if there
       is a MTYP block provided */
    mirage_disc_set_medium_type(MIRAGE_DISC(_priv->disc), MIRAGE_MEDIUM_CD, NULL);
    
    /* Read descriptor data */
    _priv->nrg_data = g_malloc(_priv->nrg_data_length);
    fseeko(file, trailer_offset, SEEK_SET);
    if (fread(_priv->nrg_data, _priv->nrg_data_length, 1, file) < 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read descriptor!\n", __func__);
        mirage_error(MIRAGE_E_READFAILED, error);
        succeeded = FALSE;
        goto end;
    }

    /* Build an index over blocks contained in the parser image */
    if(!__mirage_parser_nrg_build_block_index(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to build block index!\n", __func__);
        succeeded = FALSE;
        goto end;
    }

    /* Load parser sessions */
    gint session_num = 0;

    for (session_num = 0; ; session_num++) {
        if (__mirage_parser_nrg_find_block_entry(self, "CUEX", session_num) || __mirage_parser_nrg_find_block_entry(self, "CUES", session_num)) {
            /* CUEX/CUES block: means we need to make new session */
            if (!__mirage_parser_nrg_load_session(self, session_num, NULL)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to load session!\n", __func__);
                mirage_error(MIRAGE_E_PARSER, error);
                succeeded = FALSE;
                goto end;
            }
        } else if (__mirage_parser_nrg_find_block_entry(self, "ETN2", session_num) || __mirage_parser_nrg_find_block_entry(self, "ETNF", session_num)) {
            /* ETNF/ETN2 block: means we need to make new session */
            if (!__mirage_parser_nrg_load_session_tao(self, session_num, NULL)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to load session!\n", __func__);
                mirage_error(MIRAGE_E_PARSER, error);
                succeeded = FALSE;
                goto end;
            }
        } else {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: loaded a total of %i sessions.\n", __func__, session_num);
            break;
        }
    }

    /* Load CD text, medium type etc. */
    if (__mirage_parser_nrg_find_block_entry(self, "CDTX", 0)) {
        if (!__mirage_parser_nrg_load_cdtext(self, error)) {
            succeeded = FALSE;
            goto end;
        }
    } 

    if (__mirage_parser_nrg_find_block_entry(self, "MTYP", 0)) {
        /* MTYP: medium type */            
        if (!__mirage_parser_nrg_load_medium_type(self, error)) {
            succeeded = FALSE;
            goto end;
        }
    }

end:
    /* Destroy block index */
    __mirage_parser_nrg_destroy_block_index(self);

    g_free(_priv->nrg_data);
    fclose(file);
    
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

static void __mirage_parser_nrg_instance_init (GTypeInstance *instance, gpointer g_class) {
    mirage_parser_generate_parser_info(MIRAGE_PARSER(instance),
        "PARSER-NRG",
        "NRG Image Parser",
        PACKAGE_VERSION,
        "Rok Mandeljc",
        FALSE,
        "NRG (Nero Burning Rom) images",
        2, ".nrg", NULL
    );
    
    return;
}

static void __mirage_parser_nrg_finalize (GObject *obj) {
    MIRAGE_Parser_NRG *self = MIRAGE_PARSER_NRG(obj);
    MIRAGE_Parser_NRGPrivate *_priv = MIRAGE_PARSER_NRG_GET_PRIVATE(self);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s:\n", __func__);
    
    g_free(_priv->nrg_filename);
    
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __func__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}


static void __mirage_parser_nrg_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_ParserClass *class_parser = MIRAGE_PARSER_CLASS(g_class);
    MIRAGE_Parser_NRGClass *klass = MIRAGE_PARSER_NRG_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Parser_NRGPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_parser_nrg_finalize;
    
    /* Initialize MIRAGE_Parser methods */
    class_parser->load_image = __mirage_parser_nrg_load_image;
        
    return;
}

GType mirage_parser_nrg_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Parser_NRGClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_parser_nrg_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Parser_NRG),
            0,      /* n_preallocs */
            __mirage_parser_nrg_instance_init    /* instance_init */
        };
        
        type = g_type_module_register_type(module, MIRAGE_TYPE_PARSER, "MIRAGE_Parser_NRG", &info, 0);
    }
    
    return type;
}
