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
    GObject *parent; /* Soft-reference (= no ref) to parent */

    GObject *debug_context; /* Debug context */

    GList *children_list; /* "Children" list */
};


/**********************************************************************\
 *                         Private functions                          *
\**********************************************************************/
static void mirage_object_child_destroyed_handler (MirageObject *self, GObject *where_the_object_was)
{
    /* Remove child object's address from list */
    self->priv->children_list = g_list_remove(self->priv->children_list, where_the_object_was);
}


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
/**
 * mirage_object_set_parent:
 * @self: a #MirageObject
 * @parent: (in): parent
 *
 * <para>
 * Sets object's parent.
 * </para>
 **/
void mirage_object_set_parent (MirageObject *self, GObject *parent)
{
    if (self->priv->parent) {
        /* Remove previous weak reference pointer */
        g_object_remove_weak_pointer(G_OBJECT(self->priv->parent), (gpointer *)&self->priv->parent);
    }

    /* Set weak reference pointer to parent */
    if (parent) {
        /* Actually, we must manually set the parent pointer, too... even though
           one might think g_object_add_weak_pointer() will do it for us... */
        self->priv->parent = parent;
        g_object_add_weak_pointer(parent, (gpointer *)&self->priv->parent);
    } else {
        /* Passing NULL in parent means reseting current parent */
        self->priv->parent = NULL;
    }
}

/**
 * mirage_object_get_parent:
 * @self: a #MirageObject
 *
 * <para>
 * Retrieves object's parent.
 * </para>
 *
 * Returns: (transfer full): parent object, or %NULL.
 **/
GObject *mirage_object_get_parent (MirageObject *self)
{
    if (self->priv->parent) {
        g_object_ref(self->priv->parent);
    }
    return self->priv->parent;
}


/**
 * mirage_object_attach_child:
 * @self: a #MirageObject
 * @child: (in): child
 *
 * <para>
 * Attaches child to the object.
 * </para>
 **/
void mirage_object_attach_child (MirageObject *self, GObject *child)
{
    /* Add child to our children list */
    self->priv->children_list = g_list_append(self->priv->children_list, child);

    /* Add weak reference to child (so that when it gets destroyed, our callback
       will remove it from list) */
    g_object_weak_ref(child, (GWeakNotify)mirage_object_child_destroyed_handler, self);

    /* Set debug context to child */
    mirage_debuggable_set_debug_context(MIRAGE_DEBUGGABLE(child), self->priv->debug_context);
}

/**
 * mirage_object_detach_child:
 * @self: a #MirageObject
 * @child: (in): child
 *
 * <para>
 * Detaches child from the object. Note that the child will keep the debug context
 * it may have been passed to while being attached to the parent.
 * </para>
 **/
void mirage_object_detach_child (MirageObject *self, GObject *child)
{
    /* Remove child from our children list */
    self->priv->children_list = g_list_remove(self->priv->children_list, child);

    /* Remove weak reference to child */
    g_object_weak_unref(child, (GWeakNotify)mirage_object_child_destroyed_handler, self);
}


/**********************************************************************\
 *              MirageDebuggable methods implementation              *
\**********************************************************************/
static void mirage_object_set_debug_context (MirageDebuggable *_self, GObject *debug_context)
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

    /* Propagate the change to all children */
    for (GList *entry = self->priv->children_list; entry; entry = entry->next) {
        GObject *object = entry->data;
        mirage_debuggable_set_debug_context(MIRAGE_DEBUGGABLE(object), debug_context);
    }
}

static GObject *mirage_object_get_debug_context (MirageDebuggable *_self)
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
    self->priv->children_list = NULL;
}

static void mirage_object_dispose (GObject *gobject)
{
    MirageObject *self = MIRAGE_OBJECT(gobject);

    /* Remove weak reference pointer to parent */
    if (self->priv->parent) {
        g_object_remove_weak_pointer(G_OBJECT(self->priv->parent), (gpointer *)&self->priv->parent);
    }

    /* Unref debug context (if we have it) */
    if (self->priv->debug_context) {
        g_object_unref(self->priv->debug_context);
        self->priv->debug_context = NULL;
    }

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_object_parent_class)->dispose(gobject);
}

static void mirage_object_finalize (GObject *gobject)
{
    MirageObject *self = MIRAGE_OBJECT(gobject);

    /* Free our list of weak references to children */
    g_list_free(self->priv->children_list);

    /* Chain up to the parent class */
    return G_OBJECT_CLASS(mirage_object_parent_class)->finalize(gobject);
}

static void mirage_object_class_init (MirageObjectClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = mirage_object_dispose;
    gobject_class->finalize = mirage_object_finalize;

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
}

static void mirage_object_debuggable_init (MirageDebuggableInterface *iface)
{
    iface->set_debug_context = mirage_object_set_debug_context;
    iface->get_debug_context = mirage_object_get_debug_context;
}
