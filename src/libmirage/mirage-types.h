/*
 *  libMirage: Declaration of libMirage types
 *  Copyright (C) 2009-2009 Henrik Stokseth
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
 
#ifndef __MIRAGE_TYPES_H__
#define __MIRAGE_TYPES_H__

G_BEGIN_DECLS

/**
 * mirage_b_offset:
 *
 * <para>
 * Mirage type specifying an offset given as a byte address.
 * </para>
 **/
typedef goffset mirage_b_offset;

/**
 * mirage_s_offset:
 *
 * <para>
 * Mirage type specifying an offset given as a sector address.
 * </para>
 **/
typedef gint mirage_s_offset;

/**
 * mirage_b_size:
 *
 * <para>
 * Mirage type specifying a size given as a quantity of bytes.
 * </para>
 **/
typedef gsize mirage_b_size;

/**
 * mirage_s_size:
 *
 * <para>
 * Mirage type specifying a size given as a quantity of sectors.
 * </para>
 **/
typedef gint mirage_s_size;

G_END_DECLS

#endif /* __MIRAGE_TYPES_H__ */

