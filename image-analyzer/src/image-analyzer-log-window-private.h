/*
 *  Image Analyzer: Parser log window - private
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

#ifndef __IMAGE_ANALYZER_LOG_WINDOW_PRIVATE_H__
#define __IMAGE_ANALYZER_LOG_WINDOW_PRIVATE_H__

#define IMAGE_ANALYZER_LOG_WINDOW_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), IMAGE_ANALYZER_TYPE_LOG_WINDOW, IMAGE_ANALYZER_LogWindowPrivate))

struct _IMAGE_ANALYZER_LogWindowPrivate
{
    /* Text entry */
    GtkWidget *text_view;
};

#endif /* __IMAGE_ANALYZER_LOG_WINDOW_PRIVATE_H__ */
