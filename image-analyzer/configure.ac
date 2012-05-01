# Process this file with autoconf to produce a configure script.

# Require autoconf >= 2.59
AC_PREREQ([2.59])

# Project init
AC_INIT([image-analyzer],[1.6.0],[http://cdemu.sf.net])
AC_CONFIG_SRCDIR(src/main.c)
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
AC_CONFIG_HEADERS([config.h])

AM_MAINTAINER_MODE

# Programs
AC_PROG_CC_STDC
AC_PROG_INSTALL
AC_PROG_MAKE_SET

# Standard C headers
AC_HEADER_STDC

# Mandatory libraries
PKG_CHECK_MODULES(libmirage, libmirage >= 1.6.0)
PKG_CHECK_MODULES(libxml, libxml-2.0 >= 2.0.0)
PKG_CHECK_MODULES(glib, glib-2.0 >= 2.24)
PKG_CHECK_MODULES(gobject, gobject-2.0 >= 2.24)

# Gtk2 vs. Gtk3 switch
AC_ARG_ENABLE(gtk3,
    AC_HELP_STRING([--enable-gtk3], [compile using Gtk3]),
    [case "${enableval}" in
         yes) ENABLE_GTK3=yes ;;
         no) ENABLE_GTK3=no ;;
         *) AC_MSG_ERROR(bad value ${enableval} for --enable-gtk3) ;;
     esac], 
    [ENABLE_GTK3=check])

AS_IF([test "x$ENABLE_GTK3" = xcheck], [PKG_CHECK_EXISTS(gtk+-3.0, [ENABLE_GTK3=yes], [ENABLE_GTK3=no])])

AS_IF([test "x$ENABLE_GTK3" = "xyes"],
    [PKG_CHECK_MODULES(gtk, gtk+-3.0 >= 3.0)
     AC_DEFINE([GTK3_ENABLED], [1], [Define to 1 if you have Gtk 3])],
    [PKG_CHECK_MODULES(gtk, gtk+-2.0 >= 2.20)])


# Compile with more warnings
AC_ARG_ENABLE(more-warnings,
[  --enable-more-warnings  Maximum compiler warnings],
set_more_warnings="$enableval",[
    set_more_warnings=no
])
AC_MSG_CHECKING(for more warnings)
if test "$GCC" = "yes" -a "$set_more_warnings" != "no"; then
    AC_MSG_RESULT(yes)
    CFLAGS="\
    -Wall -Wextra \
    -Wmissing-declarations -Wmissing-prototypes \
    -Wnested-externs -Wpointer-arith -Wcast-align \
    -DG_DISABLE_DEPRECATED \
    $CFLAGS"

    for option in -Wno-strict-aliasing -Wno-sign-compare; do
        SAVE_CFLAGS="$CFLAGS"
        CFLAGS="$CFLAGS $option"
        AC_MSG_CHECKING([whether gcc understands $option])
        AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[]])],[has_option=yes],[has_option=no])
        if test $has_option = no; then
            CFLAGS="$SAVE_CFLAGS"
        fi
        AC_MSG_RESULT($has_option)
        unset has_option
        unset SAVE_CFLAGS
    done
    unset option
else
    AC_MSG_RESULT(no)
fi


# Output files
AC_CONFIG_FILES([
    Makefile
    data/Makefile
    src/Makefile
])

AC_OUTPUT
