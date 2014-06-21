cmake_minimum_required (VERSION 2.8.5)

# Project name
project (cdemu NONE)

# Additional CMake modules.
list (APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

include (GNUInstallDirs)

# Options
option (POST_INSTALL_HOOKS "Run post-install hooks" on)

# If install prefix is /usr, override the sysconf dir to be /etc instead of /usr/etc
if ("${CMAKE_INSTALL_PREFIX}" STREQUAL "/usr")
    set(CMAKE_INSTALL_SYSCONFDIR "/etc" CACHE PATH "read-only single-machine data (/etc)" FORCE)
endif()

# Dependencies
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

# Translations; we use the version provided by our FindIntlTool.cmake
# instead of the gettext version... Note: first two arguments are mandatory,
# the rest is an optional list of languages to process. If no languages
# are given, all POs are processed.
intltool_process_po_files(${PROJECT_SOURCE_DIR}/po ${PROJECT_NAME} no sl sv)

# *** Configuration summary ***
message(STATUS "")
message(STATUS "*** CDEmu client configuration summary ***")
message(STATUS "Installation:")
message(STATUS " install prefix: " ${CMAKE_INSTALL_PREFIX})
message(STATUS "Options:")
message(STATUS " run post-install hooks: " ${POST_INSTALL_HOOKS})
message(STATUS "")
