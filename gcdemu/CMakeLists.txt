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

intltool_merge ("-x" po data/apps.gcdemu.gschema.xml.in apps.gcdemu.gschema.xml)
install (
    FILES ${PROJECT_BINARY_DIR}/apps.gcdemu.gschema.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/glib-2.0/schemas
)
if (POST_INSTALL_HOOKS)
    install (CODE
        "execute_process (COMMAND ${GLIB_COMPILE_SCHEMAS_EXECUTABLE} ${CMAKE_INSTALL_FULL_DATADIR}/glib-2.0/schemas)"
    )
endif ()

intltool_merge ("-d" po data/gcdemu.desktop.in gcdemu.desktop)
intltool_merge ("-d" po data/gcdemu-indicator.desktop.in gcdemu-indicator.desktop)
install (
    FILES ${PROJECT_BINARY_DIR}/gcdemu.desktop
    ${PROJECT_BINARY_DIR}/gcdemu-indicator.desktop
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


