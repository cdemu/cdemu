/*
 *  MIRAGE Image Analyzer: Generic dump functions
 *  Copyright (C) 2007-2009 Rok Mandeljc
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

#ifndef __IMAGE_ANALYZER_DUMP_H__
#define __IMAGE_ANALYZER_DUMP_H__


G_BEGIN_DECLS

typedef struct {
    gint value;
    gchar *name;
} DUMP_Value;

#define VAL(x) { x, #x }

gchar *__dump_value (gint val, DUMP_Value *values, gint num_values);
gchar *__dump_flags (gint val, DUMP_Value *values, gint num_values);

G_END_DECLS

#endif /* __IMAGE_ANALYZER_DUMP_H__ */
