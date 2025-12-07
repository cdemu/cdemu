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

#include <gcrypt.h>

#define __debug__ "MDX-Parser"


/**********************************************************************\
 *                  Object and its private structure                  *
\**********************************************************************/
struct _MirageParserMdxPrivate
{
    MirageDisc *disc;

    /* Descriptor file stream */
    MirageStream *stream;

    /* Descriptor data (decrypted and uncompressed) */
    guint8 *descriptor_data;
    guint64 descriptor_size;

    /* Optional encryption header for encrypted track data; pointer into
     * descriptor data buffer. */
    const MDX_EncryptionHeader *data_encryption_header;
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
    header->encryption_header_offset = GUINT32_FROM_LE(header->encryption_header_offset);
}


/**********************************************************************\
 *             MDX descriptor decompression and decryption            *
\**********************************************************************/
static gboolean mirage_parser_mdx_read_descriptor (MirageParserMdx *self, const MDX_FileHeader *file_header, guint8 **out_data, guint64 *out_data_size, GError **error)
{
    GError *local_error = NULL;
    gssize read_bytes;

    gboolean is_mdx = FALSE;
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
        is_mdx = TRUE;

        MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: MDX file detected. Reading footer offset and length...!\n", __debug__);

        read_bytes = mirage_stream_read(self->priv->stream, &mdx_footer_offset, sizeof(mdx_footer_offset), NULL);
        if (read_bytes != sizeof(mdx_footer_offset)) {
            MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: failed to read MDX footer offset!\n", __debug__);
            g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Failed to read MDX footer offset..."));
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
    if (!mdx_crypto_decipher_main_encryption_header(&encryption_header, &local_error)) {
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

    if (is_mdx) {
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

    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: image contains encrypted data!\n", __debug__);

    /* Sanity check - encryption header should be located at the end of descriptor */
    guint32 encryption_header_size = self->priv->descriptor_size - descriptor_header->encryption_header_offset;
    if (encryption_header_size != sizeof(MDX_EncryptionHeader)) {
        MIRAGE_DEBUG(self, MIRAGE_DEBUG_WARNING, "%s: unexpected size of encryption header for image data - expected %" G_GINT64_MODIFIER "d, found %d!\n", __debug__, sizeof(MDX_EncryptionHeader), encryption_header_size);
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Unexpected size of encryption header for image data!"));
        return FALSE;
    }

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
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s: trying to decipher encryption header...\n", __debug__);

    GError *local_error = NULL;
    MDX_EncryptionHeader *encryption_header = (MDX_EncryptionHeader *)(self->priv->descriptor_data + descriptor_header->encryption_header_offset);
    if (!mdx_crypto_decipher_data_encryption_header(encryption_header, password, password_length, &local_error)) {
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
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_PARSER, "%s:  encryption header offset: 0x%X\n", __debug__, descriptor_header->encryption_header_offset);

    /* Check if descriptor contains encryption header for track data */
    if (!mirage_parser_mdx_read_data_encryption_header(self, &self->priv->data_encryption_header, error)) {
        return FALSE;
    }

    /* TODO: parse descriptor */
    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_PARSER_ERROR, Q_("Parser not yet implemented!"));
    return NULL;
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
