cmake_minimum_required (VERSION 2.8.5)

# Project name
project (cdemu-daemon C)

# Versioning
set (CDEMU_DAEMON_VERSION 2.1.0)
set (CDEMU_DAEMON_INTERFACE_VERSION 5)

# Additional CMake modules.
list (APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

include (GNUInstallDirs)

include (Utilities)
include (FileList)

# Options
option (SYSTEM_BUS_SERVICE "Install CDEmu daemon as D-Bus system bus service" off)
option (SESSION_BUS_SERVICE "Install CDEmu daemon as D-Bus session bus service" on)

# If install prefix is /usr, override the sysconf dir to be /etc instead of /usr/etc
if ("${CMAKE_INSTALL_PREFIX}" STREQUAL "/usr")
    set(CMAKE_INSTALL_SYSCONFDIR "/etc" CACHE PATH "read-only single-machine data (/etc)" FORCE)
endif()

# Dependencies
find_package (PkgConfig 0.16 REQUIRED)

pkg_check_modules (LIBMIRAGE REQUIRED libmirage>=${CDEMU_DAEMON_VERSION})
pkg_check_modules (GLIB REQUIRED glib-2.0>=2.24 gobject-2.0>=2.24 gmodule-2.0>=2.24 gthread-2.0>=2.24 gio-2.0>=2.26)
pkg_check_modules (AO REQUIRED ao>=0.8.0)

# Auto-generated files
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

# Global definitions
add_definitions (-std=c99) # We're compiling using C99
add_definitions (-D_POSIX_C_SOURCE) # Enable POSIX extensions, which we need for signal handling
add_definitions (-DHAVE_CONFIG_H)

if (CMAKE_COMPILER_IS_GNUCC)
    add_definitions (-Wall -Wextra -Wshadow -Wmissing-declarations -Wmissing-prototypes -Wnested-externs -Wpointer-arith -Wcast-align)
endif ()


# Include directories
include_directories (${PROJECT_BINARY_DIR})
include_directories (SYSTEM ${LIBMIRAGE_INCLUDE_DIRS})

# *** cdemu-daemon ***
add_executable (cdemu-daemon ${cdemu-daemon_SOURCES})
target_link_libraries (cdemu-daemon ${LIBMIRAGE_LIBRARIES} ${GLIB_LIBRARIES} ${AO_LIBRARIES})

list (APPEND cdemu_daemon_CFLAGS ${LIBMIRAGE_CFLAGS} ${GLIB_CFLAGS} ${AO_CFLAGS})
list (APPEND cdemu_daemon_LDFLAGS ${LIBMIRAGE_LDFLAGS} ${GLIB_LDFLAGS} ${AO_LDFLAGS})

to_list_spaces (cdemu_daemon_CFLAGS cdemu_daemon_CFLAGS_STR)
to_list_spaces (cdemu_daemon_LDFLAGS cdemu_daemon_LDFLAGS_STR)

set_target_properties (cdemu-daemon PROPERTIES
    COMPILE_FLAGS ${cdemu_daemon_CFLAGS_STR}
    LINK_FLAGS ${cdemu_daemon_LDFLAGS_STR}
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
        DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/dbus-1/system.d
    )
endif ()

