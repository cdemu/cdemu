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
 * @self: a #MirageDebuggable
 * @debug_context: (in) (transfer full): debug context (a #MirageDebugContext)
 *
 * <para>
 * Sets object's debug context.
 * </para>
 **/
void mirage_debuggable_set_debug_context (MirageDebuggable *self, MirageDebugContext *debug_context)
{
    return MIRAGE_DEBUGGABLE_GET_INTERFACE(self)->set_debug_context(self, debug_context);
}

/**
 * mirage_debuggable_get_debug_context:
 * @self: a #MirageDebuggable
 *
 * <para>
 * Retrieves object's debug context, without incrementing its reference counter.
 * </para>
 *
 * Returns: (transfer none): object's debug context (a #MirageDebugContext), or %NULL
 **/
MirageDebugContext *mirage_debuggable_get_debug_context (MirageDebuggable *self)
{
    return MIRAGE_DEBUGGABLE_GET_INTERFACE(self)->get_debug_context(self);
}


GType mirage_debuggable_get_type (void) {
    static GType iface_type = 0;
    if (iface_type == 0) {
        static const GTypeInfo info = {
            sizeof(MirageDebuggableInterface),
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

        iface_type = g_type_register_static(G_TYPE_INTERFACE, "MirageDebuggable", &info, 0);
    }

    return iface_type;
}


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_DEBUG_CONTEXT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_DEBUG_CONTEXT, MirageDebugContextPrivate))

struct _MirageDebugContextPrivate
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
 * @self: a #MirageDebugContext
 * @domain: (in): domain name
 *
 * <para>
 * Sets debug context's domain name to @domain.
 * </para>
 **/
void mirage_debug_context_set_domain (MirageDebugContext *self, const gchar *domain)
{
    /* Set domain */
    g_free(self->priv->domain);
    self->priv->domain = g_strdup(domain);
}

/**
 * mirage_debug_context_get_domain:
 * @self: a #MirageDebugContext
 *
 * <para>
 * Retrieves debug context's domain name.
 * </para>
 *
 * Returns: (transfer none): pointer to buffer containing the domain name, or %NULL. The buffer belongs to the object and should not be modified.
 **/
const gchar *mirage_debug_context_get_domain (MirageDebugContext *self)
{
    return self->priv->domain;
}


/**
 * mirage_debug_context_set_name:
 * @self: a #MirageDebugContext
 * @name: (in): name
 *
 * <para>
 * Sets debug context's name to @name.
 * </para>
 **/
void mirage_debug_context_set_name (MirageDebugContext *self, const gchar *name)
{
    /* Set name */
    g_free(self->priv->name);
    self->priv->name = g_strdup(name);
}

/**
 * mirage_debug_context_get_name:
 * @self: a #MirageDebugContext
 *
 * <para>
 * Retrieves debug context's name.
 * </para>
 *
 * Returns: (transfer none): pointer to buffer containing the name, or %NULL. The buffer belongs to the object and should not be modified.
 **/
const gchar *mirage_debug_context_get_name (MirageDebugContext *self)
{
    return self->priv->name;
}


/**
 * mirage_debug_context_set_debug_mask:
 * @self: a #MirageDebugContext
 * @debug_mask: (in): debug mask
 *
 * <para>
 * Sets debug context's debug mask.
 * </para>
 **/
void mirage_debug_context_set_debug_mask (MirageDebugContext *self, gint debug_mask)
{
    /* Set debug mask */
    self->priv->debug_mask = debug_mask;
}

/**
 * mirage_debug_context_get_debug_mask:
 * @self: a #MirageDebugContext
 *
 * <para>
 * Retrieves debug context's debug mask.
 * </para>
 *
 * Returns: debug context's debug mask
 **/
gint mirage_debug_context_get_debug_mask (MirageDebugContext *self)
{
    /* Return debug mask */
    return self->priv->debug_mask;
}


/**
 * mirage_debug_context_messagev:
 * @self: a #MirageDebugContext
 * @level: (in): debug level
 * @format: (in): message format. See the printf() documentation.
 * @args: (in): parameters to insert into the format string.
 *
 * <para>
 * Outputs debug message with verbosity level @level, format string @format and
 * format arguments @args. The message is displayed if debug context has mask
 * that covers @level, or if @level is either %MIRAGE_DEBUG_WARNING or
 * %MIRAGE_DEBUG_ERROR.
 * </para>
 **/
void mirage_debug_context_messagev (MirageDebugContext *self, gint level, gchar *format, va_list args)
{
    gchar *new_format;

    /* Insert name in case we have it */
    if (self->priv->name) {
        new_format = g_strdup_printf("%s: %s", self->priv->name, format);
    } else {
        new_format = g_strdup(format);
    }

    if (level == MIRAGE_DEBUG_ERROR) {
        g_logv(self->priv->domain, G_LOG_LEVEL_ERROR, new_format, args);
    } else if (level == MIRAGE_DEBUG_WARNING) {
        g_logv(self->priv->domain, G_LOG_LEVEL_WARNING, new_format, args);
    } else {
        if (self->priv->debug_mask & level) {
            g_logv(self->priv->domain, G_LOG_LEVEL_DEBUG, new_format, args);
        }
    }

    g_free(new_format);
}

/**
 * mirage_debug_context_message:
 * @self: a #MirageDebugContext
 * @level: (in): debug level
 * @format: (in): message format. See the printf() documentation.
 * @...: (in): parameters to insert into the format string.
 *
 * <para>
 * Outputs debug message with verbosity level @level, format string @format and
 * format arguments @Varargs. The message is displayed if debug context has mask
 * that covers @level, or if @level is either %MIRAGE_DEBUG_WARNING or
 * %MIRAGE_DEBUG_ERROR.
 * </para>
 **/
void mirage_debug_context_message (MirageDebugContext *self, gint level, gchar *format, ...)
{
    va_list args;
    va_start(args, format);
    mirage_debug_context_messagev(self, level, format, args);
    va_end(args);
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE(MirageDebugContext, mirage_debug_context, G_TYPE_OBJECT);


static void mirage_debug_context_init (MirageDebugContext *self)
{
    self->priv = MIRAGE_DEBUG_CONTEXT_GET_PRIVATE(self);

    self->priv->domain = NULL;
    self->priv->name = NULL;
}

static void mirage_debug_context_finalize (GObject *gobject)
{
    MirageDebugContext *self = MIRAGE_DEBUG_CONTEXT(gobject);

    g_free(self->priv->domain);
    g_free(self->priv->name);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_debug_context_parent_class)->finalize(gobject);
}

static void mirage_debug_context_class_init (MirageDebugContextClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = mirage_debug_context_finalize;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageDebugContextPrivate));
}
