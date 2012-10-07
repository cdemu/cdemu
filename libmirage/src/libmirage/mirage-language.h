/*
 *  libMirage: Language object
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

#ifndef __MIRAGE_LANGUAGE_H__
#define __MIRAGE_LANGUAGE_H__


G_BEGIN_DECLS

/**
 * MirageLanguagePackTypes:
 * @MIRAGE_LANGUAGE_PACK_TITLE: Album name and Track titles
 * @MIRAGE_LANGUAGE_PACK_PERFORMER: Singer/player/conductor/orchestra
 * @MIRAGE_LANGUAGE_PACK_SONGWRITER: Name of the songwriter
 * @MIRAGE_LANGUAGE_PACK_COMPOSER: Name of the composer
 * @MIRAGE_LANGUAGE_PACK_ARRANGER: Name of the arranger
 * @MIRAGE_LANGUAGE_PACK_MESSAGE: Message from content provider or artist
 * @MIRAGE_LANGUAGE_PACK_DISC_ID: Disc identification information
 * @MIRAGE_LANGUAGE_PACK_GENRE: Genre identification / information
 * @MIRAGE_LANGUAGE_PACK_TOC: TOC information
 * @MIRAGE_LANGUAGE_PACK_TOC2: Second TOC
 * @MIRAGE_LANGUAGE_PACK_RES_8A: Reserved 8A
 * @MIRAGE_LANGUAGE_PACK_RES_8B: Reserved 8B
 * @MIRAGE_LANGUAGE_PACK_RES_8C: Reserved 8C
 * @MIRAGE_LANGUAGE_PACK_CLOSED_INFO: For internal use by content provider
 * @MIRAGE_LANGUAGE_PACK_UPC_ISRC: UPC/EAN code of album and ISRC for tracks
 * @MIRAGE_LANGUAGE_PACK_SIZE: Size information of the block
 *
 * <para>
 * CD-TEXT pack types
 * </para>
 **/
typedef enum _MirageLanguagePackTypes
{
    MIRAGE_LANGUAGE_PACK_TITLE       = 0x80,
    MIRAGE_LANGUAGE_PACK_PERFORMER   = 0x81,
    MIRAGE_LANGUAGE_PACK_SONGWRITER  = 0x82,
    MIRAGE_LANGUAGE_PACK_COMPOSER    = 0x83,
    MIRAGE_LANGUAGE_PACK_ARRANGER    = 0x84,
    MIRAGE_LANGUAGE_PACK_MESSAGE     = 0x85,
    MIRAGE_LANGUAGE_PACK_DISC_ID     = 0x86,
    MIRAGE_LANGUAGE_PACK_GENRE       = 0x87,
    MIRAGE_LANGUAGE_PACK_TOC         = 0x88,
    MIRAGE_LANGUAGE_PACK_TOC2        = 0x89,
    MIRAGE_LANGUAGE_PACK_RES_8A      = 0x8A,
    MIRAGE_LANGUAGE_PACK_RES_8B      = 0x8B,
    MIRAGE_LANGUAGE_PACK_RES_8C      = 0x8C,
    MIRAGE_LANGUAGE_PACK_CLOSED_INFO = 0x8D,
    MIRAGE_LANGUAGE_PACK_UPC_ISRC    = 0x8E,
    MIRAGE_LANGUAGE_PACK_SIZE        = 0x8F,
} MirageLanguagePackTypes;


/**********************************************************************\
 *                        MirageLanguage object                       *
\**********************************************************************/
#define MIRAGE_TYPE_LANGUAGE            (mirage_language_get_type())
#define MIRAGE_LANGUAGE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_LANGUAGE, MirageLanguage))
#define MIRAGE_LANGUAGE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_LANGUAGE, MirageLanguageClass))
#define MIRAGE_IS_LANGUAGE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_LANGUAGE))
#define MIRAGE_IS_LANGUAGE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_LANGUAGE))
#define MIRAGE_LANGUAGE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_LANGUAGE, MirageLanguageClass))

typedef struct _MirageLanguage         MirageLanguage;
typedef struct _MirageLanguageClass    MirageLanguageClass;
typedef struct _MirageLanguagePrivate  MirageLanguagePrivate;

/**
 * MirageLanguage:
 *
 * <para>
 * Contains private data only, and should be accessed using the functions below.
 * </para>
 **/
struct _MirageLanguage
{
    MirageObject parent_instance;

    /*< private >*/
    MirageLanguagePrivate *priv;
};

struct _MirageLanguageClass
{
    MirageObjectClass parent_class;
};

/* Used by MIRAGE_TYPE_LANGUAGE */
GType mirage_language_get_type (void);

/* Langcode: set/get */
void mirage_language_set_code (MirageLanguage *self, gint code);
gint mirage_language_get_code (MirageLanguage *self);

/* Field: set/get */
gboolean mirage_language_set_pack_data (MirageLanguage *self, MirageLanguagePackTypes pack_type, const gchar *pack_data, gint length, GError **error);
gboolean mirage_language_get_pack_data (MirageLanguage *self, MirageLanguagePackTypes pack_type, const gchar **pack_data, gint *length, GError **error);

G_END_DECLS

#endif /* __MIRAGE_LANGUAGE_H__ */
