/*
 *  libMirage: Contextual interface
 *  Copyright (C) 2012 Rok Mandeljc
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

#ifndef __MIRAGE_CONTEXTUAL_H__
#define __MIRAGE_CONTEXTUAL_H__

/* Forward declarations */
typedef struct _MirageDisc MirageDisc;
typedef struct _MirageFragment MirageFragment;


G_BEGIN_DECLS


/**********************************************************************\
 *                     MirageContextual interface                     *
\**********************************************************************/
#define MIRAGE_TYPE_CONTEXTUAL                 (mirage_contextual_get_type())
#define MIRAGE_CONTEXTUAL(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_CONTEXTUAL, MirageContextual))
#define MIRAGE_IS_CONTEXTUAL(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_CONTEXTUAL))
#define MIRAGE_CONTEXTUAL_GET_INTERFACE(inst)  (G_TYPE_INSTANCE_GET_INTERFACE((inst), MIRAGE_TYPE_CONTEXTUAL, MirageContextualInterface))

/**
 * MirageContextual:
 *
 * <para>
 * Dummy interface structure.
 * </para>
 **/
typedef struct _MirageContextual             MirageContextual;
typedef struct _MirageContextualInterface    MirageContextualInterface;

struct _MirageContextualInterface
{
    GTypeInterface parent_iface;

    /* Interface methods */
    void (*set_context) (MirageContextual *self, MirageContext *context);
    MirageContext *(*get_context) (MirageContextual *self);
};

/* Used by MIRAGE_TYPE_CONTEXTUAL */
GType mirage_contextual_get_type (void);

void mirage_contextual_set_context (MirageContextual *self, MirageContext *context);
MirageContext *mirage_contextual_get_context (MirageContextual *self);

void mirage_contextual_debug_message (MirageContextual *self, gint level, gchar *format, ...);
void mirage_contextual_debug_messagev (MirageContextual *self, gint level, gchar *format, va_list args);

GVariant *mirage_contextual_get_option (MirageContextual *self, const gchar *name);

gchar *mirage_contextual_obtain_password (MirageContextual *self, GError **error);

MirageFragment *mirage_contextual_create_fragment (MirageContextual *self, GType fragment_interface, GInputStream *stream, GError **error);
GInputStream *mirage_contextual_create_file_stream (MirageContextual *self, const gchar *filename, GError **error);
const gchar *mirage_contextual_get_file_stream_filename (MirageContextual *self, GInputStream *stream);

G_END_DECLS

#endif /* __MIRAGE_CONTEXTUAL_H__ */
