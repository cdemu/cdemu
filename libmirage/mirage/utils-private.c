/*
 *  libMirage: private utility functions and helpers
 *  Copyright (C) 2025-2026 Rok Mandeljc
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

#include "mirage/utils-private.h"


/**********************************************************************\
 *                           Miscellaneous                            *
\**********************************************************************/
/**
 * mirage_signal_handlers_disconnect_by_func:
 * @instance: (in) (type GObject.Object): the instance to remove handlers from
 * @func: (in) (scope call) (closure user_data): the C closure callback of the handlers
 * @user_data: (in) (nullable): the closure data of the handlers' closures
 *
 * Replacement for g_signal_handlers_disconnect_by_func() that accepts
 * function pointer instead of object pointer, and type-puns the given
 * function pointer to object pointer via union to avoid warnings when
 * compiling with pedantic compiler settings.
 *
 * Returns: the number of handlers that matched.
 **/
guint mirage_signal_handlers_disconnect_by_func (gpointer instance, GCallback func, gpointer user_data)
{
    /* Type-pun the given function pointer to object pointer (required by
     * g_signal_handlers_disconnect_*()) via union to avoid warnings in
     * -pedantic mode. */
    union {
        GCallback function_ptr;
        gpointer object_ptr;
    } func_alias;
    func_alias.function_ptr = func;

    return g_signal_handlers_disconnect_by_func(instance, func_alias.object_ptr, user_data);
}
