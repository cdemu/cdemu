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
#define MIRAGE_INDEX_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), MIRAGE_TYPE_INDEX, MirageIndexPrivate))

struct _MirageIndexPrivate
{
    gint number;  /* Index' number */
    gint address; /* Index' start address (relative to track start) */
};


/**********************************************************************\
 *                             Public API                             *
\**********************************************************************/
/**
 * mirage_index_set_number:
 * @self: a #MirageIndex
 * @number: (in): index number
 *
 * <para>
 * Sets index' index number.
 * </para>
 **/
void mirage_index_set_number (MirageIndex *self, gint number)
{
    /* Set number */
    self->priv->number = number;
}

/**
 * mirage_index_get_number:
 * @self: a #MirageIndex
 *
 * <para>
 * Retrieves index' index number.
 * </para>
 *
 * Returns: index number
 **/
gint mirage_index_get_number (MirageIndex *self)
{
    /* Return number */
    return self->priv->number;
}


/**
 * mirage_index_set_address:
 * @self: a #MirageIndex
 * @address: (in): address
 *
 * <para>
 * Sets index' start address. The @address is given in sectors.
 * </para>
 **/
void mirage_index_set_address (MirageIndex *self, gint address)
{
    /* Set address */
    self->priv->address = address;
}

/**
 * mirage_index_get_address:
 * @self: a #MirageIndex
 *
 * <para>
 * Retrieves index' start adddress. The @address is given in sectors.
 * </para>
 *
 * Returns: address
 **/
gint mirage_index_get_address (MirageIndex *self)
{
    /* Return address */
    return self->priv->address;
}


/**********************************************************************\
 *                             Object init                            *
\**********************************************************************/
G_DEFINE_TYPE(MirageIndex, mirage_index, MIRAGE_TYPE_OBJECT);


static void mirage_index_init (MirageIndex *self)
{
    self->priv = MIRAGE_INDEX_GET_PRIVATE(self);

    self->priv->number = 0;
    self->priv->address = 0;
}

static void mirage_index_class_init (MirageIndexClass *klass)
{
    /* Register private structure */
    g_type_class_add_private(klass, sizeof(MirageIndexPrivate));
}
