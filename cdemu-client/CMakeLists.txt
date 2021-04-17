cmake_minimum_required(VERSION 3.7)
# NOTE: project name must be left at "cdemu" (as opposed to being changed
# to more appropriate "cdemu-client"), because several things depend on
# it (e.g., translation names).
project(cdemu VERSION 3.2.5 LANGUAGES NONE)

# Additional CMake modules
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# CMake modules
include(GNUInstallDirs)

# Options
option(POST_INSTALL_HOOKS "Run post-install hooks" ON)
option(ENABLE_BASH_COMPLETION "Install bash completion" ON)

# Dependencies
find_package(IntlTool 0.21 REQUIRED)
find_package(Gettext 0.15 REQUIRED)
find_package(PkgConfig REQUIRED)

if(POST_INSTALL_HOOKS)
    find_program(UPDATE_DESKTOP_DATABASE_EXECUTABLE NAMES update-desktop-database)
    mark_as_advanced(UPDATE_DESKTOP_DATABASE_EXECUTABLE)
endif()

# Installation
install(PROGRAMS src/cdemu DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES man/cdemu.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
install(FILES data/cdemu-client.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps)

intltool_merge("-d" ${PROJECT_SOURCE_DIR}/po ${PROJECT_SOURCE_DIR}/data/cdemu-client.desktop.in cdemu-client.desktop)
install(FILES ${PROJECT_BINARY_DIR}/cdemu-client.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

if(POST_INSTALL_HOOKS)
    install(CODE "execute_process (COMMAND ${UPDATE_DESKTOP_DATABASE_EXECUTABLE} ${CMAKE_INSTALL_FULL_DATADIR}/applications)")
endif()

# Bash-completion (optional)
if(ENABLE_BASH_COMPLETION)
    pkg_check_modules(BASH_COMPLETION bash-completion)
    if(BASH_COMPLETION_FOUND)
        pkg_get_variable(BASH_COMPLETIONSDIR bash-completion completionsdir)

        install(
            FILES data/cdemu-bash-completion.sh
            DESTINATION ${BASH_COMPLETIONSDIR}
            # the completion filename must match executable name for autoloading
            # to work (bash-completion 2.0)
            RENAME cdemu
        )
    endif()
endif()

# Translations; we use the version provided by our FindIntlTool.cmake
# instead of the gettext version... Note: first two arguments are mandatory,
# the rest is an optional list of languages to process. If no languages
# are given, all POs are processed.
intltool_process_po_files(${PROJECT_SOURCE_DIR}/po ${PROJECT_NAME})

# *** Configuration summary ***
message(STATUS "")
message(STATUS "*** CDEmu client configuration summary ***")
message(STATUS "Installation:")
message(STATUS " install prefix: " ${CMAKE_INSTALL_PREFIX})
if(ENABLE_BASH_COMPLETION AND BASH_COMPLETION_FOUND)
    message(STATUS " bash completions path: " ${BASH_COMPLETIONSDIR})
endif()
message(STATUS "Options:")
message(STATUS " enable bash completion: " ${ENABLE_BASH_COMPLETION})
message(STATUS " run post-install hooks: " ${POST_INSTALL_HOOKS})
message(STATUS "")
