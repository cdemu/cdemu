# Process this file with autoconf to produce a configure script.

# Project init
AC_INIT(mirage-image-analyzer, [1.2.0], rok.mandeljc@email.si)
AC_CONFIG_SRCDIR(src/main.c)
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
AM_CONFIG_HEADER(config.h)

AM_MAINTAINER_MODE

# Programs
AC_PROG_CC_STDC
AC_PROG_INSTALL
AC_PROG_MAKE_SET

# Standard C headers
AC_HEADER_STDC

# Mandatory libraries
PKG_CHECK_MODULES(glib, glib-2.0 >= 2.6)
PKG_CHECK_MODULES(gobject, gobject-2.0 >= 2.6)
PKG_CHECK_MODULES(gtk, gtk+-2.0 >= 2.6)
PKG_CHECK_MODULES(libmirage, libmirage >= 1.1.0)
PKG_CHECK_MODULES(gtkextra, gtkextra-2.0 >= 2.0.0)

# Compile with more warnings
AC_ARG_ENABLE(more-warnings,
[  --enable-more-warnings  Maximum compiler warnings],
set_more_warnings="$enableval",[
    set_more_warnings=no
])
AC_MSG_CHECKING(for more warnings, including -Werror)
if test "$GCC" = "yes" -a "$set_more_warnings" != "no"; then
    AC_MSG_RESULT(yes)
    CFLAGS="\
    -Wall \
    -Wchar-subscripts -Wmissing-declarations -Wmissing-prototypes \
    -Wnested-externs -Wpointer-arith \
    -Wcast-align -Wsign-compare \
    $CFLAGS"

    for option in -Wno-strict-aliasing -Wno-sign-compare; do
        SAVE_CFLAGS="$CFLAGS"
        CFLAGS="$CFLAGS $option"
        AC_MSG_CHECKING([whether gcc understands $option])
        AC_TRY_COMPILE([], [],
            has_option=yes,
            has_option=no,)
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
    src/Makefile
])

AC_OUTPUT
