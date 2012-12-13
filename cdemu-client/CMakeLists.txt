cmake_minimum_required (VERSION 2.8.5)

# Project name
project (cdemu NONE)

# Additional CMake modules.
list (APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

include (GNUInstallDirs)

# Options
option (BUILD_NLS "Support for multiple languages" on)
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


# i18n
if (NOT GETTEXT_MSGFMT_EXECUTABLE)
	message ("NOTE: msgfmt not found. Translations will *not* be installed!")
elseif (BUILD_NLS)
    set (catalogname ${PROJECT_NAME})

    add_custom_target (translations ALL)

    file (GLOB PO_FILES po/*.po)

    foreach (_poFile ${PO_FILES})
        get_filename_component (_poFileName ${_poFile} NAME)
        string (REGEX REPLACE "^${catalogname}_?" "" _langCode ${_poFileName} )
        string (REGEX REPLACE "\\.po$" "" _langCode ${_langCode} )

        if ( _langCode )
            get_filename_component (_lang ${_poFile} NAME_WE)
            set (_gmoFile ${CMAKE_CURRENT_BINARY_DIR}/${_lang}.gmo)

            add_custom_command (TARGET translations
                                COMMAND ${GETTEXT_MSGFMT_EXECUTABLE} --check -o ${_gmoFile} ${_poFile}
                                DEPENDS ${_poFile})
            install (FILES ${_gmoFile} DESTINATION ${CMAKE_INSTALL_LOCALEDIR}/${_langCode}/LC_MESSAGES/ RENAME ${catalogname}.mo)
        endif ()
    endforeach()
endif()
