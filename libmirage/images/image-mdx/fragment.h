/*
 *  libMirage: MDX image: custom fragment for handling data compression/encryption
 *  Copyright (C) 2026 Rok Mandeljc
 *
 *  Based on reverse-engineering effort from:
 *  https://github.com/Marisa-Chan/mdsx
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

#pragma once

#include "mirage/config.h"
#include <mirage/mirage.h>

#include <glib/gi18n-lib.h>

G_BEGIN_DECLS


#define MIRAGE_TYPE_FRAGMENT_MDX            (mirage_fragment_mdx_get_type())
#define MIRAGE_FRAGMENT_MDX(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_FRAGMENT_MDX, MirageFragmentMdx))
#define MIRAGE_FRAGMENT_MDX_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_FRAGMENT_MDX, MirageFragmentMdxClass))
#define MIRAGE_IS_FRAGMENT_MDX(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_FRAGMENT_MDX))
#define MIRAGE_IS_FRAGMENT_MDX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_FRAGMENT_MDX))
#define MIRAGE_FRAGMENT_MDX_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_FRAGMENT_MDX, MirageFragmentMdxClass))

typedef struct _MirageFragmentMdx         MirageFragmentMdx;
typedef struct _MirageFragmentMdxClass    MirageFragmentMdxClass;
typedef struct _MirageFragmentMdxPrivate  MirageFragmentMdxPrivate;

struct _MirageFragmentMdx
{
    MirageFragment parent_instance;

    /*< private >*/
    MirageFragmentMdxPrivate *priv;
};

struct _MirageFragmentMdxClass
{
    MirageFragmentClass parent_class;
};

/* Used by MIRAGE_TYPE_FRAGMENT_MDX */
GType mirage_fragment_mdx_get_type (void);

gboolean mirage_fragment_mdx_setup (
    MirageFragmentMdx *self,
    gint length,
    MirageStream *data_stream,
    guint64 data_offset,
    gint main_size,
    gint main_format,
    gint subchannel_size,
    gint subchannel_format,
    const MDX_EncryptionHeader *encryption_header,
    GError **error
);


G_END_DECLS
