/*
 *  libMirage: CUE image parser: Disc object
 *  Copyright (C) 2006-2008 Rok Mandeljc
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

#ifndef __IMAGE_CUE_DISC_H__
#define __IMAGE_CUE_DISC_H__


G_BEGIN_DECLS

#define MIRAGE_TYPE_DISC_CUE            (mirage_disc_cue_get_type(global_module))
#define MIRAGE_DISC_CUE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), MIRAGE_TYPE_DISC_CUE, MIRAGE_Disc_CUE))
#define MIRAGE_DISC_CUE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), MIRAGE_TYPE_DISC_CUE, MIRAGE_Disc_CUEClass))
#define MIRAGE_IS_DISC_CUE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), MIRAGE_TYPE_DISC_CUE))
#define MIRAGE_IS_DISC_CUE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), MIRAGE_TYPE_DISC_CUE))
#define MIRAGE_DISC_CUE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), MIRAGE_TYPE_DISC_CUE, MIRAGE_Disc_CUEClass))

typedef struct {
    MIRAGE_Disc parent;
} MIRAGE_Disc_CUE;

typedef struct {
    MIRAGE_DiscClass parent;
} MIRAGE_Disc_CUEClass;

/* Used by MIRAGE_TYPE_DISC_CUE */
GType mirage_disc_cue_get_type (GTypeModule *module);

G_END_DECLS

#endif /* __IMAGE_CUE_DISC_H__ */
