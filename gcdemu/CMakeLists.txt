cmake_minimum_required(VERSION 3.7)
project(gcdemu VERSION 3.2.5 LANGUAGES NONE)

# Additional CMake modules
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# CMake modules
include(GNUInstallDirs)

# Options
option(POST_INSTALL_HOOKS "Run post-install hooks" ON)

# Dependencies
find_package(IntlTool 0.21 REQUIRED)
find_package(Gettext 0.15 REQUIRED)

if(POST_INSTALL_HOOKS)
    find_program(GLIB_COMPILE_SCHEMAS_EXECUTABLE NAMES glib-compile-schemas)
    find_program(UPDATE_DESKTOP_DATABASE_EXECUTABLE NAMES update-desktop-database)
    mark_as_advanced(
        GLIB_COMPILE_SCHEMAS_EXECUTABLE
        UPDATE_DESKTOP_DATABASE_EXECUTABLE
    )
endif()

# Installation
install(PROGRAMS src/gcdemu DESTINATION ${CMAKE_INSTALL_BINDIR})

intltool_merge("-x --no-translations" ${PROJECT_SOURCE_DIR}/po ${PROJECT_SOURCE_DIR}/data/net.sf.cdemu.gcdemu.gschema.xml.in net.sf.cdemu.gcdemu.gschema.xml)
install(FILES ${PROJECT_BINARY_DIR}/net.sf.cdemu.gcdemu.gschema.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/glib-2.0/schemas)
if(POST_INSTALL_HOOKS)
    install(CODE "execute_process (COMMAND ${GLIB_COMPILE_SCHEMAS_EXECUTABLE} ${CMAKE_INSTALL_FULL_DATADIR}/glib-2.0/schemas)")
endif()

intltool_merge("-d" ${PROJECT_SOURCE_DIR}/po ${PROJECT_SOURCE_DIR}/data/gcdemu.desktop.in gcdemu.desktop)
install(FILES ${PROJECT_BINARY_DIR}/gcdemu.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
if(POST_INSTALL_HOOKS)
    install(CODE "execute_process (COMMAND ${UPDATE_DESKTOP_DATABASE_EXECUTABLE} ${CMAKE_INSTALL_FULL_DATADIR}/applications)")
endif()

install(FILES data/gcdemu-icon-gray.svg data/gcdemu-icon.svg data/gcdemu.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps)

# Translations; we use the version provided by our FindIntlTool.cmake
# instead of the gettext version... Note: first two arguments are mandatory,
# the rest is an optional list of languages to process. If no languages
# are given, all POs are processed.
intltool_process_po_files(${PROJECT_SOURCE_DIR}/po ${PROJECT_NAME})

# *** Configuration summary ***
message(STATUS "")
message(STATUS "*** gCDEmu configuration summary ***")
message(STATUS "Installation:")
message(STATUS " install prefix: " ${CMAKE_INSTALL_PREFIX})
message(STATUS "Options:")
message(STATUS " run post-install hooks: " ${POST_INSTALL_HOOKS})
message(STATUS "")
