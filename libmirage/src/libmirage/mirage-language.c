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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * SECTION: mirage-language
 * @title: MirageLanguage
 * @short_description: Object representing a language for session or track.
 * @see_also: #MirageSession, #MirageTrack, #MirageCdTextCoder
 * @include: mirage-language.h
 *
 * #MirageLanguage object represents a CD-TEXT language for session or
 * track. It is a container object that stores language code and CD-TEXT
 * pack data for different pack types.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"

#define __debug__ "Language"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_LANGUAGE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_LANGUAGE, MirageLanguagePrivate))

typedef struct
{
    gboolean set;
    guint8 *data;
    gint length;
} MirageLanguage_Pack;


struct _MirageLanguagePrivate
{
    gint code;

    gint packs_number;
    MirageLanguage_Pack *packs;
};


/**********************************************************************\
 *                          Private functions                         *
\**********************************************************************/
static MirageLanguage_Pack *mirage_language_get_pack_by_type (MirageLanguage *self, gint pack_type)
{
    static const MirageLanguagePackType pack_types[] = {
        MIRAGE_LANGUAGE_PACK_TITLE,
        MIRAGE_LANGUAGE_PACK_PERFORMER,
        MIRAGE_LANGUAGE_PACK_SONGWRITER,
        MIRAGE_LANGUAGE_PACK_COMPOSER,
        MIRAGE_LANGUAGE_PACK_ARRANGER,
        MIRAGE_LANGUAGE_PACK_MESSAGE,
        MIRAGE_LANGUAGE_PACK_DISC_ID,
        MIRAGE_LANGUAGE_PACK_GENRE,
        MIRAGE_LANGUAGE_PACK_TOC,
        MIRAGE_LANGUAGE_PACK_TOC2,
        MIRAGE_LANGUAGE_PACK_RES_8A,
        MIRAGE_LANGUAGE_PACK_RES_8B,
        MIRAGE_LANGUAGE_PACK_RES_8C,
        MIRAGE_LANGUAGE_PACK_CLOSED_INFO,
        MIRAGE_LANGUAGE_PACK_UPC_ISRC,
        MIRAGE_LANGUAGE_PACK_SIZE,
    };

    for (gint i = 0; i < G_N_ELEMENTS(pack_types); i++) {
        if (pack_types[i] == pack_type) {
            return &self->priv->packs[i];
        }
    }

    return NULL;
}


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
/**
 * mirage_language_set_code:
 * @self: a #MirageLanguage
 * @code: (in): language code
 *
 * Sets language's language code.
 */
void mirage_language_set_code (MirageLanguage *self, gint code)
{
    self->priv->code = code;
}

/**
 * mirage_language_get_code:
 * @self: a #MirageLanguage
 *
 * Retrieves language's language code.
 *
 * Returns: language code
 */
gint mirage_language_get_code (MirageLanguage *self)
{
    return self->priv->code;
}


/**
 * mirage_language_set_pack_data:
 * @self: a #MirageLanguage
 * @pack_type: (in): pack type
 * @pack_data: (in) (array length=length): pack data
 * @length: (in): length of pack data
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Sets pack data of type @pack_type to data in @pack_data. @length is length of
 * data in @pack_data. @pack_type must be one of #MirageLanguagePackType.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_language_set_pack_data (MirageLanguage *self, MirageLanguagePackType pack_type, const guint8 *pack_data, gint length, GError **error)
{
    MirageLanguage_Pack *pack = mirage_language_get_pack_by_type(self, pack_type);

    if (!pack) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LANGUAGE_ERROR, "Invalid pack type %d!", pack_type);
        return FALSE;
    }

    /* Free the field */
    g_free(pack->data);
    pack->length = 0;
    pack->set = FALSE;

    /* Set pack data only if length is not 0; if it is, assume caller wants to clear pack data... */
    if (length) {
        pack->data = g_memdup(pack_data, length);
        pack->length = length;
        pack->set = TRUE;
    }

    return TRUE;
}

/**
 * mirage_language_get_pack_data:
 * @self: a #MirageLanguage
 * @pack_type: (in): pack type
 * @pack_data: (out) (transfer none) (allow-none) (array length=length): location to store buffer containing pack data, or %NULL
 * @length: (out) (allow-none): location to store length of pack data, or %NULL
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Retrieves pack data of type @pack_type. A pointer to buffer containing pack
 * data is stored in @pack data; the buffer belongs to the object and therefore
 * should not be modified.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_language_get_pack_data (MirageLanguage *self, MirageLanguagePackType pack_type, const guint8 **pack_data, gint *length, GError **error)
{
    MirageLanguage_Pack *pack = mirage_language_get_pack_by_type(self, pack_type);

    if (!pack) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LANGUAGE_ERROR, "Invalid pack type %d!", pack_type);
        return FALSE;
    }

    if (!pack->set) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LANGUAGE_ERROR, "Data not set for pack type %d!", pack_type);
        return FALSE;
    }

    /* Return what was asked for */
    if (pack_data) {
        *pack_data = pack->data;
    }
    if (length) {
        *length = pack->length;
    }

    return TRUE;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE(MirageLanguage, mirage_language, MIRAGE_TYPE_OBJECT);


static void mirage_language_init (MirageLanguage *self)
{
    self->priv = MIRAGE_LANGUAGE_GET_PRIVATE(self);

    /* Allocate fields */
    self->priv->packs_number = 16; /* Currently, we have 16 pack types */
    self->priv->packs = g_new0(MirageLanguage_Pack, self->priv->packs_number);
}


static void mirage_language_finalize (GObject *gobject)
{
    MirageLanguage *self = MIRAGE_LANGUAGE(gobject);

    /* Free private structure elements */
    for (gint i = 0; i < self->priv->packs_number; i++) {
        g_free(self->priv->packs[i].data);
    }
    g_free(self->priv->packs);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_language_parent_class)->finalize(gobject);
}

static void mirage_language_class_init (MirageLanguageClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = mirage_language_finalize;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageLanguagePrivate));
}
