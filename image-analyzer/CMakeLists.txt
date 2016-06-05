cmake_minimum_required (VERSION 2.8.5)

# Project name
project (image-analyzer NONE)

# Additional CMake modules.
list (APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

include (GNUInstallDirs)

# Options
option (POST_INSTALL_HOOKS "Run post-install hooks" on)

# Dependencies
find_package (IntlTool 0.21 REQUIRED)
find_package (Gettext 0.15 REQUIRED)

if (POST_INSTALL_HOOKS)
    find_program (UPDATE_DESKTOP_DATABASE_EXECUTABLE NAMES update-desktop-database)
    mark_as_advanced (UPDATE_DESKTOP_DATABASE_EXECUTABLE)
endif ()

# Installation
install (
    PROGRAMS src/image-analyzer
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)

intltool_merge ("-d" po data/image-analyzer.desktop.in image-analyzer.desktop)
install (
    FILES ${PROJECT_BINARY_DIR}/image-analyzer.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
if (POST_INSTALL_HOOKS)
    install (CODE
        "execute_process (COMMAND ${UPDATE_DESKTOP_DATABASE_EXECUTABLE} ${CMAKE_INSTALL_FULL_DATADIR}/applications)"
    )
endif ()

install (
    FILES data/image-analyzer.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps
)

# Translations; we use the version provided by our FindIntlTool.cmake
# instead of the gettext version... Note: first two arguments are mandatory,
# the rest is an optional list of languages to process. If no languages
# are given, all POs are processed.
intltool_process_po_files(${PROJECT_SOURCE_DIR}/po ${PROJECT_NAME})

# *** Configuration summary ***
message(STATUS "")
message(STATUS "*** Image analyzer configuration summary ***")
message(STATUS "Installation:")
message(STATUS " install prefix: " ${CMAKE_INSTALL_PREFIX})
message(STATUS "Options:")
message(STATUS " run post-install hooks: " ${POST_INSTALL_HOOKS})
message(STATUS "")
