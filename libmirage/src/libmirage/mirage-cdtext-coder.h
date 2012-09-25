/*
 *  libMirage: CD-TEXT Encoder/Decoder object
 *  Copyright (C) 2006-2012 Rok Mandeljc
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

#ifndef __MIRAGE_CDTEXT_CODER_H__
#define __MIRAGE_CDTEXT_CODER_H__


G_BEGIN_DECLS

/**
 * MirageCdTextDataCallback:
 * @langcode: (in): language code
 * @type: (in): pack type
 * @track: (in): track number
 * @data: (in) (transfer none) (array length=data_len): data
 * @data_len: (in): data length
 * @user_data: (closure): user data
 *
 * <para>
 * Specifies the type of callback functions that can be passed to
 * mirage_cdtext_decoder_get_data().
 * </para>
 *
 * <para>
 * @langcode is the language code assigned to the block which data belongs to.
 * @track is the number of track to which data belongs to, or 0 if data is global
 * (belongs to session/disc). @data is buffer containing data and @data_len
 * is the length of data in the buffer.
 * </para>
 *
 * <para>
 * @data points to buffer that belongs to decoder and therefore should not be freed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
typedef gboolean (*MirageCdTextDataCallback) (gint langcode, gint type, gint track, const guint8 *data, gint data_len, gpointer user_data);

/******************************************************************************\
 *                               CD-TEXT EncDec                              *
\******************************************************************************/
#define MIRAGE_TYPE_CDTEXT_CODER            (mirage_cdtext_coder_get_type())
#define MIRAGE_CDTEXT_CODER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_CDTEXT_CODER, MirageCdTextCoder))
#define MIRAGE_CDTEXT_CODER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_CDTEXT_CODER, MirageCdTextCoderClass))
#define MIRAGE_IS_CDTEXT_CODER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_CDTEXT_CODER))
#define MIRAGE_IS_CDTEXT_CODER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_CDTEXT_CODER))
#define MIRAGE_CDTEXT_CODER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_CDTEXT_CODER, MirageCdTextCoderClass))

typedef struct _MirageCdTextCoder         MirageCdTextCoder;
typedef struct _MirageCdTextCoderClass    MirageCdTextCoderClass;
typedef struct _MirageCdTextCoderPrivate  MirageCdTextCoderPrivate;

/**
 * MirageCdTextCoder:
 *
 * <para>
 * Contains private data only, and should be accessed using the functions below.
 * </para>
 **/
struct _MirageCdTextCoder
{
    MirageObject parent_instance;

    /*< private >*/
    MirageCdTextCoderPrivate *priv;
};

struct _MirageCdTextCoderClass
{
    MirageObjectClass parent_class;
};

/* Used by MIRAGE_TYPE_CDTEXT_CODER */
GType mirage_cdtext_coder_get_type (void);


/**********************************************************************\
 *                         Public API: Encoder                        *
\**********************************************************************/
void mirage_cdtext_encoder_init (MirageCdTextCoder *self, guint8 *buffer, gint buflen);
gboolean mirage_cdtext_encoder_set_block_info (MirageCdTextCoder *self, gint block, gint langcode, gint charset, gint copyright, GError **error);
void mirage_cdtext_encoder_add_data (MirageCdTextCoder *self, gint langcode, gint type, gint track, const guint8 *data, gint data_len);
void mirage_cdtext_encoder_encode (MirageCdTextCoder *self, guint8 **buffer, gint *buflen);


/**********************************************************************\
 *                         Public API: Decoder                        *
\**********************************************************************/
void mirage_cdtext_decoder_init (MirageCdTextCoder *self, guint8 *buffer, gint buflen);
gboolean mirage_cdtext_decoder_get_block_info (MirageCdTextCoder *self, gint block, gint *langcode, gint *charset, gint *copyright, GError **error);
gboolean mirage_cdtext_decoder_get_data (MirageCdTextCoder *self, gint block, MirageCdTextDataCallback callback_func, gpointer user_data);


G_END_DECLS

#endif /* __MIRAGE_CDTEXT_CODER_H__ */
