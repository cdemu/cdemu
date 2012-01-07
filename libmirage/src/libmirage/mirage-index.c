/*
 *  libMirage: Index object
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
#define MIRAGE_INDEX_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_INDEX, MIRAGE_IndexPrivate))

struct _MIRAGE_IndexPrivate
{   
    gint number;  /* Index' number */
    gint address; /* Index' start address (relative to track start) */
};


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
/**
 * mirage_index_set_number:
 * @self: a #MIRAGE_Index
 * @number: index number
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets index' index number.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_index_set_number (MIRAGE_Index *self, gint number, GError **error G_GNUC_UNUSED)
{
    /* Set number */
    self->priv->number = number;
    return TRUE;
}

/**
 * mirage_index_get_number:
 * @self: a #MIRAGE_Index
 * @number: location to store index number
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves index' index number.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_index_get_number (MIRAGE_Index *self, gint *number, GError **error)
{
    MIRAGE_CHECK_ARG(number);
    /* Return number */
    *number = self->priv->number;
    return TRUE;
}

    
/**
 * mirage_index_set_address:
 * @self: a #MIRAGE_Index
 * @address: address
 * @error: location to store error, or %NULL
 *
 * <para>
 * Sets index' start address. The @address is given in sectors.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_index_set_address (MIRAGE_Index *self, gint address, GError **error G_GNUC_UNUSED)
{
    /* Set address */
    self->priv->address = address;
    return TRUE;
}

/**
 * mirage_index_get_address:
 * @self: a #MIRAGE_Index
 * @address: location to store address
 * @error: location to store error, or %NULL
 *
 * <para>
 * Retrieves index' start adddress. The @address is given in sectors.
 * </para>
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean mirage_index_get_address (MIRAGE_Index *self, gint *address, GError **error)
{
    MIRAGE_CHECK_ARG(address);
    /* Return address */
    *address = self->priv->address;
    return TRUE;
}


/**********************************************************************\
 *                             Object init                            * 
\**********************************************************************/
G_DEFINE_TYPE(MIRAGE_Index, mirage_index, MIRAGE_TYPE_OBJECT);


static void mirage_index_init (MIRAGE_Index *self)
{
    self->priv = MIRAGE_INDEX_GET_PRIVATE(self);

    self->priv->number = 0;
    self->priv->address = 0;
}

static void mirage_index_class_init (MIRAGE_IndexClass *klass)
{
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MIRAGE_IndexPrivate));
}
