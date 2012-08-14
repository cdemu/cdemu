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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"

#define __debug__ "Language"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_LANGUAGE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_LANGUAGE, MIRAGE_LanguagePrivate))

typedef struct
{
    gboolean set;
    gchar *data;
    gint length;
} MIRAGE_Language_Pack;


struct _MIRAGE_LanguagePrivate
{
    gint langcode;

    gint packs_number;
    MIRAGE_Language_Pack *packs;
};


/**********************************************************************\
 *                          Private functions                         *
\**********************************************************************/
static MIRAGE_Language_Pack *mirage_language_get_pack_by_type (MIRAGE_Language *self, gint pack_type)
{
    gint i;
    static const gint pack_types[] = {
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

    for (i = 0; i < G_N_ELEMENTS(pack_types); i++) {
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
 * mirage_language_set_langcode:
 * @self: a #MIRAGE_Language
 * @langcode: (in): language code
 *
 * <para>
 * Sets language's language code.
 * </para>
 **/
void mirage_language_set_langcode (MIRAGE_Language *self, gint langcode)
{
    self->priv->langcode = langcode;
}

/**
 * mirage_language_get_langcode:
 * @self: a #MIRAGE_Language
 *
 * <para>
 * Retrieves language's language code.
 * </para>
 *
 * Returns: language code
 **/
gint mirage_language_get_langcode (MIRAGE_Language *self)
{
    return self->priv->langcode;
}


/**
 * mirage_language_set_pack_data:
 * @self: a #MIRAGE_Language
 * @pack_type: (in): pack type
 * @pack_data: (in): pack data
 * @length: (in): length of pack data
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Sets pack data of type @pack_type to data in @pack_data. @length is length of
 * data in @pack_data. @pack_type must be one of #MIRAGE_Language_PackTypes.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_language_set_pack_data (MIRAGE_Language *self, gint pack_type, const gchar *pack_data, gint length, GError **error)
{
    MIRAGE_Language_Pack *pack = mirage_language_get_pack_by_type(self, pack_type);

    if (!pack) {
        mirage_error(MIRAGE_E_INVALIDPACKTYPE, error);
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
 * @self: a #MIRAGE_Language
 * @pack_type: (in): pack type
 * @pack_data: (out) (transfer none) (allow-none): location to store buffer containing pack data, or %NULL
 * @length: (out) (allow-none): location to store length of pack data, or %NULL
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * <para>
 * Retrieves pack data of type @pack_type. A pointer to buffer containing pack
 * data is stored in @pack data; the buffer belongs to the object and therefore
 * should not be modified.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_language_get_pack_data (MIRAGE_Language *self, gint pack_type, const gchar **pack_data, gint *length, GError **error)
{
    MIRAGE_Language_Pack *pack = mirage_language_get_pack_by_type(self, pack_type);

    if (!pack) {
        mirage_error(MIRAGE_E_INVALIDPACKTYPE, error);
        return FALSE;
    }

    if (!pack->set) {
        mirage_error(MIRAGE_E_PACKNOTSET, error);
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
G_DEFINE_TYPE(MIRAGE_Language, mirage_language, MIRAGE_TYPE_OBJECT);


static void mirage_language_init (MIRAGE_Language *self)
{
    self->priv = MIRAGE_LANGUAGE_GET_PRIVATE(self);

    /* Allocate fields */
    self->priv->packs_number = 16; /* Currently, we have 16 pack types */
    self->priv->packs = g_new0(MIRAGE_Language_Pack, self->priv->packs_number);
}


static void mirage_language_finalize (GObject *gobject)
{
    MIRAGE_Language *self = MIRAGE_LANGUAGE(gobject);
    gint i;

    /* Free private structure elements */
    for (i = 0; i < self->priv->packs_number; i++) {
        g_free(self->priv->packs[i].data);
    }
    g_free(self->priv->packs);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_language_parent_class)->finalize(gobject);
}

static void mirage_language_class_init (MIRAGE_LanguageClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = mirage_language_finalize;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_LanguagePrivate));
}
