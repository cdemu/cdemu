/*
 *  libMirage: language
 *  Copyright (C) 2006-2014 Rok Mandeljc
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

#include "mirage/config.h"
#include "mirage/mirage.h"

#include <glib/gi18n-lib.h>

#define __debug__ "Language"


/**********************************************************************\
 *                  Object and its private structure                  *
\**********************************************************************/
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


G_DEFINE_TYPE_WITH_PRIVATE(MirageLanguage, mirage_language, MIRAGE_TYPE_OBJECT)


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

    for (guint i = 0; i < G_N_ELEMENTS(pack_types); i++) {
        if (pack_types[i] == (guint)pack_type) {
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
 * @error: (out) (optional): location to store error, or %NULL
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
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LANGUAGE_ERROR, Q_("Invalid pack type %d!"), pack_type);
        return FALSE;
    }

    /* Free the field */
    g_free(pack->data);
    pack->length = 0;
    pack->set = FALSE;

    /* Set pack data only if length is not 0; if it is, assume caller wants to clear pack data... */
    if (length) {
#if GLIB_CHECK_VERSION(2, 68, 0)
        pack->data = g_memdup2(pack_data, length);
#else
        pack->data = g_memdup(pack_data, length);
#endif
        pack->length = length;
        pack->set = TRUE;
    }

    return TRUE;
}

/**
 * mirage_language_get_pack_data:
 * @self: a #MirageLanguage
 * @pack_type: (in): pack type
 * @pack_data: (out) (transfer none) (optional) (array length=length): location to store buffer containing pack data, or %NULL
 * @length: (out) (optional): location to store length of pack data, or %NULL
 * @error: (out) (optional): location to store error, or %NULL
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
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LANGUAGE_ERROR, Q_("Invalid pack type %d!"), pack_type);
        return FALSE;
    }

    if (!pack->set) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LANGUAGE_ERROR, Q_("Data not set for pack type %d!"), pack_type);
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
static void mirage_language_init (MirageLanguage *self)
{
    self->priv = mirage_language_get_instance_private(self);

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
    G_OBJECT_CLASS(mirage_language_parent_class)->finalize(gobject);
}

static void mirage_language_class_init (MirageLanguageClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = mirage_language_finalize;
}
