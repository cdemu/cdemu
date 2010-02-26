/*
 *  libMirage: Library version
 *  Copyright (C) 2008-2010 Rok Mandeljc
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mirage.h"

/**
 * mirage_major_version:
 *
 * <para>
 * The major version number of the libMirage library. (e.g. in libMirage version
 * 1.2.5 this is 1.)
 * </para>
 *
 * <para>
 * This variable is in the library, so it represents the version of libMirage
 * library you have linked against (contrary to %MIRAGE_MAJOR_VERSION macro, 
 * which represents the version of the libMirage headers you have included).
 * </para>
 **/
const guint mirage_major_version = MIRAGE_MAJOR_VERSION;

/**
 * mirage_minor_version:
 *
 * <para>
 * The minor version number of the libMirage library. (e.g. in libMirage version
 * 1.2.5 this is 2.)
 * </para>
 *
 * <para>
 * This variable is in the library, so it represents the version of libMirage
 * library you have linked against (contrary to %MIRAGE_MINOR_VERSION macro, 
 * which represents the version of the libMirage headers you have included).
 * </para>
 **/
const guint mirage_minor_version = MIRAGE_MINOR_VERSION;

/**
 * mirage_micro_version:
 *
 * <para>
 * The micro version number of the libMirage library. (e.g. in libMirage version
 * 1.2.5 this is 5.)
 * </para>
 *
 * <para>
 * This variable is in the library, so it represents the version of libMirage
 * library you have linked against (contrary to %MIRAGE_MICRO_VERSION macro, 
 * which represents the version of the libMirage headers you have included).
 * </para>
 **/
const guint mirage_micro_version = MIRAGE_MICRO_VERSION;


/**
 * mirage_version_long:
 *
 * <para>
 * The long version string of the libMirage library.
 * </para>
 *
 * <para>
 * This variable is in the library, so it represents the version of libMirage
 * library you have linked against (contrary to %MIRAGE_VERSION_LONG macro, 
 * which represents the version of the libMirage headers you have included).
 * </para>
 **/
const gchar *mirage_version_long = MIRAGE_VERSION_LONG;

/**
 * mirage_version_short:
 *
 * <para>
 * The long version string of the libMirage library.
 * </para>
 *
 * <para>
 * This variable is in the library, so it represents the version of libMirage
 * library you have linked against (contrary to %MIRAGE_VERSION_SHORT macro, 
 * which represents the version of the libMirage headers you have included).
 * </para>
 **/
const gchar *mirage_version_short = MIRAGE_VERSION_SHORT;


/**
 * mirage_lt_current:
 *
 * <para>
 * The current component of libtool version of the libMirage library. It is 
 * intended to be used in libMirage's plugin system and should not be of much
 * interest to application developers.
 * </para>
 *
 * <para>
 * This variable is in the library, so it represents the version of libMirage
 * library you have linked against (contrary to %MIRAGE_LT_CURRENT macro, 
 * which represents the version of the libMirage headers you have included).
 * </para>
 **/
const guint mirage_lt_current = MIRAGE_LT_CURRENT;

/**
 * mirage_lt_revision:
 *
 * <para>
 * The revision component of libtool version of the libMirage library. It is 
 * intended to be used in libMirage's plugin system and should not be of much
 * interest to application developers.
 * </para>
 *
 * <para>
 * This variable is in the library, so it represents the version of libMirage
 * library you have linked against (contrary to %MIRAGE_LT_REVISION macro, 
 * which represents the version of the libMirage headers you have included).
 * </para>
 **/
const guint mirage_lt_revision = MIRAGE_LT_REVISION;

/**
 * mirage_lt_age:
 *
 * <para>
 * The age component of libtool version of the libMirage library. It is 
 * intended to be used in libMirage's plugin system and should not be of much
 * interest to application developers.
 * </para>
 *
 * <para>
 * This variable is in the library, so it represents the version of libMirage
 * library you have linked against (contrary to %MIRAGE_LT_AGE macro, 
 * which represents the version of the libMirage headers you have included).
 * </para>
 **/
const guint mirage_lt_age = MIRAGE_LT_AGE;
