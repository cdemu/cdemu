/*
 *  libMirage: Debug context object
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


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_DEBUG_CONTEXT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_DEBUG_CONTEXT, MIRAGE_DebugContextPrivate))

struct _MIRAGE_DebugContextPrivate
{    
    gchar *name; /* Debug context name... e.g. 'Device 1' */
    gchar *domain; /* Debug context domain... e.g. 'libMirage' */
    
    gint debug_mask; /* Debug mask */
};


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
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
gboolean mirage_debug_context_set_domain (MIRAGE_DebugContext *self, const gchar *domain, GError **error G_GNUC_UNUSED)
{
    /* Set domain */
    g_free(self->priv->domain);
    self->priv->domain = g_strdup(domain);
    return TRUE;
}

/**
 * mirage_debug_context_get_domain:
 * @self: a #MIRAGE_DebugContext
 * @domain: location to store pointer to domain name buffer
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves debug context's domain name.
 * </para>
 *
 * <para>
 * Pointer to buffer containing the domain name is stored into @domain; buffer
 * belongs to the object and therefore should not be modified.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_debug_context_get_domain (MIRAGE_DebugContext *self, const gchar **domain, GError **error)
{
    MIRAGE_CHECK_ARG(domain);
    *domain = self->priv->domain;
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
gboolean mirage_debug_context_set_name (MIRAGE_DebugContext *self, const gchar *name, GError **error G_GNUC_UNUSED)
{
    /* Set name */
    g_free(self->priv->name);
    self->priv->name = g_strdup(name);
    return TRUE;
}

/**
 * mirage_debug_context_get_name:
 * @self: a #MIRAGE_DebugContext
 * @name: location to store pointer to name buffer
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves debug context's name.
 * </para>
 *
 * <para>
 * Pointer to buffer containing the name is stored into @name; buffer
 * belongs to the object and therefore should not be modified.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_debug_context_get_name (MIRAGE_DebugContext *self, const gchar **name, GError **error)
{
    MIRAGE_CHECK_ARG(name);
    *name = self->priv->name;
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
gboolean mirage_debug_context_set_debug_mask (MIRAGE_DebugContext *self, gint debug_mask, GError **error G_GNUC_UNUSED)
{
    /* Set debug mask */
    self->priv->debug_mask = debug_mask;
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
gboolean mirage_debug_context_get_debug_mask (MIRAGE_DebugContext *self, gint *debug_mask, GError **error)
{
    MIRAGE_CHECK_ARG(debug_mask);
    /* Return debug mask */
    *debug_mask = self->priv->debug_mask;
    return TRUE;
}


/**********************************************************************\
 *                             Object init                            * 
\**********************************************************************/
G_DEFINE_TYPE(MIRAGE_DebugContext, mirage_debug_context, G_TYPE_OBJECT);


static void mirage_debug_context_init (MIRAGE_DebugContext *self)
{
    self->priv = MIRAGE_DEBUG_CONTEXT_GET_PRIVATE(self);

    self->priv->domain = NULL;
    self->priv->name = NULL;
}

static void mirage_debug_context_finalize (GObject *gobject)
{
    MIRAGE_DebugContext *self = MIRAGE_DEBUG_CONTEXT(gobject);

    g_free(self->priv->domain);
    g_free(self->priv->name);
    
    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_debug_context_parent_class)->finalize(gobject);
}

static void mirage_debug_context_class_init (MIRAGE_DebugContextClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = mirage_debug_context_finalize;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_DebugContextPrivate));
}
