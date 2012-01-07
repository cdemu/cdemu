/*
 *  libMirage: CIF image parser: Parser object
 *  Copyright (C) 2008-2012 Henrik Stokseth
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

#define __debug__ "CIF-Parser"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_PARSER_CIF_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_PARSER_CIF, MIRAGE_Parser_CIFPrivate))

struct _MIRAGE_Parser_CIFPrivate
{
    GObject *disc;

    GList *block_index;
    gint  block_index_entries;

    gchar *cif_filename;

    GMappedFile *cif_mapped;
    guint8 *cif_data;
};


static gboolean mirage_parser_cif_build_block_index (MIRAGE_Parser_CIF *self, GError **error G_GNUC_UNUSED)
{
    GList *blockindex = NULL;
    CIFBlockIndexEntry *blockentry;
    gint num_blocks;
    gint index;
    guint8 *cur_ptr;
    CIF_BlockHeader *block;

    /* Populate block index */
    cur_ptr = self->priv->cif_data;
    index = 0;
    do {
        block = MIRAGE_CAST_PTR(cur_ptr, 0, CIF_BlockHeader *);
        if (memcmp(block->signature, "RIFF", 4)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: expected 'RIFF' block signature!\n", __debug__);
            return FALSE;
        }

        blockentry = g_new(CIFBlockIndexEntry, 1);
        if (!blockentry) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to allocate memory for block index!\n", __debug__);
            return FALSE;
        }
        blockentry->offset = cur_ptr - self->priv->cif_data;
        blockentry->block_header = block;
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Block: %2i, ID: %4s, start: 0x%X, length: %i (0x%X).\n", \
            __debug__, index, block->block_id, blockentry->offset, block->length, block->length);

        /* Got sub-blocks? */
        if (!memcmp(block->block_id, "disc", 4) || !memcmp(block->block_id, "ofs ", 4)) {
            GList *subblockindex = NULL;
            CIFSubBlockIndexEntry *subblockentry;
            gint num_subblocks;
            guint8 *cur_ptr_sub;

            if (!memcmp(block->block_id, "disc", 4)) {
                cur_ptr_sub = cur_ptr + CIF_BLOCK_LENGTH_ADJUST + sizeof(CIF_DISC_HeaderBlock);
            } else if (!memcmp(block->block_id, "ofs ", 4)) {
                cur_ptr_sub = cur_ptr + CIF_BLOCK_LENGTH_ADJUST + sizeof(CIF_OFS_HeaderBlock);
            }

            do {
                subblockentry = g_new0(CIFSubBlockIndexEntry, 1);
                if (!subblockentry) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to allocate memory for sub-block index!\n", __debug__);
                    return FALSE;
                }
                subblockentry->start = cur_ptr_sub;
                subblockentry->offset = cur_ptr_sub - self->priv->cif_data;
                if (!memcmp(block->block_id, "disc", 4)) {
                    CIF_DISC_SubBlock *subblock = MIRAGE_CAST_PTR(cur_ptr_sub, 0, CIF_DISC_SubBlock *);

                    subblockentry->length = subblock->track.length;
                } else if(!memcmp(block->block_id, "ofs ", 4)) {
                    /* CIF_OFS_SubBlock *subblock = MIRAGE_CAST_PTR(cur_ptr_sub, 0, CIF_OFS_SubBlock *); */

                    subblockentry->length = 22;
                }
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Sub-Block: start: 0x%X, length: %i (0x%X).\n", \
                    __debug__, subblockentry->offset, subblockentry->length, subblockentry->length);

                /* Add entry to list */
                subblockindex = g_list_prepend(subblockindex, subblockentry);

                /* Get ready for next block */
                cur_ptr_sub += subblockentry->length;
            } while (cur_ptr_sub < (guint8 *) block + block->length + CIF_BLOCK_LENGTH_ADJUST);
            subblockindex = g_list_reverse(subblockindex);
            num_subblocks = g_list_length(subblockindex);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: counted %i sub-blocks.\n", __debug__, num_subblocks);

            /* Link sub-block index to block entry */
            blockentry->subblock_index = subblockindex;
            blockentry->num_subblocks = num_subblocks;

        /* Seems we don't have sub-blocks */
        } else {
            blockentry->subblock_index = NULL;
            blockentry->num_subblocks = 0; /* This block has no sub-blocks */
        }

        /* Add entry to list */
        blockindex = g_list_prepend(blockindex, blockentry);

        /* Get ready for next block */
        index++;
        cur_ptr += (block->length + CIF_BLOCK_LENGTH_ADJUST);
        if (block->length % 2) cur_ptr++; /* Padding byte if length is not even */
    } while ((cur_ptr - self->priv->cif_data) < g_mapped_file_get_length(self->priv->cif_mapped));
    blockindex = g_list_reverse(blockindex);
    num_blocks = g_list_length(blockindex);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: counted %i 'RIFF' blocks.\n", __debug__, num_blocks);

    /* Link to block index */
    self->priv->block_index = blockindex;
    self->priv->block_index_entries = num_blocks;

    return TRUE;
}

static gboolean mirage_parser_cif_destroy_block_index (MIRAGE_Parser_CIF *self, GError **error G_GNUC_UNUSED)
{
    if (self->priv->block_index) {
        GList *entry;
        G_LIST_FOR_EACH(entry, self->priv->block_index) {
            CIFBlockIndexEntry *list_entry = entry->data;

            if (list_entry->subblock_index) {
                GList *subentry;

                G_LIST_FOR_EACH(subentry, list_entry->subblock_index) {
                    g_free(subentry->data);
                }
            }

            g_list_free(list_entry->subblock_index);
            g_free(list_entry);
        }

        g_list_free(self->priv->block_index);

        self->priv->block_index = NULL;
        self->priv->block_index_entries = 0;
    }

    return TRUE;
}

static gint find_block_by_id (const CIFBlockIndexEntry *blockentry, const gchar *block_id)
{
    return memcmp(blockentry->block_header->block_id, block_id, 4);
}

static CIFBlockIndexEntry *mirage_parser_cif_find_block_entry (MIRAGE_Parser_CIF *self, gchar *block_id, GError **error G_GNUC_UNUSED)
{
    GList *entry = g_list_find_custom(self->priv->block_index, block_id, (GCompareFunc)find_block_by_id);

    if (entry) {
        return entry->data;
    } else {
        return NULL;
    }
}

static gint mirage_parser_cif_convert_track_mode (MIRAGE_Parser_CIF *self, guint32 mode, guint16 sector_size)
{
    if (mode == CIF_MODE_AUDIO) {
        switch(sector_size) {
            case 2352: {
                return MIRAGE_MODE_AUDIO;
            }
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown sector size %i!\n", __debug__, sector_size);
                return -1;
            }
        }
    } else if(mode == CIF_MODE_MODE1) {
        switch(sector_size) {
            case 2048: {
                return MIRAGE_MODE_MODE1;
            }
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown sector size %i!\n", __debug__, sector_size);
                return -1;
            }
        }
    } else if(mode == CIF_MODE_MODE2_FORM1) {
        switch(sector_size) {
            case 2056: {
                return MIRAGE_MODE_MODE2_FORM1;
            }
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown sector size %i!\n", __debug__, sector_size);
                return -1;
            }
        }
    } else if(mode == CIF_MODE_MODE2_FORM2) {
        switch(sector_size) {
            case 2332: {
                return MIRAGE_MODE_MODE2_MIXED;
            }
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown sector size %i!\n", __debug__, sector_size);
                return -1;
            }
        }
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown track mode 0x%X!\n", __debug__, mode);
        return -1;
    }
}

static gboolean mirage_parser_cif_parse_track_entries (MIRAGE_Parser_CIF *self, GError **error)
{
    CIFBlockIndexEntry *disc_block_ptr;
    CIFBlockIndexEntry *ofs_block_ptr;

    CIFSubBlockIndexEntry *disc_subblock_entry;
    CIF_DISC_SubBlock *disc_subblock_data;
    CIFSubBlockIndexEntry *ofs_subblock_entry;
    CIF_OFS_SubBlock *ofs_subblock_data;
    CIF_BlockHeader *track_block;
    /*guint8 *track_block_data;*/

    GObject *cur_session = NULL;

    gint tracks;
    gint track;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading track blocks\n", __debug__);

    /* Get current session */
    if (!mirage_disc_get_session_by_index(MIRAGE_DISC(self->priv->disc), -1, &cur_session, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current session!\n", __debug__);
        return FALSE;
    }

    /* Initialize parser block and ofs block pointers first */
    disc_block_ptr = mirage_parser_cif_find_block_entry(self, "disc", error);
    ofs_block_ptr = mirage_parser_cif_find_block_entry(self, "ofs ", error);
    if (!(disc_block_ptr && ofs_block_ptr)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: 'disc' or 'ofs' blocks not located. Can not proceed.\n", __debug__);
        g_object_unref(cur_session);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }

    /* Fetch number of tracks */
    disc_subblock_entry = g_list_nth_data(disc_block_ptr->subblock_index, 0);
    disc_subblock_data = (CIF_DISC_SubBlock *)disc_subblock_entry->start;
    tracks = disc_subblock_data->first.tracks;

    /* Read track entries */
    for (track = 0; track < tracks; track++) {
        ofs_subblock_entry = g_list_nth_data(ofs_block_ptr->subblock_index, track);
        ofs_subblock_data = (CIF_OFS_SubBlock *) ofs_subblock_entry->start;
        disc_subblock_entry = g_list_nth_data(disc_block_ptr->subblock_index, track + 2);
        disc_subblock_data = (CIF_DISC_SubBlock *) disc_subblock_entry->start;
        track_block = (CIF_BlockHeader *) (self->priv->cif_data + ofs_subblock_data->ofs_offset + OFS_OFFSET_ADJUST);
        /*track_block_data = (guint8 *) (self->priv->cif_data + ofs_subblock_data->ofs_offset);*/

        gchar *track_type = ofs_subblock_data->block_id;
        guint16 track_mode = disc_subblock_data->track.mode;
        guint32 sectors = disc_subblock_data->track.sectors; /* NOTE: not always correct! */
        guint16 sector_size = disc_subblock_data->track.sector_size; /* NOTE: not always correct! */
        guint16 real_sector_size = sector_size;
        guint32 track_start = ofs_subblock_data->ofs_offset + sizeof(CIF_TRACK_HeaderBlock);
        guint32 track_length = track_block->length - sizeof(CIF_TRACK_HeaderBlock);
        gchar *isrc = NULL;
        gchar *title = NULL;

        GObject *cur_track = NULL;

        /* workaround for mode2/cdrom-xa modes */
        if (track_mode == CIF_MODE_MODE2_FORM1) {
            track_start += 8;
            track_length -= 8;
        } else if (track_mode == CIF_MODE_MODE2_FORM2) {
            real_sector_size = 2332;
        }

        /* Read main blocks related to track */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Track %2i:\n", __debug__, track);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   type: %.4s\n", __debug__, track_type);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   mode: %i\n", __debug__, track_mode);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   sector size: %i\n", __debug__, sector_size);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   real sector size: %i\n", __debug__, real_sector_size);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   sectors: %i\n", __debug__, sectors);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   start: %i (0x%X)\n", __debug__, track_start, track_start);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   length: %i (0x%X)\n", __debug__, track_length, track_length);

        /* workaround: cdrom-xa/mode2 has incorrect num. sectors */
        if ((track_mode == CIF_MODE_MODE2_FORM1) || (track_mode == CIF_MODE_MODE2_FORM2)) {
            sectors = track_length / real_sector_size;
            if (track_length % real_sector_size) sectors++; /* round up */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s:   Workaround enabled; Corrected sector count: %i\n", __debug__, sectors);
        }

        /* Add track */
        if (!mirage_session_add_track_by_number(MIRAGE_SESSION(cur_session), track+1, &cur_track, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __debug__);
            g_object_unref(cur_session);
            return FALSE;
        }

        if (track_mode == CIF_MODE_AUDIO) {
            isrc = disc_subblock_data->track.isrc;
            title = disc_subblock_data->track.title;

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   ISRC: %.12s\n", __debug__, isrc);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   Title: %s\n", __debug__, title);

            /* Set ISRC */
            if (isrc[0]) {
                mirage_track_set_isrc(MIRAGE_TRACK(cur_track), isrc, NULL);
            }
        }

        gint converted_mode = mirage_parser_cif_convert_track_mode(self, track_mode, real_sector_size);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   converted mode: 0x%X\n", __debug__, converted_mode);
        mirage_track_set_mode(MIRAGE_TRACK(cur_track), converted_mode, NULL);

        /* Pregap fragment */
        if (track == 0) {
            GObject *pregap_fragment = libmirage_create_fragment(MIRAGE_TYPE_FRAG_IFACE_NULL, "NULL", error);
            if (!pregap_fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create NULL fragment!\n", __debug__);
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
        GObject *data_fragment = libmirage_create_fragment(MIRAGE_TYPE_FRAG_IFACE_BINARY, self->priv->cif_filename, error);
        if (!data_fragment) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create fragment!\n", __debug__);
            g_object_unref(cur_track);
            g_object_unref(cur_session);
            return FALSE;
        }

        /* Prepare data fragment */
        FILE *tfile_handle = g_fopen(self->priv->cif_filename, "r");
        guint64 tfile_offset = (guint64)track_start;
        gint tfile_sectsize = real_sector_size;
        gint tfile_format = 0;

        if (converted_mode == MIRAGE_MODE_AUDIO) {
            tfile_format = FR_BIN_TFILE_AUDIO;
        } else {
            tfile_format = FR_BIN_TFILE_DATA;
        }

        gint fragment_len = sectors;

        mirage_fragment_set_length(MIRAGE_FRAGMENT(data_fragment), fragment_len, NULL);

        mirage_frag_iface_binary_track_file_set_handle(MIRAGE_FRAG_IFACE_BINARY(data_fragment), tfile_handle, NULL);
        mirage_frag_iface_binary_track_file_set_offset(MIRAGE_FRAG_IFACE_BINARY(data_fragment), tfile_offset, NULL);
        mirage_frag_iface_binary_track_file_set_sectsize(MIRAGE_FRAG_IFACE_BINARY(data_fragment), tfile_sectsize, NULL);
        mirage_frag_iface_binary_track_file_set_format(MIRAGE_FRAG_IFACE_BINARY(data_fragment), tfile_format, NULL);

        mirage_track_add_fragment(MIRAGE_TRACK(cur_track), -1, &data_fragment, error);

        g_object_unref(data_fragment);
        g_object_unref(cur_track);
    }
    g_object_unref(cur_session);

    return TRUE;
}

static gboolean mirage_parser_cif_load_disc (MIRAGE_Parser_CIF *self, GError **error)
{
    CIFBlockIndexEntry *disc_block_ptr;

    /* Build an index over blocks contained in the disc image */
    if (!mirage_parser_cif_build_block_index(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to build block index!\n", __debug__);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }

    /* The shitty CIF format does not support DVDs etc. */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD-ROM image\n", __debug__);
    mirage_disc_set_medium_type(MIRAGE_DISC(self->priv->disc), MIRAGE_MEDIUM_CD, NULL);

    /* CD-ROMs start at -150 as per Red Book... */
    mirage_disc_layout_set_start_sector(MIRAGE_DISC(self->priv->disc), -150, NULL);

    /* Add session */
    if (!mirage_disc_add_session_by_number(MIRAGE_DISC(self->priv->disc), 0, NULL, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __debug__);
        return FALSE;
    }

    /* Print some information about parser */
    disc_block_ptr = mirage_parser_cif_find_block_entry(self, "disc", error);
    if (!disc_block_ptr) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: \"parser\" block not located. Can not proceed.\n", __debug__);
        return FALSE;
    }

    CIFSubBlockIndexEntry *disc_subblock_entry = g_list_nth_data(disc_block_ptr->subblock_index, 0);
    CIF_DISC_SubBlock *disc_subblock_data = (CIF_DISC_SubBlock *)disc_subblock_entry->start;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Parser:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   number of tracks: %i\n", __debug__, disc_subblock_data->first.tracks);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   image type: %i\n", __debug__, disc_subblock_data->first.image_type);

    gint title_length = disc_subblock_data->first.title_length;
    if ((disc_subblock_data->first.image_type == CIF_IMAGE_AUDIO) && (title_length > 0)) {
        gchar *title_and_artist = disc_subblock_data->first.title_and_artist;
        gchar *title = g_strndup(title_and_artist, title_length);
        gchar *artist = (gchar *) (title_and_artist + title_length);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   title: %s\n", __debug__, title);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   artist: %s\n", __debug__, artist);
        g_free(title);
    }

    /* Load tracks */
    if (!mirage_parser_cif_parse_track_entries(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse track entries!\n", __debug__);
        return FALSE;
    }

    /* Destroy block index */
    if(!mirage_parser_cif_destroy_block_index(self, error)) {
        return FALSE;
    }

    return TRUE;
}

/**********************************************************************\
 *                MIRAGE_Parser methods implementation                *
\**********************************************************************/
static gboolean mirage_parser_cif_load_image (MIRAGE_Parser *_self, gchar **filenames, GObject **disc, GError **error)
{
    MIRAGE_Parser_CIF *self = MIRAGE_PARSER_CIF(_self);

    GError *local_error = NULL;
    gboolean succeeded = TRUE;
    FILE *file;
    gchar sig[4] = "";

    /* Check file signature */
    file = g_fopen(filenames[0], "r");
    if (!file) {
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }

    if (fread(sig, 4, 1, file) < 1) {
        fclose(file);
        mirage_error(MIRAGE_E_READFAILED, error);
        return FALSE;
    }

    fclose(file);

    if (memcmp(sig, "RIFF", 4)) {
        mirage_error(MIRAGE_E_CANTHANDLE, error);
        return FALSE;
    }

    /* Since RIFF is used also for other files, we also check the suffix */
    if (!mirage_helper_has_suffix(filenames[0], ".cif")) {
        mirage_error(MIRAGE_E_CANTHANDLE, error);
        return FALSE;
    }

    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_attach_child(MIRAGE_OBJECT(self), self->priv->disc, NULL);

    mirage_disc_set_filename(MIRAGE_DISC(self->priv->disc), filenames[0], NULL);
    self->priv->cif_filename = g_strdup(filenames[0]);

    /* Map the file using GLib's GMappedFile */
    self->priv->cif_mapped = g_mapped_file_new(self->priv->cif_filename, FALSE, &local_error);
    if (!self->priv->cif_mapped) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to map file '%s': %s!\n", __debug__, self->priv->cif_filename, local_error->message);
        g_error_free(local_error);
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        succeeded = FALSE;
        goto end;
    }

    self->priv->cif_data = (guint8 *)g_mapped_file_get_contents(self->priv->cif_mapped);

    /* Load parser */
    succeeded = mirage_parser_cif_load_disc(self, error);

    self->priv->cif_data = NULL;
    g_mapped_file_unref(self->priv->cif_mapped);

end:
    /* Return disc */
    mirage_object_detach_child(MIRAGE_OBJECT(self), self->priv->disc, NULL);
    if (succeeded) {
        *disc = self->priv->disc;
    } else {
        g_object_unref(self->priv->disc);
        *disc = NULL;
    }

    return succeeded;
}


/**********************************************************************\
 *                             Object init                            * 
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE(MIRAGE_Parser_CIF, mirage_parser_cif, MIRAGE_TYPE_PARSER);

void mirage_parser_cif_type_register (GTypeModule *type_module)
{
    return mirage_parser_cif_register_type(type_module);
}


static void mirage_parser_cif_init (MIRAGE_Parser_CIF *self)
{
    self->priv = MIRAGE_PARSER_CIF_GET_PRIVATE(self);

    mirage_parser_generate_parser_info(MIRAGE_PARSER(self),
        "PARSER-CIF",
        "CIF Image Parser",
        "CIF (Adaptec Easy CD Creator) images",
        "application/x-cif"
    );

    self->priv->cif_filename = NULL;
}

static void mirage_parser_cif_finalize (GObject *gobject)
{
    MIRAGE_Parser_CIF *self = MIRAGE_PARSER_CIF(gobject);

    g_free(self->priv->cif_filename);
    
    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_parser_cif_parent_class)->finalize(gobject);
}

static void mirage_parser_cif_class_init (MIRAGE_Parser_CIFClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MIRAGE_ParserClass *parser_class = MIRAGE_PARSER_CLASS(klass);

    gobject_class->finalize = mirage_parser_cif_finalize;

    parser_class->load_image = mirage_parser_cif_load_image;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_Parser_CIFPrivate));
}

static void mirage_parser_cif_class_finalize (MIRAGE_Parser_CIFClass *klass G_GNUC_UNUSED)
{
}
