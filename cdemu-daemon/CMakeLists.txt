cmake_minimum_required(VERSION 3.16)
project(cdemu-daemon VERSION 3.2.7 LANGUAGES C)

option(ENABLE_LOGIND_SLEEP_HANDLER "Enable support for systemd-logind sleep/hibernation signal handler." ON)
option(PEDANTIC_MODE "Enable -pedantic flag on gcc compiler" OFF)

# Additional CMake modules
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# Versioning
set(CDEMU_DAEMON_INTERFACE_VERSION_MAJOR 7)
set(CDEMU_DAEMON_INTERFACE_VERSION_MINOR 0)

# CMake modules
include(GNUInstallDirs)

# Dependencies
find_package(PkgConfig 0.16 REQUIRED)
find_package(IntlTool 0.21 REQUIRED)
find_package(Gettext 0.15 REQUIRED)

if(ENABLE_LOGIND_SLEEP_HANDLER)
    # Acquiring sleep inhibitor lock via D-Bus requires GDBus bindings
    # generated with support for GLib >= 2.64
    set(MIN_GLIB_VERSION 2.64)
else()
    set(MIN_GLIB_VERSION 2.38)
endif()

pkg_check_modules(LIBMIRAGE REQUIRED libmirage>=3.2.0 IMPORTED_TARGET)
pkg_check_modules(GLIB REQUIRED glib-2.0>=${MIN_GLIB_VERSION} gobject-2.0>=${MIN_GLIB_VERSION} gthread-2.0>=${MIN_GLIB_VERSION} gio-2.0>=${MIN_GLIB_VERSION} IMPORTED_TARGET)
pkg_check_modules(AO REQUIRED ao>=0.8.0 IMPORTED_TARGET)

# Global definitions
set(CMAKE_C_STANDARD 99) # Enable C99

if(CMAKE_COMPILER_IS_GNUCC)
    # Enable additional warnings
    add_definitions(-Wall -Wextra -Wshadow -Wmissing-declarations -Wmissing-prototypes -Wnested-externs -Wpointer-arith -Wcast-align)
    if(PEDANTIC_MODE)
        add_definitions(-pedantic)
    endif()
endif()

# Config file
configure_file(${PROJECT_SOURCE_DIR}/src/config.h.in ${PROJECT_BINARY_DIR}/config.h)

set(CMAKE_INCLUDE_CURRENT_DIR TRUE)

# *** cdemu-daemon ***
set(cdemu-daemon_SOURCES
    src/audio.c
    src/daemon.c
    src/daemon-dbus.c
    src/device.c
    src/device-commands.c
    src/device-delay.c
    src/device-disc-structures.c
    src/device-features.c
    src/device-kernel-io.c
    src/device-load.c
    src/device-mapping.c
    src/device-mode-pages.c
    src/device-recording.c
    src/error.c
    src/main.c
)

# GDBus bindings for org.freedesktop.login1
if(ENABLE_LOGIND_SLEEP_HANDLER)
    # gdbus-codegen
    pkg_get_variable(GDBUS_CODEGEN gio-2.0 gdbus_codegen)
    if(GDBUS_CODEGEN STREQUAL "")
        message(FATAL_ERROR "Could not gdbus-codegen executable!")
    else()
        message(STATUS "Found gdbus-codegen: ${GDBUS_CODEGEN}")
    endif()

    add_custom_command(
        OUTPUT ${PROJECT_BINARY_DIR}/freedesktop-login-manager.h ${PROJECT_BINARY_DIR}/freedesktop-login-manager.c
        COMMAND ${GDBUS_CODEGEN}
        --interface-prefix org.freedesktop.login1 --c-generate-autocleanup all --c-namespace FreedesktopLogin --glib-min-required ${MIN_GLIB_VERSION}
        --header --pragma-once --output ${PROJECT_BINARY_DIR}/freedesktop-login-manager.h ${PROJECT_SOURCE_DIR}/src/org.freedesktop.login1.xml
        COMMAND ${GDBUS_CODEGEN}
        --interface-prefix org.freedesktop.login1 --c-generate-autocleanup all --c-namespace FreedesktopLogin --glib-min-required ${MIN_GLIB_VERSION}
        --body --pragma-once --output ${PROJECT_BINARY_DIR}/freedesktop-login-manager.c ${PROJECT_SOURCE_DIR}/src/org.freedesktop.login1.xml
        DEPENDS ${PROJECT_SOURCE_DIR}/src/org.freedesktop.login1.xml
        COMMENT "Generate GDBus bindings for org.freedesktop.login1"
    )

    list(APPEND cdemu-daemon_SOURCES
        # auto-generated
        ${PROJECT_BINARY_DIR}/freedesktop-login-manager.h
        ${PROJECT_BINARY_DIR}/freedesktop-login-manager.c
    )
endif()

add_executable(cdemu-daemon ${cdemu-daemon_SOURCES})

target_link_libraries(cdemu-daemon PRIVATE PkgConfig::GLIB)
target_link_libraries(cdemu-daemon PRIVATE PkgConfig::LIBMIRAGE)
target_link_libraries(cdemu-daemon PRIVATE PkgConfig::AO)

# Installation
install(TARGETS cdemu-daemon DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES man/cdemu-daemon.8 DESTINATION ${CMAKE_INSTALL_MANDIR}/man8)

# Translations; we use the version provided by our FindIntlTool.cmake
# instead of the gettext version... Note: first two arguments are mandatory,
# the rest is an optional list of languages to process. If no languages
# are given, all POs are processed.
intltool_process_po_files(${PROJECT_SOURCE_DIR}/po ${PROJECT_NAME})

# *** Configuration summary ***
message(STATUS "")
message(STATUS "*** CDEmu daemon v.${PROJECT_VERSION} configuration summary ***")
message(STATUS "Installation:")
message(STATUS " install prefix: " ${CMAKE_INSTALL_PREFIX})
message(STATUS "Options:")
message(STATUS " pedantic mode: " ${PEDANTIC_MODE})
message(STATUS " systemd-logind sleep/hibernation signal handler support: " ${ENABLE_LOGIND_SLEEP_HANDLER})
message(STATUS "")
