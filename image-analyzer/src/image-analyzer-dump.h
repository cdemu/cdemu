/*
 *  Image Analyzer: Generic dump functions
 *  Copyright (C) 2007-2012 Rok Mandeljc
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

typedef struct _DumpValue DumpValue;

struct _DumpValue
{
    gint value;
    gchar *name;
};


#define VAL(x) { x, #x }

gchar *dump_value (gint val, const DumpValue *values, gint num_values);
gchar *dump_flags (gint val, const DumpValue *values, gint num_values);

gchar *dump_track_flags (gint track_flags);
gchar *dump_track_mode (gint track_mode);
gchar *dump_session_type (gint session_type);
gchar *dump_medium_type (gint medium_type);
gchar *dump_binary_fragment_main_format (gint format);
gchar *dump_binary_fragment_subchannel_format (gint format);


G_END_DECLS

#endif /* __IMAGE_ANALYZER_DUMP_H__ */
