/*
 *  libMirage: Fragment object
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

/**
 * SECTION: mirage-fragment
 * @title: MirageFragment
 * @short_description: Base object for fragment implementations.
 * @see_also: #MirageAudioFragment, #MirageDataFragment
 * @include: mirage-fragment.h
 *
 * #MirageFragment object is a base object for fragment implementations.
 * It provides functions that are used by image parsers to provide access
 * to data in image files.
 *
 * #MirageFragment provides two virtual functions: mirage_fragment_get_info(),
 * mirage_fragment_can_handle_data_format(). These must be implemented
 * by fragment implementations which derive from #MirageFragment object.
 *
 * Every fragment implementation needs to implement one of the following
 * fragment interfaces: #MirageDataFragment or #MirageAudioFragment.
 * Which interface a fragment implementation implements depends on the
 * way the implementation handles data.
 *
 * While fragment implementations are usually obtained from context
 * using mirage_context_create_fragment(), the default implementation
 * can be obtained using g_object_new() and MIRAGE_TYPE_FRAGMENT. The
 * default fragment object implementation provides so called "NULL"
 * fragment, which returns no data, and is used by libMirage to represent
 * data in tracks' pregaps and postgaps.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"

#define __debug__ "Fragment"


/**********************************************************************\
 *                          Private structure                         *
\**********************************************************************/
#define MIRAGE_FRAGMENT_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_FRAGMENT, MirageFragmentPrivate))

struct _MirageFragmentPrivate
{
    MirageFragmentInfo info;

    gint address; /* Address (relative to track start) */
    gint length; /* Length, in sectors */
};


/**********************************************************************\
 *                          Private functions                         *
\**********************************************************************/
static void mirage_fragment_commit_topdown_change (MirageFragment *self G_GNUC_UNUSED)
{
    /* Nothing to do here */
}

static void mirage_fragment_commit_bottomup_change (MirageFragment *self)
{
    /* Signal fragment change */
    g_signal_emit_by_name(self, "layout-changed", NULL);
}


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
/**
 * mirage_fragment_generate_info:
 * @self: a #MirageFragment
 * @id: (in): fragment ID
 * @name: (in): fragment name
 *
 * Generates fragment information from the input fields. It is intended as a function
 * for creating fragment information in fragment implementations.
 */
void mirage_fragment_generate_info (MirageFragment *self, const gchar *id, const gchar *name)
{
    g_snprintf(self->priv->info.id, sizeof(self->priv->info.id), "%s", id);
    g_snprintf(self->priv->info.name, sizeof(self->priv->info.name), "%s", name);
}

/**
 * mirage_fragment_get_info:
 * @self: a #MirageFragment
 *
 * Retrieves fragment information.
 *
 * Returns: (transfer none): a pointer to fragment information structure. The
 * structure belongs to object and should not be modified.
 */
const MirageFragmentInfo *mirage_fragment_get_info (MirageFragment *self)
{
    return &self->priv->info;
}


/**
 * mirage_fragment_can_handle_data_format:
 * @self: a #MirageFragment
 * @stream: (in): data stream
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Checks whether fragment can handle data stored in @stream.
 *
 * Returns: %TRUE if fragment can handle data file, %FALSE if not
 */
gboolean mirage_fragment_can_handle_data_format (MirageFragment *self, GInputStream *stream, GError **error)
{
    /* Provided by implementation */
    return MIRAGE_FRAGMENT_GET_CLASS(self)->can_handle_data_format(self, stream, error);
}


/**
 * mirage_fragment_set_address:
 * @self: a #MirageFragment
 * @address: (in): start address
 *
 * Sets fragment's start address. The @address is given in sectors.
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * <note>
 * Causes top-down change.
 * </note>
 */
void mirage_fragment_set_address (MirageFragment *self, gint address)
{
    /* Set address */
    self->priv->address = address;
    /* Top-down change */
    mirage_fragment_commit_topdown_change(self);
}

/**
 * mirage_fragment_get_address:
 * @self: a #MirageFragment
 *
 * Retrieves fragment's start address. The @address is given in sectors.
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: start address
 */
gint mirage_fragment_get_address (MirageFragment *self)
{
    /* Return address */
    return self->priv->address;
}

/**
 * mirage_fragment_set_length:
 * @self: a #MirageFragment
 * @length: (in): length
 *
 * Sets fragment's length. The @length is given in sectors.
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * <note>
 * Causes bottom-up change.
 * </note>
 */
void mirage_fragment_set_length (MirageFragment *self, gint length)
{
    /* Set length */
    self->priv->length = length;
    /* Bottom-up change */
    mirage_fragment_commit_bottomup_change(self);
}

/**
 * mirage_fragment_get_length:
 * @self: a #MirageFragment
 *
 * Retrieves fragment's length. The returned @length is given in sectors.
 *
 * <note>
 * Intended for internal use only.
 * </note>
 *
 * Returns: length
 */
gint mirage_fragment_get_length (MirageFragment *self)
{
    /* Return length */
    return self->priv->length;
}


/**
 * mirage_fragment_use_the_rest_of_file:
 * @self: a #MirageFragment
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Uses the rest of data file. It automatically calculates and sets fragment's
 * length.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_fragment_use_the_rest_of_file (MirageFragment *self, GError **error)
{
    /* Provided by implementation */
    return MIRAGE_FRAGMENT_GET_CLASS(self)->use_the_rest_of_file(self, error);
}


/**
 * mirage_fragment_read_main_data:
 * @self: a #MirageFragment
 * @address: (in): address
 * @buffer: (out) (allow-none) (array length=length): location to store pointer to buffer with read data, or %NULL
 * @length: (out): location to store read data length
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Reads main channel data for sector at fragment-relative @address (given
 * in sectors). The buffer for reading data into is allocated by function
 * and should be freed using g_free() when no longer needed. The pointer
 * to buffer is stored into @buffer and the length of read data is stored into
 * @length.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_fragment_read_main_data (MirageFragment *self, gint address, guint8 **buffer, gint *length, GError **error)
{
    /* Provided by implementation */
    return MIRAGE_FRAGMENT_GET_CLASS(self)->read_main_data(self, address, buffer, length, error);
}

/**
 * mirage_fragment_read_subchannel_data:
 * @self: a #MirageFragment
 * @address: (in): address
 * @buffer: (out) (allow-none) (array length=length): location to store pointer to buffer with read data, or %NULL
 * @length: (out): location to store read data length
 * @error: (out) (allow-none): location to store error, or %NULL
 *
 * Reads subchannel data for sector at fragment-relative @address (given
 * in sectors). The buffer for reading data into is allocated by function
 * and should be freed using g_free() when no longer needed. The pointer
 * to buffer is stored into @buffer and the length of read data is stored into
 * @length.
 *
 * Returns: %TRUE on success, %FALSE on failure
 */
gboolean mirage_fragment_read_subchannel_data (MirageFragment *self, gint address, guint8 **buffer, gint *length, GError **error)
{
    /* Provided by implementation */
    return MIRAGE_FRAGMENT_GET_CLASS(self)->read_subchannel_data(self, address, buffer, length, error);
}


/**********************************************************************\
 *                Default implementation: NULL fragment               *
\**********************************************************************/
static gboolean mirage_fragment_null_can_handle_data_format (MirageFragment *self G_GNUC_UNUSED, GInputStream *stream G_GNUC_UNUSED, GError **error)
{
    /* This should never get called, anyway */
    g_set_error(error, MIRAGE_ERROR, MIRAGE_ERROR_CANNOT_HANDLE, "Fragment cannot handle given data!");
    return FALSE;
}

static gboolean mirage_fragment_null_use_the_rest_of_file (MirageFragment *self G_GNUC_UNUSED, GError **error G_GNUC_UNUSED)
{
    /* No file, nothing to use */
    return TRUE;
}

static gboolean mirage_fragment_null_read_main_data (MirageFragment *self, gint address G_GNUC_UNUSED, guint8 **buffer, gint *length, GError **error G_GNUC_UNUSED)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: no data in NULL fragment\n", __debug__);

    /* Nothing to read */
    *length = 0;
    if (buffer) {
        *buffer = NULL;
    }

    return TRUE;
}

static gboolean mirage_fragment_null_read_subchannel_data (MirageFragment *self, gint address G_GNUC_UNUSED, guint8 **buffer, gint *length, GError **error G_GNUC_UNUSED)
{
    MIRAGE_DEBUG(self, MIRAGE_DEBUG_FRAGMENT, "%s: no data in NULL fragment\n", __debug__);

    /* Nothing to read */
    *length = 0;
    if (buffer) {
        *buffer = NULL;
    }

    return TRUE;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE(MirageFragment, mirage_fragment, MIRAGE_TYPE_OBJECT);


static void mirage_fragment_init (MirageFragment *self)
{
    self->priv = MIRAGE_FRAGMENT_GET_PRIVATE(self);

    /* Default fragment implementation is NULL fragment */
    mirage_fragment_generate_info(self,
        "FRAGMENT-NULL",
        "NULL Fragment"
    );
}

static void mirage_fragment_class_init (MirageFragmentClass *klass)
{
    /* Default implementation: NULL fragment */
    klass->can_handle_data_format = mirage_fragment_null_can_handle_data_format;
    klass->use_the_rest_of_file = mirage_fragment_null_use_the_rest_of_file;
    klass->read_main_data = mirage_fragment_null_read_main_data;
    klass->read_subchannel_data = mirage_fragment_null_read_subchannel_data;

    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageFragmentPrivate));

    /* Signals */
    /**
     * MirageFragment::layout-changed:
     * @fragment: a #MirageFragment
     *
     * Emitted when a layout of #MirageFragment changed in a way that causes a bottom-up change.
     */
    klass->signal_layout_changed = g_signal_new("layout-changed", G_OBJECT_CLASS_TYPE(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, NULL);
}
