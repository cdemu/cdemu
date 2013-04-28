/*
 *  libMirage: MacBinary file filter: File filter object
 *  Copyright (C) 2013 Henrik Stokseth
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "filter-macbinary.h"

#define __debug__ "MACBINARY-FileFilter"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_FILE_FILTER_MACBINARY_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FILE_FILTER_MACBINARY, MirageFileFilterMacBinaryPrivate))

struct _MirageFileFilterMacBinaryPrivate
{
    macbinary_header_t header;
    rsrc_fork_t        *rsrc_fork;

    guint16 *crctab;
};


/**********************************************************************\
 *                         CRC16-CCITT XModem                         *
\**********************************************************************/

#define CRC16POLY 0x1021 /* Generator polynomial */

/* Call this to init the fast CRC-16 calculation table */
static guint16 *crcinit(guint genpoly)
{
    guint16 *crctab = g_new(guint16, 256);

    for (gint val = 0; val <= 255; val++) {
        guint crc = val << 8;

        for (gint i = 0; i < 8; ++i) {
            crc <<= 1;

            if (crc & 0x10000) {
                crc ^= genpoly;
            }
        }

        crctab[val] = crc;
    }

    return crctab;
}

/* Calculate a CRC-16 for length bytes pointed at by buffer */
static guint16 docrc(guchar *buffer, guint length, guint16 *crctab)
{
    guint16 crc = 0;

    while (length-- > 0) {
        crc = (crc << 8) ^ crctab[(crc >> 8) ^ *buffer++];
    }

    return crc;
}


/**********************************************************************\
 *              MirageFileFilter methods implementations              *
\**********************************************************************/
static void mirage_file_filter_fixup_header(MirageFileFilterMacBinary *self)
{
    macbinary_header_t *header = &self->priv->header;

    header->vert_pos      = GUINT16_FROM_BE(header->vert_pos);
    header->horiz_pos     = GUINT16_FROM_BE(header->horiz_pos);
    header->window_id     = GUINT16_FROM_BE(header->window_id);
    header->getinfo_len   = GUINT16_FROM_BE(header->getinfo_len);
    header->secondary_len = GUINT16_FROM_BE(header->secondary_len);
    header->crc16         = GUINT16_FROM_BE(header->crc16);

    header->datafork_len = GUINT32_FROM_BE(header->datafork_len);
    header->resfork_len  = GUINT32_FROM_BE(header->resfork_len);
    header->created      = GUINT32_FROM_BE(header->created);
    header->modified     = GUINT32_FROM_BE(header->modified);
    header->unpacked_len = GUINT32_FROM_BE(header->unpacked_len);
}

static gboolean mirage_file_filter_macbinary_can_handle_data_format (MirageFileFilter *_self, GError **error)
{
    MirageFileFilterMacBinary *self = MIRAGE_FILE_FILTER_MACBINARY(_self);
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));

    macbinary_header_t *header = &self->priv->header;

    guint16 calculated_crc = 0;

    GString *file_name = NULL, *file_type = NULL, *creator = NULL;

    /* Read MacBinary header */
    g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_SET, NULL, NULL);
    if (g_input_stream_read(stream, header, sizeof(macbinary_header_t), NULL, NULL) != sizeof(macbinary_header_t)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Filter cannot handle given data: failed to read MacBinary header!");
        return FALSE;
    }

    /* We need to calculate CRC16 before we fixup the header */
    self->priv->crctab = crcinit(CRC16POLY);
    calculated_crc = docrc((guchar *) header, sizeof(macbinary_header_t) - 4, self->priv->crctab);

    /* Fixup header endianness */
    mirage_file_filter_fixup_header(self);

    /* Validate MacBinary header */
    if (header->version != 0 || header->reserved_1 != 0 || header->reserved_2 != 0 ||
        header->fn_length < 1 || header->fn_length > 63) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Filter cannot handle given data: invalid header!");
        return FALSE;
    }

    /* Valid CRC indicates v2.0 */
    if (calculated_crc != header->crc16) {
        /* Do we have v1.0 then? Hard to say for sure... */
        #ifndef TRUST_UNRELIABLE_V1_CHECK
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: File validates as MacBinary v1.0, however the check is unreliable and therefore disabled!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "File validates as MacBinary v1.0, however the check is unreliable and therefore disabled!");
        return FALSE;
        #endif
    }

    /* The data fork contains the image data */
    mirage_file_filter_set_file_size(MIRAGE_FILE_FILTER(self), header->datafork_len);

    /* Print some info */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing the underlying stream data...\n", __debug__);

    file_name = g_string_new_len(header->filename, header->fn_length);
    file_type = g_string_new_len(header->filetype, 4);
    creator   = g_string_new_len(header->creator, 4);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Original filename: %s\n", __debug__, file_name->str);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: File type: %s creator: %s\n", __debug__, file_type->str, creator->str);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Data fork length: %d\n", __debug__, header->datafork_len);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Resource fork length: %d\n", __debug__, header->resfork_len);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Get info comment length: %d\n", __debug__, header->getinfo_len);

    if (calculated_crc == header->crc16) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Length of total files: %d\n", __debug__, header->unpacked_len);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Length of secondary header: %d\n", __debug__, header->secondary_len);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CRC16: 0x%04x (calculated: 0x%04x)\n", __debug__, header->crc16, calculated_crc);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Version used to pack: %d\n", __debug__, header->pack_ver-129);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Version needed to unpack: %d\n", __debug__, header->unpack_ver-129);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Finder flags: 0x%04x\n", __debug__, (header->finder_flags << 8) + header->finder_flags_2);
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Finder flags: 0x%04x\n", __debug__, header->finder_flags << 8);
    }

    g_string_free(file_name, TRUE);
    g_string_free(file_type, TRUE);
    g_string_free(creator, TRUE);

    /* Read the resource fork if any exists */
    if (header->resfork_len) {
        goffset     rsrc_fork_pos = sizeof(macbinary_header_t) * 2 + header->datafork_len - header->datafork_len % 128;
        gchar       *rsrc_fork_data = NULL;
        rsrc_fork_t *rsrc_fork = NULL;

        rsrc_fork_data = g_malloc(header->resfork_len);
        if (!rsrc_fork_data) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Failed to allocate memory!");
            return FALSE;
        }

        g_seekable_seek(G_SEEKABLE(stream), rsrc_fork_pos, G_SEEK_SET, NULL, NULL);
        if (g_input_stream_read(stream, rsrc_fork_data, header->resfork_len, NULL, NULL) != header->resfork_len) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Failed to read resource-fork!");
            return FALSE;
        }

        rsrc_fork = self->priv->rsrc_fork = rsrc_fork_read_binary(rsrc_fork_data);
        if (!rsrc_fork) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Failed to parse resource-fork!");
            return FALSE;
        }

        g_free(rsrc_fork_data);
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing completed successfully\n\n", __debug__);

    return TRUE;
}


static gssize mirage_file_filter_macbinary_partial_read (MirageFileFilter *_self, void *buffer, gsize count)
{
    MirageFileFilterMacBinary *self = MIRAGE_FILE_FILTER_MACBINARY(_self);
    GInputStream *stream = g_filter_input_stream_get_base_stream(G_FILTER_INPUT_STREAM(self));
    goffset position = mirage_file_filter_get_position(MIRAGE_FILE_FILTER(self));

    goffset read_pos = position + sizeof(macbinary_header_t);

    gint ret;

    /* Are we behind end of stream? */
    if (position >= self->priv->header.datafork_len) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FILE_IO, "%s: stream position %ld (0x%lX) beyond end of stream, doing nothing!\n", __debug__, position, position);
        return 0;
    }

    /* Seek to the position */
    if (!g_seekable_seek(G_SEEKABLE(stream), read_pos, G_SEEK_SET, NULL, NULL)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to %ld in underlying stream!\n", __debug__, read_pos);
        return -1;
    }

    /* Read data into buffer */
    ret = g_input_stream_read(stream, buffer, count, NULL, NULL);
    if (ret == -1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read %d bytes from underlying stream!\n", __debug__, count);
        return -1;
    } else if (ret == 0) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unexpectedly reached EOF!\n", __debug__);
        return -1;
    }

    return count;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_DYNAMIC_TYPE(MirageFileFilterMacBinary, mirage_file_filter_macbinary, MIRAGE_TYPE_FILE_FILTER);

void mirage_file_filter_macbinary_type_register (GTypeModule *type_module)
{
    return mirage_file_filter_macbinary_register_type(type_module);
}


static void mirage_file_filter_macbinary_init (MirageFileFilterMacBinary *self)
{
    self->priv = MIRAGE_FILE_FILTER_MACBINARY_GET_PRIVATE(self);

    mirage_file_filter_generate_info(MIRAGE_FILE_FILTER(self),
        "FILTER-MACBINARY",
        "MACBINARY File Filter",
        1,
        "MacBinary images (*.bin, *.macbin)", "application/x-macbinary"
    );

    self->priv->crctab = NULL;
    self->priv->rsrc_fork = NULL;
}

static void mirage_file_filter_macbinary_finalize (GObject *gobject)
{
    MirageFileFilterMacBinary *self = MIRAGE_FILE_FILTER_MACBINARY(gobject);

    if (self->priv->rsrc_fork) {
        rsrc_fork_free(self->priv->rsrc_fork);
    }

    g_free(self->priv->crctab);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_file_filter_macbinary_parent_class)->finalize(gobject);
}

static void mirage_file_filter_macbinary_class_init (MirageFileFilterMacBinaryClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageFileFilterClass *file_filter_class = MIRAGE_FILE_FILTER_CLASS(klass);

    gobject_class->finalize = mirage_file_filter_macbinary_finalize;

    file_filter_class->can_handle_data_format = mirage_file_filter_macbinary_can_handle_data_format;

    file_filter_class->partial_read = mirage_file_filter_macbinary_partial_read;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageFileFilterMacBinaryPrivate));
}

static void mirage_file_filter_macbinary_class_finalize (MirageFileFilterMacBinaryClass *klass G_GNUC_UNUSED)
{
}

