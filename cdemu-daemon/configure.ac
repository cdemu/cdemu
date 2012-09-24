# Process this file with autoconf to produce a configure script.

# Require autoconf >= 2.59
AC_PREREQ([2.59])

# cdemu-daemon versioning:
m4_define([cdemud_major_version], [1])
m4_define([cdemud_minor_version], [6])
m4_define([cdemud_micro_version], [0])
m4_define([cdemud_version], [cdemud_major_version.cdemud_minor_version.cdemud_micro_version])

# Project init
AC_INIT([cdemu-daemon],[cdemud_version],[http://cdemu.sf.net])
AC_CONFIG_SRCDIR(src/main.c)
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
AC_CONFIG_HEADERS([config.h])

# D-BUS interface version (change whenever interface changes)
AC_DEFINE(DAEMON_INTERFACE_VERSION, 4, [D-BUS interface version])

AM_MAINTAINER_MODE

# Programs
AC_PROG_CC_STDC
AC_PROG_INSTALL
AC_PROG_MAKE_SET

# Standard C headers
AC_HEADER_STDC

# Mandatory libraries
PKG_CHECK_MODULES(glib, glib-2.0 >= 2.24)
PKG_CHECK_MODULES(gobject, gobject-2.0 >= 2.24)
PKG_CHECK_MODULES(gmodule, gmodule-2.0 >= 2.24)
PKG_CHECK_MODULES(gthread, gthread-2.0 >= 2.24)
PKG_CHECK_MODULES(gio, gio-2.0 >= 2.26)
PKG_CHECK_MODULES(libao, ao >= 0.8.0,)
PKG_CHECK_MODULES(libmirage, libmirage >= 1.6.0)

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
    -Wall -Wextra -Wshadow \
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

# System-bus service
AC_ARG_ENABLE(system-bus-service,
    [  --enable-system-bus-service    Install CDEmu daemon as D-Bus system bus service],
    [case "${enableval}" in
        yes) system_bus_service=true ;;
        no)  system_bus_service=false ;;
        *) AC_MSG_ERROR(bad value ${enableval} for --enable-system-bus-service) ;;
    esac], [system_bus_service=true])
AM_CONDITIONAL(SYSTEM_BUS_SERVICE, test x$system_bus_service = xtrue)

# Session-bus service
AC_ARG_ENABLE(session-bus-service,
    [  --enable-session-bus-service    Install CDEmu daemon as D-Bus session bus service],
    [case "${enableval}" in
        yes) session_bus_service=true ;;
        no)  session_bus_service=false ;;
        *) AC_MSG_ERROR(bad value ${enableval} for --enable-session-bus-service) ;;
    esac], [session_bus_service=true])
AM_CONDITIONAL(SESSION_BUS_SERVICE, test x$session_bus_service = xtrue)

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
    session/Makefile
    src/Makefile
    system/Makefile
])

AC_OUTPUT

# Print summary
echo "
$PACKAGE_NAME $VERSION configure summary:

C Compiler: ${CC}
CFLAGS: ${CFLAGS}
Prefix: ${prefix}
Install as system bus service: ${system_bus_service}
dbus-1 system.d dir: ${DBUS_SYS_DIR}
Install as session bus service: ${session_bus_service}

"
