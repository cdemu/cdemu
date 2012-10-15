cmake_minimum_required (VERSION 2.8.5)

# Project name and version
project (cdemu-client NONE)
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
option (POST_INSTALL_HOOKS "Run post-install hooks" on)

# Tests

find_package (IntlTool 0.21 REQUIRED)
find_package (Gettext 0.15 REQUIRED)
find_package (PythonInterp 2.6 REQUIRED)

if (POST_INSTALL_HOOKS)
    find_program (UPDATE_DESKTOP_DATABASE_EXECUTABLE NAMES update-desktop-database)
    mark_as_advanced (UPDATE_DESKTOP_DATABASE_EXECUTABLE)
endif ()

# Installation

install (
    PROGRAMS src/cdemu 
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install (
    FILES man/cdemu.1 
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
)

install (
    FILES data/cdemu-bashcomp
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/bash_completion.d
    RENAME cdemu-client
)

intltool_merge ("-d" po data/cdemu-client.desktop.in cdemu-client.desktop)
install (
    FILES ${PROJECT_BINARY_DIR}/cdemu-client.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
if (POST_INSTALL_HOOKS)
    install (CODE
        "execute_process (COMMAND ${UPDATE_DESKTOP_DATABASE_EXECUTABLE} ${CMAKE_INSTALL_FULL_DATADIR}/applications)"
    )
endif ()

if (BUILD_NLS AND GETTEXT_FOUND)
    gettext_process_po_files (${PROJECT_SOURCE_DIR}/po ${PROJECT_NAME})
endif ()

