# Process this file with autoconf to produce a configure script.

# gcdemu versioning:
m4_define([gcdemu_major_version], [1])
m4_define([gcdemu_minor_version], [2])
m4_define([gcdemu_micro_version], [0])
m4_define([gcdemu_version], [gcdemu_major_version.gcdemu_minor_version.gcdemu_micro_version])

# Project init
AC_INIT(gcdemu, [gcdemu_version], http://cdemu.sf.net)
AC_CONFIG_SRCDIR(src/gcdemu)
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)

AM_MAINTAINER_MODE

# GNOME docs
GNOME_DOC_INIT

# GConf
AC_PATH_PROG(GCONFTOOL, gconftool-2)
AM_GCONF_SOURCE_2

# Intltool
AC_PROG_INTLTOOL([0.21])

# Python
AM_PATH_PYTHON

# PyGTK
PKG_CHECK_MODULES(pygtk, pygtk-2.0 >= 2.6)
AC_SUBST(pygtk_CFLAGS)
AC_SUBST(pygtk_LIBS)

# Internationalization
ALL_LINGUAS="de no sl pl zh_CN"

GETTEXT_PACKAGE=gcdemu
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [The gettext package])

AM_GLIB_GNU_GETTEXT

# Output files
AC_OUTPUT([
    Makefile
    docs/Makefile
    pixmaps/Makefile
    po/Makefile.in
    servers/Makefile
    src/globals.py
    src/Makefile
])
