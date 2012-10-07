/*
 *  libMirage: Base object
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
#define MIRAGE_OBJECT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_OBJECT, MirageObjectPrivate))

struct _MirageObjectPrivate
{
    gpointer parent; /* Soft-reference (= no ref) to parent */

    MirageDebugContext *debug_context; /* Debug context */
};


/**********************************************************************\
 *                        Debug context changes                       *
\**********************************************************************/
static void mirage_object_parent_debug_context_changed_handler (MirageObject *parent, MirageObject *self)
{
    /* Get the new debug context and set it */
    MirageDebugContext *debug_context = mirage_debuggable_get_debug_context(MIRAGE_DEBUGGABLE(parent));
    mirage_debuggable_set_debug_context(MIRAGE_DEBUGGABLE(self), debug_context);
}


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
/**
 * mirage_object_set_parent:
 * @self: a #MirageObject
 * @parent: (in) (allow-none) (type MirageObject): parent
 *
 * <para>
 * Sets object's parent. If @parent is %NULL, the object's parent is
 * reset.
 * </para>
 **/
void mirage_object_set_parent (MirageObject *self, gpointer parent)
{
    if (self->priv->parent) {
        /* Remove "debug-context-change" signal handler */
        g_signal_handlers_disconnect_by_func(self->priv->parent, mirage_object_parent_debug_context_changed_handler, self);

        /* Remove previous weak reference pointer */
        g_object_remove_weak_pointer(G_OBJECT(self->priv->parent), &self->priv->parent);
    }

    self->priv->parent = parent;

    if (parent) {
        /* Add weak pointer to parent */
        g_object_add_weak_pointer(parent, &self->priv->parent);

        /* Connect "*/
        g_signal_connect(parent, "debug-context-changed", (GCallback)mirage_object_parent_debug_context_changed_handler, self);

        /* Set parent's debug context by simulating the signal */
        mirage_object_parent_debug_context_changed_handler(parent, self);
    }
}

/**
 * mirage_object_get_parent:
 * @self: a #MirageObject
 *
 * <para>
 * Returns pointer to object's parent object.
 * </para>
 *
 * Returns: (transfer full) (type MirageObject): parent object, or %NULL.
 **/
gpointer mirage_object_get_parent (MirageObject *self)
{
    if (self->priv->parent) {
        g_object_ref(self->priv->parent);
    }
    return self->priv->parent;
}


/**********************************************************************\
 *              MirageDebuggable methods implementation              *
\**********************************************************************/
static void mirage_object_set_debug_context (MirageDebuggable *_self, MirageDebugContext *debug_context)
{
    MirageObject *self = MIRAGE_OBJECT(_self);

    if (debug_context == self->priv->debug_context) {
        /* Don't do anything if we're trying to set the same context */
        return;
    }

    /* If debug context is already set, free it */
    if (self->priv->debug_context) {
        g_object_unref(self->priv->debug_context);
    }

    /* Set debug context and ref it */
    self->priv->debug_context = debug_context;
    if (self->priv->debug_context) {
        g_object_ref(self->priv->debug_context);
    }

    /* Signal change, so that children object can pick it up */
    g_signal_emit_by_name(self, "debug-context-changed", NULL);
}

static MirageDebugContext *mirage_object_get_debug_context (MirageDebuggable *_self)
{
    MirageObject *self = MIRAGE_OBJECT(_self);
    return self->priv->debug_context;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
static void mirage_object_debuggable_init (MirageDebuggableInterface *iface);

G_DEFINE_TYPE_EXTENDED(MirageObject,
                       mirage_object,
                       G_TYPE_OBJECT,
                       0,
                       G_IMPLEMENT_INTERFACE(MIRAGE_TYPE_DEBUGGABLE,
                                             mirage_object_debuggable_init));

static void mirage_object_init (MirageObject *self)
{
    self->priv = MIRAGE_OBJECT_GET_PRIVATE(self);

    self->priv->parent = NULL;
    self->priv->debug_context = NULL;
}

static void mirage_object_dispose (GObject *gobject)
{
    MirageObject *self = MIRAGE_OBJECT(gobject);

    /* Remove weak reference pointer to parent */
    if (self->priv->parent) {
        g_object_remove_weak_pointer(G_OBJECT(self->priv->parent), &self->priv->parent);
    }

    /* Unref debug context (if we have it) */
    if (self->priv->debug_context) {
        g_object_unref(self->priv->debug_context);
        self->priv->debug_context = NULL;
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_object_parent_class)->dispose(gobject);
}

static void mirage_object_class_init (MirageObjectClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = mirage_object_dispose;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageObjectPrivate));

    /* Signals */
    /**
     * MirageObject::object-modified:
     * @mirage_object: the object which received the signal
     *
     * <para>
     * Emitted when a #MirageObject is changed in a way that causes bottom-up change.
     * </para>
     */
    klass->signal_object_modified = g_signal_new("object-modified", G_OBJECT_CLASS_TYPE(klass), (G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED), 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, NULL);
    klass->signal_debug_context_changed = g_signal_new("debug-context-changed", G_OBJECT_CLASS_TYPE(klass), (G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED), 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, NULL);
}

static void mirage_object_debuggable_init (MirageDebuggableInterface *iface)
{
    iface->set_debug_context = mirage_object_set_debug_context;
    iface->get_debug_context = mirage_object_get_debug_context;
}
