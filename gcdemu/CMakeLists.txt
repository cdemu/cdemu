cmake_minimum_required (VERSION 2.8.5)

# Project name
project (gcdemu NONE)

# Additional CMake modules.
list (APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

include (GNUInstallDirs)

# Options
option (BUILD_NLS "Support for multiple languages" on)
option (POST_INSTALL_HOOKS "Run post-install hooks" on)

# Dependencies
find_package (IntlTool 0.21 REQUIRED)
find_package (Gettext 0.15 REQUIRED)
find_package (PythonInterp 2.6 REQUIRED)

if (POST_INSTALL_HOOKS)
    find_program (GLIB_COMPILE_SCHEMAS_EXECUTABLE NAMES glib-compile-schemas)
    find_program (UPDATE_DESKTOP_DATABASE_EXECUTABLE NAMES update-desktop-database)
    mark_as_advanced (
        GLIB_COMPILE_SCHEMAS_EXECUTABLE
        UPDATE_DESKTOP_DATABASE_EXECUTABLE
    )
endif ()

# Installation

install (
    PROGRAMS src/gcdemu
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)

intltool_merge ("-x" po data/net.sf.cdemu.gcdemu.gschema.xml.in net.sf.cdemu.gcdemu.gschema.xml)
install (
    FILES ${PROJECT_BINARY_DIR}/net.sf.cdemu.gcdemu.gschema.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/glib-2.0/schemas
)
if (POST_INSTALL_HOOKS)
    install (CODE
        "execute_process (COMMAND ${GLIB_COMPILE_SCHEMAS_EXECUTABLE} ${CMAKE_INSTALL_FULL_DATADIR}/glib-2.0/schemas)"
    )
endif ()

intltool_merge ("-d" po data/gcdemu.desktop.in gcdemu.desktop)
install (
    FILES ${PROJECT_BINARY_DIR}/gcdemu.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
if (POST_INSTALL_HOOKS)
    install (CODE
        "execute_process (COMMAND ${UPDATE_DESKTOP_DATABASE_EXECUTABLE} ${CMAKE_INSTALL_FULL_DATADIR}/applications)"
    )
endif ()

install (
    FILES data/gcdemu-icon-gray.svg data/gcdemu-icon.svg data/gcdemu.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps
)

if (BUILD_NLS AND GETTEXT_FOUND)
    # We use the version provided by our FindIntlTool.cmake instead of the gettext version...
    intltool_process_po_files(${PROJECT_SOURCE_DIR}/po ${PROJECT_NAME})
endif ()

