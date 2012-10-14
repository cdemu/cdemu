cmake_minimum_required (VERSION 2.8.5)

# Project name and version
project (gcdemu NONE)
set (PROJECT_VERSION 1.6.0)

# Search in the `cmake' directory for additional CMake modules.
list (APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

include (GNUInstallDirs)

mark_as_advanced (CLEAR CMAKE_INSTALL_SYSCONFDIR)
mark_as_advanced (CLEAR CMAKE_VERBOSE_MAKEFILE)

mark_as_advanced (GETTEXT_MSGFMT_EXECUTABLE)
mark_as_advanced (GETTEXT_MSGMERGE_EXECUTABLE)

# Configuration

option (BUILD_NLS "Support for multiple languages" on)
option (COMPILE_SCHEMAS "Compile glib schemas" on)

# Tests

find_package (IntlTool 0.21 REQUIRED)
find_package (Gettext 0.15 REQUIRED)
find_package (PythonInterp 2.6 REQUIRED)

if (COMPILE_SCHEMAS)
    find_program (GLIB_COMPILE_SCHEMAS_EXECUTABLE NAMES glib-compile-schemas)
    mark_as_advanced (GLIB_COMPILE_SCHEMAS_EXECUTABLE)
endif ()

# Installation

install (
    PROGRAMS src/gcdemu 
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)

intltool_merge ("-x" po data/apps.gcdemu.gschema.xml.in apps.gcdemu.gschema.xml)
install (
    FILES ${PROJECT_BINARY_DIR}/apps.gcdemu.gschema.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/glib-2.0/schemas
)
if (COMPILE_SCHEMAS)
    install (CODE
        "execute_process (COMMAND ${GLIB_COMPILE_SCHEMAS_EXECUTABLE} ${CMAKE_INSTALL_FULL_DATADIR}/glib-2.0/schemas)"
    )
endif ()

intltool_merge ("-d" po data/gcdemu.desktop.in gcdemu.desktop)
install (
    FILES ${PROJECT_BINARY_DIR}/gcdemu.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

install (
    FILES data/gcdemu-icon-gray.svg data/gcdemu-icon.svg data/gcdemu.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps
)

if (BUILD_NLS AND GETTEXT_FOUND)
    gettext_process_po_files (${PROJECT_SOURCE_DIR}/po ${PROJECT_NAME})
endif ()

