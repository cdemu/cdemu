# Process this file with autoconf to produce a configure script.

# cdemu-daemon versioning:
m4_define([cdemud_major_version], [1])
m4_define([cdemud_minor_version], [1])
m4_define([cdemud_micro_version], [0])
m4_define([cdemud_version], [cdemud_major_version.cdemud_minor_version.cdemud_micro_version])


# Project init
AC_INIT(cdemu-daemon, [cdemud_version], http://cdemu.sf.net)
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
PKG_CHECK_MODULES(gmodule, gmodule-2.0 >= 2.6)
PKG_CHECK_MODULES(dbus, dbus-1 >= 0.60 dbus-glib-1 >= 0.61)
PKG_CHECK_MODULES(libdaemon, libdaemon >= 0.10)
PKG_CHECK_MODULES(libmirage, libmirage >= 1.1.0)

# Optional ALSA check
AC_ARG_ENABLE(alsa,
        AS_HELP_STRING([--disable-alsa],[Disable ALSA audio backend]),
        [case "${enableval}" in
                yes) ALSA_BACKEND=yes ;;
                no)  ALSA_BACKEND=no ;;
                *) AC_MSG_ERROR(bad value ${enableval} for --enable-alsa) ;;
        esac],
        [ALSA_BACKEND=yes])

if test "x$ALSA_BACKEND" = "xyes" ; then
    PKG_CHECK_MODULES(alsa, [ alsa >= 1.0.0 ], AC_DEFINE(ALSA_BACKEND, 1, [Build ALSA audio backend?]), ALSA_BACKEND=no)
fi
AM_CONDITIONAL(ALSA_BACKEND, test "x$ALSA_BACKEND" = "xyes")

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

# D-BUS system dir
AC_ARG_WITH(dbus-system-dir, AS_HELP_STRING([--with-dbus-system-dir=<dir>], [Path to D-BUS system.d directory]))

if ! test -z "$with_dbus_system_dir" ; then
    DBUS_SYS_DIR="$with_dbus_system_dir"
else
    DBUS_SYS_DIR="${sysconfdir}/dbus-1/system.d"
fi
AC_SUBST(DBUS_SYS_DIR)

# Output files
AC_CONFIG_FILES([
    Makefile
    man/Makefile
    src/Makefile
])

AC_OUTPUT

# Print summary
echo "
$PACKAGE_NAME $VERSION configure summary:

 Prefix:                    ${prefix}
 dbus-1 system.d dir:       ${DBUS_SYS_DIR}
 C Compiler:                ${CC}
 CFLAGS:                    ${CFLAGS}
 Enable ALSA audio backed:  ${ALSA_BACKEND}
"
