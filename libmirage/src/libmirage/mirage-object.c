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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * SECTION: mirage-object
 * @title: MirageObject
 * @short_description: Base object class.
 * @see_also: #MirageContext, #MirageContextual
 * @include: mirage-object.h
 *
 * #MirageObject is used as a base object class throughout libMirage. It
 * implements #MirageContextual interface, which allows attachment of
 * #MirageContext. It also implements support for constructing parent-child
 * hierarchy, which allows propagation of the #MirageContext from parent
 * to the child objects.
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

    MirageContext *context;
};


/**********************************************************************\
 *                        Debug context changes                       *
\**********************************************************************/
static void mirage_object_parent_context_changed_handler (MirageObject *self, MirageObject *parent)
{
    /* Get the new context and set it */
    MirageContext *context = mirage_contextual_get_context(MIRAGE_CONTEXTUAL(parent));
    mirage_contextual_set_context(MIRAGE_CONTEXTUAL(self), context);
    if (context) {
        g_object_unref(context);
    }
}


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
/**
 * mirage_object_set_parent:
 * @self: a #MirageObject
 * @parent: (in) (allow-none) (type MirageObject): parent
 *
 * Sets object's parent. If @parent is %NULL, the object's parent is
 * reset.
 */
void mirage_object_set_parent (MirageObject *self, gpointer parent)
{
    if (self->priv->parent) {
        /* Remove "debug-context-change" signal handler */
        g_signal_handlers_disconnect_by_func(self->priv->parent, mirage_object_parent_context_changed_handler, self);

        /* Remove previous weak reference pointer */
        g_object_remove_weak_pointer(G_OBJECT(self->priv->parent), &self->priv->parent);
    }

    self->priv->parent = parent;

    if (parent) {
        /* Add weak pointer to parent */
        g_object_add_weak_pointer(parent, &self->priv->parent);

        /* Connect "*/
        g_signal_connect_swapped(parent, "context-changed", (GCallback)mirage_object_parent_context_changed_handler, self);

        /* Set parent's context by simulating the signal */
        mirage_object_parent_context_changed_handler(self, parent);
    }
}

/**
 * mirage_object_get_parent:
 * @self: a #MirageObject
 *
 * Returns pointer to object's parent object.
 *
 * Returns: (transfer full) (type MirageObject): parent object, or %NULL.
 */
gpointer mirage_object_get_parent (MirageObject *self)
{
    if (self->priv->parent) {
        g_object_ref(self->priv->parent);
    }
    return self->priv->parent;
}


/**********************************************************************\
 *              MirageContextual methods implementation               *
\**********************************************************************/
static void mirage_object_set_context (MirageContextual *_self, MirageContext *context)
{
    MirageObject *self = MIRAGE_OBJECT(_self);

    if (context == self->priv->context) {
        /* Don't do anything if we're trying to set the same context */
        return;
    }

    /* If context is already set, free it */
    if (self->priv->context) {
        g_object_unref(self->priv->context);
        self->priv->context = NULL;
    }

    /* Set context and ref it */
    if (context) {
        self->priv->context = g_object_ref(context);
    }

    /* Signal change, so that children object can pick it up */
    g_signal_emit_by_name(self, "context-changed", NULL);
}

static MirageContext *mirage_object_get_context (MirageContextual *_self)
{
    MirageObject *self = MIRAGE_OBJECT(_self);
    if (self->priv->context) {
        g_object_ref(self->priv->context);
    }
    return self->priv->context;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
static void mirage_object_contextual_init (MirageContextualInterface *iface);

G_DEFINE_TYPE_WITH_CODE(MirageObject,
                        mirage_object,
                        G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(MIRAGE_TYPE_CONTEXTUAL, mirage_object_contextual_init));

static void mirage_object_init (MirageObject *self)
{
    self->priv = MIRAGE_OBJECT_GET_PRIVATE(self);

    self->priv->parent = NULL;
    self->priv->context = NULL;
}

static void mirage_object_dispose (GObject *gobject)
{
    MirageObject *self = MIRAGE_OBJECT(gobject);

    /* Remove weak reference pointer to parent */
    if (self->priv->parent) {
        g_signal_handlers_disconnect_by_func(self->priv->parent, mirage_object_parent_context_changed_handler, self);
        g_object_remove_weak_pointer(G_OBJECT(self->priv->parent), &self->priv->parent);
        self->priv->parent = NULL;
    }

    /* Unref context */
    if (self->priv->context) {
        g_object_unref(self->priv->context);
        self->priv->context = NULL;
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
     * MirageObject::context-changed:
     * @object: a #MirageObject
     *
     * Emitted when a new #MirageContext is set to a #MirageObject.
     */
    g_signal_new("context-changed", G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, NULL);
}

static void mirage_object_contextual_init (MirageContextualInterface *iface)
{
    iface->set_context = mirage_object_set_context;
    iface->get_context = mirage_object_get_context;
}
