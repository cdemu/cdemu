/*
 *  libMirage: Library version
 *  Copyright (C) 2008-2012 Rok Mandeljc
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
 * SECTION: mirage-version
 * @title: Version
 * @short_description: Version information variables.
 * @include: mirage-version.h
 *
 * libMirage provides version information, primarily useful in configure
 * checks for builds that have a configure script. It can also be used
 * in applications when displaying underlying system version information.
 *
 * Additionaly, libtool version is also exposed, which is primarily
 * intended to be used in libMirage's plugin system.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"

/**
 * mirage_version_major:
 *
 * The major version number of the libMirage library. (e.g. in libMirage version
 * 1.2.5 this is 1.)
 *
 * This variable is in the library, so it represents the version of libMirage
 * library you have linked against (contrary to %MIRAGE_VERSION_MAJOR macro,
 * which represents the version of the libMirage headers you have included).
 */
const guint mirage_version_major = MIRAGE_VERSION_MAJOR;

/**
 * mirage_version_minor:
 *
 * The minor version number of the libMirage library. (e.g. in libMirage version
 * 1.2.5 this is 2.)
 *
 * This variable is in the library, so it represents the version of libMirage
 * library you have linked against (contrary to %MIRAGE_VERSION_MINOR macro,
 * which represents the version of the libMirage headers you have included).
 */
const guint mirage_version_minor = MIRAGE_VERSION_MINOR;

/**
 * mirage_version_micro:
 *
 * The micro version number of the libMirage library. (e.g. in libMirage version
 * 1.2.5 this is 5.)
 *
 * This variable is in the library, so it represents the version of libMirage
 * library you have linked against (contrary to %MIRAGE_VERSION_MICRO macro,
 * which represents the version of the libMirage headers you have included).
 */
const guint mirage_version_micro = MIRAGE_VERSION_MICRO;


/**
 * mirage_version_long:
 *
 * The long version string of the libMirage library.
 *
 * This variable is in the library, so it represents the version of libMirage
 * library you have linked against (contrary to %MIRAGE_VERSION_LONG macro,
 * which represents the version of the libMirage headers you have included).
 */
const gchar *mirage_version_long = MIRAGE_VERSION_LONG;

/**
 * mirage_version_short:
 *
 * The long version string of the libMirage library.
 *
 * This variable is in the library, so it represents the version of libMirage
 * library you have linked against (contrary to %MIRAGE_VERSION_SHORT macro,
 * which represents the version of the libMirage headers you have included).
 */
const gchar *mirage_version_short = MIRAGE_VERSION_SHORT;


/**
 * mirage_soversion_major:
 *
 * The major component of <ulink url="semver.org">semantic version</ulink> 
 * of the libMirage library. It is intended to be used in libMirage's plugin 
 * system and should not be of much interest to application developers.
 *
 * This variable is in the library, so it represents the version of libMirage
 * library you have linked against (contrary to %MIRAGE_SOVERSION_MAJOR macro,
 * which represents the version of the libMirage headers you have included).
 */
const guint mirage_soversion_major = MIRAGE_SOVERSION_MAJOR;

/**
 * mirage_soversion_minor:
 *
 * The minor component of <ulink url="semver.org">semantic version</ulink> 
 * of the libMirage library. It is intended to be used in libMirage's plugin 
 * system and should not be of much interest to application developers.
 *
 * This variable is in the library, so it represents the version of libMirage
 * library you have linked against (contrary to %MIRAGE_SOVERSION_MINOR macro,
 * which represents the version of the libMirage headers you have included).
 */
const guint mirage_soversion_minor = MIRAGE_SOVERSION_MINOR;

/**
 * mirage_soversion_patch:
 *
 * The patch component of <ulink url="semver.org">semantic version</ulink> 
 * of the libMirage library. It is intended to be used in libMirage's plugin 
 * system and should not be of much interest to application developers.
 *
 * This variable is in the library, so it represents the version of libMirage
 * library you have linked against (contrary to %MIRAGE_SOVERSION_PATCH macro,
 * which represents the version of the libMirage headers you have included).
 */
const guint mirage_soversion_patch = MIRAGE_SOVERSION_PATCH;
