/*
 *  libMirage: hard-disk image parser: parser
 *  Copyright (C) 2013-2014 Henrik Stokseth
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

#include "image-harddisk.h"

#define __debug__ "HD-Parser"


/**********************************************************************\
 *                  Object and its private structure                  *
\**********************************************************************/
struct _MirageParserHdPrivate
{
    MirageDisc *disc;

    gint track_mode;
    gint track_sectsize;

    gboolean needs_padding;
};


G_DEFINE_DYNAMIC_TYPE_EXTENDED(
    MirageParserHd,
    mirage_parser_hd,
    MIRAGE_TYPE_PARSER,
    0,
    G_ADD_PRIVATE_DYNAMIC(MirageParserHd)
)

void mirage_parser_hd_type_register (GTypeModule *type_module)
{
    mirage_parser_hd_register_type(type_module);
}


static gboolean mirage_parser_hd_is_file_valid (MirageParserHd *self, MirageStream *stream, GError **error)
{
    gsize file_length;

    /* Get stream length */
    if (!mirage_stream_seek(stream, 0, G_SEEK_END, error)) {
        return FALSE;
    }
    file_length = mirage_stream_tell(stream);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: verifying file size...\n", __debug__);

    /* Make sure the file is large enough to contain APM or GPT headers and a small partition table and a MDB */
    if (file_length < 3*512) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: parser cannot handle given image: file too small!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Parser cannot handle given image: file too small!"));
        return FALSE;
    }

    /* Hybrid ISO images are best handled by the ISO parser, so we reject those here */
    if (file_length >= 17 * 2048) {
        guint8 buf[8];

        if (!mirage_stream_seek(stream, 16 * 2048, G_SEEK_SET, NULL)) {
    	    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to ISO signature!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Failed to seek to ISO signature!"));
            return FALSE;
        }

        if (mirage_stream_read(stream, &buf, sizeof(buf), NULL) != sizeof(buf)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read ISO signature!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Failed to read ISO signature!"));
            return FALSE;
        }

        if (!memcmp(buf, mirage_pattern_cd001, sizeof(mirage_pattern_cd001))
         || !memcmp(buf, mirage_pattern_bea01, sizeof(mirage_pattern_bea01))) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: Image is an ISO image - deferring!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Image is an ISO image - deferring!"));
            return FALSE;
        }
    }

    /* Checking if image has a valid Driver Descriptor Map (DDM) */
    driver_descriptor_map_t ddm;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: checking if image has a valid Driver Descriptor Map (DDM)...\n", __debug__);

    if (!mirage_stream_seek(stream, 0, G_SEEK_SET, NULL)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to beginning of file!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Failed to seek to beginning of file!"));
        return FALSE;
    }

    if (mirage_stream_read(stream, &ddm, sizeof(driver_descriptor_map_t), NULL) != sizeof(driver_descriptor_map_t)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read driver descriptor map!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Failed to read driver descriptor map!"));
        return FALSE;
    }

    mirage_ddm_block_fix_endian (&ddm);

    if (!memcmp(&ddm.signature, "ER", 2)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: Image has a valid Driver Descriptor Map!\n", __debug__);

        mirage_print_ddm_block(MIRAGE_CONTEXTUAL(self), &ddm);

        /* Check if image has valid Apple Partition Map (APM) entries */
        apm_entry_t pme;

        for (guint p = 0;; p++) {
            if (!mirage_stream_seek(stream, ddm.block_size * (p + 1), G_SEEK_SET, NULL)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to beginning of APM entries!\n", __debug__);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Failed to seek to beginning of APM entries!"));
                return FALSE;
            }

            if (mirage_stream_read(stream, &pme, sizeof(apm_entry_t), NULL) != sizeof(apm_entry_t)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read APM entry!\n", __debug__);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Failed to read APM entry!"));
                return FALSE;
            }

            mirage_apm_entry_block_fix_endian (&pme);

            if (!memcmp(&pme.signature, "PM", 2)) {
                mirage_print_apm_entry_block(MIRAGE_CONTEXTUAL(self), &pme);
                if (p + 1 >= pme.map_entries) break; /* Last APM entry */
            } else {
                break; /* Invalid Partition Map entry */
            }
        }

        /* Apply padding if needed */
        if (ddm.block_size == 512 || ddm.block_size == 1024) {
            self->priv->needs_padding = file_length % 2048;
        } else if (ddm.block_size == 2048) {
            self->priv->needs_padding = FALSE;
        } else {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: parser cannot map this sector size!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Parser cannot map this sector size!"));
            return FALSE;
        }
        self->priv->track_sectsize = 2048;
        self->priv->track_mode = MIRAGE_SECTOR_MODE1;

        return TRUE;
    }

    /* Checking if image has a valid GPT header */
    gpt_header_t gpt_header;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: checking if image has a valid GPT header...\n", __debug__);

    if (!mirage_stream_seek(stream, 512, G_SEEK_SET, NULL)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to GPT header!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Failed to seek to GPT header!"));
        return FALSE;
    }

    if (mirage_stream_read(stream, &gpt_header, sizeof(gpt_header_t), NULL) != sizeof(gpt_header_t)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read GPT header!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Failed to read GPT header!"));
        return FALSE;
    }

    mirage_gpt_header_fix_endian (&gpt_header);

    if (!memcmp(&gpt_header.signature, "EFI PART", 8)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: Image has a valid GPT header!\n", __debug__);

        mirage_print_gpt_header(MIRAGE_CONTEXTUAL(self), &gpt_header);

        /* Check if image has valid GPT entries */
        gpt_entry_t gpt_entry;

        for (guint p = 0; p < gpt_header.gpt_entries; p++) {
            if (!mirage_stream_seek(stream, (512 * gpt_header.lba_gpt_table) + (gpt_header.gpt_entry_size * p), G_SEEK_SET, NULL)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to beginning of GPT entries!\n", __debug__);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Failed to seek to beginning of GPT entries!"));
                return FALSE;
            }

            if (mirage_stream_read(stream, &gpt_entry, sizeof(gpt_entry_t), NULL) != sizeof(gpt_entry_t)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read GPT entry!\n", __debug__);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Failed to read GPT entry!"));
                return FALSE;
            }

            mirage_gpt_entry_fix_endian (&gpt_entry);

            if (gpt_entry.type.as_int[0] == 0 && gpt_entry.type.as_int[1] == 0) {
                continue; /* Unused partition entry */
            } else {
                mirage_print_gpt_entry(MIRAGE_CONTEXTUAL(self), &gpt_entry);
            }
        }

        /* Apply padding if needed */
        self->priv->needs_padding = file_length % 2048;

        self->priv->track_sectsize = 2048;
        self->priv->track_mode = MIRAGE_SECTOR_MODE1;

        return TRUE;
    }

    /* Macintosh HFS/HFS+ image check */
    guint8  mac_buf[512];
    guint16 mac_sectsize = 512;
    guint32 mac_dev_sectors = file_length / mac_sectsize;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: checking if image is a HFS/HFS+ image...\n", __debug__);

    if (!mirage_stream_seek(stream, 2 * 512, G_SEEK_SET, NULL)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to Master Directory Block (MDB)!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Failed to seek to Master Directory Block (MDB)!"));
        return FALSE;
    }

    if (mirage_stream_read(stream, mac_buf, sizeof(mac_buf), NULL) != sizeof(mac_buf)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read Master Directory Block (MDB)!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_IMAGE_FILE_ERROR, Q_("Failed to read Master Directory Block (MDB)!"));
        return FALSE;
    }

    if (!memcmp(mac_buf, "BD", 2) || !memcmp(mac_buf, "H+", 2) || !memcmp(mac_buf, "HX", 2)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: HFS/HFS+ signature found!\n", __debug__);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: image is a HFS/HFS+ image; %d sectors, sector size: %d\n", __debug__, mac_dev_sectors, mac_sectsize);

        self->priv->needs_padding = file_length % 2048;
        self->priv->track_sectsize = 2048;
        self->priv->track_mode = MIRAGE_SECTOR_MODE1;

        return TRUE;
    }

    /* Nope, can't load the file */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: parser cannot handle given image!\n", __debug__);
    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Parser cannot handle given image!"));
    return FALSE;
}

static gboolean mirage_parser_hd_load_track (MirageParserHd *self, MirageStream *stream, GError **error)
{
    MirageSession *session;
    MirageTrack *track;
    MirageFragment *fragment;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: loading track...\n", __debug__);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track mode: %d\n", __debug__, self->priv->track_mode);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: sector size: %d\n", __debug__, self->priv->track_sectsize);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: padding sector needed: %d\n", __debug__, self->priv->needs_padding);

    /* Create data fragment */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating data fragment\n", __debug__);
    fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL);

    mirage_fragment_main_data_set_stream(fragment, stream);
    mirage_fragment_main_data_set_size(fragment, self->priv->track_sectsize);
    mirage_fragment_main_data_set_format(fragment, MIRAGE_MAIN_DATA_FORMAT_DATA);

    /* Use whole file */
    if (!mirage_fragment_use_the_rest_of_file(fragment, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to use the rest of file!\n", __debug__);
        g_object_unref(fragment);
        return FALSE;
    }

    /* Add one sector to cover otherwise truncated data */
    if (self->priv->needs_padding) {
        gint cur_length = mirage_fragment_get_length(fragment);
        mirage_fragment_set_length(fragment, cur_length + 1);
    }

    /* Add track */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding track\n", __debug__);

    session = mirage_disc_get_session_by_index(self->priv->disc, -1, NULL);

    track = g_object_new(MIRAGE_TYPE_TRACK, NULL);
    mirage_session_add_track_by_index(session, -1, track);
    g_object_unref(session);

    /* Set track mode */
    mirage_track_set_sector_type(track, self->priv->track_mode);

    /* Add fragment to track */
    mirage_track_add_fragment(track, -1, fragment);

    g_object_unref(fragment);
    g_object_unref(track);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished loading track\n", __debug__);

    return TRUE;
}


/**********************************************************************\
 *                MirageParser methods implementation                *
\**********************************************************************/
static MirageDisc *mirage_parser_hd_load_image (MirageParser *_self, MirageStream **streams, GError **error)
{
    MirageParserHd *self = MIRAGE_PARSER_HD(_self);
    const gchar *hd_filename;
    MirageStream *stream;
    gboolean succeeded = TRUE;

    /* Check if file can be loaded */
    stream = g_object_ref(streams[0]);
    hd_filename = mirage_stream_get_filename(stream);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: checking if parser can handle given image...\n", __debug__);
    if (!mirage_parser_hd_is_file_valid(self, stream, error)) {
        g_object_unref(stream);
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: parser can handle given image!\n", __debug__);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing the image...\n", __debug__);

    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_set_parent(MIRAGE_OBJECT(self->priv->disc), self);

    /* Set filenames */
    mirage_disc_set_filename(self->priv->disc, hd_filename);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Hard-disk filename: %s\n", __debug__, hd_filename);

    /* Session: one session (with one tracks) */
    MirageSession *session = g_object_new(MIRAGE_TYPE_SESSION, NULL);
    mirage_disc_add_session_by_index(self->priv->disc, 0, session);

    /* Hard-disk image parser assumes single-track image, so we're dealing with regular CD-ROM session */
    mirage_session_set_session_type(session, MIRAGE_SESSION_CDROM);
    g_object_unref(session);

    /* Load track */
    if (!mirage_parser_hd_load_track(self, stream, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to load track!\n", __debug__);
        succeeded = FALSE;
        goto end;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finishing the layout\n", __debug__);

    /* Set medium type to harddisk */
    mirage_disc_set_medium_type(self->priv->disc, MIRAGE_MEDIUM_HDD);

end:
    g_object_unref(stream);

    /* Return disc */
    if (succeeded) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing completed successfully\n\n", __debug__);
        return self->priv->disc;
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing failed!\n\n", __debug__);
        g_object_unref(self->priv->disc);
        return NULL;
    }
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
static void mirage_parser_hd_init (MirageParserHd *self)
{
    self->priv = mirage_parser_hd_get_instance_private(self);

    mirage_parser_generate_info(MIRAGE_PARSER(self),
        "PARSER-HD",
        Q_("Hard-disk Image Parser"),
        3,
        Q_("Apple Disk image (*.cdr)"), "application/x-apple-diskimage",
        Q_("Apple Disk image (*.smi)"), "application/x-apple-diskimage",
        Q_("Apple Disk image (*.img)"), "application/x-apple-diskimage"
    );

    self->priv->needs_padding = FALSE;
}

static void mirage_parser_hd_class_init (MirageParserHdClass *klass)
{
    MirageParserClass *parser_class = MIRAGE_PARSER_CLASS(klass);

    parser_class->load_image = mirage_parser_hd_load_image;
}

static void mirage_parser_hd_class_finalize (MirageParserHdClass *klass G_GNUC_UNUSED)
{
}
