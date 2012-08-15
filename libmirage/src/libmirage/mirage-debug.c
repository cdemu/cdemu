/*
 *  libMirage: Debugging facilities
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
 *                          Debuggable interface                      *
\**********************************************************************/
/**
 * mirage_debuggable_set_debug_context:
 * @self: a #MIRAGE_Debuggable
 * @debug_context: (in): debug context (a #MIRAGE_DebugContext)
 *
 * <para>
 * Sets object's debug context.
 * </para>
 **/
void mirage_debuggable_set_debug_context (MIRAGE_Debuggable *self, GObject *debug_context)
{
    return MIRAGE_DEBUGGABLE_GET_INTERFACE(self)->set_debug_context(self, debug_context);
}

/**
 * mirage_debuggable_get_debug_context:
 * @self: a #MIRAGE_Debuggable
 *
 * <para>
 * Retrieves object's debug context.
 * </para>
 *
 * Returns: (transfer none): object's debug context (a #MIRAGE_DebugContext), or %NULL
 **/
GObject *mirage_debuggable_get_debug_context (MIRAGE_Debuggable *self)
{
    return MIRAGE_DEBUGGABLE_GET_INTERFACE(self)->get_debug_context(self);
}

/**
 * mirage_debuggable_debug_messagev:
 * @self: a #MIRAGE_Debuggable
 * @level: (in): debug level
 * @format: (in): message format. See the printf() documentation.
 * @args: (in): parameters to insert into the format string.
 *
 * <para>
 * Outputs debug message with verbosity level @level, format string @format and
 * format arguments @args. The message is displayed if object's debug context
 * has mask that covers @level, or if @level is either %MIRAGE_DEBUG_WARNING or
 * %MIRAGE_DEBUG_ERROR.
 * </para>
 **/
void mirage_debuggable_debug_messagev (MIRAGE_Debuggable *self, gint level, gchar *format, va_list args)
{
    return MIRAGE_DEBUGGABLE_GET_INTERFACE(self)->debug_messagev(self, level, format, args);
}

/**
 * mirage_debuggable_debug_message:
 * @self: a #MIRAGE_Debuggable
 * @level: (in): debug level
 * @format: (in): message format. See the printf() documentation.
 * @...: (in): parameters to insert into the format string.
 *
 * <para>
 * Outputs debug message with verbosity level @level, format string @format and
 * format arguments @Varargs. The message is displayed if object's debug context
 * has mask that covers @level, or if @level is either %MIRAGE_DEBUG_WARNING or
 * %MIRAGE_DEBUG_ERROR.
 * </para>
 **/
void mirage_debuggable_debug_message (MIRAGE_Debuggable *self, gint level, gchar *format, ...)
{
    va_list args;
    va_start(args, format);
    MIRAGE_DEBUGGABLE_GET_INTERFACE(self)->debug_messagev(self, level, format, args);
    va_end(args);
}

GType mirage_debuggable_get_type (void) {
    static GType iface_type = 0;
    if (iface_type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_DebuggableInterface),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            NULL,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            0,
            0,      /* n_preallocs */
            NULL,   /* instance_init */
            NULL    /* value_table */
        };

        iface_type = g_type_register_static(G_TYPE_INTERFACE, "MIRAGE_Debuggable", &info, 0);
    }

    return iface_type;
}


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
 * @domain: (in): domain name
 *
 * <para>
 * Sets debug context's domain name to @domain.
 * </para>
 **/
void mirage_debug_context_set_domain (MIRAGE_DebugContext *self, const gchar *domain)
{
    /* Set domain */
    g_free(self->priv->domain);
    self->priv->domain = g_strdup(domain);
}

/**
 * mirage_debug_context_get_domain:
 * @self: a #MIRAGE_DebugContext
 *
 * <para>
 * Retrieves debug context's domain name.
 * </para>
 *
 * Returns: (transfer none): pointer to buffer containing the domain name, or %NULL. The buffer belongs to the object and should not be modified.
 **/
const gchar *mirage_debug_context_get_domain (MIRAGE_DebugContext *self)
{
    return self->priv->domain;
}


/**
 * mirage_debug_context_set_name:
 * @self: a #MIRAGE_DebugContext
 * @name: (in): name
 *
 * <para>
 * Sets debug context's name to @name.
 * </para>
 **/
void mirage_debug_context_set_name (MIRAGE_DebugContext *self, const gchar *name)
{
    /* Set name */
    g_free(self->priv->name);
    self->priv->name = g_strdup(name);
}

/**
 * mirage_debug_context_get_name:
 * @self: a #MIRAGE_DebugContext
 *
 * <para>
 * Retrieves debug context's name.
 * </para>
 *
 * Returns: pointer to buffer containing the name, or %NULL. The buffer belongs to the object and should not be modified.
 **/
const gchar *mirage_debug_context_get_name (MIRAGE_DebugContext *self)
{
    return self->priv->name;
}


/**
 * mirage_debug_context_set_debug_mask:
 * @self: a #MIRAGE_DebugContext
 * @debug_mask: (in): debug mask
 *
 * <para>
 * Sets debug context's debug mask.
 * </para>
 **/
void mirage_debug_context_set_debug_mask (MIRAGE_DebugContext *self, gint debug_mask)
{
    /* Set debug mask */
    self->priv->debug_mask = debug_mask;
}

/**
 * mirage_debug_context_get_debug_mask:
 * @self: a #MIRAGE_DebugContext
 *
 * <para>
 * Retrieves debug context's debug mask.
 * </para>
 *
 * Returns: debug context's debug mask
 **/
gint mirage_debug_context_get_debug_mask (MIRAGE_DebugContext *self)
{
    /* Return debug mask */
    return self->priv->debug_mask;
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
