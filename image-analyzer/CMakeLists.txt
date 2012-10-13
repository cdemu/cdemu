cmake_minimum_required (VERSION 2.8.5)

# Project name and version
project (image-analyzer C)
set (PROJECT_VERSION 1.6.0)

# Search in the `cmake' directory for additional CMake modules.
list (APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

include (CheckCSourceCompiles)
include (CheckCCompilerFlag)
include (GNUInstallDirs)

include (Utilities)
include (FileList)

mark_as_advanced (CLEAR CMAKE_INSTALL_SYSCONFDIR)
mark_as_advanced (CLEAR CMAKE_VERBOSE_MAKEFILE)

# Configuration
configure_file (
    ${PROJECT_SOURCE_DIR}/config.h.in
    ${PROJECT_BINARY_DIR}/config.h
)
include_directories (${PROJECT_BINARY_DIR})

if (NOT CMAKE_BUILD_TYPE)
    set (CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
        "Choose the type of build: None Debug Release RelWithDebInfo MinSizeRel."
        FORCE)
endif ()

list (APPEND image-analyzer_CFLAGS -std=c99)
list (APPEND image-analyzer_DEFS HAVE_CONFIG_H)

if (STRICT_WARNINGS AND CMAKE_COMPILER_IS_GNUCC)
    list (APPEND image-analyzer_CFLAGS -Wall -Wextra -Wshadow -Wmissing-declarations -Wmissing-prototypes -Wnested-externs -Wpointer-arith -Wcast-align)
    list (APPEND image-analyzer_DEFS G_DISABLE_DEPRECATED)
    check_c_compiler_flag (-Wno-strict-aliasing CC_HAVE_NSA)
    check_c_compiler_flag (-Wno-sign-compare CC_HAVE_NSC)
    if (CC_HAVE_NSA)
        list (APPEND image-analyzer_CFLAGS -Wno-strict-aliasing)
    endif ()
    if (CC_HAVE_NSC)
        list (APPEND image-analyzer_CFLAGS -Wno-sign-compare)
    endif ()
endif ()

option (GTK3_ENABLED "Compile using Gtk3" on)
option (STRICT_WARNINGS "Enable more warnings" off)

# Tests

find_package (PkgConfig 0.16 REQUIRED)
find_package (Gnuplot 4.0)

pkg_check_modules (LibMirage REQUIRED libmirage>=${PROJECT_VERSION})
pkg_check_modules (LibGlib REQUIRED glib-2.0>=2.24 gobject-2.0>=2.24)
pkg_check_modules (LibXml REQUIRED libxml-2.0>=2.0.0)

include_directories (SYSTEM ${LibMirage_INCLUDE_DIRS})

list (APPEND image-analyzer_CFLAGS ${LibMirage_CFLAGS})
list (APPEND image-analyzer_CFLAGS ${LibGlib_CFLAGS})
list (APPEND image-analyzer_CFLAGS ${LibXml_CFLAGS})

list (APPEND image-analyzer_LDFLAGS ${LibMirage_LDFLAGS})
list (APPEND image-analyzer_LDFLAGS ${LibGlib_LDFLAGS})
list (APPEND image-analyzer_LDFLAGS ${LibXml_LDFLAGS})

if (GTK3_ENABLED)
    pkg_check_modules (GTK3 REQUIRED gtk+-3.0>=3.0)
    list (APPEND image-analyzer_CFLAGS ${GTK3_CFLAGS})
    list (APPEND image-analyzer_LDFLAGS ${GTK3_LDFLAGS})
else ()
    pkg_check_modules (GTK2 REQUIRED gtk+-2.0>=2.20)
    list (APPEND image-analyzer_CFLAGS ${GTK2_CFLAGS})
    list (APPEND image-analyzer_LDFLAGS ${GTK2_LDFLAGS})
endif ()

# Source files
include_directories (${PROJECT_SOURCE_DIR}/src)

add_executable (image-analyzer ${image-analyzer_SOURCES})

target_link_libraries (image-analyzer ${LibMirage_LIBRARIES} ${LibGlib_LIBRARIES} ${LibXml_LIBRARIES})

if (GTK3_ENABLED)
    target_link_libraries (image-analyzer ${GTK3_LIBRARIES})
else ()
    target_link_libraries (image-analyzer ${GTK2_LIBRARIES})
endif ()

to_list_spaces (image-analyzer_CFLAGS image-analyzer_S_CFLAGS)
to_list_spaces (image-analyzer_LDFLAGS image-analyzer_S_LDFLAGS)

set_target_properties (image-analyzer PROPERTIES 
    COMPILE_FLAGS ${image-analyzer_S_CFLAGS}
    COMPILE_DEFINITIONS ${image-analyzer_DEFS}
    LINK_FLAGS ${image-analyzer_S_LDFLAGS}
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
