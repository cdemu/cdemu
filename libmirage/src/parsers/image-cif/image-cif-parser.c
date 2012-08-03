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

    gchar *cif_filename;
    FILE *file;

    /* "disc" block offset and length */
    guint64 disc_offset;
    guint32 disc_length;

    /* "ofs " block offset and length */
    guint64 ofs_offset;
    guint32 ofs_length;

    /* Parsed offset entries */
    gint num_offset_entries;
    CIF_OffsetEntry *offset_entries;

    gint track_counter;

    gint prev_track_mode;
};


/**********************************************************************\
 *                           Debug helpers                            *
\**********************************************************************/
static inline const gchar *debug_image_type (gint type)
{
    switch (type) {
        case CIF_IMAGE_DATA: return "Data CD";
        case CIF_IMAGE_MIXED: return "MixedMode CD";
        case CIF_IMAGE_MUSIC: return "Music CD";
        case CIF_IMAGE_ENCHANCED: return "Enchanced CD";
        case CIF_IMAGE_VIDEO: return "Video CD";
        case CIF_IMAGE_BOOTALBE: return "Bootable CD";
        case CIF_IMAGE_MP3: return "MP3 CD";
        default: return "UNKNOWN";
    }
}

static inline const gchar *debug_session_type (gint type)
{
    switch (type) {
        case CIF_SESSION_CDDA: return "CD-DA";
        case CIF_SESSION_CDROM: return "CD-ROM";
        case CIF_SESSION_CDROMXA: return "CD-ROM XA";
        default: return "UNKNOWN";
    }
}

static inline const gchar *debug_track_type (gint type)
{
    switch (type) {
        case CIF_TRACK_AUDIO: return "Audio";
        case CIF_TRACK_MODE1: return "Mode 1";
        case CIF_TRACK_MODE2_FORM1: return "Mode 2 Form 1";
        case CIF_TRACK_MODE2_MIXED: return "Mode 2 Mixed";
        default: return "UNKNOWN";
    }
}


/**********************************************************************\
 *                             Helpers                                *
\**********************************************************************/
static inline gboolean gap_between_tracks (gint mode_prev, gint mode_cur)
{
    /* We create gaps on transitions between audio and data tracks */
    return (mode_prev == MIRAGE_MODE_AUDIO && mode_cur != MIRAGE_MODE_AUDIO)  || (mode_prev != MIRAGE_MODE_AUDIO && mode_cur == MIRAGE_MODE_AUDIO);
}


/**********************************************************************\
 *                   Descriptor reading and parsing                   *
\**********************************************************************/
static gboolean mirage_parser_cif_read_descriptor (MIRAGE_Parser_CIF *self, guint8 **data, guint16 *length, GError **error)
{
    guint16 subblock_length;
    guint8 *subblock_data;

    /* Read entry length */
    if (fread(&subblock_length, sizeof(subblock_length), 1, self->priv->file) < 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to read sub-block length!\n", __debug__);
        mirage_error(MIRAGE_E_READFAILED, error);
        return FALSE;
    }
    subblock_length = GUINT16_FROM_LE(subblock_length);

    /* Go back */
    fseeko(self->priv->file, -sizeof(subblock_length), SEEK_CUR);

    /* Sanity check */
    if (ftello(self->priv->file) + subblock_length > self->priv->disc_offset + self->priv->disc_length) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: sanity check failed!\n", __debug__);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }

    /* Allocate buffer and read data */
    subblock_data = g_malloc(subblock_length);
    if (fread(subblock_data, subblock_length, 1, self->priv->file) < 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to read sub-block data (%d bytes)!\n", __debug__, subblock_length);
        g_free(subblock_data);
        mirage_error(MIRAGE_E_READFAILED, error);
        return FALSE;
    }

    *data = subblock_data;
    *length = subblock_length;

    return TRUE;
}


static GObject *mirage_parser_cif_parse_track_descriptor (MIRAGE_Parser_CIF *self, guint8 *data, guint16 length G_GNUC_UNUSED, GError **error)
{
    GObject *track;
    GObject *fragment;

    CIF_TrackDescriptor *descriptor = (CIF_TrackDescriptor *)data;
    CIF_OffsetEntry *offset_entry = &self->priv->offset_entries[self->priv->track_counter++]; /* Corresponding offset entry; we also increment track counter here */

    CIF_AudioTrackDescriptor *audio_descriptor = (CIF_AudioTrackDescriptor *)(data+sizeof(CIF_TrackDescriptor));

    gint sector_size;
    gint track_length;
    gint track_mode;

    descriptor->descriptor_length = GUINT16_FROM_LE(descriptor->descriptor_length);
    descriptor->dummy1 = GUINT16_FROM_LE(descriptor->dummy1);
    descriptor->num_sectors = GUINT32_FROM_LE(descriptor->num_sectors);
    descriptor->dummy2 = GUINT16_FROM_LE(descriptor->dummy2);
    descriptor->type = GUINT16_FROM_LE(descriptor->type);
    descriptor->dummy3 = GUINT16_FROM_LE(descriptor->dummy3);
    descriptor->dummy4 = GUINT16_FROM_LE(descriptor->dummy4);
    descriptor->dummy5 = GUINT16_FROM_LE(descriptor->dummy5);
    descriptor->dao_mode = GUINT16_FROM_LE(descriptor->dao_mode);
    descriptor->dummy7 = GUINT16_FROM_LE(descriptor->dummy7);
    descriptor->sector_data_size = GUINT16_FROM_LE(descriptor->sector_data_size);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Track descriptor:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - descriptor length: %d (0x%X)\n", __debug__, descriptor->descriptor_length, descriptor->descriptor_length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - dummy1: %d\n", __debug__, descriptor->dummy1);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - number of sectors: %d (0x%X)\n", __debug__, descriptor->num_sectors, descriptor->num_sectors);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - dummy2: %d\n", __debug__, descriptor->dummy2);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - track type: %d (%s)\n", __debug__, descriptor->type, debug_track_type(descriptor->type));
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - dummy3: %d\n", __debug__, descriptor->dummy3);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - dummy4: %d\n", __debug__, descriptor->dummy4);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - dummy5: %d\n", __debug__, descriptor->dummy5);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - dao mode: %d\n", __debug__, descriptor->dao_mode);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - dummy7: %d\n", __debug__, descriptor->dummy7);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - sector data size: %d (0x%X)\n", __debug__, descriptor->sector_data_size, descriptor->sector_data_size);

    if (descriptor->type == CIF_TRACK_AUDIO) {
        audio_descriptor->fadein_length = GUINT16_FROM_LE(audio_descriptor->fadein_length);
        audio_descriptor->fadeout_length = GUINT16_FROM_LE(audio_descriptor->fadeout_length);

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Audio track descriptor:\n", __debug__);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - ISRC: %.12s\n", __debug__, audio_descriptor->isrc);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - fade-out length: %d (0x%X)\n", __debug__, audio_descriptor->fadeout_length, audio_descriptor->fadeout_length);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - fade-in length: %d (0x%X)\n", __debug__, audio_descriptor->fadein_length, audio_descriptor->fadein_length);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - title: \"%s\"\n", __debug__, audio_descriptor->title);
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Offset entry:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - type: %.4s\n", __debug__, offset_entry->type);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - offset: %d (0x%X)\n", __debug__, offset_entry->offset, offset_entry->offset);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - length: %d (0x%X)\n", __debug__, offset_entry->length, offset_entry->length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");

    /* Set track mode */
    switch (descriptor->type) {
        case CIF_TRACK_AUDIO: {
            track_mode = MIRAGE_MODE_AUDIO;
            sector_size = 2352;
            break;
        }
        case CIF_TRACK_MODE1: {
            track_mode = MIRAGE_MODE_MODE1;
            sector_size = 2048;
            break;
        }
        case CIF_TRACK_MODE2_FORM1: {
            track_mode = MIRAGE_MODE_MODE2_FORM1;
            sector_size = 2056;
            break;
        }
        case CIF_TRACK_MODE2_MIXED: {
            track_mode = MIRAGE_MODE_MODE2_MIXED;
            sector_size = 2332;
            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown track type (%d)!\n", __debug__, descriptor->type);
            mirage_error(MIRAGE_E_PARSER, error);
            return FALSE;
        }
    }

    /* Compute the actual track length */
    if (offset_entry->length % sector_size) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: declared data chunk length (%d) not divisible by sector size (%d)!\n", __debug__, offset_entry->length, sector_size);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }
    track_length = offset_entry->length / sector_size;
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: computed track length: %d (0x%X)\n", __debug__, track_length, track_length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: difference between declared and computed track length: %d (0x%X)\n", __debug__, descriptor->num_sectors - track_length, descriptor->num_sectors - track_length);


    /* Create new track */
    track = g_object_new(MIRAGE_TYPE_TRACK, NULL);

    mirage_track_set_mode(MIRAGE_TRACK(track), track_mode, NULL);

    /* Create data fragment */
    fragment = libmirage_create_fragment(MIRAGE_TYPE_FRAG_IFACE_BINARY, self->priv->cif_filename, error);
    if (!fragment) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create fragment!\n", __debug__);
        g_object_unref(track);
        return FALSE;
    }

    /* Set file */
    if (!mirage_frag_iface_binary_track_file_set_file(MIRAGE_FRAG_IFACE_BINARY(fragment), self->priv->cif_filename, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to set track data file!\n", __debug__);
        g_object_unref(fragment);
        g_object_unref(track);
        return FALSE;
    }

    /* Set offset, length, sector size and data type */
    mirage_frag_iface_binary_track_file_set_offset(MIRAGE_FRAG_IFACE_BINARY(fragment), offset_entry->offset, NULL);
    mirage_frag_iface_binary_track_file_set_sectsize(MIRAGE_FRAG_IFACE_BINARY(fragment), sector_size, NULL);
    if (descriptor->type == CIF_TRACK_AUDIO) {
        mirage_frag_iface_binary_track_file_set_format(MIRAGE_FRAG_IFACE_BINARY(fragment), FR_BIN_TFILE_AUDIO, NULL);
    } else {
        mirage_frag_iface_binary_track_file_set_format(MIRAGE_FRAG_IFACE_BINARY(fragment), FR_BIN_TFILE_DATA, NULL);
    }

    mirage_fragment_set_length(MIRAGE_FRAGMENT(fragment), track_length, NULL);

    mirage_track_add_fragment(MIRAGE_TRACK(track), -1, &fragment, error);

    g_object_unref(fragment);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");

    return track;
}

static GObject *mirage_parser_cif_parse_session_descriptor (MIRAGE_Parser_CIF *self, guint8 *data, guint16 length G_GNUC_UNUSED, GError **error)
{
    GObject *session;
    CIF_SessionDescriptor *descriptor = (CIF_SessionDescriptor *)data;
    gint i;

    descriptor->descriptor_length = GUINT16_FROM_LE(descriptor->descriptor_length);
    descriptor->num_tracks = GUINT16_FROM_LE(descriptor->num_tracks);
    descriptor->dummy1 = GUINT16_FROM_LE(descriptor->dummy1);
    descriptor->dummy2 = GUINT16_FROM_LE(descriptor->dummy2);
    descriptor->dummy3 = GUINT16_FROM_LE(descriptor->dummy3);
    descriptor->session_type = GUINT16_FROM_LE(descriptor->session_type);
    descriptor->dummy4 = GUINT16_FROM_LE(descriptor->dummy4);
    descriptor->dummy5 = GUINT16_FROM_LE(descriptor->dummy5);
    descriptor->dummy6 = GUINT16_FROM_LE(descriptor->dummy6);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Session descriptor:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - descriptor length: %d (0x%X)\n", __debug__, descriptor->descriptor_length, descriptor->descriptor_length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - number of tracks: %d\n", __debug__, descriptor->num_tracks);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - dummy1: %d\n", __debug__, descriptor->dummy1);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - dummy2: %d\n", __debug__, descriptor->dummy2);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - dummy3: %d\n", __debug__, descriptor->dummy3);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - session type: %d (%s)\n", __debug__, descriptor->session_type, debug_session_type(descriptor->session_type));
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - dummy4: %d\n", __debug__, descriptor->dummy4);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - dummy5: %d\n", __debug__, descriptor->dummy5);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - dummy6: %d\n", __debug__, descriptor->dummy6);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");


    /* Create new session */
    session = g_object_new(MIRAGE_TYPE_SESSION, NULL);

    /* Set session type */
    switch (descriptor->session_type) {
        case CIF_SESSION_CDDA: {
            mirage_session_set_session_type(MIRAGE_SESSION(session), MIRAGE_SESSION_CD_DA, NULL);
            break;
        }
        case CIF_SESSION_CDROM: {
            mirage_session_set_session_type(MIRAGE_SESSION(session), MIRAGE_SESSION_CD_ROM, NULL);
            break;
        }
        case CIF_SESSION_CDROMXA: {
            mirage_session_set_session_type(MIRAGE_SESSION(session), MIRAGE_SESSION_CD_ROM_XA, NULL);
            break;
        }
        default: {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown session type (%d)!\n", __debug__, descriptor->session_type);
            g_object_unref(session);
            return FALSE;
        }
    }

    /* Process all tracks */
    for (i = 0; i < descriptor->num_tracks; i++) {
        guint8 *descriptor_data;
        guint16 descriptor_length;
        GObject *track;
        gint track_mode;

        /* Read track descriptor */
        if (!mirage_parser_cif_read_descriptor(self, &descriptor_data, &descriptor_length, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read track descriptor!\n", __debug__);
            g_object_unref(session);
            return FALSE;
        }

        /* Parse track descriptor */
        track = mirage_parser_cif_parse_track_descriptor(self, descriptor_data, descriptor_length, error);
        if (!track) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse track descriptor!\n", __debug__);
            g_object_unref(session);
            return FALSE;
        }

        mirage_track_get_mode(MIRAGE_TRACK(track), &track_mode, NULL);

        /* If it is a first track in session, or if track mode has changed,
           add 150-sector pregap. */
        if (i == 0 || gap_between_tracks(self->priv->prev_track_mode, track_mode)) {
            GObject *fragment;
            gint pregap_length = 150;

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: adding pregap: %d (0x%X)\n", __debug__, pregap_length, pregap_length);

            /* Create NULL fragment */
            fragment = libmirage_create_fragment(MIRAGE_TYPE_FRAG_IFACE_NULL, "NULL", error);
            if (!fragment) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to create NULL fragment!\n", __debug__);
                g_object_unref(session);
                g_object_unref(track);
                return FALSE;
            }

            mirage_fragment_set_length(MIRAGE_FRAGMENT(fragment), pregap_length, NULL);

            mirage_track_add_fragment(MIRAGE_TRACK(track), 0, &fragment, error);
            g_object_unref(fragment);

            /* Set new track start */
            mirage_track_set_track_start(MIRAGE_TRACK(track), pregap_length, NULL);
        }

        /* Add track */
        if (!mirage_session_add_track_by_index(MIRAGE_SESSION(session), i, &track, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add track!\n", __debug__);
            g_object_unref(session);
            g_object_unref(track);
            return FALSE;
        }

        g_object_unref(track);
        g_free(descriptor_data);

        self->priv->prev_track_mode = track_mode; /* Store track mode */
    }

    return session;
}


static gboolean mirage_parser_cif_parse_disc_descriptor (MIRAGE_Parser_CIF *self, guint8 *data, guint16 length G_GNUC_UNUSED, GError **error)
{
    CIF_DiscDescriptor *descriptor = (CIF_DiscDescriptor *)data;
    gint i;

    descriptor->descriptor_length = GUINT16_FROM_LE(descriptor->descriptor_length);
    descriptor->num_sessions = GUINT16_FROM_LE(descriptor->num_sessions);
    descriptor->num_tracks = GUINT16_FROM_LE(descriptor->num_tracks);
    descriptor->title_length = GUINT16_FROM_LE(descriptor->title_length);
    descriptor->descriptor_length2 = GUINT16_FROM_LE(descriptor->descriptor_length2);
    descriptor->dummy1 = GUINT16_FROM_LE(descriptor->dummy1);
    descriptor->image_type = GUINT16_FROM_LE(descriptor->image_type);
    descriptor->dummy2 = GUINT16_FROM_LE(descriptor->dummy2);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: Disc descriptor:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - descriptor length: %d (0x%X)\n", __debug__, descriptor->descriptor_length, descriptor->descriptor_length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - number of sessions: %d\n", __debug__, descriptor->num_sessions);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - number of tracks: %d\n", __debug__, descriptor->num_tracks);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - title length: %d (0x%X)\n", __debug__, descriptor->title_length, descriptor->title_length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - descriptor length 2: %d (0x%X)\n", __debug__, descriptor->descriptor_length2, descriptor->descriptor_length2);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - dummy1: %d (0x%X)\n", __debug__, descriptor->dummy1, descriptor->dummy1);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - image type: %d (%s)\n", __debug__, descriptor->image_type, debug_image_type(descriptor->image_type));
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - dummy2: %d (0x%X)\n", __debug__, descriptor->dummy2, descriptor->dummy2);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - title: \"%.*s\"\n", __debug__, descriptor->title_length, descriptor->title_and_artist);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  - artist: \"%s\"\n", __debug__, descriptor->title_and_artist+descriptor->title_length);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");


    /* Process all sessions */
    for (i = 0; i < descriptor->num_sessions; i++) {
        guint8 *descriptor_data;
        guint16 descriptor_length;
        GObject *session;

        /* Read session descriptor */
        if (!mirage_parser_cif_read_descriptor(self, &descriptor_data, &descriptor_length, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read session descriptor!\n", __debug__);
            return FALSE;
        }

        /* Parse session descriptor */
        session = mirage_parser_cif_parse_session_descriptor(self, descriptor_data, descriptor_length, error);
        if (!session) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to parse session descriptor!\n", __debug__);
            return FALSE;
        }

        if (!mirage_disc_add_session_by_index(MIRAGE_DISC(self->priv->disc), i, &session, error)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to add session!\n", __debug__);
            return FALSE;
        }

        g_object_unref(session);
        g_free(descriptor_data);

        /* Set leadout of the previous session */
        if (i > 0) {
            GObject *prev_session;
            gint leadout_length;

            if (!mirage_disc_get_session_by_index(MIRAGE_DISC(self->priv->disc), i-1, &prev_session, error)) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: failed to get previous session!\n", __debug__);
                return FALSE;
            }

            /* Second session has index 1... */
            if (i == 1) {
                leadout_length = 11250; /* Actually, it should be 6750 previous leadout, 4500 current leadin */
            } else {
                leadout_length = 6750; /* Actually, it should be 2250 previous leadout, 4500 current leadin */
            }

            mirage_session_set_leadout_length(MIRAGE_SESSION(prev_session), leadout_length, NULL);

            g_object_unref(prev_session);
        }
    }


    return TRUE;
}


static gboolean mirage_parser_cif_parse_disc_block (MIRAGE_Parser_CIF *self, GError **error)
{
    guint8 *descriptor_data;
    guint16 descriptor_length;

    /* Seek to the content of "disc" block */
    fseeko(self->priv->file, self->priv->disc_offset, SEEK_SET);

    /* Skip first 8 dummy bytes */
    fseeko(self->priv->file, 8, SEEK_CUR);

    /* Read disc descriptor */
    if (!mirage_parser_cif_read_descriptor(self, &descriptor_data, &descriptor_length, error)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read disc descriptor!\n", __debug__);
        return FALSE;
    }

    /* Parse disc descriptor */
    if (!mirage_parser_cif_parse_disc_descriptor(self, descriptor_data, descriptor_length, error)) {
        g_free(descriptor_data);
        return FALSE;
    }

    g_free(descriptor_data);

    /* Sanity check */
    if (ftello(self->priv->file) != (self->priv->disc_offset + self->priv->disc_length)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: WARNING: did not finish at the end of disc block!\n", __debug__);
    }

    return TRUE;
}

static gboolean mirage_parser_cif_parse_ofs_block (MIRAGE_Parser_CIF *self, GError **error)
{
    guint16 num_entries;
    gint i;

    /* Seek to the content of "ofs " block */
    fseeko(self->priv->file, self->priv->ofs_offset, SEEK_SET);

    /* Skip first 8 dummy bytes */
    fseeko(self->priv->file, 8, SEEK_CUR);

    /* Read number of entries */
    if (fread(&num_entries, sizeof(num_entries), 1, self->priv->file) < 1) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read number of entries!\n", __debug__);
        mirage_error(MIRAGE_E_READFAILED, error);
        return FALSE;
    }
    num_entries = GUINT16_FROM_LE(num_entries);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: number of entries: %d\n", __debug__, num_entries);

    self->priv->offset_entries = g_new0(CIF_OffsetEntry, num_entries);
    self->priv->num_offset_entries = num_entries;

    for (i = 0; i < num_entries && ftell(self->priv->file) < (self->priv->ofs_offset + self->priv->ofs_length); i++) {
        CIF_OffsetEntry entry;

        /* Read whole entry */
        if (fread(&entry, sizeof(entry), 1, self->priv->file) < 1) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read off entry!\n", __debug__);
            mirage_error(MIRAGE_E_READFAILED, error);
            return FALSE;
        }

        /* Match "RIFF" */
        if (memcmp(entry.riff, "RIFF", 4)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: expected 'RIFF', got '%.4s'\n", __debug__, entry.riff);
            mirage_error(MIRAGE_E_PARSER, error);
            return FALSE;
        }

        entry.length = GUINT32_FROM_LE(entry.length) - 4; /* Since it includes the size of length field */
        entry.offset = GUINT32_FROM_LE(entry.offset) + 4; /* Since it's offset to type field, which we need to skip */

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: entry #%d: type '%.4s', offset %d (0x%X), length %d (0x%X)\n", __debug__, i, entry.type, entry.offset, entry.offset, entry.length, entry.length);

        /* Store */
        self->priv->offset_entries[i] = entry;
    }

    /* Sanity check */
    if (ftello(self->priv->file) != (self->priv->ofs_offset + self->priv->ofs_length)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: WARNING: did not finish at the end of ofs block!\n", __debug__);
    }

    return TRUE;
}


static gboolean mirage_parser_cif_parse_blocks (MIRAGE_Parser_CIF *self, GError **error)
{
    guint64 file_size;
    CIF_Header header;
    guint64 offset;

    /* Get file length */
    fseeko(self->priv->file, 0, SEEK_END);
    file_size = ftello(self->priv->file);

    /* Build blocks list */
    fseeko(self->priv->file, 0, SEEK_SET);
    while (ftello(self->priv->file) < file_size) {
        /* Read whoe header */
        if (fread(&header, sizeof(header), 1, self->priv->file) < 1) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read header!\n", __debug__);
            mirage_error(MIRAGE_E_READFAILED, error);
            return FALSE;
        }

        /* Match "RIFF" */
        if (memcmp(header.riff, "RIFF", 4)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: expected 'RIFF', got '%.4s'\n", __debug__, header.riff);
            mirage_error(MIRAGE_E_PARSER, error);
            return FALSE;
        }

        header.length = GUINT32_FROM_LE(header.length) - 4;

        /* Store data offset */
        offset = ftello(self->priv->file);

        /* Skip the contents */
        fseeko(self->priv->file, header.length, SEEK_CUR);
        if (header.length % 2) {
            fseeko(self->priv->file, 1, SEEK_CUR); /* Pad byte */
        }

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: RIFF chunk of type '%.4s': offset %lld (0x%llX), length %ld (0x%lX)\n", __debug__, header.type, offset, offset, header.length, header.length);

        /* We need to store "disc" and "ofs " offsets */
        if (!memcmp(header.type, "disc", 4)) {
            self->priv->disc_offset = offset;
            self->priv->disc_length = header.length;
        } else if (!memcmp(header.type, "ofs ", 4)) {
            self->priv->ofs_offset = offset;
            self->priv->ofs_length = header.length;
        }
    }

    /* Make sure we got "disc" and "ofs " blocks */
    if (!self->priv->disc_offset || !self->priv->disc_length) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 'disc' block not found!\n", __debug__);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }

    if (!self->priv->ofs_offset || !self->priv->ofs_length) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: 'ofs ' block not found!\n", __debug__);
        mirage_error(MIRAGE_E_PARSER, error);
        return FALSE;
    }

    return TRUE;
}


static gboolean mirage_parser_cif_load_disc (MIRAGE_Parser_CIF *self, GError **error)
{
    /* Parse blocks */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing RIFF blocks\n", __debug__);
    if (!mirage_parser_cif_parse_blocks(self, error)) {
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");

    /* CIF format is CD-only */
    mirage_disc_set_medium_type(MIRAGE_DISC(self->priv->disc), MIRAGE_MEDIUM_CD, NULL);

    /* CD-ROMs start at -150 as per Red Book... */
    mirage_disc_layout_set_start_sector(MIRAGE_DISC(self->priv->disc), -150, NULL);

    /* Parse "ofs " block */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing 'ofs ' block\n", __debug__);
    if (!mirage_parser_cif_parse_ofs_block(self, error)) {
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");

    /* Parse "disc" block */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: parsing 'disc' block\n", __debug__);
    if (!mirage_parser_cif_parse_disc_block(self, error)) {
        return FALSE;
    }
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "\n");

    return TRUE;
}


/**********************************************************************\
 *                MIRAGE_Parser methods implementation                *
\**********************************************************************/
static gboolean mirage_parser_cif_load_image (MIRAGE_Parser *_self, gchar **filenames, GObject **disc, GError **error)
{
    MIRAGE_Parser_CIF *self = MIRAGE_PARSER_CIF(_self);

    gboolean succeeded = TRUE;
    FILE *file;
    CIF_Header header;

    /* Check file signature */
    file = g_fopen(filenames[0], "r");
    if (!file) {
        mirage_error(MIRAGE_E_IMAGEFILE, error);
        return FALSE;
    }

    if (fread(&header, sizeof(header), 1, file) < 1) {
        fclose(file);
        mirage_error(MIRAGE_E_READFAILED, error);
        return FALSE;
    }

    /* Match "RIFF" at the beginning and "imag" that comes after length
       field. We could probably also match length, since it appears to
       be fixed, but this should sufficient... */
    if (memcmp(header.riff, "RIFF", 4) || memcmp(header.type, "imag", 4)) {
        fclose(file);
        mirage_error(MIRAGE_E_CANTHANDLE, error);
        return FALSE;
    }


    /* Create disc */
    self->priv->disc = g_object_new(MIRAGE_TYPE_DISC, NULL);
    mirage_object_attach_child(MIRAGE_OBJECT(self), self->priv->disc, NULL);

    mirage_disc_set_filename(MIRAGE_DISC(self->priv->disc), filenames[0], NULL);
    self->priv->cif_filename = g_strdup(filenames[0]);


    /* Load disc */
    self->priv->file = file;

    succeeded = mirage_parser_cif_load_disc(self, error);

    fclose(self->priv->file);
    self->priv->file = NULL;


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
    self->priv->offset_entries = NULL;

    self->priv->track_counter = 0;
}

static void mirage_parser_cif_finalize (GObject *gobject)
{
    MIRAGE_Parser_CIF *self = MIRAGE_PARSER_CIF(gobject);

    g_free(self->priv->cif_filename);
    g_free(self->priv->offset_entries);

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
