/*
 *  libMirage: NRG image parser: Disc object
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
#define MIRAGE_DISC_NRG_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_DISC_NRG, MIRAGE_Disc_NRGPrivate))

/* Function prototypes */
static gboolean __mirage_disc_nrg_load_medium_type (MIRAGE_Disc *self, GError **error);
static gboolean __mirage_disc_nrg_decode_mode (MIRAGE_Disc *self, gint code, gint *mode, gint *main_sectsize, gint *sub_sectsize, GError **error);

static gboolean __mirage_disc_nrg_load_etn_data (MIRAGE_Disc *self, GError **error);
static gboolean __mirage_disc_nrg_load_cue_data (MIRAGE_Disc *self, GError **error);
static gboolean __mirage_disc_nrg_load_dao_data (MIRAGE_Disc *self, GError **error);

static gboolean __mirage_disc_nrg_load_session (MIRAGE_Disc *self, GError **error);
static gboolean __mirage_disc_nrg_load_session_tao (MIRAGE_Disc *self, GError **error);

static gboolean __mirage_disc_nrg_load_cdtext (MIRAGE_Disc *self, GError **error);

static NRGBlockIndexEntry *__mirage_disc_nrg_find_block_entry(MIRAGE_Disc *self, gchar *block_id, GError **error);
static gboolean __mirage_disc_nrg_build_block_index(MIRAGE_Disc *self, GError **error);
static gboolean __mirage_disc_nrg_destroy_block_index(MIRAGE_Disc *self, GError **error);


typedef struct {
    NRGBlockIndexEntry *block_index;
    gint               block_index_entries;

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

    /* Parser info */
    MIRAGE_ParserInfo *parser_info;
} MIRAGE_Disc_NRGPrivate;

typedef struct {
    gchar     *block_id;
    gint      subblock_offset;
    gint      subblock_length;
} NRG_BlockIDs;


static gboolean __mirage_disc_nrg_load_medium_type (MIRAGE_Disc *self, GError **error) {
    MIRAGE_Disc_NRGPrivate *_priv = MIRAGE_DISC_NRG_GET_PRIVATE(self);
    NRGBlockIndexEntry     *blockentry = NULL;
    guint8                 *cur_ptr = NULL;
    guint32                mtyp_data = 0;

    /* Look up MTYP block */
    blockentry = __mirage_disc_nrg_find_block_entry(self, "MTYP", error);
    if (!blockentry) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to look up 'MTYP' block!\n", __func__);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }
    cur_ptr = _priv->nrg_data + blockentry->offset;
    cur_ptr += 8;

    mtyp_data = MIRAGE_CAST_DATA(cur_ptr, 0, guint32);
    cur_ptr += sizeof(guint32);
    mtyp_data = GINT32_FROM_BE(mtyp_data);
    
    /* Decode medium type */
    switch (mtyp_data) {
        case 0x01: {
            /* CD-ROM */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: medium type: CD-ROM\n", __func__);
            mirage_disc_set_medium_type(self, MIRAGE_MEDIUM_CD, NULL);
            break;
        }
        case 0x1C: {
            /* DVD-ROM */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: medium type: DVD-ROM\n", __func__);
            mirage_disc_set_medium_type(self, MIRAGE_MEDIUM_DVD, NULL);
            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unhandled medium type: %d!\n", __func__, mtyp_data);
        }
    }
    
    return TRUE;
}

static gboolean __mirage_disc_nrg_decode_mode (MIRAGE_Disc *self, gint code, gint *mode, gint *main_sectsize, gint *sub_sectsize, GError **error) {    
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

static gboolean __mirage_disc_nrg_load_etn_data (MIRAGE_Disc *self, GError **error) {
    MIRAGE_Disc_NRGPrivate *_priv = MIRAGE_DISC_NRG_GET_PRIVATE(self);
    NRGBlockIndexEntry     *blockentry = NULL;
    guint8                 *cur_ptr = NULL;

    /* Look up ETN2 / ETNF block */
    if(!_priv->old_format) {
        blockentry = __mirage_disc_nrg_find_block_entry(self, "ETN2", error);
    } else {
        blockentry = __mirage_disc_nrg_find_block_entry(self, "ETNF", error);
    }
    if (!blockentry) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to look up 'ETN2' or 'ETNF' block!\n", __func__);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }
    cur_ptr = _priv->nrg_data + blockentry->offset;
    cur_ptr += 8;

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
            block->size   = GINT32_FROM_BE(MIRAGE_CAST_DATA(cur_ptr, 4, guint32));
            block->mode   = MIRAGE_CAST_DATA(cur_ptr, 11, guint8);
            block->sector = GINT32_FROM_BE(MIRAGE_CAST_DATA(cur_ptr, 12, guint32));
        }
        else {
            block->offset = GINT64_FROM_BE(MIRAGE_CAST_DATA(cur_ptr, 0, guint64));
            block->size   = GINT64_FROM_BE(MIRAGE_CAST_DATA(cur_ptr, 8, guint64));
            block->mode   = MIRAGE_CAST_DATA(cur_ptr, 19, guint8);
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

static gboolean __mirage_disc_nrg_load_cue_data (MIRAGE_Disc *self, GError **error) {
    MIRAGE_Disc_NRGPrivate *_priv = MIRAGE_DISC_NRG_GET_PRIVATE(self);
    NRGBlockIndexEntry     *blockentry = NULL;
    guint8                 *cur_ptr = NULL;

    /* Look up CUEX / CUES block */
    if(!_priv->old_format) {
        blockentry = __mirage_disc_nrg_find_block_entry(self, "CUEX", error);
    } else {
        blockentry = __mirage_disc_nrg_find_block_entry(self, "CUES", error);
    }
    if (!blockentry) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to look up 'CUEX' or 'CUES' block!\n", __func__);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }
    cur_ptr = _priv->nrg_data + blockentry->offset;
    cur_ptr += 8;

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

static gboolean __mirage_disc_nrg_load_dao_data (MIRAGE_Disc *self, GError **error) {
    MIRAGE_Disc_NRGPrivate *_priv = MIRAGE_DISC_NRG_GET_PRIVATE(self);
    NRGBlockIndexEntry     *blockentry = NULL;
    guint8                 *cur_ptr = NULL;

    /* Look up DAOX / DAOI block */
    if(!_priv->old_format) {
        blockentry = __mirage_disc_nrg_find_block_entry(self, "DAOX", error);
    } else {
        blockentry = __mirage_disc_nrg_find_block_entry(self, "DAOI", error);
    }
    if (!blockentry) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to look up 'DAOX' or 'DAOI' block!\n", __func__);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }
    cur_ptr = _priv->nrg_data + blockentry->offset;
    cur_ptr += 8;

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

static gboolean __mirage_disc_nrg_load_session (MIRAGE_Disc *self, GError **error) {
    MIRAGE_Disc_NRGPrivate *_priv = MIRAGE_DISC_NRG_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: loading session\n", __func__);
    
    /* Read CUEX/CUES blocks */
    if (!__mirage_disc_nrg_load_cue_data(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to load CUE blocks!\n", __func__);
        succeeded = FALSE;
        goto end;
    }
    
    /* Read DAOX/DAOI blocks */
    if (!__mirage_disc_nrg_load_dao_data(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to load DAO blocks!\n", __func__);
        succeeded = FALSE;
        goto end;
    }
    
    /* Set disc's MCN */
    if (_priv->dao_header->mcn) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: MCN: %.13s\n", __func__, _priv->dao_header->mcn);
        mirage_disc_set_mcn(self, _priv->dao_header->mcn, NULL);
    } 
    
    /* Build session */
    GObject *cur_session = NULL;
    gint i;
    
    if (!mirage_disc_add_session_by_index(self, -1, &cur_session, error)) {
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
        __mirage_disc_nrg_decode_mode(self, dao_block->mode_code, &mode, &main_sectsize, &sub_sectsize, NULL);
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

        gchar **filenames = NULL;
        mirage_disc_get_filenames(self, &filenames, NULL);
        
        /* Pregap fragment */
        fragment_len = (dao_block->start_offset - dao_block->pregap_offset) / dao_block->sector_size;
        if (fragment_len) {
            /* Get Mirage and have it make us binary fragment */
            GObject *mirage = NULL;

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating pregap fragment\n", __func__);

            if (!mirage_object_get_mirage(MIRAGE_OBJECT(self), &mirage, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get Mirage object!\n", __func__);
                succeeded = FALSE;
                g_object_unref(cur_track);
                g_object_unref(cur_session);
                goto end;
            }

            mirage_mirage_create_fragment(MIRAGE_MIRAGE(mirage), MIRAGE_TYPE_FINTERFACE_BINARY, filenames[0], &data_fragment, error);
            g_object_unref(mirage);
            if (!data_fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create fragment!\n", __func__);
                succeeded = FALSE;
                g_object_unref(cur_track);
                g_object_unref(cur_session);
                goto end;
            }
            
            /* Main channel data */
            tfile_handle = g_fopen(filenames[0], "r");
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
            /* Get Mirage and have it make us binary fragment */
            GObject *mirage = NULL;
            
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating data fragment\n", __func__);

            if (!mirage_object_get_mirage(MIRAGE_OBJECT(self), &mirage, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get Mirage object!\n", __func__);
                succeeded = FALSE;
                g_object_unref(cur_track);
                g_object_unref(cur_session);
                goto end;
            }

            mirage_mirage_create_fragment(MIRAGE_MIRAGE(mirage), MIRAGE_TYPE_FINTERFACE_BINARY, filenames[0], &data_fragment, error);
            g_object_unref(mirage);
            if (!data_fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create fragment!\n", __func__);
                succeeded = FALSE;
                g_object_unref(cur_track);
                g_object_unref(cur_session);
                goto end;
            }
            
            /* Main channel data */
            tfile_handle = g_fopen(filenames[0], "r");
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

        g_strfreev(filenames);
        
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
               session, so we use its lead-in block to set disc start; otherwise,
               we calculate length of previous session's leadout based on this
               session's start sector and length of disc so far */
            GObject *prev_session = NULL;

            /* Index is -1, because current session has already been added */
            if (!mirage_disc_get_session_by_index(self, -2, &prev_session, NULL)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: lead-in block of first session; setting disc start to 0x%X\n", __func__, cue_block->start_sector);
                mirage_disc_layout_set_start_sector(self, cue_block->start_sector, NULL);
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

static gboolean __mirage_disc_nrg_load_session_tao (MIRAGE_Disc *self, GError **error) {
    MIRAGE_Disc_NRGPrivate *_priv = MIRAGE_DISC_NRG_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: loading session\n", __func__);
    
    /* Read ETNF/ETN2 blocks */
    if (!__mirage_disc_nrg_load_etn_data(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to load ETNF/ETN2 blocks!\n", __func__);
        succeeded = FALSE;
        goto end_tao;
    }
    
    /* Build session */
    GObject *cur_session = NULL;
    gint i;
    
    if (!mirage_disc_add_session_by_index(self, -1, &cur_session, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __func__);
        succeeded = FALSE;
        goto end_tao;
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
            goto end_tao;
        }
        
        /* Decode mode */
        __mirage_disc_nrg_decode_mode(self, etn_block->mode, &mode, &main_sectsize, &sub_sectsize, NULL);
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

        gchar **filenames = NULL;
        mirage_disc_get_filenames(self, &filenames, NULL);

        /* Create mirage object */
        GObject *mirage = NULL;

        if (!mirage_object_get_mirage(MIRAGE_OBJECT(self), &mirage, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get Mirage object!\n", __func__);
            succeeded = FALSE;
            g_object_unref(cur_track);
            g_object_unref(cur_session);
            goto end_tao;
        }

        /* Pregap fragment */
        mirage_mirage_create_fragment(MIRAGE_MIRAGE(mirage), MIRAGE_TYPE_FINTERFACE_NULL, "NULL", &pregap_fragment, error);

        if (!pregap_fragment) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to create pregap fragment!\n", __func__);
            g_object_unref(cur_session);
            g_object_unref(cur_track);
            goto end_tao;
        }
        mirage_track_add_fragment(MIRAGE_TRACK(cur_track), 0, &pregap_fragment, NULL);
        mirage_fragment_set_length(MIRAGE_FRAGMENT(pregap_fragment), 150, NULL);

        g_object_unref(pregap_fragment);

        mirage_track_set_track_start(MIRAGE_TRACK(cur_track), 150, NULL);

        /* Data fragment */
        fragment_len = etn_block->size / main_sectsize;
        if (fragment_len) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating data fragment\n", __func__);

            mirage_mirage_create_fragment(MIRAGE_MIRAGE(mirage), MIRAGE_TYPE_FINTERFACE_BINARY, filenames[0], &data_fragment, error);
            g_object_unref(mirage);
            if (!data_fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create fragment!\n", __func__);
                succeeded = FALSE;
                g_object_unref(cur_track);
                g_object_unref(cur_session);
                goto end_tao;
            }
            
            /* Main channel data */
            tfile_handle = g_fopen(filenames[0], "r");
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

        g_object_unref(mirage);

        g_strfreev(filenames);
        
        g_object_unref(cur_track);
    }

    g_object_unref(cur_session);
    
end_tao:
    /* Free data */
    g_free(_priv->etn_blocks);
    _priv->num_etn_blocks = 0;
    
    return succeeded;
}

static gboolean __mirage_disc_nrg_load_cdtext (MIRAGE_Disc *self, GError **error) {
    MIRAGE_Disc_NRGPrivate *_priv = MIRAGE_DISC_NRG_GET_PRIVATE(self);
    NRGBlockIndexEntry     *blockentry = NULL;
    guint8                 *cur_ptr = NULL;
    guint8                 *cdtx_data = NULL;
    GObject                *session = NULL;   
    gboolean               succeeded = TRUE;

    /* Look up CDTX block */
    blockentry = __mirage_disc_nrg_find_block_entry(self, "CDTX", error);
    if (!blockentry) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to look up 'CDTX' block!\n", __func__);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }
    cur_ptr = _priv->nrg_data + blockentry->offset;
    cur_ptr += 8;
    
    /* Read CDTX data */
    cdtx_data = cur_ptr;

    if (mirage_disc_get_session_by_index(self, 0, &session, error)) {
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

static NRGBlockIndexEntry *__mirage_disc_nrg_find_block_entry(MIRAGE_Disc *self, gchar *block_id, GError **error) {
    MIRAGE_Disc_NRGPrivate *_priv = MIRAGE_DISC_NRG_GET_PRIVATE(self);
    NRGBlockIndexEntry *block = NULL;
    gint index = 0;

    for(index = 0; index < _priv->block_index_entries; index++) {
        block = &_priv->block_index[index];
        if(!memcmp(block_id, block->block_id, 4)) {
            return block;
        }
    }

    return NULL;
}

static gboolean __mirage_disc_nrg_build_block_index(MIRAGE_Disc *self, GError **error) {
    MIRAGE_Disc_NRGPrivate *_priv = MIRAGE_DISC_NRG_GET_PRIVATE(self);
    NRGBlockIndexEntry     *blockindex = NULL, *blockentry = NULL;
    gint                   num_blocks = 0, index = 0, success = TRUE;
    guint8                 *cur_ptr = NULL;

    /* Determine number of blocks in image */
    num_blocks = 0;
    cur_ptr = _priv->nrg_data;
    do {
        guint32 block_length;
        cur_ptr += 4;
        block_length = MIRAGE_CAST_DATA(cur_ptr, 0, guint32);
        block_length = GINT32_FROM_BE(block_length);
        cur_ptr += sizeof(guint32);
        cur_ptr += block_length;        
        num_blocks++;
    } while(cur_ptr < _priv->nrg_data + _priv->nrg_data_length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Counted %i blocks.\n", __func__, num_blocks);

    /* Create block index and link to it */
    blockindex = g_new(NRGBlockIndexEntry, num_blocks);
    if (!blockindex) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to allocate memory for block index!\n", __func__);
        return FALSE;
    }
    _priv->block_index = blockindex;
    _priv->block_index_entries = num_blocks;

    /* Populate block index */
    cur_ptr = _priv->nrg_data;
    for (index = 0; index < num_blocks; index++) {
        blockentry = &blockindex[index];
        blockentry->offset = (guint64) (cur_ptr - _priv->nrg_data);
        memcpy(blockentry->block_id, cur_ptr, 4);
        cur_ptr += 4;
        blockentry->length = GINT32_FROM_BE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
        cur_ptr += sizeof(guint32);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Block %2i, ID: %.4s, offset: %Li (0x%LX), length: %i (0x%X).\n", \
        __func__, index, blockentry->block_id, blockentry->offset, blockentry->offset, blockentry->length, blockentry->length);

        /* Got sub-blocks? */
        gint id_index;
        for(id_index = 0; NRGBlockID[id_index].block_id; id_index++) {
            if(!memcmp(blockentry->block_id, NRGBlockID[id_index].block_id, 4)) {
                if(NRGBlockID[id_index].subblock_length) {
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

        /* Get ready for next block */
        cur_ptr += blockentry->length;
    }

    return success;
}

static gboolean __mirage_disc_nrg_destroy_block_index(MIRAGE_Disc *self, GError **error) {
    MIRAGE_Disc_NRGPrivate *_priv = MIRAGE_DISC_NRG_GET_PRIVATE(self);

    if (_priv->block_index) {
        g_free(_priv->block_index);
        _priv->block_index = NULL;
        _priv->block_index_entries = 0;
    }
    else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to free memory for block index!\n", __func__);
        return FALSE;
    }

    return TRUE;
}


/******************************************************************************\
 *                     MIRAGE_Disc methods implementation                     *
\******************************************************************************/
static gboolean __mirage_disc_nrg_get_parser_info (MIRAGE_Disc *self, MIRAGE_ParserInfo **parser_info, GError **error) {
    MIRAGE_Disc_NRGPrivate *_priv = MIRAGE_DISC_NRG_GET_PRIVATE(self);
    *parser_info = _priv->parser_info;
    return TRUE;
}

static gboolean __mirage_disc_nrg_can_load_file (MIRAGE_Disc *self, gchar *filename, GError **error) {
    MIRAGE_Disc_NRGPrivate *_priv = MIRAGE_DISC_NRG_GET_PRIVATE(self);
    gboolean succeeded = FALSE;
    
    /* Does file exist? */
    if (!g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
        return FALSE;
    }
    
    /* Check the suffixes */
    if (!mirage_helper_match_suffixes(filename, _priv->parser_info->suffixes)) {
        return FALSE;
    }
    
    /* Also check that there's appropriate signature */
    FILE *file = g_fopen(filename, "r");
    gchar sig[4] = "";
    
    if (!file) {
        return FALSE;
    }
    
    /* New format */
    fseeko(file, -12, SEEK_END);
    fread(sig, 4, 1, file);
    if (!memcmp(sig, "NER5", 4)) {
        succeeded = TRUE;
    } else {
        /* Old format */
        fseeko(file, -8, SEEK_END);
        fread(sig, 4, 1, file);
        if (!memcmp(sig, "NERO", 4)) {
            succeeded = TRUE;
        }
    }

    fclose(file);
    
    return succeeded;
}

static gboolean __mirage_disc_nrg_load_image (MIRAGE_Disc *self, gchar **filenames, GError **error) {
    MIRAGE_Disc_NRGPrivate *_priv = MIRAGE_DISC_NRG_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    
    /* For now, NRG parser supports only one-file images */
    if (g_strv_length(filenames) > 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: only single-file images supported!\n", __func__);
        mirage_error(MIRAGE_E_SINGLEFILE, error);
        return FALSE;
    }
    
    /* Open file */
    FILE *file = g_fopen(filenames[0], "r");
    if (!file) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open file '%s'!\n", __func__, filenames[0]);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }
    
    /* Set filename */
    mirage_disc_set_filenames(self, filenames, NULL);

    /* Get filesize */
    guint64 filesize = 0;
    fseeko(file, 0, SEEK_END);
    filesize = ftello(file);
    
    /* Read signature */
    gchar sig[4] = "";
    /* Get offset with index data */
    guint64 trailer_offset = 0;
    
    fseeko(file, -12, SEEK_END);
    fread(sig, 4, 1, file);
    if (!memcmp(sig, "NER5", 4)) {
        /* New format, 64-bit offset */
        guint64 tmp_offset = 0;
        _priv->old_format = FALSE;
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: new format\n", __func__);
        fread((void *)&tmp_offset, 8, 1, file);
        trailer_offset = GUINT64_FROM_BE(tmp_offset);
        _priv->nrg_data_length = (filesize - 12) - trailer_offset;
    } else {
        /* Try old format, with 32-bit offset */
        fseeko(file, -8, SEEK_END);
        fread(sig, 4, 1, file);
        if (!memcmp(sig, "NERO", 4)) {
            guint32 tmp_offset = 0;
            _priv->old_format = TRUE;
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: old format\n", __func__);
            fread((void *)&tmp_offset, 4, 1, file);
            trailer_offset = GUINT32_FROM_BE(tmp_offset);
            _priv->nrg_data_length = (filesize - 8) - trailer_offset;
        } else {
            /* Error */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown signature!\n", __func__);
            mirage_error(MIRAGE_E_PARSER, error);
            return FALSE;
        }
    }
    
    /* Medium type; CD-ROM by default, if there's MTYP block, it'll be changed
       accordingly */
    mirage_disc_set_medium_type(self, MIRAGE_MEDIUM_CD, NULL);
    
    /* Read descriptor data */
    _priv->nrg_data = g_malloc(_priv->nrg_data_length);
    fseeko(file, trailer_offset, SEEK_SET);
    fread(_priv->nrg_data, _priv->nrg_data_length, 1, file);
    
    /* Build an index over blocks contained in the disc image */
    if(!__mirage_disc_nrg_build_block_index(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to build block index!\n", __func__);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }

    /* Go over the blocks and do what's to be done... */
    gint index = 0;
    for (index = 0; index < _priv->block_index_entries; index++) {
        gchar *block_id = _priv->block_index[index].block_id;
        
        if ((!memcmp(block_id, "CUEX", 4)) || (!memcmp(block_id, "CUES", 4))) {
            /* CUEX/CUES block: means we need to make new session */
            if (!__mirage_disc_nrg_load_session(self, error)) {
                succeeded = FALSE;
                break;
            }
        } else if ((!memcmp(block_id, "ETNF", 4)) || (!memcmp(block_id, "ETN2", 4))) {
            /* ETNF/ETN2 block: means we need to make new session */
            if (!__mirage_disc_nrg_load_session_tao(self, error)) {
                succeeded = FALSE;
                break;
            }
        } else if ((!memcmp(block_id, "DAOX", 4)) || (!memcmp(block_id, "DAOI", 4))) {
            /* DAOX/DAOI block: we skip this one, because it should've been read together with CUEX/CUES */
        } else if (!memcmp(block_id, "CDTX", 4)) {
            if (!__mirage_disc_nrg_load_cdtext(self, error)) {
                succeeded = FALSE;
                break;
            }
        } else if (!memcmp(block_id, "SINF", 4)) {
            /* SINF block: AFAIK contains no useful data (except number of tracks in session, 
               but we don't really need that one anyway) */
        } else if (!memcmp(block_id, "MTYP", 4)) {
            /* MTYP: medium type */            
            if (!__mirage_disc_nrg_load_medium_type(self, error)) {
                succeeded = FALSE;
                break;
            }
        } else if (!memcmp(block_id, "END!", 4)) {
            /* END!: self-explanatory */
        } else {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown block '%.4s'!\n", __func__, block_id);
        }
    }

    /* Destroy block index */
    if(!__mirage_disc_nrg_destroy_block_index(self, error)) {
        return FALSE;
    }

    g_free(_priv->nrg_data);
    fclose(file);
    
    return succeeded;
}


/******************************************************************************\
 *                                Object init                                 *
\******************************************************************************/
/* Our parent class */
static MIRAGE_DiscClass *parent_class = NULL;

static void __mirage_disc_nrg_instance_init (GTypeInstance *instance, gpointer g_class) {
    MIRAGE_Disc_NRG *self = MIRAGE_DISC_NRG(instance);
    MIRAGE_Disc_NRGPrivate *_priv = MIRAGE_DISC_NRG_GET_PRIVATE(self);
    
    /* Create parser info */
    _priv->parser_info = mirage_helper_create_parser_info(
        "PARSER-NRG",
        "NRG Image Parser",
        "1.0.0",
        "Rok Mandeljc",
        FALSE,
        "NRG (Nero Burning Rom) images",
        2, ".nrg", NULL
    );
    
    return;
}

static void __mirage_disc_nrg_finalize (GObject *obj) {
    MIRAGE_Disc_NRG *self = MIRAGE_DISC_NRG(obj);
    MIRAGE_Disc_NRGPrivate *_priv = MIRAGE_DISC_NRG_GET_PRIVATE(self);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s:\n", __func__);
    
    /* Free parser info */
    mirage_helper_destroy_parser_info(_priv->parser_info);
    
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __func__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}


static void __mirage_disc_nrg_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_DiscClass *class_disc = MIRAGE_DISC_CLASS(g_class);
    MIRAGE_Disc_NRGClass *klass = MIRAGE_DISC_NRG_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Disc_NRGPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_disc_nrg_finalize;
    
    /* Initialize MIRAGE_Disc methods */
    class_disc->get_parser_info = __mirage_disc_nrg_get_parser_info;
    class_disc->can_load_file = __mirage_disc_nrg_can_load_file;
    class_disc->load_image = __mirage_disc_nrg_load_image;
        
    return;
}

GType mirage_disc_nrg_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Disc_NRGClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_disc_nrg_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Disc_NRG),
            0,      /* n_preallocs */
            __mirage_disc_nrg_instance_init    /* instance_init */
        };
        
        type = g_type_module_register_type(module, MIRAGE_TYPE_DISC, "MIRAGE_Disc_NRG", &info, 0);
    }
    
    return type;
}
