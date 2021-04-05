/*
 *  libMirage: error
 *  Copyright (C) 2006-2014 Rok Mandeljc
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

/**
 * SECTION: mirage-error
 * @title: Error
 * @short_description: Error reporting facilities.
 * @include: mirage-error.h
 *
 * libMirage uses GLib's #GError system for reporting errors. Many
 * functions provided by libMirage's objects take an optional @error
 * parameter. If @error is not %NULL, in case that such function should
 * fail, a #GError error is set into it using g_set_error().
 * Unless coming from underlying GLib's systems, the returned error
 * code is one of #MirageErrorCode.
 */

#include "mirage/config.h"
#include "mirage/mirage.h"

#include <glib/gi18n-lib.h>


/**
 * mirage_error_quark:
 *
 * Registers an error quark for libMirage if necessary.
 *
 * Return value: The error quark used for libMirage errors.
 */
GQuark mirage_error_quark (void)
{
    static GQuark q = 0;

    if (q == 0) {
        q = g_quark_from_static_string("mirage-error");
    }

    return q;
}


#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }
GType mirage_error_get_type (void)
{
    static GType type = 0;
    if (type == 0) {
        static const GEnumValue values[] = {
            ENUM_ENTRY(MIRAGE_ERROR_LIBRARY_ERROR, "LibraryError"),
            ENUM_ENTRY(MIRAGE_ERROR_PARSER_ERROR, "ParserError"),
            ENUM_ENTRY(MIRAGE_ERROR_FRAGMENT_ERROR, "FragmentError"),
            ENUM_ENTRY(MIRAGE_ERROR_DISC_ERROR, "DiscError"),
            ENUM_ENTRY(MIRAGE_ERROR_LANGUAGE_ERROR, "LanguageError"),
            ENUM_ENTRY(MIRAGE_ERROR_SECTOR_ERROR, "SectorError"),
            ENUM_ENTRY(MIRAGE_ERROR_SESSION_ERROR, "SessionError"),
            ENUM_ENTRY(MIRAGE_ERROR_TRACK_ERROR, "TrackError"),
            ENUM_ENTRY(MIRAGE_ERROR_STREAM_ERROR, "StreamError"),
            ENUM_ENTRY(MIRAGE_ERROR_IMAGE_FILE_ERROR, "ImageFileError"),
            ENUM_ENTRY(MIRAGE_ERROR_DATA_FILE_ERROR, "DataFileError"),
            ENUM_ENTRY(MIRAGE_ERROR_CANNOT_HANDLE, "CannotHandle"),
            ENUM_ENTRY(MIRAGE_ERROR_ENCRYPTED_IMAGE, "EncryptedImage"),
            ENUM_ENTRY(MIRAGE_ERROR_WRITER_ERROR, "WriterError"),
            { 0, 0, 0 }
        };

        type = g_enum_register_static("MirageError", values);
    }

    return type;
}
