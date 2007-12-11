/*
 *  libMirage: Language object
 *  Copyright (C) 2006-2007 Rok Mandeljc
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


/******************************************************************************\
 *                              Private structure                             *
\******************************************************************************/
#define MIRAGE_LANGUAGE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_LANGUAGE, MIRAGE_LanguagePrivate))

typedef struct {
    gboolean set;
    gchar *data;
    gint length;
} MIRAGE_Language_Pack;

typedef struct {    
    gint langcode;
    
    gint packs_number;
    MIRAGE_Language_Pack *packs;
} MIRAGE_LanguagePrivate;


/******************************************************************************\
 *                              Private functions                             *
\******************************************************************************/
static gboolean __mirage_language_get_pack_by_type (MIRAGE_Language *self, gint pack_type, MIRAGE_Language_Pack **pack, GError **error) {
    MIRAGE_LanguagePrivate *_priv = MIRAGE_LANGUAGE_GET_PRIVATE(self);
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
            *pack = &_priv->packs[i];
            return TRUE;
        }
    }
    
    mirage_error(MIRAGE_E_INVALIDPACKTYPE, error);
    return FALSE;
}


/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
/**
 * mirage_language_set_langcode:
 * @self: a #MIRAGE_Language
 * @langcode: language code
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets language's language code.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_language_set_langcode (MIRAGE_Language *self, gint langcode, GError **error) {
    MIRAGE_LanguagePrivate *_priv = MIRAGE_LANGUAGE_GET_PRIVATE(self);
    _priv->langcode = langcode;
    return TRUE;
}

/**
 * mirage_language_get_langcode:
 * @self: a #MIRAGE_Language
 * @langcode: location to store language code
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves language's language code.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_language_get_langcode (MIRAGE_Language *self, gint *langcode, GError **error) {
    MIRAGE_LanguagePrivate *_priv = MIRAGE_LANGUAGE_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(langcode);
    *langcode = _priv->langcode;
    return TRUE;
}


/**
 * mirage_language_set_pack_data:
 * @self: a #MIRAGE_Language
 * @pack_type: pack type
 * @pack_data: pack data
 * @length: length of pack data
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets pack data of type @pack_type to data in @pack_data. @length is length of
 * data in @pack_data. @pack_type must be one of #MIRAGE_Language_PackTypes.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_language_set_pack_data (MIRAGE_Language *self, gint pack_type, gchar *pack_data, gint length, GError **error) {
    /*MIRAGE_LanguagePrivate *_priv = MIRAGE_LANGUAGE_GET_PRIVATE(self);*/
    MIRAGE_Language_Pack *pack = NULL;
    
    if (!__mirage_language_get_pack_by_type(self, pack_type, &pack, error)) {
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
 * @pack_type: pack type
 * @pack_data: location to store buffer containing pack data, or %NULL
 * @length: location to store length of pack data, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves pack data of type @pack_type. Copy of pack data is stored in
 * @pack data; it should be freed with g_free() when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_language_get_pack_data (MIRAGE_Language *self, gint pack_type, gchar **pack_data, gint *length, GError **error) {
    /*MIRAGE_LanguagePrivate *_priv = MIRAGE_LANGUAGE_GET_PRIVATE(self);*/
    MIRAGE_Language_Pack *pack = NULL;
    
    if (!__mirage_language_get_pack_by_type(self, pack_type, &pack, error)) {
        return FALSE;
    }
    
    if (!pack->set) {
        mirage_error(MIRAGE_E_PACKNOTSET, error);
        return FALSE;
    }
    
    /* Return what was asked for */
    if (pack_data) {
        *pack_data = g_memdup(pack->data, pack->length);
    }
    if (length) {
        *length = pack->length;
    }
        
    return TRUE;
}


/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static MIRAGE_ObjectClass *parent_class = NULL;

static void __mirage_language_instance_init (GTypeInstance *instance, gpointer g_class) {
    MIRAGE_Language *self = MIRAGE_LANGUAGE(instance);
    MIRAGE_LanguagePrivate *_priv = MIRAGE_LANGUAGE_GET_PRIVATE(self);
    
    /* Allocate fields */
    _priv->packs_number = 16; /* Currently, we have 16 pack types */
    _priv->packs = g_new0(MIRAGE_Language_Pack, _priv->packs_number);
    
    return;
}

static void __mirage_language_finalize (GObject *obj) {
    MIRAGE_Language *self = MIRAGE_LANGUAGE(obj);
    MIRAGE_LanguagePrivate *_priv = MIRAGE_LANGUAGE_GET_PRIVATE(self);
    gint i;
    
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s:\n", __func__);

    /* Free private structure elements */
    for (i = 0; i < _priv->packs_number; i++) {
        g_free(_priv->packs[i].data);
    }
    g_free(_priv->packs);
    
    /* Chain up to the parent class */
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_GOBJECT, "%s: chaining up to parent\n", __func__);
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __mirage_language_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_LanguageClass *klass = MIRAGE_LANGUAGE_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_LanguagePrivate));
    
    /* Initialize GObject members */
    class_gobject->finalize = __mirage_language_finalize;

    return;
}

GType mirage_language_get_type (void) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_LanguageClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_language_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Language),
            0,      /* n_preallocs */
            __mirage_language_instance_init    /* instance_init */
        };
        
        type = g_type_register_static(MIRAGE_TYPE_OBJECT, "MIRAGE_Language", &info, 0);
    }
    
    return type;
}
