# Process this file with autoconf to produce a configure script.

# cdemu-client versioning:
m4_define([cdemu_major_version], [1])
m4_define([cdemu_minor_version], [1])
m4_define([cdemu_micro_version], [0])
m4_define([cdemu_version], [cdemu_major_version.cdemu_minor_version.cdemu_micro_version])

# Project init
AC_INIT(cdemu-client, [cdemu_version], http://cdemu.sf.net)
AC_CONFIG_SRCDIR(src/cdemu)
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)

AM_MAINTAINER_MODE

# Intltool
AC_PROG_INTLTOOL([0.21])

# Python
AM_PATH_PYTHON

# Internationalization
ALL_LINGUAS="sl no"

GETTEXT_PACKAGE=cdemu-client
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [The gettext package])

AM_GLIB_GNU_GETTEXT

# Output files
AC_OUTPUT([
    Makefile
    man/Makefile
    po/Makefile.in
    src/globals.py
    src/Makefile
])
