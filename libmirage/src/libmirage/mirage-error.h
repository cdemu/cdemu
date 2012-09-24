/*
 *  libMirage: Error handling
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

#ifndef __MIRAGE_ERROR_H__
#define __MIRAGE_ERROR_H__


G_BEGIN_DECLS

#define MIRAGE_ERROR (mirage_error_quark ())
#define MIRAGE_TYPE_ERROR (mirage_error_get_type ())

GQuark mirage_error_quark (void);
GType  mirage_error_get_type (void);


/**
 * MirageErrorCodes:
 * @MIRAGE_ERROR_LIBRARY_ERROR: error in core libMirage code
 * @MIRAGE_ERROR_PARSER_ERROR: error in parser code
 * @MIRAGE_ERROR_FRAGMENT_ERROR: error in fragment code
 * @MIRAGE_ERROR_DISC_ERROR: error in disc code
 * @MIRAGE_ERROR_LANGUAGE_ERROR: error in language/CD-TEXT code
 * @MIRAGE_ERROR_SECTOR_ERROR: error in sector code
 * @MIRAGE_ERROR_SESSION_ERROR: error in session code
 * @MIRAGE_ERROR_TRACK_ERROR: error in track code
 * @MIRAGE_ERROR_STREAM_ERROR: error in stream code
 * @MIRAGE_ERROR_IMAGE_FILE_ERROR: error related to image file
 * @MIRAGE_ERROR_DATA_FILE_ERROR: error related to data file
 * @MIRAGE_ERROR_CANNOT_HANDLE: parser/fragment/file filter cannot handle given file
 * @MIRAGE_ERROR_ENCRYPTED_IMAGE: image is encrypted and password needs to be provided
 *
 * Error codes for libMirage library.
 **/
typedef enum
{
    MIRAGE_ERROR_LIBRARY_ERROR,
    MIRAGE_ERROR_PARSER_ERROR,
    MIRAGE_ERROR_FRAGMENT_ERROR,
    MIRAGE_ERROR_DISC_ERROR,
    MIRAGE_ERROR_LANGUAGE_ERROR,
    MIRAGE_ERROR_SECTOR_ERROR,
    MIRAGE_ERROR_SESSION_ERROR,
    MIRAGE_ERROR_TRACK_ERROR,
    MIRAGE_ERROR_STREAM_ERROR,
    MIRAGE_ERROR_IMAGE_FILE_ERROR,
    MIRAGE_ERROR_DATA_FILE_ERROR,
    MIRAGE_ERROR_CANNOT_HANDLE,
    MIRAGE_ERROR_ENCRYPTED_IMAGE,
} MirageErrorCodes;

G_END_DECLS

#endif /* __MIRAGE_ERROR_H__ */
