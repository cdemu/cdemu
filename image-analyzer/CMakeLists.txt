cmake_minimum_required (VERSION 2.8.5)

# Project name
project (image-analyzer C)

# Versioning
set (IMAGE_ANALYZER_VERSION 1.6.0)

# Additional CMake modules.
list (APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

include (CheckCSourceCompiles)
include (CheckCCompilerFlag)
include (GNUInstallDirs)

include (Utilities)
include (FileList)

# Options
option (GTK3_ENABLED "Compile using Gtk3" on)
option (POST_INSTALL_HOOKS "Run post-install hooks" on)

# Dependencies
find_package (PkgConfig 0.16 REQUIRED)
find_package (Gnuplot 4.0)

pkg_check_modules (LIBMIRAGE REQUIRED libmirage>=1.6.0)
pkg_check_modules (GLIB REQUIRED glib-2.0>=2.24 gobject-2.0>=2.24)
pkg_check_modules (LIBXML REQUIRED libxml-2.0>=2.0.0)

if (GTK3_ENABLED)
    pkg_check_modules (GTK REQUIRED gtk+-3.0>=3.0)
else ()
    pkg_check_modules (GTK REQUIRED gtk+-2.0>=2.20)
endif ()

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
add_definitions (-DHAVE_CONFIG_H -DG_DISABLE_DEPRECATED)

if (CMAKE_COMPILER_IS_GNUCC)
    add_definitions (-Wall -Wextra -Wshadow -Wmissing-declarations -Wmissing-prototypes -Wnested-externs -Wpointer-arith -Wcast-align)
    add_definitions (-Wno-sign-compare)
endif ()


# Include directories
include_directories (${PROJECT_BINARY_DIR})
include_directories (SYSTEM ${LIBMIRAGE_INCLUDE_DIRS})

# *** image-analyzer ***
add_executable (image-analyzer ${image-analyzer_SOURCES})
target_link_libraries (image-analyzer ${LIBMIRAGE_LIBRARIES} ${GLIB_LIBRARIES} ${LIBXML_LIBRARIES} ${GTK_LIBRARIES})

list (APPEND image_analyzer_CFLAGS ${LIBMIRAGE_CFLAGS} ${GLIB_CFLAGS} ${LIBXML_CFLAGS} ${GTK_CFLAGS})
list (APPEND image_analyzer_LDFLAGS ${LIBMIRAGE_LDFLAGS} ${GLIB_LDFLAGS} ${LIBXML_LDFLAGS} ${GTK_LDFLAGS})

to_list_spaces (image_analyzer_CFLAGS image_analyzer_CFLAGS_STR)
to_list_spaces (image_analyzer_LDFLAGS image_analyzer_LDFLAGS_STR)

set_target_properties (image-analyzer PROPERTIES
    COMPILE_FLAGS "${image_analyzer_CFLAGS_STR}"
    COMPILE_DEFINITIONS "${image_analyzer_DEFS}"
    LINK_FLAGS "${image_analyzer_LDFLAGS_STR}"
)

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

