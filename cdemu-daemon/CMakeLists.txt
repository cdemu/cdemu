cmake_minimum_required (VERSION 2.8.5)

# Project name
project (cdemu-daemon C)

# Versioning
set (CDEMU_DAEMON_VERSION 3.0.0)
set (CDEMU_DAEMON_INTERFACE_VERSION_MAJOR 7)
set (CDEMU_DAEMON_INTERFACE_VERSION_MINOR 0)

# CMake modules
include (GNUInstallDirs)

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
include_directories (SYSTEM ${GLIB_INCLUDE_DIRS})
include_directories (SYSTEM ${AO_INCLUDE_DIRS})

# Link directories
link_directories (SYSTEM ${LIBMIRAGE_LIBRARY_DIRS})
link_directories (SYSTEM ${GLIB_LIBRARY_DIRS})
link_directories (SYSTEM ${AO_LIBRARY_DIRS})


# *** cdemu-daemon ***
set (cdemu-daemon_SOURCES
    src/audio.c
    src/daemon.c
    src/daemon-dbus.c
    src/device.c
    src/device-commands.c
    src/device-delay.c
    src/device-features.c
    src/device-kernel-io.c
    src/device-load.c
    src/device-mapping.c
    src/device-mode-pages.c
    src/device-recording.c
    src/error.c
    src/main.c
)

add_executable (cdemu-daemon ${cdemu-daemon_SOURCES})
target_link_libraries (cdemu-daemon ${LIBMIRAGE_LIBRARIES} ${GLIB_LIBRARIES} ${AO_LIBRARIES})

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

# *** Configuration summary ***
message(STATUS "")
message(STATUS "*** CDEmu daemon v.${CDEMU_DAEMON_VERSION} configuration summary ***")
message(STATUS "Compiler and build info:")
message(STATUS " C compiler: " ${CMAKE_C_COMPILER} ${CMAKE_C_COMPILER_ARG1})
message(STATUS " C flags (Release): " ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_RELEASE})
message(STATUS " C flags (Debug): " ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_DEBUG})
message(STATUS " build type: " ${CMAKE_BUILD_TYPE})
message(STATUS "Installation:")
message(STATUS " install prefix: " ${CMAKE_INSTALL_PREFIX})
message(STATUS "Options:")
message(STATUS " system bus service: " ${SYSTEM_BUS_SERVICE})
message(STATUS " session bus service: " ${SESSION_BUS_SERVICE})
message(STATUS "")
