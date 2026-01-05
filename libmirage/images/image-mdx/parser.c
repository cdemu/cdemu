/*
 *  libMirage: MDX image: parser
 *  Copyright (C) 2006-2014 Henrik Stokseth
 *  Copyright (C) 2025 Rok Mandeljc
 *
 *  Based on reverse-engineering effort from:
 *  https://github.com/Marisa-Chan/mdsx
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

#include "image-mdx.h"
#include "fragment.h"

#define __debug__ "MDX-Parser"


/**********************************************************************\
 *                  Object and its private structure                  *
\**********************************************************************/
struct _MirageParserMdxPrivate
{
    MirageDisc *disc;

    /* Descriptor file stream */
    MirageStream *stream;

    /* MDX vs MDS+MDF */
    gboolean is_mdx;

    /* Descriptor data (decrypted and uncompressed) */
    guint8 *descriptor_data;
    guint64 descriptor_size;

    /* Optional encryption header for encrypted track data; pointer into
     * descriptor data buffer. */
    const MDX_EncryptionHeader *data_encryption_header;

    gint64 prev_session_end;
};


G_DEFINE_DYNAMIC_TYPE_EXTENDED(
    MirageParserMdx,
    mirage_parser_mdx,
    MIRAGE_TYPE_PARSER,
    0,
    G_ADD_PRIVATE_DYNAMIC(MirageParserMdx)
)

void mirage_parser_mdx_type_register (GTypeModule *type_module)
{
    mirage_parser_mdx_register_type(type_module);
}


/**********************************************************************\
 *                          Endianness fix-up                         *
\**********************************************************************/
static inline void mdx_descriptor_header_fix_endian (MDX_DescriptorHeader *header)
{
    header->medium_type = GUINT16_FROM_LE(header->medium_type);
    header->num_sessions = GUINT16_FROM_LE(header->num_sessions);
    header->sessions_blocks_offset = GUINT32_FROM_LE(header->sessions_blocks_offset);
    header->dpm_blocks_offset = GUINT32_FROM_LE(header->dpm_blocks_offset);
    header->encryption_header_offset = GUINT32_FROM_LE(header->encryption_header_offset);
}

static inline void mdx_session_block_fix_endian (MDX_SessionBlock *block)
{
    block->session_start = GUINT64_FROM_LE(block->session_start);
    block->session_number = GUINT16_FROM_LE(block->session_number);
    block->first_track = GUINT16_FROM_LE(block->first_track);
    block->last_track = GUINT16_FROM_LE(block->last_track);
    block->__unknown1__ = GUINT32_FROM_LE(block->__unknown1__);
    block->tracks_blocks_offset = GUINT32_FROM_LE(block->tracks_blocks_offset);
    block->session_end = GUINT64_FROM_LE(block->session_end);
}

static inline void mdx_track_block_fix_endian (MDX_TrackBlock *block)
{
    block->extra_offset = GUINT32_FROM_LE(block->extra_offset);
    block->sector_size = GUINT16_FROM_LE(block->sector_size);

    block->start_sector = GUINT32_FROM_LE(block->start_sector);
    block->start_offset = GUINT64_FROM_LE(block->start_offset);
    block->footer_count = GUINT32_FROM_LE(block->footer_count);
    block->footer_offset = GUINT32_FROM_LE(block->footer_offset);

    block->start_sector64 = GUINT64_FROM_LE(block->start_sector64);
    block->track_length64 = GUINT64_FROM_LE(block->track_length64);
}

static inline void mdx_track_extra_block_fix_endian (MDX_TrackExtraBlock *block)
{
    block->pregap = GUINT32_FROM_LE(block->pregap);
    block->length = GUINT32_FROM_LE(block->length);
}


static inline void mdx_footer_fix_endian (MDX_Footer *block)
{
    block->filename_offset = GUINT32_FROM_LE(block->filename_offset);
    block->__unknown2__ = GUINT16_FROM_LE(block->__unknown2__);
    block->__unknown3__ = GUINT32_FROM_LE(block->__unknown3__);
    block->blocks_in_compression_group = GUINT32_FROM_LE(block->blocks_in_compression_group);
    block->track_data_length = GUINT64_FROM_LE(block->track_data_length);
    block->compression_table_offset = GUINT64_FROM_LE(block->compression_table_offset);
}

static inline void dpm_data_fix_endian (guint32 *dpm_data, guint32 num_entries)
{
    for (guint i = 0; i < num_entries; i++) {
        dpm_data[i] = GUINT32_FROM_LE(dpm_data[i]);
    }
}

static inline void widechar_filename_fix_endian (gunichar2 *filename)
{
    for (guint i = 0; ; i++) {
        filename[i] = GUINT16_FROM_LE(filename[i]);
        if (!filename[i]) {
            break;
        }
    }
}


/**********************************************************************\
 *                         Parsing functions                          *
\**********************************************************************/
static gchar *_helper_find_binary_file (const gchar *declared_filename, const gchar *mds_filename)
{
    gchar *bin_filename;
    gchar *bin_fullpath;

    /* Is the filename in form of '*.mdf'? */
    GRegex *ext_regex = g_regex_new("\\*\\.(?<ext>\\w+)", 0, 0, NULL);
    GMatchInfo *match_info = NULL;
    if (g_regex_match(ext_regex, declared_filename, 0, &match_info)) {
        /* Replace the extension in mds_filename */
        gchar *ext = g_match_info_fetch_named(match_info, "ext");
        GRegex *mds_regex = g_regex_new("(?<ext>\\w+)$", 0, 0, NULL);
        bin_filename = g_regex_replace(mds_regex, mds_filename, -1, 0, ext, 0, NULL);

        g_regex_unref(mds_regex);
        g_free(ext);
    } else {
        bin_filename = g_strdup(declared_filename);
    }
    g_match_info_free(match_info);
    g_regex_unref(ext_regex);

    bin_fullpath = mirage_helper_find_data_file(bin_filename, mds_filename);
    g_free(bin_filename);

    return bin_fullpath;
}

static gchar *mirage_parser_mdx_get_mdf_filename (MirageParserMdx *self, const gchar *descriptor_filename, const MDX_Footer *footer_block, GError **error)
{
    gchar *tmp_mdf_filename;
    gchar *mdf_filename;

    /* In contrast to MDSv1, MDX/MDSv2 always seems to be using 16-bit (wide)
     * characters, and the field in footer block that used to denote whether
     * wide characters are used or not now has a different meaning. */
    gunichar2 *tmp_ptr = (gunichar2 *)(self->priv->descriptor_data + footer_block->filename_offset);
    widechar_filename_fix_endian(tmp_ptr);
    tmp_mdf_filename = g_utf16_to_utf8(tmp_ptr, -1, NULL, NULL, NULL);

    /* Find binary file */
    mdf_filename = _helper_find_binary_file(tmp_mdf_filename, descriptor_filename);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: MDF filename: <%s> -> <%s>\n", __debug__, tmp_mdf_filename, mdf_filename);
    g_free(tmp_mdf_filename);

    if (!mdf_filename) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to find data file!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DATA_FILE_ERROR, Q_("Failed to find data file!"));
        return NULL;
    }

    return mdf_filename;
}


static gboolean mirage_parser_mdx_parse_track_entries (MirageParserMdx *self, MDX_SessionBlock *session_block, GError **error)
{
    MDX_TrackBlock *track_blocks;
    MirageSession *session;
    gint medium_type;
    guint previous_track_end;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: processing track blocks\n", __debug__);

    /* Fetch medium type, which we will need later */
    medium_type = mirage_disc_get_medium_type(self->priv->disc);

    /* Get current session */
    session = mirage_disc_get_session_by_index(self->priv->disc, -1, error);
    if (!session) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get current session!\n", __debug__);
        return FALSE;
    }

    /* Determine address where we expect the first track to start; see pregap handling code later on. */
    previous_track_end = mirage_session_layout_get_start_sector(session);

    /* Process track blocks */
    track_blocks = (MDX_TrackBlock *)(self->priv->descriptor_data + session_block->tracks_blocks_offset);
    for (guint i = 0; i < session_block->num_all_blocks; i++) {
        MDX_TrackExtraBlock *extra_block = NULL;

        /* Read main track block */
        MDX_TrackBlock *track_block = &track_blocks[i];
        mdx_track_block_fix_endian(track_block);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track block #%i:\n", __debug__, i);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  sector mode: 0x%X\n", __debug__, track_block->sector_mode);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  extra data:\n", __debug__);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   sync pattern: %d\n", __debug__, track_block->has_sync_pattern);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   subheader: %d\n", __debug__, track_block->has_subheader);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   header: %d\n", __debug__, track_block->has_header);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   unknown: %d\n", __debug__, track_block->has_unknown);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:   edc/ecc: %d\n", __debug__, track_block->has_edc_ecc);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  subchannel: 0x%X\n", __debug__, track_block->subchannel);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  adr/ctl: 0x%X\n", __debug__, track_block->adr_ctl);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  tno: 0x%X\n", __debug__, track_block->tno);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  point: 0x%X\n", __debug__, track_block->point);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  min: 0x%X\n", __debug__, track_block->min);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  sec: 0x%X\n", __debug__, track_block->sec);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  frame: 0x%X\n", __debug__, track_block->frame);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  zero: 0x%X\n", __debug__, track_block->zero);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  pmin: 0x%X\n", __debug__, track_block->pmin);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  psec: 0x%X\n", __debug__, track_block->psec);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  pframe: 0x%X\n", __debug__, track_block->pframe);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  extra offset: 0x%X\n", __debug__, track_block->extra_offset);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  sector size: 0x%X\n", __debug__, track_block->sector_size);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  start sector: 0x%X\n", __debug__, track_block->start_sector);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  start offset: 0x%" G_GINT64_MODIFIER "X\n", __debug__, track_block->start_offset);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  footer count: 0x%X\n", __debug__, track_block->footer_count);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  footer offset: 0x%X\n", __debug__, track_block->footer_offset);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  start sector (64-bit): 0x%" G_GINT64_MODIFIER "X\n", __debug__, track_block->start_sector64);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  track length (64-bit): 0x%" G_GINT64_MODIFIER "X\n", __debug__, track_block->track_length64);

        /* Read extra track block, if applicable; it seems that only CD images
         * have extra blocks, though. For DVD images, extra_offset is set to 0. */
        if (medium_type == MIRAGE_MEDIUM_CD && track_block->extra_offset) {
            extra_block = (MDX_TrackExtraBlock *)(self->priv->descriptor_data + track_block->extra_offset);
            mdx_track_extra_block_fix_endian(extra_block);

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: extra block #%i:\n", __debug__, i);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  pregap: 0x%X\n", __debug__, extra_block->pregap);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  length: 0x%X\n", __debug__, extra_block->length);
        }

        /* Read footer, if applicable */
        if (track_block->footer_offset) {
            MDX_Footer *footer_blocks = (MDX_Footer *)(self->priv->descriptor_data + track_block->footer_offset);
            for (guint j = 0; j < track_block->footer_count; j++) {
                MDX_Footer *footer_block = &footer_blocks[j];
                mdx_footer_fix_endian(footer_block);

                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: footer block #%i - %i:\n", __debug__, i, j);
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  filename offset: 0x%X\n", __debug__, footer_block->filename_offset);
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  flags: 0x%X\n", __debug__, footer_block->flags);
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  unknown1: 0x%X\n", __debug__, footer_block->__unknown1__);
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  unknown2: 0x%X\n", __debug__, footer_block->__unknown2__);
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  unknown3: 0x%X\n", __debug__, footer_block->__unknown3__);
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  blocks in compression group: 0x%X\n", __debug__, footer_block->blocks_in_compression_group);
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  track data length: 0x%" G_GINT64_MODIFIER "X\n", __debug__, footer_block->track_data_length);
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  compression table offset: 0x%" G_GINT64_MODIFIER "X\n", __debug__, footer_block->compression_table_offset);
            }
        }

        if (track_block->point >= 99) {
            /* Non-track block; skip */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: skipping non-track entry 0x%X\n", __debug__, track_block->point);
            continue;
        }

        /* Track entry */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: entry is for track %i\n", __debug__, track_block->point);

        /* Decode track mode */
        /* It seems lowest three bytes encode the sector type, while higher
         * bits are flags that denote availability of extra data (sync
         * pattern, header, subheader, EDC/ECC). */
        gint converted_mode;
        guint expected_sector_size;

        switch (track_block->sector_mode) {
            case MDX_SECTOR_AUDIO: {
                converted_mode = MIRAGE_SECTOR_AUDIO;
                expected_sector_size = 2352;
                /* Ignore all extra data bits - although they seem to be
                 * all set (except for the unknown one) */
                break;
            }
            case MDX_SECTOR_MODE1: {
                converted_mode = MIRAGE_SECTOR_MODE1;
                expected_sector_size = 2048;
                if (track_block->has_sync_pattern) {
                    expected_sector_size += 12;
                }
                if (track_block->has_header) {
                    expected_sector_size += 4;
                }
                if (track_block->has_edc_ecc) {
                    expected_sector_size += 288;
                }
                break;
            }
            case MDX_SECTOR_MODE2: {
                converted_mode = MIRAGE_SECTOR_MODE2;
                expected_sector_size = 2336;
                if (track_block->has_sync_pattern) {
                    expected_sector_size += 12;
                }
                if (track_block->has_header) {
                    expected_sector_size += 4;
                }
                break;
            }
            case MDX_SECTOR_MODE2_FORM1: {
                converted_mode = MIRAGE_SECTOR_MODE2_FORM1;
                expected_sector_size = 2048;
                if (track_block->has_sync_pattern) {
                    expected_sector_size += 12;
                }
                if (track_block->has_header) {
                    expected_sector_size += 4;
                }
                if (track_block->has_subheader) {
                    expected_sector_size += 8;
                }
                if (track_block->has_edc_ecc) {
                    expected_sector_size += 280;
                }
                break;
            }
            case MDX_SECTOR_MODE2_FORM2: {
                converted_mode = MIRAGE_SECTOR_MODE2_FORM2;
                expected_sector_size = 2324;
                if (track_block->has_sync_pattern) {
                    expected_sector_size += 12;
                }
                if (track_block->has_header) {
                    expected_sector_size += 4;
                }
                if (track_block->has_subheader) {
                    expected_sector_size += 8;
                }
                if (track_block->has_edc_ecc) {
                    expected_sector_size += 4;
                }
                break;
            }
            default: {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unsupported track mode 0x%X!\n", __debug__, track_block->sector_mode);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Unsupported track mode!"));
                g_object_unref(session);
                return FALSE;
            }
        }

        /* TODO: account for subchannel data! */

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track mode: 0x%X, expected sector size: %d (0x%X)\n", __debug__, converted_mode, expected_sector_size, expected_sector_size);
        if (expected_sector_size != track_block->sector_size) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unexpected sector size - expected %d (0x%X), found %d (0x%X)!\n", __debug__, expected_sector_size, expected_sector_size, track_block->sector_size, track_block->sector_size);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Track sector size mismatch!"));
            g_object_unref(session);
            return FALSE;
        }

        /* Create track */
        MirageTrack *track = g_object_new(MIRAGE_TYPE_TRACK, NULL);
        if (!mirage_session_add_track_by_number(session, track_block->point, track, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __debug__);
            g_object_unref(track);
            g_object_unref(session);
            return FALSE;
        }

        mirage_track_set_sector_type(track, converted_mode);

        /* Flags: decoded from Ctl */
        mirage_track_set_ctl(track, track_block->adr_ctl & 0x0F);

        /* It seems that pregap data may or may not be stored in the image, and the clue about its presence is
         * the start_sector64 field in track block - if pregap data is included, then its value matches the end
         * of previous track; otherwise, the difference should match the length of pregap. The pregap for first
         * track is never included. */
        if (extra_block && extra_block->pregap) {
            if (track_block->point > 1 && track_block->start_sector64 == previous_track_end) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track %d has pregap of %d (0x%X) sectors; assuming data is included in image\n", __debug__, track_block->point, extra_block->pregap, extra_block->pregap);
            } else {
                /* TODO: check that the difference matches pregap length (although the mismatch will cause track
                 * length mismatch and trigger validation error at end of this function)... */
                MirageFragment *fragment = g_object_new(MIRAGE_TYPE_FRAGMENT, NULL);

                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: track %d has pregap of %d (0x%X) sectors; creating NULL fragment\n", __debug__, track_block->point, extra_block->pregap, extra_block->pregap);

                mirage_fragment_set_length(fragment, extra_block->pregap);

                mirage_track_add_fragment(track, -1, fragment);
                g_object_unref(fragment);
            }

            mirage_track_set_track_start(track, extra_block->pregap);
        }

        /* Data fragment(s): it seems that MDS allows splitting of MDF files into multiple files; it also seems
         * files are split on sector boundary, which means we can simply represent them with multiple data
         * fragments. Note that MDX does not seem to support splitting. */
        if (!track_block->footer_offset) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: track has no footer blocks!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Track has no footer blocks!"));
            return FALSE;
        }

        const MDX_Footer *footer_blocks = (MDX_Footer *)(self->priv->descriptor_data + track_block->footer_offset);
        for (guint j = 0; j < track_block->footer_count; j++) {
            const MDX_Footer *footer_block = &footer_blocks[j];

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: creating data fragment for footer block #%i\n", __debug__, j);

            /* Data file */
            gchar *data_filename = NULL;
            if (self->priv->is_mdx) {
                /* In MDX image, the data is included in MDX file; so filename offset is expected to be zero. */
                if (footer_block->filename_offset != 0) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: non-zero filename offset in footer block of MDX image!\n", __debug__);
                }
                data_filename = g_strdup(mirage_stream_get_filename(self->priv->stream));
            } else {
                /* In MDSv2 image, the data is in external MDF file(s), so we need a valid filename offset. */
                if (footer_block->filename_offset == 0) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: zero filename offset found in footer block of MDSv2 image!\n", __debug__);
                    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Zero-valued filename offset found in footer block!"));
                    return FALSE;
                }

                data_filename = mirage_parser_mdx_get_mdf_filename(self, mirage_stream_get_filename(self->priv->stream), footer_block, error);
                if (!data_filename) {
                    MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get MDF filename!\n", __debug__);
                    g_object_unref(track);
                    g_object_unref(session);
                    return FALSE;
                }
            }

            /* Data stream */
            MirageStream *data_stream = mirage_contextual_create_input_stream(MIRAGE_CONTEXTUAL(self), data_filename, error);
            if (!data_stream) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to open stream on data file: %s!\n", __debug__, data_filename);
                g_free(data_filename);
                g_object_unref(track);
                g_object_unref(session);
                return FALSE;
            }
            g_free(data_filename);

            /* Fragment properties */
            guint64 main_offset = 0; /* Corrected below, if needed */
            gint main_size = track_block->sector_size;
            gint main_format = 0;

            gint subchannel_size = 0; /* TODO */
            gint subchannel_format = 0; /* TODO */

            if (j == 0) {
                /* Apply offset only if it is the first file... */
                main_offset = track_block->start_offset;
            }

            if (converted_mode == MIRAGE_SECTOR_AUDIO) {
                main_format = MIRAGE_MAIN_DATA_FORMAT_AUDIO;
            } else {
                main_format = MIRAGE_MAIN_DATA_FORMAT_DATA;
            }

            /* Determine fragment's length; this corresponds to the declared
             * length in the footer block. In case a track has multiple footer
             * blocks (for example, data file split across several files), the
             * sum of all lengths in track's footer blocks should match the
             * length in the track's extra block (minus pregap, if any). */
            gint64 fragment_len = footer_block->track_data_length;

            /* Create custom MDX data fragment */
            GError *local_error = NULL;
            gboolean succeeded;

            MirageFragmentMdx *fragment = g_object_new(MIRAGE_TYPE_FRAGMENT_MDX, NULL);

            if (1) {
                /* Immediately propagate context for debug purposes; so we may
                 * see debug messages emitted during fragment setup. */
                MirageContext *context = mirage_contextual_get_context(MIRAGE_CONTEXTUAL(self));
                mirage_contextual_set_context(MIRAGE_CONTEXTUAL(fragment), context);
                if (context) {
                    g_object_unref(context);
                }
            }

            succeeded = mirage_fragment_mdx_setup(
                fragment,
                fragment_len,
                data_stream,
                main_offset,
                main_size,
                main_format,
                subchannel_size,
                subchannel_format,
                footer_block,
                self->priv->data_encryption_header,
                &local_error
            );

            g_object_unref(data_stream);

            if (!succeeded) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set up MDX data fragment: %s!\n", __debug__, local_error->message);
                g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Failed to set up MDX data fragment: %s!"), local_error->message);
                g_error_free(local_error);
                g_object_unref(fragment);
                g_object_unref(track);
                g_object_unref(session);
                return FALSE;
            }

            mirage_track_add_fragment(track, -1, MIRAGE_FRAGMENT(fragment));
            g_object_unref(fragment);
        }

        /* Validate the track size. In CD-ROM images, the total track length
         * is stored in the extra block (we need to add the pregap length,
         * also available in the extra block!). In DVD-ROM images, it is
         * stored in the track_length64 field in the main track block (and
         * annoyingly enough, it seems to be set to 0 for CD-ROM images). */
        guint64 declared_track_length = extra_block ? (extra_block->length + extra_block->pregap) : track_block->track_length64;
        guint64 total_track_length = mirage_track_layout_get_length(track);

        if (total_track_length != declared_track_length) {
            gint track_number = mirage_track_layout_get_track_number(track);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: track length validation failed for track #%d - declared length is %" G_GINT64_MODIFIER "d (0x%" G_GINT64_MODIFIER "X) sectors, actual length is %" G_GINT64_MODIFIER "d (0x%" G_GINT64_MODIFIER "X) sectors!\n", __debug__, track_number, declared_track_length, declared_track_length, total_track_length, total_track_length);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Track length validation failed for track #%d!"), track_number);
            g_object_unref(track);
            g_object_unref(session);
            return FALSE;
        }

        g_object_unref(track);

        previous_track_end += total_track_length;
    }

    g_object_unref(session);

    return TRUE;
}

static gboolean mirage_parser_mdx_parse_sessions (MirageParserMdx *self, GError **error)
{
    const MDX_DescriptorHeader *descriptor_header = (MDX_DescriptorHeader *)self->priv->descriptor_data; /* Endianess has been fixed up already. */

    MDX_SessionBlock *session_blocks;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: processing session blocks (%i)\n", __debug__, descriptor_header->num_sessions);

    /* Process session blocks */
    session_blocks = (MDX_SessionBlock *)(self->priv->descriptor_data + descriptor_header->sessions_blocks_offset);
    for (gint i = 0; i < descriptor_header->num_sessions; i++) {
        MDX_SessionBlock *session_block = &session_blocks[i];
        mdx_session_block_fix_endian(session_block);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: session block #%i:\n", __debug__, i);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  start address: 0x%" G_GINT64_MODIFIER "X\n", __debug__, session_block->session_start);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  number: %i\n", __debug__, session_block->session_number);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  number of all blocks: %i\n", __debug__, session_block->num_all_blocks);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  number of non-track block: %i\n", __debug__, session_block->num_nontrack_blocks);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  first track: %i\n", __debug__, session_block->first_track);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  last track: %i\n", __debug__, session_block->last_track);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  unknown1: 0x%X\n", __debug__, session_block->__unknown1__);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  track blocks offset: 0x%X\n", __debug__, session_block->tracks_blocks_offset);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  end address: 0x%" G_GINT64_MODIFIER "X\n", __debug__, session_block->session_end);

        /* If this is first session, we'll use its start address as disc start address;
           if not, we need to calculate previous session's leadout length, based on
           this session's start address and previous session's end... */
        if (i == 0) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: first session; setting disc's start to 0x%" G_GINT64_MODIFIER "X (%" G_GINT64_MODIFIER "i)\n", __debug__, session_block->session_start, session_block->session_start);
            mirage_disc_layout_set_start_sector(self->priv->disc, session_block->session_start);
        } else {
            guint32 leadout_length = session_block->session_start - self->priv->prev_session_end;
            MirageSession *prev_session;
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: previous session's leadout length: 0x%X (%i)\n", __debug__, leadout_length, leadout_length);

            /* Use -1 as an index, since we still haven't added current session */
            prev_session = mirage_disc_get_session_by_index(self->priv->disc, -1, error);
            if (!prev_session) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to get previous session!\n", __debug__);
                return FALSE;
            }

            mirage_session_set_leadout_length(prev_session, leadout_length);

            g_object_unref(prev_session);
        }
        /* Actually, we could've gotten that one from A2 track entry as well...
           but I'm lazy, and this will hopefully work as well */
        self->priv->prev_session_end = session_block->session_end;

        /* Add session */
        MirageSession *session = g_object_new(MIRAGE_TYPE_SESSION, NULL);
        if (!mirage_disc_add_session_by_number(self->priv->disc, session_block->session_number, session, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __debug__);
            g_object_unref(session);
            return FALSE;
        }
        g_object_unref(session);

        /* Load tracks */
        if (!mirage_parser_mdx_parse_track_entries(self, session_block, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse track entries!\n", __debug__);
            return FALSE;
        }
    }

    return TRUE;
}

static void mirage_parser_mds_parse_dpm_block (MirageParserMdx *self, const guint32 dpm_block_offset)
{
    guint8 *cur_ptr;

    guint32 dpm_block_number;
    guint32 dpm_start_sector;
    guint32 dpm_resolution;
    guint32 dpm_num_entries;

    guint32 *dpm_data;

    cur_ptr = self->priv->descriptor_data + dpm_block_offset;

    /* DPM information */
    dpm_block_number = GUINT32_FROM_LE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
    cur_ptr += sizeof(guint32);

    dpm_start_sector = GUINT32_FROM_LE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
    cur_ptr += sizeof(guint32);

    dpm_resolution = GUINT32_FROM_LE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
    cur_ptr += sizeof(guint32);

    dpm_num_entries = GUINT32_FROM_LE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
    cur_ptr += sizeof(guint32);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: block number: %d\n", __debug__, dpm_block_number);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: start sector: 0x%X\n", __debug__, dpm_start_sector);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: resolution: %d\n", __debug__, dpm_resolution);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of entries: %d\n", __debug__, dpm_num_entries);

    /* Read all entries */
    dpm_data = MIRAGE_CAST_PTR(cur_ptr, 0, guint32 *);

    dpm_data_fix_endian(dpm_data, dpm_num_entries);

    /* Set DPM data */
    mirage_disc_set_dpm_data(self->priv->disc, dpm_start_sector, dpm_resolution, dpm_num_entries, dpm_data);
}

static void mirage_parser_mds_parse_dpm_data (MirageParserMdx *self)
{
    const MDX_DescriptorHeader *descriptor_header = (MDX_DescriptorHeader *)self->priv->descriptor_data; /* Endianess has been fixed up already. */

    if (!descriptor_header->dpm_blocks_offset) {
        /* No DPM data, nothing to do */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: no DPM data found.\n", __debug__);
        return;
    }

    guint8 *cur_ptr = self->priv->descriptor_data + descriptor_header->dpm_blocks_offset;

    /* It would seem the first field is number of DPM data sets, followed by
     * appropriate number of offsets for those data sets. */
    guint32 num_dpm_blocks = GUINT32_FROM_LE(MIRAGE_CAST_DATA(cur_ptr, 0, guint32));
    cur_ptr += sizeof(guint32);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of DPM data blocks: %d\n", __debug__, num_dpm_blocks);

    if (num_dpm_blocks > 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: cannot correctly handle more than 1 DPM block yet!\n", __debug__);
    }

    /* Read each block */
    guint32 *dpm_block_offset = MIRAGE_CAST_PTR(cur_ptr, 0, guint32 *);
    for (guint i = 0; i < num_dpm_blocks; i++) {
        dpm_block_offset[i] = GUINT32_FROM_LE(dpm_block_offset[i]);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: block[%i]: offset: 0x%X\n", __debug__, i, dpm_block_offset[i]);
        mirage_parser_mds_parse_dpm_block(self, dpm_block_offset[i]);
        /* FIXME: currently, only first DPM block is loaded */
        break;
    }
}

static gboolean mirage_parser_mdx_load_disc (MirageParserMdx *self, GError **error)
{
    /* Sessions */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing sessions...\n", __debug__);
    if (!mirage_parser_mdx_parse_sessions(self, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse sessions!\n", __debug__);
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished parsing sessions\n", __debug__);

    /* DPM data */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing DPM data...\n", __debug__);
    mirage_parser_mds_parse_dpm_data(self);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: finished parsing DPM data\n", __debug__);

    return TRUE;
}

/**********************************************************************\
 *             MDX descriptor decompression and decryption            *
\**********************************************************************/
static gboolean mirage_parser_mdx_read_descriptor (MirageParserMdx *self, const MDX_FileHeader *file_header, guint8 **out_data, guint64 *out_data_size, GError **error)
{
    GError *local_error = NULL;
    gssize read_bytes;

    guint64 mdx_footer_offset = -1;
    guint64 mdx_footer_length = -1;

    /* Determine the location of encryption header for descriptor data */
    if (file_header->encryption_header_offset == 0xffffffff) {
        /* In MDX file, the MDSv2/MDX file header is followed by footer
         * offset and length of footer data, which seems to cover the
         * length of (encrypted and compressed) descriptor data, plus 64
         * bytes of PKCS#5 salt data at the end of primary encryption header.
         * The remaining 448 bytes of the primary encryption header do not
         * seem to be covered by this length. */
        self->priv->is_mdx = TRUE;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: MDX file detected. Reading footer offset and length...!\n", __debug__);

        read_bytes = mirage_stream_read(self->priv->stream, &mdx_footer_offset, sizeof(mdx_footer_offset), NULL);
        if (read_bytes != sizeof(mdx_footer_offset)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read MDX footer offset!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Failed to read MDX footer offset!"));
            return FALSE;
        }
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: MDX footer offset: %" G_GINT64_MODIFIER "d (0x%" G_GINT64_MODIFIER "X)\n", __debug__, mdx_footer_offset, mdx_footer_offset);

        read_bytes = mirage_stream_read(self->priv->stream, &mdx_footer_length, sizeof(mdx_footer_length), NULL);
        if (read_bytes != sizeof(mdx_footer_length)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read MDX footer length!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Failed to read MDX footer length!"));
            return FALSE;
        }
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: MDX footer length: %" G_GINT64_MODIFIER "d (0x%" G_GINT64_MODIFIER "X)\n", __debug__, mdx_footer_length, mdx_footer_length);

        guint64 encryption_header_offset = mdx_footer_offset + mdx_footer_length - MDX_PKCS5_SALT_SIZE;
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: encryption header offset: %" G_GINT64_MODIFIER "d (0x%" G_GINT64_MODIFIER "X)\n", __debug__, encryption_header_offset, encryption_header_offset);

        if (!mirage_stream_seek(self->priv->stream, encryption_header_offset, G_SEEK_SET, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to encryption header offset!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Failed to seek to encryption header offset!"));
            return FALSE;
        }
    } else {
        self->priv->is_mdx = FALSE;

        /* In MDSv2 file, the MDSv2/MDX header is followed by compressed
         * and encrypted descriptor data, followed by primary encryption
         * header (512 bytes), which spans to the end of file. So here
         * we are skipping the descriptor data to read the encryption
         * header. */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: MDSv2 file detected. Encryption header offset: %" G_GINT32_MODIFIER "d (0x%" G_GINT32_MODIFIER "X)\n", __debug__, file_header->encryption_header_offset, file_header->encryption_header_offset);

        if (!mirage_stream_seek(self->priv->stream, file_header->encryption_header_offset, G_SEEK_SET, NULL)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to seek to encryption header offset!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Failed to seek to encryption header offset!"));
            return FALSE;
        }
    }

    /* Read encryption header */
    MDX_EncryptionHeader encryption_header;
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading encryption header...\n", __debug__);
    read_bytes = mirage_stream_read(self->priv->stream, &encryption_header, sizeof(encryption_header), NULL);
    if (read_bytes != sizeof(encryption_header)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read encryption header!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Failed to read encryption header!"));
        return FALSE;
    }

    /* Decipher encryption header */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: trying to decipher encryption header...\n", __debug__);
    if (!mdx_crypto_decipher_encryption_header(&encryption_header, NULL, 0, TRUE, &local_error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to decipher encryption header: %s\n", __debug__, local_error->message);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Failed to decipher encryption header: %s!"), local_error->message);
        g_error_free(local_error);
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: encryption header successfully deciphered!\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: compressed descriptor size: %" G_GINT32_MODIFIER "d (0x%" G_GINT32_MODIFIER "X)!\n", __debug__, encryption_header.compressed_size, encryption_header.compressed_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: decompressed descriptor size: %" G_GINT32_MODIFIER "d (0x%" G_GINT32_MODIFIER "X)!\n", __debug__, encryption_header.decompressed_size, encryption_header.decompressed_size);

    /* Determine offset from which we can read the descriptor data. */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading descriptor data...\n", __debug__);

    guint64 descriptor_offset;
    guint64 descriptor_size;

    if (self->priv->is_mdx) {
        /* In MDX, descriptor data is located at the start of the footer */
        descriptor_offset = mdx_footer_offset;
        descriptor_size = mdx_footer_length - MDX_PKCS5_SALT_SIZE;
    } else {
        /* In MDSv2, descriptor data is located after MDSv2/MDX header (48 bytes) */
        descriptor_offset = sizeof(MDX_FileHeader);
        descriptor_size = file_header->encryption_header_offset - descriptor_offset;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: descriptor data offset: %" G_GINT64_MODIFIER "d (0x%" G_GINT64_MODIFIER "X)!\n", __debug__, descriptor_offset, descriptor_offset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: descriptor data length: %" G_GINT64_MODIFIER "d (0x%" G_GINT64_MODIFIER "X)!\n", __debug__, descriptor_size, descriptor_size);

    /* Sanity check on descriptor size.
     * NOTE: since descriptor data is encrypted with AES-256, it needs to
     * be padded so that its length is a multiple of block size (16 bytes).
     * The compressed_size field in the encryption header stores the original,
     * non-padded size! */
    guint64 expected_descriptor_size = ((encryption_header.compressed_size / 16) + (encryption_header.compressed_size % 16 != 0)) * 16;
    if (descriptor_size != expected_descriptor_size) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: sanity check on descriptor size failed! Expected size is %" G_GINT64_MODIFIER "d, actual size is %" G_GINT64_MODIFIER "d!\n", __debug__, expected_descriptor_size, descriptor_size);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Sanity check on descriptor size failed!"));
        return FALSE;
    }

    /* Read raw data */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: reading descriptor data...\n", __debug__);

    guint8 *descriptor_raw = g_malloc(expected_descriptor_size);
    if (!descriptor_raw) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to allocate read buffer for descriptor data!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Failed to allocate read buffer for descriptor data!"));
        return FALSE;
    }

    mirage_stream_seek(self->priv->stream, descriptor_offset, G_SEEK_SET, NULL);
    read_bytes = mirage_stream_read(self->priv->stream, descriptor_raw, descriptor_size, NULL);
    if ((gsize)read_bytes != descriptor_size) {
        g_free(descriptor_raw);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read all descriptor data (read %" G_GINT64_MODIFIER "d out of %" G_GINT64_MODIFIER "d)!\n", __debug__, read_bytes, descriptor_size);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Failed to read all descriptor data!"));
        return FALSE;
    }

    /* Decipher and decompress */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: deciphering and decompressing descriptor...\n", __debug__);

    guint8 *descriptor_data = mdx_crypto_decipher_and_decompress_descriptor(
        descriptor_raw, /* will be decrypted in-place */
        descriptor_size,
        &encryption_header,
        &local_error
    );
    g_free(descriptor_raw);

    if (!descriptor_data) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to decipher/decompress descriptor data: %s\n", __debug__, local_error->message);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Failed to decipher/decompress descriptor data: %s!"), local_error->message);
        g_error_free(local_error);
        return FALSE;
    }

    /* First 18 bytes in returned descriptor data are left empty, so we
     * can copy in first 18 bytes of the file header (signature plus
     * version fields). While this is mostly pointless, it makes it
     * easier to deal with offsets within the descriptor (which seem
     * to be accounting for this 18-byte prefix). */
    memcpy(descriptor_data, (guint8 *)file_header, 18);

    *out_data = descriptor_data;
    *out_data_size = encryption_header.decompressed_size + 18;

    return TRUE;
}

static gboolean mirage_parser_mdx_read_data_encryption_header (MirageParserMdx *self, const MDX_EncryptionHeader **encryption_header_out, GError **error)
{
    const MDX_DescriptorHeader *descriptor_header = (MDX_DescriptorHeader *)self->priv->descriptor_data; /* Endianess has been fixed up already. */

    if (!descriptor_header->encryption_header_offset) {
        /* No encryption header available; do nothing and return success status */
        *encryption_header_out = NULL;
        return TRUE;
    }
    MDX_EncryptionHeader *encryption_header = (MDX_EncryptionHeader *)(self->priv->descriptor_data + descriptor_header->encryption_header_offset);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: image contains encrypted data!\n", __debug__);

    /* Sanity check - encryption header should be located at the end of descriptor */
    guint32 encryption_header_size = self->priv->descriptor_size - descriptor_header->encryption_header_offset;
    if (encryption_header_size != sizeof(MDX_EncryptionHeader)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unexpected size of encryption header for image data - expected %" G_GINT64_MODIFIER "d, found %d!\n", __debug__, sizeof(MDX_EncryptionHeader), encryption_header_size);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Unexpected size of encryption header for image data!"));
        return FALSE;
    }

    /* First, attempt to decipher without a password (i.e., derive password from
     * salt data, same as with descriptor's encryption header). This seems to be
     * used by some profiles, such as TAGES, for password-less encryption of track
     * data. */

    /* As decipher is attempted in-place, we need to create a backup copy of
     * encryption header data, so we can restore it for the password-based attempt. */
    MDX_EncryptionHeader *header_backup = g_new(MDX_EncryptionHeader, 1);
    if (!header_backup) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to allocate buffer for encryption header backup!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Failed to allocate buffer!"));
        return FALSE;
    }
    memcpy(header_backup, encryption_header, sizeof(MDX_EncryptionHeader));

    if (mdx_crypto_decipher_encryption_header(encryption_header, NULL, 0, FALSE, NULL)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: encryption header deciphered without user password!\n", __debug__);
        g_free(header_backup);
        *encryption_header_out = encryption_header;
        return TRUE;
    }

    memcpy(encryption_header, header_backup, sizeof(MDX_EncryptionHeader));
    g_free(header_backup);

    /* We need password... */
    gchar *password;
    gsize password_length;

    GVariant *password_value = mirage_contextual_get_option(MIRAGE_CONTEXTUAL(self), "password");
    if (password_value) {
        password = g_variant_dup_string(password_value, &password_length);
        g_variant_unref(password_value);
    } else {
        /* Get password from user via password function */
        password = mirage_contextual_obtain_password(MIRAGE_CONTEXTUAL(self), NULL);
        if (!password) {
            /* Password not provided (or password function is not set) */
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to obtain password for encrypted image!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_ENCRYPTED_IMAGE, Q_("Image is encrypted!"));
            return FALSE;
        }
        password_length = strlen(password);
    }

    /* Decipher encryption header */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: trying to decipher encryption header with user-supplied password...\n", __debug__);

    GError *local_error = NULL;
    if (!mdx_crypto_decipher_encryption_header(encryption_header, password, password_length, FALSE, &local_error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to decipher encryption header: %s\n", __debug__, local_error->message);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Failed to decipher encryption header for image data! Incorrect password?"));
        g_error_free(local_error);
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: encryption header successfully deciphered!\n", __debug__);

    *encryption_header_out = encryption_header;
    return TRUE;
}


/**********************************************************************\
 *                MirageParser methods implementation                *
\**********************************************************************/
static MirageDisc *mirage_parser_mdx_load_image (MirageParser *_self, MirageStream **streams, GError **error)
{
    MirageParserMdx *self = MIRAGE_PARSER_MDX(_self);

    gssize read_bytes;
    MDX_FileHeader mdx_header;
    gboolean succeeded;

    /* Check if we can load the image */
    self->priv->stream = g_object_ref(streams[0]);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: checking if parser can handle given image...\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: verifying signature at the beginning of the file...\n", __debug__);

    /* Read MDX/MDSv2 header */
    mirage_stream_seek(self->priv->stream, 0, G_SEEK_SET, NULL);
    read_bytes = mirage_stream_read(self->priv->stream, &mdx_header, sizeof(mdx_header), NULL);
    if (read_bytes != sizeof(mdx_header)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: parser cannot handle given image: failed to read MDX/MDSv2 header!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Parser cannot handle given image: failed to read MDX/MDSv2 header!"));
        return FALSE;
    }

    /* Check "MEDIA DESCRIPTOR" signature and the format major version;
     * this parser handles only v2.X images (new DT format). */
    if (memcmp(mdx_header.media_descriptor, MDX_MEDIA_DESCRIPTOR, sizeof(mdx_header.media_descriptor)) != 0 || mdx_header.version_major != 2) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: parser cannot handle given image: invalid signature and/or version!\n", __debug__);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Parser cannot handle given image: invalid signature and/or version!"));
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_IMAGE_ID, "%s: image is MDX/MDSv2 image - will try to parse it!\n", __debug__);

    /* Fix endianness on multi-byte fields */
    mdx_header.encryption_header_offset = GUINT32_FROM_LE(mdx_header.encryption_header_offset);

    /* Ensure that libgcrypt is initialized - from this point on, we will need it! */
    const gchar *required_libgcrypt_version = "1.2.0"; /* All versions after v1.2 should be API/ABI compatible */
    if (!gcry_control(GCRYCTL_INITIALIZATION_FINISHED_P)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: libgcrypt is not yet initialized. Initializing...\n", __debug__);

        if (!gcry_check_version(required_libgcrypt_version)) {
            const gchar *libgcrypt_version = gcry_check_version(NULL);
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: installed version of libgcrypt (%s) does not satisfy minimum requirement (%s)!\n", __debug__, libgcrypt_version, required_libgcrypt_version);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Installed version of libgcrypt (%s) does not satisfy minimum requirement (%s)!"), libgcrypt_version, required_libgcrypt_version);
            return FALSE;
        }

        gcry_control(GCRYCTL_SUSPEND_SECMEM_WARN);
        gcry_control(GCRYCTL_INIT_SECMEM, 16384, 0);
        gcry_control(GCRYCTL_RESUME_SECMEM_WARN);

        gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: libgcrypt is already initialized.\n", __debug__);
    }

    /* Get descriptor data */
    if (!mirage_parser_mdx_read_descriptor(self, &mdx_header, &self->priv->descriptor_data, &self->priv->descriptor_size, error)) {
        return FALSE;
    }

    /* Dump descriptor data */
    if (MIRAGE_DEBUG_ON(self, MIRAGE_DEBUG_PARSER)) {
        const guint8 *descriptor_data = self->priv->descriptor_data;
        const guint64 descriptor_size = self->priv->descriptor_size;
        GString *descriptor_dump = g_string_new("");
        for (gsize i = 0; i < descriptor_size; i++) {
            g_string_append_printf(descriptor_dump, "%02hhx", descriptor_data[i]);
            if (((i + 1) % 32 == 0) && (i != descriptor_size - 1)) {
                g_string_append(descriptor_dump, "\n");
            } else {
                g_string_append(descriptor_dump, " ");
            }
        }

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: descriptor data (%" G_GINT64_MODIFIER "d):\n%s\n", __debug__, descriptor_size, descriptor_dump->str);

        g_string_free(descriptor_dump, TRUE);
    }

    /* Fix endianness on descriptor header, and display it */
    MDX_DescriptorHeader *descriptor_header = (MDX_DescriptorHeader *)self->priv->descriptor_data;
    mdx_descriptor_header_fix_endian(descriptor_header);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: MDX descriptor header:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  signature: %.16s\n", __debug__, descriptor_header->media_descriptor);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  version: %u.%u\n", __debug__, descriptor_header->version_major, descriptor_header->version_minor);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  medium type: 0x%X\n", __debug__, descriptor_header->medium_type);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  number of sessions: 0x%X\n", __debug__, descriptor_header->num_sessions);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  session blocks offset: 0x%X\n", __debug__, descriptor_header->sessions_blocks_offset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  DPM blocks offset: 0x%X\n", __debug__, descriptor_header->dpm_blocks_offset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  encryption header offset: 0x%X\n", __debug__, descriptor_header->encryption_header_offset);

    /* Check if descriptor contains encryption header for track data */
    if (!mirage_parser_mdx_read_data_encryption_header(self, &self->priv->data_encryption_header, error)) {
        return FALSE;
    }

    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_set_parent(MIRAGE_OBJECT(self->priv->disc), self);

    mirage_disc_set_filename(self->priv->disc, mirage_stream_get_filename(self->priv->stream));

    /* Parse descriptor */
    switch (descriptor_header->medium_type) {
        case MDX_MEDIUM_CD_ROM: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: CD-ROM image\n", __debug__);
            mirage_disc_set_medium_type(self->priv->disc, MIRAGE_MEDIUM_CD);
            succeeded = mirage_parser_mdx_load_disc(self, error);
            break;
        }
        case MDX_MEDIUM_DVD_ROM: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: DVD-ROM image\n", __debug__);
            mirage_disc_set_medium_type(self->priv->disc, MIRAGE_MEDIUM_DVD);
            succeeded = mirage_parser_mdx_load_disc(self, error);
            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: medium of type 0x%X not supported yet!\n", __debug__, descriptor_header->medium_type);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Medium of type 0x%X not supported yet!"), descriptor_header->medium_type);
            succeeded = FALSE;
            break;
        }
    }

    /* Cleanup */
    g_free(self->priv->descriptor_data);
    self->priv->descriptor_data = NULL;

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
static void mirage_parser_mdx_init (MirageParserMdx *self)
{
    self->priv = mirage_parser_mdx_get_instance_private(self);

    mirage_parser_generate_info(MIRAGE_PARSER(self),
        "PARSER-MDX",
        Q_("MDX Image Parser"),
        1,
        Q_("DaemonTools images (*.mdx, *.mds)"), "application/x-mdx"
    );

    self->priv->disc = NULL;

    self->priv->stream = NULL;

    self->priv->descriptor_data = NULL;
    self->priv->data_encryption_header = NULL;
}

static void mirage_parser_mdx_dispose (GObject *gobject)
{
    MirageParserMdx *self = MIRAGE_PARSER_MDX(gobject);

    if (self->priv->stream) {
        g_object_unref(self->priv->stream);
        self->priv->stream = NULL;
    }

    /* Chain up to the parent class */
    G_OBJECT_CLASS(mirage_parser_mdx_parent_class)->dispose(gobject);
}

static void mirage_parser_mdx_finalize (GObject *gobject)
{
    MirageParserMdx *self = MIRAGE_PARSER_MDX(gobject);

    g_free(self->priv->descriptor_data);

    /* Chain up to the parent class */
    G_OBJECT_CLASS(mirage_parser_mdx_parent_class)->finalize(gobject);
}

static void mirage_parser_mdx_class_init (MirageParserMdxClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageParserClass *parser_class = MIRAGE_PARSER_CLASS(klass);

    gobject_class->dispose = mirage_parser_mdx_dispose;
    gobject_class->finalize = mirage_parser_mdx_finalize;

    parser_class->load_image = mirage_parser_mdx_load_image;
}

static void mirage_parser_mdx_class_finalize (MirageParserMdxClass *klass G_GNUC_UNUSED)
{
}
