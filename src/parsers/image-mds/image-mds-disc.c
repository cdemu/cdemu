/*
 *  libMirage: MDS image parser: Disc object
 *  Copyright (C) 2006-2007 Rok Mandeljc
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
#define MIRAGE_DISC_MDS_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_DISC_MDS, MIRAGE_Disc_MDSPrivate))

typedef struct {   
    MDS_Header *header;
    
    gint32 prev_session_end;
        
    gchar *mds_filename;
    
    GMappedFile *mds_mapped;
    guint8 *mds_data;
    
    /* Parser info */
    MIRAGE_ParserInfo *parser_info;
} MIRAGE_Disc_MDSPrivate;


/*
    I hexedited the track mode field with various values and fed it to Alchohol;
    it seemed that high part of byte had no effect at all; only the lower one 
    affected the mode, in the following manner:
    00: Mode 2, 01: Audio, 02: Mode 1, 03: Mode 2, 04: Mode 2 Form 1, 05: Mode 2 Form 2, 06: UKNONOWN, 07: Mode 2
    08: Mode 2, 09: Audio, 0A: Mode 1, 0B: Mode 2, 0C: Mode 2 Form 1, 0D: Mode 2 Form 2, 0E: UKNONOWN, 0F: Mode 2
*/
static gint __mirage_disc_mds_convert_track_mode (MIRAGE_Disc *self, gint mode) {
    /* convert between two values */
    static struct {
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
    gchar *bin_filename = NULL;
    gchar *bin_fullpath = NULL;
            
    gchar ext[4] = "";
    if (sscanf(declared_filename, "*.%s", ext) == 1) {
        /* Use MDS filename and replace its extension with the one of the data file */
        bin_filename = g_strdup(mds_filename);
        gint len = strlen(bin_filename);
        sprintf(bin_filename+len-3, ext);
    } else {
        bin_filename = g_strdup(declared_filename);
    }
        
    bin_fullpath = mirage_helper_find_data_file(bin_filename, mds_filename);
    g_free(bin_filename);
    
    return bin_fullpath;
}

static gboolean __mirage_disc_mds_parse_dpm_block (MIRAGE_Disc *self, guint32 dpm_block_offset, GError **error) {
    MIRAGE_Disc_MDSPrivate *_priv = MIRAGE_DISC_MDS_GET_PRIVATE(self);
    guint8 *cur_ptr = NULL;
        
    guint32 dpm_block_number = 0;
    guint32 dpm_start_sector = 0;
    guint32 dpm_resolution = 0;
    guint32 dpm_num_entries = 0;
    
    guint32 *dpm_data = NULL;
    
    cur_ptr = _priv->mds_data + dpm_block_offset;
    
    /* */
    dpm_block_number = MIRAGE_CAST_DATA(cur_ptr, 0, guint32);
    cur_ptr += sizeof(guint32);
    
    dpm_start_sector = MIRAGE_CAST_DATA(cur_ptr, 0, guint32);
    cur_ptr += sizeof(guint32);
    
    dpm_resolution = MIRAGE_CAST_DATA(cur_ptr, 0, guint32);
    cur_ptr += sizeof(guint32);
    
    dpm_num_entries = MIRAGE_CAST_DATA(cur_ptr, 0, guint32);
    cur_ptr += sizeof(guint32);
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Block number: %d\n", __func__, dpm_block_number);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Start sector: 0x%X\n", __func__, dpm_start_sector);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Resolution: %d\n", __func__, dpm_resolution);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Number of entries: %d\n", __func__, dpm_num_entries);
    
    /* Read all entries */
    dpm_data = MIRAGE_CAST_PTR(cur_ptr, 0, guint32 *);
    
    /* FIXME: someday, somehow, I'm gonna make it alright, but not right now...
       (do something useful with it once libMirage gets whole DPM infrastructure
       sorted out */
    
    return TRUE;
}

static gboolean __mirage_disc_mds_parse_dpm_data (MIRAGE_Disc *self, GError **error) {
    MIRAGE_Disc_MDSPrivate *_priv = MIRAGE_DISC_MDS_GET_PRIVATE(self);
    guint8 *cur_ptr = NULL;
    
    gint i;
    
    guint32 num_dpm_blocks = 0;
    guint32 *dpm_block_offset = NULL;
    
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
        
    /* Read each block */
    for (i = 0; i < num_dpm_blocks; i++) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: block[%i]: offset: 0x%X\n", __func__, i, dpm_block_offset[i]);
        __mirage_disc_mds_parse_dpm_block(self, dpm_block_offset[i], NULL);
    }
     
    return TRUE;
}

static gboolean __mirage_disc_mds_parse_disc_structures (MIRAGE_Disc *self, GError **error) {
    MIRAGE_Disc_MDSPrivate *_priv = MIRAGE_DISC_MDS_GET_PRIVATE(self);
    guint8 *cur_ptr = NULL;
    
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
       layer.
       
       -- Rok */
    if (_priv->header->disc_structures_offset) {
        MIRAGE_DiscStruct_Copyright *copy_info = NULL;
        MIRAGE_DiscStruct_Manufacture *manu_info = NULL;
        MIRAGE_DiscStruct_PhysInfo *phys_info = NULL;
        
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
                
        mirage_disc_set_disc_structure(self, 0, 0x0000, (guint8 *)phys_info, sizeof(MIRAGE_DiscStruct_Copyright), NULL);
        mirage_disc_set_disc_structure(self, 0, 0x0001, (guint8 *)copy_info, sizeof(MIRAGE_DiscStruct_Manufacture), NULL);
        mirage_disc_set_disc_structure(self, 0, 0x0004, (guint8 *)manu_info, sizeof(MIRAGE_DiscStruct_PhysInfo), NULL);
                    
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
            
            mirage_disc_set_disc_structure(self, 0, 0x0000, (guint8 *)phys_info, sizeof(MIRAGE_DiscStruct_Copyright), NULL);
            mirage_disc_set_disc_structure(self, 0, 0x0001, (guint8 *)copy_info, sizeof(MIRAGE_DiscStruct_Manufacture), NULL);
            mirage_disc_set_disc_structure(self, 0, 0x0004, (guint8 *)manu_info, sizeof(MIRAGE_DiscStruct_PhysInfo), NULL);
        }
    }
    
    return TRUE;
}

static gboolean __mirage_disc_mds_parse_bca (MIRAGE_Disc *self, GError **error) {
    MIRAGE_Disc_MDSPrivate *_priv = MIRAGE_DISC_MDS_GET_PRIVATE(self);
    guint8 *cur_ptr = NULL;
    
    /* It seems BCA (Burst Cutting Area) structure is stored as well, but in separate
       place (kinda makes sense, because it doesn't have fixed length) */
    if (_priv->header->bca_len) {       
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading BCA data (0x%X bytes)\n", __func__, _priv->header->bca_len);
        
        cur_ptr = _priv->mds_data + _priv->header->bca_data_offset;
        mirage_disc_set_disc_structure(self, 0, 0x0003, MIRAGE_CAST_PTR(cur_ptr, 0, guint8 *), _priv->header->bca_len, NULL);
    }
    
    return TRUE;
}

static gboolean __mirage_disc_mds_parse_track_entries (MIRAGE_Disc *self, MDS_SessionBlock *session_block, GError **error) {
    MIRAGE_Disc_MDSPrivate *_priv = MIRAGE_DISC_MDS_GET_PRIVATE(self);
    GObject *cur_session = NULL;
    guint8 *cur_ptr = NULL;
    gint medium_type = 0;
    gint i;
    
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading track blocks\n", __func__);        
    
    /* Fetch medium type which we'll need later */
    mirage_disc_get_medium_type(self, &medium_type, NULL);
    
    /* Get current session */
    if (!mirage_disc_get_session_by_index(self, -1, &cur_session, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current session!\n", __func__);
        return FALSE;
    }
    
    cur_ptr = _priv->mds_data + session_block->tracks_blocks_offset;
    
    /* Read track entries */
    for (i = 0; i < session_block->num_all_blocks; i++) {
        MDS_TrackBlock *block = NULL;
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
                    
            gint converted_mode = __mirage_disc_mds_convert_track_mode(self, block->mode);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track mode: 0x%X\n", __func__, converted_mode);
            mirage_track_set_mode(MIRAGE_TRACK(cur_track), converted_mode, NULL);
                    
            /* Flags: decoded from Ctl */
            mirage_track_set_ctl(MIRAGE_TRACK(cur_track), block->adr_ctl & 0x0F, NULL);
                
            /* Track file: it seems all tracks have the same extra block, and
               that filename is located at the end of it... meaning filename's 
               length is from filename_offset to end of the file */
            if (!footer_block) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: track block does not have a footer, but we're supposed to get filename from it!\n", __func__);
                mirage_error(MIRAGE_E_PARSER, error);
                g_object_unref(cur_track);
                g_object_unref(cur_session);
                return FALSE;
            }
            
            gchar *tmp_mdf_filename = (gchar *)(_priv->mds_data + footer_block->filename_offset);
            gchar *mdf_filename = __helper_find_binary_file(tmp_mdf_filename, _priv->mds_filename);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: MDF filename: <%s> -> <%s>\n", __func__, tmp_mdf_filename, mdf_filename);
            
            if (!mdf_filename) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to find data file!\n", __func__);
                mirage_error(MIRAGE_E_DATAFILE, error);
                g_object_unref(cur_track);
                g_object_unref(cur_session);
                return FALSE;
            }
                
            /* Get Mirage and have it make us fragments */
            GObject *mirage = NULL;
    
            if (!mirage_object_get_mirage(MIRAGE_OBJECT(self), &mirage, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to get Mirage object!\n", __func__);
                g_object_unref(cur_track);
                g_object_unref(cur_session);
                return FALSE;
            }
            
            /* MDS format doesn't seem to store pregap data in its data file; 
               therefore, we need to provide NULL fragment for pregap */
            if (extra_block && extra_block->pregap) {
                GObject *pregap_fragment = NULL;
                
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track has pregap (0x%X); creating NULL fragment\n", __func__, extra_block->pregap);
                
                mirage_mirage_create_fragment(MIRAGE_MIRAGE(mirage), MIRAGE_TYPE_FINTERFACE_NULL, "NULL", &pregap_fragment, error);
                if (!pregap_fragment) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to create NULL fragment!\n", __func__);
                    g_object_unref(mirage);
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
            GObject *data_fragment = NULL;
            mirage_mirage_create_fragment(MIRAGE_MIRAGE(mirage), MIRAGE_TYPE_FINTERFACE_BINARY, mdf_filename, &data_fragment, error);
            g_object_unref(mirage);
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

static gboolean __mirage_disc_mds_parse_sessions (MIRAGE_Disc *self, GError **error) {
    MIRAGE_Disc_MDSPrivate *_priv = MIRAGE_DISC_MDS_GET_PRIVATE(self);
    guint8 *cur_ptr = NULL;
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
            mirage_disc_layout_set_start_sector(self, session->session_start, NULL);
        } else {
            guint32 leadout_length = session->session_start - _priv->prev_session_end;
            GObject *prev_session = NULL;
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: previous session's leadout length: 0x%X (%i)\n", __func__, leadout_length, leadout_length);
            
            /* Use -1 as an index, since we still haven't added current session */
            if (!mirage_disc_get_session_by_index(self, -1, &prev_session, error)) {
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
        if (!mirage_disc_add_session_by_number(self, session->session_number, NULL, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __func__);        
            return FALSE;
        }
        
        /* Load tracks */
        if (!__mirage_disc_mds_parse_track_entries(self, session, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse track entries!\n", __func__);
            return FALSE;
        }
    }
    
    return TRUE;
}

static gboolean __mirage_disc_mds_load_disc (MIRAGE_Disc *self, GError **error) {
    /*MIRAGE_Disc_MDSPrivate *_priv = MIRAGE_DISC_MDS_GET_PRIVATE(self);*/
    
    /* Read disc structures */
    if (!__mirage_disc_mds_parse_disc_structures(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse disc structures!\n", __func__);
        return FALSE;
    }
    
    /* Read BCA */
    if (!__mirage_disc_mds_parse_bca(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse BCA!\n", __func__);
        return FALSE;
    }
    
    /* Sessions */
    if (!__mirage_disc_mds_parse_sessions(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse sessions!\n", __func__);
        return FALSE;
    }
    
    /* DPM data */
    if (!__mirage_disc_mds_parse_dpm_data(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse DPM data!\n", __func__);
        return FALSE;
    }
    
    return TRUE;
}

/******************************************************************************\
 *                     MIRAGE_Disc methods implementation                     *
\******************************************************************************/
static gboolean __mirage_disc_mds_get_parser_info (MIRAGE_Disc *self, MIRAGE_ParserInfo **parser_info, GError **error) {
    MIRAGE_Disc_MDSPrivate *_priv = MIRAGE_DISC_MDS_GET_PRIVATE(self);
    *parser_info = _priv->parser_info;
    return TRUE;
}

static gboolean __mirage_disc_mds_can_load_file (MIRAGE_Disc *self, gchar *filename, GError **error) {
    MIRAGE_Disc_MDSPrivate *_priv = MIRAGE_DISC_MDS_GET_PRIVATE(self);

    /* Does file exist? */
    if (!g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
        return FALSE;
    }
    
    /* FIXME: Should support anything that passes the MEDIA DESCRIPTOR test and 
       ignore suffixes? */
    if (!mirage_helper_match_suffixes(filename, _priv->parser_info->suffixes)) {
        return FALSE;
    }

    /* Also check that there's appropriate signature */
    FILE *file = g_fopen(filename, "r");
    if (!file) {
        return FALSE;
    }
    
    gchar sig[16] = {0};
    fread(sig, 16, 1, file);
    fclose(file);
    if (memcmp(sig, "MEDIA DESCRIPTOR", 16)) {
        return FALSE;
    }
    
    return TRUE;
}

static gboolean __mirage_disc_mds_load_image (MIRAGE_Disc *self, gchar **filenames, GError **error) {
    MIRAGE_Disc_MDSPrivate *_priv = MIRAGE_DISC_MDS_GET_PRIVATE(self);
    guint8 *cur_ptr = NULL;
    GError *local_error = NULL;
    gboolean succeeded = TRUE;
    
    /* For now, MDS parser supports only one-file images */
    if (g_strv_length(filenames) > 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: only single-file images supported!\n", __func__);
        mirage_error(MIRAGE_E_SINGLEFILE, error);
        return FALSE;
    }
    
    mirage_disc_set_filenames(self, filenames, NULL);
    _priv->mds_filename = g_strdup(filenames[0]);
    
    /* Map the file using GLib's GMappedFile */
    _priv->mds_mapped = g_mapped_file_new(filenames[0], FALSE, &local_error);
    if (!_priv->mds_mapped) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to map file '%s': %s!\n", __func__, filenames[0], local_error->message);
        g_error_free(local_error);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
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
        case MDS_DISCMEDIA_CD:
        case MDS_DISCMEDIA_CD_R:
        case MDS_DISCMEDIA_CD_RW: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD-ROM image\n", __func__);
            mirage_disc_set_medium_type(self, MIRAGE_MEDIUM_CD, NULL);
            succeeded = __mirage_disc_mds_load_disc(self, error);
            break;
        }
        case MDS_DISCMEDIA_DVD: 
        case MDS_DISCMEDIA_DVD_MINUS_R: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: DVD-ROM image\n", __func__);
            mirage_disc_set_medium_type(self, MIRAGE_MEDIUM_DVD, NULL);
            succeeded = __mirage_disc_mds_load_disc(self, error);            
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
        
    return succeeded;    
}


/******************************************************************************\
 *                                Object init                                 *
\******************************************************************************/
/* Our parent class */
static MIRAGE_DiscClass *parent_class = NULL;

static void __mirage_disc_mds_instance_init (GTypeInstance *instance, gpointer g_class) {
    MIRAGE_Disc_MDS *self = MIRAGE_DISC_MDS(instance);
    MIRAGE_Disc_MDSPrivate *_priv = MIRAGE_DISC_MDS_GET_PRIVATE(self);
    
    /* Create parser info */
    _priv->parser_info = mirage_helper_create_parser_info(
        "PARSER-MDS",
        "MDS Image Parser",
        "1.0.0",
        "Rok Mandeljc",
        FALSE,
        "MDS (Media descriptor) images",
        3, ".mds", ".xmd", NULL
    );
    
    return;
}

static void __mirage_disc_mds_finalize (GObject *obj) {
    MIRAGE_Disc_MDS *self_mds = MIRAGE_DISC_MDS(obj);
    MIRAGE_Disc_MDSPrivate *_priv = MIRAGE_DISC_MDS_GET_PRIVATE(self_mds);
    
    MIRAGE_DEBUG(self_mds, MIRAGE_DEBUG_GOBJECT, "%s:\n", __func__);

    g_free(_priv->mds_filename);
    
    /* Free parser info */
    mirage_helper_destroy_parser_info(_priv->parser_info);

    /* Chain up to the parent class */
    MIRAGE_DEBUG(self_mds, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __func__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __mirage_disc_mds_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_DiscClass *class_disc = MIRAGE_DISC_CLASS(g_class);
    MIRAGE_Disc_MDSClass *klass = MIRAGE_DISC_MDS_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Disc_MDSPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_disc_mds_finalize;
    
    /* Initialize MIRAGE_Disc methods */
    class_disc->get_parser_info = __mirage_disc_mds_get_parser_info;
    class_disc->can_load_file = __mirage_disc_mds_can_load_file;
    class_disc->load_image = __mirage_disc_mds_load_image;
        
    return;
}

GType mirage_disc_mds_get_type (GTypeModule *module) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_Disc_MDSClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_disc_mds_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Disc_MDS),
            0,      /* n_preallocs */
            __mirage_disc_mds_instance_init    /* instance_init */
        };
        
        type = g_type_module_register_type(module, MIRAGE_TYPE_DISC, "MIRAGE_Disc_MDS", &info, 0);
    }
    
    return type;
}
