# Process this file with autoconf to produce a configure script.

# Require autoconf >= 2.59
AC_PREREQ([2.59])

# gcdemu versioning:
m4_define([gcdemu_major_version], [1])
m4_define([gcdemu_minor_version], [6])
m4_define([gcdemu_micro_version], [0])
m4_define([gcdemu_version], [gcdemu_major_version.gcdemu_minor_version.gcdemu_micro_version])

# Project init
AC_INIT([gcdemu],[gcdemu_version],[http://cdemu.sf.net])
AC_CONFIG_SRCDIR(src/gcdemu)
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)

AM_MAINTAINER_MODE

# Intltool
AC_PROG_INTLTOOL([0.21])

# GSettings
GLIB_GSETTINGS

# Internationalization
ALL_LINGUAS="de fr no sl pl zh_CN"

GETTEXT_PACKAGE=gcdemu
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [The gettext package])

AM_GLIB_GNU_GETTEXT

# Output files
AC_CONFIG_FILES([
    Makefile
    data/Makefile
    po/Makefile.in
    src/Makefile
])

AC_OUTPUT
