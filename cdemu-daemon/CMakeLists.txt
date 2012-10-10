cmake_minimum_required (VERSION 2.8.5)

# Project name and version
project (cdemu-daemon C)
set (cdemu-daemon_NAME "cdemu-daemon")
set (cdemu-daemon_VERSION "1.6.0")
set (cdemu-daemon_INTERFACE_VERSION "4")

# Search in the `cmake' directory for additional CMake modules.
list (APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

include (CheckCSourceCompiles)
include (CheckCCompilerFlag)
include (GNUInstallDirs)

include (Utilities)
include (FileList)

# Configuration
configure_file (
    ${PROJECT_SOURCE_DIR}/config.h.in
    ${PROJECT_BINARY_DIR}/config.h
)
configure_file (
    ${PROJECT_SOURCE_DIR}/session/net.sf.cdemu.CDEmuDaemon.service.in
    ${PROJECT_BINARY_DIR}/session/net.sf.cdemu.CDEmuDaemon.service
)
configure_file (
    ${PROJECT_SOURCE_DIR}/system/net.sf.cdemu.CDEmuDaemon.service.in
    ${PROJECT_BINARY_DIR}/system/net.sf.cdemu.CDEmuDaemon.service
)
include_directories (${PROJECT_BINARY_DIR})

if (NOT CMAKE_BUILD_TYPE)
    set (CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
        "Choose the type of build: None Debug Release RelWithDebInfo MinSizeRel."
        FORCE)
endif ()

#NOTE: Compilation bugs out on using signals and -std=c99
list (APPEND cdemu-daemon_CFLAGS -std=gnu99)
list (APPEND cdemu-daemon_DEFS HAVE_CONFIG_H)

if (STRICT_WARNINGS AND CMAKE_COMPILER_IS_GNUCC)
    list (APPEND cdemu-daemon_CFLAGS -Wall -Wextra -Wshadow -Wmissing-declarations -Wmissing-prototypes -Wnested-externs -Wpointer-arith -Wcast-align)
    list (APPEND cdemu-daemon_DEFS G_DISABLE_DEPRECATED)
    check_c_compiler_flag (-Wno-strict-aliasing CC_HAVE_NSA)
    check_c_compiler_flag (-Wno-sign-compare CC_HAVE_NSC)
    if (CC_HAVE_NSA)
        list (APPEND cdemu-daemon_CFLAGS -Wno-strict-aliasing)
    endif ()
    if (CC_HAVE_NSC)
        list (APPEND cdemu-daemon_CFLAGS -Wno-sign-compare)
    endif ()
endif ()

option (SYSTEM_BUS_SERVICE "Install CDEmu daemon as D-Bus system bus service" off)
option (SESSION_BUS_SERVICE "Install CDEmu daemon as D-Bus session bus service" on)
option (STRICT_WARNINGS "Enable more warnings" off)

# Tests

find_package (PkgConfig 0.16 REQUIRED)

pkg_check_modules (LibMirage REQUIRED libmirage>=${cdemu-daemon_VERSION})
pkg_check_modules (LibGlib REQUIRED glib-2.0>=2.24 gobject-2.0>=2.24 gmodule-2.0>=2.24 gthread-2.0>=2.24 gio-2.0>=2.26)
pkg_check_modules (LibAO REQUIRED ao>=0.8.0)

list (APPEND cdemu-daemon_CFLAGS ${LibMirage_CFLAGS})
list (APPEND cdemu-daemon_CFLAGS ${LibGlib_CFLAGS})
list (APPEND cdemu-daemon_CFLAGS ${LibAO_CFLAGS})

list (APPEND cdemu-daemon_LDFLAGS ${LibMirage_LDFLAGS})
list (APPEND cdemu-daemon_LDFLAGS ${LibGlib_LDFLAGS})
list (APPEND cdemu-daemon_LDFLAGS ${LibAO_LDFLAGS})

# Source files
include_directories (${PROJECT_SOURCE_DIR}/src)

add_executable (cdemu-daemon ${cdemu-daemon_SOURCES})

target_link_libraries (cdemu-daemon ${LibMirage_LIBRARIES} ${LibGlib_LIBRARIES} ${LibAO_LIBRARIES})

to_list_spaces (cdemu-daemon_CFLAGS cdemu-daemon_S_CFLAGS)
to_list_spaces (cdemu-daemon_LDFLAGS cdemu-daemon_S_LDFLAGS)

set_target_properties (cdemu-daemon PROPERTIES 
    COMPILE_FLAGS ${cdemu-daemon_S_CFLAGS}
    COMPILE_DEFINITIONS ${cdemu-daemon_DEFS}
    LINK_FLAGS ${cdemu-daemon_S_LDFLAGS}
)

# Installation

install (
    TARGETS cdemu-daemon 
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install (
    FILES man/cdemu-daemon.8 
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man8
)

if (SESSION_BUS_SERVICE)
    install (
        PROGRAMS session/cdemu-daemon-session.sh
        DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}
    )
    install (
        FILES ${PROJECT_BINARY_DIR}/session/net.sf.cdemu.CDEmuDaemon.service
        DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services
    )
endif ()

if (SYSTEM_BUS_SERVICE)
    install (
        PROGRAMS system/cdemu-daemon-system.sh
        DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}
    )
    install (
        FILES ${PROJECT_BINARY_DIR}/system/net.sf.cdemu.CDEmuDaemon.service
        DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/system-services/
    )
    install (
        FILES system/cdemu-daemon-dbus.conf
        DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/etc/dbus-1/system.d
    )
endif ()

