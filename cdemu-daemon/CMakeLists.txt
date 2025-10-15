cmake_minimum_required(VERSION 3.16)
project(cdemu-daemon VERSION 3.2.7 LANGUAGES C)

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

pkg_check_modules(LIBMIRAGE REQUIRED libmirage>=3.2.0 IMPORTED_TARGET)
pkg_check_modules(GLIB REQUIRED glib-2.0>=2.38 gobject-2.0>=2.38 gthread-2.0>=2.38 gio-2.0>=2.38 IMPORTED_TARGET)
pkg_check_modules(AO REQUIRED ao>=0.8.0 IMPORTED_TARGET)

# Global definitions
set(CMAKE_C_STANDARD 99) # Enable C99

if(CMAKE_COMPILER_IS_GNUCC)
    # Enable additional warnings
    add_definitions(-Wall -Wextra -Wshadow -Wmissing-declarations -Wmissing-prototypes -Wnested-externs -Wpointer-arith -Wcast-align)
    add_definitions(-Wno-cast-function-type)
endif()

# Config file
configure_file(${PROJECT_SOURCE_DIR}/config.h.in ${PROJECT_BINARY_DIR}/config.h)

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
    src/logind.c
    src/sleep.c
    src/main.c
)

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
message(STATUS "Compiler and build info:")
message(STATUS " C compiler: " ${CMAKE_C_COMPILER} ${CMAKE_C_COMPILER_ARG1})
message(STATUS " C flags (Release): " ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_RELEASE})
message(STATUS " C flags (Debug): " ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_DEBUG})
message(STATUS " build type: " ${CMAKE_BUILD_TYPE})
message(STATUS "Installation:")
message(STATUS " install prefix: " ${CMAKE_INSTALL_PREFIX})
message(STATUS "")
