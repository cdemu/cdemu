/*
 *  libMirage: MDX image: custom fragment for handling data compression/encryption
 *  Copyright (C) 2026 Rok Mandeljc
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

#define __debug__ "MDX-Fragment"


/**********************************************************************\
 *                  Object and its private structure                  *
\**********************************************************************/
struct _MirageFragmentMdxPrivate
{
    /* Data buffer */
    guint8 *buffer;
    gint buffer_length;

    gint current_sector;

    /* Cipher handle for data deciphering */
    gcry_cipher_hd_t crypt_handle;
    guint8 tweak_key[16];

    /* The following elements are stored in private structure of
     * MirageFragment, which is (by design) inaccessible from here.
     * So keep our own copies. */
    MirageStream *data_stream; /* Data stream (main channel and subchannel) */

    guint64 data_offset; /* Data offset in the stream */

    gint main_size;
    gint main_format;

    gint subchannel_size;
    gint subchannel_format;
};

G_DEFINE_TYPE_WITH_PRIVATE(MirageFragmentMdx, mirage_fragment_mdx, MIRAGE_TYPE_FRAGMENT)


/**********************************************************************\
 *                      Custom fragment functionality                 *
\**********************************************************************/
gboolean mirage_fragment_mdx_setup (
    MirageFragmentMdx *self,
    gint length,
    MirageStream *data_stream,
    guint64 data_offset,
    gint main_size,
    gint main_format,
    gint subchannel_size,
    gint subchannel_format,
    const MDX_EncryptionHeader *encryption_header,
    GError **error
)
{
    /* NOTE: we implicitly assume that this method is called only once;
     * since this fragment is used only within MDX parser, we can ensure
     * that this is the case. */

    /* Propagate the information to parent - this is to ensure that
     * common codepaths in parent class work as expected, as well as
     * to ensure that users that try to access the fragment's properties
     * (for example, image-analyzer), get correct values. */
    mirage_fragment_set_length(MIRAGE_FRAGMENT(self), length);
    mirage_fragment_main_data_set_stream(MIRAGE_FRAGMENT(self), data_stream);

    mirage_fragment_main_data_set_offset(MIRAGE_FRAGMENT(self), data_offset);
    mirage_fragment_main_data_set_size(MIRAGE_FRAGMENT(self), main_size);
    mirage_fragment_main_data_set_format(MIRAGE_FRAGMENT(self), main_format);

    mirage_fragment_subchannel_data_set_size(MIRAGE_FRAGMENT(self), subchannel_size);
    mirage_fragment_subchannel_data_set_format(MIRAGE_FRAGMENT(self), subchannel_format);

    /* Keep reference to data stream */
    self->priv->data_stream = g_object_ref(data_stream);
    self->priv->data_offset = data_offset;

    /* Store information about sector size */
    self->priv->main_size = main_size;
    self->priv->main_format = main_format;

    self->priv->subchannel_size = subchannel_size;
    self->priv->subchannel_format = subchannel_format;

    /* If encryption header is available, initialize the cipher handle. */
    if (encryption_header) {
        gpg_error_t rc;

        rc = gcry_cipher_open(&self->priv->crypt_handle, GCRY_CIPHER_AES256, GCRY_CIPHER_MODE_ECB, 0);
        if (rc != 0) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to initialize AES-256 cipher! Error code: %d (%X)!", rc, rc);
            return FALSE;
        }

        rc = gcry_cipher_setkey(self->priv->crypt_handle, encryption_header->key_data + MDX_IV_SIZE, 32);
        if (rc != 0) {
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to set cipher key! Error code: %d (%X)!", rc, rc);
            return FALSE;
        }

        /* Copy tweak key - first 16 bytes of the key data */
        memcpy(self->priv->tweak_key, encryption_header->key_data, 16);
    }

    /* Allocate buffer/cache */
    gint num_sectors = 1;
    gint full_size = main_size + subchannel_size;

    self->priv->buffer_length = num_sectors * full_size;
    self->priv->buffer = g_malloc0(self->priv->buffer_length);
    if (!self->priv->buffer) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_FRAGMENT_ERROR, "Failed to allocate read buffer (%d bytes)!", self->priv->buffer_length);
        return FALSE;
    }

    return TRUE;
}

static gboolean mirage_fragment_mdx_read_sector_data (MirageFragmentMdx *self, gint address, GError **error)
{
    if (address == self->priv->current_sector) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: data for relative address %d is already loaded in buffer!\n", __debug__, address);
        return TRUE;
    }

    /* Compute offset */
    guint64 full_size = self->priv->main_size + self->priv->subchannel_size;
    guint64 data_offset = self->priv->data_offset + (guint64)address * full_size;

    /* Note: we ignore all errors here in order to be able to cope with truncated mini images */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: reading %" G_GINT64_MODIFIER "d bytes from offset %" G_GINT64_MODIFIER "d (0x%" G_GINT64_MODIFIER "X) for relative sector address %d...\n", __debug__, full_size, data_offset, data_offset, address);

    mirage_stream_seek(self->priv->data_stream, data_offset, G_SEEK_SET, NULL);
    gsize read_len = mirage_stream_read(self->priv->data_stream, self->priv->buffer, full_size, NULL);

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: read %" G_GINT64_MODIFIER "d bytes\n", __debug__, read_len);

    /* Decrypt */
    if (self->priv->crypt_handle) {
        gboolean succeeded;

        succeeded = mdx_crypto_decipher_buffer_lrw(
            self->priv->crypt_handle,
            self->priv->buffer,
            read_len,
            self->priv->tweak_key,
            1 + address * full_size / 16,
            error
        );

        if (!succeeded) {
            return FALSE;
        }
    }

    self->priv->current_sector = address;

    return TRUE;
}


/**********************************************************************\
 *                          MirageFragment methods                    *
\**********************************************************************/
static gboolean mirage_fragment_mdx_read_main_data (MirageFragment *_self, gint address, guint8 **buffer, gint *length, GError **error)
{
    MirageFragmentMdx *self = MIRAGE_FRAGMENT_MDX(_self);

    if (!self->priv->crypt_handle)  {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: encryption is not used; using parent implementation of read_main_data()...\n", __debug__);
        return MIRAGE_FRAGMENT_CLASS(mirage_fragment_mdx_parent_class)->read_main_data(_self, address, buffer, length, error);
    }

    /* Clear both variables */
    *length = 0;
    if (buffer) {
        *buffer = NULL;
    }

    /* Ensure sector data is available in cache */
    if (!mirage_fragment_mdx_read_sector_data(MIRAGE_FRAGMENT_MDX(_self), address, error)) {
        return FALSE;
    }

    /* Length */
    *length = self->priv->main_size;

    /* Data */
    if (buffer) {
        guint8 *data_buffer = g_malloc0(self->priv->main_size);
        memcpy(data_buffer, self->priv->buffer, self->priv->main_size);
        *buffer = data_buffer;
    }

    return TRUE;
}

static gboolean mirage_fragment_mdx_read_subchannel_data (MirageFragment *_self, gint address, guint8 **buffer, gint *length, GError **error)
{
    MirageFragmentMdx *self = MIRAGE_FRAGMENT_MDX(_self);

    if (!self->priv->crypt_handle)  {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: encryption is not used; using parent implementation of read_subchannel_data()...\n", __debug__);
        return MIRAGE_FRAGMENT_CLASS(mirage_fragment_mdx_parent_class)->read_subchannel_data(_self, address, buffer, length, error);
    }

    /* Ensure sector data is available in cache */
    if (!mirage_fragment_mdx_read_sector_data(MIRAGE_FRAGMENT_MDX(_self), address, error)) {
        return FALSE;
    }

    /* Length */
    *length = self->priv->subchannel_size;

    /* Data */
    if (buffer) {
        guint8 *data_buffer = g_malloc0(self->priv->subchannel_size);
        memcpy(data_buffer, self->priv->buffer + self->priv->main_size, self->priv->subchannel_size);
        *buffer = data_buffer;
    }

    return TRUE;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
static void mirage_fragment_mdx_init (MirageFragmentMdx *self)
{
    self->priv = mirage_fragment_mdx_get_instance_private(self);

    self->priv->buffer = NULL;
    self->priv->buffer_length = 0;

    self->priv->current_sector = -1;

    self->priv->crypt_handle = NULL;

    self->priv->data_stream = NULL;
}

static void mirage_fragment_mdx_dispose (GObject *gobject)
{
    MirageFragmentMdx *self = MIRAGE_FRAGMENT_MDX(gobject);

    if (self->priv->data_stream) {
        g_object_unref(self->priv->data_stream);
        self->priv->data_stream = NULL;
    }

    if (self->priv->crypt_handle) {
        gcry_cipher_close(self->priv->crypt_handle);
        self->priv->crypt_handle = NULL;
    }

    /* Chain up to the parent class */
    G_OBJECT_CLASS(mirage_fragment_mdx_parent_class)->dispose(gobject);
}

static void mirage_fragment_mdx_finalize (GObject *gobject)
{
    MirageFragmentMdx *self = MIRAGE_FRAGMENT_MDX(gobject);

    g_free(self->priv->buffer);

    /* Chain up to the parent class */
    G_OBJECT_CLASS(mirage_fragment_mdx_parent_class)->finalize(gobject);
}

static void mirage_fragment_mdx_class_init (MirageFragmentMdxClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    MirageFragmentClass *fragment_class = MIRAGE_FRAGMENT_CLASS(klass);

    gobject_class->dispose = mirage_fragment_mdx_dispose;
    gobject_class->finalize = mirage_fragment_mdx_finalize;

    fragment_class->read_main_data = mirage_fragment_mdx_read_main_data;
    fragment_class->read_subchannel_data = mirage_fragment_mdx_read_subchannel_data;
}
