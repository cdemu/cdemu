/*
 *  libMirage: Base object
 *  Copyright (C) 2006-2009 Rok Mandeljc
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
#define MIRAGE_OBJECT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_OBJECT, MIRAGE_ObjectPrivate))

typedef struct {    
    GObject *parent; /* Soft-reference (= no ref) to parent */
    
    GObject *debug_context; /* Debug context */
    
    GList *children_list; /* "Children" list */
} MIRAGE_ObjectPrivate;


/******************************************************************************\
 *                              Private functions                             *
\******************************************************************************/
static void __child_destroyed_handler (MIRAGE_Object *self, GObject *where_the_object_was) {
    MIRAGE_ObjectPrivate *_priv = MIRAGE_OBJECT_GET_PRIVATE(self);

    /* Remove child object's address from list */
    _priv->children_list = g_list_remove(_priv->children_list, where_the_object_was);
    
    return;
}


/******************************************************************************\
 *                                 Public API                                 *
\******************************************************************************/
/**
 * mirage_object_set_debug_context:
 * @self: a #MIRAGE_Object
 * @debug_context: debug context
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets object's debug context.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_object_set_debug_context (MIRAGE_Object *self, GObject *debug_context, GError **error) {
    MIRAGE_ObjectPrivate *_priv = MIRAGE_OBJECT_GET_PRIVATE(self);
    GList *entry = NULL;
    
    if (debug_context == _priv->debug_context) {
        /* Don't do anything if we're trying to set the same context */
        return TRUE;
    }
    
    /* If debug context is already set, free it */
    if (_priv->debug_context) {
        g_object_unref(_priv->debug_context);
    }
    
    /* Set debug context and ref it */
    _priv->debug_context = debug_context;
    g_object_ref(_priv->debug_context);
    
    /* Propagate the change to all children */
    G_LIST_FOR_EACH(entry, _priv->children_list) {
        GObject *object = entry->data;
        mirage_object_set_debug_context(MIRAGE_OBJECT(object), debug_context, NULL);
    }
    
    return TRUE;
}

/**
 * mirage_object_get_debug_context:
 * @self: a #MIRAGE_Object
 * @debug_context: location to store debug context, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves object's debug context. A reference to debug context is stored in 
 * @debug_context; it should be released with g_object_unref() when no longer needed.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_object_get_debug_context (MIRAGE_Object *self, GObject **debug_context, GError **error) {
    MIRAGE_ObjectPrivate *_priv = MIRAGE_OBJECT_GET_PRIVATE(self);
    MIRAGE_CHECK_ARG(debug_context);
    
    /* Make sure we have debug context set */
    if (!_priv->debug_context) {
        mirage_error(MIRAGE_E_NODEBUGCONTEXT, error);
        return FALSE;
    }
    
    if (debug_context) {
        /* Return debug context and ref it */
        *debug_context = _priv->debug_context;
        g_object_ref(*debug_context);
    }
    
    return TRUE;
}

/**
 * mirage_object_debug_messagev:
 * @self: a #MIRAGE_Object
 * @level: debug level
 * @format: message format. See the printf() documentation.
 * @args: parameters to insert into the format string.
 *
 * <para>
 * Outputs debug message with verbosity level @level, format string @format and
 * format arguments @args. The message is displayed if object's debug context
 * has mask that covers @level, or if @level is either %MIRAGE_DEBUG_WARNING or
 * %MIRAGE_DEBUG_ERROR.
 * </para>
 **/
void mirage_object_debug_messagev (MIRAGE_Object *self, gint level, gchar *format, va_list args) {
    MIRAGE_ObjectPrivate *_priv = MIRAGE_OBJECT_GET_PRIVATE(self);
    gint debug_mask = 0;
    
    gchar *name = NULL;
    gchar *domain = NULL;
    
    gchar *new_format = NULL;
    
    /* Make sure we have debug context set */
    if (!_priv->debug_context || !MIRAGE_IS_DEBUG_CONTEXT(_priv->debug_context)) {
        return;
    }
    
    /* Get debug mask, domain and name */
    mirage_debug_context_get_debug_mask(MIRAGE_DEBUG_CONTEXT(_priv->debug_context), &debug_mask, NULL);
    mirage_debug_context_get_domain(MIRAGE_DEBUG_CONTEXT(_priv->debug_context), &domain, NULL);
    mirage_debug_context_get_name(MIRAGE_DEBUG_CONTEXT(_priv->debug_context), &name, NULL);
    
    /* Insert name in case we have it */
    if (name) {
        new_format = g_strdup_printf("%s: %s", name, format);
    } else {
        new_format = g_strdup(format);
    }
    
    if (level == MIRAGE_DEBUG_ERROR) {
        g_logv(domain, G_LOG_LEVEL_ERROR, new_format, args);
    } else if (level == MIRAGE_DEBUG_WARNING) {
        g_logv(domain, G_LOG_LEVEL_WARNING, new_format, args);
    } else {
        if (debug_mask & level) {
            g_logv(domain, G_LOG_LEVEL_DEBUG, new_format, args);
        }
    }
    
    g_free(name);
    g_free(domain);
    g_free(new_format);
    
    return;
}

/**
 * mirage_object_debug_message:
 * @self: a #MIRAGE_Object
 * @level: debug level
 * @format: message format. See the printf() documentation.
 * @Varargs: parameters to insert into the format string.
 *
 * <para>
 * Outputs debug message with verbosity level @level, format string @format and
 * format arguments @Varargs. The message is displayed if object's debug context
 * has mask that covers @level, or if @level is either %MIRAGE_DEBUG_WARNING or
 * %MIRAGE_DEBUG_ERROR.
 * </para>
 **/
void mirage_object_debug_message (MIRAGE_Object *self, gint level, gchar *format, ...) {
    va_list args;

    va_start(args, format); 
    mirage_object_debug_messagev(self, level, format, args);
    va_end(args);
    
    return;
}

/**
 * mirage_object_set_parent:
 * @self: a #MIRAGE_Object
 * @parent: parent
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets object's parent.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_object_set_parent (MIRAGE_Object *self, GObject *parent, GError **error) {
    MIRAGE_ObjectPrivate *_priv = MIRAGE_OBJECT_GET_PRIVATE(self);

    if (_priv->parent) {
        /* Remove previous weak reference pointer */
        g_object_remove_weak_pointer(G_OBJECT(_priv->parent), (gpointer *)&_priv->parent);
    }
    
    /* Set weak reference pointer to parent */
    if (parent) {
        /* Actually, we must manually set the parent pointer, too... even though
           one might think g_object_add_weak_pointer() will do it for us... */
        _priv->parent = parent;
        g_object_add_weak_pointer(parent, (gpointer *)&_priv->parent);
    } else {
        /* Passing NULL in parent means reseting current parent */
        _priv->parent = NULL;
    }
    
    return TRUE;
}

/**
 * mirage_object_get_parent:
 * @self: a #MIRAGE_Object
 * @parent: location to store parent, or %NULL
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves object's parent. 
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_object_get_parent (MIRAGE_Object *self, GObject **parent, GError **error) {
    MIRAGE_ObjectPrivate *_priv = MIRAGE_OBJECT_GET_PRIVATE(self);
    
    /* Make sure we have parent set */
    if (!_priv->parent) {
        mirage_error(MIRAGE_E_NOPARENT, error);
        return FALSE;
    }
    
    if (parent) {
        /* Return parent and ref it */
        g_object_ref(_priv->parent);
        *parent = _priv->parent;
    }
    
    return TRUE;
}


/**
 * mirage_object_attach_child:
 * @self: a #MIRAGE_Object
 * @child: child
 * @error: location to store error, or %NULL
 *
 * <para>
 * Attaches child to the object.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_object_attach_child (MIRAGE_Object *self, GObject *child, GError **error) {
    MIRAGE_ObjectPrivate *_priv = MIRAGE_OBJECT_GET_PRIVATE(self);
    
    /* Add child to our children list */
    _priv->children_list = g_list_append(_priv->children_list, child);
    
    /* Add weak reference to child (so that when it gets destroyed, our callback
       will remove it from list) */
    g_object_weak_ref(child, (GWeakNotify)__child_destroyed_handler, self);
        
    /* If we have debug context set, set it to child as well */
    if (_priv->debug_context) {
        if (!mirage_object_set_debug_context(MIRAGE_OBJECT(child), _priv->debug_context, error)) {
            return FALSE;
        }
    }
    
    return TRUE;
}

/**
 * mirage_object_detach_child:
 * @self: a #MIRAGE_Object
 * @child: child
 * @error: location to store error, or %NULL
 *
 * <para>
 * Detaches child from the object. Note that the child will keep the debug context
 * it may have been passed to while being attached to the parent.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_object_detach_child (MIRAGE_Object *self, GObject *child, GError **error) {
    MIRAGE_ObjectPrivate *_priv = MIRAGE_OBJECT_GET_PRIVATE(self);
    
    /* Remove child from our children list */
    _priv->children_list = g_list_remove(_priv->children_list, child);
    
    /* Remove weak reference to child */
    g_object_weak_unref(child, (GWeakNotify)__child_destroyed_handler, self);
    
    return TRUE;
}


/******************************************************************************\
 *                                 Object init                                *
\******************************************************************************/
/* Our parent class */
static GObjectClass *parent_class = NULL;

static void __mirage_object_finalize (GObject *obj) {
    MIRAGE_Object *self = MIRAGE_OBJECT(obj);
    MIRAGE_ObjectPrivate *_priv = MIRAGE_OBJECT_GET_PRIVATE(self);
        
    /* Remove weak reference pointer to parent */
    if (_priv->parent) {
        g_object_remove_weak_pointer(G_OBJECT(_priv->parent), (gpointer *)&_priv->parent);
    }
    
    /* Unref debug context (if we have it) */
    if (_priv->debug_context) {
        g_object_unref(_priv->debug_context);
    }
    
    /* Free our list of weak references to children */
    g_list_free(_priv->children_list);
        
    /* Chain up to the parent class */
    return G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void __mirage_object_class_init (gpointer g_class, gpointer g_class_data) {
    GObjectClass *class_gobject = G_OBJECT_CLASS(g_class);
    MIRAGE_ObjectClass *klass = MIRAGE_OBJECT_CLASS(g_class);
    
    /* Set parent class */
    parent_class = g_type_class_peek_parent(klass);
    
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_ObjectPrivate));
    
    /* Initialize GObject methods */
    class_gobject->finalize = __mirage_object_finalize;
        
    /* Signals */
    /**
     * MIRAGE_Object::object-modified:
     * @mirage_object: the object which received the signal
     *
     * <para>
     * Emitted each @mirage_object is changed in a way that causes bottom-up change.
     * </para>
     */
    klass->signal_object_modified = g_signal_new("object-modified", G_OBJECT_CLASS_TYPE(klass), (G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED), 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, NULL);
    
    return;
}

GType mirage_object_get_type (void) {
    static GType type = 0;
    if (type == 0) {
        static const GTypeInfo info = {
            sizeof(MIRAGE_ObjectClass),
            NULL,   /* base_init */
            NULL,   /* base_finalize */
            __mirage_object_class_init,   /* class_init */
            NULL,   /* class_finalize */
            NULL,   /* class_data */
            sizeof(MIRAGE_Object),
            0,      /* n_preallocs */
            NULL    /* instance_init */
        };
        
        type = g_type_register_static(G_TYPE_OBJECT, "MIRAGE_Object", &info, 0);
    }
    
    return type;
}
