/*
 *  libMirage: language
 *  Copyright (C) 2006-2026 Rok Mandeljc
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

    /* 16 packs, for type codes from 0x80 (MIRAGE_LANGUAGE_PACK_TITLE)
     * to 0x8F (MIRAGE_LANGUAGE_PACK_SIZE) */
    MirageLanguage_Pack packs[16];
};


G_DEFINE_TYPE_WITH_PRIVATE(MirageLanguage, mirage_language, MIRAGE_TYPE_OBJECT)


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
    gint pack_idx = pack_type - MIRAGE_LANGUAGE_PACK_TITLE;
    if ((guint)pack_idx >= G_N_ELEMENTS(self->priv->packs)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LANGUAGE_ERROR, Q_("Invalid pack type %d!"), pack_type);
        return FALSE;
    }
    MirageLanguage_Pack *pack = &self->priv->packs[pack_idx];

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
    gint pack_idx = pack_type - MIRAGE_LANGUAGE_PACK_TITLE;
    if ((guint)pack_idx >= G_N_ELEMENTS(self->priv->packs)) {
        g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_LANGUAGE_ERROR, Q_("Invalid pack type %d!"), pack_type);
        return FALSE;
    }
    const MirageLanguage_Pack *pack = &self->priv->packs[pack_idx];

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

    /* Initialize fields */
    self->priv->code = 0;

    memset(self->priv->packs, 0, sizeof(self->priv->packs));
}


static void mirage_language_finalize (GObject *gobject)
{
    MirageLanguage *self = MIRAGE_LANGUAGE(gobject);

    /* Free private structure elements */
    for (guint i = 0; i < G_N_ELEMENTS(self->priv->packs); i++) {
        MirageLanguage_Pack *pack = &self->priv->packs[i];
        g_free(pack->data);
    }

    /* Chain up to the parent class */
    G_OBJECT_CLASS(mirage_language_parent_class)->finalize(gobject);
}

static void mirage_language_class_init (MirageLanguageClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = mirage_language_finalize;
}
