/*
 *  MIRAGE Image Analyzer: Sector analysis window
 *  Copyright (C) 2011 Rok Mandeljc
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

#ifndef __IMAGE_ANALYZER_SECTOR_ANALYSIS_H__
#define __IMAGE_ANALYZER_SECTOR_ANALYSIS_H__


G_BEGIN_DECLS


#define IMAGE_ANALYZER_TYPE_SECTOR_ANALYSIS            (image_analyzer_sector_analysis_get_type())
#define IMAGE_ANALYZER_SECTOR_ANALYSIS(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), IMAGE_ANALYZER_TYPE_SECTOR_ANALYSIS, IMAGE_ANALYZER_SectorAnalysis))
#define IMAGE_ANALYZER_SECTOR_ANALYSIS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), IMAGE_ANALYZER_TYPE_SECTOR_ANALYSIS, IMAGE_ANALYZER_SectorAnalysisClass))
#define IMAGE_ANALYZER_IS_SECTOR_ANALYSIS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), IMAGE_ANALYZER_TYPE_SECTOR_ANALYSIS))
#define IMAGE_ANALYZER_IS_SECTOR_ANALYSIS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), IMAGE_ANALYZER_TYPE_SECTOR_ANALYSIS))
#define IMAGE_ANALYZER_SECTOR_ANALYSIS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), IMAGE_ANALYZER_TYPE_SECTOR_ANALYSIS, IMAGE_ANALYZER_SectorAnalysisClass))

typedef struct {
    GtkWindow parent;
} IMAGE_ANALYZER_SectorAnalysis;

typedef struct {
    GtkWindowClass parent;
} IMAGE_ANALYZER_SectorAnalysisClass;

/* Used by IMAGE_ANALYZER_TYPE_SECTOR_ANALYSIS */
GType image_analyzer_sector_analysis_get_type (void);


G_END_DECLS

#endif /* __IMAGE_ANALYZER_SECTOR_ANALYSIS_H__ */
