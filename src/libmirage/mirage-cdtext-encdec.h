/*
 *  libMirage: CD-TEXT Encoder/Decoder object
 *  Copyright (C) 2006-2009 Rok Mandeljc
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

#ifndef __MIRAGE_CDTEXT_ENCDEC_H__
#define __MIRAGE_CDTEXT_ENCDEC_H__


G_BEGIN_DECLS

/**
 * MIRAGE_CDTextDataCallback:
 * @langcode: language code
 * @type: pack type
 * @track: track number
 * @data: data
 * @data_len: data length
 * @user_data: user data
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
typedef gboolean (*MIRAGE_CDTextDataCallback) (gint langcode, gint type, gint track, guint8 *data, gint data_len, gpointer user_data);

/******************************************************************************\
 *                               CD-TEXT EncDec                              *
\******************************************************************************/
#define MIRAGE_TYPE_CDTEXT_ENCDEC            (mirage_cdtext_encdec_get_type())
#define MIRAGE_CDTEXT_ENCDEC(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_CDTEXT_ENCDEC, MIRAGE_CDTextEncDec))
#define MIRAGE_CDTEXT_ENCDEC_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_CDTEXT_ENCDEC, MIRAGE_CDTextEncDecClass))
#define MIRAGE_IS_CDTEXT_ENCDEC(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_CDTEXT_ENCDEC))
#define MIRAGE_IS_CDTEXT_ENCDEC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_CDTEXT_ENCDEC))
#define MIRAGE_CDTEXT_ENCDEC_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_CDTEXT_ENCDEC, MIRAGE_CDTextEncDecClass))

/**
 * MIRAGE_CDTextEncDec:
 *
 * <para>
 * Contains private data only, and should be accessed using the functions below.
 * </para>
 **/
typedef struct {
    MIRAGE_Object parent;
} MIRAGE_CDTextEncDec;

typedef struct {
    MIRAGE_ObjectClass parent;
} MIRAGE_CDTextEncDecClass;

/* Used by MIRAGE_TYPE_CDTEXT_ENCDEC */
GType mirage_cdtext_encdec_get_type (void);

/* Public API: Encoder */
gboolean mirage_cdtext_encoder_init (MIRAGE_CDTextEncDec *self, guint8 *buffer, gint buflen, GError **error);
gboolean mirage_cdtext_encoder_set_block_info (MIRAGE_CDTextEncDec *self, gint block, gint langcode, gint charset, gint copyright, GError **error);
gboolean mirage_cdtext_encoder_add_data (MIRAGE_CDTextEncDec *self, gint langcode, gint type, gint track, guint8 *data, gint data_len, GError **error);
gboolean mirage_cdtext_encoder_encode (MIRAGE_CDTextEncDec *self, guint8 **buffer, gint *buflen, GError **error);

/* Public API: Decoder */
gboolean mirage_cdtext_decoder_init (MIRAGE_CDTextEncDec *self, guint8 *buffer, gint buflen, GError **error);
gboolean mirage_cdtext_decoder_get_block_info (MIRAGE_CDTextEncDec *self, gint block, gint *langcode, gint *charset, gint *copyright, GError **error);
gboolean mirage_cdtext_decoder_get_data (MIRAGE_CDTextEncDec *self, gint block, MIRAGE_CDTextDataCallback callback_func, gpointer user_data, GError **error);

G_END_DECLS

#endif /* __MIRAGE_CDTEXT_ENCDEC_H__ */
