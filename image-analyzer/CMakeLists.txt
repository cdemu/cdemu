cmake_minimum_required (VERSION 2.8.5)

# Project name
project (image-analyzer C)

# Versioning
set (IMAGE_ANALYZER_VERSION 2.1.1)

# Additional CMake modules.
list (APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

include (GNUInstallDirs)

# Options
option (POST_INSTALL_HOOKS "Run post-install hooks" on)

# Dependencies
find_package (PkgConfig 0.16 REQUIRED)

pkg_check_modules (LIBMIRAGE REQUIRED libmirage>=${IMAGE_ANALYZER_VERSION})
pkg_check_modules (GLIB REQUIRED glib-2.0>=2.24 gobject-2.0>=2.24)
pkg_check_modules (LIBXML REQUIRED libxml-2.0>=2.0.0)
pkg_check_modules (GTK REQUIRED gtk+-3.0>=3.0)

if (POST_INSTALL_HOOKS)
    find_program (UPDATE_DESKTOP_DATABASE_EXECUTABLE NAMES update-desktop-database)
    mark_as_advanced (UPDATE_DESKTOP_DATABASE_EXECUTABLE)
endif ()

# AUto-generated files
configure_file (
    ${PROJECT_SOURCE_DIR}/config.h.in
    ${PROJECT_BINARY_DIR}/config.h
)

# Global definitions
add_definitions (-std=c99) # We're compiling using C99
add_definitions (-DHAVE_CONFIG_H)

if (CMAKE_COMPILER_IS_GNUCC)
    add_definitions (-Wall -Wextra -Wshadow -Wmissing-declarations -Wmissing-prototypes -Wnested-externs -Wpointer-arith -Wcast-align)
    add_definitions (-Wno-sign-compare)
endif ()


# Include directories
include_directories (${PROJECT_BINARY_DIR})

include_directories (SYSTEM ${LIBMIRAGE_INCLUDE_DIRS})
include_directories (SYSTEM ${GLIB_INCLUDE_DIRS})
include_directories (SYSTEM ${LIBXML_INCLUDE_DIRS})
include_directories (SYSTEM ${GTK_INCLUDE_DIRS})

# Link directories
link_directories (SYSTEM ${LIBMIRAGE_LIBRARY_DIRS})
link_directories (SYSTEM ${GLIB_LIBRARY_DIRS})
link_directories (SYSTEM ${LIBXML_LIBRARY_DIRS})
link_directories (SYSTEM ${GTK_LIBRARY_DIRS})

# *** image-analyzer ***
set (image-analyzer_SOURCES
    src/application.c
    src/application-window.c
    src/disc-structure-window.c
    src/disc-topology-window.c
    src/disc-tree-dump.c
    src/log-window.c
    src/main.c
    src/read-sector-window.c
    src/sector-analysis-window.c
    src/writer-dialog.c
)

add_executable (image-analyzer ${image-analyzer_SOURCES})
target_link_libraries (image-analyzer ${LIBMIRAGE_LIBRARIES} ${GLIB_LIBRARIES} ${LIBXML_LIBRARIES} ${GTK_LIBRARIES})

# Installation
install (
    TARGETS image-analyzer
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install (
    FILES ${PROJECT_SOURCE_DIR}/data/image-analyzer.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
if (POST_INSTALL_HOOKS)
    install (CODE
        "execute_process (COMMAND ${UPDATE_DESKTOP_DATABASE_EXECUTABLE} ${CMAKE_INSTALL_FULL_DATADIR}/applications)"
    )
endif ()

# *** Configuration summary ***
message(STATUS "")
message(STATUS "*** Image analyzer v.${IMAGE_ANALYZER_VERSION} configuration summary ***")
message(STATUS "Compiler and build info:")
message(STATUS " C compiler: " ${CMAKE_C_COMPILER} ${CMAKE_C_COMPILER_ARG1})
message(STATUS " C flags (Release): " ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_RELEASE})
message(STATUS " C flags (Debug): " ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_DEBUG})
message(STATUS " build type: " ${CMAKE_BUILD_TYPE})
message(STATUS "Installation:")
message(STATUS " install prefix: " ${CMAKE_INSTALL_PREFIX})
message(STATUS "Options:")
message(STATUS " run post-install hooks: " ${POST_INSTALL_HOOKS})
message(STATUS "")
