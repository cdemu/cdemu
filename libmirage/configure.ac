# Process this file with autoconf to produce a configure script.

# Require autoconf >= 2.59
AC_PREREQ(2.59)

# libMirage release and libtool versioning:
m4_define([libmirage_major_version], [1])
m4_define([libmirage_minor_version], [4])
m4_define([libmirage_micro_version], [0])
m4_define([libmirage_version], [libmirage_major_version.libmirage_minor_version.libmirage_micro_version])
# Increment if the interface has additions, changes, removals.
m4_define([libmirage_current], [4])
# Increment any time the source changes; set to 0 if you increment CURRENT
m4_define([libmirage_revision], [0])
# Increment if any interfaces have been added; set to 0 if any interfaces have
# been removed. Removal has precedence over adding, so set to 0 if both happened.
m4_define([libmirage_age], [1])


# Project init
AC_INIT(libmirage, [libmirage_version], http://cdemu.sf.net)
AC_CONFIG_SRCDIR(src/libmirage/mirage.c)
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)
AM_CONFIG_HEADER(config.h)

# Versioning
AC_SUBST(MIRAGE_MAJOR_VERSION, libmirage_major_version)
AC_SUBST(MIRAGE_MINOR_VERSION, libmirage_minor_version)
AC_SUBST(MIRAGE_MICRO_VERSION, libmirage_micro_version)

AC_SUBST(MIRAGE_LT_CURRENT, libmirage_current)
AC_SUBST(MIRAGE_LT_REVISION, libmirage_revision)
AC_SUBST(MIRAGE_LT_AGE, libmirage_age)

AC_SUBST(MIRAGE_VERSION_SHORT,
    ["libmirage_major_version.libmirage_minor_version"])

AC_SUBST(MIRAGE_VERSION_LONG,
    ["libmirage_version"])

# Libtool versioning
AC_SUBST(LT_VERSION_INFO,
    ["-version-info libmirage_current:libmirage_revision:libmirage_age"])

AM_MAINTAINER_MODE

# Programs
AC_PROG_CC_STDC
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_PROG_MAKE_SET

# Standard C headers
AC_HEADER_STDC

# We need large file support
AC_SYS_LARGEFILE

# Gtk-doc check
GTK_DOC_CHECK([1.4])

# Libraries check
PKG_CHECK_MODULES(glib, glib-2.0 >= 2.6)
PKG_CHECK_MODULES(gobject, gobject-2.0 >= 2.6)
PKG_CHECK_MODULES(gmodule, gmodule-2.0 >= 2.6)
PKG_CHECK_MODULES(sndfile, sndfile >= 1.0.0)

# Zlib check
AC_CHECK_HEADERS([zlib.h],, AC_MSG_ERROR("*** zlib headers not found ***"))
AC_CHECK_LIB([z], [inflate], [zlib_LIBS="-lz"], AC_MSG_ERROR("*** zlib library not found ***"))
AC_SUBST(zlib_LIBS)

# Compile with more warnings
AC_ARG_ENABLE(more-warnings,
[  --enable-more-warnings  maximum compiler warnings],
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

# Plugin directory
AC_ARG_WITH(plugin-dir, AS_HELP_STRING([--with-plugin-dir=<dir>], [Path to libMirage plugin directory]))
if ! test -z "$with_plugin_dir" ; then
    MIRAGE_PLUGIN_DIR="$with_plugin_dir"
else
    MIRAGE_PLUGIN_DIR="${libdir}/libmirage-$MIRAGE_VERSION_SHORT"
fi
AC_SUBST(MIRAGE_PLUGIN_DIR)

# Output files
AC_CONFIG_FILES([
    Makefile
    libmirage.pc
    docs/Makefile
    docs/reference/Makefile
    docs/reference/libmirage/Makefile
    docs/reference/libmirage/version.xml
    src/Makefile
    src/fragments/Makefile
    src/fragments/fragment-binary/Makefile
    src/fragments/fragment-sndfile/Makefile
    src/fragments/fragment-null/Makefile
    src/libmirage/Makefile
    src/libmirage/mirage-version.h
    src/parsers/Makefile
    src/parsers/image-b6t/Makefile
    src/parsers/image-c2d/Makefile
    src/parsers/image-ccd/Makefile
    src/parsers/image-cdi/Makefile
    src/parsers/image-cif/Makefile
    src/parsers/image-cue/Makefile
    src/parsers/image-daa/Makefile
    src/parsers/image-daa/lzma-sdk/Makefile
    src/parsers/image-iso/Makefile
    src/parsers/image-mds/Makefile
    src/parsers/image-nrg/Makefile
    src/parsers/image-toc/Makefile
    src/parsers/image-xcdroast/Makefile])

AC_OUTPUT
