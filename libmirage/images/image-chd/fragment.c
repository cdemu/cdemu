/*
 *  libMirage: CHD image: custom fragment for handling data chunking
 *  Copyright (C) 2026 Rok Mandeljc
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

#include "image-chd.h"
#include "fragment.h"

#define __debug__ "CHD-Fragment"


/**********************************************************************\
 *                  Object and its private structure                  *
\**********************************************************************/
struct _MirageFragmentChdPrivate
{
    shared_chd_file_t *chd_file_ptr;

    guint64 start_sector;
    guint32 num_sectors;

    guint32 hunk_size;
    guint32 sector_size;
    guint32 sectors_in_hunk;

    guint8 *buffer;
    guint32 cached_hunk_idx;

    /* The following elements are stored in private structure of
     * MirageFragment, which is (by design) inaccessible from here.
     * So keep our own copies. */
    gint main_size;
    gint main_format;

    gint subchannel_size;
    gint subchannel_format;
};

G_DEFINE_TYPE_WITH_PRIVATE(MirageFragmentChd, mirage_fragment_chd, MIRAGE_TYPE_FRAGMENT)


/**********************************************************************\
 *                      Custom fragment functionality                 *
\**********************************************************************/
gboolean mirage_fragment_chd_setup (
    MirageFragmentChd *self,
    shared_chd_file_t *chd_file_ptr,
    guint64 start_sector,
    guint32 num_sectors,
    guint32 hunk_size,
    guint32 sector_size,
    gint main_size,
    gint main_format,
    gint subchannel_size,
    gint subchannel_format,
    GError **error
)
{
    /* NOTE: we implicitly assume that this method is called only once;
     * since this fragment is used only within CHD parser, we can ensure
     * that this is the case. */

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: setting up CHD fragment:\n", __debug__);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s:  - start sector: %" G_GINT64_MODIFIER "u (0x%" G_GINT64_MODIFIER "X)\n", __debug__, start_sector, start_sector);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s:  - num sectors: %u (0x%X)\n", __debug__, num_sectors, num_sectors);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s:  - hunk size: %u\n", __debug__, hunk_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s:  - sector size: %u\n", __debug__, sector_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s:  - main channel size: %d\n", __debug__, main_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s:  - main channel format: %d\n", __debug__, main_format);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s:  - subchannel size: %d\n", __debug__, subchannel_size);
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s:  - subchannel format: %d\n", __debug__, subchannel_format);

    /* Store boxed pointer to libchdr reader object, and add reference */
    self->priv->chd_file_ptr = g_rc_box_acquire(chd_file_ptr);

    self->priv->start_sector = start_sector;
    self->priv->num_sectors = num_sectors;

    /* Allocate hunk buffer */
    self->priv->hunk_size = hunk_size;
    self->priv->sector_size = sector_size;
    self->priv->sectors_in_hunk = hunk_size / sector_size;

    if (hunk_size % sector_size != 0) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: sanity check failed - hunk size (%u) is not multiple of sector size (%u)!\n", __debug__, self->priv->hunk_size, self->priv->sector_size);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Sanity check failed!");
        return FALSE;
    }

    self->priv->buffer = g_try_malloc0(self->priv->hunk_size);
    if (!self->priv->buffer) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to allocate data buffer (%u bytes)!\n", __debug__, self->priv->hunk_size);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to allocate data buffer (%u bytes)!", self->priv->hunk_size);
        return FALSE;
    }

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: sectors per hunk: %u\n", __debug__, self->priv->sectors_in_hunk);

    /* Set fragment length */
    mirage_fragment_set_length(MIRAGE_FRAGMENT(self), num_sectors);

    /* Propagate the information to parent - this is to ensure that
     * common codepaths in parent class work as expected, as well as
     * to ensure that users that try to access the fragment's properties
     * (for example, image-analyzer), get correct values. */
    mirage_fragment_main_data_set_size(MIRAGE_FRAGMENT(self), main_size);
    mirage_fragment_main_data_set_format(MIRAGE_FRAGMENT(self), main_format);

    mirage_fragment_subchannel_data_set_size(MIRAGE_FRAGMENT(self), subchannel_size);
    mirage_fragment_subchannel_data_set_format(MIRAGE_FRAGMENT(self), subchannel_format);

    /* Set the underlying stream, so that fragment can properly report
     * its filename. Other than that, the stream will not be directly
     * used by this fragment implementation. */
    mirage_fragment_main_data_set_stream(MIRAGE_FRAGMENT(self), chd_file_ptr->stream);

    /* NOTE: the offset is not really applicable here as it bears no true
     * relation to the stream, so leave it unset (0) for now.
     *
     * If we wanted to report a monotonically-increasing value in case when
     * multiple fragments are instantiated using the same stream (for example,
     * image with multiple-tracks), we could report a product of start sector
     * address and sector (unit) size... */

    /* Store information about sector size and format */
    self->priv->main_size = main_size;
    self->priv->main_format = main_format;

    self->priv->subchannel_size = subchannel_size;
    self->priv->subchannel_format = subchannel_format;

    return TRUE;
}

static gboolean mirage_fragment_chd_read_sector_data (MirageFragmentChd *self, gint address, GError **error)
{
    guint64 chd_address;
    guint hunk_idx;
    chd_error status;

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: read sector data request for relative address %d\n", __debug__, address);

    /* Map the fragment-relative address into CHD address space, and
     * compute target hunk index. */
    chd_address = self->priv->start_sector + address;
    hunk_idx = chd_address / self->priv->sectors_in_hunk;
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: fragment start offset inside CHD: %" G_GINT64_MODIFIER "u, target sector address inside CHD: %" G_GINT64_MODIFIER "u, hunk index: %u\n", __debug__, self->priv->start_sector, chd_address, hunk_idx);

    /* Check if data is already in the buffer */
    if (hunk_idx == self->priv->cached_hunk_idx) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: data for hunk %u is already loaded in buffer!\n", __debug__, hunk_idx);
        return TRUE;
    }

    /* Read hunk */
    status = chd_read(
        self->priv->chd_file_ptr->chd_file,
        hunk_idx,
        self->priv->buffer
    );
    if (status != CHDERR_NONE) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read data for hunk %u - chd_read() failed with error code %s (%d)", __debug__, hunk_idx, _chd_error_str(status), status);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to read data for hunk %u - chd_read() failed with error code %s (%d)", hunk_idx, _chd_error_str(status), status);
        return FALSE;
    }

    /* Mark hunk as cached */
    self->priv->cached_hunk_idx = hunk_idx;

    return TRUE;
}


/**********************************************************************\
 *                          MirageFragment methods                    *
\**********************************************************************/
static gint mirage_fragment_chd_read_main_data_impl (MirageFragment *_self, gint address, guint8 *buffer, GError **error)
{
    MirageFragmentChd *self = MIRAGE_FRAGMENT_CHD(_self);

    /* No need to read anything if we don't have a buffer */
    if (!buffer) {
        return self->priv->main_size;
    }

    /* Ensure sector data is available in cache */
    if (!mirage_fragment_chd_read_sector_data(MIRAGE_FRAGMENT_CHD(_self), address, error)) {
        return -1;
    }

    /* Data */
    guint offset = 0;
    if (self->priv->sectors_in_hunk > 1) {
        guint64 chd_address = self->priv->start_sector + address; /* Fragment-relative address to CHD-global address */
        guint sector_index = chd_address % self->priv->sectors_in_hunk; /* Sector index inside hunk */
        offset = sector_index * self->priv->sector_size; /* Sector address inside hunk */
    }

    memcpy(buffer, self->priv->buffer + offset, self->priv->main_size);

    /* Audio data may need to be swapped from BE to LE */
    if (self->priv->main_format == MIRAGE_MAIN_DATA_FORMAT_AUDIO_SWAP) {
        for (gint i = 0; i < self->priv->main_size; i += 2) {
            guint16 *ptr = (guint16 *)(void *)(buffer + i);
            *ptr = GUINT16_SWAP_LE_BE(*ptr);
        }
    }

    return self->priv->main_size;
}

static gint mirage_fragment_chd_read_subchannel_data_impl (MirageFragment *_self, gint address, guint8 *buffer, GError **error)
{
    MirageFragmentChd *self = MIRAGE_FRAGMENT_CHD(_self);

    /* If there's no subchannel, return 0 for the length */
    if (!self->priv->subchannel_size) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: no subchannel (size = 0)!\n", __debug__);
        return 0;
    }

    /* No need to read anything if we don't have a buffer */
    if (!buffer) {
        return 96;
    }

    /* Ensure sector data is available in cache */
    if (!mirage_fragment_chd_read_sector_data(MIRAGE_FRAGMENT_CHD(_self), address, error)) {
        return -1;
    }

    /* Data */
    guint offset = 0;
    if (self->priv->sectors_in_hunk > 1) {
        guint64 chd_address = self->priv->start_sector + address; /* Fragment-relative address to CHD-global address */
        guint sector_index = chd_address % self->priv->sectors_in_hunk; /* Sector index inside hunk */
        offset = sector_index * self->priv->sector_size; /* Sector address inside hunk */
    }
    offset += self->priv->main_size;

    if (self->priv->subchannel_format & MIRAGE_SUBCHANNEL_DATA_FORMAT_Q16) {
        /* 16-byte Q; interleave it and pretend everything else's 0 */
        mirage_helper_subchannel_interleave(SUBCHANNEL_Q, self->priv->buffer + offset, buffer);
    } else if (self->priv->subchannel_format & MIRAGE_SUBCHANNEL_DATA_FORMAT_PW96_INTERLEAVED) {
        /* 96-byte interleaved PW; just copy it */
        memcpy(buffer, self->priv->buffer + offset, 96);
    }

    return 96;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
static void mirage_fragment_chd_init (MirageFragmentChd *self)
{
    self->priv = mirage_fragment_chd_get_instance_private(self);

    self->priv->chd_file_ptr = NULL;

    self->priv->hunk_size = 0;
    self->priv->buffer = NULL;
    self->priv->cached_hunk_idx = -1;
}

static void mirage_fragment_chd_dispose (GObject *gobject)
{
    MirageFragmentChd *self = MIRAGE_FRAGMENT_CHD(gobject);

    if (self->priv->chd_file_ptr) {
        g_rc_box_release_full(self->priv->chd_file_ptr, (GDestroyNotify)shared_chd_file_cleanup);
        self->priv->chd_file_ptr = NULL;
    }

    /* Chain up to the parent class */
    G_OBJECT_CLASS(mirage_fragment_chd_parent_class)->dispose(gobject);
}

static void mirage_fragment_chd_finalize (GObject *gobject)
{
    MirageFragmentChd *self = MIRAGE_FRAGMENT_CHD(gobject);

    g_free(self->priv->buffer);

    /* Chain up to the parent class */
    G_OBJECT_CLASS(mirage_fragment_chd_parent_class)->finalize(gobject);
}

static void mirage_fragment_chd_class_init (MirageFragmentChdClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageFragmentClass *fragment_class = MIRAGE_FRAGMENT_CLASS(klass);

    gobject_class->dispose = mirage_fragment_chd_dispose;
    gobject_class->finalize = mirage_fragment_chd_finalize;

    fragment_class->read_main_data_impl = mirage_fragment_chd_read_main_data_impl;
    fragment_class->read_subchannel_data_impl = mirage_fragment_chd_read_subchannel_data_impl;
}
