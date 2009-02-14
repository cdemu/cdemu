/*
 *  libMirage: MDS image parser: Parser object
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

#include "image-mds.h"


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_PARSER_MDS_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_MDS, MIRAGE_Parser_MDSPrivate))

typedef struct {
    GObject *disc;
    
    MDS_Header *header;
    
    gint32 prev_session_end;
        
    gchar *mds_filename;
    
    GMappedFile *mds_mapped;
    guint8 *mds_data;
} MIRAGE_Parser_MDSPrivate;


/*
    I hexedited the track mode field with various values and fed it to Alchohol;
    it seemed that high part of byte had no effect at all; only the lower one 
    affected the mode, in the following manner:
    00: Mode 2, 01: Audio, 02: Mode 1, 03: Mode 2, 04: Mode 2 Form 1, 05: Mode 2 Form 2, 06: UKNONOWN, 07: Mode 2
    08: Mode 2, 09: Audio, 0A: Mode 1, 0B: Mode 2, 0C: Mode 2 Form 1, 0D: Mode 2 Form 2, 0E: UKNONOWN, 0F: Mode 2
*/
static gint __mirage_parser_mds_convert_track_mode (MIRAGE_Parser *self, gint mode) {
    /* convert between two values */
    static const struct {
        gint mds_mode;
        gint mirage_mode;
    } modes[] = {
        {0x00, MIRAGE_MODE_MODE2},
        {0x01, MIRAGE_MODE_AUDIO},
        {0x02, MIRAGE_MODE_MODE1},
        {0x03, MIRAGE_MODE_MODE2},
        {0x04, MIRAGE_MODE_MODE2_FORM1},
        {0x05, MIRAGE_MODE_MODE2_FORM2},
        /*{0x06, MIRAGE_MODE_UNKNOWN},*/
        {0x07, MIRAGE_MODE_MODE2},
    };
    gint i;
    
    /* Basically, do the test twice; once for value, and once for value + 8 */
    for (i = 0; i < G_N_ELEMENTS(modes); i++) {
        if (((mode & 0x0F) == modes[i].mds_mode)
            || ((mode & 0x0F) == modes[i].mds_mode + 8)) {
            return modes[i].mirage_mode;
        }
    }
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown track mode 0x%X!\n", __func__, mode);    
    return -1;
}


static gchar *__helper_find_binary_file (gchar *declared_filename, gchar *mds_filename) {
    gchar *bin_filename;
    gchar *bin_fullpath;
            
    gchar ext[4] = "";
    if (sscanf(declared_filename, "*.%s", ext) == 1) {
        gint len;
        /* Use MDS filename and replace its extension with the one of the data file */
        bin_filename = g_strdup(mds_filename);
        len = strlen(bin_filename);
        sprintf(bin_filename+len-3, ext);
    } else {
        bin_filename = g_strdup(declared_filename);
    }
        
    bin_fullpath = mirage_helper_find_data_file(bin_filename, mds_filename);
    g_free(bin_filename);
    
    return bin_fullpath;
}

static gboolean __mirage_parser_mds_parse_dpm_block (MIRAGE_Parser *self, guint32 dpm_block_offset, GError **error) {
    MIRAGE_Parser_MDSPrivate *_priv = MIRAGE_PARSER_MDS_GET_PRIVATE(self);
    guint8 *cur_ptr;
        
    guint32 dpm_block_number;
    guint32 dpm_start_sector;
    guint32 dpm_resolution;
    guint32 dpm_num_entries;
    
    guint32 *dpm_data;
    
    cur_ptr = _priv->mds_data + dpm_block_offset;
    
    /* DPM information */
    dpm_block_number = MIRAGE_CAST_DATA(cur_ptr, 0, guint32);
    cur_ptr += sizeof(guint32);
    
    dpm_start_sector = MIRAGE_CAST_DATA(cur_ptr, 0, guint32);
    cur_ptr += sizeof(guint32);
    
    dpm_resolution = MIRAGE_CAST_DATA(cur_ptr, 0, guint32);
    cur_ptr += sizeof(guint32);
    
    dpm_num_entries = MIRAGE_CAST_DATA(cur_ptr, 0, guint32);
    cur_ptr += sizeof(guint32);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: block number: %d\n", __func__, dpm_block_number);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: start sector: 0x%X\n", __func__, dpm_start_sector);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: resolution: %d\n", __func__, dpm_resolution);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of entries: %d\n", __func__, dpm_num_entries);
    
    /* Read all entries */
    dpm_data = MIRAGE_CAST_PTR(cur_ptr, 0, guint32 *);
    
    /* Set DPM data */
    if (!mirage_disc_set_dpm_data(MIRAGE_DISC(_priv->disc), dpm_start_sector, dpm_resolution, dpm_num_entries, dpm_data, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set DPM data!\n", __func__);
        return FALSE;
    }
    
    return TRUE;
}

static gboolean __mirage_parser_mds_parse_dpm_data (MIRAGE_Parser *self, GError **error) {
    MIRAGE_Parser_MDSPrivate *_priv = MIRAGE_PARSER_MDS_GET_PRIVATE(self);
    guint8 *cur_ptr;
    
    gint i;
    
    guint32 num_dpm_blocks;
    guint32 *dpm_block_offset;
    
    if (!_priv->header->dpm_blocks_offset) {
        /* No DPM data, nothing to do */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: no DPM data\n", __func__);
        return TRUE;
    }
    
    cur_ptr = _priv->mds_data + _priv->header->dpm_blocks_offset;
    
    /* It would seem the first field is number of DPM data sets, followed by
       appropriate number of offsets for those data sets */
    num_dpm_blocks = MIRAGE_CAST_DATA(cur_ptr, 0, guint32);
    cur_ptr += sizeof(guint32);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of DPM data blocks: %d\n", __func__, num_dpm_blocks);
    
    dpm_block_offset = MIRAGE_CAST_PTR(cur_ptr, 0, guint32 *);
    
    if (num_dpm_blocks > 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: cannot correctly handle more than 1 DPM block yet!\n", __func__);
    }
    
    /* Read each block */
    for (i = 0; i < num_dpm_blocks; i++) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: block[%i]: offset: 0x%X\n", __func__, i, dpm_block_offset[i]);
        __mirage_parser_mds_parse_dpm_block(self, dpm_block_offset[i], NULL);
        /* FIXME: currently, only first DPM block is loaded */
        break;
    }
     
    return TRUE;
}

static gboolean __mirage_parser_mds_parse_disc_structures (MIRAGE_Parser *self, GError **error) {
    MIRAGE_Parser_MDSPrivate *_priv = MIRAGE_PARSER_MDS_GET_PRIVATE(self);
    guint8 *cur_ptr;
    
    /* *** Disc structures *** */
    /* Disc structures: in lead-in areas of DVD and BD discs there are several
       control structures that store various information about the media. There
       are various formats defined in MMC-3 for these structures, and they are 
       retrieved from disc using READ DISC STRUCTURE command. Of all the structures,
       MDS format seems to store only three types: 
        - 0x0001: DVD copyright information (4 bytes)
        - 0x0004: DVD manufacturing information (2048 bytes)
        - 0x0000: Physical format information (2048 bytes)
       They are stored in that order, taking up 4100 bytes. If disc is dual-layer,
       data consists of 8200 bytes, containing afore-mentioned sequence for each
       layer. */
    if (_priv->header->disc_structures_offset) {
        MIRAGE_DiscStruct_Copyright *copy_info;
        MIRAGE_DiscStruct_Manufacture *manu_info;
        MIRAGE_DiscStruct_PhysInfo *phys_info;
        
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading disc structures\n", __func__);
        
        cur_ptr = _priv->mds_data + _priv->header->disc_structures_offset;
        
        /* DVD copyright information */
        copy_info = MIRAGE_CAST_PTR(cur_ptr, 0, MIRAGE_DiscStruct_Copyright *);
        cur_ptr += sizeof(MIRAGE_DiscStruct_Copyright);
                        
        /* DVD manufacture information */
        manu_info = MIRAGE_CAST_PTR(cur_ptr, 0, MIRAGE_DiscStruct_Manufacture *);
        cur_ptr += sizeof(MIRAGE_DiscStruct_Manufacture);
                
        /* Physical information */
        phys_info = MIRAGE_CAST_PTR(cur_ptr, 0, MIRAGE_DiscStruct_PhysInfo *);
        cur_ptr += sizeof(MIRAGE_DiscStruct_PhysInfo);
                
        mirage_disc_set_disc_structure(MIRAGE_DISC(_priv->disc), 0, 0x0000, (guint8 *)phys_info, sizeof(MIRAGE_DiscStruct_Copyright), NULL);
        mirage_disc_set_disc_structure(MIRAGE_DISC(_priv->disc), 0, 0x0001, (guint8 *)copy_info, sizeof(MIRAGE_DiscStruct_Manufacture), NULL);
        mirage_disc_set_disc_structure(MIRAGE_DISC(_priv->disc), 0, 0x0004, (guint8 *)manu_info, sizeof(MIRAGE_DiscStruct_PhysInfo), NULL);
                    
        /* Second round if it's dual-layer... */
        if (phys_info->num_layers == 0x01) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: dual-layer disc; reading disc structures for second layer\n", __func__);
                        
            /* DVD copyright information */
            copy_info = MIRAGE_CAST_PTR(cur_ptr, 0, MIRAGE_DiscStruct_Copyright *);
            cur_ptr += sizeof(MIRAGE_DiscStruct_Copyright);
                            
            /* DVD manufacture information */
            manu_info = MIRAGE_CAST_PTR(cur_ptr, 0, MIRAGE_DiscStruct_Manufacture *);
            cur_ptr += sizeof(MIRAGE_DiscStruct_Manufacture);
                    
            /* Physical information */
            phys_info = MIRAGE_CAST_PTR(cur_ptr, 0, MIRAGE_DiscStruct_PhysInfo *);
            cur_ptr += sizeof(MIRAGE_DiscStruct_PhysInfo);
            
            mirage_disc_set_disc_structure(MIRAGE_DISC(_priv->disc), 0, 0x0000, (guint8 *)phys_info, sizeof(MIRAGE_DiscStruct_Copyright), NULL);
            mirage_disc_set_disc_structure(MIRAGE_DISC(_priv->disc), 0, 0x0001, (guint8 *)copy_info, sizeof(MIRAGE_DiscStruct_Manufacture), NULL);
            mirage_disc_set_disc_structure(MIRAGE_DISC(_priv->disc), 0, 0x0004, (guint8 *)manu_info, sizeof(MIRAGE_DiscStruct_PhysInfo), NULL);
        }
    }
    
    return TRUE;
}

static gboolean __mirage_parser_mds_parse_bca (MIRAGE_Parser *self, GError **error) {
    MIRAGE_Parser_MDSPrivate *_priv = MIRAGE_PARSER_MDS_GET_PRIVATE(self);
    guint8 *cur_ptr;
    
    /* It seems BCA (Burst Cutting Area) structure is stored as well, but in separate
       place (kinda makes sense, because it doesn't have fixed length) */
    if (_priv->header->bca_len) {       
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading BCA data (0x%X bytes)\n", __func__, _priv->header->bca_len);
        
        cur_ptr = _priv->mds_data + _priv->header->bca_data_offset;
        mirage_disc_set_disc_structure(MIRAGE_DISC(_priv->disc), 0, 0x0003, MIRAGE_CAST_PTR(cur_ptr, 0, guint8 *), _priv->header->bca_len, NULL);
    }
    
    return TRUE;
}

static gchar *__mirage_parser_mds_get_track_filename (MIRAGE_Parser *self, MDS_Footer *footer_block, GError **error) {
    MIRAGE_Parser_MDSPrivate *_priv = MIRAGE_PARSER_MDS_GET_PRIVATE(self);
    gchar *tmp_mdf_filename;
    gchar *mdf_filename;
    
    /* Track file: it seems all tracks have the same extra block, and that 
       filename is located at the end of it... meaning filename's length is 
       from filename_offset to end of the file */
    if (!footer_block) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: track block does not have a footer, but we're supposed to get filename from it!\n", __func__);
        mirage_error(MIRAGE_E_PARSER, error);
        return NULL;
    }
    
    /* If footer_block->widechar_filename is set, filename is stored using 16-bit 
       (wide) characters, otherwise 8-bit characters are used. */
    if (footer_block->widechar_filename) {
        gunichar2 *tmp_ptr = MIRAGE_CAST_PTR(_priv->mds_data, footer_block->filename_offset, gunichar2 *);
        tmp_mdf_filename = g_utf16_to_utf8(tmp_ptr, -1, NULL, NULL, NULL);
    } else {
        gchar *tmp_ptr = MIRAGE_CAST_PTR(_priv->mds_data, footer_block->filename_offset, gchar *);
        tmp_mdf_filename = g_strdup(tmp_ptr);
    }
            
    /* Find binary file */
    mdf_filename = __helper_find_binary_file(tmp_mdf_filename, _priv->mds_filename);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: MDF filename: <%s> -> <%s>\n", __func__, tmp_mdf_filename, mdf_filename);
    g_free(tmp_mdf_filename);
    
    if (!mdf_filename) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to find data file!\n", __func__);
        mirage_error(MIRAGE_E_DATAFILE, error);
        return NULL;
    }
    
    return mdf_filename;
}

static gboolean __mirage_parser_mds_parse_track_entries (MIRAGE_Parser *self, MDS_SessionBlock *session_block, GError **error) {
    MIRAGE_Parser_MDSPrivate *_priv = MIRAGE_PARSER_MDS_GET_PRIVATE(self);
    GObject *cur_session = NULL;
    guint8 *cur_ptr;
    gint medium_type;
    gint i;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading track blocks\n", __func__);        
    
    /* Fetch medium type which we'll need later */
    mirage_disc_get_medium_type(MIRAGE_DISC(_priv->disc), &medium_type, NULL);
    
    /* Get current session */
    if (!mirage_disc_get_session_by_index(MIRAGE_DISC(_priv->disc), -1, &cur_session, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current session!\n", __func__);
        return FALSE;
    }
    
    cur_ptr = _priv->mds_data + session_block->tracks_blocks_offset;
    
    /* Read track entries */
    for (i = 0; i < session_block->num_all_blocks; i++) {
        MDS_TrackBlock *block;
        MDS_TrackExtraBlock *extra_block = NULL;
        MDS_Footer *footer_block = NULL;
        
        /* Read main track block */
        block = MIRAGE_CAST_PTR(cur_ptr, 0, MDS_TrackBlock *);
        cur_ptr += sizeof(MDS_TrackBlock);
        
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track block #%i:\n", __func__, i);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  mode: 0x%X\n", __func__, block->mode);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  subchannel: 0x%X\n", __func__, block->subchannel);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  adr/ctl: 0x%X\n", __func__, block->adr_ctl);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  dummy2: 0x%X\n", __func__, block->__dummy2__);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  point: 0x%X\n", __func__, block->point);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  dummy3: 0x%X\n", __func__, block->__dummy3__);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  min: %i\n", __func__, block->min);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  sec: %i\n", __func__, block->sec);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  frame: %i\n", __func__, block->frame);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  extra offset: 0x%X\n", __func__, block->extra_offset);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  sector size: 0x%X\n", __func__, block->sector_size);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  start sector: 0x%X\n", __func__, block->start_sector);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  start offset: 0x%llX\n", __func__, block->start_offset);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  session: 0x%X\n", __func__, block->session);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  footer offset: 0x%X\n", __func__, block->footer_offset);
        
        /* Read extra track block, if applicable; it seems that only CD images
           have extra blocks, though. For DVD images, extra_offset seems to 
           contain track length */
        if (medium_type == MIRAGE_MEDIUM_CD && block->extra_offset) {
            extra_block = (MDS_TrackExtraBlock *)(_priv->mds_data + block->extra_offset);
                        
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: extra block #%i:\n", __func__, i);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  pregap: 0x%X\n", __func__, extra_block->pregap);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  length: 0x%X\n", __func__, extra_block->length);
        }
        
        /* Read footer, if applicable */
        if (block->footer_offset) {
            footer_block = (MDS_Footer *)(_priv->mds_data + block->footer_offset);
            
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: footer block #%i:\n", __func__, i);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  filename offset: 0x%X\n", __func__, footer_block->filename_offset);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  widechar filename: 0x%X\n", __func__, footer_block->widechar_filename);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  dummy1: 0x%X\n", __func__, footer_block->__dummy1__);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  dummy2: 0x%X\n", __func__, footer_block->__dummy2__);            
        }
            
        if (block->point > 0 && block->point < 99) {
            /* Track entry */
            GObject *cur_track = NULL;
            
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: entry is for track %i\n", __func__, block->point);
            
            if (!mirage_session_add_track_by_number(MIRAGE_SESSION(cur_session), block->point, &cur_track, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __func__);
                g_object_unref(cur_session);
                return FALSE;
            }
                    
            gint converted_mode = __mirage_parser_mds_convert_track_mode(self, block->mode);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track mode: 0x%X\n", __func__, converted_mode);
            mirage_track_set_mode(MIRAGE_TRACK(cur_track), converted_mode, NULL);
                    
            /* Flags: decoded from Ctl */
            mirage_track_set_ctl(MIRAGE_TRACK(cur_track), block->adr_ctl & 0x0F, NULL);
            
            /* Track file */
            gchar *mdf_filename = __mirage_parser_mds_get_track_filename(self, footer_block, error);
            
            if (!mdf_filename) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get track filename!\n", __func__);
                g_object_unref(cur_track);
                g_object_unref(cur_session);
                return FALSE;
            }
            
            /* MDS format doesn't seem to store pregap data in its data file; 
               therefore, we need to provide NULL fragment for pregap */
            if (extra_block && extra_block->pregap) {
                GObject *pregap_fragment;
                
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track has pregap (0x%X); creating NULL fragment\n", __func__, extra_block->pregap);
                
                pregap_fragment = libmirage_create_fragment(MIRAGE_TYPE_FINTERFACE_NULL, "NULL", error);
                if (!pregap_fragment) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to create NULL fragment!\n", __func__);
                    g_object_unref(cur_track);
                    g_object_unref(cur_session);
                    return FALSE;
                }
                
                mirage_fragment_set_length(MIRAGE_FRAGMENT(pregap_fragment), extra_block->pregap, NULL);
                
                mirage_track_add_fragment(MIRAGE_TRACK(cur_track), -1, &pregap_fragment, error);
                g_object_unref(pregap_fragment);
                
                mirage_track_set_track_start(MIRAGE_TRACK(cur_track), extra_block->pregap, NULL);
            }
            
            /* Data fragment */
            GObject *data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FINTERFACE_BINARY, mdf_filename, error);
            if (!data_fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to create fragment!\n", __func__);
                g_object_unref(cur_track);
                g_object_unref(cur_session);
                return FALSE;
            }
            
            /* Prepare data fragment */
            FILE *tfile_handle = g_fopen(mdf_filename, "r");
            guint64 tfile_offset = block->start_offset;
            gint tfile_sectsize = block->sector_size;
            gint tfile_format = 0;
                
            if (converted_mode == MIRAGE_MODE_AUDIO) {
                tfile_format = FR_BIN_TFILE_AUDIO;
            } else {
                tfile_format = FR_BIN_TFILE_DATA;
            }
                
            gint fragment_len = 0;
                
            g_free(mdf_filename);
            
            /* Depending on medium type, we determine track's length... */
            if (medium_type == MIRAGE_MEDIUM_DVD) {
                /* Length: for DVD-ROMs, track length seems to be stored in extra_offset */
                fragment_len = block->extra_offset;
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: DVD-ROM; track's fragment length: 0x%X\n", __func__, fragment_len);
            } else {
                /* Length: for CD-ROMs, track lengths are stored in extra blocks */
                fragment_len = extra_block->length;
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD-ROM; track's fragment length: 0x%X\n", __func__, fragment_len);
            }
                
            mirage_fragment_set_length(MIRAGE_FRAGMENT(data_fragment), fragment_len, NULL);
                
            mirage_finterface_binary_track_file_set_handle(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_handle, NULL);
            mirage_finterface_binary_track_file_set_offset(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_offset, NULL);
            mirage_finterface_binary_track_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_sectsize, NULL);
            mirage_finterface_binary_track_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_format, NULL);
                
            /* Subchannel */
            switch (block->subchannel) {
                case MDS_SUBCHAN_PW_INTERLEAVED: {
                    gint sfile_sectsize = 96;
                    gint sfile_format = FR_BIN_SFILE_PW96_INT | FR_BIN_SFILE_INT;
                    
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: subchannel found; interleaved PW96\n", __func__);

                    mirage_finterface_binary_subchannel_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), sfile_sectsize, NULL);
                    mirage_finterface_binary_subchannel_file_set_format(MIRAGE_FINTERFACE_BINARY(data_fragment), sfile_format, NULL);
                        
                    /* We need to correct the data for track sector size...
                       MDS format has already added 96 bytes to sector size,
                       so we need to subtract it */
                    tfile_sectsize = block->sector_size - sfile_sectsize;
                    mirage_finterface_binary_track_file_set_sectsize(MIRAGE_FINTERFACE_BINARY(data_fragment), tfile_sectsize, NULL);
                            
                    break;
                }
                case MDS_SUBCHAN_NONE: {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: no subchannel\n", __func__);
                    break;
                }
                default: {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown subchannel type 0x%X!\n", __func__, block->subchannel);
                    break;
                }
            }
            
            mirage_track_add_fragment(MIRAGE_TRACK(cur_track), -1, &data_fragment, error);
            g_object_unref(data_fragment);
                
            g_object_unref(cur_track);
        } else {
            /* Non-track block; skip */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: skipping non-track entry 0x%X\n", __func__, block->point);
        }
    }
    
    g_object_unref(cur_session);
    
    return TRUE;
}

static gboolean __mirage_parser_mds_parse_sessions (MIRAGE_Parser *self, GError **error) {
    MIRAGE_Parser_MDSPrivate *_priv = MIRAGE_PARSER_MDS_GET_PRIVATE(self);
    guint8 *cur_ptr;
    gint i;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading session blocks (%i)\n", __func__, _priv->header->num_sessions);
   
    cur_ptr = _priv->mds_data + _priv->header->sessions_blocks_offset;
    
    /* Read sessions */
    for (i = 0; i < _priv->header->num_sessions; i++) {
        MDS_SessionBlock *session = MIRAGE_CAST_PTR(cur_ptr, 0, MDS_SessionBlock *);
        cur_ptr += sizeof(MDS_SessionBlock);
        
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: session block #%i:\n", __func__, i);        
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  start address: 0x%X\n", __func__, session->session_start);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  length: 0x%X\n", __func__, session->session_end);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  number: %i\n", __func__, session->session_number);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  number of all blocks: %i\n", __func__, session->num_all_blocks);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  number of non-track block: %i\n", __func__, session->num_nontrack_blocks);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  first track: %i\n", __func__, session->first_track);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  last track: %i\n", __func__, session->last_track);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  dummy2: 0x%X\n", __func__, session->__dummy2__);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  track blocks offset: 0x%X\n", __func__, session->tracks_blocks_offset);
        
        /* If this is first session, we'll use its start address as disc start address;
           if not, we need to calculate previous session's leadout length, based on 
           this session's start address and previous session's end... */
        if (i == 0) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: first session; setting disc's start to 0x%X (%i)\n", __func__, session->session_start, session->session_start);
            mirage_disc_layout_set_start_sector(MIRAGE_DISC(_priv->disc), session->session_start, NULL);
        } else {
            guint32 leadout_length = session->session_start - _priv->prev_session_end;
            GObject *prev_session = NULL;
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: previous session's leadout length: 0x%X (%i)\n", __func__, leadout_length, leadout_length);
            
            /* Use -1 as an index, since we still haven't added current session */
            if (!mirage_disc_get_session_by_index(MIRAGE_DISC(_priv->disc), -1, &prev_session, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get previous session!\n", __func__);
                return FALSE;
            }
            
            if (!mirage_session_set_leadout_length(MIRAGE_SESSION(prev_session), leadout_length, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set leadout length!\n", __func__);
                g_object_unref(prev_session);
                return FALSE;
            }
            
            g_object_unref(prev_session);
        }
        /* Actually, we could've gotten that one from A2 track entry as well...
           but I'm lazy, and this will hopefully work as well */
        _priv->prev_session_end = session->session_end;
        
        /* Add session */
        if (!mirage_disc_add_session_by_number(MIRAGE_DISC(_priv->disc), session->session_number, NULL, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __func__);        
            return FALSE;
        }
        
        /* Load tracks */
        if (!__mirage_parser_mds_parse_track_entries(self, session, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse track entries!\n", __func__);
            return FALSE;
        }
    }
    
    return TRUE;
}

static gboolean __mirage_parser_mds_load_disc (MIRAGE_Parser *self, GError **error) {
    /*MIRAGE_Parser_MDSPrivate *_priv = MIRAGE_PARSER_MDS_GET_PRIVATE(self);*/
    
    /* Read parser structures */
    if (!__mirage_parser_mds_parse_disc_structures(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse disc structures!\n", __func__);
        return FALSE;
    }
    
    /* Read BCA */
    if (!__mirage_parser_mds_parse_bca(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse BCA!\n", __func__);
        return FALSE;
    }
    
    /* Sessions */
    if (!__mirage_parser_mds_parse_sessions(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse sessions!\n", __func__);
        return FALSE;
    }
    
    /* DPM data */
    if (!__mirage_parser_mds_parse_dpm_data(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse DPM data!\n", __func__);
        return FALSE;
    }
    
    return TRUE;
}

/******************************************************************************\
 *                     MIRAGE_Parser methods implementation                     *
\******************************************************************************/
static gboolean __mirage_parser_mds_load_image (MIRAGE_Parser *self, gchar **filenames, GObject **disc, GError **error) {
    MIRAGE_Parser_MDSPrivate *_priv = MIRAGE_PARSER_MDS_GET_PRIVATE(self);
    gboolean succeeded = TRUE;
    GError *local_error = NULL;
    guint8 *cur_ptr;
    FILE *file;
    gchar sig[16] = "";

    /* Check if we can load the image */
    file = g_fopen(filenames[0], "r");
    if (!file) {
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }
    
    if (fread(sig, 16, 1, file) < 1) {
        fclose(file);
        mirage_error(MIRAGE_E_READFAILED, error);
        return FALSE;
    }
    
    fclose(file);
    
    if (memcmp(sig, "MEDIA DESCRIPTOR", 16)) {
        mirage_error(MIRAGE_E_CANTHANDLE, error);
        return FALSE;
    }
    
    /* Create disc */
    _priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_attach_child(MIRAGE_OBJECT(self), _priv->disc, NULL);

    mirage_disc_set_filename(MIRAGE_DISC(_priv->disc), filenames, NULL);
    _priv->mds_filename = g_strdup(filenames[0]);
    
    /* Map the file using GLib's GMappedFile */
    _priv->mds_mapped = g_mapped_file_new(filenames[0], FALSE, &local_error);
    if (!_priv->mds_mapped) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to map file '%s': %s!\n", __func__, filenames[0], local_error->message);
        g_error_free(local_error);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        succeeded = FALSE;
        goto end;
    }
    
    _priv->mds_data = (guint8 *)g_mapped_file_get_contents(_priv->mds_mapped);
    cur_ptr = _priv->mds_data;
    
    _priv->header = MIRAGE_CAST_PTR(cur_ptr, 0, MDS_Header *);
    cur_ptr += sizeof(MDS_Header);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: MDS header:\n", __func__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  signature: %.16s\n", __func__, _priv->header->signature);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  version (?): %u.%u\n", __func__, _priv->header->version[0], _priv->header->version[1]);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  medium type: 0x%X\n", __func__, _priv->header->medium_type);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  number of sessions: 0x%X\n", __func__, _priv->header->num_sessions);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  dummy1: 0x%X, 0x%X\n", __func__, _priv->header->__dummy1__[0], _priv->header->__dummy1__[1]);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  BCA length: 0x%X\n", __func__, _priv->header->bca_len);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  dummy2: 0x%X, 0x%X\n", __func__, _priv->header->__dummy2__[0], _priv->header->__dummy2__[1]);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  BCA data offset: 0x%X\n", __func__, _priv->header->bca_data_offset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  dummy3: 0x%X, 0x%X, 0x%X, 0x%X, 0x%X, 0x%X\n", __func__, _priv->header->__dummy3__[0], _priv->header->__dummy3__[1], _priv->header->__dummy3__[2], _priv->header->__dummy3__[3], _priv->header->__dummy3__[4], _priv->header->__dummy3__[5]);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  disc structures offset: 0x%X\n", __func__, _priv->header->disc_structures_offset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  dummy4: 0x%X, 0x%X, 0x%X\n", __func__, _priv->header->__dummy4__[0], _priv->header->__dummy4__[1], _priv->header->__dummy4__[2]);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  session blocks offset: 0x%X\n", __func__, _priv->header->sessions_blocks_offset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  DPM blocks offset: 0x%X\n", __func__, _priv->header->dpm_blocks_offset);
    
    switch (_priv->header->medium_type) {
        case MDS_MEDIUM_CD:
        case MDS_MEDIUM_CD_R:
        case MDS_MEDIUM_CD_RW: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD-ROM image\n", __func__);
            mirage_disc_set_medium_type(MIRAGE_DISC(_priv->disc), MIRAGE_MEDIUM_CD, NULL);
            succeeded = __mirage_parser_mds_load_disc(self, error);
            break;
        }
        case MDS_MEDIUM_DVD: 
        case MDS_MEDIUM_DVD_MINUS_R: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: DVD-ROM image\n", __func__);
            mirage_disc_set_medium_type(MIRAGE_DISC(_priv->disc), MIRAGE_MEDIUM_DVD, NULL);
            succeeded = __mirage_parser_mds_load_disc(self, error);            
            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: medium of type 0x%X not supported yet!\n", __func__, _priv->header->medium_type);
            mirage_error(MIRAGE_E_NOTIMPL, error);
            succeeded = FALSE;
            break;
        }
    }

    _priv->mds_data = NULL;
    g_mapped_file_free(_priv->mds_mapped);

end:    
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

static void __mirage_parser_mds_instance_init (GTypeInstance *instance, gpointer g_class) {
    mirage_parser_generate_parser_info(MIRAGE_PARSER(instance),
        "PARSER-MDS",
        "MDS Image Parser",
        "MDS (Media descriptor) images",
        "application/libmirage-mds"
    );
    
    return;
}

static void __mirage_parser_mds_finalize (GObject *obj) {
    MIRAGE_Parser_MDS *self = MIRAGE_PARSER_MDS(obj);
    MIRAGE_Parser_MDSPrivate *_priv = MIRAGE_PARSER_MDS_GET_PRIVATE(self);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s:\n", __func__);

    g_free(_priv->mds_filename);
    
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __func__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __mirage_parser_mds_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_ParserClass *class_parser = MIRAGE_PARSER_CLASS(g_class);
    MIRAGE_Parser_MDSClass *klass = MIRAGE_PARSER_MDS_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Parser_MDSPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_parser_mds_finalize;
    
    /* Initialize MIRAGE_Parser methods */
    class_parser->load_image = __mirage_parser_mds_load_image;
        
    return;
}

GType mirage_parser_mds_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Parser_MDSClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_parser_mds_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Parser_MDS),
            0,      /* n_preallocs */
            __mirage_parser_mds_instance_init    /* instance_init */
        };
        
        type = g_type_module_register_type(module, MIRAGE_TYPE_PARSER, "MIRAGE_Parser_MDS", &info, 0);
    }
    
    return type;
}
