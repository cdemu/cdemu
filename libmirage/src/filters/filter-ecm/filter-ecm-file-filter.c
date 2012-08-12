/*
 *  libMirage: ECM file filter: File filter object
 *  Copyright (C) 2012 Rok Mandeljc
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

#include "filter-ecm.h"

#define __debug__ "ECM-FileFilter"


typedef struct
{
    guint8 type;
    gint num;

    goffset raw_offset;
    gsize raw_size;

    goffset offset;
    gsize size;
} ECM_Part;


static const guint8 sync_pattern[] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_FILE_FILTER_ECM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FILE_FILTER_ECM, MIRAGE_FileFilter_ECMPrivate))

struct _MIRAGE_FileFilter_ECMPrivate
{
    goffset cur_position;

    gint cur_part_idx;
    const ECM_Part *cur_part;

    guint64 file_size;

    /* Part list */
    ECM_Part *parts;
    gint num_parts;

    /* Cache */
    gint cache_part_idx;
    gint cache_block_idx;
    guint8 cache_buffer[2352];
};


/**********************************************************************\
 *                           Part indexing                            *
\**********************************************************************/
static gboolean mirage_file_filter_ecm_build_index (MIRAGE_FileFilter_ECM *self, GError **error)
{
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));

    gint8 type;
    guint32 num;

    goffset raw_offset;
    gsize raw_size;
    gsize size;

    /* Position behind the signature */
    if (!g_seekable_seek(G_SEEKABLE(stream), 4, G_SEEK_SET, NULL, NULL)) {
        mirage_error(MIRAGE_E_READFAILED, error);
        return FALSE;
    }

    while (1) {
        guint8 c;
        gint bits = 5;

        /* Read type and number of sectors */
        if (g_input_stream_read(G_INPUT_STREAM(stream), &c, sizeof(c), NULL, NULL) != sizeof(c)) {
            mirage_error(MIRAGE_E_READFAILED, error);
            return FALSE;
        }

        type = c & 3;
        num = (c >> 2) & 0x1F;

        while (c & 0x80) {
            if (g_input_stream_read(G_INPUT_STREAM(stream), &c, sizeof(c), NULL, NULL) != sizeof(c)) {
                mirage_error(MIRAGE_E_READFAILED, error);
                return FALSE;
            }

            if ( (bits > 31) || ((guint32)(c & 0x7F)) >= (((guint32)0x80000000LU) >> (bits-1)) ) {
                g_print("Corrupt ECM file; invalid sector count\n");
                mirage_error(MIRAGE_E_GENERIC, error);
                return FALSE;
            }
            num |= ((guint32)(c & 0x7F)) << bits;
            bits += 7;
        }

        /* End indicator */
        if (num == 0xFFFFFFFF) {
            break;
        }

        num++;

        /* Decode type */
        switch (type) {
            case 0: {
                raw_size = num;
                size = num;
                break;
            }
            case 1: {
                raw_size = num * (3+2048);
                size = num * 2352;
                break;
            }
            case 2: {
                raw_size = num * (4+2048);
                size = num * 2336;
                break;
            }
            case 3: {
                raw_size = num * (4+2324);
                size = num * 2336;
                break;
            }
            default: {
                g_print("Unhandled type %d!\n", type);
                mirage_error(MIRAGE_E_GENERIC, error);
                return FALSE;
            }
        }

        /* Get raw offset, then skip raw data */
        raw_offset = g_seekable_tell(G_SEEKABLE(stream));
        g_seekable_seek(G_SEEKABLE(stream), raw_size, G_SEEK_CUR, NULL, NULL);

        /* Append to list of parts */
        self->priv->num_parts++;
        self->priv->parts = g_renew(ECM_Part, self->priv->parts, self->priv->num_parts);

        self->priv->parts[self->priv->num_parts-1].num = num;
        self->priv->parts[self->priv->num_parts-1].type = type;

        self->priv->parts[self->priv->num_parts-1].raw_offset = raw_offset;
        self->priv->parts[self->priv->num_parts-1].raw_size = raw_size;

        self->priv->parts[self->priv->num_parts-1].offset = self->priv->file_size;
        self->priv->parts[self->priv->num_parts-1].size = size;

        /* Original file size */
        self->priv->file_size += size;
    }

    g_print("Number of blocks: %d\n", self->priv->num_parts);
    g_print("Original file size: %ld\n", self->priv->file_size);

    /* At least one part must be present */
    if (!self->priv->num_parts) {
        g_print("No parts in file!\n");
        mirage_error(MIRAGE_E_GENERIC, error);
        return FALSE;
    }

    self->priv->cur_position = 0;
    self->priv->cur_part_idx = 0;
    self->priv->cur_part = &self->priv->parts[self->priv->cur_part_idx];

    return TRUE;
}



/**********************************************************************\
 *                              Seeking                               *
\**********************************************************************/
static inline gboolean is_within_part (goffset position, const ECM_Part *part, gboolean last_part)
{
    if (last_part) {
        return position >= part->offset;
    } else {
        return position >= part->offset && position < (part->offset + part->size);
    }
}


static gboolean mirage_file_filter_ecm_set_current_position (MIRAGE_FileFilter_ECM *self, goffset new_position, GError **error)
{
    /* If new position matches current position, do nothing */
    if (new_position == self->priv->cur_position) {
        return TRUE;
    }

    /* Validate new position */
    if (new_position < 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Seek before beginning of file not allowed!");
        return FALSE;
    }

    if (new_position > self->priv->file_size) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Seek beyond end of file not allowed!");
        return FALSE;
    }

    /* Find the corresponding part */
    if (is_within_part(new_position, self->priv->cur_part, FALSE)) {
        /* Position still within current part; nothing to do */
    } else if (is_within_part(new_position, &self->priv->parts[0], FALSE)) {
        /* Position within first part */
        self->priv->cur_part_idx = 0;
    } else if (is_within_part(new_position, &self->priv->parts[self->priv->num_parts-1], TRUE)) {
        /* Position within last part */
        self->priv->cur_part_idx = self->priv->num_parts - 1;
    } else {
        gint i;

        /* Seek part-by-part in appropriate direction (do not check first and last part, though) */
        if (new_position < self->priv->cur_position) {
            /* Seek backward */
            for (i = self->priv->cur_part_idx; i > 0; i--) {
                if (is_within_part(new_position, &self->priv->parts[i], FALSE)) {
                    self->priv->cur_part_idx = i;
                    break;
                }
            }
        } else {
            /* Seek foward */
            for (i = self->priv->cur_part_idx; i < self->priv->num_parts-1; i++) {
                if (is_within_part(new_position, &self->priv->parts[i], FALSE)) {
                    self->priv->cur_part_idx = i;
                    break;
                }
            }
        }
    }
    self->priv->cur_part = &self->priv->parts[self->priv->cur_part_idx];

    g_print("=> Position found in part #%d\n", self->priv->cur_part_idx);

    /* Set new position */
    self->priv->cur_position = new_position;

    return TRUE;
}


/**********************************************************************\
 *                         Reading from parts                         *
\**********************************************************************/
static gssize mirage_filter_ecm_read_single_block_from_part (MIRAGE_FileFilter_ECM *self, guint8 *buffer, gsize count)
{
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));

    goffset part_offset, buffer_offset, stream_offset;
    gint block_idx;
    gssize read_len;

    gint block_size, raw_block_size, skip_bytes;

    g_print("%s: current position: %ld (part #%d, offset: %ld, size: %ld, raw offset: %ld, raw size: %ld), type %d, read %d bytes\n", __func__, self->priv->cur_position, self->priv->cur_part_idx, self->priv->cur_part->offset, self->priv->cur_part->size, self->priv->cur_part->raw_offset, self->priv->cur_part->raw_size, self->priv->cur_part->type, count);

    /* Compute part offset within filter stream */
    part_offset = self->priv->cur_position - self->priv->cur_part->offset;
    g_print("%s: offset within part: %ld\n", __func__, part_offset);

    /* Decode types */
    switch (self->priv->cur_part->type) {
        case ECM_RAW: {
            g_print("%s: => part type: raw\n", __func__);

            /* This one is different from others, because we read data directly */
            stream_offset = self->priv->cur_part->raw_offset + part_offset;

            g_print("%s: seeking to %ld in underlying stream\n", __func__, part_offset);
            if (!g_seekable_seek(G_SEEKABLE(stream), stream_offset, G_SEEK_SET, NULL, NULL)) {
                g_print("%s: FAILED TO SEEK TO %ld IN UNDERLYING STREAM!\n", __func__, part_offset);
                return -1;
            }

            /* Read all available data */
            count = MIN(count, self->priv->cur_part->size - part_offset);

            g_print("%s: reading %d bytes from underlying stream\n", __func__, count);
            read_len = g_input_stream_read(G_INPUT_STREAM(stream), buffer, count, NULL, NULL);
            if (read_len != count) {
                g_print("%s: FAILED TO READ %d BYTES FROM UNDERLYING STREAM!\n", __func__, count);
                return -1;
            }

            return read_len;
        }
        case ECM_MODE1_2352: {
            g_print("%s: => part type: mode 1\n", __func__);
            block_size = 2352;
            raw_block_size = 3+2048;
            skip_bytes = 0;
            break;
        }
        case ECM_MODE2_FORM1_2336: {
            g_print("%s: => part type: mode 2 form 1\n", __func__);
            block_size = 2336;
            raw_block_size = 4+2048;
            skip_bytes = 16;
            break;
        }
        case ECM_MODE2_FORM2_2336: {
            g_print("%s: => part type: mode 2 form 2\n", __func__);
            block_size = 2336;
            raw_block_size = 4+2324;
            skip_bytes = 16;
            break;
        }
        default: {
            return -1;
        }
    }

    /* Compute the block number, and offset within buffer */
    block_idx = part_offset / block_size;
    buffer_offset = part_offset % block_size;

    g_print("%s: => reading from block %d; buffer offset: %d\n", __func__, block_idx, buffer_offset);

    /* If this particular block in this particular part is not cached,
       read the data and reconstruct ECC/EDC */
    if (self->priv->cache_part_idx != self->priv->cur_part_idx || self->priv->cache_block_idx != block_idx) {
        /* Compute offset within underlying stream */
        stream_offset = self->priv->cur_part->raw_offset + block_idx*raw_block_size;

        g_print("%s: seeking to %ld in underlying stream\n", __func__, part_offset);
        if (!g_seekable_seek(G_SEEKABLE(stream), stream_offset, G_SEEK_SET, NULL, NULL)) {
            g_print("%s: FAILED TO SEEK TO %ld IN UNDERLYING STREAM!\n", __func__, part_offset);
            return -1;
        }

        /* Read and reconstruct sector data */
        switch (self->priv->cur_part->type) {
            case ECM_MODE1_2352: {
                /* Read data */
                if (g_input_stream_read(G_INPUT_STREAM(stream), self->priv->cache_buffer+0x00C, 0x003, NULL, NULL) != 0x003) {
                    g_print("%s: FAILED TO READ %d BYTES FROM UNDERLYING STREAM!\n", __func__, 0x003);
                    return -1;
                }
                if (g_input_stream_read(G_INPUT_STREAM(stream), self->priv->cache_buffer+0x010, 0x800, NULL, NULL) != 0x800) {
                    g_print("%s: FAILED TO READ %d BYTES FROM UNDERLYING STREAM!\n", __func__, 0x800);
                    return -1;
                }

                /* Set sync pattern */
                memcpy(self->priv->cache_buffer, sync_pattern, sizeof(sync_pattern));

                /* Set mode byte in header */
                self->priv->cache_buffer[0x00F] = 1;

                /* Generate EDC */
                mirage_helper_sector_edc_ecc_compute_edc_block(self->priv->cache_buffer+0x000, 0x810, self->priv->cache_buffer+0x810);

                /* Generate ECC P/Q codes */
                mirage_helper_sector_edc_ecc_compute_ecc_block(self->priv->cache_buffer+0x00C, 86, 24, 2, 86, self->priv->cache_buffer+0x81C); /* P */
                mirage_helper_sector_edc_ecc_compute_ecc_block(self->priv->cache_buffer+0x00C, 52, 43, 86, 88, self->priv->cache_buffer+0x8C8); /* Q */

                break;
            }
            case ECM_MODE2_FORM1_2336: {
                /* Read data */
                if (g_input_stream_read(G_INPUT_STREAM(stream), self->priv->cache_buffer+0x014, 0x804, NULL, NULL) != 0x804) {
                    g_print("%s: FAILED TO READ %d BYTES FROM UNDERLYING STREAM!\n", __func__, 0x804);
                    return -1;
                }

                /* Make sure that header fields are zeroed out */
                memset(self->priv->cache_buffer+0x00C, 0, 4);

                /* Duplicate subheader */
                memcpy(self->priv->cache_buffer+0x010, self->priv->cache_buffer+0x014, 4);

                /* Generate EDC */
                mirage_helper_sector_edc_ecc_compute_edc_block(self->priv->cache_buffer+0x010, 0x808, self->priv->cache_buffer+0x818);

                /* Generate ECC P/Q codes */
                mirage_helper_sector_edc_ecc_compute_ecc_block(self->priv->cache_buffer+0x00C, 86, 24, 2, 86, self->priv->cache_buffer+0x81C); /* P */
                mirage_helper_sector_edc_ecc_compute_ecc_block(self->priv->cache_buffer+0x00C, 52, 43, 86, 88, self->priv->cache_buffer+0x8C8); /* Q */

                break;
            }
            case ECM_MODE2_FORM2_2336: {
                /* Read data */
                if (g_input_stream_read(G_INPUT_STREAM(stream), self->priv->cache_buffer+0x014, 0x918, NULL, NULL) != 0x918) {
                    g_print("%s: FAILED TO READ %d BYTES FROM UNDERLYING STREAM!\n", __func__, 0x918);
                    return -1;
                }

                /* Duplicate subheader */
                memcpy(self->priv->cache_buffer+0x010, self->priv->cache_buffer+0x014, 4);

                /* Generate EDC */
                mirage_helper_sector_edc_ecc_compute_edc_block(self->priv->cache_buffer+0x010, 0x91C, self->priv->cache_buffer+0x92C);

                break;
            }
            default: {
                return -1;
            }
        }

        self->priv->cache_part_idx = self->priv->cur_part_idx;
        self->priv->cache_block_idx = block_idx;
    }

    /* Copy from cache buffer */
    read_len = MIN(count, block_size - buffer_offset);
    memcpy(buffer, self->priv->cache_buffer+skip_bytes+buffer_offset, read_len);

    return read_len;
}


/**********************************************************************\
 *              MIRAGE_FileFilter methods implementations             *
\**********************************************************************/
static gboolean mirage_file_filter_ecm_can_handle_data_format (MIRAGE_FileFilter *_self, GError **error)
{
    MIRAGE_FileFilter_ECM *self = MIRAGE_FILE_FILTER_ECM(_self);
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));

    guint8 sig[4];

    /* Look for "ECM " signature at the beginning */
    g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);
    if (g_input_stream_read(G_INPUT_STREAM(stream), sig, sizeof(sig), NULL, NULL) != sizeof(sig)) {
        mirage_error(MIRAGE_E_READFAILED, error);
        return FALSE;
    }

    /* Check signature */
    if (memcmp(sig, "ECM\x00", sizeof(sig))) {
        mirage_error(MIRAGE_E_CANTHANDLE, error);
        return FALSE;
    }

    /* Build index */
    if (!mirage_file_filter_ecm_build_index(self, error)) {
        return FALSE;
    }

    g_print("%s: ECM file found!\n", __debug__);
    return TRUE;
}


static gssize mirage_file_filter_ecm_read (MIRAGE_FileFilter *_self, void *buffer, gsize count, GError **error)
{
    MIRAGE_FileFilter_ECM *self = MIRAGE_FILE_FILTER_ECM(_self);

    gssize total_read, read_len;
    guint8 *ptr = buffer;

    g_print("%s: %p, %p, %ld, %p\n", __func__, _self, buffer, count, error);

    /* Read until all is read */
    total_read = 0;

    while (count > 0) {
        /* Read a single block from current part */
        read_len = mirage_filter_ecm_read_single_block_from_part(self, ptr, count);

        if (read_len == -1) {
            g_print("Byargh!\n");
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Internal filter error");
            return -1;
        }

        ptr += read_len;
        total_read += read_len;
        count -= read_len;

        g_print("= > read %ld bytes... %ld remaining\n", read_len, count);

        /* Update position */
        if (!mirage_file_filter_ecm_set_current_position(self, self->priv->cur_position+read_len, error)) {
            g_print("Byargh2!\n");
            return -1;
        }
    }

    g_debug("YAY!\n");
    return total_read;
}


static goffset mirage_file_filter_ecm_tell (MIRAGE_FileFilter *_self)
{
    MIRAGE_FileFilter_ECM *self = MIRAGE_FILE_FILTER_ECM(_self);
    return self->priv->cur_position;
}

static gboolean mirage_file_filter_ecm_seek (MIRAGE_FileFilter *_self, goffset offset, GSeekType type, GError **error)
{
    MIRAGE_FileFilter_ECM *self = MIRAGE_FILE_FILTER_ECM(_self);
    goffset new_position;

    /* Compute new position */
    switch (type) {
        case G_SEEK_SET: {
            new_position = offset;
            break;
        }
        case G_SEEK_CUR: {
            new_position = self->priv->cur_position + offset;
            break;
        }
        case G_SEEK_END: {
            new_position = self->priv->file_size + offset;
            break;
        }
        default: {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid seek type.");
            return FALSE;
        }
    }

    g_print("=> seeking to position %ld\n", new_position);

    /* Seek */
    if (!mirage_file_filter_ecm_set_current_position(self, new_position, error)) {
        return FALSE;
    }

    return TRUE;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE(MIRAGE_FileFilter_ECM, mirage_file_filter_ecm, MIRAGE_TYPE_FILE_FILTER);

void mirage_file_filter_ecm_type_register (GTypeModule *type_module)
{
    return mirage_file_filter_ecm_register_type(type_module);
}


static void mirage_file_filter_ecm_init (MIRAGE_FileFilter_ECM *self)
{
    self->priv = MIRAGE_FILE_FILTER_ECM_GET_PRIVATE(self);

    mirage_file_filter_generate_file_filter_info(MIRAGE_FILE_FILTER(self),
        "FILTER-ECM",
        "ECM File Filter"
    );

    self->priv->file_size = 0;

    self->priv->cur_position = 0;
    self->priv->cur_part_idx = 0;
    self->priv->cur_part = NULL;

    self->priv->num_parts = 0;
    self->priv->parts = NULL;

    self->priv->cache_part_idx = -1;
    self->priv->cache_block_idx = -1;
}

static void mirage_file_filter_ecm_finalize (GObject *gobject)
{
    MIRAGE_FileFilter_ECM *self = MIRAGE_FILE_FILTER_ECM(gobject);

    g_free(self->priv->parts);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_file_filter_ecm_parent_class)->finalize(gobject);
}

static void mirage_file_filter_ecm_class_init (MIRAGE_FileFilter_ECMClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MIRAGE_FileFilterClass *file_filter_class = MIRAGE_FILE_FILTER_CLASS(klass);

    gobject_class->finalize = mirage_file_filter_ecm_finalize;

    file_filter_class->can_handle_data_format = mirage_file_filter_ecm_can_handle_data_format;

    file_filter_class->read = mirage_file_filter_ecm_read;

    file_filter_class->tell = mirage_file_filter_ecm_tell;
    file_filter_class->seek = mirage_file_filter_ecm_seek;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_FileFilter_ECMPrivate));
}

static void mirage_file_filter_ecm_class_finalize (MIRAGE_FileFilter_ECMClass *klass G_GNUC_UNUSED)
{
}