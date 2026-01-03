/*
 *  libMirage: SNDFILE filter: filter stream
 *  Copyright (C) 2012-2014 Rok Mandeljc
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

#include "filter-sndfile.h"

#define __debug__ "SNDFILE-FilterStream"

/* Number of frames to cache (588 frames/sector, 75 sectors/second) */
#define NUM_FRAMES 588*75


/**********************************************************************\
 *                  Object and its private structure                  *
\**********************************************************************/
struct _MirageFilterStreamSndfilePrivate
{
    SNDFILE *sndfile;
    SF_INFO format;

    gint buflen;
    guint8 *buffer;

    gint cached_block;

    /* Resampling */
    double io_ratio;
    float *resample_buffer_in;
    float *resample_buffer_out;
    SRC_STATE *resampler;
    SRC_DATA resampler_data;
};


G_DEFINE_DYNAMIC_TYPE_EXTENDED(
    MirageFilterStreamSndfile,
    mirage_filter_stream_sndfile,
    MIRAGE_TYPE_FILTER_STREAM,
    0,
    G_ADD_PRIVATE_DYNAMIC(MirageFilterStreamSndfile)
)

void mirage_filter_stream_sndfile_type_register (GTypeModule *type_module)
{
    mirage_filter_stream_sndfile_register_type(type_module);
}


/**********************************************************************\
 *                        libsndfile I/O bridge                        *
\**********************************************************************/
static sf_count_t sndfile_io_get_filelen (MirageStream *stream)
{
    goffset position, size;

    /* Store current position */
    position = mirage_stream_tell(stream);

    /* Seek to the end of file and get position */
    mirage_stream_seek(stream, 0, G_SEEK_END, NULL);
    size = mirage_stream_tell(stream);

    /* Restore position */
    mirage_stream_seek(stream, position, G_SEEK_SET, NULL);

    return size;
}

static sf_count_t sndfile_io_seek (sf_count_t offset, int whence, MirageStream *stream)
{
    GSeekType seek_type;

    /* Map whence parameter */
    switch (whence) {
        case SEEK_CUR: {
            seek_type = G_SEEK_CUR;
            break;
        }
        case SEEK_SET: {
            seek_type = G_SEEK_SET;
            break;
        }
        case SEEK_END: {
            seek_type = G_SEEK_END;
            break;
        }
        default: {
            /* Should not happen... */
            seek_type = G_SEEK_SET;
            break;
        }
    }

    /* Seek */
    mirage_stream_seek(stream, offset, seek_type, NULL);

    return mirage_stream_tell(stream);
}

static sf_count_t sndfile_io_read (void *ptr, sf_count_t count, MirageStream *stream)
{
    return mirage_stream_read(stream, ptr, count, NULL);
}

static sf_count_t sndfile_io_write (const void *ptr, sf_count_t count, MirageStream *stream)
{
    return mirage_stream_write(stream, ptr, count, NULL);
}

static  sf_count_t sndfile_io_tell (MirageStream *stream)
{
    return mirage_stream_tell(stream);
}

static SF_VIRTUAL_IO sndfile_io_bridge = {
    .get_filelen = (sf_vio_get_filelen)sndfile_io_get_filelen,
    .seek = (sf_vio_seek)sndfile_io_seek,
    .read = (sf_vio_read)sndfile_io_read,
    .write = (sf_vio_write)sndfile_io_write,
    .tell = (sf_vio_tell)sndfile_io_tell,
};


/**********************************************************************\
 *              MirageFilterStream methods implementations            *
\**********************************************************************/
static gboolean mirage_filter_stream_sndfile_open (MirageFilterStream *_self, MirageStream *stream, gboolean writable, GError **error)
{
    MirageFilterStreamSndfile *self = MIRAGE_FILTER_STREAM_SNDFILE(_self);
    gsize length;
    gint open_mode = SFM_READ;

    /* Clear the format */
    memset(&self->priv->format, 0, sizeof(self->priv->format));

    if (writable) {
        /* If we are creating the stream (read/write mode), we need to
           provide format data ourselves */
        const gchar *filename = mirage_stream_get_filename(stream);
        const gchar *suffix = mirage_helper_get_suffix(filename);

        self->priv->format.samplerate = 44100;
        self->priv->format.channels = 2;

        if (!g_ascii_strcasecmp(suffix, ".wav")) {
            self->priv->format.format = SF_FORMAT_WAV;
        } else if (!g_ascii_strcasecmp(suffix, ".aiff")) {
            self->priv->format.format = SF_FORMAT_AIFF;
        } else if (!g_ascii_strcasecmp(suffix, ".flac")) {
            self->priv->format.format = SF_FORMAT_FLAC;
        } else if (!g_ascii_strcasecmp(suffix, ".ogg")) {
            self->priv->format.format = SF_FORMAT_OGG;
        } else {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unknown file suffix '%s'; storing as raw PCM data!\n", __debug__, suffix);
            self->priv->format.format = SF_FORMAT_RAW;
        }
        self->priv->format.format |= SF_FORMAT_PCM_16; /* Minor format */

        open_mode = SFM_RDWR;
    } else {
        const gchar *filename = mirage_stream_get_filename(stream);
        const gchar *suffix = mirage_helper_get_suffix(filename);

        /* Prevent this filter stream from operating on .BIN files, as
           those are most likely raw PCM data, but depending on the
           initial pattern, could be mistaken for a different stream.
           See: https://github.com/cdemu/cdemu/issues/26 */
        if (!g_ascii_strcasecmp(suffix, ".bin")) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Filter cannot handle given data: .BIN files are not supported."));
            return FALSE;
        }
    }

    /* Seek to beginning */
    mirage_stream_seek(stream, 0, G_SEEK_SET, NULL);

    /* Try opening sndfile on top of stream */
    self->priv->sndfile = sf_open_virtual(&sndfile_io_bridge, open_mode, &self->priv->format, stream);
    if (!self->priv->sndfile) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, Q_("Filter cannot handle given data: failed to open sndfile: %s"), sf_strerror(self->priv->sndfile));
        return FALSE;
    }

    /* Turn on auto-header update */
    sf_command(self->priv->sndfile, SFC_SET_UPDATE_HEADER_AUTO, NULL, SF_TRUE);

    /* Print audio file info, but only if we are not opening in write
       mode (because then we already know) */
    if (!writable) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: audio file info:\n", __debug__);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  frames: %" G_GINT64_MODIFIER "d\n", __debug__, self->priv->format.frames);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  samplerate: %d\n", __debug__, self->priv->format.samplerate);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  channels: %d\n", __debug__, self->priv->format.channels);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  format: %d\n", __debug__, self->priv->format.format);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  sections: %d\n", __debug__, self->priv->format.sections);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  seekable: %d\n", __debug__, self->priv->format.seekable);
    }

    /* Check some additional requirements (two channels, seekable and samplerate) */
    if (!self->priv->format.seekable) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DATA_FILE_ERROR, Q_("Audio file is not seekable!"));
        return FALSE;
    }
    if (self->priv->format.channels != 2) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_DATA_FILE_ERROR, Q_("Invalid number of channels in audio file (%d)! Only two-channel audio files are supported!"), self->priv->format.channels);
        return FALSE;
    }

    /* Compute length in bytes */
    length = self->priv->format.frames * self->priv->format.channels * sizeof(guint16);
    if (!writable) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: raw stream length: %" G_GSIZE_MODIFIER "d (0x%" G_GSIZE_MODIFIER "X) bytes\n", __debug__, length, length);
    }
    mirage_filter_stream_simplified_set_stream_length(MIRAGE_FILTER_STREAM(self), length);

    /* Allocate read buffer; we wish to hold a single (multichannel) frame */
    self->priv->buflen = self->priv->format.channels * NUM_FRAMES * sizeof(guint16);
    if (!writable) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: buffer length: %d bytes\n", __debug__, self->priv->buflen);
    }
    self->priv->buffer = g_try_malloc(self->priv->buflen);
    if (!self->priv->buffer) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Failed to allocate read buffer!"));
        return FALSE;
    }

    /* Initialize resampler, if needed */
    self->priv->io_ratio = self->priv->format.samplerate / 44100.0;
    if (self->priv->io_ratio != 1.0) {
        gint resampler_error;
        gint buffer_size;

        /* Initialize resampler */
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: audio stream needs to be resampled to 44.1 kHZ, initializing resampler...\n", __debug__);
        self->priv->resampler = src_new(SRC_LINEAR, self->priv->format.channels, &resampler_error);
        if (!self->priv->resampler) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Failed to initialize resampler; error code %d!"), resampler_error);
            return FALSE;
        }

        /* Allocate resampler's output buffer */
        buffer_size = self->priv->format.channels * NUM_FRAMES * sizeof(float);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: resampler's output buffer: %d bytes\n", __debug__, buffer_size);
        self->priv->resample_buffer_out = g_try_malloc(buffer_size);
        if (!self->priv->resample_buffer_out) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Failed to allocate resampler output buffer!"));
            return FALSE;
        }

        /* Allocate resampler's input buffer */
        buffer_size *= self->priv->io_ratio;
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: resampler's input buffer: %d bytes\n", __debug__, buffer_size);
        self->priv->resample_buffer_in = g_try_malloc(buffer_size);
        if (!self->priv->resample_buffer_in) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Failed to allocate resampler input buffer!"));
            return FALSE;
        }

        /* Initialize static fields of resampler's data structure */
        self->priv->resampler_data.data_in = self->priv->resample_buffer_in;
        self->priv->resampler_data.data_out = self->priv->resample_buffer_out;
        self->priv->resampler_data.output_frames = NUM_FRAMES;
        self->priv->resampler_data.src_ratio = 1/self->priv->io_ratio;

        /* Adjust stream length */
        length = round(length/self->priv->io_ratio);
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: resampled stream length: %" G_GSIZE_MODIFIER "d (0x%" G_GSIZE_MODIFIER "X) bytes\n", __debug__, length, length);
        mirage_filter_stream_simplified_set_stream_length(MIRAGE_FILTER_STREAM(self), length);
    }

    return TRUE;
}

static gssize mirage_filter_stream_sndfile_partial_read (MirageFilterStream *_self, void *buffer, gsize count)
{
    MirageFilterStreamSndfile *self = MIRAGE_FILTER_STREAM_SNDFILE(_self);
    goffset position = mirage_filter_stream_simplified_get_position(_self);
    gint block;

    /* Find the block of frames corresponding to current position; this
       is within the final, possibly resampled, stream */
    block = position / self->priv->buflen;
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: stream position: %" G_GOFFSET_MODIFIER "d (0x%" G_GOFFSET_MODIFIER "X) -> block #%d (cached: #%d)\n", __debug__, position, position, block, self->priv->cached_block);

    /* If we do not have block in cache, read it */
    if (block != self->priv->cached_block) {
        gsize read_length;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: block not cached, reading...\n", __debug__);

        if (self->priv->io_ratio == 1.0) {
            /* Seek to beginning of block */
            sf_count_t offset = block*NUM_FRAMES;
            if (sf_seek(self->priv->sndfile, offset, SEEK_SET) < 0) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: failed to seek to offset %" G_GOFFSET_MODIFIER "d in underlying stream!\n", __debug__, offset);
                return -1;
            }

            /* Read frames */
            read_length = sf_readf_short(self->priv->sndfile, (short *)self->priv->buffer, NUM_FRAMES);
            if (!read_length) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: block not read; EOF reached?\n", __debug__);
                return -1;
            }
        } else {
            gint resampler_error;
            sf_count_t offset = block*NUM_FRAMES*self->priv->io_ratio;

            /* Seek to beginning of block; this is in original,
               non-resampled, stream */
            if (sf_seek(self->priv->sndfile, offset, SEEK_SET) < 0) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: failed to seek to offset %" G_GOFFSET_MODIFIER "d in underlying stream!\n", __debug__, offset);
                return -1;
            }

            /* Read read frames into resampler's input buffer */
            read_length = sf_readf_float(self->priv->sndfile, self->priv->resample_buffer_in, NUM_FRAMES*self->priv->io_ratio);
            if (!read_length) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: block not read; EOF reached?\n", __debug__);
                return -1;
            }

            /* Set fields in data structure; most are static and have
               not changed since initialization */
            self->priv->resampler_data.input_frames = read_length;
            self->priv->resampler_data.end_of_input = 1;

            /* Reset resampler (assume blocks are unrelated) */
            src_reset(self->priv->resampler);

            /* Resample */
            resampler_error = src_process(self->priv->resampler, &self->priv->resampler_data);
            if (resampler_error) {
                MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to resample frames: %s!\n", __debug__, src_strerror(resampler_error));
                /* Do nothing, though */
            }

            MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: resampler: read %ld input frames, generated %ld output frames\n", __debug__, self->priv->resampler_data.input_frames_used, self->priv->resampler_data.output_frames_gen);

            /* Convert generated frames to short */
            src_float_to_short_array(self->priv->resample_buffer_out, (short *)self->priv->buffer, NUM_FRAMES*self->priv->format.channels);
        }

        /* Store the number of currently stored block */
        self->priv->cached_block = block;
    } else {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: block already cached\n", __debug__);
    }

    /* Copy data */
    goffset block_offset = position % self->priv->buflen;
    count = MIN(count, self->priv->buflen - block_offset);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_STREAM, "%s: offset within block: %" G_GOFFSET_MODIFIER "d, copying %" G_GSIZE_MODIFIER "d bytes\n", __debug__, block_offset, count);

    memcpy(buffer, self->priv->buffer + block_offset, count);

    return count;
}

static gssize mirage_filter_stream_sndfile_partial_write (MirageFilterStream *_self, const void *buffer, gsize count)
{
    MirageFilterStreamSndfile *self = MIRAGE_FILTER_STREAM_SNDFILE(_self);
    goffset position = mirage_filter_stream_simplified_get_position(_self);
    gsize write_length;
    gssize bytes_written;

    /* Seek to position */
    sf_seek(self->priv->sndfile, position/(self->priv->format.channels * sizeof(guint16)), SEEK_SET);

    /* Write */
    write_length = sf_writef_short(self->priv->sndfile, (const short *)buffer, count/(self->priv->format.channels * sizeof(guint16)));

    bytes_written = write_length * self->priv->format.channels * sizeof(guint16);

    /* If we happen to cache a block for reading and we just overwrote
       it, we should not be caching it anymore */
    gint start_block = position / self->priv->buflen;
    gint end_block = (position + bytes_written) / self->priv->buflen;
    if (self->priv->cached_block >= start_block && self->priv->cached_block < end_block) {
        self->priv->cached_block = -1;
    }

    return bytes_written;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
static void mirage_filter_stream_sndfile_init (MirageFilterStreamSndfile *self)
{
    self->priv = mirage_filter_stream_sndfile_get_instance_private(self);

    mirage_filter_stream_generate_info(MIRAGE_FILTER_STREAM(self),
        "FILTER-SNDFILE",
        Q_("SNDFILE File Filter"),
        TRUE,
        4,
        Q_("WAV audio files (*.wav)"), "audio/wav",
        Q_("AIFF audio files (*.aiff)"), "audio/x-aiff",
        Q_("FLAC audio files (*.flac)"), "audio/x-flac",
        Q_("OGG audio files (*.ogg)"), "audio/x-ogg"
    );

    self->priv->cached_block = -1;

    self->priv->sndfile = NULL;
    self->priv->buffer = NULL;

    self->priv->resample_buffer_in = NULL;
    self->priv->resample_buffer_out = NULL;
    self->priv->resampler = NULL;
}

static void mirage_filter_stream_sndfile_dispose (GObject *gobject)
{
    MirageFilterStreamSndfile *self = MIRAGE_FILTER_STREAM_SNDFILE(gobject);

    /* Close sndfile */
    if (self->priv->sndfile) {
        sf_close(self->priv->sndfile);
        self->priv->sndfile = NULL;
    }

    /* Cleanup resampler */
    if (self->priv->resampler) {
        src_delete(self->priv->resampler);
        self->priv->resampler = NULL;
    }

    G_OBJECT_CLASS(mirage_filter_stream_sndfile_parent_class)->dispose(gobject);
}


static void mirage_filter_stream_sndfile_finalize (GObject *gobject)
{
    MirageFilterStreamSndfile *self = MIRAGE_FILTER_STREAM_SNDFILE(gobject);

    /* Free read buffer */
    g_free(self->priv->buffer);

    /* Free resampler buffers */
    g_free(self->priv->resample_buffer_in);
    g_free(self->priv->resample_buffer_out);

    /* Chain up to the parent class */
    G_OBJECT_CLASS(mirage_filter_stream_sndfile_parent_class)->finalize(gobject);
}

static void mirage_filter_stream_sndfile_class_init (MirageFilterStreamSndfileClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageFilterStreamClass *filter_stream_class = MIRAGE_FILTER_STREAM_CLASS(klass);

    gobject_class->dispose = mirage_filter_stream_sndfile_dispose;
    gobject_class->finalize = mirage_filter_stream_sndfile_finalize;

    filter_stream_class->open = mirage_filter_stream_sndfile_open;

    filter_stream_class->simplified_partial_read = mirage_filter_stream_sndfile_partial_read;
    filter_stream_class->simplified_partial_write = mirage_filter_stream_sndfile_partial_write;
}

static void mirage_filter_stream_sndfile_class_finalize (MirageFilterStreamSndfileClass *klass G_GNUC_UNUSED)
{
}
