/*
 *  libMirage: Debug context object
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
#define MIRAGE_DEBUG_CONTEXT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_DEBUG_CONTEXT, MIRAGE_DebugContextPrivate))

typedef struct {    
    gchar *name; /* Debug context name... e.g. 'Device 1' */
    gchar *domain; /* Debug context domain... e.g. 'libMirage' */
    
    gint debug_mask; /* Debug mask */
} MIRAGE_DebugContextPrivate;


/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
/**
 * mirage_debug_context_set_domain:
 * @self: a #MIRAGE_DebugContext
 * @domain: domain name
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets debug context's domain name to @domain.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_debug_context_set_domain (MIRAGE_DebugContext *self, gchar *domain, GError **error) {
    MIRAGE_DebugContextPrivate *_priv = MIRAGE_DEBUG_CONTEXT_GET_PRIVATE(self);
    /* Set domain */
    g_free(_priv->domain);
    _priv->domain = g_strdup(domain);
    return TRUE;
}

/**
 * mirage_debug_context_get_domain:
 * @self: a #MIRAGE_DebugContext
 * @domain: location to store domain name
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves debug context's domain name.
 * </para>
 *
 * <para>
 * A copy of domain name is stored into @domain; it should be freed with
 * g_free() when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_debug_context_get_domain (MIRAGE_DebugContext *self, gchar **domain, GError **error) {
    MIRAGE_DebugContextPrivate *_priv = MIRAGE_DEBUG_CONTEXT_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(domain);
    /* Return domain, if it's set */
    if (_priv->domain) {
        *domain = g_strdup(_priv->domain);
    }
    return TRUE;
}


/**
 * mirage_debug_context_set_name:
 * @self: a #MIRAGE_DebugContext
 * @name: name
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets debug context's name to @name.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_debug_context_set_name (MIRAGE_DebugContext *self, gchar *name, GError **error) {
    MIRAGE_DebugContextPrivate *_priv = MIRAGE_DEBUG_CONTEXT_GET_PRIVATE(self);
    /* Set name */
    g_free(_priv->name);
    _priv->name = g_strdup(name);
    return TRUE;
}

/**
 * mirage_debug_context_get_name:
 * @self: a #MIRAGE_DebugContext
 * @name: location to store name
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves debug context's name.
 * </para>
 *
 * <para>
 * A copy of name is stored into @name; it should be freed with g_free() when 
 * no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_debug_context_get_name (MIRAGE_DebugContext *self, gchar **name, GError **error) {
    MIRAGE_DebugContextPrivate *_priv = MIRAGE_DEBUG_CONTEXT_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(name);    
    /* Return name, if it's set */
    if (_priv->name) {
        *name= g_strdup(_priv->name);
    }
    return TRUE;
}


/**
 * mirage_debug_context_set_debug_mask:
 * @self: a #MIRAGE_DebugContext
 * @debug_mask: debug mask
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets debug context's debug mask.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_debug_context_set_debug_mask (MIRAGE_DebugContext *self, gint debug_mask, GError **error) {
    MIRAGE_DebugContextPrivate *_priv = MIRAGE_DEBUG_CONTEXT_GET_PRIVATE(self);
    /* Set debug mask */
    _priv->debug_mask = debug_mask;
    return TRUE;
}

/**
 * mirage_debug_context_get_debug_mask:
 * @self: a #MIRAGE_DebugContext
 * @debug_mask: location to store debug mask
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves debug context's debug mask.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_debug_context_get_debug_mask (MIRAGE_DebugContext *self, gint *debug_mask, GError **error) {
    MIRAGE_DebugContextPrivate *_priv = MIRAGE_DEBUG_CONTEXT_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(debug_mask);
    /* Return debug mask */
    *debug_mask = _priv->debug_mask;
    return TRUE;
}


/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static GObjectClass *parent_class = NULL;

static void __mirage_debug_context_finalize (GObject *obj) {
    MIRAGE_DebugContext *self = MIRAGE_DEBUG_CONTEXT(obj);
    MIRAGE_DebugContextPrivate *_priv = MIRAGE_DEBUG_CONTEXT_GET_PRIVATE(self);
    
    g_free(_priv->domain);
    g_free(_priv->name);
    
    /* Chain up to the parent class */
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __mirage_debug_context_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_DebugContextClass *klass = MIRAGE_DEBUG_CONTEXT_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_DebugContextPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_debug_context_finalize;
        
    return;
}

GType mirage_debug_context_get_type (void) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_DebugContextClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_debug_context_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_DebugContext),
            0,      /* n_preallocs */
            NULL    /* instance_init */
        };
        
        type = g_type_register_static(G_TYPE_OBJECT, "MIRAGE_DebugContext", &info, 0);
    }
    
    return type;
}
